#pragma once

#include "imvk/base/Context.hpp"
#include "imvk/base/Queue.hpp"

#include <unordered_map>

namespace imvk {

struct QueueCapsInfo {
  bool present;
  bool graphics;
  bool compute;
  bool transfer;
};

class ContextImpl {
public:
  auto &device() { return m_device; }
  auto &shaderFactory() { return m_shaderFactory; }
  /// TODO: add queue management.

  /// @brief Hands over one queue that satisfy all required capabilities.
  /// This queue may be already acquired by another engine in which case
  /// lock mechanism is introduces. Context tries to minimize amount of
  /// shared queues by picking queue family that is just enough to satisfy
  /// required capabilities.
  /// IMPORTANT: calls to this procedure must be externally synchronized.
  Queue &allocateQueue(const QueueCapsInfo &queueInfo);

  /// @brief Upon destruction engine must 'free' it's queue which reduces number
  /// of references to it. If it reaches 1 - lock is abolished, if it reaches 0
  /// - queue is freed and is ready to be reallocated again for new engines.
  /// IMPORTANT: calls to this procedure must be externally synchronized.
  void freeQueue(Queue &queue);

private:
  ContextImpl(const ContextCreateInfo &CI);

  friend class Context;
  vkw::Device &m_device;
  ShaderFactory &m_shaderFactory;

  Queue &m_allocateQueue(unsigned queueFamilyIndex, unsigned queueIndex);

  std::unordered_map<Queue *, std::unique_ptr<Queue>> m_queueStorage;
  std::unordered_map<unsigned,
                     std::unordered_map<unsigned, std::pair<Queue *, unsigned>>>
      m_queueMap;
};

} // namespace imvk