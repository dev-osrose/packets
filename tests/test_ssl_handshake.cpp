// Copyright 2016 Chirstopher Torres (Raven), L3nn0x
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http ://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*!
 * \file test_ssl_handshake.cpp
 *
 * Real TLS over loopback: one CNetwork_Asio listening, another dialling it,
 * with certificates minted at runtime.  Everything here goes through
 * connect_and_wait(), which exists precisely so a caller can get a definite
 * answer instead of racing the asynchronous handshake.
 *
 * The negatives matter more than the positives.  A configuration that fails to
 * authenticate the peer still produces a working, encrypted connection, so
 * "it connected" proves nothing on its own - each success below is paired with
 * the case that has to fail.
 */

#include "gtest/gtest.h"

#include "cnetwork_asio.h"
#include "network_thread_pool.h"
#include "tls_test_certs.h"

#ifdef USE_SSL

#include <asio.hpp>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace {

using tls_test::TestCertificates;

constexpr auto kHandshakeTimeout = 10s;
/// How long to wait before concluding that something is never going to happen.
constexpr auto kNegativeTimeout = 3s;

/*!
 * \brief Let the pool run the completion handlers shutdown() just cancelled,
 *        before the objects those handlers reference are destroyed.
 *
 * Not politeness - a workaround.  CNetwork_Asio's asynchronous handlers capture
 * a raw `this` and are not tied to the object's lifetime, so shutdown(true)
 * closing a socket only *schedules* the cancelled operations' handlers; the
 * destructor then returns while they are still queued.  The accept loop's
 * re-arm at cnetwork_asio.cpp:635 (`if (is_active()) AcceptConnection();`)
 * makes that a virtual call on a destroyed object, which aborts with
 * "pure virtual method called" perhaps one run in three.
 *
 * The servers only survive the same teardown because main() sleeps for a
 * second before the listeners leave scope.  This does the same thing, less
 * hopefully.  Remove it once CNetwork_Asio owns its handlers' lifetime
 * (shared_from_this, or an outstanding-operation count the destructor waits
 * on).
 */
void DrainNetwork(std::chrono::milliseconds _for = 250ms) {
  std::this_thread::sleep_for(_for);
}

class NetworkEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { Core::NetworkThreadPool::GetInstance(); }
  void TearDown() override { Core::NetworkThreadPool::DeleteInstance(); }
};

const auto* const kEnvironment =
    ::testing::AddGlobalTestEnvironment(new NetworkEnvironment);

/// Resolve the loopback name the certificates are issued for. Both ends go
/// through the same resolution, so they agree on the family even on a host
/// where "localhost" is IPv6 first.
asio::ip::tcp::endpoint LoopbackEndpoint() {
  asio::io_context io;
  asio::ip::tcp::resolver resolver(io);
  auto results = resolver.resolve("localhost", "0");
  return results.begin()->endpoint();
}

uint16_t FreePort() {
  asio::io_context io;
  asio::ip::tcp::acceptor probe(io, LoopbackEndpoint());
  const uint16_t port = probe.local_endpoint().port();
  probe.close();
  return port;
}

/// Collects the peers a listener accepts. A promise is not enough: the
/// reconnect test accepts twice, and OnAccepted fires on a pool thread.
class AcceptQueue {
 public:
  void Push(std::unique_ptr<Core::INetwork> _peer) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      peers_.push_back(std::move(_peer));
      ++count_;
    }
    cv_.notify_all();
  }

  /// \return false if \a _n peers had not arrived before the timeout.
  bool WaitFor(std::size_t _n, std::chrono::milliseconds _timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, _timeout, [&] { return count_ >= _n; });
  }

  std::size_t count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
  }

  Core::INetwork* peer(std::size_t _index) {
    std::lock_guard<std::mutex> lock(mutex_);
    return _index < peers_.size() ? peers_[_index].get() : nullptr;
  }

  /// Cancel every peer's I/O without destroying it yet, so the handlers have
  /// somewhere valid to land while DrainNetwork() runs.
  void ShutdownAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& peer : peers_) peer->shutdown(true);
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    peers_.clear();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::unique_ptr<Core::INetwork>> peers_;
  std::size_t count_ = 0;
};

