#include "IMVKBasicRenderPass.hpp"
#include "IMVKDevice.hpp"
#include "IMVKShaderLoader.hpp"
#include "IMVKTexture.hpp"
#include "IMVKWindow.hpp"

#include "imvk/base/Context.hpp"
#include "imvk/base/Frame.hpp"
#include "imvk/copy/Engine.hpp"
#include "imvk/graphics/Engine.hpp"
#if 0
#include "imvk/graphics/Graph.hpp"
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

template <typename Buf, imvk::fon_type type> class MyBuffer {};

template <typename Buf>
class MyBuffer<Buf, imvk::fon_type::cow>
    : public imvk::FONode<Buf, imvk::fon_type::cow> {
public:
  template <typename U>
  struct CopyWorkload : public imvk::CopyEngine::Workload {
    CopyWorkload(vkw::StagingBuffer<U> &&src, Buf &dst)
        : src(std::move(src)), dst(dst){};

    void record(vkw::TransferPassRecorder &commands) const override {
      VkBufferCopy region{0, 0, src.size() * sizeof(U)};
      commands.copyBufferToBuffer(src, dst, {&region, 1u});
    }
    vkw::StagingBuffer<U> src;
    Buf &dst;
  };

  template <std::ranges::range U>
  static imvk::FObject::Ptr create(imvk::FramedEngine &engine,
                                   imvk::CopyEngine &copyEngine, U data) {
    auto ret = engine.createObject<Buf>(
        engine.context().getDeviceAllocator(), data.size(),
        VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                .usage = VMA_MEMORY_USAGE_GPU_ONLY,
                                .requiredFlags =
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT},
        VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    vkw::StagingBuffer<std::ranges::range_value_t<U>> staging{
        engine.context().getDeviceAllocator(), data};
    Buf &bufRef = ret->as<Buf>();
    auto copyFuture = copyEngine.copy(
        std::make_unique<CopyWorkload<std::ranges::range_value_t<U>>>(
            std::move(staging), bufRef));
    copyFuture.wait();
    return ret;
  }

  template <typename U>
    requires not
             std::ranges::range<U> static imvk::FObject::Ptr create(
                 imvk::FramedEngine & engine, imvk::CopyEngine &copyEngine,
                 U data) {
    auto ret = engine.createObject<Buf>(
        engine.context().getDeviceAllocator(),
        VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                .usage = VMA_MEMORY_USAGE_GPU_ONLY,
                                .requiredFlags =
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT},
        VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    vkw::StagingBuffer<U> staging{engine.context().getDeviceAllocator(), data};
    Buf &bufRef = ret->as<Buf>();
    auto copyFuture = copyEngine.copy(
        std::make_unique<CopyWorkload>(std::move(staging), bufRef));
    copyFuture.wait();
    return ret;
  }

  template <typename U>
  MyBuffer(imvk::FramedEngine &engine, imvk::CopyEngine &copyEngine, U &&data)
      : imvk::FONode<Buf, imvk::fon_type::cow>(
            create(engine, copyEngine, std::forward<U>(data))) {}

private:
  imvk::FObject::Ptr
  constructNew(imvk::FramedEngine &engine) noexcept override {
    return nullptr;
  }
};

template <typename Buf>
class MyBuffer<Buf, imvk::fon_type::swap>
    : public imvk::FONode<Buf, imvk::fon_type::swap> {
public:
  MyBuffer(imvk::FramedEngine &engine, size_t size, auto &&action)
      : imvk::FONode<Buf, imvk::fon_type::swap>(
            engine,
            [&](imvk::FrameID id) {
              return engine.createObject<Buf>(
                  engine.context().getDeviceAllocator(), size,
                  VmaAllocationCreateInfo{
                      .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                      .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
                      .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT});
            }),
        m_action(std::forward<decltype(action)>(action)) {}

  MyBuffer(imvk::FramedEngine &engine, auto &&action)
      : imvk::FONode<Buf, imvk::fon_type::swap>(
            engine,
            [&](imvk::FrameID id) {
              return engine.createObject<Buf>(
                  engine.context().getDeviceAllocator(),
                  VmaAllocationCreateInfo{
                      .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                      .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
                      .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT});
            }),
        m_action(std::forward<decltype(action)>(action)) {}

private:
  void onCowExpire(const imvk::Frame &frame) override {
    // do nothing
  }

  void onUseAction(const imvk::Frame &frame, imvk::FObject &obj) override {
    std::invoke(m_action, frame, obj.as<Buf>());
  }
  imvk::FObject::Ptr constructNew(imvk::FramedEngine &engine,
                                  imvk::FrameID frame) override {
    return nullptr;
  }
  std::function<void(const imvk::Frame &, Buf &)> m_action;
};

