
#include "imvk/base/Frame.hpp"

#include <numeric>
#include <ranges>
#include <span>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

namespace imvk::graph {

struct ImageResourceUseInfo {
  VkImageUsageFlags useFlags;
};

struct BufferResourceUseInfo {
  VkBufferUsageFlags useFlags;
};

struct BufferAccessInfo {
  struct SubresourceInfo {
    VkPipelineStageFlags2 stageFlags;
    VkAccessFlags2 accessFlags;
    size_t offset;
    size_t size;
  };
  std::vector<SubresourceInfo> subresources;
};

struct ImageAccessInfo {
  struct SubresourceInfo {
    VkPipelineStageFlags2 stageFlags;
    VkAccessFlags2 accessFlags;
    VkImageLayout layout;
    VkImageSubresourceRange subresourceRange;
  };
  std::vector<SubresourceInfo> subresources;
};

struct ImageResourceTraits {
  using ResourceHandle = VkImage;
  using UseInfo = ImageResourceUseInfo;
  using AccessInfo = ImageAccessInfo;
  using BarrierType = VkImageMemoryBarrier2;

  static UseInfo mergeUseInfo(const UseInfo &a, const UseInfo &b);

  static bool needsExplicitBarrier(const AccessInfo &firstScope,
                                   const AccessInfo &secondScope);
  static AccessInfo advanceScope(const AccessInfo &currentScope,
                                 const AccessInfo &useScope);
  static std::vector<BarrierType> getBarriers(const AccessInfo &firstScope,
                                              const AccessInfo &secondScope);

  static uint32_t &getSizeField(VkDependencyInfo &depInfo);
  static const BarrierType *&getDataField(VkDependencyInfo &depInfo);
  static void amendBarrierWithHandle(BarrierType &barrier,
                                     ResourceHandle handle);
};

struct BufferResourceTraits {
  using ResourceHandle = VkBuffer;
  using UseInfo = BufferResourceUseInfo;
  using AccessInfo = BufferAccessInfo;
  using BarrierType = VkBufferMemoryBarrier2;

  static UseInfo mergeUseInfo(const UseInfo &a, const UseInfo &b);

  static bool needsExplicitBarrier(const AccessInfo &firstScope,
                                   const AccessInfo &secondScope);
  static AccessInfo advanceScope(const AccessInfo &currentScope,
                                 const AccessInfo &useScope);
  static std::vector<BarrierType> getBarriers(const AccessInfo &firstScope,
                                              const AccessInfo &secondScope);

  static uint32_t &getSizeField(VkDependencyInfo &depInfo);
  static const BarrierType *&getDataField(VkDependencyInfo &depInfo);
  static void amendBarrierWithHandle(BarrierType &barrier,
                                     ResourceHandle handle);
};

template <typename T>
concept ResourceTraits =
    requires() {
      typename T::ResourceHandle;
      typename T::UseInfo;
      typename T::AccessInfo;
      typename T::BarrierType;
      {
        T::needsExplicitBarrier(std::declval<typename T::AccessInfo>(),
                                std::declval<typename T::AccessInfo>())
        } -> std::same_as<bool>;
    };

template <typename T>
concept FramedEngineLike =
    std::derived_from<T, FramedEngine> && requires() { typename T::FrameT; };

template <FramedEngineLike EngineT, ResourceTraits T> class Resource {
public:
  virtual typename T::ResourceHandle get(const typename EngineT::FrameT &frame);
  virtual ~Resource() = default;
};

template <FramedEngineLike EngineT, ResourceTraits T>
class ResourceDescription {
public:
  unsigned id = 0;
#if 0
  virtual std::unique_ptr<ResourceDescription> copy() const = 0;
  virtual bool isIrreplacable() const = 0;
  virtual bool isCompatibleWith(const ResourceDescription &another) const = 0;
#endif
  virtual std::unique_ptr<Resource<EngineT, T>>
  allocate(EngineT &engine, const typename T::UseInfo &useInfo,
           const typename T::AccessInfo &initialAccessInfo) const = 0;

  virtual ~ResourceDescription() = default;
};

class ResourceAccess {
public:
  virtual ~ResourceAccess() = default;
};

template <FramedEngineLike EngineT, ResourceTraits T>
class ResourceAccessDescription {
public:
  ResourceAccessDescription(const ResourceDescription<EngineT, T> &resource,
                            const typename T::AccessInfo &access,
                            const typename T::UseInfo &info,
                            bool internallySynchronized)
      : m_resourceId(resource.id), m_access(access), m_useInfo(info),
        m_internallySynchronized(internallySynchronized) {}
#if 0
  virtual std::unique_ptr<ResourceAccessDescription> copy() const = 0;

