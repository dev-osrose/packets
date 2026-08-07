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
 * \file test_ssl_config.cpp
 *
 * Validation half of the TLS tests: what enable_ssl_server() and
 * enable_ssl_client() accept and reject, and the rule that a TLS build's
 * listen() fails closed.  No handshakes here - see test_ssl_handshake.cpp.
 *
 * These are the failure modes a deployment actually hits: a mistyped path, a
 * key that needs a password, mTLS configured with nothing to verify against.
 * Every one of them has to be a loud false rather than a server that comes up
 * anyway.
 */

#include "gtest/gtest.h"

#include "cnetwork_asio.h"
#include "network_thread_pool.h"
#include "tls_test_certs.h"

#ifdef USE_SSL

#include <asio.hpp>

#include <chrono>
#include <string>
#include <thread>

namespace {

using tls_test::TestCertificates;

/// See the long note on DrainNetwork() in test_ssl_handshake.cpp: closing a
/// socket only schedules the cancelled operations' handlers, and they capture a
/// raw `this` that the destructor is about to invalidate.
void DrainNetwork() { std::this_thread::sleep_for(std::chrono::milliseconds(250)); }

// CNetwork_Asio's constructor takes the io_context off NetworkThreadPool, and
// DeleteInstance() is only safe once every socket built against it is gone.
// Standing the pool up for the whole process and tearing it down at the end is
// what keeps that ordering true regardless of test order.
class NetworkEnvironment : public ::testing::Environment {
 public:
  void SetUp() override { Core::NetworkThreadPool::GetInstance(); }
  void TearDown() override { Core::NetworkThreadPool::DeleteInstance(); }
};

const auto* const kEnvironment =
    ::testing::AddGlobalTestEnvironment(new NetworkEnvironment);

/// A TCP port nothing is listening on, obtained by binding and immediately
/// releasing one. Racy in principle; in practice the kernel does not hand the
/// same ephemeral port back this quickly, and nothing here ever accepts.
uint16_t FreePort() {
  asio::io_context io;
  asio::ip::tcp::acceptor probe(io, asio::ip::tcp::endpoint(
                                        asio::ip::make_address("127.0.0.1"), 0));
  const uint16_t port = probe.local_endpoint().port();
  probe.close();
  return port;
}

/// The configuration a correct deployment would have.
Core::SslServerConfig GoodServerConfig() {
  const auto& certs = TestCertificates::Get();
  Core::SslServerConfig cfg;
  cfg.certificate_chain_file = certs.server().certificate_file;
  cfg.private_key_file = certs.server().private_key_file;
  return cfg;
}

}  // namespace

// ---------------------------------------------------------------------------
// listen() fails closed
// ---------------------------------------------------------------------------

// The single most important behaviour in the whole change. A TLS build has no
// plaintext fallback: a listener that came up anyway because a certificate path
// was mistyped would serve the game in the clear while every log line and every
// config file said otherwise.
TEST(SslServerConfig, ListenFailsWithoutEnableSslServer) {
  Core::CNetwork_Asio socket;
  ASSERT_TRUE(socket.init("127.0.0.1", FreePort()));

  EXPECT_FALSE(socket.listen());
  EXPECT_FALSE(socket.is_active()) << "a failed listen() must not leave the socket active";
}

TEST(SslServerConfig, ListenSucceedsOnceTheServerContextIsInstalled) {
  Core::CNetwork_Asio socket;
  ASSERT_TRUE(socket.init("127.0.0.1", FreePort()));
  ASSERT_TRUE(socket.enable_ssl_server(GoodServerConfig()));

  EXPECT_TRUE(socket.listen());
  EXPECT_TRUE(socket.is_active());

  socket.shutdown(true);
  DrainNetwork();
}

// ---------------------------------------------------------------------------
// enable_ssl_server
// ---------------------------------------------------------------------------

TEST(SslServerConfig, AcceptsAValidCertificateAndKey) {
  Core::CNetwork_Asio socket;
  EXPECT_TRUE(socket.enable_ssl_server(GoodServerConfig()));
}

TEST(SslServerConfig, RejectsAnEmptyCertificateOrKeyPath) {
  const auto& certs = TestCertificates::Get();
  Core::CNetwork_Asio socket;

  Core::SslServerConfig no_cert;
  no_cert.private_key_file = certs.server().private_key_file;
  EXPECT_FALSE(socket.enable_ssl_server(no_cert));

  Core::SslServerConfig no_key;
  no_key.certificate_chain_file = certs.server().certificate_file;
  EXPECT_FALSE(socket.enable_ssl_server(no_key));

  EXPECT_FALSE(socket.enable_ssl_server(Core::SslServerConfig{}));
}

// A mistyped path is the likeliest real-world failure, and the one where
// failing closed matters most.
TEST(SslServerConfig, RejectsAMissingCertificateFile) {
  const auto& certs = TestCertificates::Get();
  auto cfg = GoodServerConfig();
  cfg.certificate_chain_file = certs.missing_file();

  Core::CNetwork_Asio socket;
  EXPECT_FALSE(socket.enable_ssl_server(cfg));
}

