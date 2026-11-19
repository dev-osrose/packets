/*!
 * \file test_tls_full_duplex.cpp
 *
 * Reading and writing one TLS session at the same time, the way a game client
 * does: the pool's strand runs the read loop while the game's own thread calls
 * send_data(). asio's ssl::stream is not thread-safe, so a write started off
 * the strand races the read inside the TLS engine, and a packet can be lost
 * or the session broken. The game client saw it as a login that never left:
 * TLS up, then silence.
 */

#include "gtest/gtest.h"

#ifdef USE_SSL

#include "tls_test_harness.h"

#include <atomic>
#include <cstdint>
#include <thread>

namespace {

constexpr int kPackets = 4000;
constexpr uint16_t kFromClient = 0x0101;
constexpr uint16_t kFromServer = 0x0202;

/// A read loop for ROSE framing over `_net`: the 6-byte header first, then the
/// rest of the packet; `_on_packet` gets each whole packet's command and payload.
template <typename F>
void ReadPackets(Core::INetwork& _net, F _on_packet) {
  _net.registerOnReceived([&_net, _on_packet](uint16_t, uint16_t& _size, uint8_t* _buffer) {
    uint16_t size = 0;
    std::memcpy(&size, _buffer, sizeof(size));
    if (_size == 6 && size > 6) {
      _size = size;  // the header said how much follows
      return true;
    }
    uint16_t command = 0, payload = 0;
    std::memcpy(&command, _buffer + 2, sizeof(command));
    std::memcpy(&payload, _buffer + 6, sizeof(payload));
    _on_packet(command, payload);
    _net.reset_internal_buffer();
    return true;
  });
  _net.recv_data();
}

}  // namespace

// The server streams packets at the client (so the client's read loop is busy
// inside the TLS engine) while this thread sends: every packet has to arrive,
// in order, and the session has to stay up.
TEST(TlsFullDuplex, SendingFromAnotherThreadWhileReadingLosesNothing) {
  TlsListener listener;
  ASSERT_TRUE(listener.Start(ServerConfig()));

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(ClientConfig()));
  ASSERT_TRUE(client.connect_and_wait(kHandshakeTimeout));
  ASSERT_TRUE(listener.accepted().WaitFor(1, kHandshakeTimeout));
  Core::INetwork* peer = listener.accepted().peer(0);
  ASSERT_NE(nullptr, peer);

  std::atomic_int at_server{0}, out_of_order{0}, at_client{0};
  ReadPackets(*peer, [&](uint16_t _command, uint16_t _payload) {
    if (_command != kFromClient) return;
    const int n = at_server.load();
    if (_payload != static_cast<uint16_t>(n)) ++out_of_order;
    at_server.store(n + 1);
  });
  ReadPackets(client, [&](uint16_t, uint16_t) { ++at_client; });

  std::atomic_bool stop{false};
  std::thread server_stream([&] {
    for (int i = 0; !stop && i < kPackets * 4; ++i) {
      peer->send_data(MakePacket(kFromServer, static_cast<uint16_t>(i)));
      if (i % 64 == 0) std::this_thread::yield();
    }
  });

  for (int i = 0; i < kPackets; ++i) {
    ASSERT_TRUE(client.send_data(MakePacket(kFromClient, static_cast<uint16_t>(i))));
    if (i % 64 == 0) std::this_thread::yield();
  }

  const auto deadline = std::chrono::steady_clock::now() + 20s;
  while (at_server < kPackets && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(10ms);
  stop = true;
  server_stream.join();

  EXPECT_EQ(kPackets, at_server.load()) << "packets sent while the client was reading never arrived";
  EXPECT_EQ(0, out_of_order.load());
  EXPECT_GT(at_client.load(), 0) << "the client read nothing, so the race was not exercised";
  EXPECT_TRUE(client.is_active()) << "the session broke";

  client.shutdown(true);
  DrainNetwork(500ms);
}

#else  // !USE_SSL

TEST(TlsFullDuplex, SkippedInAPlaintextBuild) {
  GTEST_SKIP() << "built without ENABLE_SSL; there is no TLS session to test";
}

#endif  // USE_SSL
