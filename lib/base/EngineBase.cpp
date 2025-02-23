#include "imvk/base/EngineBase.hpp"
#include "imvk/base/Frame.hpp"

namespace imvk {

FramedEngine::FramedEngine(ContextImpl &ctx, const QueueCapsInfo &queueInfo,
                           unsigned frameInFlightCount)
    : EngineBase(ctx, queueInfo), m_frameInFlightCount(frameInFlightCount),
      m_dynamicFIFCount(frameInFlightCount) {
  m_frames.reserve(frameInFlightCount);
  std::ranges::transform(std::ranges::iota_view{0u, frameInFlightCount},
                         std::back_inserter(m_frames), [this](auto &&i) {
                           return std::make_unique<Frame>(*this, i);
                         });
}

void FramedEngine::setDynamicFIFCount(unsigned count) {
  assert(count <= m_frameInFlightCount);
  m_dynamicFIFCount = m_frameInFlightCount;
  if (m_currentFrame >= m_dynamicFIFCount)
    m_currentFrame = 0u;
}

void FramedEngine::endAndAdvanceFrame() {
  m_frames.at(m_currentFrame)->end();
  m_currentFrame = (m_currentFrame + 1u) % m_dynamicFIFCount;
}

const Frame &FramedEngine::beginAndGetCurrentFrame() const {
  Frame &frame = *m_frames.at(m_currentFrame);
  frame.begin();
  return frame;
}

} // namespace imvk