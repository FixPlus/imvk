#include "imvk/graph/Nodes.hpp"
#include "imvk/graph/Attributes.hpp"

#include <algorithm>

namespace imvk::graph {

namespace {

VkExtent3D halfExtents(VkExtent3D extents) {
  return {std::max(1u, extents.width / 2), std::max(1u, extents.height / 2),
          std::max(1u, extents.depth / 2)};
}

} // namespace

bool Constant<IntegerScalarTy>::materialize(MaterializationContext &ctx) {
  ctx.materialize<MatIntegerScalar>(
      results().front(), MatIntegerScalar(ctx.env().engine(), value));
  return true;
}
bool Constant<FormatTy>::materialize(MaterializationContext &ctx) {
  ctx.materialize<MatFormat>(results().front(),
                             MatFormat(ctx.env().engine(), value));
  return true;
}
bool Constant<ExtentsTy>::materialize(MaterializationContext &ctx) {
  ctx.materialize<MatExtents>(results().front(),
                              MatExtents(ctx.env().engine(), value));
  return true;
}

bool ScreenExtents::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.env().engine();
  auto swapchain = engine.swapchain();
  ctx.materialize<MatExtents>(
      results().front(),
      MatExtents(
          engine,
          [](FramedEngine &engine, MatHostValueImpl<VkExtent3D> &) {
            auto &swapchain =
                static_cast<GraphicsEngine &>(engine).swapchain().get();
            return swapchain.images().front().rawExtents();
          },
          FOUses{std::move(swapchain)}));
  return true;
}

bool HalfExtents::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.env().engine();
  auto extents = ctx.get<MatExtents>(uses().front().value());
  ctx.materialize<MatExtents>(
      results().front(),
      MatExtents(
          engine,
          [extents](FramedEngine &, MatHostValueImpl<VkExtent3D> &) {
            return halfExtents(extents->get());
          },
          FOUses{extents}));
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

bool Dynamic<FormatTy>::materialize(MaterializationContext &ctx) {
  auto dynVal = MatFormat(ctx.env().engine());
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
                   const MatExtents &extents, const MatFormat &format,
                   const MatIntegerScalar &layers,
                   const MatIntegerScalar &levels,
                   const VkImageCreateInfo &info)
      : FONode<RegularImage, fon_type::swap, RegularImageNode>(
            engine, FOUses{extents, format, layers, levels}),
        MatImageBase(fon_type::swap), m_info(info),
        m_imageType(info.imageType == VK_IMAGE_TYPE_MAX_ENUM
                        ? std::nullopt
                        : std::optional{info.imageType}) {}
  FOReconstructible &node() override { return *this; }
  VkImage image(FrameID id) const final { return get(id); }
  VkImage useImage(const Frame &id) final { return use(id); }
  const VkImageCreateInfo &info() const {
    assert(!isDestroyed());
    return m_info;
  }

  void m_updateInfo() {
    m_info.extent = getUse<MatExtents>(0)->get();
    m_info.imageType = m_imageType.value_or(imageTypeForExtents(m_info.extent));
    m_info.format = getUse<MatFormat>(1)->get();
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
  std::optional<VkImageType> m_imageType;
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
    m_info.imageType = srcInfo.imageType;
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

class ConvertedImageNode final
    : public FONode<RegularImage, fon_type::swap, ConvertedImageNode>,
      public MatImageBase {
public:
  ConvertedImageNode(FramedEngine &engine, const MatImage &src,
                     const MatFormat &format, VkImageUsageFlags usage)
      : FONode<RegularImage, fon_type::swap, ConvertedImageNode>(
            engine, FOUses{&src->node(), format}),
        MatImageBase(fon_type::swap), m_usage(usage) {}
  FOReconstructible &node() override { return *this; }
  VkImage image(FrameID id) const final { return get(id); }
  VkImage useImage(const Frame &id) final { return use(id); }
  const VkImageCreateInfo &info() const final {
    assert(!isDestroyed());
    return m_info;
  }

  RegularImage constructNew(FramedEngine &engine, FrameID frame) {
    auto *src = dynamic_cast<MatImageBase *>(&getUseRaw(0));
    assert(src);
    m_info = src->info();
    m_info.format = getUse<MatFormat>(1)->get();
    m_info.usage = m_usage;
    vkw::AllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_GPU_ONLY};
    return RegularImage(engine.context().getDeviceAllocator(), allocInfo,
                        m_info);
  }
  void onUseAction(const Frame &frame, RegularImage &obj) {
    // do nothing
  }

private:
  VkImageCreateInfo m_info{};
  VkImageUsageFlags m_usage;
};

