#pragma once

#include "imvk/graph/Context.hpp"
#include "imvk/graph/Materialization.hpp"
#include "imvk/graph/Node.hpp"
#include "imvk/graph/Types.hpp"

#include "imvk/base/DescriptorSet.hpp"
#include "imvk/graphics/Pipeline.hpp"

#include <array>
#include <span>

#include <vkw/CommandRecorder.hpp>

namespace imvk::graph {

// Source nodes

template <typename Ty> class Constant {};

template <> class Constant<IntegerScalarTy> : public Node {
public:
  Constant(Context &ctx, auto &&v)
      : Node(ctx, Node::EmptyUses,
             std::array{Node::Def{&ctx.types().get<IntegerScalarTy>(),
                                  new BasicDefInfo{"value"}}}),
        value(static_cast<size_t>(v)) {}
  size_t value;
  size_t getValue() const { return value; }
  void setValue(size_t newValue) { value = newValue; }
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "constant"; }
  void dumpAttributes(std::ostream &os) const final { os << value; }
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;

private:
  Constant(size_t v) : value(v) {}
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new Constant(value)};
  }
};

template <> class Constant<FormatTy> : public Node {
public:
  Constant(Context &ctx, VkFormat v)
      : Node(ctx, Node::EmptyUses,
             std::array{Node::Def{&ctx.types().get<FormatTy>(),
                                  new BasicDefInfo{"value"}}}),
        value(v) {}
  VkFormat value;
  VkFormat getValue() const { return value; }
  void setValue(VkFormat newValue) { value = newValue; }
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "constant"; }
  void dumpAttributes(std::ostream &os) const final { os << value; }
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;

private:
  Constant(VkFormat v) : value(v) {}
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new Constant(value)};
  }
};

template <> class Constant<ExtentsTy> : public Node {
public:
  Constant(Context &ctx, VkExtent3D v)
      : Node(ctx, Node::EmptyUses,
             std::array{Node::Def{&ctx.types().get<ExtentsTy>(),
                                  new BasicDefInfo{"value"}}}),
        value(v) {}
  VkExtent3D value;
  VkExtent3D getValue() const { return value; }
  void setValue(VkExtent3D newValue) { value = newValue; }
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "constant"; }
  void dumpAttributes(std::ostream &os) const final { os << value; }
  bool hasVisibleSideEffects() const final { return false; }

  bool materialize(MaterializationContext &ctx) final;

private:
  Constant(VkExtent3D v) : value(v) {}
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new Constant(value)};
  }
};

template <typename Ty> class Dynamic {};

template <> class Dynamic<IntegerScalarTy> : public Node {
public:
  Dynamic(Context &ctx, auto &&p)
      : Node(ctx, Node::EmptyUses,
             std::array{Node::Def{&ctx.types().get<IntegerScalarTy>(),
                                  new BasicDefInfo{"value"}}}),
        producer(std::forward<decltype(p)>(p)) {}
  std::function<size_t()> producer;
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "dynamic"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;

private:
  Dynamic(std::function<size_t()> p) : producer(std::move(p)) {}
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new Dynamic(producer)};
  }
};

template <> class Dynamic<FormatTy> : public Node {
public:
  Dynamic(Context &ctx, auto &&p)
      : Node(ctx, Node::EmptyUses,
             std::array{Node::Def{&ctx.types().get<FormatTy>(),
                                  new BasicDefInfo{"value"}}}),
        producer(std::forward<decltype(p)>(p)) {}
  std::function<VkFormat()> producer;
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "dynamic"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;

private:
  Dynamic(std::function<VkFormat()> p) : producer(std::move(p)) {}
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new Dynamic(producer)};
  }
};

