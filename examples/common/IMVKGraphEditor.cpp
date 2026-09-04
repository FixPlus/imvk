
#include "IMVKGraphEditor.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <cctype>
#include <iostream>
#include <sstream>

namespace ed = ax::NodeEditor;

namespace imvk::examples {
GraphEditor::GraphEditor(const MaterializationEnvironment &me,
                         imvk::graph::Workflow initialWorkflow,
                         SceneTable availableScenes)
    : m_me(me), m_ctx([]() {
        ed::Config config;
        auto *context = ed::CreateEditor(&config);
        return context;
      }()),
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

static void untangleLayout(imvk::graph::Workflow &wf) {}

static std::string displayName(std::string_view name) {
  std::string result{name};
  bool capitalize = true;
  for (auto &character : result) {
    if (character == '_') {
      character = ' ';
      capitalize = true;
    } else if (capitalize) {
      character = static_cast<char>(
          std::toupper(static_cast<unsigned char>(character)));
      capitalize = false;
    }
  }
  return result;
}

static std::string typeName(const imvk::graph::Type &type) {
  std::ostringstream stream;
  stream << type;
  return stream.str();
}

static std::string pinName(const imvk::graph::Use &use) {
  if (use.info() && !use.info()->name().empty())
    return std::string{use.info()->name()};
  return typeName(use.value().type());
}

static std::string pinName(const imvk::graph::Value &value) {
  if (value.infoOrNull() && !value.infoOrNull()->name().empty())
    return std::string{value.infoOrNull()->name()};
  return typeName(value.type());
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
  if (isa<imvk::graph::DescriptorTy>(&type))
    return {PinIconShape::triangle, IM_COL32(218, 85, 183, 255)};
  if (isa<imvk::graph::ArrayTy>(&type))
    return {PinIconShape::grid, IM_COL32(92, 210, 210, 255)};
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
  if (isa<imvk::graph::Constant<imvk::graph::ExtentsTy>>(&node))
    return 210.0f;
  if (isa<imvk::graph::RenderPass>(&node))
    return 160.0f;
  return 0.0f;
}

static bool drawNodeWidget(imvk::graph::Node &node,
                           const GraphEditor::SceneTable &availableScenes,
                           float contentWidth) {
  bool changed = false;
  ImGui::PushID(&node);
  if (auto *constant =
          dyn_cast<imvk::graph::Constant<imvk::graph::IntegerScalarTy>>(
              &node)) {
    auto value = static_cast<unsigned long long>(constant->getValue());
    ImGui::SetNextItemWidth(contentWidth);
    if (ImGui::InputScalar("##value", ImGuiDataType_U64, &value)) {
      constant->setValue(static_cast<size_t>(value));
      changed = true;
    }
  } else if (auto *constant =
                 dyn_cast<imvk::graph::Constant<imvk::graph::ExtentsTy>>(
                     &node)) {
    auto value = constant->getValue();
    unsigned components[] = {value.width, value.height, value.depth};
    ImGui::SetNextItemWidth(contentWidth);
    if (ImGui::InputScalarN("##value", ImGuiDataType_U32, components, 3)) {
      constant->setValue({components[0], components[1], components[2]});
      changed = true;
    }
  } else if (auto *renderPass = dyn_cast<imvk::graph::RenderPass>(&node)) {
    const auto currentName = sceneName(renderPass->scene(), availableScenes);
    ImGui::SetNextItemWidth(contentWidth);
    if (ImGui::BeginCombo("##scene", currentName.data())) {
      for (const auto &[name, sceneRef] : availableScenes) {
        const auto &scene = sceneRef.get();
        const bool selected = &scene == &renderPass->scene();
        const bool compatible = renderPass->acceptsScene(scene);
        ImGui::BeginDisabled(!compatible);
        if (ImGui::Selectable(name.c_str(), selected) && !selected) {
          changed = renderPass->setScene(scene);
        }
        ImGui::EndDisabled();
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  }
  ImGui::PopID();
  return changed;
}

static bool drawNode(imvk::graph::Node &node,
                     const GraphEditor::SceneTable &availableScenes) {
  const auto nodeId = ed::NodeId(&node);
  const auto title = displayName(node.name());
  const auto uses = node.uses();
  const auto results = node.results();
  const auto rowCount = std::max(uses.size(), results.size());
  constexpr float iconSize = 14.0f;
  const auto iconSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
  const auto pinDecorationWidth = iconSize + iconSpacing;
  constexpr float pinGap = 32.0f;
  float contentWidth =
      std::max(ImGui::CalcTextSize(title.c_str()).x, nodeWidgetWidth(node));
  for (size_t index = 0; index < rowCount; ++index) {
    const auto inputWidth =
        index < uses.size()
            ? ImGui::CalcTextSize(pinName(uses[index]).c_str()).x +
                  pinDecorationWidth
            : 0.0f;
    const auto outputWidth =
        index < results.size()
            ? ImGui::CalcTextSize(pinName(results[index]).c_str()).x +
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

  const bool stateChanged = drawNodeWidget(node, availableScenes, contentWidth);

  auto drawUse = [](auto &&use, const std::string &name) {
    ed::BeginPin(ed::PinId(&use), ed::PinKind::Input);
    ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
    ed::PinPivotSize(ImVec2(0.0f, 0.0f));
    drawPinIcon(use.value().type(), ed::HasAnyLinks(ed::PinId(&use)));
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(name.c_str());
    ed::EndPin();
  };
  auto drawResult = [](auto &&result, const std::string &name) {
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
      drawUse(uses[index], pinName(uses[index]));
    if (index < results.size()) {
      const auto label = pinName(results[index]);
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
  return stateChanged;
}

void GraphEditor::onRecord(vkw::BufferRecorder &commands, const Frame &frame) {
  if (m_needRematerialization) {
    m_matCtx.reset();
    m_materializedWorkflow = m_currentWorkflow;
    m_inject_into_workflow(m_materializedWorkflow);
    m_matCtx.emplace(m_me, m_materializedWorkflow);
    m_needRematerialization = false;
  }
  assert(m_matCtx);
  m_matCtx->run(commands, frame);
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
  ImGui::Separator();
  ed::SetCurrentEditor(m_ctx.get());
  ed::PushStyleVar(ed::StyleVar_PivotSize, ImVec2(3, 3));
  // ed::PushStyleVar(ed::StyleVar_PinArrowSize, 4.0f);
  // ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 4.0f);
  ed::Begin("Render graph", ImVec2(0, 0));
  for (auto &node : workflow)
    m_needRematerialization |= drawNode(node, m_availableScenes);
  for (auto &node : workflow) {
    for (auto &&[index, use] : std::views::enumerate(node.uses())) {
      ed::Link(ed::LinkId(&use), ed::PinId(&use.value()), ed::PinId(&use));
    }
  }
  if (m_needUntangleLayout) {
    /// TODO: implement
    untangleLayout(workflow);
    m_needUntangleLayout = false;
  }
  ed::PopStyleVar(1);
  ed::End();
  ImGui::PopStyleVar(2);
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::Begin("scene");
  ImGui::Image(scene.resultBuffer(), ImGui::GetContentRegionAvail());
  ImGui::End();
}

void GraphEditor::EditorDeleter::operator()(
    ax::NodeEditor::EditorContext *ctx) const {
  ed::DestroyEditor(ctx);
}

} // namespace imvk::examples