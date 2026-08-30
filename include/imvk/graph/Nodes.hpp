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
             std::array{
                 Node::Def{&ctx.types().get<IntegerScalarTy>(), nullptr}}),
        value(static_cast<size_t>(v)) {}
  size_t value;
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::string_view name() const final { return "constant"; }
  void dumpAttributes(std::ostream &os) const final { os << value; }
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;
};

template <> class Constant<ExtentsTy> : public Node {
public:
  Constant(Context &ctx, VkExtent3D v)
      : Node(ctx, Node::EmptyUses,
             std::array{Node::Def{&ctx.types().get<ExtentsTy>(), nullptr}}),
        value(v) {}
  VkExtent3D value;
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::string_view name() const final { return "constant"; }
  void dumpAttributes(std::ostream &os) const final { os << value; }
  bool hasVisibleSideEffects() const final { return false; }

  bool materialize(MaterializationContext &ctx) final;
};

template <typename Ty> class Dynamic {};

template <> class Dynamic<IntegerScalarTy> : public Node {
public:
  Dynamic(Context &ctx, auto &&p)
      : Node(ctx, Node::EmptyUses,
             std::array{
                 Node::Def{&ctx.types().get<IntegerScalarTy>(), nullptr}}),
        producer(std::forward<decltype(p)>(p)) {}
  std::function<size_t()> producer;
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::string_view name() const final { return "dynamic"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;
};

class MakeImage : public Node {
public:
  MakeImage(Context &ctx, const ImageTy &type, Value &extents, Value &format,
            Value &layers, Value &mips)
      : Node(ctx,
             std::array{Node::Use{&extents, nullptr},
                        Node::Use{&format, nullptr},
                        Node::Use{&layers, nullptr}, Node::Use{&mips, nullptr}},
             std::array{Node::Def{&type, new ImageDefInfo{}}}) {}
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::string_view name() const final { return "make_image"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }

  bool materialize(MaterializationContext &ctx) final;
};

class AcquireImage : public Node {
public:
  AcquireImage(Context &ctx)
      : Node(ctx, Node::EmptyUses,
             std::array{Node::Def{&ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                                  new ImageDefInfo{}}}) {}
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::string_view name() const final { return "acquire_image"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }

  bool materialize(MaterializationContext &ctx) final;
};

// Attribute read nodes.

class GetExtents : public Node {
public:
  GetExtents(Context &ctx, Value &image)
      : Node(ctx, std::array{Node::Use{&image, new ImageUseInfo{}}},
             std::array{Node::Def{&ctx.types().get<ExtentsTy>(), nullptr}}) {}

  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::string_view name() const final { return "get_extents"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;
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

template <> class Clone<ImageTy> : public Node {
public:
  Clone(Context &ctx, Value &image)
      : Node(ctx, std::array{Node::Use(&image, new ImageUseInfo{[]() {
               ImageAccessInfo info{};
               return info;
             }()})},
             std::array{Node::Def(&image.type(), new ImageDefInfo{[]() {
               ImageAccessInfo info{};
               return info;
             }()})}) {}

  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::string_view name() const final { return "clone"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;
};

template <> class Copy<ImageTy> : public Node {
public:
  Copy(Context &ctx, Value &src, Value &dst)
      : Node(ctx,
             std::array{Node::Use(&src, new ImageUseInfo{[]() {
                          ImageAccessInfo info{};
                          info.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                          info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                          info.accessFlags = VK_ACCESS_MEMORY_READ_BIT;
                          info.stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
                          return info;
                        }()}),
                        Node::Use(&dst, new ImageUseInfo{[]() {
                          ImageUseInfo info{};
                          info.access.layout =
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                          info.passthrough = 0;
                          return info;
                        }()})},
             std::array{Node::Def(&dst.type(), new ImageDefInfo{[]() {
               ImageDefInfo info{};
               info.access.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
               info.access.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
               info.access.accessFlags = VK_ACCESS_MEMORY_WRITE_BIT;
               info.access.stageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
               info.passthrough = 1;
               return info;
             }()})}) {}

  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::string_view name() const final { return "copy"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;
};

template <typename T> class Barrier {};

template <> class Barrier<ImageTy> : public Node {
public:
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
                                    return ret;
                                  }())},
             std::array{Node::Def(&image.type(), [&]() {
               ImageAccessInfo info{};
               info.layout = dst;
               info.accessFlags = 0;
               info.stageFlags = 0;
               auto ret = new ImageDefInfo{info};
               ret->passthrough = 0;
               return ret;
             }())}) {}

  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::string_view name() const final { return "barrier"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;
};

// Command nodes

using Attachment = std::pair<Value *, ImageAttachmentUseInfo *>;

inline Attachment colorAttachment(Value &image,
                                  ImageAttachmentUseInfo::LoadOp loadOp) {
  return std::make_pair(
      &image,
      new ImageAttachmentUseInfo{ImageAttachmentUseInfo::Kind::color, loadOp});
}
inline Attachment depthAttachment(Value &image,
                                  ImageAttachmentUseInfo::LoadOp loadOp) {
  return std::make_pair(
      &image,
      new ImageAttachmentUseInfo{ImageAttachmentUseInfo::Kind::depth, loadOp});
}
inline Attachment inputAttachment(Value &image,
                                  ImageAttachmentUseInfo::LoadOp loadOp) {
  return std::make_pair(
      &image,
      new ImageAttachmentUseInfo{ImageAttachmentUseInfo::Kind::input, loadOp});
}

Node::Def attachmentDef(const Attachment &);

Node::Use combinedImageSampler(
    Value &image,
    boost::compat::move_only_function<void(Descriptor)> onMaterialization);

class RenderPass : public Node {
public:
  class PipeHook : public GraphicsPipelineStage {
  public:
    PipeHook(FramedEngine &e, RenderPass &pass, MaterializationContext &ctx,
             unsigned firstDescriptor);
    bool isProvoking() const override { return true; }

    vkw::GraphicsPipelineCreateInfo
    initCreateInfo(const vkw::PipelineLayout &layout) const override;

    void amendCreateInfo(vkw::GraphicsPipelineCreateInfo &info) const override {
      info.addDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
      info.addDynamicState(VK_DYNAMIC_STATE_SCISSOR);
    }

  private:
    vkw::RenderingFormatInfo m_info;
  };
  struct PassInfo {
    StageLayout<PipeHook> passStage;
    StageSet<PipeHook> set;
    boost::container::small_vector<Descriptor, 2> descriptors;
    PassInfo(RenderPass &pass, MaterializationContext &ctx,
             unsigned firstDescriptor);
  };
  using PassRecord = std::function<void(
      const PassInfo &, vkw::RenderPassRecorder &, const Frame &)>;
  RenderPass(Context &ctx, auto &&attachments, auto &&descriptors,
             PassRecord recorder)
      : Node(
            ctx,
            [&]() {
              boost::container::small_vector<Node::Use, 4> uses;
              unsigned passIndex = 0;
              for (auto &&[val, info] : attachments) {
                if (info->kind != ImageAttachmentUseInfo::Kind::input) {
                  info->passthrough = passIndex++;
                }
                uses.emplace_back(val, info);
              }
              std::ranges::copy(descriptors, std::back_inserter(uses));
              return uses;
            }(),
            [&]() {
              boost::container::small_vector<Node::Def, 4> defs;
              unsigned passIndex = 0;
              for (auto &&a : attachments) {
                if (a.second->kind != ImageAttachmentUseInfo::Kind::input) {
                  static_cast<ImageDefInfo *>(
                      defs.emplace_back(attachmentDef(a)).second)
                      ->passthrough = passIndex;
                }
                passIndex++;
              }
              return defs;
            }()),
        m_record(std::move(recorder)),
        m_firstDescriptor(std::ranges::size(attachments)) {}
  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const override;
  std::string_view name() const final { return "render_pass"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &ctx) final;

private:
  PassRecord m_record;
  unsigned m_firstDescriptor;
};

// Terminator nodes

class Present : public Node {
public:
  Present(Context &ctx, Value &image)
      : Node(ctx,
             std::array{Node::Use{
                 &image, new ImageUseInfo{ImageAccessInfo{
                             .accessFlags = 0,
                             .stageFlags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR}}}},
             Node::EmptyResults) {}
  const AttributesBase *getAttributes(
      Context &ctx, const Value &result,
      std::span<const AttributesBase *> useAttributes) const override {
    return nullptr;
  }
  std::string_view name() const final { return "present_image"; }
  void dumpAttributes(std::ostream &os) const final {}
  bool hasVisibleSideEffects() const final { return true; }
  bool materialize(MaterializationContext &ctx) final;
};

} // namespace imvk::graph