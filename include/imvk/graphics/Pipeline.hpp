
#include "imvk/base/Pipeline.hpp"
#include "imvk/graphics/Engine.hpp"

namespace imvk {

class GraphicsPipelineStage : public PipelineStage {
public:
  using PipeT = vkw::GraphicsPipeline;

  GraphicsPipelineStage(GraphicsEngine &engine, const Description &description,
                        VkShaderStageFlagBits shaderStage)
      : PipelineStage(engine, description), m_stage(shaderStage) {}

  auto getShaderStage() const { return m_stage; }

  virtual bool isProvoking() const { return false; }

  virtual vkw::GraphicsPipelineCreateInfo
  initCreateInfo(const vkw::PipelineLayout &layout) const;

  virtual void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const = 0;

  static PipeT createPipeline(
      FramedEngine &engine, const vkw::PipelineLayout &layout,
      std::span<const std::shared_ptr<GraphicsPipelineStage>> stages);

private:
  VkShaderStageFlagBits m_stage;
};

using GraphicsPipelineStageSet = PipelineStageSet<GraphicsPipelineStage>;
using GraphicsPipeline = Pipeline<GraphicsPipelineStage>;
template <unsigned StageCount>
using GraphicsPipelinePool = PipelinePool<GraphicsPipelineStage, StageCount>;

} // namespace imvk