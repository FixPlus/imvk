
#include "IMVKGraphEditor.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>
#include <vulkan/vk_enum_string_helper.h>

#include <boost/static_string/static_string.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ed = ax::NodeEditor;

namespace imvk::examples {
static ed::EditorContext *createEditorContext() {
  ed::Config config;
  return ed::CreateEditor(&config);
}

GraphEditor::GraphEditor(const MaterializationEnvironment &me,
                         imvk::graph::Workflow initialWorkflow,
                         SceneTable availableScenes)
    : m_me(me), m_ctx(createEditorContext()),
      m_materializedCtx(createEditorContext()),
      m_scene(GraphScene::get([this](GraphScene &scene, const Frame &frame) {
        onGui(scene, frame);
      })),
      m_availableScenes(std::move(availableScenes)),
      m_currentWorkflow(std::move(initialWorkflow)),
      m_materializedWorkflow(m_currentWorkflow) {
  m_inject_into_workflow(m_materializedWorkflow);
  m_matCtx.emplace(m_me, m_materializedWorkflow);
}
void GraphEditor::m_inject_into_workflow(imvk::graph::Workflow &wf) {
  auto foundAquireImage = std::ranges::find_if(
      wf, [&](auto &&node) { return isa<graph::AcquireImage>(&node); });
  auto foundPresent = std::ranges::find_if(
      wf, [&](auto &&node) { return isa<graph::Present>(&node); });
  if (foundAquireImage == wf.end()) {
    assert(foundPresent == wf.end() && "cannot have present without acquire");
    // this is headless workflow. just insert work at the end.
    auto builder = imvk::graph::WorkflowBuilder{wf, wf.end()};
    auto &image =
        builder.create<imvk::graph::AcquireImage>()->results().front();
    auto &renderedImage =
        builder
            .create<imvk::graph::RenderPass>(
                std::array{&image}, imvk::graph::Node::EmptyValues, m_scene)
            ->results()
            .front();
    builder.create<imvk::graph::Present>(renderedImage);
    return;
  }
  assert(foundPresent != wf.end() && "cannot have acquire without present");
  auto &originalSwapchain = foundAquireImage->results().front();
  auto &swapChainClone =
      imvk::graph::WorkflowBuilder{wf, *std::next(foundAquireImage)}
          .create<imvk::graph::Clone<imvk::graph::ImageTy>>(
              foundAquireImage->results().front())
          ->results()
          .front();

  originalSwapchain.replaceAllUsesWith(&swapChainClone);
  swapChainClone.node().uses().front().replaceBy(&originalSwapchain);

  auto &originalPresent = foundPresent->uses().front();
  auto &renderedImage = imvk::graph::WorkflowBuilder{wf, *foundPresent}
                            .create<imvk::graph::RenderPass>(
                                std::array{&originalSwapchain},
                                std::array{&originalPresent.value()}, m_scene)
                            ->results()
                            .front();
  originalPresent.replaceBy(&renderedImage);
}

struct LayoutVertex {
  imvk::graph::Node *node = nullptr;
  size_t rank = 0;
  size_t stableOrder = 0;
  float width = 0.0f;
  float height = 0.0f;
  std::vector<size_t> predecessors;
  std::vector<size_t> successors;