TEST(SslServerConfig, RejectsAMissingPrivateKeyFile) {
  const auto& certs = TestCertificates::Get();
  auto cfg = GoodServerConfig();
  cfg.private_key_file = certs.missing_file();

  Core::CNetwork_Asio socket;
  EXPECT_FALSE(socket.enable_ssl_server(cfg));
}

// A file that exists but is not PEM is a different code path inside OpenSSL
// from a file that does not exist.
TEST(SslServerConfig, RejectsACertificateFileThatIsNotPem) {
  const auto& certs = TestCertificates::Get();
  auto cfg = GoodServerConfig();
  cfg.certificate_chain_file = certs.garbage_file();

  Core::CNetwork_Asio socket;
  EXPECT_FALSE(socket.enable_ssl_server(cfg));
}

// A rejected configuration must leave nothing behind: listen() has to keep
// failing closed afterwards, rather than running on a half-installed context.
TEST(SslServerConfig, ARejectedConfigurationLeavesTheSocketUnconfigured) {
  const auto& certs = TestCertificates::Get();
  auto cfg = GoodServerConfig();
  cfg.certificate_chain_file = certs.missing_file();

  Core::CNetwork_Asio socket;
  ASSERT_TRUE(socket.init("127.0.0.1", FreePort()));
  ASSERT_FALSE(socket.enable_ssl_server(cfg));

  EXPECT_FALSE(socket.listen());
}

TEST(SslServerConfig, LoadsAKeyThatNeedsAPassword) {
  const auto& certs = TestCertificates::Get();
  Core::SslServerConfig cfg;
  cfg.certificate_chain_file = certs.server_with_encrypted_key().certificate_file;
  cfg.private_key_file = certs.server_with_encrypted_key().private_key_file;
  cfg.private_key_password = certs.key_password();

  Core::CNetwork_Asio socket;
  EXPECT_TRUE(socket.enable_ssl_server(cfg));
}

TEST(SslServerConfig, RejectsAnEncryptedKeyWithTheWrongPassword) {
  const auto& certs = TestCertificates::Get();
  Core::SslServerConfig cfg;
  cfg.certificate_chain_file = certs.server_with_encrypted_key().certificate_file;
  cfg.private_key_file = certs.server_with_encrypted_key().private_key_file;
  cfg.private_key_password = "not-the-password";

  Core::CNetwork_Asio socket;
  EXPECT_FALSE(socket.enable_ssl_server(cfg));
}

// mTLS with no CA to verify against would accept any certificate at all, which
// is worse than not asking for one.
TEST(SslServerConfig, RejectsRequireClientCertWithoutAClientCa) {
  auto cfg = GoodServerConfig();
  cfg.require_client_cert = true;

  Core::CNetwork_Asio socket;
  EXPECT_FALSE(socket.enable_ssl_server(cfg));
}

TEST(SslServerConfig, AcceptsRequireClientCertWithAClientCa) {
  const auto& certs = TestCertificates::Get();
  auto cfg = GoodServerConfig();
  cfg.require_client_cert = true;
  cfg.client_ca_file = certs.ca().certificate_file;

  Core::CNetwork_Asio socket;
  EXPECT_TRUE(socket.enable_ssl_server(cfg));
}

TEST(SslServerConfig, RejectsAnUnreadableClientCa) {
  const auto& certs = TestCertificates::Get();
  auto cfg = GoodServerConfig();
  cfg.require_client_cert = true;
  cfg.client_ca_file = certs.missing_file();

  Core::CNetwork_Asio socket;
  EXPECT_FALSE(socket.enable_ssl_server(cfg));
}

// A typo in the cipher list must not silently fall back to the defaults: an
// operator who narrowed the suite list is entitled to know it did not take.
TEST(SslServerConfig, RejectsAnUnusableCipherList) {
  auto cfg = GoodServerConfig();
  cfg.cipher_list = "THIS-IS-NOT-A-CIPHER";

  Core::CNetwork_Asio socket;
  EXPECT_FALSE(socket.enable_ssl_server(cfg));
}

TEST(SslServerConfig, AcceptsAUsableCipherList) {
  auto cfg = GoodServerConfig();
  cfg.cipher_list = "ECDHE-RSA-AES256-GCM-SHA384";

  Core::CNetwork_Asio socket;
  EXPECT_TRUE(socket.enable_ssl_server(cfg));
}

// The one deliberate asymmetry: DH parameters are irrelevant to an ECDHE-only
// configuration, so a bad file warns and carries on where every other bad file
// is fatal. Pinned so the asymmetry stays intentional.
TEST(SslServerConfig, ABadDhParamsFileIsNonFatal) {
  const auto& certs = TestCertificates::Get();
  auto cfg = GoodServerConfig();
  cfg.dh_params_file = certs.garbage_file();

  Core::CNetwork_Asio socket;
  EXPECT_TRUE(socket.enable_ssl_server(cfg));
}

