#include "imvk/base/EngineBase.hpp"
#include "imvk/base/Frame.hpp"

namespace imvk {

FramedEngine::FramedEngine(Context &ctx, const QueueCapsInfo &queueInfo,
                           unsigned frameInFlightCount)
    : EngineBase(ctx, queueInfo), m_gc(*this) {
  m_frames.reserve(frameInFlightCount);
  std::ranges::transform(std::ranges::iota_view{0u, frameInFlightCount},
                         std::back_inserter(m_frames), [this, &ctx](auto &&i) {
                           return FrameInfo{*this, i};
                         });
}
FramedEngine::~FramedEngine() { flush(); }

void FramedEngine::flush() {
  FrameInfo::waitAll(m_frames);
  FrameID lastRetired = 0;
  for (auto &&info : m_frames) {
    info.reset();
    if (info.frame().ordinal() > lastRetired)
      lastRetired = info.frame().ordinal();
  }
  assert(lastRetired == m_ordinal);
  queue().acquire().get().waitIdle();
  m_gc.retireFrame(lastRetired);
  m_gc.waitIdle();
  while (!m_freeList.empty()) {
    m_gc.trySubmit(m_freeList);
    m_gc.waitIdle();
  }
}

FramedEngine::FrameInfo &FramedEngine::getNextFrame() {
  FrameInfo::waitAny(m_frames);
  boost::container::small_vector<std::reference_wrapper<FrameInfo>, 3> retired;
  std::ranges::copy_if(m_frames, std::back_inserter(retired),
                       [](auto &&info) { return info.isRetired(); });
  assert(!retired.empty());
  std::sort(retired.begin(), retired.end(), [](auto &&lhs, auto &&rhs) {
    return lhs.get().frame().ordinal() < rhs.get().frame().ordinal();
  });

  auto oldRetired = m_retiredPrivate;
  for (auto &&info : retired) {
    if (m_retiredPrivate + 1 == info.get().frame().ordinal())
      m_retiredPrivate++;
  }
  if (oldRetired != m_retiredPrivate) {
    auto gcWaiting = m_gcWaitingFor.load(std::memory_order::relaxed);
    if (gcWaiting && gcWaiting <= m_retiredPrivate)
      m_gc.retireFrame(m_retiredPrivate);
  }
  auto &ret = retired.front().get();
  ret.record(++m_ordinal);
  if (m_ordinal == 0)
    throw std::runtime_error("Frame count overflow");
  return ret;
}

void FramedEngine::GarbageCollector::gcLoop(std::stop_token token) {
  FrameID lastRetired = 0;
  while (!token.stop_requested()) {
    if (m_currentList.empty()) {
      m_currentList.reset();
      std::unique_lock lc{m_listMutex};
      if (!m_pendingList.empty()) {
        std::swap(m_pendingList, m_currentList);
        continue;
      } else {
        m_idle = true;
        m_idleWaker.notify_one();
        m_waker.wait(lc, [this, &token]() {
          auto ret = !m_pendingList.empty() || token.stop_requested();
          if (!ret && !m_idle) {
            m_idleWaker.notify_one();
            m_idle = true;
          }
          return ret;
        });
        continue;
      }
    }
    while (!m_currentList.empty()) {
      auto nextFrame = m_currentList.nextElementTag();
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
          auto ret = lastRetired >= nextFrame || token.stop_requested();
          if (!ret && !m_idle) {
            m_idleWaker.notify_one();
            m_idle = true;
          }
          return ret;
        });
        continue;
      }
      m_currentList.popNext();
    }
  }
  while (!m_currentList.empty())
    m_currentList.popNext();
  std::unique_lock lc{m_listMutex};
  while (!m_pendingList.empty())
    m_pendingList.popNext();
}

} // namespace imvk