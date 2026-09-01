
#include "IMVKGraphEditor.hpp"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <iostream>

namespace imvk::examples {
GraphEditor::GraphEditor(const MaterializationEnvironment &me,
                         imvk::graph::Workflow initialWorkflow)
    : m_me(me),
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
void GraphEditor::onGui(GraphScene &scene, const Frame &frame) {
  ImGui::Begin("scene");
  ImGui::Image(scene.resultBuffer(), ImVec2(800, 600));
  ImGui::End();
}

} // namespace imvk::examples