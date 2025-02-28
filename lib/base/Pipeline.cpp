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
    : m_engine(engine), m_stage(description.stage ? *description.stage : 0) {
  if (description.shaders.empty())
    return;
  assert(description.stage);
  m_module.emplace(
      description.shaders |
          std::views::transform(
              [](auto &&pModule) -> decltype(auto) { return *pModule; }),
      /*link library */ true);
  auto &reflectInfo = m_module->info();
  for (auto &&[num, flags, setsPerPool] : description.sets) {
    auto setInfo = findStageSet(reflectInfo, num);
    boost::container::small_vector<vkw::DescriptorSetLayoutBinding, 4> bindings;

    if (!setInfo) {
      m_sets.emplace(
          num, vkw::DescriptorSetLayout{engine.context().device(), bindings});
    }
    for (auto &&binding : setInfo->bindings()) {
      bindings.emplace_back(binding.index(), binding.descriptorType(), flags);
    }
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

PipelineStage::PipelineStage(FramedEngine &engine,
                             const Description::Set &setInfo,
                             vkw::DescriptorSetLayout &&layout)
    : m_engine(engine) {
  if (layout.info().bindingCount)
    m_sets.emplace(setInfo.num,
                   DescriptorPool(engine.context().device(), std::move(layout),
                                  setInfo.setsPerPool));
  else
    m_sets.emplace(setInfo.num, std::move(layout));
}

} // namespace imvk