/// A listening CNetwork_Asio plus the peers it accepted, torn down in the right
/// order: peers first, then the listener, all before the pool goes away.
class TlsListener {
 public:
  TlsListener() : port_(FreePort()) {
    server_.registerOnAccepted(
        [this](std::unique_ptr<Core::INetwork> _peer) { accepted_.Push(std::move(_peer)); });
  }

  // Teardown order matters, see DrainNetwork(): cancel all I/O first, let the
  // cancelled handlers run, and only then destroy anything they reference.
  ~TlsListener() {
    accepted_.ShutdownAll();
    server_.shutdown(true);
    DrainNetwork();
    accepted_.Clear();
  }

  /// \return whether the listener came up; a false here is a failed
  ///         enable_ssl_server(), not a failed bind.
  bool Start(const Core::SslServerConfig& _cfg) {
    if (!server_.init("localhost", port_)) return false;
    if (!server_.enable_ssl_server(_cfg)) return false;
    return server_.listen();
  }

  uint16_t port() const { return port_; }
  AcceptQueue& accepted() { return accepted_; }

 private:
  Core::CNetwork_Asio server_;
  AcceptQueue accepted_;
  uint16_t port_;
};

Core::SslServerConfig ServerConfig() {
  const auto& certs = TestCertificates::Get();
  Core::SslServerConfig cfg;
  cfg.certificate_chain_file = certs.server().certificate_file;
  cfg.private_key_file = certs.server().private_key_file;
  return cfg;
}

/// What a correctly configured deployment hands its ISC clients: verification
/// on, against the deployment's own CA.
Core::SslClientConfig ClientConfig() {
  const auto& certs = TestCertificates::Get();
  Core::SslClientConfig cfg;
  cfg.ca_file = certs.ca().certificate_file;
  return cfg;
}

/// Builds a ROSE-framed packet: uint16 size, uint16 command, then padding out
/// to the 6-byte header the receiver reads first.
std::unique_ptr<uint8_t[]> MakePacket(uint16_t _command, uint16_t _payload) {
  auto buffer = std::make_unique<uint8_t[]>(8);
  const uint16_t size = 8;
  std::memcpy(&buffer[0], &size, sizeof(size));
  std::memcpy(&buffer[2], &_command, sizeof(_command));
  const uint16_t reserved = 0;
  std::memcpy(&buffer[4], &reserved, sizeof(reserved));
  std::memcpy(&buffer[6], &_payload, sizeof(_payload));
  return buffer;
}

}  // namespace

// ---------------------------------------------------------------------------
// The happy path, and the case it has to be distinguishable from
// ---------------------------------------------------------------------------

// The shape a real deployment has: a private CA, a server certificate issued by
// it, and clients pointed at that CA via ssl.caFile.
TEST(TlsHandshake, SucceedsAgainstAPrivateCa) {
  TlsListener listener;
  ASSERT_TRUE(listener.Start(ServerConfig()));

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(ClientConfig()));

  EXPECT_TRUE(client.connect_and_wait(kHandshakeTimeout));
  EXPECT_TRUE(client.is_active());
  EXPECT_TRUE(listener.accepted().WaitFor(1, kHandshakeTimeout))
      << "the server never surfaced the connection through OnAccepted";

  client.shutdown(true);
  DrainNetwork();
}

// The certificate is valid and correctly named, but issued by a CA the client
// does not trust. Without this, SucceedsAgainstAPrivateCa above would pass just
// as happily with verification broken.
TEST(TlsHandshake, FailsWhenThePeerCertificateIsNotTrusted) {
  const auto& certs = TestCertificates::Get();

  TlsListener listener;
  ASSERT_TRUE(listener.Start(ServerConfig()));

  Core::SslClientConfig cfg;
  cfg.ca_file = certs.foreign_ca().certificate_file;  // right shape, wrong CA

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(cfg));

  // The callback is the only way a caller of the asynchronous connect() ever
  // learns the handshake failed, so a silent failure here would strand every
  // reconnect loop in the servers.
  std::promise<void> disconnected;
  auto disconnected_future = disconnected.get_future();
  std::atomic_bool fired{false};
  client.registerOnDisconnected([&] {
    if (!fired.exchange(true)) disconnected.set_value();
  });

  EXPECT_FALSE(client.connect_and_wait(kHandshakeTimeout));
  EXPECT_FALSE(client.is_active());
  EXPECT_EQ(std::future_status::ready, disconnected_future.wait_for(kNegativeTimeout))
      << "OnDisconnected never fired for a failed handshake";

  // A dropped handshake must not reach the application as a connection.
  EXPECT_EQ(0u, listener.accepted().count());

  DrainNetwork();
}

