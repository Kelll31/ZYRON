// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <thread>
#include <utility>
#include <vector>

namespace zyron::test {

/// Owns test threads and joins them on destruction, so an exception in the test body cannot terminate the process
/// through a joinable std::thread. (std::jthread is avoided: older libc++ gates it behind an experimental flag.)
class ThreadGroup {
 public:
  ThreadGroup() = default;
  ThreadGroup(const ThreadGroup&) = delete;
  ThreadGroup& operator=(const ThreadGroup&) = delete;
  ~ThreadGroup() { joinAll(); }

  template <class Fn>
  void spawn(Fn&& fn) {
    threads_.emplace_back(std::forward<Fn>(fn));
  }

  void joinAll() {
    for (auto& thread : threads_) {
      if (thread.joinable()) {
        thread.join();
      }
    }
    threads_.clear();
  }

 private:
  std::vector<std::thread> threads_;
};

}  // namespace zyron::test
