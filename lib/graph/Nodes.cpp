#include "imvk/graph/Nodes.hpp"
#include "imvk/graph/Attributes.hpp"

namespace imvk::graph {

class RegularImage : public vkw::Allocation<VkImage> {
public:
  RegularImage(vkw::DeviceAllocator &allocator,
               const vkw::AllocationCreateInfo &allocCreateInfo,
               const VkImageCreateInfo &info)
      : vkw::Allocation<VkImage>(allocator, allocCreateInfo, info) {}

  operator VkImage() const noexcept { return handle(); }
};

class RegularImageNode : public MatRegularImage {
public:
  RegularImageNode(FramedEngine &engine, const MaterializationContext &ctx,
                   const VkImageCreateInfo &info)
      : MatRegularImage(engine,
                        [&](FrameID id) {
                          vkw::AllocationCreateInfo allocInfo{
                              .usage = VMA_MEMORY_USAGE_GPU_ONLY};
                          return engine.createObject<RegularImage>(
                              engine.context().getDeviceAllocator(), allocInfo,
                              info);
                        }),
        m_info(info) {}
  VkImage image(FrameID id) const final { return get(id).as<RegularImage>(); }
  VkImage useImage(const Frame &id) final { return use(id).as<RegularImage>(); }
  const VkImageCreateInfo &info() const { return m_info; }

private:
  void onUseAction(const Frame &frame, FObject &obj) final {
    // do nothing
  }
  VkImageCreateInfo m_info;
};

class SwapchainImageNode : public MatSwapchainImage {
public:
  SwapchainImageNode(GraphicsEngine &engine, const MaterializationContext &ctx,
                     const VkImageCreateInfo &info)
      : MatSwapchainImage(
            [&]() {
              auto &swap =
                  static_cast<GraphicsEngine &>(engine).swapchain().get();
              boost::container::small_vector<FObject::Ptr, 2> images;
              std::ranges::transform(swap.images(), std::back_inserter(images),
                                     [&](auto &image) {
                                       return engine.createObject<VkImage>(
                                           image.operator VkImage());
                                     });
              return images;
            }(),
            FOUses{engine.swapchain()}) {
    m_fillInfo(engine.swapchain().get());
  }
  VkImage image(FrameID id) const final { return get(id).as<VkImage>(); }
  VkImage useImage(const Frame &id) final { return use(id).as<VkImage>(); }
  const VkImageCreateInfo &info() const { return m_info; }

private:
  unsigned getExtIndex(const Frame &frame) const final {
    return static_cast<GraphicsEngine &>(frame.engine())
        .swapchain()
        .get()
        .currentImage();
  }
  void onUseAction(const Frame &frame, FObject &obj) final {
    // do nothing
  }

  void m_fillInfo(const vkw::SwapChain &swapchain) {
    auto &image = swapchain.images().front();

    m_info = image.fullInfo();
  }
  VkImageCreateInfo m_info;
};

const AttributesBase *RenderPass::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  // todo: safe cast
  auto &imageDefInfo = static_cast<const ImageDefInfo &>(result.info());

  assert(imageDefInfo.passthrough);
  return useAttributes[*imageDefInfo.passthrough];
}
#if 0
const AttributesBase *GetElement::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  auto *arrayAttr = dyn_cast<Attributes<ArrayTy>>(useAttributes.front());
  auto *indexAttr = dyn_cast<Attributes<IntegerScalarTy>>(useAttributes.back());
  assert(arrayAttr && indexAttr);
  if (auto index = indexAttr->value.getConstant()) {
    return arrayAttr->elements.at(*index);
  }
  return result.type().getUndefined(ctx);
}
const AttributesBase *MakeArray::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == std::ranges::size(uses()));
  return &ctx.attributes().get<Attributes<ArrayTy>>(useAttributes);
}
#endif
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
#if 0
const AttributesBase *SampledImage::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  return &ctx.attributes().get<Attributes<DescriptorTy>>();
}

const AttributesBase *
Copy::getAttributes(Context &ctx, const Value &result,
                    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 1);
  return useAttributes.front();
}
#endif

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

void MakeImage::m_fillTemplate(VkImageCreateInfo &info,
                               MaterializationContext &ctx) {
  info.extent = ctx.get<MatExtents>(uses()[0].value());
  info.format =
      static_cast<VkFormat>(ctx.get<MatIntegerScalar>(uses()[1].value()));
  info.arrayLayers = ctx.get<MatIntegerScalar>(uses()[2].value());
  info.mipLevels = ctx.get<MatIntegerScalar>(uses()[3].value());
}