// ---------------------------------------------------------------------------
// enable_ssl_client
// ---------------------------------------------------------------------------

TEST(SslClientConfig, AcceptsTheSecureDefaults) {
  Core::CNetwork_Asio socket;
  EXPECT_TRUE(socket.enable_ssl_client(Core::SslClientConfig{}))
      << "the default configuration verifies against the OS trust store";
}

TEST(SslClientConfig, AcceptsAPrivateCaBundle) {
  const auto& certs = TestCertificates::Get();
  Core::SslClientConfig cfg;
  cfg.ca_file = certs.ca().certificate_file;

  Core::CNetwork_Asio socket;
  EXPECT_TRUE(socket.enable_ssl_client(cfg));
}

TEST(SslClientConfig, RejectsAMissingCaBundle) {
  const auto& certs = TestCertificates::Get();
  Core::SslClientConfig cfg;
  cfg.ca_file = certs.missing_file();

  Core::CNetwork_Asio socket;
  EXPECT_FALSE(socket.enable_ssl_client(cfg));
}

// Asymmetric with ca_file above, and worth knowing before a deployment relies
// on it: OpenSSL does not stat a CApath, so a typo in ssl.caPath configures
// cleanly and then fails every handshake at runtime with an unrelated-looking
// verification error. Pinned as the behaviour that exists, not the behaviour
// one would design.
TEST(SslClientConfig, AMissingCaDirectoryIsAcceptedAndOnlyFailsLater) {
  Core::SslClientConfig cfg;
  cfg.ca_path = "/nonexistent/osirose-test-ca-directory";

  Core::CNetwork_Asio socket;
  EXPECT_TRUE(socket.enable_ssl_client(cfg))
      << "if this now fails, OpenSSL started validating CApath - a strict "
         "improvement, and the comment above should go";
}

// verify_peer = false skips the CA entirely, so a bad ca_file is not even
// looked at. Worth pinning: it is the difference between "misconfigured" and
// "deliberately unauthenticated".
TEST(SslClientConfig, VerifyPeerFalseIgnoresTheCaConfiguration) {
  const auto& certs = TestCertificates::Get();
  Core::SslClientConfig cfg;
  cfg.verify_peer = false;
  cfg.ca_file = certs.missing_file();

  Core::CNetwork_Asio socket;
  EXPECT_TRUE(socket.enable_ssl_client(cfg));
}

TEST(SslClientConfig, AcceptsAClientCertificateForMutualTls) {
  const auto& certs = TestCertificates::Get();
  Core::SslClientConfig cfg;
  cfg.ca_file = certs.ca().certificate_file;
  cfg.certificate_chain_file = certs.client().certificate_file;
  cfg.private_key_file = certs.client().private_key_file;

  Core::CNetwork_Asio socket;
  EXPECT_TRUE(socket.enable_ssl_client(cfg));
}

// Half-configured mTLS: the certificate would be offered and then fail to
// prove ownership, which surfaces as an opaque handshake error later.
TEST(SslClientConfig, RejectsAClientCertificateWithoutAKey) {
  const auto& certs = TestCertificates::Get();
  Core::SslClientConfig cfg;
  cfg.ca_file = certs.ca().certificate_file;
  cfg.certificate_chain_file = certs.client().certificate_file;

  Core::CNetwork_Asio socket;
  EXPECT_FALSE(socket.enable_ssl_client(cfg));
}

TEST(SslClientConfig, RejectsAMissingClientCertificate) {
  const auto& certs = TestCertificates::Get();
  Core::SslClientConfig cfg;
  cfg.ca_file = certs.ca().certificate_file;
  cfg.certificate_chain_file = certs.missing_file();
  cfg.private_key_file = certs.client().private_key_file;

  Core::CNetwork_Asio socket;
  EXPECT_FALSE(socket.enable_ssl_client(cfg));
}

TEST(SslClientConfig, RejectsAnUnusableCipherList) {
  Core::SslClientConfig cfg;
  cfg.cipher_list = "THIS-IS-NOT-A-CIPHER";

  Core::CNetwork_Asio socket;
  EXPECT_FALSE(socket.enable_ssl_client(cfg));
}

// Rebinding the stream to a different context mid-session would silently drop
// TLS on an already-established connection. The open-socket half of the same
// guard is covered in test_ssl_handshake.cpp against a real connection.
TEST(SslClientConfig, RejectsConfigurationOfAnActiveSocket) {
  Core::CNetwork_Asio socket;
  socket.set_active(true);

  EXPECT_FALSE(socket.enable_ssl_client(Core::SslClientConfig{}));

  socket.set_active(false);
}

#else  // !USE_SSL

// A plaintext build compiles the entire TLS implementation out. Reporting that
// as a skip rather than a pass is the point: this file passing green against a
// build with no TLS in it would be worthless evidence.
TEST(SslConfig, SkippedInAPlaintextBuild) {
  GTEST_SKIP() << "built without ENABLE_SSL; there is no TLS implementation to test";
}

#endif  // USE_SSL