  bool isVirtual() const { return node == nullptr; }
};

struct LayoutEdge {
  size_t tail;
  size_t head;
};

static double median(std::vector<double> values) {
  if (values.empty())
    return 0.0;
  std::ranges::sort(values);
  const auto middle = values.size() / 2;
  if (values.size() % 2 != 0)
    return values[middle];
  return (values[middle - 1] + values[middle]) * 0.5;
}

// The weighted median from Fig. 10 of Gansner et al. It biases an even
// median toward the side on which the adjacent vertices are packed tighter.
static double orderingMedian(std::vector<size_t> positions) {
  if (positions.empty())
    return -1.0;
  std::ranges::sort(positions);
  const auto middle = positions.size() / 2;
  if (positions.size() % 2 != 0)
    return static_cast<double>(positions[middle]);
  if (positions.size() == 2)
    return (static_cast<double>(positions[0]) +
            static_cast<double>(positions[1])) *
           0.5;

  const auto left = positions[middle - 1] - positions.front();
  const auto right = positions.back() - positions[middle];
  if (left + right == 0)
    return (static_cast<double>(positions[middle - 1]) +
            static_cast<double>(positions[middle])) *
           0.5;
  return (static_cast<double>(positions[middle - 1]) * right +
          static_cast<double>(positions[middle]) * left) /
         static_cast<double>(left + right);
}

static void untangleLayout(imvk::graph::Workflow &wf) {
  constexpr float rankSeparation = 120.0f;
  constexpr float nodeSeparation = 44.0f;
  constexpr float virtualSeparation = 18.0f;
  constexpr size_t orderingIterations = 24;
  constexpr size_t positioningIterations = 8;

  std::vector<imvk::graph::Node *> nodes;
  std::unordered_map<imvk::graph::Node *, size_t> nodeIndices;
  for (auto &node : wf) {
    nodeIndices.emplace(&node, nodes.size());
    nodes.push_back(&node);
  }
  if (nodes.empty())
    return;

  // Workflow links are already required to form a DAG. Keep parallel links:
  // they represent independent visual edges and should influence ordering.
  std::vector<LayoutEdge> graphEdges;
  std::vector<std::vector<size_t>> successors(nodes.size());
  std::vector<std::vector<size_t>> predecessors(nodes.size());
  std::vector<size_t> predecessorCounts(nodes.size());
  for (size_t consumer = 0; consumer < nodes.size(); ++consumer) {
    for (const auto &use : nodes[consumer]->uses()) {
      if (!use.hasValue())
        continue;
      const auto found = nodeIndices.find(&use.value().node());
      if (found == nodeIndices.end() || found->second == consumer)
        continue;
      const auto producer = found->second;
      graphEdges.push_back({producer, consumer});
      successors[producer].push_back(consumer);
      predecessors[consumer].push_back(producer);
      ++predecessorCounts[consumer];
    }
  }

  // Stable Kahn traversal followed by longest-path ranking implements the
  // first pass of a Sugiyama layout. The fallback only matters for malformed
  // externally supplied cyclic workflows and still guarantees no overlap.
  std::vector<size_t> topologicalOrder;
  std::vector<bool> emitted(nodes.size());
  topologicalOrder.reserve(nodes.size());
  while (topologicalOrder.size() != nodes.size()) {
    size_t next = nodes.size();
    for (size_t index = 0; index < nodes.size(); ++index) {
      if (!emitted[index] && predecessorCounts[index] == 0) {
        next = index;
        break;
      }
    }
    if (next == nodes.size())
      break;
    emitted[next] = true;
    topologicalOrder.push_back(next);
    for (const auto successor : successors[next])
      --predecessorCounts[successor];
  }

  const bool isAcyclic = topologicalOrder.size() == nodes.size();
  if (!isAcyclic) {
    topologicalOrder.resize(nodes.size());
    for (size_t index = 0; index < nodes.size(); ++index)
      topologicalOrder[index] = index;
  }

  std::vector<size_t> ranks(nodes.size());
  if (isAcyclic) {
    for (const auto vertex : topologicalOrder) {
      for (const auto successor : successors[vertex])
        ranks[successor] = std::max(ranks[successor], ranks[vertex] + 1);
    }

    // Move vertices within their feasible rank interval toward the median of
    // their neighbors. This is a compact approximation of the paper's
    // network-simplex rank optimization and reduces unnecessarily long links.
    for (size_t iteration = 0; iteration < 8; ++iteration) {
      const bool reverse = iteration % 2 == 0;
      for (size_t step = 0; step < topologicalOrder.size(); ++step) {
        const auto orderIndex =
            reverse ? topologicalOrder.size() - step - 1 : step;
        const auto vertex = topologicalOrder[orderIndex];
        size_t lower = 0;
        size_t upper = std::numeric_limits<size_t>::max();
        std::vector<double> adjacentRanks;
        adjacentRanks.reserve(predecessors[vertex].size() +
                              successors[vertex].size());
        for (const auto predecessor : predecessors[vertex]) {
          lower = std::max(lower, ranks[predecessor] + 1);
          adjacentRanks.push_back(static_cast<double>(ranks[predecessor]));
        }
        for (const auto successor : successors[vertex]) {
          if (ranks[successor] == 0)
            continue;
          upper = std::min(upper, ranks[successor] - 1);
          adjacentRanks.push_back(static_cast<double>(ranks[successor]));
        }
        if (upper == std::numeric_limits<size_t>::max())
          upper = std::max(lower, ranks[vertex]);
        if (!adjacentRanks.empty() && lower <= upper) {
          const auto desired = static_cast<size_t>(
              std::max(0.0, std::round(median(std::move(adjacentRanks)))));
          ranks[vertex] = std::clamp(desired, lower, upper);
        }
      }
    }
  } else {
    for (size_t index = 0; index < nodes.size(); ++index)
      ranks[index] = index;
  }

  // Remove empty ranks left by balancing. Relative order and all DAG
  // constraints remain unchanged.
  std::vector<size_t> occupiedRanks = ranks;
  std::ranges::sort(occupiedRanks);
  occupiedRanks.erase(std::unique(occupiedRanks.begin(), occupiedRanks.end()),
                      occupiedRanks.end());
  for (auto &rank : ranks)
    rank = static_cast<size_t>(
        std::lower_bound(occupiedRanks.begin(), occupiedRanks.end(), rank) -
        occupiedRanks.begin());
  const auto rankCount = occupiedRanks.size();

  std::vector<LayoutVertex> vertices;
  vertices.reserve(nodes.size() + graphEdges.size() * rankCount);
  for (size_t index = 0; index < nodes.size(); ++index) {
    auto size = ed::GetNodeSize(ed::NodeId(nodes[index]));
    if (!std::isfinite(size.x) || size.x <= 0.0f)
      size.x = 160.0f;
    if (!std::isfinite(size.y) || size.y <= 0.0f)
      size.y = 80.0f;
    vertices.push_back(LayoutVertex{.node = nodes[index],
                                    .rank = ranks[index],
                                    .stableOrder = index,
                                    .width = size.x,
                                    .height = size.y});
  }

  // Replace every long edge with a chain through virtual vertices. Ordering
  // those chains is what lets the median heuristic account for crossings made
  // by links spanning more than one rank.
  for (size_t edgeIndex = 0; edgeIndex < graphEdges.size(); ++edgeIndex) {
    const auto [tail, head] = graphEdges[edgeIndex];
    if (ranks[tail] >= ranks[head])
      continue;
    auto previous = tail;
    for (auto rank = ranks[tail] + 1; rank < ranks[head]; ++rank) {
      const auto virtualVertex = vertices.size();
      vertices.push_back(LayoutVertex{
          .rank = rank,
          .stableOrder = nodes.size() + edgeIndex * rankCount + rank});
      vertices[previous].successors.push_back(virtualVertex);
      vertices[virtualVertex].predecessors.push_back(previous);
      previous = virtualVertex;
    }
    vertices[previous].successors.push_back(head);
    vertices[head].predecessors.push_back(previous);
  }

  std::vector<std::vector<size_t>> layers(rankCount);
  for (size_t vertex = 0; vertex < vertices.size(); ++vertex)
    layers[vertices[vertex].rank].push_back(vertex);
  for (auto &layer : layers) {
    std::ranges::stable_sort(layer, {}, [&](const auto vertex) {
      return vertices[vertex].stableOrder;
    });
  }

  std::vector<size_t> positions(vertices.size());
  const auto refreshPositions = [&]() {
    for (const auto &layer : layers) {
      for (size_t position = 0; position < layer.size(); ++position)
        positions[layer[position]] = position;
    }
  };
  refreshPositions();

  const auto reorderByMedian = [&](size_t rank, bool usePredecessors,
                                   bool reverseTies) {
    auto &layer = layers[rank];
    struct WeightedVertex {
      size_t vertex;
      double weight;
      size_t oldPosition;
    };
    std::vector<size_t> slots;
    std::vector<WeightedVertex> weighted;
    for (size_t position = 0; position < layer.size(); ++position) {
      const auto vertex = layer[position];
      const auto &adjacent = usePredecessors ? vertices[vertex].predecessors
                                             : vertices[vertex].successors;
      if (adjacent.empty())
        continue;
      std::vector<size_t> adjacentPositions;
      adjacentPositions.reserve(adjacent.size());
      for (const auto neighbor : adjacent)
        adjacentPositions.push_back(positions[neighbor]);
      slots.push_back(position);
      weighted.push_back(
          {vertex, orderingMedian(std::move(adjacentPositions)), position});
    }
    std::stable_sort(weighted.begin(), weighted.end(),
                     [reverseTies](const auto &left, const auto &right) {
                       if (left.weight != right.weight)
                         return left.weight < right.weight;
                       return reverseTies
                                  ? left.oldPosition > right.oldPosition
                                  : left.oldPosition < right.oldPosition;
                     });
    for (size_t index = 0; index < slots.size(); ++index)
      layer[slots[index]] = weighted[index].vertex;
    for (size_t position = 0; position < layer.size(); ++position)
      positions[layer[position]] = position;
  };

  const auto pairCrossings = [&](size_t left, size_t right, bool leftFirst) {
    std::uint64_t crossings = 0;
    const auto countOnSide = [&](const auto &leftAdjacent,
                                 const auto &rightAdjacent) {
      std::uint64_t result = 0;
      for (const auto leftNeighbor : leftAdjacent) {
        for (const auto rightNeighbor : rightAdjacent) {
          if (leftFirst)
            result += positions[leftNeighbor] > positions[rightNeighbor];
          else
            result += positions[leftNeighbor] < positions[rightNeighbor];
        }
      }
      return result;
    };
    crossings +=
        countOnSide(vertices[left].predecessors, vertices[right].predecessors);
    crossings +=
        countOnSide(vertices[left].successors, vertices[right].successors);
    return crossings;
  };

  const auto transpose = [&]() {
    bool anyImprovement = false;
    bool improved = true;
    size_t pass = 0;
    size_t maximumPasses = 1;
    for (const auto &layer : layers)
      maximumPasses += layer.size() * layer.size();
    while (improved && pass++ < maximumPasses) {
      improved = false;
      for (auto &layer : layers) {
        if (layer.size() < 2)
          continue;
        for (size_t position = 0; position + 1 < layer.size(); ++position) {
          const auto left = layer[position];
          const auto right = layer[position + 1];
          if (pairCrossings(left, right, false) >=
              pairCrossings(left, right, true))
            continue;
          std::swap(layer[position], layer[position + 1]);
          positions[left] = position + 1;
          positions[right] = position;
          improved = true;
          anyImprovement = true;
        }
      }
    }
    return anyImprovement;
  };

  const auto crossingCount = [&]() {
    std::uint64_t result = 0;
    for (size_t rank = 0; rank + 1 < layers.size(); ++rank) {
      struct Segment {
        size_t tail;
        size_t head;
      };
      std::vector<Segment> segments;
      for (const auto tail : layers[rank]) {
        for (const auto head : vertices[tail].successors)
          segments.push_back({positions[tail], positions[head]});
      }
      for (size_t first = 0; first < segments.size(); ++first) {
        for (size_t second = first + 1; second < segments.size(); ++second) {
          const auto tailOrder = segments[first].tail < segments[second].tail;
          const auto headOrder = segments[first].head < segments[second].head;
          if (segments[first].tail != segments[second].tail &&
              segments[first].head != segments[second].head &&
              tailOrder != headOrder)
            ++result;
        }
      }
    }
    return result;
  };

  auto bestLayers = layers;
  auto bestCrossings = crossingCount();
  for (size_t iteration = 0; iteration < orderingIterations; ++iteration) {
    const bool downward = iteration % 2 == 0;
    if (downward) {
      for (size_t rank = 1; rank < layers.size(); ++rank)
        reorderByMedian(rank, true, iteration % 4 >= 2);
    } else {
      for (size_t rank = layers.size() - 1; rank-- > 0;)
        reorderByMedian(rank, false, iteration % 4 >= 2);
    }
    transpose();
    const auto crossings = crossingCount();
    if (crossings < bestCrossings) {
      bestCrossings = crossings;
      bestLayers = layers;
    }
  }
  layers = std::move(bestLayers);
  refreshPositions();

  std::vector<double> coordinates(vertices.size());
  const auto centerSeparation = [&](size_t upper, size_t lower) {
    const auto gap = vertices[upper].isVirtual() && vertices[lower].isVirtual()
                         ? virtualSeparation
                         : nodeSeparation;
    return static_cast<double>(vertices[upper].height) * 0.5 + gap +
           static_cast<double>(vertices[lower].height) * 0.5;
  };

  // Start each rank tightly packed and centered around the same axis.
  for (const auto &layer : layers) {
    if (layer.empty())
      continue;
    coordinates[layer.front()] = vertices[layer.front()].height * 0.5;
    for (size_t index = 1; index < layer.size(); ++index) {
      coordinates[layer[index]] =
          coordinates[layer[index - 1]] +
          centerSeparation(layer[index - 1], layer[index]);
    }
    const auto extent =
        coordinates[layer.back()] + vertices[layer.back()].height * 0.5;
    for (const auto vertex : layer)
      coordinates[vertex] -= extent * 0.5;
  }

  // Project desired median coordinates onto the non-overlap constraints. This
  // is weighted isotonic regression (PAVA) after subtracting the required
  // cumulative separation from each coordinate.
  const auto positionLayer = [&](size_t rank, bool usePredecessors,
                                 bool useAllNeighbors) {
    const auto &layer = layers[rank];
    if (layer.empty())
      return;
    std::vector<double> offsets(layer.size());
    std::vector<double> desired(layer.size());
    std::vector<double> weights(layer.size(), 1.0);
    for (size_t index = 1; index < layer.size(); ++index) {
      offsets[index] =
          offsets[index - 1] + centerSeparation(layer[index - 1], layer[index]);
    }
    for (size_t index = 0; index < layer.size(); ++index) {
      const auto vertex = layer[index];
      std::vector<double> adjacentCoordinates;
      const auto append = [&](const auto &adjacent) {
        for (const auto neighbor : adjacent)
          adjacentCoordinates.push_back(coordinates[neighbor]);
      };
      if (useAllNeighbors) {
        append(vertices[vertex].predecessors);
        append(vertices[vertex].successors);
      } else if (usePredecessors) {
        append(vertices[vertex].predecessors);
      } else {
        append(vertices[vertex].successors);
      }
      desired[index] = adjacentCoordinates.empty()
                           ? coordinates[vertex]
                           : median(std::move(adjacentCoordinates));
      const auto degree = vertices[vertex].predecessors.size() +
                          vertices[vertex].successors.size();
      weights[index] = 1.0 + static_cast<double>(degree);
    }

    struct Block {
      size_t first;
      size_t last;
      double weight;
      double weightedValue;
      double mean() const { return weightedValue / weight; }
    };
    std::vector<Block> blocks;
    blocks.reserve(layer.size());
    for (size_t index = 0; index < layer.size(); ++index) {
      blocks.push_back({index, index, weights[index],
                        weights[index] * (desired[index] - offsets[index])});
      while (blocks.size() >= 2 &&
             blocks[blocks.size() - 2].mean() > blocks.back().mean()) {
        auto right = blocks.back();
        blocks.pop_back();
        auto &left = blocks.back();
        left.last = right.last;
        left.weight += right.weight;
        left.weightedValue += right.weightedValue;
      }
    }
    for (const auto &block : blocks) {
      for (size_t index = block.first; index <= block.last; ++index)
        coordinates[layer[index]] = block.mean() + offsets[index];
    }
  };

  for (size_t iteration = 0; iteration < positioningIterations; ++iteration) {
    for (size_t rank = 1; rank < layers.size(); ++rank)
      positionLayer(rank, true, false);
    for (size_t rank = layers.size() - 1; rank-- > 0;)
      positionLayer(rank, false, false);
  }
  for (size_t iteration = 0; iteration < 4; ++iteration) {
    for (size_t rank = 0; rank < layers.size(); ++rank)
      positionLayer(rank, false, true);
  }

  std::vector<float> rankWidths(rankCount);
  for (const auto &vertex : vertices) {
    if (!vertex.isVirtual())
      rankWidths[vertex.rank] = std::max(rankWidths[vertex.rank], vertex.width);
  }
  std::vector<float> rankCenters(rankCount);
  rankCenters.front() = rankWidths.front() * 0.5f;
  for (size_t rank = 1; rank < rankCount; ++rank) {
    rankCenters[rank] = rankCenters[rank - 1] + rankWidths[rank - 1] * 0.5f +
                        rankSeparation + rankWidths[rank] * 0.5f;
  }

  double minimumTop = std::numeric_limits<double>::max();
  for (size_t vertex = 0; vertex < nodes.size(); ++vertex) {
    minimumTop = std::min(minimumTop,
                          coordinates[vertex] - vertices[vertex].height * 0.5);
  }
  for (size_t vertex = 0; vertex < nodes.size(); ++vertex) {
    const auto &layout = vertices[vertex];
    ed::SetNodePosition(
        ed::NodeId(layout.node),
        ImVec2(rankCenters[layout.rank] - layout.width * 0.5f,
               static_cast<float>(coordinates[vertex] - minimumTop -
                                  layout.height * 0.5)));
  }
}

static void displayName(std::string_view name, boost::static_string<50> &out) {
  out.append(name);
  bool capitalize = true;
  for (auto &character : out) {
    if (character == '_') {
      character = ' ';
      capitalize = true;
    } else if (capitalize) {
      character = static_cast<char>(
          std::toupper(static_cast<unsigned char>(character)));
      capitalize = false;
    }
  }
}

static void pinName(const imvk::graph::Use &use,
                    boost::static_string<50> &out) {
  if (use.info() && !use.info()->name().empty()) {
    auto name = use.info()->name();
    out.append(name);
    return;
  }
  out.append(use.type().name());
}

static void pinName(const imvk::graph::Value &value,
                    boost::static_string<50> &out) {
  if (value.infoOrNull() && !value.infoOrNull()->name().empty()) {
    out.append(value.infoOrNull()->name());
    return;
  }
  out.append(value.type().name());
}

enum class PinIconShape {
  circle,
  square,
  diamond,
  triangle,
  grid,
  roundSquare
};

struct PinIconStyle {
  PinIconShape shape;
  ImU32 color;
};

static PinIconStyle pinIconStyle(const imvk::graph::Type &type) {
  if (isa<imvk::graph::ImageTy>(&type))
    return {PinIconShape::square, IM_COL32(51, 150, 215, 255)};
  if (isa<imvk::graph::IntegerScalarTy>(&type))
    return {PinIconShape::circle, IM_COL32(68, 201, 156, 255)};
  if (isa<imvk::graph::ExtentsTy>(&type))
    return {PinIconShape::diamond, IM_COL32(238, 184, 82, 255)};
  if (isa<imvk::graph::BufferTy>(&type))
    return {PinIconShape::roundSquare, IM_COL32(147, 112, 219, 255)};
#if 0
  if (isa<imvk::graph::DescriptorTy>(&type))
    return {PinIconShape::triangle, IM_COL32(218, 85, 183, 255)};
  if (isa<imvk::graph::ArrayTy>(&type))
    return {PinIconShape::grid, IM_COL32(92, 210, 210, 255)};
#endif
  return {PinIconShape::circle, IM_COL32(190, 190, 190, 255)};
}

static void drawPinIcon(const imvk::graph::Type &type, bool connected) {
  constexpr float iconSize = 14.0f;
  constexpr float outlineWidth = 2.0f;
  const auto start = ImGui::GetCursorScreenPos();
  const ImVec2 end{start.x + iconSize, start.y + iconSize};
  const ImVec2 center{start.x + iconSize * 0.5f, start.y + iconSize * 0.5f};
  const auto style = pinIconStyle(type);
  auto *drawList = ImGui::GetWindowDrawList();
  const auto innerColor = IM_COL32(32, 32, 32, 255);
  const auto fill = connected ? style.color : innerColor;
  const auto drawPolygon = [&](const ImVec2 *points, int count) {
    drawList->AddConvexPolyFilled(points, count, fill);
    drawList->AddPolyline(points, count, style.color, ImDrawFlags_Closed,
                          outlineWidth);
  };

  switch (style.shape) {
  case PinIconShape::circle:
    drawList->AddCircleFilled(center, iconSize * 0.34f, fill, 12);
    drawList->AddCircle(center, iconSize * 0.34f, style.color, 12,
                        outlineWidth);
    break;
  case PinIconShape::square: {
    const ImVec2 min{start.x + 2.0f, start.y + 2.0f};
    const ImVec2 max{end.x - 2.0f, end.y - 2.0f};
    drawList->AddRectFilled(min, max, fill);
    drawList->AddRect(min, max, style.color, 0.0f, ImDrawFlags_None,
                      outlineWidth);
    break;
  }
  case PinIconShape::roundSquare: {
    const ImVec2 min{start.x + 1.5f, start.y + 2.5f};
    const ImVec2 max{end.x - 1.5f, end.y - 2.5f};
    drawList->AddRectFilled(min, max, fill, 3.0f);
    drawList->AddRect(min, max, style.color, 3.0f, ImDrawFlags_RoundCornersAll,
                      outlineWidth);
    break;
  }
  case PinIconShape::diamond: {
    const ImVec2 points[] = {{center.x, start.y + 1.0f},
                             {end.x - 1.0f, center.y},
                             {center.x, end.y - 1.0f},
                             {start.x + 1.0f, center.y}};
    drawPolygon(points, 4);
    break;
  }
  case PinIconShape::triangle: {
    const ImVec2 points[] = {{center.x, start.y + 1.0f},
                             {end.x - 1.0f, end.y - 2.0f},
                             {start.x + 1.0f, end.y - 2.0f}};
    drawPolygon(points, 3);
    break;
  }
  case PinIconShape::grid: {
    constexpr float cellSize = 4.0f;
    for (int y = 0; y < 2; ++y) {
      for (int x = 0; x < 2; ++x) {
        const ImVec2 min{start.x + 2.0f + x * 6.0f, start.y + 2.0f + y * 6.0f};
        const ImVec2 max{min.x + cellSize, min.y + cellSize};
        drawList->AddRectFilled(min, max, connected ? style.color : innerColor);
        drawList->AddRect(min, max, style.color);
      }
    }
    break;
  }
  }
  ImGui::Dummy(ImVec2(iconSize, iconSize));
}

static std::string_view
sceneName(const imvk::graph::Scene &scene,
          const GraphEditor::SceneTable &availableScenes) {
  auto found = std::ranges::find_if(availableScenes, [&](const auto &entry) {
    return &entry.second.get() == &scene;
  });
  if (found == availableScenes.end())
    return "Unregistered";
  return found->first;
}

static float nodeWidgetWidth(const imvk::graph::Node &node) {
  if (isa<imvk::graph::Constant<imvk::graph::IntegerScalarTy>>(&node))
    return 120.0f;
  if (isa<imvk::graph::Constant<imvk::graph::FormatTy>>(&node))
    return 360.0f;
  if (isa<imvk::graph::Constant<imvk::graph::ExtentsTy>>(&node))
    return 210.0f;
  if (isa<imvk::graph::RenderPass>(&node))
    return 160.0f;
  if (isa<imvk::graph::Barrier<imvk::graph::ImageTy>>(&node))
    return 300.0f;
  return 0.0f;
}

static const std::vector<VkFormat> &vulkanFormats() {
  static const auto formats = [] {
    // VkFormat's core values and the extension ranges defined by Vulkan 1.4.
    constexpr std::array ranges{
        std::pair{0, 184},
        std::pair{1000054000, 1000054007},
        std::pair{1000066000, 1000066013},
        std::pair{1000156000, 1000156033},
        std::pair{1000330000, 1000330003},
        std::pair{1000340000, 1000340001},
        std::pair{1000460000, 1000460000},
        std::pair{1000464000, 1000464000},
        std::pair{1000470000, 1000470001},
        std::pair{1000609000, 1000609013},
    };
    std::vector<VkFormat> result;
    for (const auto [first, last] : ranges) {
      for (int value = first; value <= last; ++value) {
        auto format = static_cast<VkFormat>(value);
        const std::string_view name = string_VkFormat(format);
        if (name.starts_with("VK_FORMAT_"))
          result.push_back(format);
      }
    }
    return result;
  }();
  return formats;
}

static bool containsCaseInsensitive(std::string_view text,
                                    std::string_view filter) {
  return std::search(text.begin(), text.end(), filter.begin(), filter.end(),
                     [](char lhs, char rhs) {
                       return std::toupper(static_cast<unsigned char>(lhs)) ==
                              std::toupper(static_cast<unsigned char>(rhs));
                     }) != text.end();
}

static std::optional<VkFormat> formatByName(std::string_view input) {
  auto found = std::ranges::find_if(vulkanFormats(), [&](VkFormat format) {
    const std::string_view name = string_VkFormat(format);
    return name.size() == input.size() && containsCaseInsensitive(name, input);
  });
  if (found == vulkanFormats().end())
    return std::nullopt;
  return *found;
}

struct FormatInputState {
  std::array<char, 128> text{};
  VkFormat value = VK_FORMAT_MAX_ENUM;
};

struct NodePopupState {
  ImVec2 anchor{};
  float width = 0.0f;
  bool openRequested = false;
  bool inputDeactivated = false;
  FormatInputState *formatInput = nullptr;
};

static void setFormatInputText(std::array<char, 128> &text, VkFormat format) {
  std::strncpy(text.data(), string_VkFormat(format), text.size() - 1);
  text.back() = '\0';
}

static bool drawFormatInput(imvk::graph::Constant<imvk::graph::FormatTy> &node,
                            float width, NodePopupState &popup) {
  static std::unordered_map<size_t, FormatInputState> states;
  auto &inputState = states[node.results().front().index()];
  auto &[text, synchronizedValue] = inputState;
  if (synchronizedValue != node.getValue()) {
    setFormatInputText(text, node.getValue());
    synchronizedValue = node.getValue();
  }

  ImGui::SetNextItemWidth(width);
  const bool submitted = ImGui::InputText("##value", text.data(), text.size(),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
  const bool deactivated = ImGui::IsItemDeactivatedAfterEdit();
  popup.anchor = {ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y};
  popup.width = width;
  popup.openRequested = ImGui::IsItemActivated();
  popup.inputDeactivated = deactivated;
  popup.formatInput = &inputState;

  bool changed = false;
  if (submitted || deactivated) {
    if (auto format = formatByName(text.data())) {
      if (*format != node.getValue()) {
        node.setValue(*format);
        synchronizedValue = *format;
        changed = true;
      }
      setFormatInputText(text, *format);
    }
  }
  return changed;
}

static bool drawNodeWidget(imvk::graph::Node &node,
                           const GraphEditor::SceneTable &availableScenes,
                           float contentWidth, NodePopupState &popup,
                           bool editable) {
  bool changed = false;
  ImGui::PushID(&node);
  if (auto *constant =
          dyn_cast<imvk::graph::Constant<imvk::graph::IntegerScalarTy>>(
              &node)) {
    if (editable) {
      auto value = static_cast<unsigned long long>(constant->getValue());
      ImGui::SetNextItemWidth(contentWidth);
      if (ImGui::InputScalar("##value", ImGuiDataType_U64, &value)) {
        constant->setValue(static_cast<size_t>(value));
        changed = true;
      }
    } else {
      ImGui::Text("%llu",
                  static_cast<unsigned long long>(constant->getValue()));
    }
  } else if (auto *constant =
                 dyn_cast<imvk::graph::Constant<imvk::graph::FormatTy>>(
                     &node)) {
    if (editable)
      changed = drawFormatInput(*constant, contentWidth, popup);
    else
      ImGui::TextUnformatted(string_VkFormat(constant->getValue()));
  } else if (auto *constant =
                 dyn_cast<imvk::graph::Constant<imvk::graph::ExtentsTy>>(
                     &node)) {
    const auto value = constant->getValue();
    if (editable) {
      unsigned components[] = {value.width, value.height, value.depth};
      ImGui::SetNextItemWidth(contentWidth);
      if (ImGui::InputScalarN("##value", ImGuiDataType_U32, components, 3)) {
        constant->setValue({components[0], components[1], components[2]});
        changed = true;
      }
    } else {
      ImGui::Text("%u x %u x %u", value.width, value.height, value.depth);
    }
  } else if (auto *renderPass = dyn_cast<imvk::graph::RenderPass>(&node)) {
    const auto currentName = sceneName(renderPass->scene(), availableScenes);
    if (editable) {
      popup.openRequested =
          ImGui::Button(currentName.data(), ImVec2(contentWidth, 0.0f));
      popup.anchor = {ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y};
      popup.width = contentWidth;
    } else {
      ImGui::TextUnformatted(currentName.data());
    }
  } else if (auto *barrier =
                 dyn_cast<imvk::graph::Barrier<imvk::graph::ImageTy>>(&node)) {
    const auto *useInfo =
        dyn_cast<const imvk::graph::ImageUseInfo>(barrier->uses()[0].info());
    const auto *defInfo = dyn_cast<const imvk::graph::ImageDefInfo>(
        &barrier->results()[0].info());
    if (useInfo && defInfo) {
      ImGui::Text("%s", string_VkImageLayout(useInfo->access.layout));
      ImGui::Text("->");
      ImGui::Text("%s", string_VkImageLayout(defInfo->access.layout));
    }
  }
  ImGui::PopID();
  return changed;
}

static bool drawNodePopup(imvk::graph::Node &node,
                          const GraphEditor::SceneTable &availableScenes,
                          NodePopupState &popup) {
  bool changed = false;
  const auto screenAnchor = ed::CanvasToScreen(popup.anchor);
  // Popups must be created outside node-editor's transformed canvas space.
  ed::Suspend();
  ImGui::PushID(&node);
  ImGui::SetNextWindowPos(screenAnchor, ImGuiCond_Always);
  ImGui::SetNextWindowSizeConstraints(ImVec2(popup.width, 0.0f),
                                      ImVec2(popup.width, 240.0f));

  if (auto *constant =
          dyn_cast<imvk::graph::Constant<imvk::graph::FormatTy>>(&node)) {
    if (popup.openRequested)
      ImGui::OpenPopup("##format_suggestions");
    const bool popupOpen = ImGui::BeginPopup(
        "##format_suggestions", ImGuiWindowFlags_NoFocusOnAppearing);
    if (popupOpen) {
      auto &[text, synchronizedValue] = *popup.formatInput;
      const std::string_view filter{text.data()};
      for (const auto format : vulkanFormats()) {
        const auto *name = string_VkFormat(format);
        if (!containsCaseInsensitive(name, filter))
          continue;
        const bool selected = format == constant->getValue();
        if (ImGui::Selectable(name, selected)) {
          if (!selected) {
            constant->setValue(format);
            changed = true;
          }
          synchronizedValue = format;
          setFormatInputText(text, format);
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::EndPopup();
    }
    if (popup.inputDeactivated && !popupOpen &&
        !formatByName(popup.formatInput->text.data()))
      setFormatInputText(popup.formatInput->text, constant->getValue());
  } else if (auto *renderPass = dyn_cast<imvk::graph::RenderPass>(&node)) {
    if (popup.openRequested)
      ImGui::OpenPopup("##scene");
    if (ImGui::BeginPopup("##scene")) {
      for (const auto &[name, sceneRef] : availableScenes) {
        const auto &scene = sceneRef.get();
        const bool selected = &scene == &renderPass->scene();
        const bool compatible = renderPass->acceptsScene(scene);
        ImGui::BeginDisabled(!compatible);
        if (ImGui::Selectable(name.c_str(), selected) && !selected)
          changed = renderPass->setScene(scene);
        ImGui::EndDisabled();
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndPopup();
    }
  }

  ImGui::PopID();
  ed::Resume();
  return changed;
}

static bool drawNode(imvk::graph::Node &node,
                     const GraphEditor::SceneTable &availableScenes,
                     bool editable) {
  const auto nodeId = ed::NodeId(&node);
  boost::static_string<50> title;
  displayName(node.name(), title);
  const auto uses = node.uses();
  const auto results = node.results();
  const auto rowCount = std::max(uses.size(), results.size());
  constexpr float iconSize = 14.0f;
  const auto iconSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
  const auto pinDecorationWidth = iconSize + iconSpacing;
  constexpr float pinGap = 32.0f;
  boost::container::small_vector<boost::static_string<50>, 10> m_useNames;
  boost::container::small_vector<boost::static_string<50>, 10> m_defNames;
  std::ranges::transform(uses, std::back_inserter(m_useNames), [](auto &&use) {
    boost::static_string<50> ret;
    pinName(use, ret);
    return ret;
  });
  std::ranges::transform(results, std::back_inserter(m_defNames),
                         [](auto &&def) {
                           boost::static_string<50> ret;
                           pinName(def, ret);
                           return ret;
                         });
  float contentWidth =
      std::max(ImGui::CalcTextSize(title.c_str()).x, nodeWidgetWidth(node));
  for (size_t index = 0; index < rowCount; ++index) {
    const auto inputWidth =
        index < uses.size() ? ImGui::CalcTextSize(m_useNames[index].c_str()).x +
                                  pinDecorationWidth
                            : 0.0f;
    const auto outputWidth =
        index < results.size()
            ? ImGui::CalcTextSize(m_defNames[index].c_str()).x +
                  pinDecorationWidth
            : 0.0f;
    contentWidth = std::max(contentWidth, inputWidth + pinGap + outputWidth);
  }

  ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(8, 4, 8, 8));
  ed::BeginNode(nodeId);

  const auto titleStart = ImGui::GetCursorScreenPos();
  ImGui::TextUnformatted(title.c_str());
  ImGui::SameLine(0.0f, 0.0f);
  const auto titleWidth = ImGui::CalcTextSize(title.c_str()).x;
  ImGui::Dummy(ImVec2(std::max(0.0f, contentWidth - titleWidth),
                      ImGui::GetTextLineHeight()));
  const auto titleEnd = ImGui::GetItemRectMax();
  ImGui::Dummy(ImVec2(0.0f, 4.0f));

  NodePopupState popup;
  bool stateChanged =
      drawNodeWidget(node, availableScenes, contentWidth, popup, editable);

  auto drawUse = [](auto &&use, auto &&name) {
    ed::BeginPin(ed::PinId(&use), ed::PinKind::Input);
    ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
    ed::PinPivotSize(ImVec2(0.0f, 0.0f));
    drawPinIcon(use.type(), ed::HasAnyLinks(ed::PinId(&use)));
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(name.c_str());
    ed::EndPin();
  };
  auto drawResult = [](auto &&result, auto &&name) {
    ed::BeginPin(ed::PinId(&result), ed::PinKind::Output);
    ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
    ed::PinPivotSize(ImVec2(0.0f, 0.0f));
    ImGui::TextUnformatted(name.c_str());
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    drawPinIcon(result.type(), ed::HasAnyLinks(ed::PinId(&result)));
    ed::EndPin();
  };

  for (size_t index = 0; index < rowCount; ++index) {
    const auto rowStart = ImGui::GetCursorPosX();
    if (index < uses.size())
      drawUse(uses[index], m_useNames[index]);
    if (index < results.size()) {
      const auto label = m_defNames[index];
      if (index < uses.size())
        ImGui::SameLine();
      ImGui::SetCursorPosX(rowStart + contentWidth -
                           ImGui::CalcTextSize(label.c_str()).x -
                           pinDecorationWidth);
      drawResult(results[index], label);
    }
  }

  ed::EndNode();
  if (ImGui::IsItemVisible()) {
    const ImVec2 headerMin{titleStart.x - 8.0f, titleStart.y - 4.0f};
    const ImVec2 headerMax{titleEnd.x + 8.0f, titleEnd.y + 4.0f};
    auto *drawList = ed::GetNodeBackgroundDrawList(nodeId);
    drawList->AddRectFilled(headerMin, headerMax, IM_COL32(48, 74, 102, 255),
                            ed::GetStyle().NodeRounding,
                            ImDrawFlags_RoundCornersTop);
    drawList->AddLine(ImVec2{headerMin.x, headerMax.y}, headerMax,
                      IM_COL32(255, 255, 255, 32));
  }
  ed::PopStyleVar();
  if (editable && popup.width > 0.0f)
    stateChanged |= drawNodePopup(node, availableScenes, popup);
  return stateChanged;
}

struct LinkCandidate {
  imvk::graph::Value *value = nullptr;
  imvk::graph::Use *use = nullptr;
};

static LinkCandidate findLinkCandidate(imvk::graph::Workflow &workflow,
                                       ed::PinId first, ed::PinId second) {
  LinkCandidate candidate;
  for (auto &node : workflow) {
    for (auto &value : node.results()) {
      const auto pin = ed::PinId(&value);
      if (pin == first || pin == second)
        candidate.value = &value;
    }
    for (auto &use : node.uses()) {
      const auto pin = ed::PinId(&use);
      if (pin == first || pin == second)
        candidate.use = &use;
    }
  }
  return candidate;
}

static std::optional<std::vector<imvk::graph::Node *>>
topologicalOrder(imvk::graph::Workflow &workflow,
                 const imvk::graph::Use &changedUse,
                 imvk::graph::Value &changedValue) {
  std::vector<imvk::graph::Node *> nodes;
  std::unordered_map<imvk::graph::Node *, size_t> indices;
  for (auto &node : workflow) {
    indices.emplace(&node, nodes.size());
    nodes.push_back(&node);
  }

  std::vector<std::vector<size_t>> successors(nodes.size());
  std::vector<size_t> predecessorCounts(nodes.size());
  for (auto *consumer : nodes) {
    for (auto &use : consumer->uses()) {
      imvk::graph::Value *value = nullptr;
      if (&use == &changedUse)
        value = &changedValue;
      else if (use.hasValue())
        value = &use.value();
      if (!value)
        continue;

      const auto producerIndex = indices.at(&value->node());
      const auto consumerIndex = indices.at(consumer);
      successors[producerIndex].push_back(consumerIndex);
      ++predecessorCounts[consumerIndex];
    }
  }

  std::vector<imvk::graph::Node *> order;
  std::vector<bool> emitted(nodes.size());
  order.reserve(nodes.size());
  while (order.size() != nodes.size()) {
    size_t next = nodes.size();
    for (size_t index = 0; index < nodes.size(); ++index) {
      if (!emitted[index] && predecessorCounts[index] == 0) {
        next = index;
        break;
      }
    }
    if (next == nodes.size())
      return std::nullopt;

    emitted[next] = true;
    order.push_back(nodes[next]);
    for (const auto successor : successors[next])
      --predecessorCounts[successor];
  }
  return order;
}

static bool handleLinkCreation(imvk::graph::Workflow &workflow) {
  bool changed = false;
  if (ed::BeginCreate()) {
    ed::PinId first;
    ed::PinId second;
    if (ed::QueryNewLink(&first, &second) && first && second) {
      const auto candidate = findLinkCandidate(workflow, first, second);
      const bool hasBothKinds = candidate.value && candidate.use;
      const bool differentNodes =
          hasBothKinds && &candidate.value->node() != &candidate.use->user();
      const bool compatibleTypes =
          hasBothKinds && &candidate.value->type() == &candidate.use->type();
      auto order =
          differentNodes && compatibleTypes
              ? topologicalOrder(workflow, *candidate.use, *candidate.value)
              : std::nullopt;

      if (!order) {
        ed::RejectNewItem(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), 2.0f);
      } else if (ed::AcceptNewItem(ImVec4(0.25f, 1.0f, 0.25f, 1.0f), 2.0f)) {
        candidate.use->replaceBy(candidate.value);
        workflow.reorder(*order);
        changed = true;
      }
    }
  }
  ed::EndCreate();
  return changed;
}

static imvk::graph::Node *findNode(imvk::graph::Workflow &workflow,
                                   ed::NodeId id) {
  auto found = std::ranges::find_if(
      workflow, [id](auto &node) { return ed::NodeId(&node) == id; });
  return found == workflow.end() ? nullptr : &*found;
}

static imvk::graph::Use *findUse(imvk::graph::Workflow &workflow,
                                 ed::LinkId id) {
  for (auto &node : workflow) {
    auto found = std::ranges::find_if(
        node.uses(), [id](auto &use) { return ed::LinkId(&use) == id; });
    if (found != node.uses().end())
      return &*found;
  }
  return nullptr;
}

static bool handleDeletion(imvk::graph::Workflow &workflow) {
  boost::container::small_vector<imvk::graph::Node *, 4> deletedNodes;
  boost::container::small_vector<imvk::graph::Use *, 4> deletedLinks;
  if (ed::BeginDelete()) {
    ed::NodeId nodeId;
    while (ed::QueryDeletedNode(&nodeId)) {
      if (auto *node = findNode(workflow, nodeId);
          node && ed::AcceptDeletedItem())
        deletedNodes.push_back(node);
    }

    ed::LinkId linkId;
    while (ed::QueryDeletedLink(&linkId)) {
      if (auto *use = findUse(workflow, linkId); use && ed::AcceptDeletedItem())
        deletedLinks.push_back(use);
    }
  }
  ed::EndDelete();

  for (auto *use : deletedLinks)
    use->replaceBy(nullptr);
  for (auto *node : deletedNodes)
    workflow.erase(node);
  return !deletedNodes.empty() || !deletedLinks.empty();
}

static imvk::graph::Node *
drawCreateNodeMenu(imvk::graph::Workflow &workflow,
                   const GraphEditor::SceneTable &availableScenes) {
  imvk::graph::Node *created = nullptr;
  auto create = [&]<typename T>(auto &&...args) {
    created = imvk::graph::WorkflowBuilder{workflow, workflow.end()}.create<T>(
        std::forward<decltype(args)>(args)...);
  };
  auto &context = workflow.context();
  const auto &imageType =
      context.types().get<imvk::graph::ImageTy>(VK_IMAGE_TYPE_2D);

  if (ImGui::BeginMenu("Constants")) {
    if (ImGui::MenuItem("Integer"))
      create.template
      operator()<imvk::graph::Constant<imvk::graph::IntegerScalarTy>>(
          size_t{0});
    if (ImGui::MenuItem("Format"))
      create.template operator()<imvk::graph::Constant<imvk::graph::FormatTy>>(
          VK_FORMAT_UNDEFINED);
    if (ImGui::MenuItem("Extents"))
      create.template operator()<imvk::graph::Constant<imvk::graph::ExtentsTy>>(
          VkExtent3D{1, 1, 1});
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("Make Image"))
    create.template operator()<imvk::graph::MakeImage>(imageType);
  if (ImGui::MenuItem("Acquire Image"))
    create.template operator()<imvk::graph::AcquireImage>();
  if (ImGui::MenuItem("Get Extents"))
    create.template operator()<imvk::graph::GetExtents>();
  if (ImGui::MenuItem("Assume Compatible Image Format"))
    create.template operator()<imvk::graph::AssumeCompatibleFormat>();
  if (ImGui::MenuItem("Convert Image Format"))
    create.template operator()<imvk::graph::ConvertFormat>();
  if (ImGui::MenuItem("Clone Image"))
    create.template operator()<imvk::graph::Clone<imvk::graph::ImageTy>>();
  if (ImGui::MenuItem("Copy Image"))
    create.template operator()<imvk::graph::Copy<imvk::graph::ImageTy>>();
  if (ImGui::MenuItem("Image Barrier"))
    create.template operator()<imvk::graph::Barrier<imvk::graph::ImageTy>>(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
  if (ImGui::BeginMenu("Render Pass")) {
    if (availableScenes.empty())
      ImGui::MenuItem("No scenes available", nullptr, false, false);
    for (const auto &[name, scene] : availableScenes) {
      if (ImGui::MenuItem(name.c_str()))
        create.template operator()<imvk::graph::RenderPass>(scene.get());
    }
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("Present Image"))
    create.template operator()<imvk::graph::Present>();
  return created;
}

void GraphEditor::onRecord(vkw::BufferRecorder &commands, const Frame &frame) {
  if (m_needRematerialization) {
    m_matCtx.reset();
    m_materializedWorkflow = m_currentWorkflow;
    m_inject_into_workflow(m_materializedWorkflow);
    m_matCtx.emplace(m_me, m_materializedWorkflow);
    m_materializedCtx.reset(createEditorContext());
    m_needMaterializedUntangleLayout = true;
    m_hasUnmaterializedChanges = false;
    m_needRematerialization = false;
  }
  assert(m_matCtx);
  m_matCtx->run(commands, frame);
}

void GraphEditor::m_request_rematerialization() {
  if (auto error = imvk::graph::verifyWorkflow(m_currentWorkflow)) {
    m_verificationLog = VerificationLogEntry{
        error->error ? std::string(error->error->what())
                     : std::string("unknown workflow verification error"),
        error->location};
    m_showVerificationLog = true;
    return;
  }

  m_clear_verification_log();
  m_needRematerialization = true;
}

void GraphEditor::m_clear_verification_log() {
  m_verificationLog.reset();
  m_showVerificationLog = false;
}

static std::optional<size_t> nodeIndex(const imvk::graph::Workflow &workflow,
                                       const imvk::graph::Node *needle) {
  size_t index = 0;
  for (auto &node : workflow) {
    if (&node == needle)
      return index;
    ++index;
  }
  return std::nullopt;
}

void GraphEditor::m_draw_verification_log() {
  if (!m_showVerificationLog)
    return;

  ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Graph verification log", &m_showVerificationLog)) {
    ImGui::End();
    return;
  }

  if (!m_verificationLog) {
    ImGui::TextDisabled("No workflow verification errors.");
    ImGui::End();
    return;
  }

  const auto &entry = *m_verificationLog;
  const auto &location = entry.location;
  ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.3f, 1.0f),
                     "Workflow verification failed");
  ImGui::Separator();
  ImGui::TextWrapped("%s", entry.message.c_str());
  ImGui::Spacing();

  auto *focusNode = location.node;
  if (!focusNode && location.use)
    focusNode = &location.use->user();
  if (!focusNode && location.value && !location.value->isNull())
    focusNode = &location.value->node();

  if (focusNode) {
    boost::static_string<50> name;
    displayName(focusNode->name(), name);
    if (const auto index = nodeIndex(m_currentWorkflow, focusNode))
      ImGui::Text("Node: %s (#%llu, id=%p)", name.c_str(),
                  static_cast<unsigned long long>(*index),
                  static_cast<void *>(focusNode));
    else
      ImGui::Text("Node: %s (id=%p)", name.c_str(),
                  static_cast<void *>(focusNode));
  }

  if (location.use) {
    boost::static_string<50> name;
    pinName(*location.use, name);
    ImGui::Text("Input pin: %s (id=%p)", name.c_str(),
                static_cast<void *>(location.use));
    if (location.use->hasValue())
      ImGui::Text("Link: id=%p", static_cast<void *>(location.use));
  }

  if (location.value) {
    boost::static_string<50> name;
    pinName(*location.value, name);
    ImGui::Text("Output pin: %s (id=%p)", name.c_str(),
                static_cast<void *>(location.value));
  }

  if (focusNode) {
    ImGui::Spacing();
    if (ImGui::Button(location.use && location.use->hasValue()
                          ? "Focus link"
                          : "Focus node")) {
      ed::ClearSelection();
      if (location.use && location.use->hasValue())
        ed::SelectLink(ed::LinkId(location.use));
      else
        ed::SelectNode(ed::NodeId(focusNode));
      ed::NavigateToSelection(true);
    }
  }

  ImGui::End();
}

void GraphEditor::m_draw_materialized_workflow() {
  if (!m_showMaterializedWorkflow)
    return;

  ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Materialized workflow", &m_showMaterializedWorkflow)) {
    ImGui::End();
    return;
  }

