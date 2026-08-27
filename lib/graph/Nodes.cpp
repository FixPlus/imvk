#include "imvk/graph/Nodes.hpp"
#include "imvk/graph/Attributes.hpp"

namespace imvk::graph {

bool Constant<IntegerScalarTy>::materialize(MaterializationContext &ctx) {
  ctx.materialize<MatIntegerScalar>(results().front(),
                                    MatIntegerScalar(ctx.engine(), value));
  return true;
}
bool Constant<ExtentsTy>::materialize(MaterializationContext &ctx) {
  ctx.materialize<MatExtents>(results().front(),
                              MatExtents(ctx.engine(), value));
  return true;
}

bool Dynamic<IntegerScalarTy>::materialize(MaterializationContext &ctx) {

  auto dynVal = MatIntegerScalar(ctx.engine());
  ctx.materialize(results().front(), dynVal);
  ctx.materializeNode(
      *this, [dynVal = std::move(dynVal), this](vkw::BufferRecorder &recorder,
                                                const imvk::Frame &frame) {
        auto newVal = producer();
        if (newVal != dynVal->get())
          dynVal.reset(frame.engine(), newVal);
      });
  return true;
}

class RegularImage : public vkw::Allocation<VkImage> {
public:
  RegularImage(vkw::DeviceAllocator &allocator,
               const vkw::AllocationCreateInfo &allocCreateInfo,
               const VkImageCreateInfo &info)
      : vkw::Allocation<VkImage>(allocator, allocCreateInfo, info) {}

  operator VkImage() const noexcept { return handle(); }
};

class RegularImageNode final
    : public FONode<RegularImage, fon_type::swap, RegularImageNode>,
      public MatImageBase {
public:
  RegularImageNode(FramedEngine &engine, const MaterializationContext &ctx,
                   const MatExtents &extents, const MatIntegerScalar &format,
                   const MatIntegerScalar &layers,
                   const MatIntegerScalar &levels,
                   const VkImageCreateInfo &info)
      : FONode<RegularImage, fon_type::swap, RegularImageNode>(
            FOUses{extents, format, layers, levels}),
        MatImageBase(fon_type::swap), m_info(info) {}
  FOReconstructible &node() override { return *this; }
  VkImage image(FrameID id) const final { return get(id); }
  VkImage useImage(const Frame &id) final { return use(id); }
  const VkImageCreateInfo &info() const {
    assert(!isDestroyed());
    return m_info;
  }

  void m_updateInfo() {
    m_info.extent = getUse<MatExtents>(0)->get();
    m_info.format = static_cast<VkFormat>(getUse<MatIntegerScalar>(1)->get());
    m_info.arrayLayers = getUse<MatIntegerScalar>(2)->get();
    m_info.mipLevels = getUse<MatIntegerScalar>(3)->get();
  }
  FObject::Ptr constructNew(FramedEngine &engine, FrameID frame) {
    m_updateInfo();
    vkw::AllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_GPU_ONLY};
    return engine.createObject<RegularImage>(
        engine.context().getDeviceAllocator(), allocInfo, m_info);
  }
  void onUseAction(const Frame &frame, RegularImage &obj) {
    // do nothing
  }
  VkImageCreateInfo m_info;
};

inline void intrusive_ptr_add_ref(RegularImageNode *p) {
  assert(p);
  intrusive_ptr_add_ref(static_cast<FONodeBase *>(p));
}
inline void intrusive_ptr_release(RegularImageNode *p) {
  assert(p);
  intrusive_ptr_release(static_cast<FONodeBase *>(p));
}

