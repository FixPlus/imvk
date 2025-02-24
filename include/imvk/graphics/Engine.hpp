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

/// @brief Implements FramedEngine for swapchain image presenting sequence.
class GraphicsEngine : public FramedEngine {
private:
  struct Terminator {
    void operator()(GraphicsEngine *engine) { engine->m_terminate(); }
  };

public:
  GraphicsEngine(ContextImpl &context, const GraphicsEngineCreateInfo &CI);

  /// @brief Adds callbacks that are called in event of swapchain recreation.
  /// @param beforeDestroyCallback is called right before current swapchain is
  /// destroyed. May be used for destruction of swapchain-derived resources.
  /// Callback must be void(void) compatible.
  /// @param afterCreateCallback is called right after new swapchain is created.
  /// Reference to new swapchain is passed as the first parameter. May be used
  /// for initialization of swapchain derived resources. Callback must be
  /// void(Swapchain&) compatible.
  void addSwapchainCallback(auto &&beforeDestroyCallback,
                            auto &&afterCreateCallback) {
    m_swapChainCallbacks.emplace_back(
        std::forward<decltype(beforeDestroyCallback)>(beforeDestroyCallback),
        std::forward<decltype(afterCreateCallback)>(afterCreateCallback));
  }

  /// @brief Initiates a swapchain cycle. This cycle involves:
  /// 1. Inter-frame scope - this scope is outside of visible frame scope and
  /// interFrameJob callback is called. If this callback returns false goto 6.
  /// 2. Wait for current frame to finish previous job.
  /// 3. Next swap image acquire. If fails - swapchain is recreated and goto
  /// step 1. If swapchain cannot be recreated due to surface minimization, it
  /// is kept alive and goto 1.
  /// 4. Frame-scope - in this scope frameJob callback is called which may
  /// record work for current frame.
  /// 5. Submit and present - current frame's command buffer and swapchain
  /// present are submitted to queue.
  /// 6. Advance to next frame and goto 1.
  /// 7. Termination - wait for queue idle, free all frame resources and
  /// return.
  ///
  ///  If any uncaught exception reaches scope of run(), step 7 is executed
  ///  before further unwind.
  /// @param frameJob callback that fills frame's commands. Must be
  /// void(imvk::SwapFrame&) compatible.
  /// @param interFrameJob callback that is called outside of frame scope. This
  /// callback issues cycle termination if returns false. Must be bool(void)
  /// compatible.
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
  struct FrameSyncObjects final {
    FrameSyncObjects(GraphicsEngine &engine);
    vkw::Semaphore renderComplete, presentComplete;
    bool needFenceWait = false;
    vkw::Fence fence;
    void waitIfNeeded();
  };

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