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

class Semaphore final : public FONode<vkw::Semaphore, fon_type::swap> {
public:
  Semaphore(FramedEngine &engine)
      : FONode<vkw::Semaphore, fon_type::swap>(engine, [&](auto id) {
          return engine.createObject<vkw::Semaphore>(engine.context().device());
        }) {}

private:
  void onCowExpire(const Frame &frame) override {
    // do nothing
  }
  void onUseAction(const Frame &frame, FObject &obj) override {
    // do nothing
  }
};

class SSemaphore final : public FOENode<vkw::Semaphore, fon_type::ext> {
public:
  SSemaphore(FramedEngine &engine, Swapchain &swapchain);

  const Swapchain &swapchain() const {
    return static_cast<const Swapchain &>(*m_uses.front());
  }

private:
  unsigned getExtIndex(const Frame &frame) const override {
    return swapchain().get().currentImage();
  }

  void constructNew(
      FramedEngine &engine,
      boost::container::small_vector_base<FObject::Ptr> &res) override {
    doConstructNew(engine, std::ranges::size(swapchain().get().images()), res);
  }
  void onUseAction(const Frame &, FObject &obj) final {
    // nothing to do.
  }
  static void
  doConstructNew(FramedEngine &engine, unsigned count,
                 boost::container::small_vector_base<FObject::Ptr> &res);
};

/// @brief Graphics engine is used to render and present images using
/// swapchain. It supports all types of operation including compute and
/// transfer. Implements FramedEngine for swapchain image presenting sequence.
class GraphicsEngine : public FramedEngine {
public:
  class SwapFrame {
  public:
    SwapFrame(GraphicsEngine &engine, FramedEngine::FrameRecorder &&frame,
              SSemaphore &rc, Semaphore &pc)
        : m_engine(&engine,
                   FrameEnder{engine.swapchain(), rc, pc, std::move(frame)}){};

    /// @brief wrappers over frame methods.
    GraphicsEngine &engine() const { return *m_engine; }
    const auto &id() const { return frame().id(); }
    const Frame &frame() const { return m_recorder().frame.get(); }

    vkw::BufferRecorder &commands() { return m_recorder().recorder; }

    const auto &swapchain() const {
      return m_engine.get_deleter().swapchain->get();
    }

  private:
    friend class GraphicsEngine;
    FramedEngine::FrameRecorder &m_recorder() const {
      return m_engine.get_deleter().recorder;
    }
    struct FrameEnder {
      FrameEnder(Swapchain &swapchain, SSemaphore &rc, Semaphore &pc,
                 FramedEngine::FrameRecorder &&frame)
          : swapchain(&swapchain), renderComplete(&rc), presentComplete(&pc),
            recorder(std::move(frame)) {}
      void operator()(GraphicsEngine *engine) const;

      Ref<Swapchain> swapchain;
      Ref<SSemaphore> renderComplete;
      Ref<Semaphore> presentComplete;
      mutable FramedEngine::FrameRecorder recorder;
    };
    std::unique_ptr<GraphicsEngine, FrameEnder> m_engine;
    ;
  };

  using FrameT = SwapFrame;

  GraphicsEngine(Context &context, const GraphicsEngineCreateInfo &CI);

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
    while (std::invoke(interFrameJob)) {
      auto frame = m_beginFrame();
      if (!frame)
        continue;
      std::invoke(frameJob, *frame);
    }
  }

  /// @brief override of similar template in FramedEngine.
  template <std::derived_from<FONodeBase> T, typename... Args>
  Ref<T> createNode(Args &&...args) {
    return new T(*this, std::forward<Args>(args)...);
  }
  const Swapchain &swapchain() const { return *m_swapchain; }
  Swapchain &swapchain() { return *m_swapchain; }

  ~GraphicsEngine() override;

private:
  friend class SwapFrame::FrameEnder;

  void m_recreate_swapchain();
  bool m_surface_minimized();
  FObject::Ptr m_createSwapchain();
  friend class Swapchain;

  std::optional<SwapFrame> m_beginFrame();

  SwapchainFactory &m_swapchainFactory;
  Ref<Swapchain> m_swapchain;
  Ref<SSemaphore> m_renderComplete;
  Ref<Semaphore> m_presentComplete;
  std::optional<FrameRecorder> m_pending;
};

} // namespace imvk