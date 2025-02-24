#include "imvk/base/Frame.hpp"
#include "imvk/base/DescriptorSet.hpp"
#include "imvk/base/Primitive.hpp"

namespace imvk {

Frame::Frame(FramedEngine &engine, unsigned id)
    : m_engine(engine), m_id(id), m_commandBuffer(engine.commandPool()),
      m_registeredObjects(100) {}

void Frame::begin() {
  // garbage collect objects
  m_toBeDeleted.clear();
  for (auto &&[i, pair] : m_registeredObjects.items()) {
    auto &&[pObj, used] = pair;
    if (used) {
      used = false;
      continue;
    }
    pObj->m_regiterForFrame(*this, 0u);
    m_toBeDeleted.emplace_back(i);
  }

  for (auto &&i : m_toBeDeleted)
    m_registeredObjects.erase(i);

  m_commandBuffer.reset(0);
  m_commandBuffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
}

void Frame::use(const std::shared_ptr<FrameObject> &pObject) const {
  auto &object = *pObject;
  if (!object.m_getID(*this)) {
    auto &&[index, pair] = m_registeredObjects.emplace();
    object.m_regiterForFrame(*this, index);
    pair.first = pObject;
    pair.second = true;
    return;
  }
  auto index = object.m_getID(*this);
  assert(m_registeredObjects.contains(index));
  auto &&[pObj, used] = m_registeredObjects.at(index);
  assert(pObj == pObject);
  used = true;
}

void Frame::end() { m_commandBuffer.end(); }

void Frame::terminate() {
  m_registeredObjects.clear();
  m_toBeDeleted.clear();
}

Frame::~Frame() = default;
} // namespace imvk