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

  auto frameIds() const { return std::ranges::iota_view{0ul, getFIFCount()}; }

  /// @brief Creates new object node of specified type T. Operations with nodes
  /// are not internally synchronized, therefore this function is not
  /// thread-safe.
  /// @tparam T is a type of node to create. Must be derived from FONodeBase.
  /// @param args to pass to T's constructor.
  /// @return a shared reference to instance of T.
  template <std::derived_from<FONodeBase> T, typename... Args>
  Ref<T> createNode(Args &&...args) {
    return new T(*this, std::forward<Args>(args)...);
  }

  /// @brief allocates new object of specified type. This function is safe to
  /// call from any thread. T's constructor therefore must also be internally
  /// thread-safe.
  /// @tparam T is a type of object implementation to create.
  /// @param args passed to constructor of object T.
  /// @return uniquely owned pointer to an instance of FObject.
  template <typename T, typename... Args>
  FObject::Ptr createObject(Args &&...args) {
    return FObject::Ptr{new FObjectImpl<T>(std::forward<Args>(args)...), *this};
  }

  /// @brief enqueues object in free list. Objects are freed strictly in order
  /// they were enqueued and only after last frame they were used in is retired.
  /// This function is called by deleter of FObject::Ptr.
  /// @param object pointer to FObject instance to destroy.
  void destroyObject(FObject *object);

  void run() {
    FrameInfo *nextFrame = nullptr;
    do {
      nextFrame = &getNextFrame();
      submit(onFrame(nextFrame->frame), *nextFrame);
    } while (postSubmit(nextFrame->frame));
  }

  void flush();

  ~FramedEngine() override;

protected:
  virtual vkw::SubmitInfo onFrame(const Frame &frame) = 0;
  virtual bool postSubmit(const Frame &frame) = 0;

private:
  class GarbageCollector {
  public:
    GarbageCollector(FramedEngine &engine)
        : m_engine(engine), m_thread(gcLoopProxy, std::ref(*this)) {}

    void retireFrame(FrameID frame) {
      std::unique_lock lc{m_listMutex};
      m_engine.m_retired.store(frame, std::memory_order::relaxed);
      m_engine.m_gcWaitingFor.store(0, std::memory_order::release);
      lc.unlock();
      m_waker.notify_one();
    }

    void trySubmit(std::vector<FObject *> &nextList) {
      std::unique_lock lc{m_listMutex};
      if (!m_pendingList.empty())
        return;
      std::swap(nextList, m_pendingList);
      lc.unlock();
      m_waker.notify_one();
    }

    void waitIdle() {
      std::unique_lock lc{m_listMutex};
      if (m_idle)
        return;
      m_idleWaker.wait(lc, [this]() { return m_idle; });
    }

  private:
    static void gcLoopProxy(std::stop_token token, GarbageCollector &gc) {
      gc.gcLoop(token);
    }
    void gcLoop(std::stop_token token) {
      FrameID lastRetired = 0;
      while (!token.stop_requested()) {
        if (m_currentListIndex == m_currentList.size()) {
          m_currentListIndex = 0;
          m_currentList.clear();
          std::unique_lock lc{m_listMutex};
          if (!m_pendingList.empty()) {
            std::swap(m_pendingList, m_currentList);
            continue;
          } else {
            m_idle = true;
            m_idleWaker.notify_one();
            m_waker.wait(lc, [this, &token]() {
              return !m_pendingList.empty() || token.stop_requested();
            });
            m_idle = false;
            continue;
          }
        }
        while (m_currentListIndex != m_currentList.size()) {
          auto &next = m_currentList[m_currentListIndex];
          auto nextFrame = next->lastFrame();
          if (nextFrame > lastRetired) {
            std::unique_lock lc{m_listMutex};
            lastRetired = m_engine.m_retired.load(std::memory_order::acquire);
            if (lastRetired >= nextFrame)
              continue;
            m_engine.m_gcWaitingFor.store(nextFrame,
                                          std::memory_order::release);
            m_idle = true;
            m_idleWaker.notify_one();
            m_waker.wait(lc, [&]() {
              lastRetired = m_engine.m_retired.load(std::memory_order::acquire);
              return lastRetired >= nextFrame || token.stop_requested();
            });
            m_idle = false;
            continue;
          }
          delete next;
          ++m_currentListIndex;
        }
      }
      for (auto *obj : m_currentList)
        delete obj;
      std::unique_lock lc{m_listMutex};
      for (auto *obj : m_pendingList)
        delete obj;
    }

    FramedEngine &m_engine;
    std::mutex m_listMutex;
    std::condition_variable m_waker;
    std::condition_variable m_idleWaker;
    bool m_idle = false;

    size_t m_currentListIndex = 0;
    std::vector<FObject *> m_currentList;
    std::vector<FObject *> m_pendingList;
    std::jthread m_thread;
  };
  struct FrameInfo {
    Frame frame;
    vkw::Fence fence;
  };
  FrameInfo &getNextFrame();

  void submit(vkw::SubmitInfo &&info, FrameInfo &frame) {
    queue().acquire().get().submit(info, frame.fence);
    if (!m_freeList.empty())
      m_gc.trySubmit(m_freeList);
  }
  std::vector<FrameInfo> m_frames;
  std::vector<FObject *> m_freeList;
  FrameID m_ordinal = 0;
  FrameID m_retiredPrivate = 0;
  std::atomic<FrameID> m_retired = 0;
  std::atomic<FrameID> m_gcWaitingFor = 0;
  GarbageCollector m_gc;
};

} // namespace imvk