inline void intrusive_ptr_add_ref(ConvertedImageNode *p) {
  assert(p);
  intrusive_ptr_add_ref(static_cast<FONodeBase *>(p));
}
inline void intrusive_ptr_release(ConvertedImageNode *p) {
  assert(p);
  intrusive_ptr_release(static_cast<FONodeBase *>(p));
}

class ResizedImageNode final
    : public FONode<RegularImage, fon_type::swap, ResizedImageNode>,
      public MatImageBase {
public:
  ResizedImageNode(FramedEngine &engine, const MatImage &src,
                   const MatExtents &extents, VkImageUsageFlags usage,
                   std::optional<VkImageType> imageType)
      : FONode<RegularImage, fon_type::swap, ResizedImageNode>(
            engine, FOUses{&src->node(), extents}),
        MatImageBase(fon_type::swap), m_usage(usage), m_imageType(imageType) {}
  FOReconstructible &node() override { return *this; }
  VkImage image(FrameID id) const final { return get(id); }
  VkImage useImage(const Frame &id) final { return use(id); }
  const VkImageCreateInfo &info() const final {
    assert(!isDestroyed());
    return m_info;
  }

  RegularImage constructNew(FramedEngine &engine, FrameID frame) {
    auto *src = dynamic_cast<MatImageBase *>(&getUseRaw(0));
    assert(src);
    m_info = src->info();
    m_info.extent = getUse<MatExtents>(1)->get();
    m_info.imageType = m_imageType.value_or(imageTypeForExtents(m_info.extent));
    m_info.usage = m_usage;
    vkw::AllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_GPU_ONLY};
    return RegularImage(engine.context().getDeviceAllocator(), allocInfo,
                        m_info);
  }
  void onUseAction(const Frame &frame, RegularImage &obj) {
    // do nothing
  }

private:
  VkImageCreateInfo m_info{};
  VkImageUsageFlags m_usage;
  std::optional<VkImageType> m_imageType;
};

inline void intrusive_ptr_add_ref(ResizedImageNode *p) {
  assert(p);
  intrusive_ptr_add_ref(static_cast<FONodeBase *>(p));
}
inline void intrusive_ptr_release(ResizedImageNode *p) {
  assert(p);
  intrusive_ptr_release(static_cast<FONodeBase *>(p));
}

class ConvertedResizedImageNode final
    : public FONode<RegularImage, fon_type::swap, ConvertedResizedImageNode>,
      public MatImageBase {
public:
  ConvertedResizedImageNode(FramedEngine &engine, const MatImage &src,
                            const MatExtents &extents, const MatFormat &format,
                            VkImageUsageFlags usage,
                            std::optional<VkImageType> imageType)
      : FONode<RegularImage, fon_type::swap, ConvertedResizedImageNode>(
            engine, FOUses{&src->node(), extents, format}),
        MatImageBase(fon_type::swap), m_usage(usage), m_imageType(imageType) {}
  FOReconstructible &node() override { return *this; }
  VkImage image(FrameID id) const final { return get(id); }
  VkImage useImage(const Frame &id) final { return use(id); }
  const VkImageCreateInfo &info() const final {
    assert(!isDestroyed());
    return m_info;
  }

  RegularImage constructNew(FramedEngine &engine, FrameID frame) {
    auto *src = dynamic_cast<MatImageBase *>(&getUseRaw(0));
    assert(src);
    m_info = src->info();
    m_info.extent = getUse<MatExtents>(1)->get();
    m_info.format = getUse<MatFormat>(2)->get();
    m_info.imageType = m_imageType.value_or(imageTypeForExtents(m_info.extent));
    m_info.usage = m_usage;
    vkw::AllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_GPU_ONLY};
    return RegularImage(engine.context().getDeviceAllocator(), allocInfo,
                        m_info);
  }
  void onUseAction(const Frame &frame, RegularImage &obj) {
    // do nothing
  }

