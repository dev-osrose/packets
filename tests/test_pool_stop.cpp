// NetworkThreadPool::Stop(): after it returns no completion handler runs, so
// sockets (and anything their handlers point at) can be destroyed safely
// before DeleteInstance(). A client that freed its sockets while the pool
// still ran had a cancelled read's handler run on the freed socket at exit.
#include "network_thread_pool.h"

#include <gtest/gtest.h>

#include <asio/post.hpp>
#include <asio/steady_timer.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace std::chrono_literals;

TEST(PoolStop, WaitsForTheHandlerRunningNow) {
  auto& pool = Core::NetworkThreadPool::GetInstance(2);
  std::atomic<bool> started{false}, finished{false};
  asio::post(*pool.Get_IO_Service(), [&] {
    started = true;
    std::this_thread::sleep_for(200ms);
    finished = true;
  });
  while (!started) std::this_thread::yield();
  pool.Stop();
  EXPECT_TRUE(finished) << "Stop() returned while a handler still ran";
}

TEST(PoolStop, ACancelledWaitNeverRunsAfterward) {
  auto& pool = Core::NetworkThreadPool::GetInstance(2);
  auto ran   = std::make_shared<std::atomic<bool>>(false);
  {
    asio::steady_timer timer(*pool.Get_IO_Service(), 1h);
    timer.async_wait([ran](const asio::error_code&) { *ran = true; });
    pool.Stop();
  } // the timer dies here: its wait is cancelled, as a socket's read would be
  std::this_thread::sleep_for(100ms);
  EXPECT_FALSE(*ran) << "a handler ran after Stop()";
}

TEST(PoolStop, IsSafeTwiceAndBeforeDelete) {
  auto& pool = Core::NetworkThreadPool::GetInstance(2);
  pool.Stop();
  pool.Stop();
  Core::NetworkThreadPool::DeleteInstance();
}
