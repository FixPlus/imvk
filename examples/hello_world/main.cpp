#include "IMVKBuffers.hpp"
#include "IMVKDevice.hpp"
#include "IMVKGraphEditor.hpp"
#include "IMVKModel.hpp"
#include "IMVKPipeline.hpp"
#include "IMVKScene.hpp"
#include "IMVKShaderLoader.hpp"
#include "IMVKTexture.hpp"
#include "IMVKWindow.hpp"
#include "IMVKgui.hpp"

#include "imvk/base/Context.hpp"
#include "imvk/base/Frame.hpp"
#include "imvk/copy/Engine.hpp"
#include "imvk/graph/Materialization.hpp"
#include "imvk/graph/Nodes.hpp"
#include "imvk/graphics/Engine.hpp"

#include <vkw/UniformBuffer.hpp>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>

std::atomic<size_t> totalAllocations;
std::atomic<size_t> totalFrees;
std::atomic<size_t> totalAllocated;

void *operator new(size_t size) {
  void *p = malloc(size);
  if (!p)
    throw std::bad_alloc{};
  totalAllocations.fetch_add(1ull, std::memory_order_relaxed);
  totalAllocated.fetch_add(size, std::memory_order_relaxed);
  return p;
}

void *operator new[](size_t size) {
  void *p = malloc(size);
  if (!p)
    throw std::bad_alloc{};
  totalAllocations.fetch_add(1ull, std::memory_order_relaxed);
  totalAllocated.fetch_add(size, std::memory_order_relaxed);
  return p;
}

void operator delete(void *p) {
  free(p);
  totalFrees.fetch_add(1u, std::memory_order_relaxed);
}

void operator delete[](void *p) {
  free(p);
  totalFrees.fetch_add(1u, std::memory_order_relaxed);
}

struct VertexInfo : public vkw::AttributeBase<vkw::VertexAttributeType::VEC2F,
                                              vkw::VertexAttributeType::VEC3F,
                                              vkw::VertexAttributeType::VEC2F> {
  float pos[2];
  float color[3];
  float uv[2];
};

struct Pos2D {
  float x = 0;
  float y = 0;
};
struct MyUniform {
  float vals[4];
};
using imvk::examples::UniformBuffer;
using imvk::examples::VertexBuffer;

std::array<VertexInfo, 3> getVerticesForFrame(float time, Pos2D pos,
                                              float scale) {
  std::array<VertexInfo, 3> ret;
  float phaseOffset = 0.0f;
  constexpr float period = 1.5;
  constexpr float pi = 3.141592f;
  const auto phase = time * 2.0f * pi / period;
  std::array<float, 9> colorTable{1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                  1.0f, 1.0f, 0.0f, 1.0f};

  for (auto &&i : std::ranges::iota_view{0u, 3u}) {
    auto &vertex = ret[i];
    vertex.pos[0] = pos.x + std::cos(phase + phaseOffset) * scale;
    vertex.pos[1] = pos.y + std::sin(phase + phaseOffset) * scale;
    vertex.uv[0] = (vertex.pos[0] + 1.0) / 2.0;
    vertex.uv[1] = (vertex.pos[1] + 1.0) / 2.0;

    std::copy(std::next(colorTable.begin(), i * 3),
              std::next(colorTable.begin(), (i + 1) * 3), vertex.color);
    phaseOffset += 2.0f * pi / 3.0f;
  }
  return ret;
}

class AllocLogger {
public:
  AllocLogger() = default;

  void stamp(unsigned framesPassed) {
    auto newAllocAcc = totalAllocations.load(std::memory_order_relaxed);
    auto newFreeAcc = totalFrees.load(std::memory_order_relaxed);
    auto newSizeAllocAcc = totalAllocated.load(std::memory_order_relaxed);

    std::cout << "a: "
              << static_cast<double>(newAllocAcc - allocAcc) /
                     static_cast<double>(framesPassed)
              << ", f: "
              << static_cast<double>(newFreeAcc - freeAcc) /
                     static_cast<double>(framesPassed)
              << ", b: "
              << static_cast<double>(newSizeAllocAcc - sizeAllocAcc) /
                     static_cast<double>(framesPassed)
              << std::endl;
    allocAcc = newAllocAcc;
    freeAcc = newFreeAcc;
    sizeAllocAcc = newSizeAllocAcc;
  }

private:
  size_t allocAcc = 0;
  size_t freeAcc = 0;
  size_t sizeAllocAcc = 0;
};

