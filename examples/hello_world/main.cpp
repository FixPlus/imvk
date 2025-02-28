#include "IMVKBasicRenderPass.hpp"
#include "IMVKDevice.hpp"
#include "IMVKShaderLoader.hpp"
#include "IMVKWindow.hpp"

#include "imvk/base/Context.hpp"
#include "imvk/base/Primitive.hpp"
#include "imvk/copy/Engine.hpp"
#include "imvk/graphics/Engine.hpp"

#include <vkw/StagingBuffer.hpp>

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

template <typename T, imvk::PrimitiveBase::Type PType> struct VBOAllocator {};

template <typename T> struct VBOAllocator<T, imvk::PrimitiveBase::Type::swap> {
  using HandleType = std::unique_ptr<vkw::VertexBuffer<T>>;
  std::unique_ptr<vkw::VertexBuffer<T>> allocate(imvk::FramedEngine &engine,
                                                 size_t size) {
    return std::make_unique<vkw::VertexBuffer<T>>(
        engine.context().device(), size,
        VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
                                .requiredFlags =
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT});
  }

  void write(vkw::VertexBuffer<T> &buffer, const imvk::Frame &frame,
             std::span<const T> data) {
    std::ranges::copy(data, buffer.mapped().begin());
    buffer.flush();
  }
};

template <typename T> struct VBOAllocator<T, imvk::PrimitiveBase::Type::cow> {
  using HandleType = std::unique_ptr<vkw::VertexBuffer<T>>;

  struct CopyWorkload : public imvk::CopyEngine::Workload {
    CopyWorkload(vkw::StagingBuffer<T> &&src, vkw::VertexBuffer<T> &dst)
        : src(std::move(src)), dst(dst){};

    void record(vkw::CommandBuffer &commands) const override {
      VkBufferCopy region{0, 0, src.size() * sizeof(T)};
      commands.copyBufferToBuffer(src, dst, {&region, 1u});
    }
    vkw::StagingBuffer<T> src;
    vkw::VertexBuffer<T> &dst;
  };

  std::pair<HandleType, std::future<void>> allocate(imvk::FramedEngine &engine,
                                                    std::span<const T> data) {
    auto buffer = std::make_unique<vkw::VertexBuffer<T>>(
        engine.context().device(), data.size(),
        VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                .usage = VMA_MEMORY_USAGE_GPU_ONLY,
                                .requiredFlags =
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT},
        VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    vkw::StagingBuffer<T> staging{engine.context().device(), data};
    auto &bufRef = *buffer;
    return std::make_pair(std::move(buffer),
                          copyEngine.copy(std::make_unique<CopyWorkload>(
                              std::move(staging), bufRef)));
  }

  imvk::CopyEngine &copyEngine;
};

template <typename T> struct VBOTraits {
  template <imvk::PrimitiveBase::Type PType>
  using Allocator = VBOAllocator<T, PType>;
};

template <typename T, imvk::PrimitiveBase::Type PType> struct UBOAllocator {};

template <typename T> struct UBOAllocator<T, imvk::PrimitiveBase::Type::swap> {
  using HandleType = std::unique_ptr<vkw::UniformBuffer<T>>;
  HandleType allocate(imvk::FramedEngine &engine) {
    return std::make_unique<vkw::UniformBuffer<T>>(
        engine.context().device(),
        VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
                                .requiredFlags =
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT});
  }

  void write(vkw::UniformBuffer<T> &buffer, const imvk::Frame &frame,
             const T &data) {
    buffer.mapped().front() = data;
    buffer.flush();
  }
};

template <typename T> struct UBOTraits {
  template <imvk::PrimitiveBase::Type PType>
  using Allocator = UBOAllocator<T, PType>;
  static void write(vkw::UniformBuffer<T> &buffer, vkw::DescriptorSet &set,
                    unsigned binding, unsigned writeOp) {
    set.write(binding, buffer);
  }
};

struct VertexInfo : public vkw::AttributeBase<vkw::VertexAttributeType::VEC2F,
                                              vkw::VertexAttributeType::VEC3F> {
  float pos[2];
  float color[3];
};

struct Pos2D {
  float x = 0;
  float y = 0;
};
struct MyUniform {
  float vals[4];
};

template <imvk::PrimitiveBase::Type PType>
using MyVertexBuffer = imvk::Primitive<VBOTraits<VertexInfo>, PType>;

using MyUniformBuffer =
    imvk::Primitive<UBOTraits<MyUniform>, imvk::PrimitiveBase::Type::swap>;

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
    std::copy(std::next(colorTable.begin(), i * 3),
              std::next(colorTable.begin(), (i + 1) * 3), vertex.color);
    phaseOffset += 2.0f * pi / 3.0f;
  }
  return ret;
}

static std::atomic<bool> doQuit = false;

void secondThread(
    MyVertexBuffer<imvk::PrimitiveBase::Type::cow> &someVertices) {
  unsigned counter = 0;
  while (!doQuit.load()) {
    bool even = counter % 2 == 0;
    someVertices
        .reset(getVerticesForFrame(0.5, Pos2D{0.3, 0.3},
                                   /* scale */ even ? 0.5f : 0.2f))
        .get()
        .get()
        .get();
    counter++;
    std::this_thread::sleep_for(200ms);
  }
}

