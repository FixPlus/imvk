#pragma once
#include "imvk/base/Shader.hpp"
#include "imvk/base/Swapchain.hpp"
#include "vkw/Device.hpp"

namespace imvk {

struct ContextCreateInfo {
  /// The device that this context will be using to do all jobs.
  /// There are some required extensions expected to be present:
  ///    VK_KHR_Swapchain
  /// Additional extensions may be passed that may improve capabilities
  /// of this context but are not required to run:
  ///    TBD
  ///
  /// There is expected to be at least one universal queue that could be used
  /// for graphics, transfer and compute commands. Additional queues may be
  /// provided which could improve capabilities of context but usually not
  /// required.
  std::reference_wrapper<vkw::Device> device;

  /// Shader factory is used to fetch shader modules using string as a key. User
  /// must provide their implementation of this interface.
  std::reference_wrapper<ShaderFactory> shaderFactory;
};

struct GraphicsEngineCreateInfo {
  /// Swapchain factory is used to create and maintain internal swapchain. User
  /// must provide their implementation of this interface. Pass null for no
  /// swapchain. Without swapchain engine won't be able to perform present
  /// operations.
  SwapchainFactory *swapchainFactory;

  /// Number of frames in flight to allocate resources to. Pass 0 for auto.
  unsigned maxFramesInFlight;
};

struct ComputeEngineCreateInfo {
  // TODO
};

struct CopyEngineCreateInfo {
  // TODO
};

class ContextImpl;

class GraphicsEngine;
class ComputeEngine;
class CopyEngine;

template <typename T> using EngineHandle = std::unique_ptr<T>;

class Context {
public:
  /// Basic context that controls operation of all engines.
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

  Context(const ContextCreateInfo &CI);

  /// Graphics engine is used to render and present images using swapchain.
  /// It supports all types of operation including compute and transfer.
  EngineHandle<GraphicsEngine>
  createGraphicsEngine(const GraphicsEngineCreateInfo &CI);

  /// Compute engine is used to perform compute operations. It also supports
  /// transfer. It is suitable for compute tasks that are not directly used
  /// by graphics pipeline.
  EngineHandle<ComputeEngine>
  createComputeEngine(const ComputeEngineCreateInfo &CI);

  /// Copy engine is used for data transfer. It is suitable for background
  /// data streaming operations asynchronous to graphics pipeline operations.
  EngineHandle<CopyEngine> createCopyEngine(const CopyEngineCreateInfo &CI);

  virtual ~Context();

private:
  std::unique_ptr<ContextImpl> m_pimpl;
};

} // namespace imvk