  ImGui::TextDisabled(
      "Read-only graph after materialization (including inserted copies and "
      "barriers)");
  ImGui::Separator();

  ed::SetCurrentEditor(m_materializedCtx.get());
  ed::PushStyleVar(ed::StyleVar_PivotSize, ImVec2(3, 3));
  ed::Begin("Materialized render graph", ImVec2(0, 0));
  for (auto &node : m_materializedWorkflow)
    drawNode(node, m_availableScenes, false);
  for (auto &node : m_materializedWorkflow) {
    for (auto &use : node.uses()) {
      if (use.hasValue())
        ed::Link(ed::LinkId(&use), ed::PinId(&use.value()), ed::PinId(&use));
    }
  }
  if (m_needMaterializedUntangleLayout) {
    untangleLayout(m_materializedWorkflow);
    m_needMaterializedUntangleLayout = false;
  }
  ed::PopStyleVar();
  ed::End();
  ed::SetCurrentEditor(m_ctx.get());

  ImGui::End();
}

void GraphEditor::onGui(GraphScene &scene, const Frame &frame) {

  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
  auto size = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
  const auto windowBorderSize = ImGui::GetStyle().WindowBorderSize;
  const auto windowRounding = ImGui::GetStyle().WindowRounding;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("Content", nullptr, flags);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, windowBorderSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, windowRounding);

  auto &workflow = m_currentWorkflow;
  ImGui::Text("fps: %.2f, nodes: %lld", m_me.window().clock().fps(),
              std::distance(workflow.begin(), workflow.end()));
  ImGui::SameLine();
  ImGui::BeginDisabled(!m_hasUnmaterializedChanges || m_needRematerialization);
  if (ImGui::Button("Rematerialize"))
    m_request_rematerialization();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::Checkbox("Materialized workflow", &m_showMaterializedWorkflow);
  ImGui::Separator();
  ed::SetCurrentEditor(m_ctx.get());
  ed::PushStyleVar(ed::StyleVar_PivotSize, ImVec2(3, 3));
  ed::Begin("Render graph", ImVec2(0, 0));
  bool workflowChanged = false;
  for (auto &node : workflow)
    workflowChanged |= drawNode(node, m_availableScenes, true);
  for (auto &node : workflow) {
    for (auto &&[index, use] : std::views::enumerate(node.uses())) {
      if (!use.hasValue())
        continue;
      ed::Link(ed::LinkId(&use), ed::PinId(&use.value()), ed::PinId(&use));
    }
  }
  workflowChanged |= handleLinkCreation(workflow);
  workflowChanged |= handleDeletion(workflow);
  ed::Suspend();
  if (ed::ShowBackgroundContextMenu())
    ImGui::OpenPopup("Create New Node");
  if (ImGui::BeginPopup("Create New Node")) {
    const auto position =
        ed::ScreenToCanvas(ImGui::GetMousePosOnOpeningCurrentPopup());
    if (auto *created = drawCreateNodeMenu(workflow, m_availableScenes)) {
      ed::SetNodePosition(ed::NodeId(created), position);
      workflowChanged = true;
    }
    ImGui::EndPopup();
  }
  ed::Resume();
  if (m_needUntangleLayout) {
    untangleLayout(workflow);
    m_needUntangleLayout = false;
  }
  ed::PopStyleVar(1);
  ed::End();
  if (workflowChanged) {
    m_hasUnmaterializedChanges = true;
    m_clear_verification_log();
  }
  ImGui::PopStyleVar(2);
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::Begin("scene");
  GUI::setNoAlpha();
  GUI::filterLinear();
  ImGui::Image(scene.resultBuffer(), ImGui::GetContentRegionAvail());
  GUI::reset();
  ImGui::End();
  m_draw_materialized_workflow();
  m_draw_verification_log();
}

void GraphEditor::EditorDeleter::operator()(
    ax::NodeEditor::EditorContext *ctx) const {
  ed::DestroyEditor(ctx);
}

} // namespace imvk::examples