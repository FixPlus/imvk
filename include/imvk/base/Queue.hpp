#pragma once

#include "vkw/Queue.hpp"
#include <mutex>

namespace imvk {

class Queue {
public:
  /// Represents a reference to a vkw::Queue object.
  /// Accesses to that object are provided either asynchronously or
  /// synchronously, depending on whether a lock was manually introduced.
  /// Either way all accesses to underlying vkw::Queue must be done using
  /// provided HandedQueue object.
  Queue(vkw::Queue queue, bool sync) : m_queue(std::move(queue)) {
    if (sync)
      m_mutex.emplace();
  }

  /// @brief creates internal mutex if not present.
  /// IMPORTANT: calls to this procedure must be externally synchronized.
  void introduceLock() {
    if (!m_mutex)
      m_mutex.emplace();
  }

  /// @brief destroys internal mutex if present.
  /// IMPORTANT: calls to this procedure must be externally synchronized.
  void giveUpLock() { m_mutex.reset(); }

  /// @brief Scope-based handle for vkw::Queue.
  class HandedQueue {
  public:
    auto &get() const { return m_queue; }

  private:
    HandedQueue(vkw::Queue &queue) : m_queue(queue){};
    HandedQueue(vkw::Queue &queue, std::unique_lock<std::mutex> &&lock)
        : m_queue(queue), m_lock(std::move(lock)){};
    friend class Queue;
    vkw::Queue &m_queue;
    std::optional<std::unique_lock<std::mutex>> m_lock;
  };

  /// @brief Grants access to underlying queue, possibly blocking.
  /// If lock is introduced - locks it and returns reference to a queue being
  /// used. With no lock just returns a queue without blocking.
  HandedQueue acquire() const {
    if (m_mutex)
      return {m_queue, std::unique_lock<std::mutex>{*m_mutex}};
    else
      return {m_queue};
  }

  /// @brief Optionally grants access to underlying queue, never blocking.
  /// If lock is introduced - tries to lock it, if succeed - returns reference
  /// to a queue, returns nullopt otherwise. With no lock just returns a queue
  /// without blocking.
  std::optional<HandedQueue> tryAcquire() const {
    if (m_mutex) {
      auto lock = std::unique_lock<std::mutex>{*m_mutex, std::try_to_lock};
      if (lock)
        return HandedQueue{m_queue, std::move(lock)};
      else
        return std::nullopt;
    } else
      return HandedQueue{m_queue};
  }

private:
  mutable vkw::Queue m_queue;
  mutable std::optional<std::mutex> m_mutex;
};

} // namespace imvk