  virtual bool
  isCompatibleWith(const ResourceAccessDescription &another) const = 0;
#endif
  virtual std::unique_ptr<ResourceAccess>
  allocate(EngineT &engine, const Resource<EngineT, T> &resource) const = 0;

  const auto &resourceId() const { return m_resourceId; }
  const auto &access() const { return m_access; }
  const auto &useInfo() const { return m_useInfo; }
  bool isInternallySynchronized() const { return m_internallySynchronized; }

  virtual ~ResourceAccessDescription() = default;

private:
  unsigned m_resourceId;
  typename T::AccessInfo m_access;
  typename T::UseInfo m_useInfo;
  bool m_internallySynchronized;
};

template <FramedEngineLike EngineT> class Node {
public:
  virtual void run(const typename EngineT::FrameT &frame) = 0;
  virtual ~Node() = default;
};

template <ResourceTraits T> struct ResourceAccessUseInfo {
  const ResourceAccess *access;
  using AccessVec = std::vector<std::pair<typename T::AccessInfo, bool>>;
  AccessVec externalAccesses;
  ResourceAccessUseInfo(const ResourceAccess &acc) : access(&acc){};
};

template <FramedEngineLike EngineT, ResourceTraits... Ts>
class NodeDescription {
private:
  template <ResourceTraits T>
  using AccessDescVec =
      std::vector<const ResourceAccessDescription<EngineT, T> *>;
  using AccessDescStorage = std::tuple<AccessDescVec<Ts>...>;

public:
  template <ResourceTraits T> auto getAccesses() const {
    return std::get<AccessDescVec<T>>(m_accesses) |
           std::views::transform(
               [](auto &&descPtr) -> decltype(auto) { return *descPtr; });
  }
#if 0
  virtual bool isCompatibleWith(const NodeDescription &another) const = 0;

  virtual std::unique_ptr<NodeDescription> copy() const = 0;
#endif
  virtual std::unique_ptr<Node<EngineT>>
  allocate(EngineT &engine,
           std::tuple<std::span<const ResourceAccessUseInfo<Ts>>...> accesses)
      const = 0;

  virtual ~NodeDescription() = default;

protected:
  template <ResourceTraits T>
  void appendAccess(const ResourceAccessDescription<EngineT, T> &accessInfo) {
    std::get<AccessDescVec<T>>(m_accesses).emplace_back(&accessInfo);
  }

private:
  AccessDescStorage m_accesses;
};

template <FramedEngineLike EngineT, ResourceTraits... Ts> class Graph {
private:
  template <ResourceTraits T>
  using ResourceDescStorage =
      std::vector<const ResourceDescription<EngineT, T> *>;

public:
  template <ResourceTraits T>
  void addResource(ResourceDescription<EngineT, T> &resource) {
    auto &storage = std::get<ResourceDescStorage<T>>(m_resources);
    storage.emplace_back(&resource);
    resource.id = storage.size();
  }

  void addNode(const NodeDescription<EngineT, Ts...> &node) {
    m_nodes.emplace_back(&node);
  }

  template <ResourceTraits T> auto resources() const {
    return std::get<ResourceDescStorage<T>>(m_resources) |
           std::views::transform(
               [](auto &&ptrRes) -> decltype(auto) { return *ptrRes; });
  }

  template <ResourceTraits T> size_t resourceCount() const {
    return std::get<ResourceDescStorage<T>>(m_resources).size();
  }

  template <ResourceTraits T> auto &getResource(unsigned id) const {
    assert(id);
    return *std::get<ResourceDescStorage<T>>(m_resources).at(id - 1);
  }

  auto nodes() const {
    return m_nodes |
           std::views::transform(
               [](auto &&ptrNode) -> decltype(auto) { return *ptrNode; });
  }

  auto nodeCount() const { return m_nodes.size(); }

private:
  std::tuple<ResourceDescStorage<Ts>...> m_resources;
  std::vector<const NodeDescription<EngineT, Ts...> *> m_nodes;
};

template <FramedEngineLike EngineT, ResourceTraits... Ts>
class BarrierNode : public Node<EngineT> {
private:
  template <ResourceTraits T>
  static void applyBarriers(VkDependencyInfo &info,
                            std::span<const typename T::BarrierType> barriers) {
    T::getSizeField(info) = barriers.size();
    T::getDataField(info) = barriers.data();
  }

