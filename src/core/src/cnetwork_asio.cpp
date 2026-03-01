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

namespace Core {

// NetworkThreadPool::instance_ is defined in network_thread_pool.cpp

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

#ifdef USE_SSL
CNetwork_Asio::CNetwork_Asio()
    : INetwork(),
      networkService_(&NetworkThreadPool::GetInstance()),
      ssl_ctx_(asio::ssl::context::tls_client),
      socket_(*networkService_->Get_IO_Service(), ssl_ctx_),
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
  bool rtnValue = false;
  if (!is_active())
    return true;

  if (_final || (OnShutdown() && disconnect())) {
    rtnValue = true;
    if (listener_.is_open()) {
      std::error_code ignored;
      listener_.close(ignored);
    }

#ifdef USE_SSL
    if (socket_.lowest_layer().is_open()) {
      std::error_code ignored;
      socket_.lowest_layer().close(ignored);
    }
#else
    if (socket_.is_open()) {
      std::error_code ignored;
      socket_.close(ignored);
    }
#endif
    active_ = false;
  }
  return rtnValue;
}

// ---------------------------------------------------------------------------
// connect
// ---------------------------------------------------------------------------

bool CNetwork_Asio::connect() {
  tcp::resolver resolver(*networkService_->Get_IO_Service());
  auto endpoint_iterator =
      resolver.resolve(network_address_, std::to_string(network_port_));

  if (!OnConnect()) return false;

  send_mutex_.lock();
  std::error_code errorCode;
#ifdef USE_SSL
  asio::connect(socket_.lowest_layer(), endpoint_iterator, errorCode);
#else
  asio::connect(socket_, endpoint_iterator, errorCode);
#endif
  send_mutex_.unlock();
  remote_connection_ = true;

  if (errorCode) {
    logger_->info("Connect failed - {}: {}", errorCode.value(), errorCode.message());
    return false;
  }

#ifdef USE_SSL
  // Perform the TLS handshake asynchronously after the TCP connection is up.
  socket_.async_handshake(
      asio::ssl::stream_base::client,
      [this](const asio::error_code& ec) {
        if (!ec) {
          active_ = true;
          OnConnected();
        } else {
          logger_->error("SSL handshake failed - {}: {}", ec.value(), ec.message());
        }
      });
  return true;
#else
  OnConnected();
  active_ = true;
  return active_;
#endif
}

// ---------------------------------------------------------------------------
// listen
// ---------------------------------------------------------------------------

bool CNetwork_Asio::listen() {
  OnListen();
  // asio::ip::address::from_string() is deprecated in ASIO 1.13+; use make_address()
  tcp::endpoint endpoint(asio::ip::make_address(network_address_), network_port_);
  listener_.open(endpoint.protocol());
  listener_.set_option(tcp::acceptor::reuse_address(true));
  listener_.non_blocking(true);
  listener_.bind(endpoint);
  listener_.listen();
  logger_->info("Listening started on {}:{}", endpoint.address().to_string(), endpoint.port());
  active_ = true;
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
  return connect();
}

bool CNetwork_Asio::disconnect() {
  if (!OnDisconnect()) return false;

  std::error_code ignored;
#ifdef USE_SSL
  socket_.lowest_layer().shutdown(asio::socket_base::shutdown_both, ignored);
#else
  socket_.shutdown(asio::socket_base::shutdown_both, ignored);
#endif
  OnDisconnected();
  return true;
}

// ---------------------------------------------------------------------------
// ProcessSend  (chained async_write)
// ---------------------------------------------------------------------------

void CNetwork_Asio::ProcessSend() {
  logger_->trace("CNetwork_Asio::ProcessSend enter");
  if (this->is_active()) {
    send_mutex_.lock();
    uint8_t* raw_ptr = send_queue_.front().get();
    send_mutex_.unlock();

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
        [this](const asio::error_code& error,
               [[maybe_unused]] std::size_t bytes_transferred)
      {
        if (!error) {
          OnSent();
        } else {
          logger_->debug("ProcessSend: error = {}: {}", error.value(), error.message());

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
        send_queue_.pop_front();
        const bool is_empty = send_queue_.empty();
        send_mutex_.unlock();

        if (!is_empty) {
          ProcessSend();
        } else {
          async_write_active_ = false;
        }
      });
    } else {
      logger_->debug("Not sending packet: [{0}, 0x{1:x}] to client {2}. Removing from queue.",
                     _size, _command, get_id());

      send_mutex_.lock();
      send_queue_.pop_front();
      const bool is_empty = send_queue_.empty();
      send_mutex_.unlock();

      if (!is_empty) {
        ProcessSend();
      } else {
        async_write_active_ = false;
      }
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

  const auto size = send_queue_.size();
  send_mutex_.unlock();

  if (size > 1 && async_write_active_.load() == true) {
    return true;
  }

  async_write_active_ = true;
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
        [this](std::error_code errorCode, std::size_t length) {

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
            switch (errorCode.value()) {
              // In ASIO 1.13+ error values live directly in asio::error::,
              // not in nested asio::error::basic_errors:: / misc_errors:: scopes.
              case asio::error::try_again:
                if (this->is_active())
                  recv_data();
                break;

              case asio::error::not_connected:
                if (shutdown()) {
                  logger_->info("Socket {} is not connected, shutting down.", get_id());
                  OnDisconnected();
                }
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
                if (shutdown()) {
                  logger_->info("Socket {} ({}) disconnected.", get_id(), get_name());
                  OnDisconnected();
                }
                break;

              default:
                logger_->debug("Socket Error {}: {}", errorCode.value(), errorCode.message());
                break;
            }
          }
        });
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
    }
    if (active_) AcceptConnection();
  });
}
#else
void CNetwork_Asio::AcceptConnection() {
  // SSL server-side accept is not implemented in this client-oriented build.
  // Compile the server without USE_SSL or provide a separate SSL server impl.
  logger_->warn("AcceptConnection() called on an SSL socket — server-side SSL accept not supported.");
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
