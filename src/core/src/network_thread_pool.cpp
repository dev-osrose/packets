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

#include "network_thread_pool.h"

#include "logconsole.h"

namespace Core {

// Definition of the static singleton pointer
std::atomic<NetworkThreadPool*> NetworkThreadPool::instance_{nullptr};
std::mutex                      NetworkThreadPool::instance_mutex_;

NetworkThreadPool& NetworkThreadPool::GetInstance(uint16_t maxthreads) {
  // Fast path: a single acquire load, no lock, once the pool exists.  The
  // acquire pairs with the release store below, so a thread that observes the
  // pointer is guaranteed to observe the fully-constructed io_context and its
  // worker threads behind it.
  NetworkThreadPool* pool = instance_.load(std::memory_order_acquire);

  if (pool == nullptr) {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    // Re-check under the lock: another thread may have created it while we
    // were waiting.  Relaxed is enough here - the mutex already orders us.
    pool = instance_.load(std::memory_order_relaxed);
    if (pool == nullptr) {
      pool = new NetworkThreadPool(maxthreads);
      instance_.store(pool, std::memory_order_release);
      return *pool;
    }
  }

  // The pool already existed, so maxthreads had no effect.  Say so rather than
  // letting it read as a working knob.
  if (maxthreads != 0 && maxthreads != pool->GetThreadCount()) {
    if (auto logger = CLog::GetLogger(log_type::NETWORK).lock()) {
      logger->warn(
          "NetworkThreadPool::GetInstance({}) ignored: the pool already exists "
          "and runs {} threads. Only the first call sizes the pool.",
          maxthreads, pool->GetThreadCount());
    }
  }

  return *pool;
}

void NetworkThreadPool::DeleteInstance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  delete instance_.exchange(nullptr, std::memory_order_acq_rel);
}

} // namespace Core