// Turning verification off leaves the connection encrypted but
// unauthenticated. Pinned because it is the documented escape hatch, and
// because a change that broke it would push deployments towards worse
// workarounds.
TEST(TlsHandshake, VerifyPeerFalseStillCompletesTheHandshake) {
  TlsListener listener;
  ASSERT_TRUE(listener.Start(ServerConfig()));

  Core::SslClientConfig cfg;
  cfg.verify_peer = false;

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(cfg));

  EXPECT_TRUE(client.connect_and_wait(kHandshakeTimeout));
  EXPECT_TRUE(listener.accepted().WaitFor(1, kHandshakeTimeout));

  client.shutdown(true);
  DrainNetwork();
}

// ---------------------------------------------------------------------------
// Hostname verification
// ---------------------------------------------------------------------------

// A certificate issued by the trusted CA but for a different name. Chain
// validity alone is not identity: without the hostname check, any host holding
// any certificate from the same CA could impersonate the char server.
TEST(TlsHandshake, RejectsACertificateIssuedForAnotherHostname) {
  const auto& certs = TestCertificates::Get();

  Core::SslServerConfig server_cfg;
  server_cfg.certificate_chain_file = certs.other_host().certificate_file;
  server_cfg.private_key_file = certs.other_host().private_key_file;

  TlsListener listener;
  ASSERT_TRUE(listener.Start(server_cfg));

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(ClientConfig()));

  EXPECT_FALSE(client.connect_and_wait(kHandshakeTimeout));
  EXPECT_EQ(0u, listener.accepted().count());

  DrainNetwork();
}

// The ssl.sniHostname override, end to end: the certificate names
// other.invalid, the host dialled is localhost, and naming it explicitly is
// what makes the two agree. This is the deployment that fronts several
// services with one certificate.
TEST(TlsHandshake, AnExplicitSniHostnameIsWhatGetsVerified) {
  const auto& certs = TestCertificates::Get();

  Core::SslServerConfig server_cfg;
  server_cfg.certificate_chain_file = certs.other_host().certificate_file;
  server_cfg.private_key_file = certs.other_host().private_key_file;

  TlsListener listener;
  ASSERT_TRUE(listener.Start(server_cfg));

  auto cfg = ClientConfig();
  cfg.sni_hostname = "other.invalid";

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(cfg));

  EXPECT_TRUE(client.connect_and_wait(kHandshakeTimeout));
  EXPECT_TRUE(listener.accepted().WaitFor(1, kHandshakeTimeout));

  client.shutdown(true);
  DrainNetwork();
}

// ---------------------------------------------------------------------------
// Mutual TLS
// ---------------------------------------------------------------------------

TEST(TlsHandshake, MutualTlsSucceedsWhenTheClientPresentsACertificate) {
  const auto& certs = TestCertificates::Get();

  auto server_cfg = ServerConfig();
  server_cfg.require_client_cert = true;
  server_cfg.client_ca_file = certs.ca().certificate_file;

  TlsListener listener;
  ASSERT_TRUE(listener.Start(server_cfg));

  auto cfg = ClientConfig();
  cfg.certificate_chain_file = certs.client().certificate_file;
  cfg.private_key_file = certs.client().private_key_file;

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(cfg));

  EXPECT_TRUE(client.connect_and_wait(kHandshakeTimeout));
  EXPECT_TRUE(listener.accepted().WaitFor(1, kHandshakeTimeout));

  client.shutdown(true);
  DrainNetwork();
}

// require_client_cert has to actually require one. A server that asked for a
// certificate and accepted its absence would be mTLS in the configuration file
// only.
TEST(TlsHandshake, MutualTlsRejectsAClientWithoutACertificate) {
  const auto& certs = TestCertificates::Get();

  auto server_cfg = ServerConfig();
  server_cfg.require_client_cert = true;
  server_cfg.client_ca_file = certs.ca().certificate_file;

  TlsListener listener;
  ASSERT_TRUE(listener.Start(server_cfg));

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(ClientConfig()));  // no client certificate

  // TLS 1.3 sends the client's Finished before the server has verified the
  // certificate, so the client's own connect_and_wait() may well report
  // success. The server rejecting it is what matters, and the server never
  // surfaces the connection.
  client.connect_and_wait(kHandshakeTimeout);
  EXPECT_FALSE(listener.accepted().WaitFor(1, kNegativeTimeout))
      << "a client with no certificate reached OnAccepted";

  client.shutdown(true);
  DrainNetwork();
}

