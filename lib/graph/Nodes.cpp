#include "imvk/graph/Nodes.hpp"
#include "imvk/graph/Attributes.hpp"

namespace imvk::graph {

bool Constant<IntegerScalarTy>::materialize(MaterializationContext &ctx) {
  ctx.materialize<MatIntegerScalar>(
      results().front(), MatIntegerScalar(ctx.env().engine(), value));
  return true;
}
bool Constant<ExtentsTy>::materialize(MaterializationContext &ctx) {
  ctx.materialize<MatExtents>(results().front(),
                              MatExtents(ctx.env().engine(), value));
  return true;
}

bool Dynamic<IntegerScalarTy>::materialize(MaterializationContext &ctx) {

  auto dynVal = MatIntegerScalar(ctx.env().engine());
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
            engine, FOUses{extents, format, layers, levels}),
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
  RegularImage constructNew(FramedEngine &engine, FrameID frame) {
    m_updateInfo();
    vkw::AllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_GPU_ONLY};
    return RegularImage(engine.context().getDeviceAllocator(), allocInfo,
                        m_info);
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
            engine, FOUses{&src->node()}),
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
  RegularImage constructNew(FramedEngine &engine, FrameID frame) {
    m_updateInfo();
    vkw::AllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_GPU_ONLY};
    return RegularImage(engine.context().getDeviceAllocator(), allocInfo,
                        m_info);
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
      : Base(engine, FOUses{engine.swapchain()}), MatImageBase(fon_type::ext) {
    engine.setSwapchainUsage(info.usage);
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
                    boost::container::small_vector_base<VkImage> &res) {
    auto &swap = static_cast<GraphicsEngine &>(engine).swapchain().get();
    m_fillInfo(swap);
    std::ranges::transform(
        swap.images(), std::back_inserter(res),
        [&](auto &image) { return image.operator VkImage(); });
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
      : Base(engine, vkw::Sampler(m_createSampler(engine)),
             FOUses{&view->node()}) {
    if (view->type() == fon_type::ext) {
      throw std::runtime_error(
          "Cannot create descriptor adaptor for external object");
    }
    auto &node = view->node();
    if (node.isDestroyed())
      node.construct();
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
  vkw::Sampler constructNew(FramedEngine &engine) {
    return m_createSampler(engine);
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
    vkw::DescriptorWrite write{binding,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    write.addImage(casted.get(), view->view(frame),
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    set.write(write);
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
  auto &engine = ctx.env().engine();
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
  auto &engine = ctx.env().engine();
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
  auto &engine = ctx.env().engine();
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
  auto &engine = ctx.env().engine();
  auto &value = results().front();
  assert(ctx.startsImageChain(value));
  auto &templ = ctx.chainImageTemplate(value);
  ctx.materializeImageChain(value,
                            engine.createNode<SwapchainImageNode>(ctx, templ));
  return true;
}

bool GetExtents::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.env().engine();
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

FramebufferInfoImpl::FramebufferInfoImpl(FramedEngine &ge,
                                         MatImage refAttachment)
    : FONode<FramebufferInfoFields, fon_type::cow, FramebufferInfoImpl>(
          ge, doConstructNew(ge, refAttachment),
          FOUses{&refAttachment->node()}) {}

FramebufferInfoFields FramebufferInfoImpl::constructNew(FramedEngine &engine) {
  auto *image = dyn_cast<MatImageBase>(&getUseRaw(0));
  assert(image);
  return doConstructNew(engine, image);
}

FramebufferInfoFields
FramebufferInfoImpl::doConstructNew(FramedEngine &ge, MatImage refAttachment) {
  FramebufferInfoFields ret{};
  ret.extents = refAttachment->info().extent;
  /// TODO: come up with better solution for swapchain identification here.
  ret.isSwapchain = refAttachment->type() == fon_type::ext;
  return ret;
}

bool RenderPass::acceptsScene(const Scene &scene) const {
  if (scene.attachments.size() != m_firstDescriptor ||
      scene.descriptors.size() != uses().size() - m_firstDescriptor)
    return false;

  for (auto &&[use, candidate] : std::views::zip(
           uses() | std::views::take(m_firstDescriptor), scene.attachments)) {
    const auto *current = dyn_cast<ImageAttachmentUseInfo>(use.info());
    if (!current || current->kind != candidate.kind ||
        current->load != candidate.load || current->access != candidate.access)
      return false;
  }
  for (auto &&[use, candidate] : std::views::zip(
           uses() | std::views::drop(m_firstDescriptor), scene.descriptors)) {
    if (typeid(*use.info()) != typeid(candidate.useInfo()))
      return false;
    const auto *currentImage = dyn_cast<ImageUseInfo>(use.info());
    const auto *candidateImage = dyn_cast<ImageUseInfo>(&candidate.useInfo());
    if ((currentImage || candidateImage) &&
        (!currentImage || !candidateImage ||
         currentImage->access != candidateImage->access))
      return false;
  }
  return true;
}

bool RenderPass::setScene(const Scene &scene) {
  if (!acceptsScene(scene))
    return false;
  m_scene = &scene;
  return true;
}

bool RenderPass::materialize(MaterializationContext &ctx) {
  vkw::RenderingInfo info{};
  Scene::MaterializationInfo sceneInfo{};

  boost::container::small_vector<
      std::pair<MatImageView, ImageAttachmentUseInfo::Kind>, 4>
      attachments;
  auto getLoadOp = [](ImageAttachmentUseInfo::LoadOp loadOp) {
    switch (loadOp) {
    case ImageAttachmentUseInfo::LoadOp::load:
      return VK_ATTACHMENT_LOAD_OP_LOAD;
    case ImageAttachmentUseInfo::LoadOp::clear:
      return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case ImageAttachmentUseInfo::LoadOp::dc:
      return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    default:
      return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
  };
  for (auto &&use : uses() | std::views::take(m_firstDescriptor)) {
    auto &useInfo = static_cast<const ImageAttachmentUseInfo &>(*use.info());
    attachments.emplace_back(ctx.get<MatImageView>(use.value()), useInfo.kind);
    VkRenderingAttachmentInfo a{};
    a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    a.imageLayout = useInfo.access.layout;
    auto &img = *ctx.get<MatImage>(use.value());
    if (img.node().isDestroyed())
      img.node().construct();
    auto format = img.info().format;
    switch (useInfo.kind) {
    case ImageAttachmentUseInfo::Kind::color: {
      a.loadOp = getLoadOp(useInfo.load);
      a.clearValue = VkClearValue{.color = {0.8, 0.5, 0.2, 0.0}};
      a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      info.addColorAttachment(a, false);
      sceneInfo.renderingInfo.addColorAttachment(format, false);
      break;
    }
    case ImageAttachmentUseInfo::Kind::depth: {
      a.loadOp = getLoadOp(useInfo.load);
      VkClearValue cv{};
      cv.depthStencil.depth = 1.0;
      a.clearValue = cv;
      a.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      info.addDepthAttachment(a);
      sceneInfo.renderingInfo.addDepthAttachment(format);
      break;
    }
    default:
      break;
    }
  }
  MatImage refImage = ctx.get<MatImage>(uses().front().value());
  auto framebuf = uses() | std::views::take(m_firstDescriptor);
  /// TODO: come up with better way to identify swapchain image.
  auto foundSwapchain = std::ranges::find_if(framebuf, [&](auto &&use) {
    return ctx.get<MatImage>(use.value())->type() == fon_type::ext;
  });
  if (foundSwapchain != framebuf.end()) {
    refImage = ctx.get<MatImage>(foundSwapchain->value());
  }
  sceneInfo.framebufferInfo = FramebufferInfo{ctx.env().engine(), refImage};

  for (auto &&use : uses() | std::views::drop(m_firstDescriptor)) {
    assert(isa<ImageDescriptorUseInfo>(use.info()));
    auto &info = static_cast<const ImageDescriptorUseInfo &>(*use.info());
    auto view = ctx.get<MatImageView>(use.value());
    sceneInfo.descriptors.emplace_back(
        ctx.env().engine(), ImageSampledAdaptor(ctx.env().engine(), view));
  }
  auto matScene = std::invoke(m_scene->materialization, ctx.env(), sceneInfo);

  ctx.materializeNode(
      *this,
      [info = std::move(info), attachments = std::move(attachments), refImage,
       matScene = std::move(matScene),
       this](vkw::BufferRecorder &recorder, const imvk::Frame &frame) mutable {
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
        matScene->onDraw(renderPass, frame);
      });
  return false;
}

bool Present::materialize(MaterializationContext &ctx) {
  // nothing to materialize for now.
  return false;
}

} // namespace imvk::graph