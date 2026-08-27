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

class StageLayoutDescription {
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
    struct ExternalSet {
      unsigned num;
      vkw::DescriptorSetLayout layout;
      unsigned setsPerPool;
    };
    boost::container::small_vector<std::variant<Set, ExternalSet>, 2> sets;
  };

  StageLayoutDescription(FramedEngine &engine, Description &&description);
  StageLayoutDescription(FramedEngine &engine);

  virtual ~StageLayoutDescription() = default;
  FramedEngine &engine() const { return m_engine; }

  VkShaderStageFlagBits stage() const { return m_stage; }
  auto getPushConstants() const {
    return std::span<const VkPushConstantRange>{m_pushConstants};
  }

  bool hasShader() const { return m_module.has_value(); }
  auto &getShader() const { return *m_module; }

  bool hasSet(unsigned binding) const { return m_setIds.contains(binding); }
  unsigned getSetId(unsigned binding) const { return m_setIds.at(binding); }
  auto sets() {
    return std::ranges::subrange{m_setIds.begin(), m_setIds.end()};
  }

protected:
  friend class StageLayoutImpl;
  FramedEngine &m_engine;
  std::optional<vkw::SPIRVModule> m_module;
  boost::container::small_vector<VkPushConstantRange, 3> m_pushConstants;
  boost::container::small_flat_map<unsigned, unsigned, 2> m_setIds;
  VkShaderStageFlagBits m_stage{};
  FOUses pools;
};

class StageLayoutImpl : public FONode<std::unique_ptr<StageLayoutDescription>,
                                      fon_type::mut, StageLayoutImpl> {
public:
  StageLayoutImpl(FramedEngine &engine, StageLayoutDescription *description)
      : FONode<std::unique_ptr<StageLayoutDescription>, fon_type::mut,
               StageLayoutImpl>(
            engine.createObject<std::unique_ptr<StageLayoutDescription>>(
                description),
            std::move(description->pools)) {}

  void onUse(const Frame &frame) override {
    // do nothing.
  }
};

template <std::derived_from<StageLayoutDescription> T>
class StageLayout : public FONodeView<StageLayoutImpl> {
public:
  StageLayout(FramedEngine &en, auto &&...args)
      : FONodeView<StageLayoutImpl>(
            en, new T{std::forward<decltype(args)>(args)...}) {}

  StageLayout(StageLayout::BaseNode *ptr) : FONodeView<StageLayoutImpl>(ptr) {}
  const T &get() const { return static_cast<const T &>(*(*this)->get()); }

  bool hasSet(unsigned binding) {
    auto &obj = **this;
    return obj.get()->hasSet(binding);
  }
  bool getSetId(unsigned binding) {
    auto &obj = **this;
    return obj.get()->getSetId(binding);
  }
  DescriptorPool getSet(unsigned binding) {
    auto &obj = **this;
    return obj.getUse<DescriptorPool>(obj.get()->getSetId(binding));
  }

  auto sets() {
    auto &obj = **this;
    return obj.get()->sets() | std::views::transform([&obj](auto &&p) {
             return std::tuple<unsigned, DescriptorPool>(
                 p.first, obj.getUse<DescriptorPool>(p.second));
           });
  }
};

class StageSetImpl final : public FONode<char, fon_type::swap, StageSetImpl> {
public:
  StageSetImpl(FramedEngine &engine, auto &stage, auto &&sets)
      : FONode<char, fon_type::swap, StageSetImpl>([&]() {
          FOUses uses(*stage);
          uses.addUses(sets |
                       std::views::transform([](auto &&p) -> decltype(auto) {
                         return *p.first;
                       }));
          return uses;
        }()) {}

  FObject::Ptr constructNew(FramedEngine &engine, FrameID frame) {
    return engine.createObject<char>();
  }
  void onUseAction(const Frame &frame, char &obj) {
    // no action required.
  }
};

