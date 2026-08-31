#include "IMVKgui.hpp"
#include "IMVKTexture.hpp"

#include "backends/imgui_impl_glfw.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <ranges>

namespace imvk::examples {

GUI::GUI(Window &window, GraphicsEngine &engine, CopyEngine &ce,
         ShaderLoader &loader)
    : m_engine(engine), m_ce(ce), m_window(window),
      m_ctx(ImGui::CreateContext()), m_geometry([&]() {
        StageLayout<GeometryStage> layout{
            engine, loader, "ui",
            std::make_unique<vkw::VertexInputStateCreateInfo<
                vkw::per_vertex<VertexInfo, 0>>>()};
        return StageSet<GeometryStage>{engine, layout};
      }()),
      m_proj([&]() {
        StageLayout<ProjectionStage> layout{engine, loader, "identity"};
        return StageSet<ProjectionStage>{engine, layout};
      }()),
      m_materialLayout(engine, loader, "ui",
                       vkw::RasterizationStateCreateInfo{}),
      m_lighting([&]() {
        VkPipelineColorBlendAttachmentState state{};
        state.blendEnable = VK_TRUE;
        state.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.colorBlendOp = VK_BLEND_OP_ADD;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        state.alphaBlendOp = VK_BLEND_OP_ADD;
        StageLayout<LightingStage> layout{engine, loader, "identity",
                                          std::array{state}};
        return StageSet<LightingStage>(engine, layout);
      }()) {

  ImGui_ImplGlfw_InitForOther(window.rawHandle(), /*install callbacks*/ true);

  auto &IO = ImGui::GetIO();
  IO.BackendRendererName = "imgui_impl_imvk";
  IO.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
}

void GUI::gui(boost::compat::function_ref<void(void)> recorder) {
  ImGui::SetCurrentContext(m_ctx.get());
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  recorder();

  ImGui::EndFrame();
}

void GUI::ContextDeleter::operator()(ImGuiContext *ctx) const {
  ImGui::SetCurrentContext(ctx);
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext(ctx);
}

StageSet<MaterialStage> GUI::m_getSetForTex(ImTextureID id) {
  return StageSet<MaterialStage>{reinterpret_cast<StageSetImpl *>(id)};
}

ImTextureID GUI::m_toTexId(StageSet<MaterialStage> set) {
  return reinterpret_cast<ImTextureID>(&*set);
}

void GUI::m_actualizeTexture(ImTextureData &tex) {
  if (tex.Status == ImTextureStatus_OK)
    return;

  if (tex.Status == ImTextureStatus_WantCreate ||
      tex.Status == ImTextureStatus_WantUpdates) {
    assert(tex.Format == ImTextureFormat_RGBA32);
    Texture image{m_engine,
                  Texture::load(m_engine, m_ce,
                                std::span<const unsigned char>(
                                    tex.Pixels,
                                    tex.Width * tex.Height * tex.BytesPerPixel),
                                tex.Width, tex.Height)};
    if (tex.GetTexID() != ImTextureID_Invalid)
      removeImage(tex.GetTexID());
    tex.SetTexID(addImage(SampledView{m_engine, std::move(image)}).first);
    tex.SetStatus(ImTextureStatus_OK);
  }

  if (tex.Status == ImTextureStatus_WantDestroy) {
    removeImage(tex.GetTexID());
    tex.SetStatus(ImTextureStatus_Destroyed);
  }
}

void GUI::draw(PipelineManager &pipeMngr, vkw::RenderPassRecorder &commands,
               const imvk::Frame &frame) {
  ImGui::Render();
  auto *imDrawData = ImGui::GetDrawData();
  int32_t vertexOffset = 0;
  int32_t indexOffset = 0;

  if ((!imDrawData) || (imDrawData->CmdListsCount == 0)) {
    return;
  }

  if (imDrawData->Textures != nullptr)
    for (ImTextureData *tex : *imDrawData->Textures)
      if (tex->Status != ImTextureStatus_OK)
        m_actualizeTexture(*tex);

  m_fillBuffers(frame);

  ImGuiIO &io = ImGui::GetIO();

  auto boundTexture = io.Fonts->TexRef.GetTexID();

  pipeMngr.bind(m_geometry, m_proj, m_getSetForTex(boundTexture), m_lighting);
  pipeMngr.bindPipeline();

  struct PushConstBlock {
    glm::vec2 scale;
    glm::vec2 translate;
  } pushConstBlock;

  pushConstBlock.scale =
      glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
  pushConstBlock.translate = glm::vec2(-1.0f);

  commands.pushConstant(pipeMngr.currentLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                        pushConstBlock);

  auto &vertices = m_vertices->use(frame);
  auto &indices = m_indices->use(frame);
  commands.bindVertexBuffer(vertices, 0, 0);
  commands.bindIndexBuffer(indices, 0);

  for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
    const ImDrawList *cmd_list = imDrawData->CmdLists[i];
    for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
      const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[j];

      auto currentTextureID = pcmd->GetTexID();

      if (currentTextureID != boundTexture) {
        pipeMngr.bind(m_getSetForTex(currentTextureID));
        pipeMngr.bindPipeline();
        boundTexture = currentTextureID;
      }

      VkRect2D scissorRect;
      scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
      scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
      scissorRect.extent.width =
          (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
      scissorRect.extent.height =
          (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
      commands.setScissors({&scissorRect, 1}, 0);
      commands.drawIndexed(pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
      indexOffset += pcmd->ElemCount;
    }
    vertexOffset += cmd_list->VtxBuffer.Size;
  }
}

void GUI::m_fillBuffers(const imvk::Frame &frame) {
  auto *drawData = ImGui::GetDrawData();

  if (!drawData)
    return;
  auto verticesCnt = drawData->TotalVtxCount;
  auto indicesCnt = drawData->TotalIdxCount;

  if (verticesCnt == 0 || indicesCnt == 0)
    return;

  // Reallocate buffers if not enough space

  if (!m_vertices || m_vertices->use(frame).size() < verticesCnt) {
    m_vertices = VertexBuffer(
        m_engine, verticesCnt,
        [](const imvk::Frame &f, vkw::VertexBuffer<VertexInfo> &vbuf) {
          // nothing to do.
        });
  }

  if (!m_indices || m_indices->use(frame).size() < indicesCnt) {
    m_indices = IndexBuffer(
        m_engine, indicesCnt,
        [](const imvk::Frame &f, vkw::IndexBuffer<VK_INDEX_TYPE_UINT16> &vbuf) {
          // nothing to do.
        });
  }

  // Upload data
  auto &vertices = m_vertices->use(frame);
  auto &indices = m_indices->use(frame);

  auto vtxDst = vertices.mapped();
  auto idxDst = indices.mapped();

  for (int n = 0; n < drawData->CmdListsCount; n++) {
    const ImDrawList *cmd_list = drawData->CmdLists[n];
    auto vtxSrc = std::span<const ImDrawVert>(cmd_list->VtxBuffer.Data,
                                              cmd_list->VtxBuffer.Size);
    auto idxSrc = std::span<const ImDrawIdx>(cmd_list->IdxBuffer.Data,
                                             cmd_list->IdxBuffer.Size);
    std::ranges::copy(vtxSrc, vtxDst.begin());
    std::ranges::copy(idxSrc, idxDst.begin());
    vtxDst = vtxDst.subspan(cmd_list->VtxBuffer.Size);
    idxDst = idxDst.subspan(cmd_list->IdxBuffer.Size);
  }
  vertices.flush();
  indices.flush();
}

} // namespace imvk::examples