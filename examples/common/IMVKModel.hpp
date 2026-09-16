#pragma once

#include "IMVKPipeline.hpp"
#include "tiny_gltf_v3.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string_view>

namespace imvk {
class CopyEngine;
}

namespace imvk::examples {
class GLTFModel {
public:
  class Materialized {
  public:
    Materialized(Materialized &&) noexcept;
    Materialized &operator=(Materialized &&) noexcept;
    ~Materialized();

    Materialized(const Materialized &) = delete;
    Materialized &operator=(const Materialized &) = delete;

    void draw(PipelineManager &pipelineManager,
              vkw::RenderPassRecorder &commands, const Frame &frame,
              StageSet<ProjectionStage> projection,
              StageSet<LightingStage> lighting) const;

  private:
    struct Impl;
    explicit Materialized(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
    friend class GLTFModel;
  };

  GLTFModel(std::string_view name);
  Materialized materialize(GraphicsEngine &engine, CopyEngine &copyEngine,
                           ShaderLoader &shaderLoader) const;

private:
  void m_init(std::span<const uint8_t> data,
              const std::filesystem::path &sourcePath);
  tinygltf3::Model m_handle;
  std::filesystem::path m_sourcePath;
};
} // namespace imvk::examples