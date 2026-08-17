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
} // namespace imvk