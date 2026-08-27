#pragma once

#include "imvk/base/EngineBase.hpp"
#include "imvk/base/Object.hpp"
#include "imvk/graphics/Swapchain.hpp"

#include <vkw/CommandPool.hpp>
#include <vkw/Fence.hpp>
#include <vkw/Semaphore.hpp>

#include <functional>

namespace imvk {

struct GraphicsEngineCreateInfo {
  /// @brief Swapchain factory is used to create and maintain internal
  /// swapchain. User must provide their implementation of this interface. Pass
  /// null for no swapchain. Without swapchain engine won't be able to perform
  /// present operations.
  SwapchainFactory *swapchainFactory;

  /// @brief Number of frames in flight to allocate resources to. Pass 0 for
  /// auto.
  unsigned maxFramesInFlight;
};

class SemaphoreImpl final
    : public FONode<vkw::Semaphore, fon_type::swap_mut, SemaphoreImpl> {
public:
  SemaphoreImpl(FramedEngine &engine)
      : FONode<vkw::Semaphore, fon_type::swap_mut, SemaphoreImpl>(
            engine, [&](auto id) {
              return vkw::Semaphore(engine.context().device());
            }) {}

  void onUseAction(const Frame &frame, vkw::Semaphore &obj) {
    // do nothing
  }
};

class Semaphore : public FONodeView<SemaphoreImpl> {
public:
  Semaphore(auto &&...args)
      : FONodeView<SemaphoreImpl>(std::forward<decltype(args)>(args)...) {}
};

class SSemaphoreImpl final
    : public Swapchained<vkw::Semaphore, SSemaphoreImpl> {
public:
  template <std::convertible_to<Swapchain> T>
  SSemaphoreImpl(GraphicsEngine &engine, T &&swapchain)
      : Swapchained<vkw::Semaphore, SSemaphoreImpl>(
            engine, std::forward<T>(swapchain)) {}

  void onUseAction(const Frame &, vkw::Semaphore &obj) {
    // nothing to do.
  }

  vkw::Semaphore constructOne(FramedEngine &engine, unsigned image);
};

class SSemaphore : public FONodeView<SSemaphoreImpl> {
public:
  SSemaphore(auto &&...args)
      : FONodeView<SSemaphoreImpl>(std::forward<decltype(args)>(args)...) {}
};

/// @brief Graphics engine is used to render and present images using
/// swapchain. It supports all types of operation including compute and
/// transfer. Implements FramedEngine for swapchain image presenting sequence.
class GraphicsEngine : public FramedEngine {
public:
  GraphicsEngine(Context &context, const GraphicsEngineCreateInfo &CI);

  /// @brief override of similar template in FramedEngine.
  template <std::derived_from<FONodeBase> T, typename... Args>
  Ref<T> createNode(Args &&...args) {
    return new T(*this, std::forward<Args>(args)...);
  }
  const Swapchain &swapchain() const { return m_swapchain; }
  void submitFrame(auto &&frameRecord) {
    FramedEngine::submitFrame(
        [&](const Frame &frame) -> std::optional<vkw::SubmitInfo> {
          if (!m_aquireSwapchainImage(frame))
            return std::nullopt;
          auto submitInfo = std::invoke(
              std::forward<decltype(frameRecord)>(frameRecord), frame);
          submitInfo.addWaitCondition(
              m_presentComplete->use(frame),
              VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
          submitInfo.addSignalTo(m_renderComplete->use(frame));
          return submitInfo;
        });
  }

  void setSwapchainUsage(VkImageUsageFlags newUsage) {
    if (m_swapchainUsage != newUsage) {
      m_swapchainUsage = newUsage;
      m_swapchain->destroy(true);
      m_swapchain->construct();
    }
  }
  ~GraphicsEngine() override;

private:
  void postSubmit(const Frame &frame) final;

  bool m_aquireSwapchainImage(const Frame &frame);
  bool m_surface_minimized();

  vkw::SwapChain m_createSwapchain();
  friend class SwapchainImpl;

  SwapchainFactory &m_swapchainFactory;
  VkImageUsageFlags m_swapchainUsage;
  Swapchain m_swapchain;
  SSemaphore m_renderComplete;
  Semaphore m_presentComplete;
};

} // namespace imvk