private:
  VkImageCreateInfo m_info{};
  VkImageUsageFlags m_usage;
  std::optional<VkImageType> m_imageType;
};

inline void intrusive_ptr_add_ref(ConvertedResizedImageNode *p) {
  assert(p);
  intrusive_ptr_add_ref(static_cast<FONodeBase *>(p));
}
inline void intrusive_ptr_release(ConvertedResizedImageNode *p) {
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

class CombinedImageSamplerAdaptorImpl
    : public FONode<vkw::Sampler, fon_type::cow,
                    CombinedImageSamplerAdaptorImpl> {
public:
  using Base =
      FONode<vkw::Sampler, fon_type::cow, CombinedImageSamplerAdaptorImpl>;
  CombinedImageSamplerAdaptorImpl(FramedEngine &engine,
                                  const MatImageView &view)
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
};

class CombinedImageSamplerAdaptor
    : public FONodeView<CombinedImageSamplerAdaptorImpl> {
public:
  CombinedImageSamplerAdaptor(auto &&...args)
      : FONodeView<CombinedImageSamplerAdaptorImpl>(
            std::forward<decltype(args)>(args)...) {}
  static void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                              FONodeBase &obj, unsigned binding) {
    auto &casted = static_cast<CombinedImageSamplerAdaptorImpl &>(obj);
    auto *view = dynamic_cast<MatImageViewBase *>(&obj.getUseRaw(0));
    assert(view);
    vkw::DescriptorWrite write{binding,
                               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    write.addImage(casted.get(), view->view(frame),
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    set.write(write);
  }
};

class ImageSampledAdaptorImpl
    : public FONode<char, fon_type::cow, ImageSampledAdaptorImpl> {
public:
  using Base = FONode<char, fon_type::cow, ImageSampledAdaptorImpl>;
  ImageSampledAdaptorImpl(FramedEngine &engine, const MatImageView &view)
      : Base(engine, char(0), FOUses{&view->node()}) {
    if (view->type() == fon_type::ext) {
      throw std::runtime_error(
          "Cannot create descriptor adaptor for external object");
    }
    auto &node = view->node();
    if (node.isDestroyed())
      node.construct();
  }
  char constructNew(FramedEngine &engine) { return 0; }
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
    vkw::DescriptorWrite write{binding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE};
    write.addImage(nullptr, view->view(frame),
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    set.write(write);
  }
};

class ImageStorageAdaptorImpl
    : public FONode<char, fon_type::cow, ImageStorageAdaptorImpl> {
public:
  using Base = FONode<char, fon_type::cow, ImageStorageAdaptorImpl>;
  ImageStorageAdaptorImpl(FramedEngine &engine, const MatImageView &view)
      : Base(engine, char(0), FOUses{&view->node()}) {
    if (view->type() == fon_type::ext) {
      throw std::runtime_error(
          "Cannot create descriptor adaptor for external object");
    }
    auto &node = view->node();
    if (node.isDestroyed())
      node.construct();
  }
  char constructNew(FramedEngine &engine) { return 0; }
};

class ImageStorageAdaptor : public FONodeView<ImageStorageAdaptorImpl> {
public:
  ImageStorageAdaptor(auto &&...args)
      : FONodeView<ImageStorageAdaptorImpl>(
            std::forward<decltype(args)>(args)...) {}
  static void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                              FONodeBase &obj, unsigned binding) {
    auto *view = dynamic_cast<MatImageViewBase *>(&obj.getUseRaw(0));
    assert(view);
    vkw::DescriptorWrite write{binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    write.addImage(nullptr, view->view(frame), VK_IMAGE_LAYOUT_GENERAL);
    set.write(write);
  }
};

static Scene::MaterializedDescriptor
materializeImageDescriptor(MaterializationContext &ctx, const Use &use) {
  assert(isa<ImageDescriptorUseInfo>(use.info()));
  const auto &info = static_cast<const ImageDescriptorUseInfo &>(*use.info());
  auto view = ctx.get<MatImageView>(use.value());
  auto image = ctx.get<MatImage>(use.value());
  if (image->node().isDestroyed())
    image->node().construct();

  Descriptor descriptor = [&]() -> Descriptor {
    switch (info.type()) {
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return Descriptor{ctx.env().engine(),
                        CombinedImageSamplerAdaptor(ctx.env().engine(), view)};
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      return Descriptor{ctx.env().engine(),
                        ImageSampledAdaptor(ctx.env().engine(), view)};
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      return Descriptor{ctx.env().engine(),
                        ImageStorageAdaptor(ctx.env().engine(), view)};
    default:
      throw std::runtime_error("unsupported image descriptor type");
    }
  }();
  return {std::move(descriptor), std::move(image)};
}

const AttributesBase *RenderPass::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  // todo: safe cast
  auto &imageDefInfo = static_cast<const ImageDefInfo &>(result.info());

  assert(imageDefInfo.passthrough);
  return useAttributes[*imageDefInfo.passthrough];
}

const AttributesBase *ComputePass::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  const auto &imageDefInfo = static_cast<const ImageDefInfo &>(result.info());
  assert(imageDefInfo.passthrough);
  return useAttributes[*imageDefInfo.passthrough];
}

