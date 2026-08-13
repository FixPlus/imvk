#include "imvk/base/EngineBase.hpp"
#include "imvk/base/Frame.hpp"

namespace imvk {

FramedEngine::FramedEngine(Context &ctx, const QueueCapsInfo &queueInfo,
                           unsigned frameInFlightCount)
    : EngineBase(ctx, queueInfo) {
  m_frames.reserve(frameInFlightCount);
  std::ranges::transform(
      std::ranges::iota_view{0u, frameInFlightCount},
      std::back_inserter(m_frames), [this, &ctx](auto &&i) {
        return FrameInfo{std::unique_ptr<Frame>(FrameCreator::create(*this, i)),
                         vkw::Fence{ctx.device()},
                         std::async(std::launch::deferred, []() {})};
      });
  for (auto i : std::ranges::iota_view{0u, frameInFlightCount})
    m_frameQueue.push(i);
}
FramedEngine::~FramedEngine() {
  queue().acquire().get().waitIdle();
  for (auto &&frame : m_frames) {
    if (frame.waitFence.valid())
      frame.waitFence.get();
  }
  for (auto &&obj : m_freeList)
    delete obj;
}

FramedEngine::FrameRecorder FramedEngine::m_beginFrameImpl(Frame &frame,
                                                           unsigned ordinal) {
  return FrameRecorder{frame.begin(ordinal), frame};
}
void FramedEngine::destroyObject(FObject *object) {
  m_freeList.push_back(object);
}
} // namespace imvk