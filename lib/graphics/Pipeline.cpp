#include "imvk/graphics/Pipeline.hpp"

namespace imvk {
FObject::Ptr
GraphicsPipelineTraits::create(FramedEngine &engine,
                               PipelineLayout<GraphicsPipelineTraits> &layout) {
  auto stages = layout.stages();
  auto checkIfProvoking = [](const GraphicsPipelineStage &pStage) {
    return pStage.isProvoking();
  };
  auto numberOfProvoking = std::ranges::count_if(stages, checkIfProvoking);
  assert(numberOfProvoking == 1u);
  auto foundProvoking = std::ranges::find_if(stages, checkIfProvoking);
  assert(foundProvoking != stages.end());
  boost::container::small_vector<const vkw::SPIRVModule *, 4> vertexShaderParts;
  boost::container::small_vector<const vkw::SPIRVModule *, 4>
      fragmentShaderParts;

  for (const GraphicsPipelineStage &stage : stages) {
    auto &stageInfo = stage.get();
    if (!stageInfo.hasShader())
      continue;
    auto *pShader = &stageInfo.getShader();
    switch (stageInfo.stage()) {
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
  static thread_local vkw::SPIRVLinkContext ctx{};

  vkw::VertexShader vShader{
      device, ctx.link(vertexShaderParts |
                       std::views::transform(
                           [](auto &&ptr) -> decltype(auto) { return *ptr; }))};
  vkw::FragmentShader fShader{
      device, ctx.link(fragmentShaderParts |
                       std::views::transform(
                           [](auto &&ptr) -> decltype(auto) { return *ptr; }))};

  auto createInfo = (*foundProvoking).initCreateInfo(layout.get());

  for (const GraphicsPipelineStage &stage : stages) {
    stage.amendCreateInfo(createInfo);
  }

  createInfo.addShader(vShader).addShader(fShader);
  return engine.createObject<vkw::GraphicsPipeline>(device, createInfo);
}

vkw::GraphicsPipelineCreateInfo
GraphicsPipelineStage::initCreateInfo(const vkw::PipelineLayout &layout) const {
  assert(0 && "initCreateInfo is called for non-provoking stage");
  abort();
}

} // namespace imvk