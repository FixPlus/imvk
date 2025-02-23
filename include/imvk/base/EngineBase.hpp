#pragma once

#include "imvk/base/ContextImpl.hpp"
#include "imvk/base/Queue.hpp"

#include "vkw/CommandPool.hpp"

namespace imvk {

class EngineBase {
public:
  /// @class EngineBase
  /// Engine base is common base class for any engine. It allocates
  /// a queue and command pool for this queue to be used for submitting
  /// work recorded by engine. Engine implementation is expected to not
  /// use any other queues.

  /// @brief  EngineBase constructor
  /// @param ctx reference to context this engine will be operating within.
  /// @param queueInfo create info for engine queue.
  EngineBase(ContextImpl &ctx, const QueueCapsInfo &queueInfo)
      : m_context(ctx), m_queue(m_context.allocateQueue(queueInfo)),
        m_commandPool(ctx.device(),
                      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                      m_queue.acquire().get().family().index()){};

  ContextImpl &context() const { return m_context; }
  auto &commandPool() { return m_commandPool; }
  const auto &commandPool() const { return m_commandPool; }

  virtual ~EngineBase() { m_context.freeQueue(m_queue); }

protected:
  /// @brief Only engine implementation is expected to have access to the queue.
  /// @return queue
  Queue &queue() const { return m_queue; }

private:
  ContextImpl &m_context;
  Queue &m_queue;
  vkw::CommandPool m_commandPool;
};

class Frame;

class FramedEngine : public EngineBase {
public:
  /// @class FramedEngine
  /// Implements a common interface for frame-based engines.
  /// It allows to allocate resources on a per-frame basis and manage
  /// switching of frames.

  /// @brief  FramedEngine constructor
  /// @param ctx context passed to EngineBase
  /// @param queueInfo queueInfo passed to EngineBase
  /// @param frameInFlightCount count of expected frames in flight for this
  /// engine.
  FramedEngine(ContextImpl &ctx, const QueueCapsInfo &queueInfo,
               unsigned frameInFlightCount);

  const auto &getFIFCount() const { return m_frameInFlightCount; }

  const auto &getDynamicFIFCount() const { return m_dynamicFIFCount; }

  void setDynamicFIFCount(unsigned count);

protected:
  void endAndAdvanceFrame();
  unsigned getCurrentFrameId() const { return m_currentFrame; }

  const Frame &beginAndGetCurrentFrame() const;

private:
  const unsigned m_frameInFlightCount;
  std::vector<std::unique_ptr<Frame>> m_frames;
  unsigned m_dynamicFIFCount;
  unsigned m_currentFrame = 0;
};

} // namespace imvk