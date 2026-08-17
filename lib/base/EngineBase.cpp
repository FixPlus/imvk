#include "imvk/base/EngineBase.hpp"
#include "imvk/base/Frame.hpp"

namespace imvk {

FramedEngine::FramedEngine(Context &ctx, const QueueCapsInfo &queueInfo,
                           unsigned frameInFlightCount)
    : EngineBase(ctx, queueInfo), m_gc(*this) {
  m_frames.reserve(frameInFlightCount);
  std::ranges::transform(std::ranges::iota_view{0u, frameInFlightCount},
                         std::back_inserter(m_frames), [this, &ctx](auto &&i) {
                           return FrameInfo{
                               Frame(*this, i),
                               vkw::Fence{ctx.device(), /* signaled */ true}};
                         });
}
FramedEngine::~FramedEngine() { flush(); }

void FramedEngine::flush() {
  auto fences =
      m_frames | std::views::transform([](FrameInfo &info) -> vkw::Fence & {
        return info.fence;
      });
  vkw::Fence::wait_all(std::begin(fences), std::end(fences));
  FrameID lastRetired = 0;
  for (auto &&info : m_frames) {
    if (info.fence.signaled()) {
      info.fence.reset();
      if (info.frame.ordinal() > lastRetired)
        lastRetired = info.frame.ordinal();
    }
  }
  m_gc.retireFrame(lastRetired);
  queue().acquire().get().waitIdle();
  m_gc.waitIdle();
  while (!m_freeList.empty()) {
    m_gc.trySubmit(m_freeList);
    m_gc.waitIdle();
  }
}

FramedEngine::FrameInfo &FramedEngine::getNextFrame() {
  auto fences =
      m_frames | std::views::transform([](FrameInfo &info) -> vkw::Fence & {
        return info.fence;
      });
  vkw::Fence::wait_any(std::begin(fences), std::end(fences));
  boost::container::small_vector<std::reference_wrapper<FrameInfo>, 3> retired;
  std::ranges::copy_if(m_frames, std::back_inserter(retired),
                       [](auto &&info) { return info.fence.signaled(); });
  assert(!retired.empty());
  std::sort(retired.begin(), retired.end(), [](auto &&lhs, auto &&rhs) {
    return lhs.get().frame.ordinal() < rhs.get().frame.ordinal();
  });

  auto oldRetired = m_retiredPrivate;
  for (auto &&info : retired) {
    if (m_retiredPrivate + 1 == info.get().frame.ordinal())
      m_retiredPrivate++;
  }
  if (oldRetired != m_retiredPrivate) {
    auto gcWaiting = m_gcWaitingFor.load(std::memory_order::relaxed);
    if (gcWaiting && gcWaiting <= m_retiredPrivate)
      m_gc.retireFrame(m_retiredPrivate);
  }
  auto &ret = retired.front().get();
  ret.fence.reset();
  ret.frame.ordinal() = m_ordinal++;
  return ret;
}

void FramedEngine::destroyObject(FObject *object) {
  m_freeList.push_back(object);
}

void FramedEngine::GarbageCollector::gcLoop(std::stop_token token) {
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
        m_engine.m_gcWaitingFor.store(nextFrame, std::memory_order::release);
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

} // namespace imvk