template <typename T, imvk::fon_type type>
class UniBuffer : public MyBuffer<vkw::UniformBuffer<T>, type>,
                  public imvk::Descriptable {
public:
  UniBuffer(auto &&...args)
      : MyBuffer<vkw::UniformBuffer<T>, type>(
            std::forward<decltype(args)>(args)...) {}
  void descriptorWrite(imvk::FrameID frame, vkw::DescriptorSet &set,
                       unsigned binding) const final {
    if constexpr (type == imvk::fon_type::swap) {
      set.write(binding, this->get(frame));
    } else {
      set.write(binding, this->get());
    }
  }
};

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

template <imvk::fon_type PType>
using MyVertexBuffer = MyBuffer<vkw::VertexBuffer<VertexInfo>, PType>;

using MyUniformBuffer = UniBuffer<MyUniform, imvk::fon_type::swap>;

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

static std::atomic<bool> doQuit = false;
#if 0
void secondThread(
    MyVertexBuffer<imvk::PrimitiveBase::Type::cow> &someVertices) {
  unsigned counter = 0;
  while (!doQuit.load()) {
    bool even = counter % 2 == 0;
    someVertices.resetSync(getVerticesForFrame(0.5, Pos2D{0.3, 0.3},
                                               /* scale */ even ? 0.5f : 0.2f));
    counter++;
    std::this_thread::sleep_for(200ms);
  }
}
#endif
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

class MyCommandBuffer
    : public imvk::FONode<vkw::PrimaryCommandBuffer, imvk::fon_type::swap> {
public:
  MyCommandBuffer(imvk::FramedEngine &engine)
      : imvk::FONode<vkw::PrimaryCommandBuffer, imvk::fon_type::swap>(
            engine, [&](imvk::FrameID id) {
              return engine.createObject<vkw::PrimaryCommandBuffer>(
                  engine.commandPool());
            }) {}

private:
  void onCowExpire(const imvk::Frame &frame) override {
    // do nothing
  }

  void onUseAction(const imvk::Frame &frame, imvk::FObject &obj) override {
    // do nothing
  }

  imvk::FObject::Ptr constructNew(imvk::FramedEngine &engine,
                                  imvk::FrameID frame) override {
    return nullptr;
  }
};

class MyGraphicsEngine : public imvk::GraphicsEngine {
public:
  MyGraphicsEngine(imvk::Context &ctx, imvk::examples::Window &window)
      : imvk::GraphicsEngine(
            ctx, imvk::GraphicsEngineCreateInfo{.swapchainFactory = &window,
                                                .maxFramesInFlight = 2}),
        m_window(window), m_commands(createNode<MyCommandBuffer>()) {}