const AttributesBase *Constant<IntegerScalarTy>::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  return &ctx.attributes().get<Attributes<IntegerScalarTy>>(
      constant<size_t>(value));
}

const AttributesBase *Constant<FormatTy>::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  return &ctx.attributes().get<Attributes<FormatTy>>(constant<VkFormat>(value));
}

const AttributesBase *Constant<ExtentsTy>::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  return &ctx.attributes().get<Attributes<ExtentsTy>>(
      constant<VkExtent3D>(value), constant(imageTypeForExtents(value)));
}

const AttributesBase *ScreenExtents::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.empty());
  return &ctx.attributes().get<Attributes<ExtentsTy>>(
      dynamic<VkExtent3D>(result), constant(VK_IMAGE_TYPE_2D));
}

const AttributesBase *HalfExtents::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 1);
  const auto &input =
      static_cast<const Attributes<ExtentsTy> &>(*useAttributes.front());
  auto extents = [&]() -> Attribute<VkExtent3D> {
    using Status = Attribute<VkExtent3D>::Status;
    switch (input.extents.status()) {
    case Status::undefined:
      return undefined<VkExtent3D>();
    case Status::constant:
      return constant(halfExtents(*input.extents.getConstant()));
    case Status::dynamic:
      return dynamic<VkExtent3D>(result);
    case Status::overdefined:
      return overdefined<VkExtent3D>();
    }
    return undefined<VkExtent3D>();
  }();
  return &ctx.attributes().get<Attributes<ExtentsTy>>(extents, input.imageType);
}

const AttributesBase *GetExtents::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 1);
  auto *imgAttr = dyn_cast<const Attributes<ImageTy>>(useAttributes.front());
  assert(imgAttr);
  return &ctx.attributes().get<Attributes<ExtentsTy>>(imgAttr->extents,
                                                      imgAttr->imageType);
}

static Attribute<VkImageType>
imageTypeForExtents(const Attributes<ExtentsTy> &extentsAttributes) {
  if (extentsAttributes.imageType.status() !=
      Attribute<VkImageType>::Status::undefined)
    return extentsAttributes.imageType;
  const auto &extents = extentsAttributes.extents;
  using Status = Attribute<VkExtent3D>::Status;
  switch (extents.status()) {
  case Status::undefined:
    return undefined<VkImageType>();
  case Status::constant:
    return constant(graph::imageTypeForExtents(*extents.getConstant()));
  case Status::dynamic:
    return dynamic<VkImageType>(*extents.getDynamicValue());
  case Status::overdefined:
    return overdefined<VkImageType>();
  }
  return undefined<VkImageType>();
}

