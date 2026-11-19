/*!
 * \file tls_test_harness.h
 *
 * Shared by the TLS tests: a listening CNetwork_Asio with its accepted peers,
 * certificates and configurations, and ROSE-framed test packets. One test
 * binary per file (see CMakeLists.txt), so each includes this once.
 */
#pragma once

#include "gtest/gtest.h"

#include "cnetwork_asio.h"
#include "network_thread_pool.h"
#include "tls_test_certs.h"

#include <asio.hpp>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
