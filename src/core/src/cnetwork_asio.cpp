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
 * cnetwork_asio.cpp
 *
 *  Created on: Nov 23, 2015
 *      Author: ctorres
 */

#include <cstdlib>
#include <iostream>
#include <thread>
#include "cnetwork_asio.h"
#include "platform_defines.h"

#ifdef USE_SSL
#include <asio/ssl/host_name_verification.hpp>
#include <openssl/ssl.h>
#endif

namespace Core {

// NetworkThreadPool::instance_ is defined in network_thread_pool.cpp

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

#ifdef USE_SSL
CNetwork_Asio::CNetwork_Asio() : CNetwork_Asio(nullptr) {}

CNetwork_Asio::CNetwork_Asio(std::shared_ptr<asio::ssl::context> _ctx)
    : INetwork(),
      networkService_(&NetworkThreadPool::GetInstance()),
      strand_(asio::make_strand(*networkService_->Get_IO_Service())),
      ssl_ctx_(_ctx ? std::move(_ctx)
                    : std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client)),
      socket_(*networkService_->Get_IO_Service(), *ssl_ctx_),
      listener_(*networkService_->Get_IO_Service()),
      packet_offset_(0),
      packet_size_(6),
      active_(false),
      remote_connection_(false) {
  INetwork::set_update_time(Core::Time::GetTickCount());
  logger_ = CLog::GetLogger(log_type::NETWORK).lock();
}
#else
CNetwork_Asio::CNetwork_Asio()
    : INetwork(),
      networkService_(&NetworkThreadPool::GetInstance()),
      strand_(asio::make_strand(*networkService_->Get_IO_Service())),
      socket_(*networkService_->Get_IO_Service()),
      listener_(*networkService_->Get_IO_Service()),
      packet_offset_(0),
      packet_size_(6),
      active_(false),
      remote_connection_(false) {
  INetwork::set_update_time(Core::Time::GetTickCount());
  logger_ = CLog::GetLogger(log_type::NETWORK).lock();
}
#endif

