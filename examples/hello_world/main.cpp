#include "IMVKBasicRenderPass.hpp"
#include "IMVKBuffers.hpp"
#include "IMVKDevice.hpp"
#include "IMVKShaderLoader.hpp"
#include "IMVKTexture.hpp"
#include "IMVKWindow.hpp"

#include "imvk/base/Context.hpp"
#include "imvk/base/Frame.hpp"
#include "imvk/copy/Engine.hpp"
#include "imvk/graphics/Engine.hpp"
#if 1
#include "imvk/graph/Materialization.hpp"
#include "imvk/graph/Nodes.hpp"

#endif

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
    vertex.uv[0] = vertex.pos[0] + 0.5;
    vertex.uv[1] = vertex.pos[1] + 0.5;

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
          return engine.createObject<vkw::PrimaryCommandBuffer>(
              engine.commandPool());
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

imvk::graph::Value &createCopyExtents(imvk::graph::WorkflowBuilder &builder,
                                      imvk::graph::Value &extentSource,
                                      VkFormat format) {
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

imvk::graph::Value &renderImage(imvk::graph::WorkflowBuilder &builder,
                                imvk::graph::Value &image, auto &&descriptors) {

  return builder
      .create<imvk::graph::RenderPass>(
          std::array{imvk::graph::colorAttachment(image)},
          std::forward<decltype(descriptors)>(descriptors),
          [](const imvk::graph::RenderPass::PassInfo &info,
             vkw::RenderPassRecorder &rec, const imvk::Frame &frame) {
            // do nothing.
          })
      ->results()
      .front();
}

imvk::graph::Workflow basicWorkflow(imvk::graph::Context &ctx, auto &&passJob,
                                    auto &&offscreenPassJob) {
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
          .create<imvk::graph::RenderPass>(
              std::array{imvk::graph::colorAttachment(offscreenBuffer)},
              imvk::graph::Node::EmptyUses,
              std::forward<decltype(offscreenPassJob)>(offscreenPassJob))
          ->results()
          .front();
  imvk::graph::Value &renderedImage =
      builder
          .create<imvk::graph::RenderPass>(
              std::array{imvk::graph::colorAttachment(image),
                         imvk::graph::depthAttachment(depthBuffer)},
              std::array{imvk::graph::combinedImageSampler(texture)},
              std::forward<decltype(passJob)>(passJob))
          ->results()
          .front();
  builder.create<imvk::graph::Present>(renderedImage);
  return workflow;
}

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

  // Create shader loader
  imvk::examples::ShaderLoaderCreateInfo shaderLoaderCI{.shaderDirectory =
                                                            "assets/shaders"};
  imvk::examples::ShaderLoader shaderLoader{shaderLoaderCI};

  // Create instance of imvk context.
  imvk::ContextCreateInfo imvkCCI{};

  imvk::Context imvkContext{imvkDevice.get(), imvkDevice.getAllocator(),
                            imvkCCI};

  // Create graphics engine.
  imvk::GraphicsEngineCreateInfo eCi{.swapchainFactory = &window,
                                     .maxFramesInFlight = 2};
  auto graphicsEngine = imvk::GraphicsEngine(imvkContext, eCi);

  auto copyEngine = imvk::CopyEngine(imvkContext, imvk::CopyEngineCreateInfo{});

  auto vertexStage = imvk::StageLayout<imvk::examples::BasicVertexStage>(
      graphicsEngine, graphicsEngine, shaderLoader, "hello.vert",
      std::make_unique<
          vkw::VertexInputStateCreateInfo<vkw::per_vertex<VertexInfo, 0>>>());

  auto fragmentStage = imvk::StageLayout<imvk::examples::BasicFragmentStage>(
      graphicsEngine, graphicsEngine, shaderLoader, "hello.frag");
  auto fragmentStage2 = imvk::StageLayout<imvk::examples::BasicFragmentStage>(
      graphicsEngine, graphicsEngine, shaderLoader, "hello2.frag");
  auto pipelinePool =
      imvk::GraphicsPipelinePool<imvk::graph::RenderPass::PipeHook,
                                 imvk::examples::BasicVertexStage,
                                 imvk::examples::BasicFragmentStage>{
          graphicsEngine, /* cache size*/ 10u};
  auto vertices = VertexBuffer<VertexInfo, imvk::fon_type::swap_mut>(
      graphicsEngine, 3,
      [&](const imvk::Frame &f, vkw::VertexBuffer<VertexInfo> &vbuf) {
        std::ranges::copy(
            getVerticesForFrame(window.clock().totalTime().count() / 1000.0,
                                Pos2D{}, /* scale */ 0.75f),
            vbuf.mapped().begin());
        vbuf.flush();
      });
  auto moreVertices = VertexBuffer<VertexInfo, imvk::fon_type::swap_mut>(
      graphicsEngine, 3,
      [&](const imvk::Frame &f, vkw::VertexBuffer<VertexInfo> &vbuf) {
        std::ranges::copy(
            getVerticesForFrame(-window.clock().totalTime().count() / 1000.0,
                                Pos2D{}, /* scale */ 0.35f),
            vbuf.mapped().begin());
        vbuf.flush();
      });
  auto anotherVertices = VertexBuffer<VertexInfo, imvk::fon_type::cow>(
      graphicsEngine, copyEngine,
      getVerticesForFrame(0.5, Pos2D{0.3, 0.3}, /* scale */ 0.2f));
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
  auto myTexture = imvk::examples::Texture(
      graphicsEngine,
      imvk::examples::Texture::load(graphicsEngine, copyEngine,
                                    imvk::examples::assetsDir() / "image1"));
  auto myTextureView = imvk::examples::SampledView(graphicsEngine, myTexture);
  auto vertexStageSet =
      [&]() -> imvk::StageSet<imvk::examples::BasicVertexStage> {
    auto vsbuilder = imvk::StageSetBuilder{graphicsEngine, vertexStage};
    vsbuilder.addDescriptorSet(1).addDescriptor(myUniform, 0);
    return vsbuilder;
  }();
  auto fragmentStageSet =
      [&]() -> imvk::StageSet<imvk::examples::BasicFragmentStage> {
    auto vsbuilder = imvk::StageSetBuilder{graphicsEngine, fragmentStage};
    vsbuilder.addDescriptorSet(2).addDescriptor(myTextureView, 0);
    return vsbuilder;
  }();
  MyUniform uniValue{};
  uniValue.vals[0] = 0.5;

  size_t allocAcc = 0;
  size_t freeAcc = 0;
  size_t sizeAllocAcc = 0;
  size_t auxCount = 0;
  bool auxEven = false;
  auto updateCowVertices = [&]() {
    auxCount++;
    if (auxCount < 2000)
      return;
    auxEven = !auxEven;
    auxCount = 0;
    anotherVertices->replace(
        graphicsEngine,
        imvk::examples::BufferImpl<VertexInfo, imvk::fon_type::cow,
                                   vkw::VertexBuffer<VertexInfo>>::
            create(graphicsEngine, copyEngine,
                   getVerticesForFrame(0.5, Pos2D{0.3, 0.3},
                                       /* scale */ auxEven ? 0.5f : 0.2f)));
    myTexture->replace(
        graphicsEngine,
        imvk::examples::Texture::load(graphicsEngine, copyEngine,
                                      imvk::examples::assetsDir() /
                                          (auxEven ? "image2" : "image1")));
  };
  auto passJob = [&](const imvk::graph::RenderPass::PassInfo &pass,
                     vkw::RenderPassRecorder &commands,
                     const imvk::Frame &frame) {
    imvk::GraphicsPipelineManager<imvk::graph::RenderPass::PipeHook,
                                  imvk::examples::BasicVertexStage,
                                  imvk::examples::BasicFragmentStage>
        pipelineManager{pipelinePool, commands, frame};
    pipelineManager.bind(pass.set, vertexStageSet, fragmentStageSet);
    pipelineManager.bindPipeline();

    auto &vertexBuffer = vertices->use(frame);
    commands.bindVertexBuffer(vertexBuffer, 0, 0);
    commands.draw(vertexBuffer.size(), 1u);

    auto &anotherBuffer = anotherVertices->use(frame);
    commands.bindVertexBuffer(anotherBuffer, 0, 0);
    commands.draw(anotherBuffer.size(), 1u);
    updateCowVertices();
  };
  auto offscreenJob = [&](const imvk::graph::RenderPass::PassInfo &pass,
                          vkw::RenderPassRecorder &commands,
                          const imvk::Frame &frame) {
    imvk::Pipeline<imvk::GraphicsPipelineTraits> pipeline =
        pipelinePool.get(pass.passStage, vertexStage, fragmentStage2);
    commands.bindPipeline(pipeline->use(frame));
    commands.bindDescriptorSet(pipeline.layout()->use(frame),
                               VK_PIPELINE_BIND_POINT_GRAPHICS,
                               *vertexStageSet.getSet(1u)->use(frame), 1u);
    commands.bindDescriptorSet(pipeline.layout()->use(frame),
                               VK_PIPELINE_BIND_POINT_GRAPHICS,
                               *fragmentStageSet.getSet(2u)->use(frame), 2u);
    auto &vertexBuffer = moreVertices->use(frame);
    commands.bindVertexBuffer(vertexBuffer, 0, 0);
    commands.draw(vertexBuffer.size(), 1u);
  };
  imvk::graph::Context graphCtx{};
  auto wf = basicWorkflow(graphCtx, passJob, offscreenJob);
  std::cout << wf << std::endl;
  imvk::graph::MaterializationContext matCtx{graphicsEngine, wf};
  std::cout << wf << std::endl;
#if 1

  auto commands = MyCommandBuffer(graphicsEngine);
  //  Main application loop.
  while (!window.shouldClose()) {
    window.pollEvents();
    if (window.clock().totalFrames() % 10000 == 0u) {
      std::cout << "fps: " << window.clock().fps() << std::endl;
      allocLogger.stamp(10000);
    }
    graphicsEngine.submitFrame([&](const imvk::Frame &frame) {
      auto &cb = commands->use(frame);
      vkw::BufferRecorder recorder{cb,
                                   VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
      matCtx.run(recorder, frame);
      vkw::SubmitInfo ret{};
      ret.addCommands(cb);
      return ret;
    });
  }
#endif

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