#include "IMVKPipeline.hpp"
#include <array>

namespace imvk::examples {

GeometryStage::GeometryStage(
    FramedEngine &engine, ShaderLoader &shaderFactory,
    std::string_view shaderName,
    std::unique_ptr<vkw::VertexInputStateCreateInfoBase> vertexState)
    : GraphicsPipelineStage(
          engine,
          [&]() {
            Stage::Description desc{};
            desc.stage = VK_SHADER_STAGE_VERTEX_BIT;
            desc.shaders.emplace_back(shaderFactory.getModule(
                std::string(shaderName).append(".gm.vert")));
            desc.shaders.emplace_back(shaderFactory.getModule("general.vert"));
            desc.sets.emplace_back(
                Stage::Description::Set{/* set*/ 1, VK_SHADER_STAGE_VERTEX_BIT,
                                        /* sets per pool*/ 10u});
            return desc;
          }()),
      m_vertexState(std::move(vertexState)) {}

void GeometryStage::amendCreateInfo(
    vkw::GraphicsPipelineCreateInfo &info) const {
  info.addInputAssemblyState(vkw::InputAssemblyStateCreateInfo{});
  if (m_vertexState)
    info.addVertexInputState(*m_vertexState);
}

ProjectionStage::ProjectionStage(FramedEngine &engine,
                                 ShaderLoader &shaderFactory,
                                 std::string_view shaderName)
    : GraphicsPipelineStage(engine, [&]() {
        Stage::Description desc{};
        desc.stage = VK_SHADER_STAGE_VERTEX_BIT;
        desc.shaders.emplace_back(shaderFactory.getModule(
            std::string(shaderName).append(".pr.vert")));
        desc.sets.emplace_back(
            Stage::Description::Set{/* set*/ 2, VK_SHADER_STAGE_VERTEX_BIT,
                                    /* sets per pool*/ 10u});
        return desc;
      }()) {}
MaterialStage::MaterialStage(
    FramedEngine &engine, ShaderLoader &shaderFactory,
    std::string_view shaderName,
    vkw::RasterizationStateCreateInfo rasterization,
    std::optional<vkw::DepthTestStateCreateInfo> depthTest)
    : GraphicsPipelineStage(engine,
                            [&]() {
                              Stage::Description desc{};
                              desc.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                              desc.shaders.emplace_back(shaderFactory.getModule(
                                  std::string(shaderName).append(".mt.frag")));
                              desc.shaders.emplace_back(
                                  shaderFactory.getModule("general.frag"));
                              desc.sets.emplace_back(Stage::Description::Set{
                                  /* set*/ 3, VK_SHADER_STAGE_FRAGMENT_BIT,
                                  /* sets per pool*/ 10u});
                              return desc;
                            }()),
      m_rasterizationState(rasterization), m_depthTestState(depthTest) {}
void MaterialStage::amendCreateInfo(
    vkw::GraphicsPipelineCreateInfo &info) const {
  info.addRasterizationState(m_rasterizationState);
  if (m_depthTestState)
    info.addDepthTestState(*m_depthTestState);
}

LightingStage::LightingStage(
    FramedEngine &engine, ShaderLoader &shaderFactory,
    std::string_view shaderName, const vkw::RenderingFormatInfo &renderingInfo,
    std::span<const VkPipelineColorBlendAttachmentState> blends)
    : GraphicsPipelineStage(engine,
                            [&]() {
                              Stage::Description desc{};
                              desc.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                              desc.shaders.emplace_back(shaderFactory.getModule(
                                  std::string(shaderName).append(".lt.frag")));
                              desc.sets.emplace_back(Stage::Description::Set{
                                  /* set*/ 4, VK_SHADER_STAGE_FRAGMENT_BIT,
                                  /* sets per pool*/ 10u});
                              return desc;
                            }()),
      m_renderingInfo(renderingInfo) {
  std::ranges::copy(blends, std::back_inserter(m_blends));
}
vkw::GraphicsPipelineCreateInfo
LightingStage::initCreateInfo(const vkw::PipelineLayout &layout) const {
  return vkw::GraphicsPipelineCreateInfo{m_renderingInfo, layout};
}

void LightingStage::amendCreateInfo(
    vkw::GraphicsPipelineCreateInfo &info) const {
  for (auto &&[index, state] : m_blends | std::views::enumerate) {
    if (state.blendEnable == VK_FALSE)
      continue;
    info.addBlendState(state, index);
  }
  info.addDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
  info.addDynamicState(VK_DYNAMIC_STATE_SCISSOR);
}

} // namespace imvk::examples