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

#ifndef NETWORK_THREAD_POOL_H_
#define NETWORK_THREAD_POOL_H_

#ifdef _WIN32
  #ifndef __MINGW32__
    #pragma warning(push)
    #pragma warning(disable : 6011 6031 6102 6255 6258 6326 6387)
    #define _WIN32_WINNT 0x0601
  #endif
#endif

#include <asio.hpp>

#ifdef _WIN32
  #ifndef __MINGW32__
    #pragma warning(pop)
  #endif
#endif

#include <thread>
#include "threadpool.h"

namespace Core {

/**
 * \class NetworkThreadPool
 * \brief Singleton that owns the ASIO io_context and runs it on a thread pool.
 *
 * Call GetInstance() to obtain the singleton.  The io_context runs on
 * up to hardware_concurrency() threads (optionally capped by maxthreads).
 * A work guard keeps it alive until the singleton is destroyed.
 */
class NetworkThreadPool {
public:
  /// Returns the singleton, creating it with the given thread cap on first call.
  /// maxthreads == 0 means "use hardware_concurrency()".
  static NetworkThreadPool& GetInstance(uint16_t maxthreads = 0) {
    if (instance_ == nullptr) instance_ = new NetworkThreadPool(maxthreads);
    return *instance_;
  }

  /// Destroy the singleton (releases io_context and joins threads).
  static void DeleteInstance() {
    if (instance_ != nullptr) {
      delete instance_;
      instance_ = nullptr;
    }
  }

  /// Alias kept for backward compatibility.
  static void DestroyInstance() { DeleteInstance(); }

  asio::io_context* Get_IO_Service() { return &io_context_; }

  uint16_t GetThreadCount() const { return static_cast<uint16_t>(thread_count_); }

  ~NetworkThreadPool() {
    work_guard_.reset();
    io_context_.stop();
    // ThreadPool destructor joins all worker threads
  }

  NetworkThreadPool(const NetworkThreadPool&)            = delete;
  NetworkThreadPool& operator=(const NetworkThreadPool&) = delete;

private:
  static size_t computeThreadCount(uint16_t maxthreads) {
    size_t n = std::thread::hardware_concurrency();
    if (n == 0) n = 1;
    if (maxthreads != 0 && n > static_cast<size_t>(maxthreads))
      n = static_cast<size_t>(maxthreads);
    return n;
  }

  explicit NetworkThreadPool(uint16_t maxthreads = 0)
      : thread_count_(computeThreadCount(maxthreads))
      , io_context_()
      , work_guard_(asio::make_work_guard(io_context_))
      , thread_pool_(thread_count_)
  {
    for (size_t i = 0; i < thread_count_; ++i) {
      thread_pool_.enqueue([this] { io_context_.run(); });
    }
  }

  static NetworkThreadPool* instance_;

  size_t                                               thread_count_;
  asio::io_context                                     io_context_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  ThreadPool                                           thread_pool_;
};

} // namespace Core

#endif // NETWORK_THREAD_POOL_H_
