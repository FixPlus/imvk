#include "imvk/base/Pipeline.hpp"

namespace imvk {

namespace {

std::optional<vkw::SPIRVDescriptorSetInfo>
findStageSet(vkw::SPIRVModuleInfo const &moduleInfo, uint32_t stageSet) {
  auto sets = moduleInfo.sets();
  auto stageSetIt = std::ranges::find_if(
      sets, [&](auto &&set) { return set.index() == stageSet; });
  if (stageSetIt == sets.end())
    return std::nullopt;
  return *stageSetIt;
}

} // namespace

PipelineStage::PipelineStage(FramedEngine &engine,
                             const Description &description)
    : m_engine(engine), m_stage(description.stage ? *description.stage
                                                  : VkShaderStageFlagBits{}) {
  if (description.shaders.empty())
    return;
  assert(description.stage);
  m_module.emplace(
      description.shaders.size() == 1
          ? description.shaders.front()
          : engine.context().linkContext().link(description.shaders,
                                                /*link library */ true));
  auto reflectInfo = vkw::SPIRVModuleInfo{*m_module};
  for (auto &&[num, flags, setsPerPool] : description.sets) {
    auto setInfo = findStageSet(reflectInfo, num);
    if (!setInfo)
      throw std::runtime_error(
          "Stage declared set that is not defined by shader module");
    boost::container::small_vector<vkw::DescriptorSetLayoutBinding, 4> bindings;

    for (auto &&binding : setInfo->bindings())
      bindings.emplace_back(binding.index(), binding.descriptorType(), flags);

    m_sets.emplace(num, DescriptorPool{engine.context().device(),
                                       vkw::DescriptorSetLayout{
                                           engine.context().device(), bindings},
                                       setsPerPool});
  }

  for (auto &&pushC : reflectInfo.pushConstants()) {
    m_pushConstants.emplace_back(VkPushConstantRange{
        .stageFlags = static_cast<VkShaderStageFlags>(*description.stage),
        .offset = pushC.offset(),
        .size = pushC.size()});
  }
}

} // namespace imvk