const AttributesBase *MakeImage::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 4);
  const auto &extentsAttributes =
      static_cast<const Attributes<ExtentsTy> &>(*useAttributes[0]);
  const auto extents = extentsAttributes.extents;
  auto format =
      static_cast<const Attributes<FormatTy> &>(*useAttributes[1]).value;
  auto layers =
      static_cast<const Attributes<IntegerScalarTy> &>(*useAttributes[2]).value;
  auto levels =
      static_cast<const Attributes<IntegerScalarTy> &>(*useAttributes[3]).value;
  return &ctx.attributes().get<Attributes<ImageTy>>(
      extents, format, imageTypeForExtents(extentsAttributes), layers, levels);
}

const AttributesBase *ConvertFormat::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 2);
  const auto &image =
      static_cast<const Attributes<ImageTy> &>(*useAttributes[0]);
  const auto format =
      static_cast<const Attributes<FormatTy> &>(*useAttributes[1]).value;
  return &ctx.attributes().get<Attributes<ImageTy>>(
      image.extents, format, image.imageType, image.layers, image.levels);
}

const AttributesBase *ResizeImage::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 2);
  const auto &image =
      static_cast<const Attributes<ImageTy> &>(*useAttributes[0]);
  const auto &extentsAttributes =
      static_cast<const Attributes<ExtentsTy> &>(*useAttributes[1]);
  const auto extents = extentsAttributes.extents;
  const auto imageType = m_imageType ? constant(*m_imageType)
                                     : imageTypeForExtents(extentsAttributes);
  return &ctx.attributes().get<Attributes<ImageTy>>(
      extents, image.format, imageType, image.layers, image.levels);
}

const AttributesBase *ConvertResizeImage::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 3);
  const auto &image =
      static_cast<const Attributes<ImageTy> &>(*useAttributes[0]);
  const auto &extentsAttributes =
      static_cast<const Attributes<ExtentsTy> &>(*useAttributes[1]);
  const auto format =
      static_cast<const Attributes<FormatTy> &>(*useAttributes[2]).value;
  const auto imageType = m_imageType ? constant(*m_imageType)
                                     : imageTypeForExtents(extentsAttributes);
  return &ctx.attributes().get<Attributes<ImageTy>>(
      extentsAttributes.extents, format, imageType, image.layers, image.levels);
}

const AttributesBase *AssumeCompatibleFormat::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 1);
  return useAttributes.front();
}

const AttributesBase *AssumeCompatibleExtents::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  assert(useAttributes.size() == 1);
  return useAttributes.front();
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

const AttributesBase *Dynamic<FormatTy>::getAttributes(
    Context &ctx, const Value &result,
    std::span<const AttributesBase *> useAttributes) const {
  assert(&result == results().data());
  return &ctx.attributes().get<Attributes<FormatTy>>(dynamic<VkFormat>(result));
}