  bool midFrameAction() override {
    m_window.pollEvents();
    if (m_window.clock().totalFrames() % 10000 == 0u) {
      std::cout << "fps: " << m_window.clock().fps() << std::endl;
      m_allocLogger.stamp(10000);
    }
    return !m_window.shouldClose();
  }
  vkw::SubmitInfo frameAction(const imvk::Frame &frame) override {
    auto &cb = m_commands->use(frame);
    vkw::BufferRecorder recorder{cb,
                                 VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    m_frameAction(recorder, frame);
    vkw::SubmitInfo ret{};
    ret.addCommands(cb);
    return ret;
  }
  void setFrameAction(auto &&action) {
    m_frameAction = std::forward<decltype(action)>(action);
  }

private:
  imvk::examples::Window &m_window;
  AllocLogger m_allocLogger;
  imvk::Ref<MyCommandBuffer> m_commands;
  std::function<void(vkw::BufferRecorder &, const imvk::Frame &)> m_frameAction;
};
int app() try {
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
  auto graphicsEngine = MyGraphicsEngine(imvkContext, window);

  auto copyEngine = imvk::CopyEngine(imvkContext, imvk::CopyEngineCreateInfo{});
  // Create basic render pass.
  auto renderPass = imvk::examples::BasicRenderPass{graphicsEngine};

  auto vertexStage =
      graphicsEngine.createNode<imvk::examples::BasicVertexStage>(
          shaderLoader, "hello.vert",
          std::make_unique<vkw::VertexInputStateCreateInfo<
              vkw::per_vertex<VertexInfo, 0>>>());

  auto fragmentStage =
      graphicsEngine.createNode<imvk::examples::BasicFragmentStage>(
          shaderLoader, "hello.frag", renderPass.pass()->get());
  auto pipelinePool =
      imvk::GraphicsPipelinePool<imvk::examples::BasicVertexStage,
                                 imvk::examples::BasicFragmentStage>{
          graphicsEngine, /* cache size*/ 10u};
  auto vertices =
      graphicsEngine.createNode<MyVertexBuffer<imvk::fon_type::swap>>(
          3, [&](const imvk::Frame &f, vkw::VertexBuffer<VertexInfo> &vbuf) {
            std::ranges::copy(
                getVerticesForFrame(window.clock().totalTime().count() / 1000.0,
                                    Pos2D{}, /* scale */ 0.75f),
                vbuf.mapped().begin());
            vbuf.flush();
          });
  auto anotherVertices =
      graphicsEngine.createNode<MyVertexBuffer<imvk::fon_type::cow>>(
          copyEngine,
          getVerticesForFrame(0.5, Pos2D{0.3, 0.3}, /* scale */ 0.2f));
  auto myUniform = graphicsEngine.createNode<MyUniformBuffer>(
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
  auto myTexture = graphicsEngine.createNode<imvk::examples::Texture>(
      imvk::examples::Texture::load(graphicsEngine, copyEngine,
                                    imvk::examples::assetsDir() / "image1"));
  auto myTextureView =
      graphicsEngine.createNode<imvk::examples::SampledView>(*myTexture);
  auto vertexStageSet = [&]() -> imvk::Ref<imvk::StageSet> {
    auto vsbuilder = imvk::StageSetBuilder{*vertexStage};
    vsbuilder.addDescriptorSet(0).addDescriptor(*myUniform, 0);
    return std::move(vsbuilder);
  }();
  auto fragmentStageSet = [&]() -> imvk::Ref<imvk::StageSet> {
    auto vsbuilder = imvk::StageSetBuilder{*fragmentStage};
    vsbuilder.addDescriptorSet(1).addDescriptor(*myTextureView, 0);
    return std::move(vsbuilder);
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
        anotherVertices->create(
            graphicsEngine, copyEngine,
            getVerticesForFrame(0.5, Pos2D{0.3, 0.3},
                                /* scale */ auxEven ? 0.5f : 0.2f)));
    myTexture->replace(
        graphicsEngine,
        imvk::examples::Texture::load(graphicsEngine, copyEngine,
                                      imvk::examples::assetsDir() /
                                          (auxEven ? "image2" : "image1")));
  };
  std::function<void(vkw::RenderPassRecorder &, const imvk::Frame &)> passJob =
      [&](vkw::RenderPassRecorder &commands, const imvk::Frame &frame) {
        imvk::Pipeline<imvk::GraphicsPipelineTraits> &pipeline =
            pipelinePool.get(*vertexStage, *fragmentStage);
        commands.bindPipeline(pipeline.use(frame));
        commands.bindDescriptorSet(pipeline.layout().use(frame),
                                   VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   vertexStageSet->getSet(0u).use(frame), 0u);
        commands.bindDescriptorSet(pipeline.layout().use(frame),
                                   VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   fragmentStageSet->getSet(1u).use(frame), 1u);
        auto &vertexBuffer = vertices->use(frame);
        commands.bindVertexBuffer(vertexBuffer, 0, 0);
        commands.draw(vertexBuffer.size(), 1u);

        auto &anotherBuffer = anotherVertices->use(frame);
        commands.bindVertexBuffer(anotherBuffer, 0, 0);
        commands.draw(anotherBuffer.size(), 1u);
        updateCowVertices();
      };
#if 0
  imvk::RenderGraph rGraph;
  imvk::CompiledRenderGraph rCompiledGraph{*graphicsEngine};
  rCompiledGraph.recompile(rGraph);
#endif
  // std::jthread thread2{secondThread, std::ref(anotherVertices)};
  //  Main application loop.
  graphicsEngine.setFrameAction(
      [&](vkw::BufferRecorder &recorder, const imvk::Frame &frame) {
        renderPass.run(recorder, frame, passJob);
      });
  graphicsEngine.run();

  doQuit = true;

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