// A client certificate the server's CA did not issue.
TEST(TlsHandshake, MutualTlsRejectsAClientCertificateFromAnotherCa) {
  const auto& certs = TestCertificates::Get();

  auto server_cfg = ServerConfig();
  server_cfg.require_client_cert = true;
  server_cfg.client_ca_file = certs.ca().certificate_file;

  TlsListener listener;
  ASSERT_TRUE(listener.Start(server_cfg));

  auto cfg = ClientConfig();
  cfg.certificate_chain_file = certs.foreign_server().certificate_file;
  cfg.private_key_file = certs.foreign_server().private_key_file;

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(cfg));

  client.connect_and_wait(kHandshakeTimeout);
  EXPECT_FALSE(listener.accepted().WaitFor(1, kNegativeTimeout))
      << "a client certificate from an untrusted CA reached OnAccepted";

  client.shutdown(true);
  DrainNetwork();
}

// ---------------------------------------------------------------------------
// The handshake timeout
// ---------------------------------------------------------------------------

// A peer that connects and then says nothing holds a socket, an SSL object and
// a CNetwork_Asio open indefinitely without the timer - which is a free
// resource-exhaustion route against a public game port. The only coverage of
// StartServerHandshake's timer and its `finished` interlock.
TEST(TlsHandshake, ASilentPeerIsDroppedWhenTheHandshakeTimesOut) {
  auto server_cfg = ServerConfig();
  server_cfg.handshake_timeout_seconds = 1;

  TlsListener listener;
  ASSERT_TRUE(listener.Start(server_cfg));

  // A plain TCP socket: connect, then send nothing at all.
  asio::io_context io;
  asio::ip::tcp::socket silent(io);
  auto endpoint = LoopbackEndpoint();
  endpoint.port(listener.port());
  std::error_code ec;
  silent.connect(endpoint, ec);
  ASSERT_FALSE(ec) << "could not reach the listener: " << ec.message();

  EXPECT_FALSE(listener.accepted().WaitFor(1, kNegativeTimeout))
      << "a peer that never handshook was surfaced to the application";

  // The server closed it rather than leaving it parked.
  uint8_t byte = 0;
  const std::size_t read = silent.read_some(asio::buffer(&byte, 1), ec);
  EXPECT_EQ(0u, read);
  EXPECT_TRUE(ec) << "the server left the connection open after the timeout";

  DrainNetwork();
}

// ---------------------------------------------------------------------------
// Data over the established session
// ---------------------------------------------------------------------------

TEST(TlsHandshake, DataRoundTripsOverTheEncryptedSession) {
  TlsListener listener;
  ASSERT_TRUE(listener.Start(ServerConfig()));

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(ClientConfig()));
  ASSERT_TRUE(client.connect_and_wait(kHandshakeTimeout));
  ASSERT_TRUE(listener.accepted().WaitFor(1, kHandshakeTimeout));

  Core::INetwork* peer = listener.accepted().peer(0);
  ASSERT_NE(nullptr, peer);

  std::promise<uint16_t> received;
  auto received_future = received.get_future();
  std::atomic_bool fired{false};
  peer->registerOnReceived([&](uint16_t, uint16_t&, uint8_t* _buffer) {
    uint16_t command = 0;
    std::memcpy(&command, _buffer + 2, sizeof(command));
    if (!fired.exchange(true)) received.set_value(command);
    // false ends the read loop, which is what this test wants: the framing
    // beyond the 6-byte header is the packet layer's business, not the
    // transport's.
    return false;
  });
  peer->recv_data();

  ASSERT_TRUE(client.send_data(MakePacket(0x7ab1, 0x1234)));

  ASSERT_EQ(std::future_status::ready, received_future.wait_for(kHandshakeTimeout))
      << "nothing arrived over the TLS session";
  EXPECT_EQ(0x7ab1, received_future.get());

  client.shutdown(true);
  DrainNetwork();
}