class MyCommandBufferImpl
    : public imvk::FONode<vkw::PrimaryCommandBuffer, imvk::fon_type::swap_mut,
                          MyCommandBufferImpl> {
public:
  MyCommandBufferImpl(imvk::FramedEngine &engine)
      : imvk::FONode<vkw::PrimaryCommandBuffer, imvk::fon_type::swap_mut,
                     MyCommandBufferImpl>(engine, [&](imvk::FrameID id) {
          return vkw::PrimaryCommandBuffer(engine.commandPool());
        }) {}

  void onUseAction(const imvk::Frame &frame, vkw::PrimaryCommandBuffer &obj) {
    // do nothing
  }
};

class MyCommandBuffer : public imvk::FONodeView<MyCommandBufferImpl> {
public:
  MyCommandBuffer(auto &&...args)
      : imvk::FONodeView<MyCommandBufferImpl>(
            std::forward<decltype(args)>(args)...) {}
};

class MySampleWidget {
public:
  MySampleWidget(imvk::GraphicsEngine &engine, imvk::examples::GUI &ui) {
    std::tie(m_imageId, m_image) = ui.addImage(imvk::NullDescriptor{engine});
  }

  void onGui(imvk::examples::Window &w) {
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    ImGui::Begin("offscreen");
    ImGui::Text("fps: %.2f", w.clock().fps());
    imvk::examples::GUI::setNoAlpha();
    ImGui::Image(m_imageId, ImVec2(300, 300));
    imvk::examples::GUI::reset();
    ImGui::End();
  }

  void updateImage(imvk::Descriptor desc) {
    m_image.getSet(3).replaceDescriptor(std::move(desc), /* binding */ 0);
  }

private:
  ImTextureID m_imageId;
  imvk::StageSet<imvk::examples::MaterialStage> m_image;
};

static imvk::graph::Value &
createScreenCompatibleImage(imvk::graph::WorkflowBuilder &builder,
                            imvk::graph::Value &extents, VkFormat format) {
  auto &c1 =
      builder.create<imvk::graph::Constant<imvk::graph::IntegerScalarTy>>(1)
          ->results()
          .front();
  auto &fmt =
      builder.create<imvk::graph::Constant<imvk::graph::FormatTy>>(format)
          ->results()
          .front();

  return builder.create<imvk::graph::MakeImage>(extents, fmt, c1, c1)
      ->results()
      .front();
}

static imvk::graph::Workflow
basicWorkflow(imvk::graph::Context &ctx, imvk::graph::Scene &mainScene,
              imvk::graph::Scene &offscreenScene,
              imvk::graph::ComputeContext &channelShuffle) {
  using enum imvk::graph::ImageAttachmentUseInfo::LoadOp;
  imvk::graph::Workflow workflow{ctx};
  imvk::graph::WorkflowBuilder builder{workflow, workflow.end()};
  auto &extents =
      builder.create<imvk::graph::ScreenExtents>()->results().front();
  imvk::graph::Value &image =
      createScreenCompatibleImage(builder, extents, VK_FORMAT_R8G8B8A8_UNORM);

  imvk::graph::Value &depthBuffer =
      createScreenCompatibleImage(builder, extents, VK_FORMAT_D32_SFLOAT);
  imvk::graph::Value &offscreenTexture =
      builder
          .create<imvk::graph::RenderPass>(std::array{&image},
                                           imvk::graph::Node::EmptyValues,
                                           offscreenScene)
          ->results()
          .front();
  imvk::graph::Value &texture =
      builder
          .create<imvk::graph::ComputePass>(std::array{&offscreenTexture},
                                            channelShuffle)
          ->results()
          .front();
  imvk::graph::Value &renderedImage =
      builder
          .create<imvk::graph::RenderPass>(std::array{&image, &depthBuffer},
                                           std::array{&texture}, mainScene)
          ->results()
          .front();
  builder.create<imvk::graph::Present>(renderedImage);
  return workflow;
}