int app() try {
  // Open vulkan loader library, construct vulkan instance, pick
  // physical device and construct logical device.
  imvk::examples::Device imvkDevice{
      imvk::examples::DeviceCreateInfo{.enableValidation = true}};

  // Create presentable window and it's surface. This will be used as swapchain
  // factory.
  imvk::examples::WindowSettings windowSettings{
      .title = "Hello world", .width = 800, .height = 600};
  imvk::examples::Window window{windowSettings, imvkDevice.get().parent()};

  // Create shader loader
  imvk::examples::ShaderLoaderCreateInfo shaderLoaderCI{.shaderDirectory =
                                                            "assets/shaders"};
  imvk::examples::ShaderLoader shaderLoader{shaderLoaderCI};

  // Create instance of imvk context.
  imvk::ContextCreateInfo imvkCCI{.device = imvkDevice.get(),
                                  .shaderFactory = shaderLoader};
  imvk::Context imvkContext{imvkCCI};

  // Create graphics engine.
  imvk::GraphicsEngineCreateInfo geCI{.swapchainFactory = &window,
                                      .maxFramesInFlight = 2};
  auto graphicsEngine = imvkContext.createGraphicsEngine(geCI);

  auto copyEngine = imvkContext.createCopyEngine(imvk::CopyEngineCreateInfo{});
  // Create basic render pass.
  auto renderPass = imvk::examples::BasicRenderPass{*graphicsEngine};

  auto vertexStage = std::make_shared<imvk::examples::BasicVertexStage>(
      *graphicsEngine, "hello.vert",
      std::make_unique<
          vkw::VertexInputStateCreateInfo<vkw::per_vertex<VertexInfo, 0>>>());

  auto fragmentStage = std::make_shared<imvk::examples::BasicFragmentStage>(
      *graphicsEngine, "hello.frag", renderPass.pass(), 0u);
  auto pipelinePool =
      imvk::GraphicsPipelinePool<imvk::examples::BasicVertexStage,
                                 imvk::examples::BasicFragmentStage>{
          *graphicsEngine, /* cache size*/ 10u};
  MyVertexBuffer<imvk::PrimitiveBase::Type::swap> vertices{*graphicsEngine};
  MyVertexBuffer<imvk::PrimitiveBase::Type::cow> anotherVertices{
      *graphicsEngine, *copyEngine};
  MyUniformBuffer myUniform{*graphicsEngine};

  std::array<std::pair<imvk::PrimitiveBase *, unsigned>, 1> unifromInfos{
      std::pair<imvk::PrimitiveBase *, unsigned>{&myUniform, 0}};
  auto vertexStageSet =
      imvk::PipelineStageSet<imvk::examples::BasicVertexStage>{
          vertexStage, std::span{&unifromInfos, 1u}};

  myUniform.resetAll();
  MyUniform uniValue{};
  uniValue.vals[0] = 0.5;

  vertices.resetAll(/* size */ 3);
  anotherVertices
      .reset(getVerticesForFrame(0.5, Pos2D{0.3, 0.3}, /* scale */ 0.2f))
      .get()
      .get()
      .get();

  size_t allocAcc = 0;
  size_t freeAcc = 0;
  size_t sizeAllocAcc = 0;
  std::function<void(const imvk::SwapFrame &)> passJob =
      [&](const imvk::SwapFrame &frame) {
        uniValue.vals[0] =
            std::sin(window.clock().totalTime().count() / 1000.0) * 0.5 + 0.5;
        uniValue.vals[1] =
            std::cos(window.clock().totalTime().count() / 500.0 + 1.0) * 0.5 +
            0.5;
        uniValue.vals[2] =
            std::sin(window.clock().totalTime().count() / 1000.0 + 2.0) * 0.5 +
            0.5;

        myUniform.write(frame.frame(), uniValue);
        auto &pipeline = pipelinePool.get(vertexStage, fragmentStage);
        frame.frame().use(pipeline);
        auto &commands = frame.frame().commands();
        commands.bindGraphicsPipeline(pipeline->pipeline());
        commands.bindDescriptorSets(
            pipeline->layout(), VK_PIPELINE_BIND_POINT_GRAPHICS,
            vertexStageSet.getSet(0u).get(frame.frame())->set(), 0u);

        vertices.write(
            frame.frame(),
            getVerticesForFrame(window.clock().totalTime().count() / 1000.0,
                                Pos2D{}, /* scale */ 0.5f));
        auto vertexBuffer = vertices.getImpl(frame.frame());
        frame.frame().use(vertexBuffer);
        commands.bindVertexBuffer(vertexBuffer->get(), 0, 0);
        commands.draw(vertexBuffer->get().size(), 1u);

        auto anotherBuffer = anotherVertices.getImpl(frame.frame());
        frame.frame().use(anotherBuffer);
        commands.bindVertexBuffer(anotherBuffer->get(), 0, 0);
        commands.draw(anotherBuffer->get().size(), 1u);
      };

  std::jthread thread2{secondThread, std::ref(anotherVertices)};
  // Main application loop.
  graphicsEngine->run(
      [&](const imvk::SwapFrame &frame) { renderPass.run(frame, passJob); },
      [&]() {
        window.pollEvents();
        if (window.clock().totalFrames() % 10000 == 0u) {
          auto newAllocAcc = totalAllocations.load(std::memory_order_relaxed);
          auto newFreeAcc = totalFrees.load(std::memory_order_relaxed);
          auto newSizeAllocAcc = totalAllocated.load(std::memory_order_relaxed);

          std::cout << "fps: " << window.clock().fps() << std::endl;
          std::cout << "a: "
                    << static_cast<double>(newAllocAcc - allocAcc) / 10000.0
                    << ", f: "
                    << static_cast<double>(newFreeAcc - freeAcc) / 10000.0
                    << ", b: "
                    << static_cast<double>(newSizeAllocAcc - sizeAllocAcc) /
                           10000.0
                    << std::endl;
          allocAcc = newAllocAcc;
          freeAcc = newFreeAcc;
          sizeAllocAcc = newSizeAllocAcc;
        }
        return !window.shouldClose();
      });

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