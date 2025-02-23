#include "imvk/base/ContextImpl.hpp"
#include "imvk/compute/Engine.hpp"
#include "imvk/copy/Engine.hpp"
#include "imvk/graphics/Engine.hpp"
#include <iostream>


namespace imvk {

Context::~Context() = default;
Context::Context(const ContextCreateInfo &CI) : m_pimpl(new ContextImpl{CI}) {}

EngineHandle<GraphicsEngine>
Context::createGraphicsEngine(const GraphicsEngineCreateInfo &CI) {
  return std::make_unique<GraphicsEngine>(*m_pimpl, CI);
}

EngineHandle<ComputeEngine>
Context::createComputeEngine(const ComputeEngineCreateInfo &CI) {
  // TODO
  return nullptr;
}

EngineHandle<CopyEngine>
Context::createCopyEngine(const CopyEngineCreateInfo &CI) {
  // TODO
  return nullptr;
}

} // namespace imvk