CNetwork_Asio::~CNetwork_Asio() {
  CNetwork_Asio::shutdown(true);
  if (process_thread_.joinable()) process_thread_.join();
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool CNetwork_Asio::init(std::string _ip, uint16_t _port) {
  if (_ip.length() < 2)
    return false;

  // Keep the caller's original string: network_address_ below holds the
  // *resolved* address, and SNI and certificate hostname verification against
  // an IP literal always fail.
  network_hostname_ = _ip;

  asio::error_code ec;
  tcp::resolver resolver(*networkService_->Get_IO_Service());
  // resolve() returns basic_resolver_results<tcp> (a range) in ASIO 1.13+
  auto results = resolver.resolve(_ip, std::to_string(_port), ec);
  if (!ec && !results.empty())
    network_address_ = results.begin()->endpoint().address().to_string();
  else
    network_address_ = _ip;
  network_port_ = _port;
  return true;
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

bool CNetwork_Asio::shutdown(bool _final) {
  if (!is_active())
    return true;

  // Single atomic claim. The old code was a check-then-act on is_active(), so
  // two threads hitting an error concurrently both passed the check, both ran
  // OnShutdown() + disconnect(), and OnDisconnected() double-fired. Under TLS
  // that also meant SSL_shutdown racing another thread inside SSL_read.
  //
  // A dedicated flag rather than active_.exchange(false), because shutdown()
  // may decline below when OnShutdown() returns false, and restoring active_
  // afterwards would reintroduce the race. The claim also suppresses re-entry
  // via disconnect() -> OnDisconnected() -> user code -> shutdown().
  if (shutting_down_.exchange(true))
    return true;

  if (!_final && !OnShutdown()) {
    shutting_down_ = false;
    return false;
  }

  // Stops completion handlers from re-arming recv_data()/ProcessSend().
  set_active(false);

  std::error_code ignored;
  if (listener_.is_open())
    listener_.close(ignored);

  // Abort in-flight I/O before touching the TLS engine.
  lowest_layer().cancel(ignored);

  auto teardown = [this, _final]() {
    if (!_final)
      disconnect();  // graceful TLS close_notify
    std::error_code ec;
    if (lowest_layer().is_open())
      lowest_layer().close(ec);
  };

  if (_final) {
    // Destructor path: the object is dying, so a posted strand task would run
    // against freed memory. See the lifetime caveat on the class.
    teardown();
  } else {
    // dispatch() runs inline when we are already on the strand - i.e. every
    // call from a read/write handler, the common case - so those callers still
    // observe a synchronous teardown. Off-strand callers get it posted, but
    // ordered against the I/O handlers.
    asio::dispatch(strand_, teardown);
  }
  return true;
}

// ---------------------------------------------------------------------------
// connect
// ---------------------------------------------------------------------------

void CNetwork_Asio::DrainPendingSends() {
  // send_data() may have queued packets while the socket was not yet active -
  // ProcessSend() bails out in that state, so nothing is pumping the queue.
  bool queue_empty;
  {
    std::lock_guard<std::mutex> lock(send_mutex_);
    queue_empty = send_queue_.empty();
  }
  if (!queue_empty && !async_write_active_.exchange(true))
    ProcessSend();
}

bool CNetwork_Asio::connect() {
  return connect_impl(nullptr);
}

bool CNetwork_Asio::connect_and_wait(std::chrono::milliseconds _timeout) {
  auto ready = std::make_shared<std::promise<bool>>();
  auto future = ready->get_future();

  if (!connect_impl(ready))
    return false;  // TCP-level failure; the promise was never handed off

  if (future.wait_for(_timeout) != std::future_status::ready) {
    logger_->error("connect_and_wait: handshake timed out after {}ms", _timeout.count());
    shutdown();
    // The handler still holds the shared_ptr, so a late completion sets a value
    // nobody reads. Harmless, and nothing dangles.
    return false;
  }
  return future.get();
}

bool CNetwork_Asio::connect_impl(std::shared_ptr<std::promise<bool>> _ready) {
  tcp::resolver resolver(*networkService_->Get_IO_Service());
  auto endpoint_iterator =
      resolver.resolve(network_address_, std::to_string(network_port_));

  if (!OnConnect()) return false;

#ifdef USE_SSL
  // Must happen before asio::connect: this is the last point at which rebinding
  // the stream to a different context is still safe.
  if (!EnsureClientContext())
    return false;

  // Always start from a fresh engine. An SSL object is spent once it has been
  // shut down, so reusing the stream after a previous session - via reconnect(),
  // or shutdown() followed by connect() - would fail the handshake.
  {
    std::error_code ignored;
    if (lowest_layer().is_open())
      lowest_layer().close(ignored);
    socket_ = asio::ssl::stream<tcp::socket>(*networkService_->Get_IO_Service(), *ssl_ctx_);
  }
#endif

  // A reactivated socket must be claimable by shutdown() again.
  shutting_down_ = false;

  std::error_code errorCode;
  // NB: send_mutex_ deliberately not held here. It guards send_queue_, not the
  // socket, and holding it across a blocking connect was purely misleading.
  asio::connect(lowest_layer(), endpoint_iterator, errorCode);
  remote_connection_ = true;

  if (errorCode) {
    logger_->info("Connect failed - {}: {}", errorCode.value(), errorCode.message());
    return false;
  }

#ifdef USE_SSL
  const std::string& host = ssl_client_cfg_.sni_hostname.empty()
                          ? network_hostname_
                          : ssl_client_cfg_.sni_hostname;

  // SNI unconditionally: a server doing certificate selection needs it whether
  // or not we go on to verify the result.
  if (!host.empty()) {
    if (!SSL_set_tlsext_host_name(socket_.native_handle(), host.c_str()))
      logger_->warn("Failed to set SNI hostname '{}'", host);
  }

  if (ssl_client_cfg_.verify_peer) {
    socket_.set_verify_callback(asio::ssl::host_name_verification(host), errorCode);
    if (errorCode) {
      logger_->error("Failed to install hostname verification for '{}' - {}: {}",
                     host, errorCode.value(), errorCode.message());
      std::error_code ignored;
      lowest_layer().close(ignored);
      return false;
    }
  }

  // The handshake stays asynchronous so callers built around async connect
  // notification (and callers on a UI/game thread) are not stalled. Use
  // connect_and_wait() when a definite answer is needed.
  socket_.async_handshake(
      asio::ssl::stream_base::client,
      asio::bind_executor(strand_, [this, ready = std::move(_ready)](const asio::error_code& ec) {
        if (ec) {
          logger_->error("TLS handshake failed - {}: {}", ec.value(), ec.message());
          // Not shutdown(): active_ is still false at this point, so shutdown()
          // would early-return having closed nothing, stranding the connected
          // TCP socket. Close the transport directly.
          std::error_code ignored;
          lowest_layer().close(ignored);
          OnDisconnected();  // callers finally learn about a handshake failure
          if (ready) ready->set_value(false);
          return;
        }
        set_active(true);
        OnConnected();
        DrainPendingSends();
        if (ready) ready->set_value(true);
      }));
  return true;
#else
  OnConnected();
  set_active(true);
  DrainPendingSends();
  // No handshake to wait for, so connect_and_wait() resolves immediately here
  // and stays equivalent to connect() in this build flavour.
  if (_ready) _ready->set_value(true);
  return true;
#endif
}

// ---------------------------------------------------------------------------
// listen
// ---------------------------------------------------------------------------

bool CNetwork_Asio::listen() {
#ifdef USE_SSL
  // Fail closed. There is no plaintext fallback in a TLS build: silently
  // accepting cleartext because a cert path was mistyped is worse than not
  // starting at all.
  if (!ssl_server_ctx_) {
    logger_->error("listen(): TLS build requires enable_ssl_server() before listen().");
    return false;
  }
#endif

  // A reactivated listener must be claimable by shutdown() again.
  shutting_down_ = false;

  OnListen();
  // asio::ip::address::from_string() is deprecated in ASIO 1.13+; use make_address()
  tcp::endpoint endpoint(asio::ip::make_address(network_address_), network_port_);
  listener_.open(endpoint.protocol());
  listener_.set_option(tcp::acceptor::reuse_address(true));
  listener_.non_blocking(true);
  listener_.bind(endpoint);
  listener_.listen();
  logger_->info("Listening started on {}:{}", endpoint.address().to_string(), endpoint.port());
  set_active(true);
  AcceptConnection();
  OnListening();
  return true;
}

// ---------------------------------------------------------------------------
// reconnect / disconnect
// ---------------------------------------------------------------------------

bool CNetwork_Asio::reconnect() {
  if (!remote_connection_) return false;
  disconnect();
  // connect() rebuilds the TLS stream, so the spent engine left behind by
  // disconnect() is not reused.
  return connect();
}

bool CNetwork_Asio::disconnect() {
  if (!OnDisconnect()) return false;

  std::error_code ignored;
#ifdef USE_SSL
  // asio::ssl::stream::async_shutdown() is unusable here: its handler would
  // capture `this` on an object the caller may destroy the moment disconnect()
  // returns. The synchronous form is not free either - shutdown_op waits for
  // the peer's close_notify and detail::io loops until the engine is done, so
  // on a blocking socket it can hang.
  //
  // Bound it by flipping to non-blocking first: the close_notify record still
  // goes out, the read of the peer's reply returns would_block, and detail::io
  // returns immediately.
  std::error_code ec;
  lowest_layer().non_blocking(true, ec);
  socket_.shutdown(ec);
  if (ec && ec != asio::error::would_block && ec != asio::error::try_again &&
      ec != asio::error::eof && ec != asio::ssl::error::stream_truncated)
    logger_->debug("TLS shutdown: {}: {}", ec.value(), ec.message());
#endif
  lowest_layer().shutdown(asio::socket_base::shutdown_both, ignored);
  OnDisconnected();
  return true;
}

// ---------------------------------------------------------------------------
// ProcessSend  (chained async_write)
// ---------------------------------------------------------------------------

void CNetwork_Asio::ProcessSend() {
  logger_->trace("CNetwork_Asio::ProcessSend enter");

  if (!this->is_active()) {
    // Nothing can drain the queue right now - either the handshake has not
    // completed yet, or we are shutting down. Clearing the flag is what lets a
    // later send_data() (or DrainPendingSends() once the handshake lands)
    // re-kick the pump. Leaving it latched true froze the queue permanently.
    async_write_active_ = false;
    return;
  }

  std::unique_lock<std::mutex> lock(send_mutex_);
  if (send_queue_.empty()) {
    async_write_active_ = false;
    return;
  }
  uint8_t* raw_ptr = send_queue_.front().get();
  lock.unlock();

  const uint16_t _size    = *reinterpret_cast<uint16_t*>(raw_ptr);
  const uint16_t _command = *reinterpret_cast<uint16_t*>(raw_ptr + sizeof(uint16_t));

#ifdef SPDLOG_TRACE_ON
  fmt::memory_buffer out;
  logger_->trace("ProcessSend: Header[{0}, 0x{1:04x}]: ", _size, (uint16_t)_command);
  for (int i = 0; i < _size; i++) format_to(out, "0x{0:02x} ", raw_ptr[i]);
  logger_->trace("{}", fmt::to_string(out));
#endif

  if (OnSend(socket_id_, raw_ptr)) {
    asio::async_write(
      socket_, asio::buffer(raw_ptr, _size),
      asio::bind_executor(strand_, [this](const asio::error_code& error,
                                          [[maybe_unused]] std::size_t bytes_transferred)
    {
      if (!error) {
        OnSent();
      } else {
        logger_->debug("ProcessSend: error = {}: {}", error.value(), error.message());

#ifdef USE_SSL
        // Compared whole, ahead of the switch: the switch below tests only
        // error.value() and so ignores the error *category*, and TLS errors
        // live in a different one. A peer vanishing without close_notify
        // yields stream_truncated, which would otherwise fall to default: and
        // leave the connection wedged instead of shut down.
        if (error == asio::ssl::error::stream_truncated) {
          shutdown();
        } else
#endif
        // In ASIO 1.13+ error values live directly in asio::error::,
        // not in the nested asio::error::basic_errors:: enum scope.
        switch (error.value()) {
          case asio::error::connection_aborted:
          case asio::error::connection_reset:
          case asio::error::network_reset:
          case asio::error::network_down:
          case asio::error::broken_pipe:
          case asio::error::shut_down:
          case asio::error::timed_out:
            shutdown();
            break;
          default:
            logger_->warn("ProcessSend: async_write returned an error. {}: {}", error.value(), error.message());
            break;
        }
      }

      send_mutex_.lock();
      if (!send_queue_.empty()) send_queue_.pop_front();
      const bool is_empty = send_queue_.empty();
      send_mutex_.unlock();

      if (!is_empty) {
        ProcessSend();
      } else {
        async_write_active_ = false;
      }
    }));
  } else {
    logger_->debug("Not sending packet: [{0}, 0x{1:x}] to client {2}. Removing from queue.",
                   _size, _command, get_id());

    send_mutex_.lock();
    if (!send_queue_.empty()) send_queue_.pop_front();
    const bool is_empty = send_queue_.empty();
    send_mutex_.unlock();

    if (!is_empty) {
      ProcessSend();
    } else {
      async_write_active_ = false;
    }
  }
}

// ---------------------------------------------------------------------------
// send_data
// ---------------------------------------------------------------------------

bool CNetwork_Asio::send_data(std::unique_ptr<uint8_t[]> _buffer) {
  logger_->trace("CNetwork_Asio::send_data enter");
  send_mutex_.lock();

  try {
    send_queue_.push_back(std::move(_buffer));
  } catch (...) {
    send_mutex_.unlock();
    return false;
  }

  send_mutex_.unlock();

  // Exactly one thread may drive the send pump. The previous
  //     if (size > 1 && async_write_active_) return;
  //     async_write_active_ = true; ProcessSend();
  // was a check-then-act: two threads enqueueing concurrently could both
  // observe async_write_active_ == false, both call ProcessSend(), and both
  // async_write the same front element - sending it twice and dropping its
  // successor. The exchange makes the claim atomic; whoever loses simply
  // leaves the packet on the queue for the winner's chain to pick up.
  if (!async_write_active_.exchange(true))
    ProcessSend();
  return true;
}

// ---------------------------------------------------------------------------
// recv_data  (two-phase async read loop)
// ---------------------------------------------------------------------------

bool CNetwork_Asio::recv_data([[maybe_unused]] uint16_t _size /*= 6*/) {
  if (OnReceive()) {
    int16_t BytesToRead = packet_size_ - packet_offset_;
    asio::async_read(
        socket_,
        asio::buffer(&buffer_[packet_offset_], BytesToRead),
        asio::transfer_exactly(BytesToRead),
        asio::bind_executor(strand_, [this](std::error_code errorCode, std::size_t length) {

          packet_offset_ += (uint16_t)length;
          update_time_ = Core::Time::GetTickCount();

          if (!errorCode) {
            if (!OnReceived(this->socket_id_, packet_size_, buffer_)) {
              logger_->debug("OnReceived aborted the connection, disconnecting...");
              shutdown();
            } else {
              if (this->is_active())
                recv_data();
            }
          } else {
#ifdef USE_SSL
            // Compared whole, ahead of the switch: the switch tests only
            // errorCode.value() and so ignores the error *category*, and TLS
            // errors live in a different one. A peer vanishing without
            // close_notify yields stream_truncated, which would otherwise fall
            // to default: and leave the connection wedged instead of shut down.
            if (errorCode == asio::ssl::error::stream_truncated) {
              if (shutdown())
                logger_->info("Socket {} ({}) truncated the TLS stream.", get_id(), get_name());
            } else
#endif
            // In ASIO 1.13+ error values live directly in asio::error::,
            // not in nested asio::error::basic_errors:: / misc_errors:: scopes.
            switch (errorCode.value()) {
              case asio::error::try_again:
                if (this->is_active())
                  recv_data();
                break;

              // NB: shutdown() -> disconnect() already fires OnDisconnected().
              // The explicit calls that used to sit in these branches made it
              // fire twice per teardown.
              case asio::error::not_connected:
                if (shutdown())
                  logger_->info("Socket {} is not connected, shutting down.", get_id());
                break;

              case asio::error::connection_aborted:
              case asio::error::operation_aborted:
              case asio::error::connection_reset:
              case asio::error::network_reset:
              case asio::error::network_down:
              case asio::error::broken_pipe:
              case asio::error::shut_down:
              case asio::error::timed_out:
              case asio::error::eof:
                if (shutdown())
                  logger_->info("Socket {} ({}) disconnected.", get_id(), get_name());
                break;

              default:
                logger_->debug("Socket Error {}: {}", errorCode.value(), errorCode.message());
                break;
            }
          }
        }));
  }
  return true;
}

// ---------------------------------------------------------------------------
// AcceptConnection
// ---------------------------------------------------------------------------

#ifndef USE_SSL
void CNetwork_Asio::AcceptConnection() {
  listener_.async_accept([this](std::error_code ec, tcp::socket socket) {
    if (!ec) {
      if (this->OnAccept()) {
        socket.non_blocking(true);
        auto nSock = std::make_unique<CNetwork_Asio>();
        nSock->set_address(socket.remote_endpoint().address().to_string());
        nSock->SetSocket(std::move(socket));
        nSock->set_active(true);
        this->OnAccepted(std::move(nSock));
      } else {
        std::error_code ignored;
        socket.close(ignored);
      }
    } else {
      logger_->debug("AcceptConnection: {}: {}", ec.value(), ec.message());
    }
    if (is_active()) AcceptConnection();
  });
}
#else
void CNetwork_Asio::AcceptConnection() {
  listener_.async_accept([this](std::error_code ec, tcp::socket socket) {
    if (!ec) {
      if (this->OnAccept()) {
        // Accepted connections share the listener's server context; the
        // shared_ptr keeps it alive for as long as any stream references it.
        auto nSock = std::make_unique<CNetwork_Asio>(ssl_server_ctx_);

        std::error_code addr_ec;
        auto endpoint = socket.remote_endpoint(addr_ec);
        if (!addr_ec)
          nSock->set_address(endpoint.address().to_string());

        // NOTE: unlike the plain-TCP path the socket is deliberately left in
        // blocking mode. Async ops do not need non_blocking(true), and a
        // would_block surfacing mid-TLS-record only complicates the engine's
        // state machine.
        nSock->SetSocket(std::move(socket));

        // Deliberately not setting remote_connection_: it gates reconnect(),
        // which is meaningless for an inbound connection. Matches the
        // plain-TCP path.
        //
        // Ownership moves into the handshake. Nothing outside sees this
        // connection until the handshake succeeds; failure or timeout destroys
        // it, so OnAccepted only ever fires on a fully-established session.
        auto* raw = nSock.get();
        raw->StartServerHandshake(std::move(nSock), this->OnAccepted,
                                  ssl_server_cfg_.handshake_timeout_seconds);
      } else {
        std::error_code ignored;
        socket.close(ignored);
      }
    } else {
      logger_->debug("AcceptConnection: {}: {}", ec.value(), ec.message());
    }
    // Re-arm. The stub this replaced did not, so the accept loop died on the
    // very first call from listen().
    if (is_active()) AcceptConnection();
  });
}

void CNetwork_Asio::StartServerHandshake(
    std::unique_ptr<CNetwork_Asio> _self,
    std::function<void(std::unique_ptr<Core::INetwork>)> _on_accepted,
    uint32_t _timeout_seconds) {
  auto guard = std::make_shared<HandshakeGuard>(*networkService_->Get_IO_Service());

  if (_timeout_seconds > 0) {
    guard->timer.expires_after(std::chrono::seconds(_timeout_seconds));
    // Both the timer and the handshake below are bound to strand_, so they
    // cannot run concurrently and `finished` needs no atomic.
    //
    // Capturing `this` raw is safe: `finished` is only false while the
    // handshake handler still holds _self, so *this is alive. The guard
    // outlives *this via the shared_ptr, so the timer object never dangles -
    // and the early return below touches only the guard.
    guard->timer.async_wait(asio::bind_executor(strand_,
      [this, guard](const asio::error_code& ec) {
        if (ec == asio::error::operation_aborted || guard->finished) return;
        guard->finished = true;
        logger_->warn("TLS handshake timed out for {}, closing.", get_address());
        std::error_code ignored;
        socket_.next_layer().close(ignored);  // aborts the pending async_handshake
      }));
  }

  socket_.async_handshake(asio::ssl::stream_base::server, asio::bind_executor(strand_,
      [self = std::move(_self), on_accepted = std::move(_on_accepted), guard]
      (const asio::error_code& ec) mutable {
        const bool timed_out = guard->finished;
        guard->finished = true;
        guard->timer.cancel();

        if (timed_out || ec) {
          if (!timed_out)
            self->logger_->warn("TLS handshake failed for {} - {}: {}",
                                self->get_address(), ec.value(), ec.message());
          std::error_code ignored;
          self->socket_.next_layer().close(ignored);
          return;  // self destroyed here -> connection dropped
        }

        self->set_active(true);  // matches the plain-TCP path's ordering
        on_accepted(std::move(self));
      }));
}

// ---------------------------------------------------------------------------
// TLS configuration
// ---------------------------------------------------------------------------

namespace {

/// The option set shared by both ends: workarounds, fresh DH per handshake, and
/// everything below TLS 1.2 turned off.
asio::ssl::context::options SecureContextOptions() {
  return asio::ssl::context::default_workarounds
       | asio::ssl::context::single_dh_use
       | asio::ssl::context::no_sslv2
       | asio::ssl::context::no_sslv3
       | asio::ssl::context::no_tlsv1
       | asio::ssl::context::no_tlsv1_1;
}

}  // namespace

bool CNetwork_Asio::enable_ssl_server(const SslServerConfig& _cfg) {
  if (_cfg.certificate_chain_file.empty() || _cfg.private_key_file.empty()) {
    logger_->error("enable_ssl_server(): certificate_chain_file and private_key_file are both required.");
    return false;
  }
  if (_cfg.require_client_cert && _cfg.client_ca_file.empty()) {
    logger_->error("enable_ssl_server(): require_client_cert needs a client_ca_file to verify against.");
    return false;
  }

  auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_server);
  asio::error_code ec;

  ctx->set_options(SecureContextOptions(), ec);
  if (ec) {
    logger_->error("enable_ssl_server(): set_options failed - {}: {}", ec.value(), ec.message());
    return false;
  }

  // Must be installed before the key is loaded - loading is what consumes it.
  if (!_cfg.private_key_password.empty()) {
    const std::string password = _cfg.private_key_password;
    ctx->set_password_callback(
        [password](std::size_t, asio::ssl::context::password_purpose) { return password; }, ec);
    if (ec) {
      logger_->error("enable_ssl_server(): set_password_callback failed - {}: {}", ec.value(), ec.message());
      return false;
    }
  }

  ctx->use_certificate_chain_file(_cfg.certificate_chain_file, ec);
  if (ec) {
    logger_->error("enable_ssl_server(): failed to load certificate chain '{}' - {}: {}",
                   _cfg.certificate_chain_file, ec.value(), ec.message());
    return false;
  }

  ctx->use_private_key_file(_cfg.private_key_file, asio::ssl::context::pem, ec);
  if (ec) {
    logger_->error("enable_ssl_server(): failed to load private key '{}' - {}: {}",
                   _cfg.private_key_file, ec.value(), ec.message());
    return false;
  }

  // Non-fatal: irrelevant for ECDHE-only configurations.
  if (!_cfg.dh_params_file.empty()) {
    ctx->use_tmp_dh_file(_cfg.dh_params_file, ec);
    if (ec) {
      logger_->warn("enable_ssl_server(): failed to load DH params '{}' - {}: {}. Continuing without them.",
                    _cfg.dh_params_file, ec.value(), ec.message());
      ec.clear();
    }
  }

  if (!_cfg.client_ca_file.empty()) {
    ctx->load_verify_file(_cfg.client_ca_file, ec);
    if (ec) {
      logger_->error("enable_ssl_server(): failed to load client CA '{}' - {}: {}",
                     _cfg.client_ca_file, ec.value(), ec.message());
      return false;
    }
    auto mode = asio::ssl::verify_peer;
    if (_cfg.require_client_cert)
      mode |= asio::ssl::verify_fail_if_no_peer_cert;
    ctx->set_verify_mode(mode, ec);
  } else {
    ctx->set_verify_mode(asio::ssl::verify_none, ec);
  }
  if (ec) {
    logger_->error("enable_ssl_server(): set_verify_mode failed - {}: {}", ec.value(), ec.message());
    return false;
  }

  if (!_cfg.cipher_list.empty()) {
    if (SSL_CTX_set_cipher_list(ctx->native_handle(), _cfg.cipher_list.c_str()) != 1) {
      logger_->error("enable_ssl_server(): no usable cipher in cipher_list '{}'.", _cfg.cipher_list);
      return false;
    }
  }

  ssl_server_ctx_ = std::move(ctx);
  ssl_server_cfg_ = _cfg;
  logger_->info("TLS server context ready (cert '{}', mTLS {}).",
                _cfg.certificate_chain_file, _cfg.require_client_cert ? "required" : "off");
  return true;
}

bool CNetwork_Asio::enable_ssl_client(const SslClientConfig& _cfg) {
  // Rebinding the stream is only safe before a handshake; doing it mid-session
  // would silently drop TLS.
  if (is_active() || lowest_layer().is_open()) {
    logger_->error("enable_ssl_client() must be called before connect().");
    return false;
  }

  auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
  asio::error_code ec;

  ctx->set_options(SecureContextOptions(), ec);
  if (ec) {
    logger_->error("enable_ssl_client(): set_options failed - {}: {}", ec.value(), ec.message());
    return false;
  }

  if (_cfg.verify_peer) {
    ctx->set_verify_mode(asio::ssl::verify_peer, ec);
    if (ec) {
      logger_->error("enable_ssl_client(): set_verify_mode failed - {}: {}", ec.value(), ec.message());
      return false;
    }

    // CA source precedence: explicit file, then explicit directory, then the
    // OS trust store.
    if (!_cfg.ca_file.empty()) {
      ctx->load_verify_file(_cfg.ca_file, ec);
      if (ec) {
        logger_->error("enable_ssl_client(): failed to load CA bundle '{}' - {}: {}",
                       _cfg.ca_file, ec.value(), ec.message());
        return false;
      }
    } else if (!_cfg.ca_path.empty()) {
      ctx->add_verify_path(_cfg.ca_path, ec);
      if (ec) {
        logger_->error("enable_ssl_client(): failed to add CA path '{}' - {}: {}",
                       _cfg.ca_path, ec.value(), ec.message());
        return false;
      }
    } else {
      ctx->set_default_verify_paths(ec);
      if (ec) {
        logger_->error("enable_ssl_client(): failed to load the system trust store - {}: {}",
                       ec.value(), ec.message());
        return false;
      }
    }
  } else {
    ctx->set_verify_mode(asio::ssl::verify_none, ec);
    if (ec) {
      logger_->error("enable_ssl_client(): set_verify_mode failed - {}: {}", ec.value(), ec.message());
      return false;
    }
    logger_->warn("TLS peer verification is DISABLED for this socket. The connection is "
                  "encrypted but unauthenticated, and trivially machine-in-the-middle-able. "
                  "Ship the server's CA via ca_file instead of turning this off.");
  }

  // Optional client certificate for mTLS.
  if (!_cfg.certificate_chain_file.empty()) {
    if (!_cfg.private_key_password.empty()) {
      const std::string password = _cfg.private_key_password;
      ctx->set_password_callback(
          [password](std::size_t, asio::ssl::context::password_purpose) { return password; }, ec);
      if (ec) {
        logger_->error("enable_ssl_client(): set_password_callback failed - {}: {}", ec.value(), ec.message());
        return false;
      }
    }
    ctx->use_certificate_chain_file(_cfg.certificate_chain_file, ec);
    if (ec) {
      logger_->error("enable_ssl_client(): failed to load client certificate '{}' - {}: {}",
                     _cfg.certificate_chain_file, ec.value(), ec.message());
      return false;
    }
    if (_cfg.private_key_file.empty()) {
      logger_->error("enable_ssl_client(): certificate_chain_file was given without a private_key_file.");
      return false;
    }
    ctx->use_private_key_file(_cfg.private_key_file, asio::ssl::context::pem, ec);
    if (ec) {
      logger_->error("enable_ssl_client(): failed to load client key '{}' - {}: {}",
                     _cfg.private_key_file, ec.value(), ec.message());
      return false;
    }
  }

  if (!_cfg.cipher_list.empty()) {
    if (SSL_CTX_set_cipher_list(ctx->native_handle(), _cfg.cipher_list.c_str()) != 1) {
      logger_->error("enable_ssl_client(): no usable cipher in cipher_list '{}'.", _cfg.cipher_list);
      return false;
    }
  }

  // asio::ssl::stream is move-assignable, so the stream can be rebound to the
  // new context - but only before a handshake, which the guard above enforces.
  ssl_ctx_ = std::move(ctx);
  socket_  = asio::ssl::stream<tcp::socket>(*networkService_->Get_IO_Service(), *ssl_ctx_);
  ssl_client_cfg_ = _cfg;
  ssl_client_configured_ = true;
  return true;
}

bool CNetwork_Asio::EnsureClientContext() {
  if (ssl_client_configured_)
    return true;
  // Verification is on by default: a caller who never configured TLS still gets
  // an authenticated session against the OS trust store. Opting out takes an
  // explicit enable_ssl_client({.verify_peer = false}).
  return enable_ssl_client(SslClientConfig{});
}
#endif

// ---------------------------------------------------------------------------
// dispatch
// ---------------------------------------------------------------------------

void CNetwork_Asio::dispatch(std::function<void()> _handler) {
  // ASIO 1.18+: dispatch() requires an explicit executor; pass the io_context's
  // executor so the handler runs on the network thread pool.
  asio::dispatch(networkService_->Get_IO_Service()->get_executor(),
                 [_handler]() { _handler(); });
}

} // namespace Core