class ChannelShuffleComputeContext final
    : public imvk::graph::MatComputeContext {
public:
  ChannelShuffleComputeContext(
      const imvk::examples::MaterializationEnvironment &env,
      const imvk::graph::ComputeContext::MaterializationInfo &computeInfo)
      : m_image([&]() {
          assert(computeInfo.descriptors.size() == 1);
          const auto *image = computeInfo.descriptors.front().image();
          assert(image);
          return *image;
        }()),
        m_layout(env.engine(), env.shaderLoader(), "channel_shuffle"),
        m_stageSet([&]() {
          auto builder = imvk::StageSetBuilder{env.engine(), m_layout};
          builder.addDescriptorSet(0).addDescriptor(
              computeInfo.descriptors.front().descriptor, 0);
          return builder;
        }()),
        m_pipeline(env.engine(), imvk::examples::ComputePipelineLayout(
                                     env.engine(), 0, m_layout)) {}

  static imvk::graph::ComputeContext get() {
    imvk::graph::ComputeContext ret{};
    ret.descriptors.emplace_back(
        imvk::graph::DescriptorUseInfo::storageImage(true));
    ret.materialization =
        [](const imvk::graph::MaterializationEnvironment &envBase,
           const imvk::graph::ComputeContext::MaterializationInfo
               &computeInfo) {
          assert(isa<imvk::examples::MaterializationEnvironment>(&envBase));
          const auto &env =
              static_cast<const imvk::examples::MaterializationEnvironment &>(
                  envBase);
          return std::make_unique<ChannelShuffleComputeContext>(env,
                                                                computeInfo);
        };
    return ret;
  }

  void onCompute(vkw::ComputePassRecorder &commands,
                 const imvk::Frame &frame) override {
    const auto &pipeline = m_pipeline->use(frame);
    const auto layout = m_pipeline.layout();
    const auto &pipelineLayout = layout->use(frame);
    const auto descriptorSet = m_stageSet.getSet(0);
    const auto &set = *descriptorSet->use(frame);
    const auto extents = m_image->info().extent;

    commands.bindPipeline(pipeline);
    commands.bindDescriptorSet(pipelineLayout, VK_PIPELINE_BIND_POINT_COMPUTE,
                               set, 0);
    commands.dispatch((extents.width + 15) / 16, (extents.height + 15) / 16, 1);
  }

private:
  imvk::graph::MatImage m_image;
  imvk::StageLayout<imvk::examples::ComputeStage> m_layout;
  imvk::StageSet<imvk::examples::ComputeStage> m_stageSet;
  imvk::examples::ComputePipeline m_pipeline;
};

static imvk::StageSet<imvk::examples::GeometryStage>
someCoolGeometry(imvk::GraphicsEngine &graphicsEngine,
                 imvk::examples::Window &window,
                 imvk::examples::ShaderLoader &shaderLoader) {
  auto myUniform = UniformBuffer<MyUniform, imvk::fon_type::swap_mut>(
      graphicsEngine,
      [&](const imvk::Frame &f, vkw::UniformBuffer<MyUniform> &u) {
        MyUniform uniValue;
        uniValue.vals[0] =
            std::sin(window.clock().totalTime().count() / 593.0) * 0.5 + 0.5;
        uniValue.vals[1] =
            std::cos(window.clock().totalTime().count() / 769.0 + 1.0) * 0.5 +
            0.5;
        uniValue.vals[2] =
            std::sin(window.clock().totalTime().count() / 947.0 + 2.0) * 0.5 +
            0.5;
        u.mapped().front() = uniValue;
        u.flush();
      });

  auto vsbuilder = imvk::StageSetBuilder{
      graphicsEngine, imvk::StageLayout<imvk::examples::GeometryStage>(
                          graphicsEngine, shaderLoader, "box",
                          std::make_unique<vkw::VertexInputStateCreateInfo<
                              vkw::per_vertex<VertexInfo, 0>>>())};
  vsbuilder.addDescriptorSet(1).addDescriptor(myUniform, 0);
  return vsbuilder;
}