template <std::derived_from<StageLayoutDescription> T>
class StageSet : public FONodeView<StageSetImpl> {
public:
  StageSet(auto &&...args)
      : FONodeView<StageSetImpl>(std::forward<decltype(args)>(args)...) {}
  StageLayout<T> stage() { return (*this)->getUse<StageLayout<T>>(0); }

  DescriptorSet getSet(unsigned num) {
    assert(stage().hasSet(num));
    return (*this)->getUse<DescriptorSet>(stage().getSetId(num) + 1);
  }

  auto sets() {
    auto st = stage();
    auto &obj = **this;
    return st.sets() | std::views::elements<0> |
           std::views::transform([st, &obj](auto &&p) mutable {
             return std::make_tuple(
                 p, obj.getUse<DescriptorSet>(st.getSetId(p) + 1));
           });
  }
};

template <std::derived_from<StageLayoutDescription> T>
class StageSetBuilder final {
public:
  StageSetBuilder(FramedEngine &engine, StageLayout<T> &stage)
      : m_engine(engine), m_stage(stage) {
    for (auto &&[setn, _] : stage.sets()) {
      m_setBuilders.insert({setn, nullptr});
    }
  };
  DescriptorSetBuilder &addDescriptorSet(unsigned binding) {
    assert(m_setBuilders.contains(binding));
    auto &optSet = m_setBuilders.at(binding);

    if (!optSet) {
      optSet = std::make_unique<DescriptorSetBuilder>(m_engine.get(),
                                                      m_stage.getSet(binding));
    }
    return *optSet;
  }
  operator StageSet<T>() && {
    boost::container::small_vector<std::pair<DescriptorSet, unsigned>, 2u> sets;
    for (auto &&[binding, setBuilder] : m_setBuilders) {
      assert(setBuilder);
      sets.emplace_back(DescriptorSet{std::move(*setBuilder)}, binding);
    }
    return StageSet<T>(m_engine.get(), m_stage, sets);
  }

private:
  std::reference_wrapper<FramedEngine> m_engine;
  StageLayout<T> m_stage;
  boost::container::small_flat_map<unsigned,
                                   std::unique_ptr<DescriptorSetBuilder>, 2u>
      m_setBuilders;
};
// template <typename T>
// StageSetBuilder(FramedEngine &, T &) -> StageSetBuilder<T>;
template <typename PipelineTraits>
class PipelineLayoutImpl final
    : public FONode<vkw::PipelineLayout, fon_type::mut,
                    PipelineLayoutImpl<PipelineTraits>> {
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
    for (StageLayout<StageTy> &stage : stages) {
      std::ranges::transform(
          stage.sets(), std::back_inserter(descriptorLayouts), [](auto &&set) {
            return std::make_pair(
                std::get<0>(set),
                std::ref(std::get<1>(set).descriptorLayout()));
          });
      std::ranges::copy(stage.get().getPushConstants(),
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
      if (setNum != expectedSetNum++)
        throw std::runtime_error(
            "Pipeline declared non-contigous set number range");
      descriptorLayoutsRaw.emplace_back(set.get());
    }

    /// TODO: add merging push constants.
    return engine.createObject<vkw::PipelineLayout>(
        engine.context().device(), descriptorLayoutsRaw, pushConstants, flags);
  }

public:
  PipelineLayoutImpl(FramedEngine &engine, VkPipelineLayoutCreateFlags flags,
                     std::ranges::range auto &&stages)
      : FONode<vkw::PipelineLayout, fon_type::mut, PipelineLayoutImpl>{
            init(engine, flags,
                 stages |
                     std::views::transform([](auto &&stage) -> decltype(auto) {
                       return *stage;
                     })),
            FOUses{stages}} {}
  PipelineLayoutImpl(FramedEngine &engine, VkPipelineLayoutCreateFlags flags,
                     auto &&...stages)
      : FONode<vkw::PipelineLayout, fon_type::mut, PipelineLayoutImpl>{
            init(engine, flags,
                 std::array<StageLayout<StageTy>, sizeof...(stages)>{
                     StageLayout<StageTy>{&*stages}...}),
            FOUses{(*stages)...}} {}

  void onUse(const Frame &frame) final {
    // do nothing.
  }
};
template <typename PipelineTraits>
class PipelineLayout : public FONodeView<PipelineLayoutImpl<PipelineTraits>> {
public:
  PipelineLayout(auto &&...args)
      : FONodeView<PipelineLayoutImpl<PipelineTraits>>(
            std::forward<decltype(args)>(args)...) {}
  using StageTy = PipelineTraits::StageTy;
  auto stages() const {
    return (*this)->uses() | std::views::transform([](auto &&stage) {
             return StageLayout<StageTy>{
                 static_cast<typename StageLayout<StageTy>::BaseNode *>(
                     &stage)};
           });
  }
};

