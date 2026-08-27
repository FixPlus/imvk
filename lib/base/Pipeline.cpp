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
#if 1
Stage::Stage(FramedEngine &engine, Description &&description)
    : m_engine(engine), m_stage(description.stage ? *description.stage
                                                  : VkShaderStageFlagBits{}) {
  auto isExternal = [](auto &&set) {
    return std::holds_alternative<Description::ExternalSet>(set);
  };
  auto asExternal = [](auto &&set) -> decltype(auto) {
    return std::get<Description::ExternalSet>(set);
  };
  unsigned counter = 0;
  for (auto &&[num, layout, setsPerPool] :
       description.sets | std::views::filter(isExternal) |
           std::views::transform(asExternal)) {
    m_setIds.emplace(num, counter++);
    pools.addUse(DescriptorPool(engine, std::make_unique<DescriptorPoolImpl>(
                                            engine.context().device(),
                                            std::move(layout), setsPerPool)));
  }
  if (description.shaders.empty())
    return;
  assert(description.stage);
  m_module.emplace(
      description.shaders.size() == 1
          ? description.shaders.front()
          : engine.context().linkContext().link(description.shaders,
                                                /*link library */ true));
  auto reflectInfo = vkw::SPIRVModuleInfo{*m_module};
  auto isReflected = [](auto &&set) {
    return std::holds_alternative<Description::Set>(set);
  };
  auto asReflected = [](auto &&set) -> decltype(auto) {
    return std::get<Description::Set>(set);
  };
  for (auto &&[num, flags, setsPerPool] :
       description.sets | std::views::filter(isReflected) |
           std::views::transform(asReflected)) {
    auto setInfo = findStageSet(reflectInfo, num);
    if (!setInfo)
      throw std::runtime_error(
          "Stage declared set that is not defined by shader module");
    boost::container::small_vector<vkw::DescriptorSetLayoutBinding, 4> bindings;

    for (auto &&binding : setInfo->bindings())
      bindings.emplace_back(binding.index(), binding.descriptorType(), flags);
    m_setIds.emplace(num, counter++);
    pools.addUse(DescriptorPool(
        engine,
        std::make_unique<DescriptorPoolImpl>(
            engine.context().device(),
            vkw::DescriptorSetLayout{engine.context().device(), bindings},
            setsPerPool)));
  }

  for (auto &&pushC : reflectInfo.pushConstants()) {
    m_pushConstants.emplace_back(VkPushConstantRange{
        .stageFlags = static_cast<VkShaderStageFlags>(*description.stage),
        .offset = pushC.offset(),
        .size = pushC.size()});
  }
}
#endif

} // namespace imvk