  template <ResourceTraits T>
  static void
  applyResourceHandles(const typename EngineT::FrameT &frame,
                       std::span<const typename T::BarrierType> barriers,
                       std::span<const Resource<EngineT, T> *const> resources) {
    assert(barriers.size() == resources.size());
    for (auto &&i : std::views::iota(0u, barriers.size())) {
      T::amendBarrierWithHandle(barriers[i], resources[i]->get(frame));
    }
  }

public:
  BarrierNode(EngineT &engine, auto &&...barriers)
      : m_engine(engine),
        m_command(
            m_engine.context().device().core<1, 3>().vkCmdPipelineBarrier2) {
    m_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    m_info.pNext = nullptr;
    /// TODO: make this flags configurable.
    m_info.dependencyFlags = 0;

    std::apply(
        [&barriers...](auto &&...outBarriers) {
          (outBarriers.reserve(barriers.size()), ...);
          (std::ranges::copy(barriers | std::views::elements<0>,
                             std::back_inserter(outBarriers)),
           ...);
        },
        m_barriers);
    std::apply(
        [&barriers...](auto &&...outResources) {
          (outResources.reserve(barriers.size()), ...);
          (std::ranges::copy(barriers | std::views::elements<1>,
                             std::back_inserter(outResources)),
           ...);
        },
        m_resources);
    std::apply(
        [this](auto &&...barrierSpans) {
          (applyBarriers<Ts>(m_info, barrierSpans), ...);
        },
        m_barriers);
  }
  void run(const EngineT::FrameT &frame) override {
    auto applyHandles = [this, &frame](auto &&...resources) {
      std::apply(
          [&resources..., &frame](auto &&...barriers) {
            (applyResourceHandles<Ts>(frame, barriers, resources), ...);
          },
          m_barriers);
    };

    m_command(frame.commands(), &m_info);
  }

private:
  EngineT &m_engine;
  PFN_vkCmdPipelineBarrier2 m_command;

