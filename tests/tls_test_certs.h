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
 * \file tls_test_certs.h
 *
 * Throwaway PEM material for the TLS tests, minted at runtime through the
 * OpenSSL API and written to a scratch directory that is removed at exit.
 *
 * Generated rather than checked in, for two reasons: a committed certificate
 * expires and turns the suite red on a date nobody chose, and a committed
 * private key is a private key in the repository.
 *
 * Everything is built once per process (see TestCertificates::Get) because RSA
 * keygen is the only slow thing in these tests.
 */
#ifndef TLS_TEST_CERTS_H_
#define TLS_TEST_CERTS_H_

#ifdef USE_SSL

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

namespace tls_test {

using KeyPtr = std::shared_ptr<EVP_PKEY>;
using CertPtr = std::shared_ptr<X509>;

/// A certificate and its key, both in memory (so the CA can sign with them) and
/// on disk (so the SslServerConfig / SslClientConfig fields can point at them).
struct Identity {
  std::string certificate_file;
  std::string private_key_file;
  CertPtr cert;
  KeyPtr key;
};

namespace detail {

inline void Fail(const char* _what) {
  throw std::runtime_error(std::string("tls_test: ") + _what);
}

inline KeyPtr GenerateKey() {
  // RSA rather than EC so that the RSA-specific cipher suites the servers may
  // be configured with stay usable against this material.
  EVP_PKEY* raw = EVP_RSA_gen(2048);
  if (!raw) Fail("EVP_RSA_gen failed");
  return KeyPtr(raw, EVP_PKEY_free);
}

inline void AddExtension(X509* _cert, X509V3_CTX* _ctx, int _nid, const char* _value) {
  X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, _ctx, _nid, _value);
  if (!ext) Fail("X509V3_EXT_conf_nid failed");
  X509_add_ext(_cert, ext, -1);
  X509_EXTENSION_free(ext);
}

inline void WriteCertificate(const std::string& _path, X509* _cert) {
  BIO* bio = BIO_new_file(_path.c_str(), "wb");
  if (!bio) Fail("could not open the certificate file for writing");
  const int ok = PEM_write_bio_X509(bio, _cert);
  BIO_free(bio);
  if (!ok) Fail("PEM_write_bio_X509 failed");
}

/// \param _password when non-empty, the key is written encrypted under it -
///        which is what exercises SslServerConfig::private_key_password.
inline void WriteKey(const std::string& _path, EVP_PKEY* _key, const std::string& _password = "") {
  BIO* bio = BIO_new_file(_path.c_str(), "wb");
  if (!bio) Fail("could not open the key file for writing");
  int ok = 0;
  if (_password.empty()) {
    ok = PEM_write_bio_PrivateKey(bio, _key, nullptr, nullptr, 0, nullptr, nullptr);
  } else {
    ok = PEM_write_bio_PrivateKey(bio, _key, EVP_aes_256_cbc(), nullptr, 0, nullptr,
                                  const_cast<char*>(_password.c_str()));
  }
  BIO_free(bio);
  if (!ok) Fail("PEM_write_bio_PrivateKey failed");
}

}  // namespace detail

/*!
 * \brief Mint one certificate.
 *
 * \param _dir            directory the two PEM files land in
 * \param _basename       file stem; produces <stem>.crt and <stem>.key
 * \param _common_name    subject CN
 * \param _san            subjectAltName value, e.g. "DNS:localhost".  Leave
 *                        empty for a CA.  A leaf *must* have one: OpenSSL's
 *                        hostname verification does not fall back to the CN,
 *                        so a CN-only leaf fails every verification test for
 *                        the wrong reason.
 * \param _issuer         signer; nullptr makes the certificate self-signed
 * \param _is_ca          adds basicConstraints CA:TRUE and certSign key usage
 * \param _key_password   encrypts the written key when non-empty
 */