// send_data() before the socket is active leaves the packet on a queue that
// ProcessSend() deliberately refuses to pump. DrainPendingSends() is what
// releases it once the handshake lands; without it the packets sit there until
// some later send happens to re-arm the pump.
TEST(TlsHandshake, PacketsQueuedBeforeTheHandshakeAreFlushedAfterIt) {
  TlsListener listener;
  ASSERT_TRUE(listener.Start(ServerConfig()));

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(ClientConfig()));

  // Queued while the socket is definitely inactive - there is not even a TCP
  // connection yet, so this cannot race the handshake.
  ASSERT_FALSE(client.is_active());
  ASSERT_TRUE(client.send_data(MakePacket(0x0042, 0x1234)));

  ASSERT_TRUE(client.connect_and_wait(kHandshakeTimeout));
  ASSERT_TRUE(listener.accepted().WaitFor(1, kHandshakeTimeout));

  Core::INetwork* peer = listener.accepted().peer(0);
  ASSERT_NE(nullptr, peer);

  std::promise<uint16_t> received;
  auto received_future = received.get_future();
  std::atomic_bool fired{false};
  peer->registerOnReceived([&](uint16_t, uint16_t&, uint8_t* _buffer) {
    uint16_t command = 0;
    std::memcpy(&command, _buffer + 2, sizeof(command));
    if (!fired.exchange(true)) received.set_value(command);
    return false;
  });
  peer->recv_data();

  ASSERT_EQ(std::future_status::ready, received_future.wait_for(kHandshakeTimeout))
      << "the packet queued before the handshake was never flushed";
  EXPECT_EQ(0x0042, received_future.get());

  client.shutdown(true);
  DrainNetwork();
}

// ---------------------------------------------------------------------------
// Session lifetime
// ---------------------------------------------------------------------------

// An SSL object is spent once it has been shut down, so reusing the stream
// after a disconnect fails the next handshake. connect_impl() rebuilds it;
// this is what says so.
TEST(TlsHandshake, ConnectingAgainAfterADisconnectRebuildsTheTlsEngine) {
  TlsListener listener;
  ASSERT_TRUE(listener.Start(ServerConfig()));

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(ClientConfig()));

  ASSERT_TRUE(client.connect_and_wait(kHandshakeTimeout));
  ASSERT_TRUE(listener.accepted().WaitFor(1, kHandshakeTimeout));

  ASSERT_TRUE(client.disconnect());

  EXPECT_TRUE(client.connect_and_wait(kHandshakeTimeout))
      << "the second handshake reused a spent TLS engine";
  EXPECT_TRUE(listener.accepted().WaitFor(2, kHandshakeTimeout));

  client.shutdown(true);
  DrainNetwork();
}

// The other half of enable_ssl_client()'s guard: rebinding the stream to a new
// context once the transport is open would drop TLS on a live session.
// test_ssl_config.cpp covers the is_active() half without a connection.
TEST(TlsHandshake, EnableSslClientIsRefusedOnAConnectedSocket) {
  TlsListener listener;
  ASSERT_TRUE(listener.Start(ServerConfig()));

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  ASSERT_TRUE(client.enable_ssl_client(ClientConfig()));
  ASSERT_TRUE(client.connect_and_wait(kHandshakeTimeout));

  EXPECT_FALSE(client.enable_ssl_client(ClientConfig()));

  client.shutdown(true);
  DrainNetwork();
}

// A caller that never touched enable_ssl_client() still gets a verifying
// client, because EnsureClientContext() applies SslClientConfig's defaults -
// which trust the OS store, not our test CA. So this connection must fail, and
// the reason it fails is the whole point: the default is secure.
TEST(TlsHandshake, AnUnconfiguredClientStillVerifiesAndSoRejectsAPrivateCa) {
  TlsListener listener;
  ASSERT_TRUE(listener.Start(ServerConfig()));

  Core::CNetwork_Asio client;
  ASSERT_TRUE(client.init("localhost", listener.port()));
  // deliberately no enable_ssl_client()

  EXPECT_FALSE(client.connect_and_wait(kHandshakeTimeout));
  EXPECT_EQ(0u, listener.accepted().count());

  DrainNetwork();
}

#else  // !USE_SSL

TEST(TlsHandshake, SkippedInAPlaintextBuild) {
  GTEST_SKIP() << "built without ENABLE_SSL; there is no TLS handshake to test";
}

#endif  // USE_SSL
