#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace fif {

template <typename T>
class BoundedDropOldQueue {
 public:
  explicit BoundedDropOldQueue(std::size_t capacity) : capacity_(capacity) {}

  void push(T item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
      return;
    }
    if (queue_.size() >= capacity_) {
      queue_.pop_front();
      ++dropped_;
    }
    queue_.push_back(std::move(item));
    cv_.notify_one();
  }

  std::optional<T> pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
      return std::nullopt;
    }
    T item = std::move(queue_.front());
    queue_.pop_front();
    return item;
  }

  void close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    cv_.notify_all();
  }

  [[nodiscard]] std::size_t dropped_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
  }

  [[nodiscard]] std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<T> queue_;
  bool closed_ = false;
  std::size_t dropped_ = 0;
};

}  // namespace fif