bool MakeImage::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.env().engine();
  auto &value = results().front();
  if (!ctx.startsImageChain(value))
    return false;
  auto templ = ctx.chainImageTemplate(value);
  if (ctx.isPresentedImageChain(value)) {
    ctx.materializeImageChain(
        value, engine.createNode<SwapchainImageNode>(ctx, templ));
    return true;
  }
  ctx.materializeImageChain(
      value, engine.createNode<RegularImageNode>(
                 ctx, ctx.get<MatExtents>(uses()[0].value()),
                 ctx.get<MatFormat>(uses()[1].value()),
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

static VkExtent3D mipExtent(VkExtent3D extent, uint32_t level) {
  while (level--) {
    extent.width = std::max(1u, extent.width / 2);
    extent.height = std::max(1u, extent.height / 2);
    extent.depth = std::max(1u, extent.depth / 2);
  }
  return extent;
}

bool ResizeImage::materialize(MaterializationContext &ctx) {
  auto &value = results().front();
  if (!ctx.startsImageChain(value))
    return false;

  auto &engine = ctx.env().engine();
  auto src = ctx.get<MatImage>(uses()[0].value());
  auto extents = ctx.get<MatExtents>(uses()[1].value());
  auto outputTemplate = ctx.chainImageTemplate(value);
  if (m_imageType)
    outputTemplate.imageType = *m_imageType;
  const auto outputImageType =
      outputTemplate.imageType == VK_IMAGE_TYPE_MAX_ENUM
          ? std::nullopt
          : std::optional{outputTemplate.imageType};
  const auto outputUsage = outputTemplate.usage;
  MatImage dst;
  if (ctx.isPresentedImageChain(value))
    dst = engine.createNode<SwapchainImageNode>(ctx, outputTemplate);
  else
    dst = engine.createNode<ResizedImageNode>(src, extents, outputUsage,
                                              outputImageType);
  ctx.materializeImageChain(value, dst);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

  ctx.materializeNode(*this, [src = std::move(src), dst = std::move(dst),
                              barrier](vkw::BufferRecorder &recorder,
                                       const imvk::Frame &frame) mutable {
    const auto srcImage = src->useImage(frame);
    const auto dstImage = dst->useImage(frame);
    const auto &srcInfo = src->info();
    const auto &dstInfo = dst->info();
    barrier.image = dstImage;
    barrier.subresourceRange = completeSubresourceRange(dstInfo);

    boost::container::small_vector<VkImageBlit, 4> blits;
    const auto mipLevels = std::min(srcInfo.mipLevels, dstInfo.mipLevels);
    blits.reserve(mipLevels);
    for (uint32_t level = 0; level < mipLevels; ++level) {
      const auto srcExtent = mipExtent(srcInfo.extent, level);
      const auto dstExtent = mipExtent(dstInfo.extent, level);
      auto srcSubresource = completeSubresourceRangeLayers(srcInfo);
      srcSubresource.mipLevel = level;
      auto dstSubresource = completeSubresourceRangeLayers(dstInfo);
      dstSubresource.mipLevel = level;

      VkImageBlit blit{};
      blit.srcSubresource = srcSubresource;
      blit.srcOffsets[1] = {static_cast<int32_t>(srcExtent.width),
                            static_cast<int32_t>(srcExtent.height),
                            static_cast<int32_t>(srcExtent.depth)};
      blit.dstSubresource = dstSubresource;
      blit.dstOffsets[1] = {static_cast<int32_t>(dstExtent.width),
                            static_cast<int32_t>(dstExtent.height),
                            static_cast<int32_t>(dstExtent.depth)};
      blits.push_back(blit);
    }

    auto transfer = recorder.beginTransferPass();
    transfer.imageMemoryBarrier(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                std::array{barrier});
    transfer.blitImage(srcImage, dstImage, blits, VK_FILTER_NEAREST);
  });
  return true;
}

bool ConvertResizeImage::materialize(MaterializationContext &ctx) {
  auto &value = results().front();
  if (!ctx.startsImageChain(value))
    return false;

  auto &engine = ctx.env().engine();
  auto src = ctx.get<MatImage>(uses()[0].value());
  auto extents = ctx.get<MatExtents>(uses()[1].value());
  auto format = ctx.get<MatFormat>(uses()[2].value());
  auto outputTemplate = ctx.chainImageTemplate(value);
  if (m_imageType)
    outputTemplate.imageType = *m_imageType;
  const auto outputImageType =
      outputTemplate.imageType == VK_IMAGE_TYPE_MAX_ENUM
          ? std::nullopt
          : std::optional{outputTemplate.imageType};
  const auto outputUsage = outputTemplate.usage;
  MatImage dst;
  if (ctx.isPresentedImageChain(value))
    dst = engine.createNode<SwapchainImageNode>(ctx, outputTemplate);
  else
    dst = engine.createNode<ConvertedResizedImageNode>(
        src, extents, format, outputUsage, outputImageType);
  ctx.materializeImageChain(value, dst);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

  ctx.materializeNode(*this, [src = std::move(src), dst = std::move(dst),
                              barrier](vkw::BufferRecorder &recorder,
                                       const imvk::Frame &frame) mutable {
    const auto srcImage = src->useImage(frame);
    const auto dstImage = dst->useImage(frame);
    const auto &srcInfo = src->info();
    const auto &dstInfo = dst->info();
    barrier.image = dstImage;
    barrier.subresourceRange = completeSubresourceRange(dstInfo);

    boost::container::small_vector<VkImageBlit, 4> blits;
    const auto mipLevels = std::min(srcInfo.mipLevels, dstInfo.mipLevels);
    blits.reserve(mipLevels);
    for (uint32_t level = 0; level < mipLevels; ++level) {
      const auto srcExtent = mipExtent(srcInfo.extent, level);
      const auto dstExtent = mipExtent(dstInfo.extent, level);
      auto srcSubresource = completeSubresourceRangeLayers(srcInfo);
      srcSubresource.mipLevel = level;
      auto dstSubresource = completeSubresourceRangeLayers(dstInfo);
      dstSubresource.mipLevel = level;

      VkImageBlit blit{};
      blit.srcSubresource = srcSubresource;
      blit.srcOffsets[1] = {static_cast<int32_t>(srcExtent.width),
                            static_cast<int32_t>(srcExtent.height),
                            static_cast<int32_t>(srcExtent.depth)};
      blit.dstSubresource = dstSubresource;
      blit.dstOffsets[1] = {static_cast<int32_t>(dstExtent.width),
                            static_cast<int32_t>(dstExtent.height),
                            static_cast<int32_t>(dstExtent.depth)};
      blits.push_back(blit);
    }

    auto transfer = recorder.beginTransferPass();
    transfer.imageMemoryBarrier(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                std::array{barrier});
    transfer.blitImage(srcImage, dstImage, blits, VK_FILTER_NEAREST);
  });
  return true;
}

bool ConvertFormat::materialize(MaterializationContext &ctx) {
  auto &value = results().front();
  if (!ctx.startsImageChain(value))
    return false;

  auto &engine = ctx.env().engine();
  auto src = ctx.get<MatImage>(uses()[0].value());
  auto format = ctx.get<MatFormat>(uses()[1].value());
  const auto outputUsage = ctx.chainImageTemplate(value).usage;
  MatImage dst;
  if (ctx.isPresentedImageChain(value))
    dst = engine.createNode<SwapchainImageNode>(ctx,
                                                ctx.chainImageTemplate(value));
  else
    dst = engine.createNode<ConvertedImageNode>(src, format, outputUsage);
  ctx.materializeImageChain(value, dst);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

  ctx.materializeNode(*this, [src = std::move(src), dst = std::move(dst),
                              barrier](vkw::BufferRecorder &recorder,
                                       const imvk::Frame &frame) mutable {
    const auto srcImage = src->useImage(frame);
    const auto dstImage = dst->useImage(frame);
    const auto &srcInfo = src->info();
    const auto &dstInfo = dst->info();
    barrier.image = dstImage;
    barrier.subresourceRange = completeSubresourceRange(dstInfo);

    boost::container::small_vector<VkImageBlit, 4> blits;
    blits.reserve(srcInfo.mipLevels);
    for (uint32_t level = 0; level < srcInfo.mipLevels; ++level) {
      const auto extent = mipExtent(srcInfo.extent, level);
      auto srcSubresource = completeSubresourceRangeLayers(srcInfo);
      srcSubresource.mipLevel = level;
      auto dstSubresource = completeSubresourceRangeLayers(dstInfo);
      dstSubresource.mipLevel = level;

      VkImageBlit blit{};
      blit.srcSubresource = srcSubresource;
      blit.srcOffsets[1] = {static_cast<int32_t>(extent.width),
                            static_cast<int32_t>(extent.height),
                            static_cast<int32_t>(extent.depth)};
      blit.dstSubresource = dstSubresource;
      blit.dstOffsets[1] = blit.srcOffsets[1];
      blits.push_back(blit);
    }

    auto transfer = recorder.beginTransferPass();
    transfer.imageMemoryBarrier(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                std::array{barrier});
    transfer.blitImage(srcImage, dstImage, blits, VK_FILTER_NEAREST);
  });
  return true;
}

bool AssumeCompatibleFormat::materialize(MaterializationContext &ctx) {
  // Analysis-only marker removed before image-chain materialization.
  return false;
}

bool AssumeCompatibleExtents::materialize(MaterializationContext &ctx) {
  // Analysis-only marker removed before image-chain materialization.
  return false;
}

bool Clone<ImageTy>::materialize(MaterializationContext &ctx) {
  auto &engine = ctx.env().engine();
  auto &value = results().front();
  if (!ctx.startsImageChain(value))
    return false;
  auto templ = ctx.chainImageTemplate(value);
  auto src = ctx.get<MatImage>(uses().front().value());
  MatImage dst;
  if (ctx.isPresentedImageChain(value))
    dst = engine.createNode<SwapchainImageNode>(ctx, templ);
  else
    dst = engine.createNode<CopyImageNode>(src, templ);

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
        current->load != candidate.load ||
        current->access != candidate.access ||
        current->viewTypeConstraint != candidate.viewTypeConstraint)
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
         currentImage->access != candidateImage->access ||
         currentImage->viewTypeConstraint !=
             candidateImage->viewTypeConstraint))
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
    auto image = ctx.get<MatImage>(use.value());
    attachments.emplace_back(ctx.get<MatImageView>(use.value()), useInfo.kind);
    sceneInfo.attachments.emplace_back(image);
    VkRenderingAttachmentInfo a{};
    a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    a.imageLayout = useInfo.access.layout;
    auto &img = *image;
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
    sceneInfo.descriptors.push_back(materializeImageDescriptor(ctx, use));
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