class CopyImageNode final
    : public FONode<RegularImage, fon_type::swap, CopyImageNode>,
      public MatImageBase {
public:
  CopyImageNode(FramedEngine &engine, const MatImage &src,
                const VkImageCreateInfo &info)
      : FONode<RegularImage, fon_type::swap, CopyImageNode>(
            FOUses{&src->node()}),
        MatImageBase(fon_type::swap), m_info(info) {}
  FOReconstructible &node() override { return *this; }
  VkImage image(FrameID id) const final { return get(id); }
  VkImage useImage(const Frame &id) final { return use(id); }
  const VkImageCreateInfo &info() const {
    assert(!isDestroyed());
    return m_info;
  }

  void m_updateInfo() {
    auto *img = dynamic_cast<MatImageBase *>(&getUseRaw(0));
    assert(img);
    auto &srcInfo = img->info();
    m_info.extent = srcInfo.extent;
    m_info.format = srcInfo.format;
    m_info.arrayLayers = srcInfo.arrayLayers;
    m_info.mipLevels = srcInfo.mipLevels;
  }
  FObject::Ptr constructNew(FramedEngine &engine, FrameID frame) {
    m_updateInfo();
    vkw::AllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_GPU_ONLY};
    return engine.createObject<RegularImage>(
        engine.context().getDeviceAllocator(), allocInfo, m_info);
  }
  void onUseAction(const Frame &frame, RegularImage &obj) {
    // do nothing
  }
  VkImageCreateInfo m_info;
};

inline void intrusive_ptr_add_ref(CopyImageNode *p) {
  assert(p);
  intrusive_ptr_add_ref(static_cast<FONodeBase *>(p));
}
inline void intrusive_ptr_release(CopyImageNode *p) {
  assert(p);
  intrusive_ptr_release(static_cast<FONodeBase *>(p));
}

class SwapchainImageNode final
    : public FONode<VkImage, fon_type::ext, SwapchainImageNode>,
      public MatImageBase {
public:
  using Base = FONode<VkImage, fon_type::ext, SwapchainImageNode>;
  SwapchainImageNode(GraphicsEngine &engine, const MaterializationContext &ctx,
                     const VkImageCreateInfo &info)
      : Base(FOUses{engine.swapchain()}), MatImageBase(fon_type::ext) {
    m_fillInfo(engine.swapchain().get());
  }
  FOReconstructible &node() override { return *this; }
  VkImage image(FrameID id) const final { return get(id); }
  VkImage useImage(const Frame &id) final { return use(id); }
  const VkImageCreateInfo &info() const {
    assert(!isDestroyed());
    return m_info;
  }

  unsigned getExtIndex(const Frame &frame) const {
    return static_cast<GraphicsEngine &>(frame.engine())
        .swapchain()
        .get()
        .currentImage();
  }
  void onUseAction(const Frame &frame, VkImage &obj) {
    // do nothing
  }
  void constructNew(FramedEngine &engine,
                    boost::container::small_vector_base<FObject::Ptr> &res) {
    auto &swap = static_cast<GraphicsEngine &>(engine).swapchain().get();
    m_fillInfo(swap);
    std::ranges::transform(
        swap.images(), std::back_inserter(res), [&](auto &image) {
          return engine.createObject<VkImage>(image.operator VkImage());
        });
  }

  void m_fillInfo(const vkw::SwapChain &swapchain) {
    auto &image = swapchain.images().front();

    m_info = image.fullInfo();
  }
  VkImageCreateInfo m_info;
};

inline void intrusive_ptr_add_ref(SwapchainImageNode *p) {
  assert(p);
  intrusive_ptr_add_ref(static_cast<FONodeBase *>(p));
}
inline void intrusive_ptr_release(SwapchainImageNode *p) {
  assert(p);
  intrusive_ptr_release(static_cast<FONodeBase *>(p));
}

