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

class StageLayoutImpl {
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
  StageLayoutImpl(FramedEngine &engine, const Description &description);

  /// @brief Create empty stage
  /// @param engine
  StageLayoutImpl(FramedEngine &engine) : m_engine(engine) {}

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

  bool hasSet(unsigned binding) const { return m_sets.contains(binding); }
  DescriptorPool &getSet(unsigned binding) { return m_sets.at(binding); }
  const DescriptorPool &getSet(unsigned binding) const {
    return m_sets.at(binding);
  }

private:
  FramedEngine &m_engine;
  std::optional<vkw::SPIRVModule> m_module;
  boost::container::small_vector<VkPushConstantRange, 3> m_pushConstants;
  boost::container::small_flat_map<unsigned, DescriptorPool, 2> m_sets;
  VkShaderStageFlagBits m_stage{};
};

class StageLayout : public FONode<StageLayoutImpl, fon_type::cow> {
public:
  StageLayout(FramedEngine &engine, auto &&...args)
      : FONode<StageLayoutImpl, fon_type::cow>{
            engine.createObject<StageLayoutImpl>(
                engine, std::forward<decltype(args)>(args)...)} {}

private:
  FObject::Ptr constructNew(FramedEngine &engine) noexcept final {
    // this is leaf object. can return null.
    return nullptr;
  }
};

struct StageSetView {
  boost::container::small_flat_map<unsigned, const vkw::DescriptorSet *, 2u>
      sets;
  const StageLayoutImpl *layout;
};

class StageSet final : public FONode<StageSetView, fon_type::swap> {
public:
  StageSet(FramedEngine &engine, StageLayout &stage, auto &&sets)
      : FONode<StageSetView, fon_type::swap>(
            engine,
            [&](FrameID frame) {
              auto &layout = stage.get();
              StageSetView view;
              view.layout = &layout;
              for (auto &&[set, binding] : sets) {
                view.sets.insert({binding, &*set->get(frame)});
              }
              return engine.createObject<StageSetView>(std::move(view));
            },
            [&]() {
              FOUses uses(stage);
              uses.addUses(
                  sets | std::views::transform([](auto &&p) -> decltype(auto) {
                    return *p.first;
                  }));
              return uses;
            }()) {
    unsigned index = 1;
    for (auto &&[_, binding] : sets) {
      m_setMap.insert({binding, index++});
    }
  }

  StageLayout &stage() const {
    return static_cast<StageLayout &>(*m_uses.front());
  }

  bool hasSet(unsigned num) const { return m_setMap.contains(num); }

  const DescriptorSet &getSet(unsigned num) const {
    assert(m_setMap.contains(num));
    return static_cast<const DescriptorSet &>(*m_uses.at(m_setMap.at(num)));
  }
  DescriptorSet &getSet(unsigned num) {
    assert(m_setMap.contains(num));
    return static_cast<DescriptorSet &>(*m_uses.at(m_setMap.at(num)));
  }

private:
  void onCowExpire(const Frame &frame) final {
    // TODO: implement.
    std::terminate();
  }

  void onUseAction(const Frame &frame, FObject &obj) override {
    // no action required.
  }

  boost::container::small_flat_map<unsigned, unsigned, 2u> m_setMap;
};

class StageSetBuilder final {
public:
  StageSetBuilder(StageLayout &stage) : m_stage(&stage) {
    for (auto &&[setn, _] : stage.get().sets()) {
      m_setBuilders.insert({setn, nullptr});
    }
  };
  DescriptorSetBuilder &addDescriptorSet(unsigned binding) {
    assert(m_setBuilders.contains(binding));
    auto &optSet = m_setBuilders.at(binding);
    // this is super fishy, but according to cow object semantics underlying
    // object is immutable, however it is 100% safe to modify stage layout on go
    // (trust me bro)
    auto &stage = const_cast<StageLayoutImpl &>(m_stage->get());
    if (!optSet) {
      optSet = std::make_unique<DescriptorSetBuilder>(stage.engine(),
                                                      stage.getSet(binding));
    }
    return *optSet;
  }
  operator Ref<StageSet>() && {
    boost::container::small_vector<std::pair<Ref<DescriptorSet>, unsigned>, 2u>
        sets;
    for (auto &&[binding, setBuilder] : m_setBuilders) {
      assert(setBuilder);
      sets.emplace_back(Ref<DescriptorSet>{std::move(*setBuilder)}, binding);
    }
    return m_stage->get().engine().createNode<StageSet>(*m_stage, sets);
  }

private:
  Ref<StageLayout> m_stage;
  boost::container::small_flat_map<unsigned,
                                   std::unique_ptr<DescriptorSetBuilder>, 2u>
      m_setBuilders;
};

template <typename PipelineTraits>
class PipelineLayout final : public FONode<vkw::PipelineLayout, fon_type::cow> {
private:
  using StageTy = PipelineTraits::StageTy;