bool ComputePass::acceptsComputeContext(
    const ComputeContext &computeContext) const {
  if (computeContext.descriptors.size() != uses().size())
    return false;

  unsigned resultIndex = 0;
  for (auto &&[use, candidate] :
       std::views::zip(uses(), computeContext.descriptors)) {
    const auto *current = dyn_cast<ImageDescriptorUseInfo>(use.info());
    const auto *replacement =
        dyn_cast<ImageDescriptorUseInfo>(&candidate.useInfo());
    if (!current || !replacement || current->type() != replacement->type() ||
        current->access.layout != replacement->access.layout ||
        current->access.usage != replacement->access.usage ||
        current->viewTypeConstraint != replacement->viewTypeConstraint)
      return false;

    const auto expectedPassthrough = candidate.isPassthrough()
                                         ? std::optional<size_t>{resultIndex++}
                                         : std::nullopt;
    if (current->passthrough != expectedPassthrough)
      return false;
  }
  return resultIndex == results().size();
}

bool ComputePass::setComputeContext(const ComputeContext &computeContext) {
  if (!acceptsComputeContext(computeContext))
    return false;
  m_computeContext = &computeContext;
  return true;
}

bool ComputePass::materialize(MaterializationContext &ctx) {
  ComputeContext::MaterializationInfo computeInfo{};
  for (auto &&use : uses())
    computeInfo.descriptors.push_back(materializeImageDescriptor(ctx, use));

  auto matComputeContext =
      std::invoke(m_computeContext->materialization, ctx.env(), computeInfo);
  ctx.materializeNode(*this, [matComputeContext = std::move(matComputeContext)](
                                 vkw::BufferRecorder &recorder,
                                 const imvk::Frame &frame) mutable {
    auto computePass = recorder.beginComputePass();
    matComputeContext->onCompute(computePass, frame);
  });
  return false;
}

bool Present::materialize(MaterializationContext &ctx) {
  // nothing to materialize for now.
  return false;
}

} // namespace imvk::graph