template <typename PipelineTraits>
class PipelineImpl final
    : public FONode<typename PipelineTraits::HandleTy, fon_type::mut,
                    PipelineImpl<PipelineTraits>> {
public:
  FObject::Ptr init(FramedEngine &engine,
                    PipelineLayout<PipelineTraits> &layout) {
    return PipelineTraits::create(engine, layout);
  }
  void onUse(const Frame &frame) final {
    // do nothing.
  }

  PipelineImpl(FramedEngine &engine, PipelineLayout<PipelineTraits> &layout)
      : FONode<typename PipelineTraits::HandleTy, fon_type::mut, PipelineImpl>(
            init(engine, layout), FOUses{*layout}){};
};

template <typename PipelineTraits>
class Pipeline : public FONodeView<PipelineImpl<PipelineTraits>> {
public:
  Pipeline(auto &&...args)
      : FONodeView<PipelineImpl<PipelineTraits>>(
            std::forward<decltype(args)>(args)...) {}
  PipelineLayout<PipelineTraits> layout() {
    return (*this)->getUse<PipelineLayout<PipelineTraits>>(0);
  }
};

template <typename Stage1, typename... Stages> class PipelinePool {
private:
  using PipelineTraits = typename Stage1::PipelineTraits;
  constexpr static auto StageCount = sizeof...(Stages) + 1;
  using StageTy = PipelineTraits::StageTy;
  using PipelineKey = std::array<const StageTy *, StageCount>;
  using PipelineTy = Pipeline<PipelineTraits>;
  using PipelineLayoutTy = PipelineLayout<PipelineTraits>;

public:
  PipelinePool(FramedEngine &engine, size_t cacheSize,
               VkPipelineLayoutCreateFlags flags = 0)
      : m_engine(engine), m_pipelineCache(cacheSize), m_flags(flags) {}
  template <typename Arg, typename... Args>
  PipelineTy get(Arg &&arg, Args &&...args)
    requires(std::is_convertible_v<Args, StageLayout<Stages>> && ... &&
             std::is_convertible_v<Arg, StageLayout<Stage1>>)
  {

    PipelineKey key{&arg.get(), &args.get()...};
    auto &ret = m_pipelineCache.get(key, nullptr);
    if (!ret) {
      PipelineLayoutTy layout(m_engine, m_flags, arg, args...);
      ret = PipelineTy(m_engine, layout);
    }
    return ret;
  }

private:
  struct PipelineKeyHash {
    size_t operator()(const PipelineKey &key) const {
      auto ptrHash = std::hash<const StageTy *>{};
      return std::accumulate(
          key.begin(), key.end(), 0ull,
          [&](auto acc, auto &ptr) { return acc ^ ptrHash(ptr); });
    }
  };
  FramedEngine &m_engine;
  Cache<PipelineKey, Pipeline<PipelineTraits>, CachePolicy::LRU,
        PipelineKeyHash>
      m_pipelineCache;
  VkPipelineLayoutCreateFlags m_flags;
};

} // namespace imvk
