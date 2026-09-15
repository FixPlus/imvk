
#pragma once

#include "IMVKPipeline.hpp"
#include "IMVKScene.hpp"
#include "IMVKShaderLoader.hpp"
#include "IMVKgui.hpp"

#include "imvk/copy/Engine.hpp"
#include "imvk/graph/Materialization.hpp"
#include "imvk/graph/Nodes.hpp"
#include "imvk/graphics/Engine.hpp"

#include <map>
#include <optional>
#include <string>

namespace ax::NodeEditor {
struct EditorContext;
}

namespace imvk::examples {

class GraphScene : public imvk::graph::MatScene {
public:
  GraphScene(const MaterializationEnvironment &me,
             const imvk::graph::Scene::MaterializationInfo &sceneInfo,
             auto &&onGui)
      : m_gui(me.window(), me.engine(), me.copyEngine(), me.shaderLoader()),
        m_pp(me.pipelinePool()), m_onGui(std::forward<decltype(onGui)>(onGui)) {
    std::tie(m_resultID, std::ignore) =
        m_gui.addImage(sceneInfo.descriptors.front().descriptor);
    assert(sceneInfo.descriptors.size() == 1);
    m_gui.updateRenderingInfo(sceneInfo.renderingInfo,
                              sceneInfo.framebufferInfo);
  }
  static imvk::graph::Scene get(auto &&onGui) {
    imvk::graph::Scene ret;
    using enum imvk::graph::ImageAttachmentUseInfo::Kind;
    using enum imvk::graph::ImageAttachmentUseInfo::LoadOp;
    ret.attachments.emplace_back(color, clear);
    ret.descriptors.emplace_back(
        imvk::graph::DescriptorUseInfo::sampledImage());
    ret.materialization =
        [onGui = std::move(onGui)](
            const imvk::graph::MaterializationEnvironment &envBase,
            const imvk::graph::Scene::MaterializationInfo &sceneInfo) {
          assert(isa<imvk::examples::MaterializationEnvironment>(&envBase));
          auto &env =
              static_cast<const imvk::examples::MaterializationEnvironment &>(
                  envBase);
          return std::make_unique<GraphScene>(env, sceneInfo, onGui);
        };
    return ret;
  }
  void onDraw(vkw::RenderPassRecorder &commands, const Frame &frame) override {
    PipelineManager mng{m_pp, commands, frame};
    m_gui.gui([this, &frame]() { m_onGui(*this, frame); });
    m_gui.draw(mng, commands, frame);
  }

  ImTextureID resultBuffer() const { return m_resultID; }

private:
  GUI m_gui;
  PipelinePool &m_pp;
  std::function<void(GraphScene &, const Frame &)> m_onGui;
  ImTextureID m_resultID;
};

class GraphEditor {
public:
  using SceneTable =
      std::map<std::string, std::reference_wrapper<const imvk::graph::Scene>>;
  using ComputeContextTable =
      std::map<std::string,
               std::reference_wrapper<const imvk::graph::ComputeContext>>;

  GraphEditor(const MaterializationEnvironment &me,
              imvk::graph::Workflow initialWorkflow,
              SceneTable availableScenes = {},
              ComputeContextTable availableComputeContexts = {});

  void onRecord(vkw::BufferRecorder &commands, const Frame &frame);

private:
  struct VerificationLogEntry {
    std::string message;
    imvk::graph::VerifyError::Location location;
  };

  class EditorDeleter {
  public:
    void operator()(ax::NodeEditor::EditorContext *ctx) const;
  };
  void onGui(GraphScene &scene, const Frame &frame);
  void m_inject_into_workflow(imvk::graph::Workflow &wf);
  void m_request_rematerialization();
  void m_draw_verification_log();
  void m_draw_materialized_workflow();
  void m_clear_verification_log();
  const MaterializationEnvironment &m_me;
  std::unique_ptr<ax::NodeEditor::EditorContext, EditorDeleter> m_ctx;
  std::unique_ptr<ax::NodeEditor::EditorContext, EditorDeleter>
      m_materializedCtx;
  imvk::graph::Scene m_scene;
  SceneTable m_availableScenes;
  ComputeContextTable m_availableComputeContexts;
  imvk::graph::Workflow m_currentWorkflow;
  imvk::graph::Workflow m_materializedWorkflow;
  std::optional<imvk::graph::MaterializationContext> m_matCtx;
  bool m_needUntangleLayout = true;
  bool m_needMaterializedUntangleLayout = true;
  bool m_hasUnmaterializedChanges = false;
  bool m_needRematerialization = false;
  bool m_showMaterializedWorkflow = false;
  bool m_showVerificationLog = false;
  bool m_sceneImageFilterLinear = true;
  std::optional<VerificationLogEntry> m_verificationLog;
};

} // namespace imvk::examples
