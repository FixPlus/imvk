#pragma once
#include "imvk/base/EngineBase.hpp"
#include "imvk/base/Utils.hpp"

#include "vkw/CommandBuffer.hpp"
#include "vkw/CommandPool.hpp"

namespace imvk {

class ContextImpl;
class PrimitiveHandleBase;

class Frame final {
public:
  Frame(FramedEngine &engine, unsigned id);

  void begin();

  void end();

  FramedEngine &engine() const { return m_engine; }

  const auto &id() const { return m_id; }

  void usePrimitive(const std::shared_ptr<PrimitiveHandleBase> &primitive);

  vkw::PrimaryCommandBuffer &commands() const { return m_commandBuffer; }
  ~Frame();

private:
  FramedEngine &m_engine;
  unsigned m_id;
  mutable vkw::PrimaryCommandBuffer m_commandBuffer;
  mutable LinearTable<unsigned,
                      std::pair<std::shared_ptr<PrimitiveHandleBase>, bool>>
      m_registeredPrimitives;
  std::vector<unsigned> m_toBeDeleted;
};

} // namespace imvk