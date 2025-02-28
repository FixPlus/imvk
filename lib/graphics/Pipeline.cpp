#include "imvk/graphics/Pipeline.hpp"

namespace imvk {

vkw::GraphicsPipeline GraphicsPipelineStage::createPipeline(
    FramedEngine &engine, const vkw::PipelineLayout &layout,
    std::span<const std::shared_ptr<GraphicsPipelineStage>> stages) {
  auto checkIfProvoking = [](auto &&pStage) { return pStage->isProvoking(); };
  auto numberOfProvoking = std::ranges::count_if(stages, checkIfProvoking);
  assert(numberOfProvoking == 1u);
  auto foundProvoking = std::ranges::find_if(stages, checkIfProvoking);
  assert(foundProvoking != stages.end());
  boost::container::small_vector<const vkw::SPIRVModule *, 4> vertexShaderParts;
  boost::container::small_vector<const vkw::SPIRVModule *, 4>
      fragmentShaderParts;

  for (auto &&pStage : stages) {
    if (!pStage->hasShader())
      continue;
    auto *pShader = &pStage->getShader();
    switch (pStage->stage()) {
    case VK_SHADER_STAGE_VERTEX_BIT:
      vertexShaderParts.emplace_back(pShader);
      break;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
      fragmentShaderParts.emplace_back(pShader);
      break;
    default:
      assert(0 && "invalid shader stage");
    }
  }
  /// TODO: implement shader caching.
  auto &device = engine.context().device();
  assert(!vertexShaderParts.empty() && "stages does not define vertex shader");
  assert(!fragmentShaderParts.empty() &&
         "stages does not define fragment shader");

  vkw::VertexShader vShader{
      device,
      vkw::SPIRVModule{vertexShaderParts |
                       std::views::transform(
                           [](auto &&ptr) -> decltype(auto) { return *ptr; })}};
  vkw::FragmentShader fShader{
      device,
      vkw::SPIRVModule{fragmentShaderParts |
                       std::views::transform(
                           [](auto &&ptr) -> decltype(auto) { return *ptr; })}};

  auto createInfo = (*foundProvoking)->initCreateInfo(layout);

  for (auto &&pStage : stages) {
    pStage->amendCreateInfo(createInfo);
  }

  createInfo.addVertexShader(vShader);
  createInfo.addFragmentShader(fShader);
  return vkw::GraphicsPipeline{device, createInfo};
}

vkw::GraphicsPipelineCreateInfo
GraphicsPipelineStage::initCreateInfo(const vkw::PipelineLayout &layout) const {
  assert(0 && "initCreateInfo is called for non-provoking stage");
  abort();
}

} // namespace imvk