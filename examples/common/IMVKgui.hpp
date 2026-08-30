#pragma once

#include "IMVKBuffers.hpp"
#include "IMVKPipeline.hpp"
#include "IMVKWindow.hpp"
#include "imgui.h"

#include <boost/compat/function_ref.hpp>

namespace imvk::examples {

class GUI {
public:
  GUI(Window &window, GraphicsEngine &engine, ShaderLoader &loader);

  template <typename ImageDescriptor>
  std::pair<ImTextureID, StageSet<MaterialStage>>
  addImage(ImageDescriptor &&image) {
    StageSetBuilder<MaterialStage> builder{m_engine, m_materialLayout};
    builder.addDescriptorSet(/* set id */ 3)
        .addDescriptor(std::forward<ImageDescriptor>(image), /* binding id*/ 0);
    auto ret = m_images.emplace_back(std::move(builder));
    return std::make_pair(m_toTexId(ret), ret);
  }

  void gui(boost::compat::function_ref<void(void)> recorder);

  void draw(PipelineManager &pipeMngr, vkw::RenderPassRecorder &commands,
            const imvk::Frame &frame);

private:
  struct VertexInfo
      : public vkw::AttributeBase<vkw::VertexAttributeType::VEC2F,
                                  vkw::VertexAttributeType::VEC2F,
                                  vkw::VertexAttributeType::RGBA8_UNORM>,
        public ImDrawVert {
    VertexInfo(ImDrawVert vert = {}) : ImDrawVert(vert) {}
  };
  void m_createFontTexture(ImFontAtlas &atlas);
  static ImTextureID m_toTexId(StageSet<MaterialStage> set);
  static StageSet<MaterialStage> m_getSetForTex(ImTextureID id);

  void m_fillBuffers(const imvk::Frame &frame);

  GraphicsEngine &m_engine;
  Window &m_window;
  struct ContextDeleter {
    void operator()(ImGuiContext *ctx) const;
  };
  std::unique_ptr<ImGuiContext, ContextDeleter> m_ctx;
  StageSet<GeometryStage> m_geometry;
  StageSet<ProjectionStage> m_proj;
  StageLayout<MaterialStage> m_materialLayout;
  StageSet<LightingStage> m_lighting;
  std::vector<StageSet<MaterialStage>> m_images;
  using VertexBuffer = VertexBuffer<VertexInfo, fon_type::swap_mut>;
  VertexBuffer m_vertices;
  using IndexBuffer = IndexBuffer<VK_INDEX_TYPE_UINT16, fon_type::swap_mut>;
  IndexBuffer m_indices;
};
} // namespace imvk::examples