
#pragma once
#include "imvk/base/Pipeline.hpp"
#include "imvk/graphics/Engine.hpp"

namespace imvk {

class GraphicsPipelineStage;

struct GraphicsPipelineTraits {
  using HandleTy = vkw::GraphicsPipeline;
  using StageTy = GraphicsPipelineStage;
  static vkw::GraphicsPipeline
  create(FramedEngine &engine, PipelineLayout<GraphicsPipelineTraits> &layout);
};

class GraphicsPipelineStage : public Stage {
public:
  using PipelineTraits = GraphicsPipelineTraits;
  GraphicsPipelineStage(FramedEngine &engine, Stage::Description &&description)
      : Stage(engine, std::move(description)) {}

  virtual bool isProvoking() const { return false; }

  virtual vkw::GraphicsPipelineCreateInfo
  initCreateInfo(const vkw::PipelineLayout &layout) const;

  virtual void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const = 0;
};

using GraphicsPipeline = Pipeline<GraphicsPipelineTraits>;

template <std::derived_from<GraphicsPipelineStage>... Stages>
class GraphicsPipelinePool final : public PipelinePool<Stages...> {
public:
  GraphicsPipelinePool(GraphicsEngine &engine, size_t cacheSize,
                       VkPipelineLayoutCreateFlags flags = 0)
      : PipelinePool<Stages...>(engine, cacheSize, flags){};
};

template <std::derived_from<GraphicsPipelineStage>... Stages>
class GraphicsPipelineManager final {
private:
  using StageKey = std::tuple<StageSet<Stages>...>;

public:
  GraphicsPipelineManager(GraphicsPipelinePool<Stages...> &pool,
                          vkw::RenderPassRecorder &recorder, const Frame &frame)
      : m_pool(pool), m_recorder(recorder), m_frame(frame) {}

  template <typename... Args> void bind(Args... sets) {
    ((std::get<std::remove_cvref_t<Args>>(m_sets) = std::move(sets)), ...);
  }
  void bindPipeline() {
    auto bindDescriptors = [this](auto &&stageSet, auto &&layout) {
      for (auto &&[num, set] : stageSet.sets()) {
        m_recorder.bindDescriptorSet(layout, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                     *set->use(m_frame), num);
      }
    };
    auto checkSet = [](auto &&pset) { assert(pset); };
    std::apply(
        [&](auto &&...sets) {
          (checkSet(sets), ...);
          (sets->use(m_frame), ...);
          m_boundPipeline = m_pool.get(sets.stage()...);
          auto &pipeline = m_boundPipeline->use(m_frame);
          m_recorder.bindPipeline(pipeline);
          auto &layout = m_boundPipeline.layout()->use(m_frame);
          (bindDescriptors(sets, layout), ...);
        },
        m_sets);
  }

  const vkw::PipelineLayout &currentLayout() const {
    assert(m_boundPipeline);
    return m_boundPipeline.layout()->use(m_frame);
  }

private:
  GraphicsPipelinePool<Stages...> &m_pool;
  vkw::RenderPassRecorder &m_recorder;
  const Frame &m_frame;
  GraphicsPipeline m_boundPipeline = nullptr;
  StageKey m_sets;
};

} // namespace imvk