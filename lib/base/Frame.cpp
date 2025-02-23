#include "imvk/base/Frame.hpp"
#include "imvk/base/Primitive.hpp"

namespace imvk {

Frame::Frame(FramedEngine &engine, unsigned id)
    : m_engine(engine), m_id(id), m_commandBuffer(engine.commandPool()),
      m_registeredPrimitives(100) {}

void Frame::begin() {
  // garbage collect primitives
  m_toBeDeleted.clear();
  for (auto &&[i, pair] : m_registeredPrimitives.items()) {
    auto &&[pPrim, used] = pair;
    if (used) {
      used = false;
      continue;
    }
    pPrim->setIDforFrame(id(), 0u);
    m_toBeDeleted.emplace_back(i);
  }

  for (auto &&i : m_toBeDeleted)
    m_registeredPrimitives.erase(i);

  m_commandBuffer.reset(0);
  m_commandBuffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
}

void Frame::usePrimitive(
    const std::shared_ptr<PrimitiveHandleBase> &primitive) {
  auto &prim = *primitive;
  if (!prim.getIDforFrame(id())) {
    auto &&[index, pair] = m_registeredPrimitives.emplace();
    prim.setIDforFrame(id(), index);
    pair.first = primitive;
    pair.second = true;
    return;
  }
  auto index = prim.getIDforFrame(id());
  assert(m_registeredPrimitives.contains(index));
  auto &&[pPrim, used] = m_registeredPrimitives.at(index);
  assert(pPrim == primitive);
  used = true;
}

void Frame::end() { m_commandBuffer.end(); }
Frame::~Frame() = default;
} // namespace imvk