bool MakeImage::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.engine();
  auto &value = results().front();
  if (!ctx.startsImageChain(value))
    return false;
  auto templ = ctx.chainImageTemplate(value);
  m_fillTemplate(templ, ctx);
  if (ctx.has<MatImage>(value)) {
    auto &image = *std::get<Ref<MatRegularImage>>(ctx.get<MatImage>(value));
    // TODO: implement comparison.
#if 0
    if (image.info() == templ)
      return false;
#endif
  }
  ctx.materializeImageChain(value,
                            engine.createNode<RegularImageNode>(ctx, templ));
  return true;
}
void Copy<ImageTy>::m_fillTemplate(VkImageCreateInfo &info,
                                   MaterializationContext &ctx) {
  auto &image = ctx.get<MatImage>(uses().front().value());
  auto &templateInfo = std::visit(
      [](auto pimg) -> decltype(auto) { return pimg->info(); }, image);
  info.extent = templateInfo.extent;
  info.format = templateInfo.format;
  info.arrayLayers = templateInfo.arrayLayers;
  info.mipLevels = templateInfo.mipLevels;
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

bool Copy<ImageTy>::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.engine();
  auto &value = results().front();
  if (!ctx.startsImageChain(value))
    return false;
  auto templ = ctx.chainImageTemplate(value);
  m_fillTemplate(templ, ctx);
  if (ctx.has<MatImage>(value)) {
    auto &image = *std::get<Ref<MatRegularImage>>(ctx.get<MatImage>(value));
    // TODO: implement comparison.
#if 0
    if (image.info() == templ)
      return false;
#endif
  }
  auto dst = engine.createNode<RegularImageNode>(ctx, templ);
  auto src = ctx.get<MatImage>(uses().front().value());
  auto &info = dst->info();
  auto subresource = completeSubresourceRangeLayers(info);
  VkImageCopy region{};
  region.extent = info.extent;
  region.srcSubresource = subresource;
  region.dstSubresource = subresource;

  ctx.materializeImageChain(value, dst);
  ctx.materializeNode(*this, [dst = std::move(dst), src = std::move(src),
                              region](vkw::BufferRecorder &recorder,
                                      const imvk::Frame &frame) {
    auto transfer = recorder.beginTransferPass();
    transfer.copyImageToImage(
        std::visit([&](auto &pimg) -> VkImage { return pimg->useImage(frame); },
                   src),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->useImage(frame),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, std::array{region});
  });
  return true;
}

bool AcquireImage::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.engine();
  auto &value = results().front();
  assert(ctx.startsImageChain(value));
  auto &templ = ctx.chainImageTemplate(value);
  if (ctx.has<MatImage>(value)) {
    auto &image = static_cast<SwapchainImageNode &>(
        *std::get<Ref<MatSwapchainImage>>(ctx.get<MatImage>(value)));
    if (!image.isExpired())
      return false;
  }
  ctx.materializeImageChain(value,
                            engine.createNode<SwapchainImageNode>(ctx, templ));
  return true;
}

bool GetExtents::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.engine();
  auto &value = results().front();
  auto &use = uses().front().value();
  auto &image = ctx.get<MatImage>(use);
  MatExtents extents =
      std::visit([](auto pimg) { return pimg->info().extent; }, image);
  ctx.materialize(value, extents);
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

  barrier.subresourceRange = completeSubresourceRange(std::visit(
      [](auto &pimg) -> decltype(auto) { return pimg->info(); }, image));
  ctx.materializeNode(*this, [image = std::move(image), barrier, srcStage,
                              dstStage](vkw::BufferRecorder &recorder,
                                        const imvk::Frame &frame) mutable {
    barrier.image = std::visit(
        [&frame](auto &pimg) { return pimg->useImage(frame); }, image);
    auto transfer = recorder.beginTransferPass();
    transfer.imageMemoryBarrier(srcStage, dstStage, std::array{barrier});
  });
  return false;
}

bool RenderPass::materialize(MaterializationContext &ctx) {
  vkw::RenderingInfo info{};
  PassInfo pInfo{*this, ctx, m_firstDescriptor};
  auto extents = std::visit([](auto &pimg) { return pimg->info().extent; },
                            ctx.get<MatImage>(uses().front().value()));
  auto drawArea = VkRect2D{{0, 0}, {extents.width, extents.height}};
  info.setRenderArea(drawArea);
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

  ctx.materializeNode(*this, [info = std::move(info), pInfo = std::move(pInfo),
                              attachments = std::move(attachments), drawArea,
                              this](vkw::BufferRecorder &recorder,
                                    const imvk::Frame &frame) mutable {
    auto counter = 0u;
    for (auto &&[view, kind] : attachments) {
      VkImageView handle =
          std::visit([&](auto &ping) { return ping->useView(frame); }, view);
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
    : passStage(ctx.engine().createNode<PipeHook>(pass, ctx, firstDescriptor)) {
}

bool Present::materialize(MaterializationContext &ctx) {
  // nothing to materialize for now.
  return false;
}

vkw::GraphicsPipelineCreateInfo
RenderPass::PipeHook::initCreateInfo(const vkw::PipelineLayout &layout) const {
  return vkw::GraphicsPipelineCreateInfo{m_info, layout};
}

RenderPass::PipeHook::PipeHook(GraphicsEngine &engine, RenderPass &pass,
                               MaterializationContext &ctx,
                               unsigned firstDescriptor)
    : GraphicsPipelineStage(ctx.engine(),
                            [&]() {
                              StageLayout::Description ret{};
                              return ret;
                            }()),
      m_info([&]() {
        vkw::RenderingFormatInfo info;
        for (auto &&use : pass.uses() | std::views::take(firstDescriptor)) {
          auto kind =
              static_cast<const ImageAttachmentUseInfo &>(*use.info()).kind;
          auto format =
              std::visit(
                  [](auto &pimg) -> decltype(auto) { return pimg->info(); },
                  ctx.get<MatImage>(use.value()))
                  .format;
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