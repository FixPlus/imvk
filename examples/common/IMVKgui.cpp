#include "IMVKgui.hpp"
#include "IMVKTexture.hpp"

#include "backends/imgui_impl_glfw.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <ranges>

namespace imvk::examples {

class GUIPlatform {
public:
  virtual void newFrame() = 0;
  virtual ~GUIPlatform() = default;
};

namespace {

class GUISamplerImpl
    : public FONode<vkw::Sampler, fon_type::cow, GUISamplerImpl> {
public:
  GUISamplerImpl(FramedEngine &engine, VkFilter filter)
      : FONode<vkw::Sampler, fon_type::cow, GUISamplerImpl>(
            engine, doConstructNew(engine, filter)),
        m_filter(filter) {}

  static vkw::Sampler doConstructNew(FramedEngine &engine, VkFilter filter) {
    VkSamplerCreateInfo info{};
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.magFilter = filter;
    info.minFilter = VK_FILTER_LINEAR;
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.pNext = nullptr;
    return vkw::Sampler{engine.context().device(), info};
  }
  vkw::Sampler constructNew(FramedEngine &engine) {
    return doConstructNew(engine, m_filter);
  }

private:
  VkFilter m_filter;
};

class GUISampler : public FONodeView<GUISamplerImpl> {
public:
  GUISampler(auto &&...args)
      : FONodeView<GUISamplerImpl>(std::forward<decltype(args)>(args)...) {}
  static void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                              FONodeBase &obj, unsigned binding) {
    auto &casted = static_cast<GUISamplerImpl &>(obj);
    vkw::DescriptorWrite write{binding, VK_DESCRIPTOR_TYPE_SAMPLER};
    write.addImage(casted.get(), nullptr,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    set.write(write);
  }
};

class GLFWPlatform : public GUIPlatform {
public:
  GLFWPlatform(ImGuiContext *ctx, Window &window) : m_ctx(ctx) {
    ImGui::SetCurrentContext(ctx);

    ImGui_ImplGlfw_InitForOther(window.rawHandle(), /*install callbacks*/ true);
  }

  void newFrame() override { ImGui_ImplGlfw_NewFrame(); }
  ~GLFWPlatform() override {
    ImGui::SetCurrentContext(m_ctx);
    ImGui_ImplGlfw_Shutdown();
  }

private:
  ImGuiContext *m_ctx;
};

class OffscreenPlatform : public GUIPlatform {
public:
  OffscreenPlatform(ImGuiContext *ctx, imvk::graph::FramebufferInfo fb)
      : m_ctx(ctx), m_fb(fb) {}

  void newFrame() override {
    auto &io = ImGui::GetIO();
    if (m_fb->isDestroyed())
      m_fb->construct();
    auto fbInfo = m_fb->get();
    io.DisplaySize = ImVec2(fbInfo.extents.width, fbInfo.extents.height);
    io.DeltaTime = 0.06;
  }
  ~OffscreenPlatform() override = default;

private:
  ImGuiContext *m_ctx;
  imvk::graph::FramebufferInfo m_fb;
};

} // namespace

GUI::GUI(Window &window, GraphicsEngine &engine, CopyEngine &ce,
         ShaderLoader &loader)
    : m_engine(engine), m_ce(ce), m_sl(loader), m_window(window),
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
                       vkw::RasterizationStateCreateInfo{}) {
  ImGui::SetCurrentContext(m_ctx.get());

  auto &IO = ImGui::GetIO();
  IO.BackendRendererName = "imgui_impl_imvk";
  IO.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
  IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  auto &platformIO = ImGui::GetPlatformIO();
  platformIO.DrawCallback_ResetRenderState = [](const ImDrawList *,
                                                const ImDrawCmd *) {
    auto *state = reinterpret_cast<RenderState *>(
        ImGui::GetPlatformIO().Renderer_RenderState);
    state->pipeMngr->bind(state->gui->m_lightings->alpha.nearest);
    state->pipeMngr->bindPipeline();
  };
  platformIO.DrawCallback_SetSamplerLinear = [](const ImDrawList *,
                                                const ImDrawCmd *) {
    auto *state = reinterpret_cast<RenderState *>(
        ImGui::GetPlatformIO().Renderer_RenderState);
    auto &pm = *state->pipeMngr;
    auto &gui = *state->gui;
    if (pm.current<LightingStage>() == gui.m_lightings->alpha.linear ||
        pm.current<LightingStage>() == gui.m_lightings->alpha.nearest) {
      pm.bind(gui.m_lightings->alpha.linear);
    } else {
      pm.bind(gui.m_lightings->noAlpha.linear);
    }
    state->pipeMngr->bindPipeline();
  };
  platformIO.DrawCallback_SetSamplerNearest = [](const ImDrawList *,
                                                 const ImDrawCmd *) {
    auto *state = reinterpret_cast<RenderState *>(
        ImGui::GetPlatformIO().Renderer_RenderState);
    auto &pm = *state->pipeMngr;
    auto &gui = *state->gui;
    if (pm.current<LightingStage>() == gui.m_lightings->alpha.linear ||
        pm.current<LightingStage>() == gui.m_lightings->alpha.nearest) {
      pm.bind(gui.m_lightings->alpha.nearest);
    } else {
      pm.bind(gui.m_lightings->noAlpha.nearest);
    }
    state->pipeMngr->bindPipeline();
  };
}

