
#include "IMVKGraphEditor.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <iostream>

namespace ed = ax::NodeEditor;

namespace imvk::examples {
GraphEditor::GraphEditor(const MaterializationEnvironment &me,
                         imvk::graph::Workflow initialWorkflow)
    : m_me(me), m_ctx([]() {
        ed::Config config;
        auto *context = ed::CreateEditor(&config);
        return context;
      }()),
      m_scene(GraphScene::get([this](GraphScene &scene, const Frame &frame) {
        onGui(scene, frame);
      })),
      m_currentWorkflow(std::move(initialWorkflow)),
      m_materializedWorkflow(m_currentWorkflow) {
  m_inject_into_workflow(m_materializedWorkflow);
  std::cout << m_materializedWorkflow << std::endl;
  m_matCtx.emplace(m_me, m_materializedWorkflow);
  std::cout << m_materializedWorkflow << std::endl;
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

  auto &workflow = m_materializedWorkflow;
  ImGui::Text("fps: %.2f, nodes: %lld", m_me.window().clock().fps(),
              std::distance(workflow.begin(), workflow.end()));
  ImGui::Separator();
  ed::SetCurrentEditor(m_ctx.get());
  ed::PushStyleVar(ed::StyleVar_PivotSize, ImVec2(3, 3));
  // ed::PushStyleVar(ed::StyleVar_PinArrowSize, 4.0f);
  // ed::PushStyleVar(ed::StyleVar_PinArrowWidth, 4.0f);
  ed::Begin("Render graph", ImVec2(0, 0));
  for (auto &node : workflow) {
    ed::BeginNode(ed::NodeId(&node));

    ImGui::Text("%s", node.name().data());
    auto usePin = [](auto &&use, auto index) {
      ed::BeginPin(ed::PinId(&use), ed::PinKind::Input);
      ImGui::Text("in %lld", index);
      ed::EndPin();
    };
    auto defPin = [](auto &&val, auto index) {
      ed::BeginPin(ed::PinId(&val), ed::PinKind::Output);
      ImGui::Text("out %lld", index);
      ed::EndPin();
    };
    for (auto &&[index, use_def] :
         std::views::enumerate(std::views::zip(node.uses(), node.results()))) {
      auto &&[use, val] = use_def;
      usePin(use, index);
      ImGui::SameLine();
      defPin(val, index);
    }
    if (std::ranges::size(node.uses()) < std::ranges::size(node.results()))
      for (auto &&[index, val] :
           node.results() | std::views::enumerate |
               std::views::drop(std::ranges::size(node.uses()))) {
        defPin(val, index);
      }
    else {
      for (auto &&[index, use] :
           node.uses() | std::views::enumerate |
               std::views::drop(std::ranges::size(node.results()))) {
        usePin(use, index);
      }
    }
    ed::EndNode();
  }
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