class ImageSampledAdaptorImpl
    : public FONode<vkw::Sampler, fon_type::cow, ImageSampledAdaptorImpl> {
public:
  using Base = FONode<vkw::Sampler, fon_type::cow, ImageSampledAdaptorImpl>;
  ImageSampledAdaptorImpl(FramedEngine &engine, const MatImageView &view)
      : Base(engine.createObject<vkw::Sampler>(m_createSampler(engine)),
             FOUses{&view->node()}) {
    if (view->type() == fon_type::ext) {
      throw std::runtime_error(
          "Cannot create descriptor adaptor for external object");
    }
    auto &node = view->node();
    if (node.isDestroyed())
      node.construct(engine);
  }

  static vkw::Sampler m_createSampler(FramedEngine &engine) {
    VkSamplerCreateInfo info{};
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.pNext = nullptr;
    return vkw::Sampler{engine.context().device(), info};
  }
  FObject::Ptr constructNew(FramedEngine &engine) {
    return engine.createObject<vkw::Sampler>(m_createSampler(engine));
  }
  VkImageLayout m_layout;
};
class ImageSampledAdaptor : public FONodeView<ImageSampledAdaptorImpl> {
public:
  ImageSampledAdaptor(auto &&...args)
      : FONodeView<ImageSampledAdaptorImpl>(
            std::forward<decltype(args)>(args)...) {}
  static void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                              FONodeBase &obj, unsigned binding) {
    auto &casted = static_cast<ImageSampledAdaptorImpl &>(obj);
    auto *view = dynamic_cast<MatImageViewBase *>(&obj.getUseRaw(0));
    assert(view);
    set.write(binding, view->view(frame),
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, casted.get());
  }
};

const AttributesBase *RenderPass::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  // todo: safe cast
  auto &imageDefInfo = static_cast<const ImageDefInfo &>(result.info());

  assert(imageDefInfo.passthrough);
  return useAttributes[*imageDefInfo.passthrough];
}

const AttributesBase *AcquireImage::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  return &ctx.attributes().get<Attributes<ImageTy>>(
      dynamic<VkExtent3D>(results().front()),
      dynamic<VkFormat>(results().front()), dynamic<size_t>(results().front()),
      constant<size_t>(1));
}

const AttributesBase *Constant<IntegerScalarTy>::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  return &ctx.attributes().get<Attributes<IntegerScalarTy>>(
      constant<size_t>(value));
}

const AttributesBase *Constant<ExtentsTy>::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  return &ctx.attributes().get<Attributes<ExtentsTy>>(
      constant<VkExtent3D>(value));
}
const AttributesBase *GetExtents::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 1);
  auto *imgAttr = dyn_cast<const Attributes<ImageTy>>(useAttributes.front());
  assert(imgAttr);
  return &ctx.attributes().get<Attributes<ExtentsTy>>(imgAttr->extents);
}

const AttributesBase *MakeImage::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 4);
  auto extents =
      static_cast<const Attributes<ExtentsTy> &>(*useAttributes[0]).extents;
  Attribute<VkFormat> format =
      static_cast<const Attributes<IntegerScalarTy> &>(*useAttributes[1]).value;
  auto layers =
      static_cast<const Attributes<IntegerScalarTy> &>(*useAttributes[2]).value;
  auto levels =
      static_cast<const Attributes<IntegerScalarTy> &>(*useAttributes[3]).value;
  return &ctx.attributes().get<Attributes<ImageTy>>(extents, format, layers,
                                                    levels);
}

Node::Def attachmentDef(const Attachment &a) {
  ImageAccessInfo info{};
  switch (a.second->kind) {
  case ImageAttachmentUseInfo::Kind::color:
    info.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    info.accessFlags = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;
  case ImageAttachmentUseInfo::Kind::depth:
    info.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    info.stageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; //?
    info.accessFlags = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;
  }
  auto *ret = new ImageDefInfo{info};
  return Node::Def(&a.first->type(), ret);
}

Node::Use combinedImageSampler(Value &image) {
  return Node::Use(&image, new ImageDescriptorUseInfo());
}

const AttributesBase *Copy<ImageTy>::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 2);
  return useAttributes.front();
}
const AttributesBase *Clone<ImageTy>::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 1);
  return useAttributes.front();
}

