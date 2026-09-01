#pragma once
#include "imvk/copy/Engine.hpp"
#include "imvk/graph/Materialization.hpp"
#include "imvk/graph/Nodes.hpp"

#include "IMVKPipeline.hpp"
#include "IMVKShaderLoader.hpp"
#include "IMVKWindow.hpp"

namespace imvk::examples {

class MaterializationEnvironment
    : public imvk::graph::MaterializationEnvironment {
public:
  MaterializationEnvironment(GraphicsEngine &engine, Window &window)
      : imvk::graph::MaterializationEnvironment(engine), m_window(window),
        m_ce(engine.context(), {}), m_pp(engine, /*cache size*/ 100),
        m_sl(engine,
             ShaderLoaderCreateInfo{.shaderDirectory = "assets/shaders"}) {}

  Window &window() const { return m_window; }
  CopyEngine &copyEngine() const { return m_ce; }
  PipelinePool &pipelinePool() const { return m_pp; }
  ShaderLoader &shaderLoader() const { return m_sl; }

private:
  Window &m_window;
  mutable CopyEngine m_ce;
  mutable PipelinePool m_pp;
  mutable ShaderLoader m_sl;
};

} // namespace imvk::examples