  FObject::Ptr init(FramedEngine &engine, VkPipelineLayoutCreateFlags flags,
                    auto &&stages) {
    boost::container::small_vector<
        std::pair<unsigned,
                  std::reference_wrapper<const vkw::DescriptorSetLayout>>,
        4>
        descriptorLayouts;
    boost::container::small_vector<VkPushConstantRange, 4> pushConstants;
    for (const StageLayout &stageRef : stages) {
      const StageLayoutImpl &stage = stageRef.get();
      std::ranges::transform(
          stage.sets(), std::back_inserter(descriptorLayouts), [](auto &&set) {
            return std::make_pair(
                std::get<0>(set),
                std::ref(std::get<1>(set).descriptorLayout()));
          });
      std::ranges::copy(stage.getPushConstants(),
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
    return engine.createObject<vkw::PipelineLayout>(
        engine.context().device(), descriptorLayoutsRaw, pushConstants, flags);
  }

public:
  PipelineLayout(FramedEngine &engine, VkPipelineLayoutCreateFlags flags,
                 std::ranges::range auto &&stages)
      : FONode<vkw::PipelineLayout,
               fon_type::cow>{init(engine, flags,
                                   stages |
                                       std::views::transform(
                                           [](auto &&stage) -> decltype(auto) {
                                             return *stage;
                                           })),
                              FOUses{stages}},
        m_flags(flags) {}
  PipelineLayout(FramedEngine &engine, VkPipelineLayoutCreateFlags flags,
                 auto &&...stages)
      : FONode<
            vkw::PipelineLayout,
            fon_type::cow>{init(engine, flags,
                                std::array<std::reference_wrapper<StageLayout>,
                                           sizeof...(stages)>{stages...}),
                           FOUses{std::forward<decltype(stages)>(stages)...}},
        m_flags(flags) {}

  auto stages() const {
    return m_uses | std::views::transform([](auto &&stage) -> decltype(auto) {
             return static_cast<const StageTy &>(*stage);
           });
  }

private:
  FObject::Ptr constructNew(FramedEngine &engine) noexcept final {
    return init(engine, m_flags, stages());
  }
  VkPipelineLayoutCreateFlags m_flags;
};

template <typename PipelineTraits>
class Pipeline final
    : public FONode<typename PipelineTraits::HandleTy, fon_type::cow> {
private:
  FObject::Ptr init(FramedEngine &engine,
                    PipelineLayout<PipelineTraits> &layout) {
    return PipelineTraits::create(engine, layout);
  }

public:
  Pipeline(FramedEngine &engine, PipelineLayout<PipelineTraits> &layout)
      : FONode<typename PipelineTraits::HandleTy, fon_type::cow>(
            init(engine, layout), FOUses{layout}){};

  PipelineLayout<PipelineTraits> &layout() {
    return static_cast<PipelineLayout<PipelineTraits> &>(*this->m_uses.front());
  }

private:
  FObject::Ptr constructNew(FramedEngine &engine) noexcept final {
    return init(engine, layout());
  }
};

template <typename PipelineTraits, size_t StageCount> class PipelinePool {
private:
  using StageTy = PipelineTraits::StageTy;
  using PipelineKey = std::array<StageTy *, StageCount>;
  using PipelineTy = Pipeline<PipelineTraits>;
  using PipelineLayoutTy = PipelineLayout<PipelineTraits>;

public:
  PipelinePool(FramedEngine &engine, size_t cacheSize,
               VkPipelineLayoutCreateFlags flags = 0)
      : m_engine(engine), m_pipelineCache(cacheSize), m_flags(flags) {}

  template <std::convertible_to<StageTy &>... Args>
  PipelineTy &get(Args &&...stages) {
    static_assert(sizeof...(stages) == StageCount);

    PipelineKey key{&stages...};
    auto &ret = m_pipelineCache.get(key, nullptr);
    if (!ret) {
      Ref<PipelineLayoutTy> layout =
          m_engine.createNode<PipelineLayoutTy>(m_flags, stages...);
      ret = m_engine.createNode<PipelineTy>(*layout);
    }
    return *ret;
  }

private:
  struct PipelineKeyHash {
    size_t operator()(const PipelineKey &key) const {
      auto ptrHash = std::hash<StageTy *>{};
      return std::accumulate(
          key.begin(), key.end(), 0ull,
          [&](auto acc, auto &ptr) { return acc ^ ptrHash(ptr); });
    }
  };
  FramedEngine &m_engine;
  Cache<PipelineKey, Ref<Pipeline<PipelineTraits>>, CachePolicy::LRU,
        PipelineKeyHash>
      m_pipelineCache;
  VkPipelineLayoutCreateFlags m_flags;
};

} // namespace imvk