  std::tuple<std::vector<const Resource<EngineT, Ts> *>...> m_resources;
  std::tuple<std::vector<typename Ts::BarrierType>...> m_barriers;
  VkDependencyInfo m_info{};
};

template <FramedEngineLike EngineT, ResourceTraits... Ts> class CompiledGraph {
private:
  template <ResourceTraits T>
  static void
  m_allocateResources(std::vector<std::unique_ptr<Resource<EngineT, T>>> &out,
                      EngineT &engine, const Graph<EngineT, Ts...> &graph) {
    std::map<unsigned, std::pair<typename T::UseInfo, typename T::AccessInfo>>
        useMap;
    for (auto &&node : graph.nodes()) {
      for (auto &&access : node.getAccesses<T>()) {
        auto id = access.resourceId();
        if (!useMap.contains(id)) {
          useMap.emplace(
              std::piecewise_construct, std::make_tuple(id),
              std::forward_as_tuple(access.useInfo(), access.access()));
        } else {
          auto &[useInfo, accessScope] = useMap.at(id);
          useInfo = T::mergeUseInfo(useInfo, access.useInfo());
          accessScope = T::advanceScope(accessScope, access.access());
        }
      }
    }
    out.reserve(graph.resourceCount<T>());

    for (auto &&[id, useInfoPair] : useMap) {
      while (out.size() != id - 1)
        out.emplace_back();
      out.emplace_back(graph.getResource<T>(id).allocate(
          engine, useInfoPair.first, useInfoPair.second));
    }
  }

  template <ResourceTraits T>
  using NodeAccessMap =
      std::unordered_map<const NodeDescription<EngineT, Ts...> *,
                         std::vector<ResourceAccessUseInfo<T>>>;

  template <ResourceTraits T>
  NodeAccessMap<T>
  m_allocateResourceAccesses(const Graph<EngineT, Ts...> &graph) {
    // Mapping from nodes to their access infos for this resource type.
    NodeAccessMap<T> ret;
    for (auto &&node : graph.nodes()) {
      ret.emplace(&node, std::vector<ResourceAccessUseInfo<T>>{});
      auto &nodeAccessVec = ret.at(&node);
      std::vector<std::unique_ptr<ResourceAccess>> nodeAccesses;
      for (auto &&access : node.getAccesses<T>()) {
        auto id = access.resourceId();
        auto &resource =
            *std::get<std::vector<std::unique_ptr<Resource<EngineT, T>>>>(
                 m_resources)
                 .at(id - 1);
        auto &accessRef = *m_resourceAccesses.emplace_back(
            access.allocate(m_engine, resource));
        nodeAccesses.emplace_back(access.allocate(m_engine, resource));
        nodeAccessVec.emplace_back(accessRef);
      }
    }
    return ret;
  }
  template <ResourceTraits T>
  using NodeBarrierVector =
      std::vector<std::pair<const NodeDescription<EngineT, Ts...> *,
                            std::vector<typename T::BarrierType>>>;
  template <ResourceTraits T>
  NodeBarrierVector<T>
  m_getNeededBarriersForResource(const Graph<EngineT, Ts...> &graph,
                                 unsigned id, NodeAccessMap<T> &map) {
    NodeBarrierVector<T> ret;
    auto nodes = graph.nodes();
    if (nodes.empty())
      return ret;
    struct UseInfo {
      std::ranges::iterator_t<decltype(nodes)> barrierInsertPoint;
      std::ranges::iterator_t<decltype(nodes)> nodeIt;
      const ResourceAccessDescription<EngineT, T> *accessUseInfo;
      size_t useIndex;
    };
    std::vector<UseInfo> useRange;

    auto nodeIt = nodes.begin();
    auto nodeItEnd = nodes.end();
    auto nodeItPrev = std::prev(nodeItEnd);
    auto isUse = [&](auto &&access) { return access.resourceId() == id; };
    for (; nodeIt != nodes.end(); ++nodeIt, (nodeItPrev = std::prev(nodeIt))) {
      auto uses = (*nodeIt).getAccesses<T>();
      assert(std::ranges::count_if(uses, isUse) < 2);
      auto foundUse = std::ranges::find_if(uses, isUse);
      if (foundUse == uses.end())
        continue;
      useRange.emplace_back(nodeItPrev, nodeIt, &*foundUse,
                            foundUse - uses.begin());
    }

    if (useRange.empty())
      return ret;

    // Current scope is initialized as final scope state of the frame,
    // because it is also an initial scope for next frame.
    // Scope is not advanced in case if use is marked as internally
    // synchronized, because it implies that node will be in charge of inserting
    // barriers for it's access scope.
    auto currentScope = std::accumulate(
        std::next(useRange.begin()), useRange.end(),
        useRange.front().accessUseInfo->access(), [](auto &&acc, auto &&use) {
          return use.accessUseInfo->isInternallySynchronized()
                     ? acc
                     : T::advanceScope(acc, use.accessUseInfo->access());
        });

    auto useBegin = useRange.begin();
    auto useIt = useBegin;
    auto useEnd = useRange.end();
    for (; useIt != useEnd; ++useIt) {
      auto &use = *useIt;
      auto useScope = use.accessUseInfo->access();
      if (use.accessUseInfo->isInternallySynchronized()) {
        // If access is internally synchronized, we need to pass information to
        // the node about other resource accesses that must be separated by
        // barriers. Node will determine on it's own which barriers it needs to
        // put. The scope, again, is not advanced in this case.
        auto &nodeAccessInfo = map.at(&*use.nodeIt).at(use.useIndex);
        auto createPairInfo = [](auto &&prevUse) {
          return std::make_pair(
              prevUse.accessUseInfo->access(),
              prevUse.accessUseInfo->isInternallySynchronized());
        };
        std::ranges::transform(
            std::ranges::subrange(std::next(useIt), useEnd) |
                std::views::filter([&useScope](auto &&prevUse) {
                  return T::needsExplicitBarrier(
                      prevUse.accessUseInfo->access(), useScope);
                }),
            std::back_inserter(nodeAccessInfo.externalAccesses),
            createPairInfo);
        std::ranges::transform(
            std::ranges::subrange(useBegin, useIt) |
                std::views::filter([&useScope](auto &&prevUse) {
                  return T::needsExplicitBarrier(
                      prevUse.accessUseInfo->access(), useScope);
                }),
            std::back_inserter(nodeAccessInfo.externalAccesses),
            createPairInfo);
        continue;
      }

      auto nextScope = T::advanceScope(currentScope, useScope);
      auto barriers = T::getBarriers(currentScope, nextScope);
      if (barriers.empty())
        continue;
      ret.emplace_back(&*use.barrierInsertPoint, std::move(barriers));
    }

    return ret;
  }

  template <ResourceTraits T>
  using BarrierPair =
      std::pair<typename T::BarrierType, const Resource<EngineT, T> *>;

  template <ResourceTraits T> using BarrierVector = std::vector<BarrierPair<T>>;

  template <ResourceTraits T>
  using NodeBarrierMap =
      std::unordered_map<const NodeDescription<EngineT, Ts...> *,
                         BarrierVector<T>>;

  template <ResourceTraits T>
  NodeBarrierMap<T> m_getNeededBarriers(const Graph<EngineT, Ts...> &graph,
                                        NodeAccessMap<T> &accessMap) {
    NodeBarrierMap<T> ret;
    for (auto &&id : std::ranges::iota_view{0u, graph.resourceCount<T>()}) {
      auto neededBarriers =
          m_getNeededBarriersForResource<T>(graph, id, accessMap);
      for (auto &&[pNode, barriers] : neededBarriers) {
        if (!ret.contains(pNode))
          ret.emplace(pNode, BarrierVector<T>{});
        auto *resource =
            std::get<std::vector<std::unique_ptr<Resource<EngineT, T>>>>(
                m_resources)
                .at(id - 1)
                .get();
        std::ranges::transform(barriers, std::back_inserter(ret.at(pNode)),
                               [resource](auto &&barrier) {
                                 return std::make_pair(barrier, resource);
                               });
      }
    }
    return ret;
  }

  void m_allocateNodes(const Graph<EngineT, Ts...> &graph,
                       auto &&accessMapTuple, auto &&barrierMapTuple) {
    for (auto &&node : graph.nodes()) {
      auto accessInfos =
          std::make_tuple(std::span<const ResourceAccessUseInfo<Ts>>(
              std::get<NodeAccessMap<Ts>>(accessMapTuple).at(&node))...);
      m_nodes.emplace_back(node.allocate(m_engine, accessInfos));

      auto barrierInfos = std::apply(
          [&](auto &&...maps) {
            return std::make_tuple(
                (maps.contains(&node)
                     ? std::span<const BarrierPair<Ts>>(maps.at(&node))
                     : std::span<const BarrierPair<Ts>>{})...);
          },
          barrierMapTuple);
      // don't insert a barrier node if no barriers needed.
      if (std::apply(
              [](auto &&...spans) { return (spans.empty() && ... && true); },
              barrierInfos))
        continue;

      m_nodes.emplace_back(std::apply(
          [this](auto &&...infos) {
            return std::make_unique<BarrierNode<EngineT, Ts...>>(m_engine,
                                                                 infos...);
          },
          barrierInfos));
    }
  }

public:
  CompiledGraph(EngineT &engine) : m_engine(engine) {}

  void recompile(const Graph<EngineT, Ts...> &graph) {
    m_clear();
    // 1. collect use info for each resource and allocate them.
    auto allocateResWrap = [&](auto &&...out) {
      (m_allocateResources(out, m_engine, graph), ...);
    };
    std::apply(allocateResWrap, m_resources);

    // 2. allocate resource accesses and fill node access map.
    auto nodeAccessMap =
        std::make_tuple(m_allocateResourceAccesses<Ts>(graph)...);

    // 3. collect information about needed barrier and fill in information about
    // access scopes in node access map.
    auto neededBarriers = std::make_tuple(m_getNeededBarriers<Ts>(
        graph, std::get<NodeAccessMap<Ts>>(nodeAccessMap))...);

    // 4. allocate nodes, including barrier nodes.
    m_allocateNodes(graph, nodeAccessMap, neededBarriers);
  }

  void run(const typename EngineT::FrameT &frame) {
    for (auto &&node : m_nodes)
      node->run(frame);
  }

private:
  void m_clear() {
    m_nodes.clear();
    m_resourceAccesses.clear();
    std::apply([](auto &&...res) { (res.clear(), ...); }, m_resources);
  }
  EngineT &m_engine;

  std::tuple<std::vector<std::unique_ptr<Resource<EngineT, Ts>>>...>
      m_resources;
  std::vector<std::unique_ptr<ResourceAccess>> m_resourceAccesses;
  std::vector<std::unique_ptr<Node<EngineT>>> m_nodes;
};

} // namespace imvk::graph