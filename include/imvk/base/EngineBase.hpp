#pragma once

#include "imvk/base/Context.hpp"
#include "imvk/base/Frame.hpp"

#include "vkw/CommandPool.hpp"
#include "vkw/CommandRecorder.hpp"
#include "vkw/DescriptorSet.hpp"
#include "vkw/Fence.hpp"

#include <future>
#include <queue>
#include <typeindex>
#include <unordered_map>

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
                      m_queue.get().acquire().get().family().index()),
        m_dummy(ctx.device()){};

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
  const vkw::DescriptorSetLayout &dummyDescriptorSetLayout() const {
    return m_dummy;
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
  vkw::DescriptorSetLayout m_dummy;
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

  auto totalNodeCount() const { return m_nodeCounter; }
  /// @brief Creates new object node of specified type T. Operations with nodes
  /// are not internally synchronized, therefore this function is not
  /// thread-safe.
  /// @tparam T is a type of node to create. Must be derived from FONodeBase.
  /// @param args to pass to T's constructor.
  /// @return a shared reference to instance of T.
  template <std::derived_from<FONodeBase> T, typename... Args>
  Ref<T> createNode(Args &&...args) {
    ++m_nodeCounter;
    return new T(*this, std::forward<Args>(args)...);
  }

  void submitFrame(auto &&frameRecord) {
    auto &nextFrame = getNextFrame();
    auto submitOpt = std::invoke(
        std::forward<decltype(frameRecord)>(frameRecord), nextFrame.frame());
    if (submitOpt && nextFrame.status() == FrameInfo::stat::recd) {
      submit(*std::move(submitOpt), nextFrame);
      postSubmit(nextFrame.frame());
    }
  }

  void flush();

  ~FramedEngine() override;

protected:
  virtual void postSubmit(const Frame &frame) = 0;

private:
  class FreeQueueBase {
  public:
    virtual bool empty() = 0;
    virtual FrameID nextElementTag() = 0;
    virtual void popNext() = 0;
    virtual void reset() = 0;
    virtual ~FreeQueueBase() = default;
  };

  template <typename T> class FreeQueue : public FreeQueueBase {
  public:
    bool empty() override { return m_current == m_list.size(); }
    FrameID nextElementTag() override { return m_list[m_current]->second; }
    void popNext() override { m_list[m_current++].reset(); }
    void reset() override {
      m_current = 0;
      m_list.clear();
    }
    void destroy(FObject<T> &&obj) { m_list.emplace_back(std::move(obj)); }

  private:
    ptrdiff_t m_current = 0;
    std::vector<std::optional<FObject<T>>> m_list;
  };

  class FreeList {
  public:
    template <typename T> void destroy(FObject<T> &&obj) {
      auto &freeQueue = m_queues.try_emplace(typeid(T), nullptr).first->second;
      if (!freeQueue)
        freeQueue = std::make_unique<FreeQueue<T>>();
      m_list.push_back(typeid(T));
      static_cast<FreeQueue<T> &>(*freeQueue).destroy(std::move(obj));
    }
    bool empty() { return m_current == m_list.size(); }
    FrameID nextElementTag() {
      return m_queues[m_list[m_current]]->nextElementTag();
    }
    void popNext() { m_queues[m_list[m_current++]]->popNext(); }
    void reset() {
      m_current = 0;
      m_list.clear();
      for (auto &&q : m_queues | std::views::elements<1>) {
        q->reset();
      }
    }

  private:
    ptrdiff_t m_current = 0;
    std::vector<std::type_index> m_list;
    std::unordered_map<std::type_index, std::unique_ptr<FreeQueueBase>>
        m_queues;
  };

public:
  /// @brief enqueues object in free list. Objects are freed strictly in order
  /// they were enqueued and only after last frame they were used in is retired.
  /// @param object pointer to FObject instance to destroy.
  template <typename T> void destroyObject(FObject<T> &&object) {
    m_freeList.destroy<T>(std::move(object));
  }

private:
  class GarbageCollector final {
  public:
    GarbageCollector(FramedEngine &engine)
        : m_engine(engine), m_thread(gcLoopProxy, std::ref(*this)) {}

    void retireFrame(FrameID frame) {
      std::unique_lock lc{m_listMutex};
      m_engine.m_retired.store(frame, std::memory_order::relaxed);
      m_engine.m_gcWaitingFor.store(0, std::memory_order::release);
      m_idle = false;
      lc.unlock();
      m_waker.notify_one();
    }

    void trySubmit(FreeList &nextList) {
      std::unique_lock lc{m_listMutex};
      if (!m_pendingList.empty())
        return;
      m_idle = false;
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
    ~GarbageCollector() {
      std::unique_lock lc{m_listMutex};
      m_thread.request_stop();
      lc.unlock();
      m_waker.notify_one();
    }

  private:
    static void gcLoopProxy(std::stop_token token, GarbageCollector &gc) {
      gc.gcLoop(token);
    }
    void gcLoop(std::stop_token token);

    FramedEngine &m_engine;
    std::mutex m_listMutex;
    std::condition_variable m_waker;
    std::condition_variable m_idleWaker;
    bool m_idle = false;

    FreeList m_currentList;
    FreeList m_pendingList;
    std::jthread m_thread;
  };

  class FrameInfo final {
  public:
    enum class stat { init, recd, subd };
    FrameInfo(FramedEngine &e, FrameID index)
        : m_frame(e, index),
          m_fence(e.context().device(), /* create signaled */ true),
          m_status(stat::init) {}
    FrameInfo(FrameInfo &&another)
        : m_frame(std::move(another.m_frame)),
          m_fence(std::move(another.m_fence)),
          m_status(std::exchange(another.m_status, stat::init)) {}
    FrameInfo &operator=(FrameInfo &&another) {
      if (this == &another)
        return *this;
      std::swap(m_frame, another.m_frame);
      std::swap(m_fence, another.m_fence);
      std::swap(m_status, another.m_status);
      return *this;
    }
    ~FrameInfo() { reset(); }
    void reset() {
      if (m_status == stat::init)
        return;
      if (m_status == stat::subd) {
        m_fence.wait();
        m_fence.reset();
      } else {
        // we have no direct way to set fence in signaled state
        m_fence = vkw::Fence(m_frame.engine().context().device(),
                             /* create signaled */ true);
      }
      m_status = stat::init;
    }

    void record(FrameID ordinal) {
      assert(m_status == stat::init);
      m_fence.reset();
      m_frame.ordinal() = ordinal;
      m_status = stat::recd;
    }

    void submit(vkw::Queue &queue, vkw::SubmitInfo &si) {
      assert(m_status == stat::recd);
      queue.submit(si, m_fence);
      m_status = stat::subd;
    }

    const Frame &frame() const { return m_frame; }
    bool isRetired() const { return m_status == stat::init; }
    stat status() const { return m_status; }

    static FrameInfo &waitAny(auto &&frames) {
      for (auto &&frame : frames)
        frame.retireIfSignaled();
      auto retiredPred = [](const FrameInfo &frame) {
        return frame.isRetired();
      };
      auto foundRetired = std::ranges::find_if(frames, retiredPred);
      if (foundRetired != std::end(frames))
        return *foundRetired;

      auto submitted = frames | std::views::filter([](const FrameInfo &frame) {
                         return frame.m_status == stat::subd;
                       });
      auto fences =
          submitted | std::views::transform(
                          [](const FrameInfo &frame) -> const vkw::Fence & {
                            return frame.m_fence;
                          });
      if (!std::ranges::empty(fences))
        vkw::Fence::wait_any(std::begin(fences), std::end(fences));
      for (auto &&frame : frames)
        frame.retireIfSignaled();
      foundRetired = std::ranges::find_if(frames, retiredPred);
      assert(foundRetired != std::end(frames));
      return *foundRetired;
    }

    static void waitAll(auto &&frames) {
      auto submitted = frames | std::views::filter([](const FrameInfo &frame) {
                         return frame.m_status == stat::subd;
                       });
      auto fences =
          submitted | std::views::transform(
                          [](const FrameInfo &frame) -> const vkw::Fence & {
                            return frame.m_fence;
                          });
      if (!std::ranges::empty(fences))
        vkw::Fence::wait_all(std::begin(fences), std::end(fences));
      for (auto &&frame : frames)
        frame.retireIfSignaled();
    }

  private:
    void retireIfSignaled() {
      if (m_status != stat::subd)
        return;
      if (m_fence.signaled())
        m_status = stat::init;
    }
    Frame m_frame;
    vkw::Fence m_fence;
    stat m_status;
  };

  FrameInfo &getNextFrame();

  void submit(vkw::SubmitInfo &&info, FrameInfo &frame) {
    frame.submit(queue().acquire().get(), info);
    if (!m_freeList.empty())
      m_gc.trySubmit(m_freeList);
  }
  std::vector<FrameInfo> m_frames;
  FreeList m_freeList;
  FrameID m_ordinal = 0;
  FrameID m_retiredPrivate = 0;
  std::atomic<FrameID> m_retired = 0;
  std::atomic<FrameID> m_gcWaitingFor = 0;
  GarbageCollector m_gc;
  size_t m_nodeCounter = 0;
};

} // namespace imvk