const AttributesBase *Barrier<ImageTy>::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 1);
  return useAttributes.front();
}

const AttributesBase *Dynamic<IntegerScalarTy>::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  return &ctx.attributes().get<Attributes<IntegerScalarTy>>(
      dynamic<size_t>(result));
}

bool MakeImage::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.engine();
  auto &value = results().front();
  if (!ctx.startsImageChain(value))
    return false;
  auto templ = ctx.chainImageTemplate(value);
  ctx.materializeImageChain(
      value, engine.createNode<RegularImageNode>(
                 ctx, ctx.get<MatExtents>(uses()[0].value()),
                 ctx.get<MatIntegerScalar>(uses()[1].value()),
                 ctx.get<MatIntegerScalar>(uses()[2].value()),
                 ctx.get<MatIntegerScalar>(uses()[3].value()), templ));
  return true;
}

static VkImageSubresourceLayers
completeSubresourceRangeLayers(const VkImageCreateInfo &info) {
  VkImageSubresourceLayers ret{};
  ret.mipLevel = 0; // todo support multiple mips.
  ret.aspectMask = vkw::ImageInterface::isDepthFormat(info.format)
                       ? VK_IMAGE_ASPECT_DEPTH_BIT
                       : VK_IMAGE_ASPECT_COLOR_BIT;
  ret.baseArrayLayer = 0;
  ret.layerCount = info.arrayLayers;
  return ret;
}

static VkImageSubresourceRange
completeSubresourceRange(const VkImageCreateInfo &info) {
  VkImageSubresourceRange ret{};
  ret.baseMipLevel = 0;
  ret.levelCount = info.mipLevels;
  ret.aspectMask = vkw::ImageInterface::isDepthFormat(info.format)
                       ? VK_IMAGE_ASPECT_DEPTH_BIT
                       : VK_IMAGE_ASPECT_COLOR_BIT;
  ret.baseArrayLayer = 0;
  ret.layerCount = info.arrayLayers;
  return ret;
}
bool Clone<ImageTy>::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.engine();
  auto &value = results().front();
  if (!ctx.startsImageChain(value))
    return false;
  auto templ = ctx.chainImageTemplate(value);
  auto src = ctx.get<MatImage>(uses().front().value());
  auto dst = engine.createNode<CopyImageNode>(src, templ);

  ctx.materializeImageChain(value, dst);
  return true;
}
bool Copy<ImageTy>::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.engine();
  auto src = ctx.get<MatImage>(uses().front().value());
  auto dst = ctx.get<MatImage>(uses().back().value());

  ctx.materializeNode(
      *this, [dst = std::move(dst), src = std::move(src)](
                 vkw::BufferRecorder &recorder, const imvk::Frame &frame) {
        auto viewSrc = src->useImage(frame);
        auto viewDst = dst->useImage(frame);
        auto &info = dst->info();
        auto subresource = completeSubresourceRangeLayers(info);
        VkImageCopy region{};
        region.extent = info.extent;
        region.srcSubresource = subresource;
        region.dstSubresource = subresource;
        auto transfer = recorder.beginTransferPass();
        transfer.copyImageToImage(viewSrc, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  viewDst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  std::array{region});
      });
  return true;
}

bool AcquireImage::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.engine();
  auto &value = results().front();
  assert(ctx.startsImageChain(value));
  auto &templ = ctx.chainImageTemplate(value);
  ctx.materializeImageChain(value,
                            engine.createNode<SwapchainImageNode>(ctx, templ));
  return true;
}

bool GetExtents::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.engine();
  auto &value = results().front();
  auto &use = uses().front().value();
  auto &image = ctx.get<MatImage>(use);
  ctx.materialize<MatExtents>(
      value,
      MatExtents(
          engine,
          [&image = *image](FramedEngine &, MatHostValueImpl<VkExtent3D> &) {
            return image.info().extent;
          },
          FOUses{&image->node()}));
  return true;
}

