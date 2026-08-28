#include "imvk/base/Pipeline.hpp"

#include <iostream>

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

  for (auto &shader : description.shaders) {
    std::optional<vkw::SPIRVModuleInfo> reflectInfoOpt;
    try {
      reflectInfoOpt.emplace(shader->get());
    } catch (vkw::SPIRVReflectError &e) {
      std::cerr << "warning: failed to parse spirv module:" << std::endl;
      std::cerr << e.what() << std::endl;
    }
    if (!reflectInfoOpt)
      continue;
    auto &reflectInfo = *reflectInfoOpt;
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
      if (!setInfo) {
        // assume ok
        continue;
      };
      boost::container::small_vector<vkw::DescriptorSetLayoutBinding, 4>
          bindings;

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
  m_module.emplace(
      description.shaders.size() == 1
          ? description.shaders.front()->get()
          : engine.context().linkContext().link(
                description.shaders |
                    std::views::transform(
                        [](auto &&shader) -> const vkw::SPIRVModule & {
                          return shader->get();
                        }),
                /*link library */ true));
}

} // namespace imvk