class OffscreenScene : public imvk::graph::MatScene {
public:
  OffscreenScene(imvk::GraphicsEngine &ge, imvk::CopyEngine &ce,
                 imvk::examples::ShaderLoader &sl,
                 imvk::examples::PipelinePool &pp,
                 imvk::examples::Window &window,
                 const imvk::graph::Scene::MaterializationInfo &sceneInfo)
      : m_ge(ge), m_sl(sl), m_pp(pp),
        m_geometry(someCoolGeometry(ge, window, sl)),
        m_projection(imvk::StageSetBuilder{
            ge, imvk::StageLayout<imvk::examples::ProjectionStage>(
                    ge, sl, "identity")}),
        m_materialTexture(ge,
                          imvk::examples::Texture::load(
                              ge, ce, imvk::examples::assetsDir() / "image2")),
        m_material([&]() {
          auto layout = imvk::StageLayout<imvk::examples::MaterialStage>(
              ge, sl, "textured", vkw::RasterizationStateCreateInfo{});
          auto builder = imvk::StageSetBuilder{ge, layout};
          builder.addDescriptorSet(3).addDescriptor(
              imvk::examples::SampledView(ge, m_materialTexture), 0);
          return builder;
        }()),
        m_vertices(
            ge, 3,
            [&](const imvk::Frame &f, vkw::VertexBuffer<VertexInfo> &vbuf) {
              std::ranges::copy(
                  getVerticesForFrame(-window.clock().totalTime().count() /
                                          1000.0,
                                      Pos2D{}, /* scale */ 3.0f),
                  vbuf.mapped().begin());
              vbuf.flush();
            }) {
    m_lighting = imvk::StageSetBuilder{
        m_ge, imvk::StageLayout<imvk::examples::LightingStage>(
                  m_ge, m_sl, "identity", sceneInfo.renderingInfo)};
  }
  static imvk::graph::Scene get() {
    imvk::graph::Scene ret{};
    using enum imvk::graph::ImageAttachmentUseInfo::Kind;
    using enum imvk::graph::ImageAttachmentUseInfo::LoadOp;
    ret.attachments.emplace_back(color, clear);
    ret.materialization =
        [](const imvk::graph::MaterializationEnvironment &envBase,
           const imvk::graph::Scene::MaterializationInfo &sceneInfo) {
          assert(isa<imvk::examples::MaterializationEnvironment>(&envBase));
          auto &env =
              static_cast<const imvk::examples::MaterializationEnvironment &>(
                  envBase);
          return std::make_unique<OffscreenScene>(
              env.engine(), env.copyEngine(), env.shaderLoader(),
              env.pipelinePool(), env.window(), sceneInfo);
        };
    return ret;
  }
  void onDraw(vkw::RenderPassRecorder &commands,
              const imvk::Frame &frame) override {
    imvk::examples::PipelineManager mng{m_pp, commands, frame};
    mng.bind(m_geometry, m_projection, m_material, m_lighting);
    mng.bindPipeline();
    auto &vtx = m_vertices->use(frame);
    commands.bindVertexBuffer(vtx, 0, 0);
    commands.draw(vtx.size(), 1u);
  }

private:
  imvk::GraphicsEngine &m_ge;
  imvk::examples::ShaderLoader &m_sl;
  imvk::examples::PipelinePool &m_pp;
  imvk::StageSet<imvk::examples::GeometryStage> m_geometry;
  imvk::StageSet<imvk::examples::ProjectionStage> m_projection;
  imvk::examples::Texture m_materialTexture;
  imvk::StageSet<imvk::examples::MaterialStage> m_material;
  imvk::StageSet<imvk::examples::LightingStage> m_lighting = nullptr;
  VertexBuffer<VertexInfo, imvk::fon_type::swap_mut> m_vertices;
};