void GUI::setNoAlpha() {
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->AddCallback([](const ImDrawList *, const ImDrawCmd *) {
    auto *state = reinterpret_cast<RenderState *>(
        ImGui::GetPlatformIO().Renderer_RenderState);
    auto &pm = *state->pipeMngr;
    auto &gui = *state->gui;
    if (pm.current<LightingStage>() == gui.m_lightings->noAlpha.linear ||
        pm.current<LightingStage>() == gui.m_lightings->alpha.linear) {
      pm.bind(gui.m_lightings->noAlpha.linear);
    } else {
      pm.bind(gui.m_lightings->noAlpha.nearest);
    }
    state->pipeMngr->bindPipeline();
  });
}

void GUI::setAlpha() {
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->AddCallback([](const ImDrawList *, const ImDrawCmd *) {
    auto *state = reinterpret_cast<RenderState *>(
        ImGui::GetPlatformIO().Renderer_RenderState);
    auto &pm = *state->pipeMngr;
    auto &gui = *state->gui;
    if (pm.current<LightingStage>() == gui.m_lightings->noAlpha.linear ||
        pm.current<LightingStage>() == gui.m_lightings->alpha.linear) {
      pm.bind(gui.m_lightings->alpha.linear);
    } else {
      pm.bind(gui.m_lightings->alpha.nearest);
    }
    state->pipeMngr->bindPipeline();
  });
}

void GUI::filterLinear() {
  auto &platformIO = ImGui::GetPlatformIO();
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->AddCallback(platformIO.DrawCallback_SetSamplerLinear);
}

void GUI::filterNearest() {
  auto &platformIO = ImGui::GetPlatformIO();
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->AddCallback(platformIO.DrawCallback_SetSamplerNearest);
}

void GUI::reset() {
  auto &platformIO = ImGui::GetPlatformIO();
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  draw_list->AddCallback(platformIO.DrawCallback_ResetRenderState);
}

GUI::~GUI() = default;

GUI::Lightings::Lightings(GraphicsEngine &engine, ShaderLoader &sl,
                          const vkw::RenderingFormatInfo &info) {
  VkPipelineColorBlendAttachmentState state{};
  state.blendEnable = VK_TRUE;
  state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  state.colorBlendOp = VK_BLEND_OP_ADD;
  state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  state.alphaBlendOp = VK_BLEND_OP_ADD;
  StageLayout<LightingStage> alphaLayout{engine, sl, "ui", info,
                                         std::array{state}};
  StageLayout<LightingStage> noAlphaLayout{engine, sl, "ui", info};
  alpha.linear = [&]() {
    auto builder = StageSetBuilder<LightingStage>(engine, alphaLayout);
    builder.addDescriptorSet(4).addDescriptor(
        GUISampler(engine, VK_FILTER_LINEAR), 0);
    return builder;
  }();
  alpha.nearest = [&]() {
    auto builder = StageSetBuilder<LightingStage>(engine, alphaLayout);
    builder.addDescriptorSet(4).addDescriptor(
        GUISampler(engine, VK_FILTER_NEAREST), 0);
    return builder;
  }();
  noAlpha.linear = [&]() {
    auto builder = StageSetBuilder<LightingStage>(engine, noAlphaLayout);
    builder.addDescriptorSet(4).addDescriptor(
        GUISampler(engine, VK_FILTER_LINEAR), 0);
    return builder;
  }();
  noAlpha.nearest = [&]() {
    auto builder = StageSetBuilder<LightingStage>(engine, noAlphaLayout);
    builder.addDescriptorSet(4).addDescriptor(
        GUISampler(engine, VK_FILTER_NEAREST), 0);
    return builder;
  }();
}

void GUI::updateRenderingInfo(const vkw::RenderingFormatInfo &info,
                              const imvk::graph::FramebufferInfo &fbInfo) {

  m_lightings.emplace(m_engine, m_sl, info);
  auto fb = fbInfo->get();
  if (fb.isSwapchain)
    m_platform = std::make_unique<GLFWPlatform>(m_ctx.get(), m_window);
  else
    m_platform = std::make_unique<OffscreenPlatform>(m_ctx.get(), fbInfo);
}

void GUI::gui(boost::compat::function_ref<void(void)> recorder) {
  ImGui::SetCurrentContext(m_ctx.get());
  assert(m_platform && "called gui() with no bound platform");
  m_platform->newFrame();
  ImGui::NewFrame();

  recorder();

  ImGui::EndFrame();
}

void GUI::ContextDeleter::operator()(ImGuiContext *ctx) const {
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
    tex.SetTexID(
        addImage(SampledView{m_engine, std::move(image), /* no sampler */ true})
            .first);
    tex.SetStatus(ImTextureStatus_OK);
  }

  if (tex.Status == ImTextureStatus_WantDestroy) {
    removeImage(tex.GetTexID());
    tex.SetStatus(ImTextureStatus_Destroyed);
  }
}

void GUI::draw(PipelineManager &pipeMngr, vkw::RenderPassRecorder &commands,
               const imvk::Frame &frame) {
  ImGui::SetCurrentContext(m_ctx.get());
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

  assert(m_lightings &&
         "calling gui::draw() without attaching to specific scene");
  auto defaultLighting = m_lightings->alpha.nearest;
  pipeMngr.bind(m_geometry, m_proj, m_getSetForTex(boundTexture),
                defaultLighting);
  pipeMngr.bindPipeline();
  // todo: not exception safe.
  RenderState rState{this, &pipeMngr};
  ImGui::GetPlatformIO().Renderer_RenderState = &rState;

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
      if (pcmd->UserCallback) {
        pcmd->UserCallback(cmd_list, pcmd);
        continue;
      }

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
  ImGui::GetPlatformIO().Renderer_RenderState = nullptr;
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