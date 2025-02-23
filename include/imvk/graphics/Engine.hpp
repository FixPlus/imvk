#pragma once

#include "imvk/base/Context.hpp"
#include "imvk/base/EngineBase.hpp"
#include "imvk/graphics/Frame.hpp"

#include "vkw/CommandPool.hpp"
#include "vkw/Fence.hpp"
#include "vkw/Semaphore.hpp"

#include <functional>

namespace imvk {

class Swapchain;
class FrameWithSync;
class SwapFrame;

class GraphicsEngine : public FramedEngine {
private:
  struct Terminator {
    void operator()(GraphicsEngine *engine) { engine->m_terminate(); }
  };

public:
  GraphicsEngine(ContextImpl &context, const GraphicsEngineCreateInfo &CI);

  void addSwapchainCallback(auto &&beforeDestroyCallback,
                            auto &&afterCreateCallback) {
    m_swapChainCallbacks.emplace_back(
        std::forward<decltype(beforeDestroyCallback)>(beforeDestroyCallback),
        std::forward<decltype(afterCreateCallback)>(afterCreateCallback));
  }

  void run(auto &&frameJob, auto &&interFrameJob) {
    std::unique_ptr<GraphicsEngine, Terminator> terminatorGuard{this};
    while (std::invoke(interFrameJob)) {
      auto frame = m_beginFrame();
      if (!frame)
        continue;
      std::invoke(frameJob, *frame);
      m_endFrame();
    }
  }

  const Swapchain &swapchain() const { return *m_swapchain; }

  ~GraphicsEngine() override;

private:
  void m_recreate_swapchain();
  bool m_surface_minimized();
  void m_terminate();

  std::optional<SwapFrame> m_beginFrame();
  void m_endFrame();

  SwapchainFactory &m_swapchainFactory;
  std::unique_ptr<Swapchain> m_swapchain;
  std::vector<FrameSyncObjects> m_frameSyncs;
  std::optional<SwapFrame> m_currentFrame;
  std::vector<std::pair<std::function<void(void)>,
                        std::function<void(const Swapchain &)>>>
      m_swapChainCallbacks;
};

} // namespace imvk