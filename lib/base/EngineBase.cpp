#include "imvk/base/EngineBase.hpp"
#include "imvk/base/Frame.hpp"

namespace imvk {

FramedEngine::FramedEngine(Context &ctx, const QueueCapsInfo &queueInfo,
                           unsigned frameInFlightCount)
    : EngineBase(ctx, queueInfo) {
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
  for (auto &&info : m_frames) {
    if (info.fence.signaled())
      info.fence.reset();
  }
  queue().acquire().get().waitIdle();
  for (auto &&obj : m_freeList)
    delete obj;
}

void FramedEngine::destroyObject(FObject *object) {
  m_freeList.push_back(object);
}
} // namespace imvk