
#include "imvk/base/Pipeline.hpp"
#include "imvk/graphics/Engine.hpp"

namespace imvk {

class GraphicsPipelineStage : public StageLayout {
public:
  GraphicsPipelineStage(GraphicsEngine &engine,
                        const StageLayoutImpl::Description &description)
      : StageLayout(engine, description) {}

  virtual bool isProvoking() const { return false; }

  virtual vkw::GraphicsPipelineCreateInfo
  initCreateInfo(const vkw::PipelineLayout &layout) const;

  virtual void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const = 0;
};

struct GraphicsPipelineTraits {
  using HandleTy = vkw::GraphicsPipeline;
  using StageTy = GraphicsPipelineStage;
  static FObject::Ptr create(FramedEngine &engine,
                             PipelineLayout<GraphicsPipelineTraits> &layout);
};
using GraphicsPipelineStageSet = StageSet;
using GraphicsPipeline = Pipeline<GraphicsPipelineTraits>;

template <std::derived_from<GraphicsPipelineStage>... Stages>
class GraphicsPipelinePool final
    : public PipelinePool<GraphicsPipelineTraits, sizeof...(Stages)> {
public:
  GraphicsPipelinePool(GraphicsEngine &engine, size_t cacheSize,
                       VkPipelineLayoutCreateFlags flags = 0)
      : PipelinePool<GraphicsPipelineTraits, sizeof...(Stages)>(
            engine, cacheSize, flags){};

  template <typename... Args>
  auto &get(Args &&...args)
    requires(std::is_convertible_v<Args, Stages &> && ... && true)
  {
    return PipelinePool<GraphicsPipelineTraits, sizeof...(Stages)>::get(
        std::forward<Args>(args)...);
  }
};
} // namespace imvk