class MakeImage : public Node {
public:
  MakeImage(Context &ctx, const ImageTy &type)
      : Node(ctx,
             std::array{Node::Use{nullptr, &ctx.types().get<ExtentsTy>(),
                                  new BasicUseInfo{"extents"}},
                         Node::Use{nullptr, &ctx.types().get<FormatTy>(),
                                  new BasicUseInfo{"format"}},
                        Node::Use{nullptr, &ctx.types().get<IntegerScalarTy>(),
                                  new BasicUseInfo{"layers"}},
                        Node::Use{nullptr, &ctx.types().get<IntegerScalarTy>(),
                                  new BasicUseInfo{"mip levels"}}},
             std::array{Node::Def{
                 &type, new ImageDefInfo{ImageAccessInfo{}, "image"}}}) {}
  MakeImage(Context &ctx, const ImageTy &type, Value &extents, Value &format,
            Value &layers, Value &mips)
      : Node(ctx,
             std::array{Node::Use{&extents, new BasicUseInfo{"extents"}},
                         Node::Use{&format, &ctx.types().get<FormatTy>(),
                                   new BasicUseInfo{"format"}},
                        Node::Use{&layers, new BasicUseInfo{"layers"}},
                        Node::Use{&mips, new BasicUseInfo{"mip levels"}}},
             std::array{Node::Def{
                 &type, new ImageDefInfo{ImageAccessInfo{}, "image"}}}) {}
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final;
  std::string_view name() const final { return "make_image"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }

  bool materialize(MaterializationContext &ctx) final;

private:
  MakeImage() = default;
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new MakeImage()};
  }
};

class AcquireImage : public Node {
public:
  AcquireImage(Context &ctx)
      : Node(ctx, Node::EmptyUses,
             std::array{Node::Def{
                 &ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                 new ImageDefInfo{ImageAccessInfo{}, "swapchain image"}}}) {}
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::string_view name() const final { return "acquire_image"; }
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final {
    return std::nullopt;
  }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }

  bool materialize(MaterializationContext &ctx) final;

private:
  AcquireImage() = default;
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new AcquireImage()};
  }
};

// Attribute read nodes.

class GetExtents : public Node {
public:
  GetExtents(Context &ctx)
      : Node(ctx,
             std::array{
                 Node::Use{nullptr, &ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                           new ImageUseInfo{ImageAccessInfo{}, "image"}}},
             std::array{Node::Def{&ctx.types().get<ExtentsTy>(),
                                  new BasicDefInfo{"extents"}}}) {}
  GetExtents(Context &ctx, Value &image)
      : Node(ctx,
             std::array{Node::Use{
                 &image, new ImageUseInfo{ImageAccessInfo{}, "image"}}},
             std::array{Node::Def{&ctx.types().get<ExtentsTy>(),
                                  new BasicDefInfo{"extents"}}}) {}

  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "get_extents"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;

private:
  GetExtents() = default;
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new GetExtents()};
  }
};

#if 0
class GetFormat : public Node {
public:
  GetFormat(Value &image);
};


class GetLayers : public Node {
public:
  GetLayers(Value &image);
};

class GetMips : public Node {
public:
  GetMips(Value &image);
};
#endif

// Copy and barriers.

template <typename T> class Clone {};
template <typename T> class Copy {};

class ConvertFormat : public Node {
public:
  ConvertFormat(Context &ctx)
      : Node(ctx,
             std::array{
                 Node::Use{
                     nullptr, &ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                     new ImageUseInfo{
                         ImageAccessInfo{
                             .accessFlags = VK_ACCESS_MEMORY_READ_BIT,
                             .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT,
                             .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                             .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
                         "image"}},
                 Node::Use{nullptr, &ctx.types().get<FormatTy>(),
                           new BasicUseInfo{"format"}}},
             std::array{Node::Def{
                 &ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                 new ImageDefInfo{
                     ImageAccessInfo{
                         .accessFlags = VK_ACCESS_MEMORY_WRITE_BIT,
                         .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT,
                         .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
                     "image"}}}) {}
  ConvertFormat(Context &ctx, Value &image, Value &format)
      : Node(ctx,
             std::array{
                 Node::Use{
                     &image,
                     new ImageUseInfo{
                         ImageAccessInfo{
                             .accessFlags = VK_ACCESS_MEMORY_READ_BIT,
                             .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT,
                             .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                             .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
                         "image"}},
                 Node::Use{&format, &ctx.types().get<FormatTy>(),
                           new BasicUseInfo{"format"}}},
             std::array{Node::Def{
                 &image.type(),
                 new ImageDefInfo{
                     ImageAccessInfo{
                         .accessFlags = VK_ACCESS_MEMORY_WRITE_BIT,
                         .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT,
                         .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
                     "image"}}}) {}

  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "convert_format"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;

private:
  ConvertFormat() = default;
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new ConvertFormat()};
  }
};

template <> class Clone<ImageTy> : public Node {
public:
  Clone(Context &ctx)
      : Node(ctx,
             std::array{
                 Node::Use{nullptr, &ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                           new ImageUseInfo{ImageAccessInfo{}, "source"}}},
             std::array{
                 Node::Def{&ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                           new ImageDefInfo{ImageAccessInfo{}, "clone"}}}) {}
  Clone(Context &ctx, Value &image)
      : Node(ctx,
             std::array{
                 Node::Use(&image, new ImageUseInfo{[]() {
                                                      ImageAccessInfo info{};
                                                      return info;
                                                    }(),
                                                    "source"})},
             std::array{Node::Def(&image.type(),
                                  new ImageDefInfo{[]() {
                                                     ImageAccessInfo info{};
                                                     return info;
                                                   }(),
                                                   "clone"})}) {}

  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "clone"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;

private:
  Clone() = default;
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new Clone()};
  }
};

