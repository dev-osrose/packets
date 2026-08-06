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

/*
 * cnetwork_asio.h
 *
 *  Created on: Nov 23, 2015
 *      Author: ctorres
 */

#ifndef _CNETWORK_ASIO_H_
#define _CNETWORK_ASIO_H_

#ifdef _WIN32
  #ifndef __MINGW32__
    #pragma warning(push)
    #pragma warning(disable : 6011 6031 6102 6255 6258 6326 6387)
    #define _WIN32_WINNT 0x0601
  #endif
#endif

#include <asio.hpp>
#ifdef USE_SSL
#include <asio/ssl.hpp>
#endif

#ifdef _WIN32
  #ifndef __MINGW32__
    #pragma warning(pop)
  #endif
#endif

#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <spdlog/spdlog.h>
#include "inetwork.h"
#include "logconsole.h"
#include "network_thread_pool.h"

#ifndef MAX_PACKET_SIZE
  #define MAX_PACKET_SIZE 0x7FF
#endif

using asio::ip::tcp;
namespace Core {

/*!
 * \class CNetwork_Asio
 *
 * \brief An asio impl for networking sockets
 *
 * This class uses ASIO (http://think-async.com/) to implement the networking
 * interface. When compiled with USE_SSL defined, it uses an SSL/TLS stream
 * over TCP; otherwise it uses a plain TCP socket. The public API is identical
 * in both cases.
 *
 * \sa INetwork
 *
 * \author Raven
 * \date nov 2015
 */
class CNetwork_Asio : public INetwork {
 public:
  CNetwork_Asio();

#ifdef USE_SSL
  /*!
   * \brief Construct bound to an existing TLS context.
   *
   * Used for accepted connections, which share the listener's server context.
   * asio::ssl::stream holds a *reference* to its context, so the shared_ptr is
   * what keeps that context alive for as long as any stream references it.
   * Passing nullptr builds a fresh tls_client context (the default ctor).
   */
  explicit CNetwork_Asio(std::shared_ptr<asio::ssl::context> _ctx);
#endif

  virtual ~CNetwork_Asio();

  virtual bool init(std::string _ip, uint16_t _port) override;
  virtual bool shutdown(bool _final = false) override;

  virtual bool connect() override;
  virtual bool listen() override;
  virtual bool reconnect() override;
  virtual bool disconnect() override;

  /*!
   * \brief connect() that returns only once the TLS handshake has resolved.
   *
   * connect() is asynchronous: it returns as soon as the TCP connection is up,
   * while the handshake is still in flight, so is_active() is still false on
   * return.  This variant blocks until the handshake succeeds, fails, or the
   * timeout elapses, and reports the real outcome.
   *
   * \warning Must NOT be called from a network completion handler.  It blocks
   * the calling thread, and the handshake needs a pool thread to complete on -
   * calling it from inside a handler can deadlock.
   *
   * In a non-SSL build there is no handshake, so this is equivalent to
   * connect(); callers stay portable across both build flavours.
   */
  bool connect_and_wait(std::chrono::milliseconds _timeout = std::chrono::seconds(10));

  virtual bool send_data(std::unique_ptr<uint8_t[]> _buffer) override;
  virtual bool recv_data(uint16_t _size = MAX_PACKET_SIZE) override;

  virtual bool is_active() const override {
    return active_.load(std::memory_order_acquire);
  }
  virtual void set_active(bool _val) override {
    active_.store(_val, std::memory_order_release);
  }

  bool isRemoteConnection() const { return remote_connection_; }

#ifdef USE_SSL
  virtual bool enable_ssl_server(const SslServerConfig& _cfg) override;
  virtual bool enable_ssl_client(const SslClientConfig& _cfg) override;
#endif

  virtual void dispatch(std::function<void()> _handler) override;

 protected:
  void AcceptConnection();
  void ProcessSend();

  /// The TCP socket underneath, whether or not TLS is layered on top.
  ///
  /// NB: next_layer(), not lowest_layer().  ssl::stream's lowest_layer_type is
  /// asio::basic_socket, whereas next_layer_type is tcp::socket
  /// (basic_stream_socket) - the same type the non-SSL branch holds, which is
  /// what lets both branches share one accessor and one SetSocket.
  tcp::socket& lowest_layer() {
#ifdef USE_SSL
    return socket_.next_layer();
#else
    return socket_;
#endif
  }

