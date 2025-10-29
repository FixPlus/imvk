#pragma once

#include "imvk/base/DescriptorSet.hpp"
#include "imvk/base/EngineBase.hpp"
#include "imvk/base/Frame.hpp"
#include "imvk/base/Utils.hpp"

#include "vkw/Pipeline.hpp"
#include "vkw/SPIRVModule.hpp"

#include "boost/container/flat_map.hpp"
#include "boost/container/small_vector.hpp"

#include <array>
#include <numeric>
#include <string_view>
#include <variant>

namespace imvk {

class PipelineStage {
public:
  struct Description {
    /// TODO: think what can be done to eliminate need to copy shader code to
    /// create stage.
    boost::container::small_vector<vkw::SPIRVModule, 2> shaders;
    std::optional<VkShaderStageFlagBits> stage;
    struct Set {
      unsigned num;
      VkShaderStageFlags usedByStages;
      unsigned setsPerPool;
    };
    boost::container::small_vector<Set, 2> sets;
  };

  /// @brief Create a stage with shader. Descriptor layouts are read from
  /// shader's reflect information. For sets which are listed in description a
  /// DescriptorPool will be allocated, that could be used by PipelineStageSet.
  /// @param engine
  /// @param description
  PipelineStage(FramedEngine &engine, const Description &description);

  /// @brief Create empty stage
  /// @param engine
  PipelineStage(FramedEngine &engine) : m_engine(engine) {}

  FramedEngine &engine() const { return m_engine; }

  VkShaderStageFlagBits stage() const { return m_stage; }
  auto getPushConstants() const {
    return std::span<const VkPushConstantRange>{m_pushConstants};
  }

  bool hasShader() const { return m_module.has_value(); }
  auto &getShader() const { return *m_module; }

  auto sets() const {
    return std::ranges::subrange(m_sets.begin(), m_sets.end());
  }

  auto sets() { return std::ranges::subrange(m_sets.begin(), m_sets.end()); }

  virtual ~PipelineStage() = default;

private:
  FramedEngine &m_engine;
  std::optional<vkw::SPIRVModule> m_module;
  boost::container::small_vector<VkPushConstantRange, 3> m_pushConstants;
  boost::container::small_flat_map<unsigned, DescriptorPool, 2> m_sets;
  VkShaderStageFlagBits m_stage{};
};

using PipelineStageHandle = std::shared_ptr<PipelineStage>;

template <typename StageT> class PipelineStageSet {
public:
  PipelineStageSet(std::shared_ptr<StageT> stage, auto &&primitveInfos)
      : m_stage(std::move(stage)) {
    auto primitveInfoIt = primitveInfos.begin();
    for (auto &&[setNum, pool] : m_stage->sets()) {
      assert(primitveInfoIt != primitveInfos.end());
      m_sets.emplace(
          std::piecewise_construct, std::make_tuple(setNum),
          std::forward_as_tuple(m_stage->engine(), pool, *primitveInfoIt));
      ++primitveInfoIt;
    }
  }

  PipelineStageSet(std::shared_ptr<StageT> stage) : m_stage(std::move(stage)) {}

  const auto &stage() const { return m_stage; }

  bool hasSet(unsigned num) const { return m_sets.contains(num); }

  const DescriptorSet &getSet(unsigned num) const {
    assert(m_sets.contains(num));
    return m_sets.at(num);
  }

private:
  boost::container::small_flat_map<unsigned, DescriptorSet, 2> m_sets;
  std::shared_ptr<StageT> m_stage;
};

template <typename StageT> class Pipeline : public FrameObject {
public:
  using PipeT = typename StageT::PipeT;
  Pipeline(FramedEngine &engine, VkPipelineLayoutCreateFlags flags,
           auto &&stages)
      : FrameObject(engine), m_stages([&]() {
          std::vector<std::shared_ptr<StageT>> ret;
          ret.reserve(stages.size());
          for (auto &&stage : stages) {
            ret.emplace_back(std::forward<decltype(stage)>(stage));
          }
          return ret;
        }()),
        m_layout([&]() {
          boost::container::small_vector<
              std::pair<unsigned,
                        std::reference_wrapper<const vkw::DescriptorSetLayout>>,
              4>
              descriptorLayouts;
          boost::container::small_vector<VkPushConstantRange, 4> pushConstants;
          for (auto &&stage : m_stages) {
            std::ranges::transform(
                stage->sets(), std::back_inserter(descriptorLayouts),
                [](auto &&set) {
                  return std::make_pair(
                      std::get<0>(set),
                      std::ref(std::get<1>(set).descriptorLayout()));
                });
            std::ranges::copy(stage->getPushConstants(),
                              std::back_inserter(pushConstants));
          }
          std::ranges::sort(descriptorLayouts, [](auto &&a, auto &&b) {
            return std::get<0>(a) < std::get<0>(b);
          });
          boost::container::small_vector<
              std::reference_wrapper<const vkw::DescriptorSetLayout>, 4>
              descriptorLayoutsRaw;
          unsigned expectedSetNum = 0;

          /// TODO: add support for descriptor 'gaps'
          for (auto &&[setNum, set] : descriptorLayouts) {
            if (setNum != expectedSetNum)
              throw std::runtime_error(
                  "Pipeline declared non-contigous set number range");
            descriptorLayoutsRaw.emplace_back(set.get());
          }

          /// TODO: add merging push constants.
          return vkw::PipelineLayout(engine.context().device(),
                                     descriptorLayoutsRaw, pushConstants,
                                     flags);
        }()),
        m_pipeline(
            std::invoke(StageT::createPipeline, engine, m_layout, m_stages)) {}

  auto &layout() const { return m_layout; }
  auto &pipeline() const { return m_pipeline; }

private:
  std::vector<std::shared_ptr<StageT>> m_stages;
  vkw::PipelineLayout m_layout;
  PipeT m_pipeline;
};

template <typename StageT, unsigned StageCount> class PipelinePool {
private:
  using PipelineKey = std::array<StageT *, StageCount>;

public:
  PipelinePool(FramedEngine &engine, size_t cacheSize,
               VkPipelineLayoutCreateFlags flags = 0)
      : m_engine(engine), m_pipelineCache(cacheSize), m_flags(flags) {}

  template <std::convertible_to<std::shared_ptr<StageT>>... Args>
  const std::shared_ptr<Pipeline<StageT>> &get(Args &&...stages) {
    static_assert(sizeof...(stages) == StageCount);

    std::array<std::shared_ptr<StageT>, StageCount> stageArray{
        std::forward<Args>(stages)...};
    PipelineKey key;
    std::ranges::transform(stageArray, key.begin(),
                           [](auto &&stage) { return stage.get(); });
    auto &ret = m_pipelineCache.get(key, nullptr);
    if (!ret) {
      ret = std::make_shared<Pipeline<StageT>>(m_engine, m_flags,
                                               std::move(stageArray));
    }
    return ret;
  }

private:
  struct PipelineKeyHash {
    size_t operator()(const PipelineKey &key) const {
      auto ptrHash = std::hash<StageT *>{};
      return std::accumulate(
          key.begin(), key.end(), 0ull,
          [&](auto acc, auto &ptr) { return acc ^ ptrHash(ptr); });
    }
  };
  FramedEngine &m_engine;
  Cache<PipelineKey, std::shared_ptr<Pipeline<StageT>>, CachePolicy::LRU,
        PipelineKeyHash>
      m_pipelineCache;
  VkPipelineLayoutCreateFlags m_flags;
};

} // namespace imvk