template <> class Copy<ImageTy> : public Node {
public:
  Copy(Context &ctx)
      : Node(ctx,
             std::array{
                 Node::Use{
                     nullptr, &ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                     new ImageUseInfo{
                         ImageAccessInfo{
                             .accessFlags = VK_ACCESS_MEMORY_READ_BIT,
                             .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT,
                             .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                             .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
                         "source"}},
                 Node::Use{
                     nullptr, &ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                     new ImageUseInfo{
                         ImageAccessInfo{
                             .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
                         0, "destination"}}},
             std::array{Node::Def(
                 &ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                 new ImageDefInfo{
                     ImageAccessInfo{
                         .accessFlags = VK_ACCESS_MEMORY_WRITE_BIT,
                         .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT,
                         .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
                     1, "destination"})}) {}
  Copy(Context &ctx, Value &src, Value &dst)
      : Node(ctx,
             std::array{
                 Node::Use(&src,
                           new ImageUseInfo{
                               []() {
                                 ImageAccessInfo info{};
                                 info.layout =
                                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                                 info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                                 info.accessFlags = VK_ACCESS_MEMORY_READ_BIT;
                                 info.stageFlags =
                                     VK_PIPELINE_STAGE_TRANSFER_BIT;
                                 return info;
                               }(),
                               "source"}),
                 Node::Use(
                     &dst,
                     new ImageUseInfo{
                         ImageAccessInfo{
                             .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
                         0, "destination"})},
             std::array{Node::Def(
                 &dst.type(),
                 new ImageDefInfo{
                     ImageAccessInfo{
                         .accessFlags = VK_ACCESS_MEMORY_WRITE_BIT,
                         .stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT,
                         .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
                     1, "destination"})}) {}

  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final;
  std::string_view name() const final { return "copy"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;

private:
  Copy() = default;
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new Copy()};
  }
};

template <typename T> class Barrier {};

template <> class Barrier<ImageTy> : public Node {
public:
  Barrier(Context &ctx, VkImageLayout src, VkImageLayout dst)
      : Node(ctx,
             std::array{Node::Use{
                 nullptr, &ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                 new ImageUseInfo{ImageAccessInfo{.layout = src}, 0, "image"}}},
             std::array{
                 Node::Def(&ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                           new ImageDefInfo{ImageAccessInfo{.layout = dst}, 0,
                                            "image"})}) {}
  Barrier(Context &ctx, Value &image, VkImageLayout src, VkImageLayout dst)
      : Node(ctx,
             std::array{Node::Use(&image,
                                  [&]() {
                                    ImageAccessInfo info{};
                                    info.layout = src;
                                    info.accessFlags = 0;
                                    info.stageFlags = 0;
                                    auto ret = new ImageUseInfo{info};
                                    ret->passthrough = 0;
                                    ret->setName("image");
                                    return ret;
                                  }())},
             std::array{Node::Def(&image.type(), [&]() {
               ImageAccessInfo info{};
               info.layout = dst;
               info.accessFlags = 0;
               info.stageFlags = 0;
               auto ret = new ImageDefInfo{info};
               ret->passthrough = 0;
               ret->setName("image");
               return ret;
             }())}) {}

  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "barrier"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;

private:
  Barrier() = default;
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new Barrier()};
  }
};

// Command nodes

struct FramebufferInfoFields {
  VkExtent3D extents;
  bool isSwapchain;
};

class FramebufferInfoImpl
    : public FONode<FramebufferInfoFields, fon_type::cow, FramebufferInfoImpl> {
public:
  FramebufferInfoImpl(FramedEngine &ge, MatImage refAttachment);

  FramebufferInfoFields constructNew(FramedEngine &);

private:
  FramebufferInfoFields doConstructNew(FramedEngine &ge,
                                       MatImage refAttachment);
};

class FramebufferInfo : public FONodeView<FramebufferInfoImpl> {
public:
  FramebufferInfo(auto &&...args)
      : FONodeView<FramebufferInfoImpl>(std::forward<decltype(args)>(args)...) {
  }
};

/// @brief Scene is an interface consumed by render pass node. It must describe
/// framebuffer and descriptor layout and provide callbacks for materialization
/// and rendering.

class MatScene {
public:
  virtual void onDraw(vkw::RenderPassRecorder &commands,
                      const Frame &frame) = 0;
  virtual ~MatScene() = default;
};
struct Scene final {

  struct MaterializationInfo {
    vkw::RenderingFormatInfo renderingInfo;
    FramebufferInfo framebufferInfo;
    boost::container::small_vector<Descriptor, 2> descriptors;
  };

  boost::container::small_vector<ImageAttachmentUseInfo, 2> attachments;
  boost::container::small_vector<DescriptorUseInfo, 2> descriptors;
  std::function<std::unique_ptr<MatScene>(const MaterializationEnvironment &,
                                          const MaterializationInfo &)>
      materialization;
};

class RenderPass : public Node {
public:
  RenderPass(Context &ctx, const Scene &scene)
      : Node(
            ctx,
            [&]() {
              const auto &imageType =
                  ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D);
              boost::container::small_vector<Node::Use, 4> uses;
              unsigned passIndex = 0;
              for (const auto &info : scene.attachments) {
                auto *copyInfo = info.clone();
                switch (copyInfo->kind) {
                case ImageAttachmentUseInfo::Kind::color:
                  copyInfo->setName("color attachment");
                  break;
                case ImageAttachmentUseInfo::Kind::depth:
                  copyInfo->setName("depth attachment");
                  break;
                case ImageAttachmentUseInfo::Kind::input:
                  copyInfo->setName("input attachment");
                  break;
                }
                if (copyInfo->kind != ImageAttachmentUseInfo::Kind::input)
                  copyInfo->passthrough = passIndex++;
                uses.emplace_back(nullptr, &imageType, copyInfo);
              }
              for (const auto &info : scene.descriptors) {
                auto *copyInfo = info.useInfo().clone();
                copyInfo->setName("descriptor");
                uses.emplace_back(nullptr, &imageType, copyInfo);
              }
              return uses;
            }(),
            [&]() {
              const auto &imageType =
                  ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D);
              boost::container::small_vector<Node::Def, 4> defs;
              unsigned passIndex = 0;
              for (const auto &info : scene.attachments) {
                if (info.kind != ImageAttachmentUseInfo::Kind::input) {
                  auto *def = new ImageDefInfo{info.defFromThis()};
                  def->passthrough = passIndex;
                  def->setName(info.kind == ImageAttachmentUseInfo::Kind::color
                                   ? "color attachment"
                                   : "depth attachment");
                  defs.emplace_back(&imageType, def);
                }
                ++passIndex;
              }
              return defs;
            }()),
        m_scene(&scene), m_firstDescriptor(scene.attachments.size()) {}
  /// @brief create render pass node.
  /// @param ctx graph context handle.
  /// @param attachments array of image values. must match scene attachment
  /// layout.
  /// @param descriptors array of descripted values. must match scene attachment
  /// layout.
  /// @param scene a scene handle to render in this pass. it's layout must match
  /// attachments and descriptor values.
  RenderPass(Context &ctx, auto &&attachments, auto &&descriptors,
             const Scene &scene)
      : Node(
            ctx,
            [&]() {
              auto &attachmentLayout = scene.attachments;
              if (attachmentLayout.size() != std::ranges::size(attachments))
                throw std::runtime_error(
                    "scene has incompatible number of attachments");
              boost::container::small_vector<Node::Use, 4> uses;
              unsigned passIndex = 0;
              for (auto &&[val, info] :
                   std::views::zip(attachments, attachmentLayout)) {
                auto *copyInfo = info.clone();
                switch (copyInfo->kind) {
                case ImageAttachmentUseInfo::Kind::color:
                  copyInfo->setName("color attachment");
                  break;
                case ImageAttachmentUseInfo::Kind::depth:
                  copyInfo->setName("depth attachment");
                  break;
                case ImageAttachmentUseInfo::Kind::input:
                  copyInfo->setName("input attachment");
                  break;
                }
                if (copyInfo->kind != ImageAttachmentUseInfo::Kind::input) {
                  copyInfo->passthrough = passIndex++;
                }
                uses.emplace_back(val, copyInfo);
              }
              auto &descriptorLayout = scene.descriptors;
              if (descriptorLayout.size() != std::ranges::size(descriptors))
                throw std::runtime_error(
                    "scene has incompatible number of descriptors");
              for (auto &&[val, info] :
                   std::views::zip(descriptors, descriptorLayout)) {
                auto *copyInfo = info.useInfo().clone();
                copyInfo->setName("descriptor");
                uses.emplace_back(val, copyInfo);
              }

              return uses;
            }(),
            [&]() {
              auto &attachmentLayout = scene.attachments;
              if (attachmentLayout.size() != std::ranges::size(attachments))
                throw std::runtime_error(
                    "scene has incompatible number of attachments");
              boost::container::small_vector<Node::Def, 4> defs;
              unsigned passIndex = 0;
              for (auto &&[val, info] :
                   std::views::zip(attachments, attachmentLayout)) {
                if (info.kind != ImageAttachmentUseInfo::Kind::input) {
                  auto *def = new ImageDefInfo{info.defFromThis()};
                  def->passthrough = passIndex;
                  def->setName(info.kind == ImageAttachmentUseInfo::Kind::color
                                   ? "color attachment"
                                   : "depth attachment");
                  defs.emplace_back(&val->type(), def);
                }
                passIndex++;
              }
              return defs;
            }()),
        m_scene(&scene), m_firstDescriptor(std::ranges::size(attachments)) {}
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final;
  std::string_view name() const final { return "render_pass"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;
  const Scene &scene() const { return *m_scene; }
  bool acceptsScene(const Scene &scene) const;
  bool setScene(const Scene &scene);

private:
  const Scene *m_scene;
  unsigned m_firstDescriptor;
  RenderPass(const Scene &scene, unsigned firstDescriptor)
      : m_scene(&scene), m_firstDescriptor(firstDescriptor) {}
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new RenderPass(*m_scene, m_firstDescriptor)};
  }
};

// Terminator nodes

class Present : public Node {
public:
  Present(Context &ctx)
      : Node(ctx,
             std::array{Node::Use{
                 nullptr, &ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                 new ImageUseInfo{
                     ImageAccessInfo{.accessFlags = 0,
                                     .stageFlags =
                                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                     .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
                     "image"}}},
             Node::EmptyResults) {}
  Present(Context &ctx, Value &image)
      : Node(ctx,
             std::array{Node::Use{
                 &image,
                 new ImageUseInfo{
                     ImageAccessInfo{.accessFlags = 0,
                                     .stageFlags =
                                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                     .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
                     "image"}}},
             Node::EmptyResults) {}
  const AttributesBase *getAttributes(
      Context &ctx, const Value &result,
      std::span<const AttributesBase *> useAttributes) const override {
    return nullptr;
  }
  std::optional<VerifyError> verify(Context &ctx,
                                    const AttributesAnalysis &aa) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "present_image"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return true; }
  bool materialize(MaterializationContext &ctx) final;

private:
  Present() = default;
  std::unique_ptr<Node> doClone() const override {
    return std::unique_ptr<Node>{new Present()};
  }
};

} // namespace imvk::graph