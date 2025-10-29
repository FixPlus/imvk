#pragma once

#include "imvk/base/Context.hpp"

#include "imvk/base/Frame.hpp"
#include "vkw/CommandPool.hpp"
#include "vkw/CommandRecorder.hpp"
#include "vkw/Fence.hpp"

#include <future>
#include <queue>

namespace imvk {

/// @brief Engine base is common base class for any engine. It allocates
/// a queue and command pool for this queue to be used for submitting
/// work recorded by engine. Engine implementation is expected to not
/// use any other queues.
class EngineBase {
private:
  struct OneTimeSubmit {
    OneTimeSubmit(EngineBase &engine)
        : pool{engine.m_context.get().device(),
               VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
               engine.m_queue.get().acquire().get().family().index()},
          buffer{pool}, fence{engine.m_context.get().device()} {}
    vkw::CommandPool pool;
    vkw::PrimaryCommandBuffer buffer;
    vkw::Fence fence;
  };

public:
  /// @brief  EngineBase constructor
  /// @param ctx reference to context this engine will be operating within.
  /// @param queueInfo create info for engine queue.
  EngineBase(Context &ctx, const QueueCapsInfo &queueInfo)
      : m_context(ctx), m_queue(m_context.get().allocateQueue(queueInfo)),
        m_commandPool(ctx.device(),
                      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                      m_queue.get().acquire().get().family().index()){};

  Context &context() const { return m_context; }
  auto &commandPool() { return m_commandPool; }
  const auto &commandPool() const { return m_commandPool; }

  std::future<void> oneTimeSubmit(auto &&recorder) {

    OneTimeSubmit submitContext{*this};
    {
      vkw::BufferRecorder rcd{submitContext.buffer,
                              VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};

      std::invoke(recorder, rcd);
    }
    vkw::SubmitInfo submitInfo{submitContext.buffer};
    m_queue.get().acquire().get().submit(submitInfo, submitContext.fence);

    return std::async(std::launch::deferred,
                      [submitContext = std::move(submitContext)]() mutable {
                        submitContext.fence.wait();
                      });
  }

  virtual ~EngineBase() { m_context.get().freeQueue(m_queue); }

protected:
  /// @brief Only engine implementation is expected to have access to the queue.
  /// @return queue
  Queue &queue() const { return m_queue; }

private:
  std::reference_wrapper<Context> m_context;
  std::reference_wrapper<Queue> m_queue;
  vkw::CommandPool m_commandPool;
};

class Frame;

/// @brief Implements a common interface for frame-based engines.
/// It allows to allocate resources on a per-frame basis and manage
/// switching of frames.
class FramedEngine : public EngineBase {
public:
  /// @brief  FramedEngine constructor
  /// @param ctx context passed to EngineBase
  /// @param queueInfo queueInfo passed to EngineBase
  /// @param frameInFlightCount count of expected frames in flight for this
  /// engine.
  FramedEngine(Context &ctx, const QueueCapsInfo &queueInfo,
               unsigned frameInFlightCount);

  auto getFIFCount() const { return m_frames.size(); }

protected:
  void terminate();

  struct FrameRecorder {
    vkw::BufferRecorder recorder;
    std::reference_wrapper<Frame> frame;
  };

  std::optional<std::future<FrameRecorder>> nextFrame() {
    if (m_frameQueue.empty())
      return std::nullopt;
    auto nextId = m_frameQueue.front();
    auto &next = m_frames.at(nextId);
    m_frameQueue.pop();
    return std::async(std::launch::deferred, [&next]() {
      next.waitFence.get();
      return m_beginFrameImpl(*next.frame);
    });
  }

  template <typename Fn = void (*)(vkw::SubmitInfo &),
            typename CallbackFn = void (*)(void)>
  void submitFrame(
      FrameRecorder &&recorder, Fn &&amendSubmitInfo = [](vkw::SubmitInfo &) {},
      CallbackFn &&afterCompletionCallback = []() {}) {
    auto &frame = recorder.frame.get();
    { auto endFrame = std::move(recorder); }
    auto id = frame.id();
    assert(id < m_frames.size());
    auto &frameInfo = m_frames.at(id);
    vkw::SubmitInfo info;
    info.addCommands(frame.commands());
    std::invoke(amendSubmitInfo, info);
    queue().acquire().get().submit(info, frameInfo.fence);
    frameInfo.waitFence =
        std::async(std::launch::deferred,
                   [&frameInfo, afterCompletionCallback = std::move(
                                    afterCompletionCallback)]() mutable {
                     frameInfo.fence.wait();
                     frameInfo.fence.reset();
                     std::invoke(afterCompletionCallback);
                   });
    m_frameQueue.push(id);
  }

private:
  static FrameRecorder m_beginFrameImpl(Frame &frame);
  struct FrameInfo {
    std::unique_ptr<Frame> frame;
    vkw::Fence fence;
    std::future<void> waitFence;
  };
  std::vector<FrameInfo> m_frames;
  std::queue<unsigned> m_frameQueue;
};

} // namespace imvk