class FirstPersonCamera {
public:
  struct Uniform {
    glm::mat4 viewProjection{1.0f};
    glm::vec4 position{0.0f};
  };

  FirstPersonCamera(imvk::GraphicsEngine &engine,
                    imvk::examples::Window &window,
                    imvk::examples::ShaderLoader &shaderLoader)
      : m_window(window),
        m_uniform(
            engine,
            [this](const imvk::Frame &, vkw::UniformBuffer<Uniform> &buffer) {
              buffer.mapped().front() = m_uniformData();
              buffer.flush();
            }),
        m_projection([&]() {
          auto layout = imvk::StageLayout<imvk::examples::ProjectionStage>(
              engine, shaderLoader, "perspective");
          auto builder = imvk::StageSetBuilder{engine, layout};
          builder.addDescriptorSet(2).addDescriptor(m_uniform, 0);
          return imvk::StageSet<imvk::examples::ProjectionStage>(
              std::move(builder));
        }()) {}

  FirstPersonCamera(const FirstPersonCamera &) = delete;
  FirstPersonCamera &operator=(const FirstPersonCamera &) = delete;

  ~FirstPersonCamera() {
    if (m_looking)
      m_window.enableCursor();
  }

  void update() {
    auto *handle = m_window.rawHandle();
    const bool lookButtonDown =
        glfwGetMouseButton(handle, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (lookButtonDown && !m_lookButtonDown &&
        !ImGui::GetIO().WantCaptureMouse) {
      m_window.disableCursor();
      glfwGetCursorPos(handle, &m_lastCursorX, &m_lastCursorY);
      m_looking = true;
    } else if (!lookButtonDown && m_lookButtonDown) {
      m_window.enableCursor();
      m_looking = false;
    }
    m_lookButtonDown = lookButtonDown;

    if (m_looking) {
      double cursorX = 0.0;
      double cursorY = 0.0;
      glfwGetCursorPos(handle, &cursorX, &cursorY);
      m_yaw += static_cast<float>(cursorX - m_lastCursorX) * m_mouseSensitivity;
      m_pitch -=
          static_cast<float>(cursorY - m_lastCursorY) * m_mouseSensitivity;
      m_pitch = std::clamp(m_pitch, -m_pitchLimit, m_pitchLimit);
      m_lastCursorX = cursorX;
      m_lastCursorY = cursorY;
    }

    if (ImGui::GetIO().WantCaptureKeyboard)
      return;

    const auto deltaSeconds = static_cast<float>(
        std::min(m_window.clock().frameTime().count() / 1000.0, 0.1));
    const auto speed = glfwGetKey(handle, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                           ? m_fastSpeed
                           : m_moveSpeed;
    const auto distance = speed * deltaSeconds;
    const auto forward = m_forward();
    const auto right = glm::normalize(glm::cross(forward, m_worldUp));
    if (glfwGetKey(handle, GLFW_KEY_W) == GLFW_PRESS)
      m_position += forward * distance;
    if (glfwGetKey(handle, GLFW_KEY_S) == GLFW_PRESS)
      m_position -= forward * distance;
    if (glfwGetKey(handle, GLFW_KEY_D) == GLFW_PRESS)
      m_position += right * distance;
    if (glfwGetKey(handle, GLFW_KEY_A) == GLFW_PRESS)
      m_position -= right * distance;
    if (glfwGetKey(handle, GLFW_KEY_SPACE) == GLFW_PRESS)
      m_position += m_worldUp * distance;
    if (glfwGetKey(handle, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
      m_position -= m_worldUp * distance;
  }

  imvk::StageSet<imvk::examples::ProjectionStage> projection() const {
    return m_projection;
  }

private:
  glm::vec3 m_forward() const {
    return glm::normalize(glm::vec3{std::cos(m_pitch) * std::cos(m_yaw),
                                    std::sin(m_pitch),
                                    std::cos(m_pitch) * std::sin(m_yaw)});
  }

  Uniform m_uniformData() const {
    const auto [width, height] = m_window.getSize();
    const auto aspect = static_cast<float>(std::max(width, 1)) /
                        static_cast<float>(std::max(height, 1));
    auto projection =
        glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 200.0f);
    projection[1][1] *= -1.0f;
    const auto view =
        glm::lookAtRH(m_position, m_position + m_forward(), m_worldUp);
    return {.viewProjection = projection * view,
            .position = glm::vec4{m_position, 1.0f}};
  }

  static constexpr glm::vec3 m_worldUp{0.0f, 1.0f, 0.0f};
  static constexpr float m_moveSpeed = 5.0f;
  static constexpr float m_fastSpeed = 15.0f;
  static constexpr float m_mouseSensitivity = 0.0025f;
  static constexpr float m_pitchLimit = glm::radians(89.0f);

  imvk::examples::Window &m_window;
  glm::vec3 m_position{0.0f, 4.0f, 16.0f};
  float m_yaw = glm::radians(-90.0f);
  float m_pitch = 0.0f;
  double m_lastCursorX = 0.0;
  double m_lastCursorY = 0.0;
  bool m_lookButtonDown = false;
  bool m_looking = false;
  UniformBuffer<Uniform, imvk::fon_type::swap_mut> m_uniform;
  imvk::StageSet<imvk::examples::ProjectionStage> m_projection;
};

static_assert(sizeof(FirstPersonCamera::Uniform) == sizeof(float) * 20);

class ExampleEnvironment : public imvk::examples::MaterializationEnvironment {
public:
  ExampleEnvironment(imvk::GraphicsEngine &engine,
                     imvk::examples::Window &window)
      : imvk::examples::MaterializationEnvironment(engine, window),
        m_model("Sponza"),
        m_mat_model(m_model.materialize(engine, copyEngine(), shaderLoader())),
        m_camera(engine, window, shaderLoader()) {}

  const auto &model() { return m_mat_model; }
  auto &camera() { return m_camera; }

private:
  imvk::examples::GLTFModel m_model;
  imvk::examples::GLTFModel::Materialized m_mat_model;
  FirstPersonCamera m_camera;
};

class MainScene : public imvk::graph::MatScene {
public:
  MainScene(ExampleEnvironment &env,
            const imvk::graph::Scene::MaterializationInfo &sceneInfo)
      : m_env(env),
        m_gui(env.window(), env.engine(), env.copyEngine(), env.shaderLoader()),
        m_offscreenWidget(env.engine(), m_gui) {
    assert(sceneInfo.descriptors.size() == 1);
    m_offscreenWidget.updateImage(sceneInfo.descriptors.front().descriptor);
    m_gui.updateRenderingInfo(sceneInfo.renderingInfo,
                              sceneInfo.framebufferInfo);
    m_lighting = imvk::StageSetBuilder{
        m_env.engine(), imvk::StageLayout<imvk::examples::LightingStage>(
                            m_env.engine(), m_env.shaderLoader(), "identity",
                            sceneInfo.renderingInfo)};
  }
  static imvk::graph::Scene get() {
    imvk::graph::Scene ret{};
    using enum imvk::graph::ImageAttachmentUseInfo::Kind;
    using enum imvk::graph::ImageAttachmentUseInfo::LoadOp;
    ret.attachments.emplace_back(color, clear);
    ret.attachments.emplace_back(depth, clear);
    ret.descriptors.emplace_back(
        imvk::graph::DescriptorUseInfo::sampledImage());
    ret.materialization =
        [](const imvk::graph::MaterializationEnvironment &envBase,
           const imvk::graph::Scene::MaterializationInfo &sceneInfo) {
          assert(isa<imvk::examples::MaterializationEnvironment>(&envBase));
          auto *env = dynamic_cast<const ExampleEnvironment *>(&envBase);
          if (!env)
            throw std::runtime_error("Incorrect materialization environment");
          auto &mutEnv = const_cast<ExampleEnvironment &>(*env);
          return std::make_unique<MainScene>(mutEnv, sceneInfo);
        };
    return ret;
  }
  void onDraw(vkw::RenderPassRecorder &commands,
              const imvk::Frame &frame) override {
    m_gui.gui([&]() {
      ImGui::ShowDemoWindow();
      m_offscreenWidget.onGui(m_env.window());
      ImGui::Begin("camera controls");
      ImGui::TextUnformatted("Hold right mouse button to look");
      ImGui::TextUnformatted("W/A/S/D: move, Space/Ctrl: up/down");
      ImGui::TextUnformatted("Left Shift: move faster");
      ImGui::End();
    });
    m_env.camera().update();
    imvk::examples::PipelineManager mng{m_env.pipelinePool(), commands, frame};
    m_env.model().draw(mng, commands, frame, m_env.camera().projection(),
                       m_lighting);

    m_gui.draw(mng, commands, frame);
  }

private:
  ExampleEnvironment &m_env;
  imvk::examples::GUI m_gui;
  MySampleWidget m_offscreenWidget;
  imvk::StageSet<imvk::examples::LightingStage> m_lighting = nullptr;
};

int app() try {
  AllocLogger allocLogger{};
  // Open vulkan loader library, construct vulkan instance, pick
  // physical device and construct logical device.
  imvk::examples::Device imvkDevice{
      imvk::examples::DeviceCreateInfo{.enableValidation = true}};

  // Create presentable window and it's surface. This will be used as
  // swapchain factory.
  imvk::examples::WindowSettings windowSettings{
      .title = "Hello world", .width = 800, .height = 600};
  imvk::examples::Window window{windowSettings, imvkDevice.get().parent()};

  // Create instance of imvk context.
  imvk::ContextCreateInfo imvkCCI{};

  imvk::Context imvkContext{imvkDevice.get(), imvkDevice.getAllocator(),
                            imvkCCI};

  // Create graphics engine.
  imvk::GraphicsEngineCreateInfo eCi{.swapchainFactory = &window,
                                     .maxFramesInFlight = 2};
  auto graphicsEngine = imvk::GraphicsEngine(imvkContext, eCi);

  auto offscreenScene = OffscreenScene::get();
  auto mainScene = MainScene::get();
  auto channelShuffle = ChannelShuffleComputeContext::get();

  imvk::graph::Context graphCtx{};
  auto iniWf =
      basicWorkflow(graphCtx, mainScene, offscreenScene, channelShuffle);

  ExampleEnvironment matEnv{graphicsEngine, window};
  imvk::examples::GraphEditor::SceneTable availableScenes{
      {"Main", std::cref(mainScene)}, {"Offscreen", std::cref(offscreenScene)}};
  imvk::examples::GraphEditor::ComputeContextTable availableComputeContexts{
      {"channel_shuffle", std::cref(channelShuffle)}};
  imvk::examples::GraphEditor ged{matEnv, std::move(iniWf),
                                  std::move(availableScenes),
                                  std::move(availableComputeContexts)};

  auto commands = MyCommandBuffer(graphicsEngine);
  //  Main application loop.
  while (!window.shouldClose()) {
    window.pollEvents();
    if (window.clock().totalFrames() % 10000 == 0u) {
      allocLogger.stamp(10000);
    }
    graphicsEngine.submitFrame([&](const imvk::Frame &frame) {
      auto &cb = commands->use(frame);
      vkw::BufferRecorder recorder{cb,
                                   VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
      ged.onRecord(recorder, frame);
      vkw::SubmitInfo ret{};
      ret.addCommands(cb);
      return ret;
    });
  }

  return 0;
} catch (std::runtime_error &e) {
  std::cerr << "RUNTIME ERROR: " << e.what() << std::endl;
  return 1;
} catch (std::logic_error &e) {
  std::cerr << "LOGIC ERROR: " << e.what() << std::endl;
  return 1;
} catch (...) {
  std::cerr << "UNKNOWN ERROR" << std::endl;
  return 1;
}

int main() {
  int ret = 0;

  ret = app();
  std::cout << "Total allocations: "
            << totalAllocations.load(std::memory_order_acquire) << std::endl;
  std::cout << "Total frees: " << totalFrees.load(std::memory_order_acquire)
            << std::endl;
  std::cout << "Total bytes allocated: "
            << totalAllocated.load(std::memory_order_acquire) << std::endl;

  return ret;
}