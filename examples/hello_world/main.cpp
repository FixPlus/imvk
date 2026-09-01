#include "IMVKBuffers.hpp"
#include "IMVKDevice.hpp"
#include "IMVKGraphEditor.hpp"
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

#include <vkw/StagingBuffer.hpp>
#include <vkw/UniformBuffer.hpp>

#include <atomic>
#include <cmath>
#include <iostream>
#include <thread>

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
    ImGui::Image(m_imageId, ImVec2(300, 300));
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
createCopyExtents(imvk::graph::WorkflowBuilder &builder,
                  imvk::graph::Value &extentSource, VkFormat format) {
  auto &c1 =
      builder.create<imvk::graph::Constant<imvk::graph::IntegerScalarTy>>(1)
          ->results()
          .front();
  auto &fmt =
      builder
          .create<imvk::graph::Constant<imvk::graph::IntegerScalarTy>>(format)
          ->results()
          .front();
  auto &extents =
      builder.create<imvk::graph::GetExtents>(extentSource)->results().front();
  return builder
      .create<imvk::graph::MakeImage>(
          static_cast<const imvk::graph::ImageTy &>(extentSource.type()),
          extents, fmt, c1, c1)
      ->results()
      .front();
}

static imvk::graph::Workflow basicWorkflow(imvk::graph::Context &ctx,
                                           imvk::graph::Scene &mainScene,
                                           imvk::graph::Scene &offscreenScene) {
  using enum imvk::graph::ImageAttachmentUseInfo::LoadOp;
  imvk::graph::Workflow workflow{ctx};
  imvk::graph::WorkflowBuilder builder{workflow, workflow.end()};
  imvk::graph::Value &image =
      builder.create<imvk::graph::AcquireImage>()->results().front();
  imvk::graph::Value &offscreenBuffer =
      createCopyExtents(builder, image, VK_FORMAT_R8G8B8A8_UNORM);
  imvk::graph::Value &depthBuffer =
      createCopyExtents(builder, image, VK_FORMAT_D32_SFLOAT);
  imvk::graph::Value &texture =
      builder
          .create<imvk::graph::RenderPass>(std::array{&offscreenBuffer},
                                           imvk::graph::Node::EmptyValues,
                                           offscreenScene)
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

class MainScene : public imvk::graph::MatScene {
public:
  MainScene(const imvk::examples::MaterializationEnvironment &env,
            const imvk::graph::Scene::MaterializationInfo &sceneInfo)
      : m_env(env),
        m_gui(env.window(), env.engine(), env.copyEngine(), env.shaderLoader()),
        m_offscreenWidget(env.engine(), m_gui),
        m_geometry(
            someCoolGeometry(env.engine(), env.window(), env.shaderLoader())),
        m_projection(imvk::StageSetBuilder{
            env.engine(), imvk::StageLayout<imvk::examples::ProjectionStage>(
                              env.engine(), env.shaderLoader(), "identity")}),
        m_materialTexture(env.engine(),
                          imvk::examples::Texture::load(
                              env.engine(), env.copyEngine(),
                              imvk::examples::assetsDir() / "image1")),
        m_swapTexture(std::async(std::launch::deferred,
                                 [&]() {
                                   return imvk::examples::Texture::load(
                                       env.engine(), env.copyEngine(),
                                       imvk::examples::assetsDir() / "image2");
                                 })),
        m_material([&]() {
          auto layout = imvk::StageLayout<imvk::examples::MaterialStage>(
              env.engine(), env.shaderLoader(), "textured2",
              vkw::RasterizationStateCreateInfo{});
          auto builder = imvk::StageSetBuilder{env.engine(), layout};
          builder.addDescriptorSet(3).addDescriptor(
              imvk::examples::SampledView(env.engine(), m_materialTexture), 0);
          return builder;
        }()),
        m_vertices(
            env.engine(), 3,
            [&](const imvk::Frame &f, vkw::VertexBuffer<VertexInfo> &vbuf) {
              std::ranges::copy(
                  getVerticesForFrame(env.window().clock().totalTime().count() /
                                          1000.0,
                                      Pos2D{}, /* scale */ 0.75f),
                  vbuf.mapped().begin());
              vbuf.flush();
            }),
        m_anotherVertices(
            env.engine(), env.copyEngine(),
            getVerticesForFrame(0.5, Pos2D{0.3, 0.3}, /* scale */ 0.2f)),
        m_swapVertices(std::async(std::launch::deferred, [&]() {
          return imvk::examples::BufferImpl<
              VertexInfo, imvk::fon_type::cow,
              vkw::VertexBuffer<VertexInfo>>::create(env.engine(),
                                                     env.copyEngine(),
                                                     getVerticesForFrame(
                                                         0.5, Pos2D{0.3, 0.3},
                                                         /* scale */ 0.5f));
        })) {
    assert(sceneInfo.descriptors.size() == 1);
    m_offscreenWidget.updateImage(sceneInfo.descriptors.front());
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
          auto &env =
              static_cast<const imvk::examples::MaterializationEnvironment &>(
                  envBase);
          return std::make_unique<MainScene>(env, sceneInfo);
        };
    return ret;
  }
  void onDraw(vkw::RenderPassRecorder &commands,
              const imvk::Frame &frame) override {
    m_gui.gui([&]() {
      ImGui::ShowDemoWindow();
      m_offscreenWidget.onGui(m_env.window());
    });
    imvk::examples::PipelineManager mng{m_env.pipelinePool(), commands, frame};
    mng.bind(m_geometry, m_projection, m_material, m_lighting);
    mng.bindPipeline();
    auto &vertexBuffer = m_vertices->use(frame);
    commands.bindVertexBuffer(vertexBuffer, 0, 0);
    commands.draw(vertexBuffer.size(), 1u);

    auto &anotherBuffer = m_anotherVertices->use(frame);
    commands.bindVertexBuffer(anotherBuffer, 0, 0);
    commands.draw(anotherBuffer.size(), 1u);

    m_gui.draw(mng, commands, frame);
    m_updateCowVertices();
  }

private:
  void m_updateCowVertices() {
    auxCount++;
    if (auxCount < 2000)
      return;
    auxCount = 0;
    m_swapVertices = m_anotherVertices->exchange(m_swapVertices.get());
    m_swapTexture = m_materialTexture->exchange(m_swapTexture.get());
  }
  const imvk::examples::MaterializationEnvironment &m_env;
  imvk::examples::GUI m_gui;
  size_t auxCount = 0;
  MySampleWidget m_offscreenWidget;
  imvk::StageSet<imvk::examples::GeometryStage> m_geometry;
  imvk::StageSet<imvk::examples::ProjectionStage> m_projection;
  imvk::examples::Texture m_materialTexture;
  std::future<vkw::Image<vkw::COLOR, vkw::I2D>> m_swapTexture;
  imvk::StageSet<imvk::examples::MaterialStage> m_material;
  imvk::StageSet<imvk::examples::LightingStage> m_lighting = nullptr;
  VertexBuffer<VertexInfo, imvk::fon_type::swap_mut> m_vertices;
  VertexBuffer<VertexInfo, imvk::fon_type::cow> m_anotherVertices;
  std::future<vkw::VertexBuffer<VertexInfo>> m_swapVertices;
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

  // Create shader loader

  auto offscreenScene = OffscreenScene::get();
  auto mainScene = MainScene::get();

  imvk::graph::Context graphCtx{};
  auto iniWf = basicWorkflow(graphCtx, mainScene, offscreenScene);

  imvk::examples::MaterializationEnvironment matEnv{graphicsEngine, window};
  imvk::examples::GraphEditor ged{matEnv, std::move(iniWf)};

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