bool Barrier<ImageTy>::materialize(MaterializationContext &ctx) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout =
      static_cast<const ImageUseInfo &>(*uses().front().info()).access.layout;
  barrier.newLayout =
      static_cast<const ImageDefInfo &>(results().front().info()).access.layout;
  VkPipelineStageFlags srcStage{};
  VkPipelineStageFlags dstStage{};
  for (auto &useInfo :
       uses().front().value().users() |
           std::views::transform([](auto &u) -> decltype(auto) {
             return static_cast<const ImageUseInfo &>(*u.info());
           })) {
    srcStage |= useInfo.access.stageFlags;
    barrier.srcAccessMask |= useInfo.access.accessFlags;
  }
  {
    auto &defInfo =
        static_cast<const ImageDefInfo &>(uses().front().value().info());
    srcStage |= defInfo.access.stageFlags;
    barrier.srcAccessMask |= defInfo.access.accessFlags;
  }
  if (srcStage == 0)
    srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  for (auto &u : results().front().users()) {
    auto &useInfo = static_cast<const ImageUseInfo &>(*u.info());
    dstStage |= useInfo.access.stageFlags;
    barrier.dstAccessMask |= useInfo.access.accessFlags;
    if (useInfo.passthrough) {
      auto &def = static_cast<const ImageDefInfo &>(
          u.user().results()[*useInfo.passthrough].info());
      dstStage |= def.access.stageFlags;
      barrier.dstAccessMask |= def.access.accessFlags;
    }
  }
  auto image = ctx.get<MatImage>(uses().front().value());

  ctx.materializeNode(*this, [image = std::move(image), barrier, srcStage,
                              dstStage](vkw::BufferRecorder &recorder,
                                        const imvk::Frame &frame) mutable {
    barrier.image = image->useImage(frame);
    barrier.subresourceRange = completeSubresourceRange(image->info());
    auto transfer = recorder.beginTransferPass();
    transfer.imageMemoryBarrier(srcStage, dstStage, std::array{barrier});
  });
  return false;
}

bool RenderPass::materialize(MaterializationContext &ctx) {
  vkw::RenderingInfo info{};
  PassInfo pInfo{*this, ctx, m_firstDescriptor};

  boost::container::small_vector<
      std::pair<MatImageView, ImageAttachmentUseInfo::Kind>, 4>
      attachments;
  for (auto &&use : uses() | std::views::take(m_firstDescriptor)) {
    auto &useInfo = static_cast<const ImageAttachmentUseInfo &>(*use.info());
    attachments.emplace_back(ctx.get<MatImageView>(use.value()), useInfo.kind);
    VkRenderingAttachmentInfo a{};
    a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    a.imageLayout = useInfo.access.layout;
    switch (useInfo.kind) {
    case ImageAttachmentUseInfo::Kind::color: {
      a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      a.clearValue = VkClearValue{.color = {0.8, 0.5, 0.2, 0.0}};
      a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      info.addColorAttachment(a, false);
      break;
    }
    case ImageAttachmentUseInfo::Kind::depth: {
      a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      VkClearValue cv{};
      cv.depthStencil.depth = 1.0;
      a.clearValue = cv;
      a.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      info.addDepthAttachment(a);
      break;
    }
    default:
      break;
    }
  }
  MatImage refImage = ctx.get<MatImage>(uses().front().value());

  ctx.materializeNode(*this, [info = std::move(info), pInfo = std::move(pInfo),
                              attachments = std::move(attachments), refImage,
                              this](vkw::BufferRecorder &recorder,
                                    const imvk::Frame &frame) mutable {
    auto extents = refImage->info().extent;
    auto drawArea = VkRect2D{{0, 0}, {extents.width, extents.height}};
    info.setRenderArea(drawArea);
    auto counter = 0u;
    for (auto &&[view, kind] : attachments) {
      VkImageView handle = view->useView(frame);
      switch (kind) {
      case ImageAttachmentUseInfo::Kind::color:
        info.setColorView(handle, counter++);
        break;
      case ImageAttachmentUseInfo::Kind::depth:
        info.setDepthView(handle);
        break;
      default:
        break;
      }
    }
    auto renderPass = recorder.beginRenderPass(info);
    auto &drawAreaExtent = drawArea.extent;
    VkViewport viewport;
    viewport.height = drawAreaExtent.height;
    viewport.width = drawAreaExtent.width;
    viewport.x = viewport.y = 0.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor;
    scissor.extent.width = drawAreaExtent.width;
    scissor.extent.height = drawAreaExtent.height;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    renderPass.setViewports({&viewport, 1});
    renderPass.setScissors({&scissor, 1});
    m_record(pInfo, renderPass, frame);
  });
  return false;
}

