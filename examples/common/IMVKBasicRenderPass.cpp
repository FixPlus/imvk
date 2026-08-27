#include "IMVKBasicRenderPass.hpp"
#include <array>

namespace imvk::examples {

BasicVertexStage::BasicVertexStage(
    GraphicsEngine &engine, ShaderLoader &shaderFactory,
    std::string_view shaderName,
    std::unique_ptr<vkw::VertexInputStateCreateInfoBase> vertexState)
    : GraphicsPipelineStage(
          engine,
          [&]() {
            StageLayoutDescription::Description desc{};
            desc.stage = VK_SHADER_STAGE_VERTEX_BIT;
            desc.shaders.emplace_back(*shaderFactory.getModule(shaderName));
            desc.sets.emplace_back(StageLayoutDescription::Description::Set{
                /* set*/ 1, VK_SHADER_STAGE_VERTEX_BIT,
                /* sets per pool*/ 10u});
            return desc;
          }()),
      m_vertexState(std::move(vertexState)) {}

void BasicVertexStage::amendCreateInfo(
    vkw::GraphicsPipelineCreateInfo &info) const {
  info.addInputAssemblyState(vkw::InputAssemblyStateCreateInfo{});
  if (m_vertexState)
    info.addVertexInputState(*m_vertexState);
}

BasicFragmentStage::BasicFragmentStage(GraphicsEngine &engine,
                                       ShaderLoader &shaderFactory,
                                       std::string_view shaderName)
    : GraphicsPipelineStage(engine, [&]() {
        StageLayoutDescription::Description desc{};
        desc.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        desc.shaders.emplace_back(*shaderFactory.getModule(shaderName));
        desc.sets.emplace_back(StageLayoutDescription::Description::Set{
            /* set*/ 2, VK_SHADER_STAGE_FRAGMENT_BIT,
            /* sets per pool*/ 10u});
        return desc;
      }()) {}
void BasicFragmentStage::amendCreateInfo(
    vkw::GraphicsPipelineCreateInfo &info) const {
  info.addDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
  info.addDynamicState(VK_DYNAMIC_STATE_SCISSOR);
}

} // namespace imvk::examples