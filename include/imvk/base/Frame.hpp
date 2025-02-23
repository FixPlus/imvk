#pragma once
#include "imvk/base/EngineBase.hpp"
#include "vkw/CommandBuffer.hpp"
#include "vkw/CommandPool.hpp"


namespace imvk {

class ContextImpl;

class Frame final {
public:
  Frame(FramedEngine &engine, unsigned id);

  void begin();

  void end();

  FramedEngine &engine() const { return m_engine; }

  const auto &id() const { return m_id; }

  vkw::PrimaryCommandBuffer &commands() const { return m_commandBuffer; }

private:
  FramedEngine &m_engine;
  unsigned m_id;
  mutable vkw::PrimaryCommandBuffer m_commandBuffer;
};

} // namespace imvk