RenderPass::PassInfo::PassInfo(RenderPass &pass, MaterializationContext &ctx,
                               unsigned firstDescriptor)
    : passStage(ctx.engine(), pass, ctx, firstDescriptor),
      set([&]() -> StageSet<PipeHook> {
        StageSetBuilder setBuilder{ctx.engine(), passStage};
        DescriptorSetBuilder &builder = setBuilder.addDescriptorSet(0);
        for (auto &&[index, use] : pass.uses() |
                                       std::views::drop(firstDescriptor) |
                                       std::views::enumerate) {
          auto &info = static_cast<const ImageDescriptorUseInfo &>(*use.info());
          auto view = ctx.get<MatImageView>(use.value());
          builder.addDescriptor(ImageSampledAdaptor(ctx.engine(), view), index);
        }
        if (builder.empty())
          return nullptr;
        return setBuilder;
      }()) {}

bool Present::materialize(MaterializationContext &ctx) {
  // nothing to materialize for now.
  return false;
}

vkw::GraphicsPipelineCreateInfo
RenderPass::PipeHook::initCreateInfo(const vkw::PipelineLayout &layout) const {
  return vkw::GraphicsPipelineCreateInfo{m_info, layout};
}

RenderPass::PipeHook::PipeHook(RenderPass &pass, MaterializationContext &ctx,
                               unsigned firstDescriptor)
    : GraphicsPipelineStage(
          ctx.engine(),
          [&]() {
            Stage::Description ret{};
            boost::container::small_vector<vkw::DescriptorSetLayoutBinding, 2>
                bindings;
            auto counter = 0;
            for (auto &&use : pass.uses() | std::views::drop(firstDescriptor)) {
              auto &info =
                  static_cast<const ImageDescriptorUseInfo &>(*use.info());
              auto binding = info.descriptorInfo();
              binding.binding = counter++;
              bindings.push_back(binding);
            }
            ret.sets.emplace_back(Stage::Description::ExternalSet{
                0,
                vkw::DescriptorSetLayout{ctx.engine().context().device(),
                                         bindings},
                static_cast<unsigned>(ctx.engine().getFIFCount())});
            return ret;
          }()),
      m_info([&]() {
        vkw::RenderingFormatInfo info;
        for (auto &&use : pass.uses() | std::views::take(firstDescriptor)) {
          auto kind =
              static_cast<const ImageAttachmentUseInfo &>(*use.info()).kind;
          auto &img = *ctx.get<MatImage>(use.value());
          if (img.node().isDestroyed())
            img.node().construct(ctx.engine());
          auto format = img.info().format;
          switch (kind) {
          case ImageAttachmentUseInfo::Kind::color:
            info.addColorAttachment(format, false);
            break;
          case ImageAttachmentUseInfo::Kind::depth:
            info.addDepthAttachment(format);
            break;
          default:
            break;
          }
        };
        return info;
      }()) {}

} // namespace imvk::graph