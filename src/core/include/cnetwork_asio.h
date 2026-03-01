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

#include <deque>
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
  virtual ~CNetwork_Asio();

  virtual bool init(std::string _ip, uint16_t _port) override;
  virtual bool shutdown(bool _final = false) override;

  virtual bool connect() override;
  virtual bool listen() override;
  virtual bool reconnect() override;
  virtual bool disconnect() override;

  virtual bool send_data(std::unique_ptr<uint8_t[]> _buffer) override;
  virtual bool recv_data(uint16_t _size = MAX_PACKET_SIZE) override;

  virtual bool is_active() const override {
    return active_;
  }
  virtual void set_active(bool _val) override { active_ = _val; }

  bool isRemoteConnection() const { return remote_connection_; }

  virtual void dispatch(std::function<void()> _handler) override;

 protected:
  void AcceptConnection();
  void ProcessSend();

#ifndef USE_SSL
  void SetSocket(tcp::socket&& _sock) { socket_ = std::move(_sock); }
#endif

  virtual void reset_internal_buffer() override {
    packet_offset_ = 0;
    packet_size_ = 6;
  }
  std::shared_ptr<spdlog::logger> logger_;

 protected:
  Core::NetworkThreadPool* networkService_;

#ifdef USE_SSL
  asio::ssl::context                       ssl_ctx_;
  asio::ssl::stream<tcp::socket>           socket_;
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
  bool active_ = false;
  bool remote_connection_ = false;
};
}

#endif