inline Identity MakeCertificate(const std::filesystem::path& _dir, const std::string& _basename,
                                const std::string& _common_name, const std::string& _san,
                                const Identity* _issuer = nullptr, bool _is_ca = false,
                                const std::string& _key_password = "") {
  Identity id;
  id.key = detail::GenerateKey();

  X509* cert = X509_new();
  if (!cert) detail::Fail("X509_new failed");
  id.cert = CertPtr(cert, X509_free);

  // v3, so the extensions below are actually honoured.
  X509_set_version(cert, 2);

  static std::mt19937_64 serials{std::random_device{}()};
  ASN1_INTEGER_set_int64(X509_get_serialNumber(cert),
                         static_cast<int64_t>(serials() & 0x7fffffff));

  // Valid from an hour ago, to survive a modest clock skew on a CI runner.
  X509_gmtime_adj(X509_getm_notBefore(cert), -3600);
  X509_gmtime_adj(X509_getm_notAfter(cert), 24 * 3600);

  X509_set_pubkey(cert, id.key.get());

  X509_NAME* subject = X509_get_subject_name(cert);
  X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                             reinterpret_cast<const unsigned char*>(_common_name.c_str()), -1, -1,
                             0);
  X509_set_issuer_name(cert, _issuer ? X509_get_subject_name(_issuer->cert.get()) : subject);

  X509V3_CTX ctx;
  X509V3_set_ctx_nodb(&ctx);
  X509V3_set_ctx(&ctx, _issuer ? _issuer->cert.get() : cert, cert, nullptr, nullptr, 0);

  if (_is_ca) {
    detail::AddExtension(cert, &ctx, NID_basic_constraints, "critical,CA:TRUE");
    detail::AddExtension(cert, &ctx, NID_key_usage, "critical,keyCertSign,cRLSign");
  } else {
    detail::AddExtension(cert, &ctx, NID_basic_constraints, "critical,CA:FALSE");
    // Without digitalSignature the purpose check rejects the certificate before
    // the handshake gets anywhere near the hostname.
    detail::AddExtension(cert, &ctx, NID_key_usage,
                         "critical,digitalSignature,keyEncipherment");
    detail::AddExtension(cert, &ctx, NID_ext_key_usage, "serverAuth,clientAuth");
  }
  if (!_san.empty()) detail::AddExtension(cert, &ctx, NID_subject_alt_name, _san.c_str());

  EVP_PKEY* signing_key = _issuer ? _issuer->key.get() : id.key.get();
  if (!X509_sign(cert, signing_key, EVP_sha256())) detail::Fail("X509_sign failed");

  id.certificate_file = (_dir / (_basename + ".crt")).string();
  id.private_key_file = (_dir / (_basename + ".key")).string();
  detail::WriteCertificate(id.certificate_file, cert);
  detail::WriteKey(id.private_key_file, id.key.get(), _key_password);
  return id;
}

/*!
 * \brief The full set of material the TLS tests need, built once per process.
 *
 * The scratch directory is unique per run so concurrent ctest jobs cannot
 * collide, and is removed when the process exits.
 */
class TestCertificates {
 public:
  static const TestCertificates& Get() {
    static TestCertificates instance;
    return instance;
  }

  const std::filesystem::path& dir() const { return dir_; }

  /// The deployment's own CA. Its certificate is what a client points ca_file
  /// at, and what a server points client_ca_file at.
  const Identity& ca() const { return ca_; }

  /// Leaf signed by ca(), SAN DNS:localhost - the normal server identity.
  const Identity& server() const { return server_; }

  /// Same, but with an encrypted private key ("testpass").
  const Identity& server_with_encrypted_key() const { return server_encrypted_; }

  /// Leaf signed by ca(), SAN DNS:other.invalid - for the hostname mismatch.
  const Identity& other_host() const { return other_host_; }

  /// Leaf signed by ca(), for the mTLS client side.
  const Identity& client() const { return client_; }

  /// A second, unrelated CA. Handing this to a client as its ca_file proves
  /// verification actually rejects rather than merely happening to succeed.
  const Identity& foreign_ca() const { return foreign_ca_; }

  /// Leaf signed by foreign_ca(), SAN DNS:localhost. Correct name, wrong chain.
  const Identity& foreign_server() const { return foreign_server_; }

  /// An existing file that is not valid PEM - distinct from a path that does
  /// not exist at all, and a different failure mode inside OpenSSL.
  const std::string& garbage_file() const { return garbage_file_; }

  /// A path that is guaranteed not to exist.
  std::string missing_file() const { return (dir_ / "no-such-file.pem").string(); }

  const std::string& key_password() const { return key_password_; }

 private:
  TestCertificates() {
    dir_ = std::filesystem::temp_directory_path() /
           ("osirose-tls-test-" + std::to_string(std::random_device{}()));
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);

    ca_ = MakeCertificate(dir_, "ca", "osIROSE Test CA", "", nullptr, true);
    server_ = MakeCertificate(dir_, "server", "localhost", "DNS:localhost", &ca_);
    server_encrypted_ = MakeCertificate(dir_, "server-enc", "localhost", "DNS:localhost", &ca_,
                                        false, key_password_);
    other_host_ =
        MakeCertificate(dir_, "other-host", "other.invalid", "DNS:other.invalid", &ca_);
    client_ = MakeCertificate(dir_, "client", "isc-client", "DNS:client.invalid", &ca_);

    foreign_ca_ = MakeCertificate(dir_, "foreign-ca", "Someone Else CA", "", nullptr, true);
    foreign_server_ =
        MakeCertificate(dir_, "foreign-server", "localhost", "DNS:localhost", &foreign_ca_);

    garbage_file_ = (dir_ / "garbage.pem").string();
    if (std::FILE* f = std::fopen(garbage_file_.c_str(), "wb")) {
      std::fputs("this is not a certificate\n", f);
      std::fclose(f);
    }
  }

  ~TestCertificates() {
    std::error_code ignored;
    std::filesystem::remove_all(dir_, ignored);
  }

  TestCertificates(const TestCertificates&) = delete;
  TestCertificates& operator=(const TestCertificates&) = delete;

  std::filesystem::path dir_;
  Identity ca_;
  Identity server_;
  Identity server_encrypted_;
  Identity other_host_;
  Identity client_;
  Identity foreign_ca_;
  Identity foreign_server_;
  std::string garbage_file_;
  const std::string key_password_ = "testpass";
};

}  // namespace tls_test

#endif  // USE_SSL

#endif  // TLS_TEST_CERTS_H_
