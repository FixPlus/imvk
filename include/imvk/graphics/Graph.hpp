#include "imvk/base/Primitive.hpp"
#include "imvk/graph/Graph.hpp"
#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Swapchain.hpp"

#include "vkw/Image.hpp"

namespace imvk {

using RenderGraphBase = graph::Graph<GraphicsEngine, graph::ImageResourceTraits,
                                     graph::BufferResourceTraits>;

using CompiledRenderGraphBase =
    graph::CompiledGraph<GraphicsEngine, graph::ImageResourceTraits,
                         graph::BufferResourceTraits>;

class RenderGraph : public RenderGraphBase {
public:
  RenderGraph() = default;
};

void recordInitialLayoutTransit(
    VkImage image, vkw::CommandBuffer &buffer,
    const typename graph::ImageResourceTraits::AccessInfo &initialAccessInfo);

template <graph::FramedEngineLike EngineT, vkw::ImagePixelType ptype,
          vkw::ImageType itype, vkw::ImageArrayness iarr>
struct ImageAllocator {
  using ImageType = vkw::Image<ptype, itype, iarr>;
  using HandleType = std::unique_ptr<vkw::Image<ptype, itype, iarr>>;

  std::future<HandleType>
  allocate(EngineT &engine, VkFormat format, uint32_t width, uint32_t height,
           uint32_t depth, uint32_t layers, uint32_t mipLevels,
           const typename graph::ImageResourceTraits::UseInfo &useInfo,
           const typename graph::ImageResourceTraits::AccessInfo
               &initialAccessInfo) {
    auto &device = engine.context().device();
    auto image = std::make_unique<ImageType>(
        device.getAllocator(),
        VmaAllocationCreateInfo{.usage = VMA_MEMORY_USAGE_GPU_ONLY}, format,
        width, height, depth, layers, mipLevels, useInfo.useFlags);
    auto initFuture =
        engine.oneTimeSubmit([&](vkw::PrimaryCommandBuffer &buffer) {
          recordInitialLayoutTransit(*image, buffer, initialAccessInfo);
        });
    return std::async(std::launch::deferred,
                      [initFuture = std::move(initFuture),
                       image = std::move(image)]() mutable {
                        initFuture.get();
                        return std::move(image);
                      });
  }
};

template <graph::FramedEngineLike EngineT, vkw::ImagePixelType ptype,
          vkw::ImageType itype, vkw::ImageArrayness iarr>
class ImageRes {};

template <graph::FramedEngineLike EngineT>
class ImageResBase
    : public graph::Resource<EngineT, graph::ImageResourceTraits> {
public:
  virtual std::vector<std::pair<VkImage, std::shared_ptr<PrimitiveHandle>>>
  getImages() const = 0;
};

template <graph::FramedEngineLike EngineT, vkw::ImagePixelType ptype>
class ImageRes<EngineT, ptype, vkw::I2D, vkw::SINGLE>
    : public ImageResBase<EngineT>,
      private Primitive<ImageAllocator<EngineT, ptype, vkw::I2D, vkw::SINGLE>,
                        PrimitiveBase::Type::swap_imm> {
private:
  using BasePrim =
      Primitive<ImageAllocator<EngineT, ptype, vkw::I2D, vkw::SINGLE>,
                PrimitiveBase::Type::swap_imm>;

public:
  ImageRes(
      EngineT &engine, VkFormat format, unsigned width, unsigned height,
      unsigned mips,
      const typename graph::ImageResourceTraits::UseInfo &useInfo,
      const typename graph::ImageResourceTraits::AccessInfo &initialAccessInfo)
      : BasePrim(engine) {
    reset(format, width, height, /* depth */ 1u, /* layers */ 1u, mips, useInfo,
          initialAccessInfo)
        .get()
        .get()
        .get();
  }

  std::vector<std::pair<VkImage, std::shared_ptr<PrimitiveHandle>>>
  getImages() const override {
    std::vector<std::pair<VkImage, std::shared_ptr<PrimitiveHandle>>> ret;
    std::ranges::transform(all(), std::back_inserter(ret), [](auto &&handle) {
      return std::make_pair(handle.get(),
                            std::static_pointer_cast<PrimitiveHandle>(handle));
    });
    return ret;
  }

  VkImage get(const typename EngineT::FrameT &frame) override {
    return BasePrim::getImpl(frame)->get();
  }
};

template <graph::FramedEngineLike EngineT, vkw::ImagePixelType ptype,
          vkw::ImageType itype, vkw::ImageArrayness iarr>
class ImageResDescription {};

template <graph::FramedEngineLike EngineT, vkw::ImagePixelType ptype>
class ImageResDescription<EngineT, ptype, vkw::I2D, vkw::SINGLE>
    : public graph::ResourceDescription<EngineT, graph::ImageResourceTraits> {
public:
  ImageResDescription(VkFormat format, unsigned width, unsigned height,
                      unsigned mips)
      : m_format(format), m_width(width), m_height(height), m_mips(mips) {}

  std::unique_ptr<graph::Resource<EngineT, graph::ImageResourceTraits>>
  allocate(EngineT &engine,
           const typename graph::ImageResourceTraits::UseInfo &useInfo,
           const typename graph::ImageResourceTraits::AccessInfo
               &initialAccessInfo) const override {
    return std::make_unique<ImageRes<EngineT, ptype, vkw::I2D, vkw::SINGLE>>(
        engine, m_format, m_width, m_height, m_mips, useInfo,
        initialAccessInfo);
  }

private:
  VkFormat m_format;
  unsigned m_width, m_height, m_mips;
};

class SwapchainImageRes
    : public graph::Resource<GraphicsEngine, graph::ImageResourceTraits> {
public:
  SwapchainImageRes(GraphicsEngine &engine,
                    const typename graph::ImageResourceTraits::UseInfo &useInfo,
                    const typename graph::ImageResourceTraits::AccessInfo
                        &initialAccessInfo) {
    /// TODO: amend swapchain image uses if needed.
  }

  VkImage get(const SwapFrame &frame) override {
    auto &swapchain = frame.swapchain();
    return swapchain.images()
        .at(swapchain.currentImage())
        .
        operator VkImage_T *();
  }
};

class SwapchainImageDescription
    : public graph::ResourceDescription<GraphicsEngine,
                                        graph::ImageResourceTraits> {
public:
  std::unique_ptr<graph::Resource<GraphicsEngine, graph::ImageResourceTraits>>
  allocate(GraphicsEngine &engine,
           const typename graph::ImageResourceTraits::UseInfo &useInfo,
           const typename graph::ImageResourceTraits::AccessInfo
               &initialAccessInfo) const override {
    return std::make_unique<SwapchainImageRes>(engine, useInfo,
                                               initialAccessInfo);
  }
};

class CompiledRenderGraph : public CompiledRenderGraphBase {
public:
  CompiledRenderGraph(GraphicsEngine &engine)
      : CompiledRenderGraphBase(engine) {}
};

} // namespace imvk