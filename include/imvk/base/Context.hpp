#pragma once
#include <imvk/base/Queue.hpp>
#include <imvk/base/Utils.hpp>

#include <vkw/Allocation.hpp>
#include <vkw/Device.hpp>
#include <vkw/SPIRVModule.hpp>

namespace imvk {

struct ContextCreateInfo {
  /// TODO: fill this one.
};

struct QueueCapsInfo {
  bool present = false;
  bool graphics = false;
  bool compute = false;
  bool transfer = false;
};

/// @brief Basic context that controls operation of all engines.
///
/// Each engine assumes synchronous operation within itself.
/// If enough device queues are provided each engine will operate
/// on a separate queue. If any 2 engines happen to operate on
/// the same queue - their accesses to that queue are internally
/// synchronized.
/// TODO: On-device inter queue synchronization is not supported yet.
///
/// IMPORTANT: creating of engines must be synchronized, which means no
/// engine that was created prior to creation of a new one must not execute
/// any operations asynchronously during new engine creation. It is advised to
/// create all needed engines upfront to avoid synchronization problems.
/// Same applies to the destruction of engines - it must be done
/// synchronously.
class Context {
public:
  Context(vkw::Device &device, vkw::DeviceAllocator &devAlloc,
          const ContextCreateInfo &CI);

  vkw::Device &device() { return m_device; }

  /// @brief handle to thread-local SPIRVLinkContext.
  vkw::SPIRVLinkContext &linkContext() {
    /// TODO: create with some message consumer.
    return m_linkCtx.get();
  }
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

  vkw::DeviceAllocator &getDeviceAllocator() const {
    return m_deviceAllocator.get();
  }

  virtual ~Context() = default;

private:
  vkw::StrongReference<vkw::Device> m_device;
  std::reference_wrapper<vkw::DeviceAllocator> m_deviceAllocator;

  Queue &m_allocateQueue(unsigned queueFamilyIndex, unsigned queueIndex);

  std::unordered_map<Queue *, std::unique_ptr<Queue>> m_queueStorage;
  std::unordered_map<unsigned,
                     std::unordered_map<unsigned, std::pair<Queue *, unsigned>>>
      m_queueMap;

  PerThreadStorage<vkw::SPIRVLinkContext> m_linkCtx;
};

} // namespace imvk