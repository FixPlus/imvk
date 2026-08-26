#pragma once

#include "imvk/base/EngineBase.hpp"
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

class Semaphore final : public FONode<vkw::Semaphore, fon_type::swap_mut> {
public:
  Semaphore(FramedEngine &engine)
      : FONode<vkw::Semaphore, fon_type::swap_mut>(engine, [&](auto id) {
          return engine.createObject<vkw::Semaphore>(engine.context().device());
        }) {}

private:
  void onUseAction(const Frame &frame, FObject &obj) override {
    // do nothing
  }
};

class SSemaphore final : public Swapchained<vkw::Semaphore> {
public:
  SSemaphore(FramedEngine &engine, Swapchain &swapchain);

private:
  void onUseAction(const Frame &, FObject &obj) final {
    // nothing to do.
  }

  FObject::Ptr constructOne(FramedEngine &engine, unsigned image) final;
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
  const Swapchain &swapchain() const { return *m_swapchain; }
  Swapchain &swapchain() { return *m_swapchain; }
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
  ~GraphicsEngine() override;

private:
  void postSubmit(const Frame &frame) final;

  bool m_aquireSwapchainImage(const Frame &frame);
  bool m_surface_minimized();

  FObject::Ptr m_createSwapchain();
  friend class Swapchain;

  SwapchainFactory &m_swapchainFactory;
  Ref<Swapchain> m_swapchain;
  Ref<SSemaphore> m_renderComplete;
  Ref<Semaphore> m_presentComplete;
};

} // namespace imvk