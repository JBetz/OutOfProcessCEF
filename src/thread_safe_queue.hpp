#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

template <typename T>
class ThreadSafeQueue {
 public:
  ThreadSafeQueue() = default;
  ~ThreadSafeQueue() = default;

  // Push item into queue (non-blocking)
  void push(const T& val) {
    {
      std::lock_guard<std::mutex> lock(mtx);
      q.push(val);
    }
    cv.notify_one();
  }

  void push(T&& val) {
    {
      std::lock_guard<std::mutex> lock(mtx);
      q.push(std::move(val));
    }
    cv.notify_one();
  }

  // Blocking pop: waits until an item is available
  T pop() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this] { return !q.empty(); });
    T val = std::move(q.front());
    q.pop();
    return val;
  }

  // Non-blocking pop, returns false if queue was empty
  bool try_pop(T& out) {
    std::lock_guard<std::mutex> lock(mtx);
    if (q.empty()) {
      return false;
    }
    out = std::move(q.front());
    q.pop();
    return true;
  }

 private:
  std::mutex mtx;
  std::condition_variable cv;
  std::queue<T> q;
};

// Per-request synchronization entry
struct ResponseEntry {
  std::mutex mutex;
  std::condition_variable cond;
  std::string payload;
  bool ready = false;

  ResponseEntry() = default;
  ~ResponseEntry() = default;
};