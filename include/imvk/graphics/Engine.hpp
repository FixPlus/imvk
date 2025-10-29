#pragma once

#include "imvk/base/EngineBase.hpp"
#include "imvk/base/Frame.hpp"

#include <vkw/CommandPool.hpp>
#include <vkw/Fence.hpp>
#include <vkw/Semaphore.hpp>
#include <vkw/Surface.hpp>
#include <vkw/SwapChain.hpp>

#include <functional>

namespace imvk {

class Swapchain;

/// @brief abstracts away external surface and swapchain creation.
class SwapchainFactory {
public:
  using RecreateCallbackType = void (*)(void);
  /// @brief returns reference to VkSwapchainCreateInfoKHR object which is
  /// prefilled with information needed to construct a swapchain object.
  ///
  /// Returned reference must remain valid until subsequent getCreateInfo()
  /// call. pNext, imageUsage, imageSharingMode, queueFamilyIndexCount,
  /// pQueueFamilyIndices fields are not used.
  /// This may throw if given device cannot present to selected surface.
  virtual const VkSwapchainCreateInfoKHR &
  getCreateInfo(vkw::Device &device) = 0;

  /// @brief returns a reference to surface the swapchain is being created on.
  virtual vkw::Surface &getSurface() noexcept = 0;

  /// @brief sets a callback for manual swapchain recreation.
  /// This callback should be called if factory decides forcibly
  /// recreate swapchain.
  /// IMPORTANT: caller of callback must be externally synchronized with
  /// swapchain producer entity (which in most cases is GraphicsEngine).
  virtual void setRecreateCallback(RecreateCallbackType callback) noexcept = 0;

  virtual ~SwapchainFactory() = default;
};

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

/// @brief Graphics engine is used to render and present images using
/// swapchain. It supports all types of operation including compute and
/// transfer. Implements FramedEngine for swapchain image presenting sequence.
class GraphicsEngine : public FramedEngine {
private:
  struct Terminator {
    void operator()(GraphicsEngine *engine) { engine->terminate(); }
  };
  struct FrameSyncObjects final {
    FrameSyncObjects(GraphicsEngine &engine);
    vkw::Semaphore renderComplete, presentComplete;
  };

public:
  class SwapFrame {
  public:
    SwapFrame(GraphicsEngine &engine, FramedEngine::FrameRecorder &&frame,
              std::unique_ptr<FrameSyncObjects> &&semas)
        : m_engine(&engine, FrameEnder{engine.swapchain(), std::move(semas),
                                       std::move(frame)}){};

    /// @brief wrappers over frame methods.
    GraphicsEngine &engine() const { return *m_engine; }
    const auto &id() const { return frame().id(); }
    const Frame &frame() const { return m_recorder().frame.get(); }
    void use(const std::shared_ptr<FrameObject> &object) const {
      frame().use(object);
    }
    vkw::BufferRecorder &commands() { return m_recorder().recorder; }

    const auto &swapchain() const {
      return m_engine.get_deleter().swapchain.get();
    }

  private:
    friend class GraphicsEngine;
    FramedEngine::FrameRecorder &m_recorder() const {
      return m_engine.get_deleter().recorder;
    }
    struct FrameEnder {
      FrameEnder(const Swapchain &swapchain,
                 std::unique_ptr<FrameSyncObjects> &&semas,
                 FramedEngine::FrameRecorder &&frame)
          : swapchain(swapchain), semas(std::move(semas)),
            recorder(std::move(frame)) {}
      void operator()(GraphicsEngine *engine) const;

      std::reference_wrapper<const Swapchain> swapchain;
      mutable std::unique_ptr<FrameSyncObjects> semas;
      mutable FramedEngine::FrameRecorder recorder;
    };
    std::unique_ptr<GraphicsEngine, FrameEnder> m_engine;
    ;
  };

  using FrameT = SwapFrame;

  GraphicsEngine(Context &context, const GraphicsEngineCreateInfo &CI);

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
    }
  }

  const Swapchain &swapchain() const { return *m_swapchain; }

  ~GraphicsEngine() override;

private:
  friend class SwapFrame::FrameEnder;
  std::unique_ptr<FrameSyncObjects> m_getSemaphores();
  void m_returnSemaphores(std::unique_ptr<FrameSyncObjects> &&semas);

  void m_recreate_swapchain();
  bool m_surface_minimized();

  std::optional<SwapFrame> m_beginFrame();

  SwapchainFactory &m_swapchainFactory;
  std::unique_ptr<Swapchain> m_swapchain;
  std::vector<std::unique_ptr<FrameSyncObjects>> m_frameSyncs;
  std::vector<std::pair<std::function<void(void)>,
                        std::function<void(const Swapchain &)>>>
      m_swapChainCallbacks;
};

} // namespace imvk