  void SetSocket(tcp::socket&& _sock) {
#ifdef USE_SSL
    // basic_stream_socket is move-assignable, and the TLS engine drives I/O
    // through in-memory BIOs rather than the fd, so swapping the next layer
    // *before any handshake* is safe.
    socket_.next_layer() = std::move(_sock);
#else
    socket_ = std::move(_sock);
#endif
  }

  /// Kick the send queue if send_data() enqueued anything while the socket was
  /// not yet active (i.e. during a handshake).  Without this the queued packets
  /// sit there until the *next* send_data() happens to re-arm the pump.
  void DrainPendingSends();

  bool connect_impl(std::shared_ptr<std::promise<bool>> _ready);

#ifdef USE_SSL
  /// Carries the handshake deadline timer and the single-claim flag.  Held by
  /// shared_ptr so the timer outlives *this if the connection is dropped.
  struct HandshakeGuard {
    asio::steady_timer timer;
    bool               finished = false;  //!< strand-serialized; no atomic needed
    explicit HandshakeGuard(asio::io_context& _io) : timer(_io) {}
  };

  /*!
   * \brief Run the server side of the TLS handshake for a freshly accepted peer.
   *
   * Takes ownership of the new connection: nothing outside sees it until the
   * handshake succeeds, and a failure or timeout destroys it.  _on_accepted is
   * passed by value so the handler never touches the listener afterwards.
   */
  void StartServerHandshake(std::unique_ptr<CNetwork_Asio> _self,
                            std::function<void(std::unique_ptr<Core::INetwork>)> _on_accepted,
                            uint32_t _timeout_seconds);

  /// Apply the default (verifying) client configuration if enable_ssl_client()
  /// was never called.  Invoked from connect_impl() before asio::connect - the
  /// last point at which rebinding the stream is still safe.
  bool EnsureClientContext();
#endif

  virtual void reset_internal_buffer() override {
    packet_offset_ = 0;
    packet_size_ = 6;
  }
  std::shared_ptr<spdlog::logger> logger_;

 protected:
  Core::NetworkThreadPool* networkService_;

  /// The sole executor for everything touching the stream.  ASIO runs a
  /// composed operation's intermediate steps in the *handler's* associated
  /// executor, so binding the final handler pulls the whole read/write/handshake
  /// chain onto this strand.  That is what makes concurrent SSL_read/SSL_write/
  /// SSL_shutdown on one SSL* impossible.
  asio::strand<asio::io_context::executor_type> strand_;

#ifdef USE_SSL
  // Declaration order matters: ssl_ctx_ must precede socket_, whose initializer
  // dereferences it.
  std::shared_ptr<asio::ssl::context> ssl_ctx_;
  asio::ssl::stream<tcp::socket>      socket_;
  std::shared_ptr<asio::ssl::context> ssl_server_ctx_;  //!< listener only
  SslServerConfig                     ssl_server_cfg_;  //!< listener only
  SslClientConfig                     ssl_client_cfg_;
  bool                                ssl_client_configured_ = false;
#else
  tcp::socket socket_;
#endif

  tcp::acceptor listener_;

  std::atomic_bool async_write_active_ = false;
  std::deque<std::unique_ptr<uint8_t[]>> send_queue_;
  std::mutex send_mutex_;
  std::condition_variable recv_condition_;

  uint8_t buffer_[MAX_PACKET_SIZE];
  uint16_t packet_offset_ = 0;
  uint16_t packet_size_ = 6;
  /// Read by is_active() from every pool thread and written by shutdown() and
  /// set_active(); a plain bool here is a data race.
  std::atomic_bool active_ = false;
  /// Claimed once by whichever thread wins the teardown, so OnShutdown() and
  /// disconnect() run exactly once. Reset by listen()/connect_impl() so a
  /// reactivated socket is claimable again.
  std::atomic_bool shutting_down_ = false;
  bool remote_connection_ = false;
  /// The hostname as passed to init(), before resolution.  network_address_
  /// holds the *resolved* address, and SNI and hostname verification against an
  /// IP literal always fail.
  std::string network_hostname_;
};
}

#endif
