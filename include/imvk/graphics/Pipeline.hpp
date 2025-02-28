
#include "imvk/base/Pipeline.hpp"
#include "imvk/graphics/Engine.hpp"

namespace imvk {

class GraphicsPipelineStage : public PipelineStage {
public:
  using PipeT = vkw::GraphicsPipeline;

  GraphicsPipelineStage(GraphicsEngine &engine, const Description &description)
      : PipelineStage(engine, description) {}

  virtual bool isProvoking() const { return false; }

  virtual vkw::GraphicsPipelineCreateInfo
  initCreateInfo(const vkw::PipelineLayout &layout) const;

  virtual void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const = 0;

  static PipeT createPipeline(
      FramedEngine &engine, const vkw::PipelineLayout &layout,
      std::span<const std::shared_ptr<GraphicsPipelineStage>> stages);
};

using GraphicsPipelineStageSet = PipelineStageSet<GraphicsPipelineStage>;
using GraphicsPipeline = Pipeline<GraphicsPipelineStage>;

template <std::derived_from<GraphicsPipelineStage>... Stages>
class GraphicsPipelinePool
    : public PipelinePool<GraphicsPipelineStage, sizeof...(Stages)> {
public:
  GraphicsPipelinePool(GraphicsEngine &engine, size_t cacheSize,
                       VkPipelineLayoutCreateFlags flags = 0)
      : PipelinePool<GraphicsPipelineStage, sizeof...(Stages)>(
            engine, cacheSize, flags){};

  template <typename... Args>
  auto &get(Args &&...args)
    requires(std::is_convertible_v<Args, std::shared_ptr<Stages>> && ... &&
             true)
  {
    return PipelinePool<GraphicsPipelineStage, sizeof...(Stages)>::get(
        std::forward<Args>(args)...);
  }
};
} // namespace imvk