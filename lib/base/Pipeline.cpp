#include "imvk/base/Pipeline.hpp"

namespace imvk {

PipelineStage::PipelineStage(FramedEngine &engine,
                             const Description &description)
    : m_engine(engine) {
  /// TODO:
  if (!description.shaders.empty())
    if (description.shaders.size() > 1)
      m_module.emplace(
          description.shaders |
          std::views::transform(
              [](auto &&pModule) -> decltype(auto) { return *pModule; }));
    else
      m_module.emplace(*description.shaders.front());
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