#include "imvk/base/Frame.hpp"

namespace imvk {

Frame::Frame(FramedEngine &engine, unsigned id)
    : m_engine(engine), m_id(id), m_commandBuffer(engine.commandPool()) {}

void Frame::begin() {
  m_commandBuffer.reset(0);
  m_commandBuffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
}

void Frame::end() { m_commandBuffer.end(); }

} // namespace imvk