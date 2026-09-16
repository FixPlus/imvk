#pragma once

#include "imvk/graph/Attributes.hpp"
#include "imvk/graph/Nodes.hpp"
#include "imvk/graph/Passes.hpp"

#include <iostream>

namespace imvk::graph {
namespace {

class ImageSource final : public Node {
public:
  ImageSource(Context &ctx, std::optional<VkFormat> format,
              std::optional<VkImageType> imageType = VK_IMAGE_TYPE_2D)
      : Node(ctx, Node::EmptyUses,
             std::array{
                 Node::Def{&ctx.types().get<ImageTy>(),
                           new ImageDefInfo{ImageAccessInfo{}, "image"}}}),
        m_format(format), m_imageType(imageType) {}

  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *>) const final {
    auto format = m_format ? constant(*m_format) : dynamic<VkFormat>(result);
    auto imageType =
        m_imageType ? constant(*m_imageType) : dynamic<VkImageType>(result);
    return &ctx.attributes().get<Attributes<ImageTy>>(
        constant(extentsForType(m_imageType)), format, imageType,
        constant<size_t>(1), constant<size_t>(1));
  }
  std::optional<VerifyError> verify(Context &,
                                    const AttributesAnalysis &) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "image_source"; }
  void dumpAttributes(std::ostream &) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &) final { return false; }

private:
  static VkExtent3D extentsForType(std::optional<VkImageType> imageType) {
    if (imageType == VK_IMAGE_TYPE_1D)
      return {16, 1, 1};
    if (imageType == VK_IMAGE_TYPE_3D)
      return {16, 1, 1};
    return {16, 16, 1};
  }
  ImageSource(std::optional<VkFormat> format,
              std::optional<VkImageType> imageType)
      : m_format(format), m_imageType(imageType) {}
  std::unique_ptr<Node> doClone() const final {
    return std::unique_ptr<Node>{new ImageSource{m_format, m_imageType}};
  }
  std::optional<VkFormat> m_format;
  std::optional<VkImageType> m_imageType;
};

class Sink final : public Node {
public:
  Sink(Context &ctx, Value &image, const FormatConstraintInfo &constraint,
       std::optional<VkImageViewType> viewType = std::nullopt)
      : Node(ctx, std::array{Node::Use{&image, makeInfo(constraint, viewType)}},
             Node::EmptyResults) {}

  const AttributesBase *
  getAttributes(Context &, const Value &,
                std::span<const AttributesBase *>) const final {
    return nullptr;
  }
  std::optional<VerifyError> verify(Context &,
                                    const AttributesAnalysis &) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "sink"; }
  void dumpAttributes(std::ostream &) const final {}
  bool hasVisibleSideEffects() const final { return true; }
  bool materialize(MaterializationContext &) final { return false; }

private:
  Sink() = default;
  static ImageUseInfo *makeInfo(const FormatConstraintInfo &constraint,
                                std::optional<VkImageViewType> viewType) {
    auto *info = new ImageUseInfo{};
    info->formatConstraint = constraint;
    info->viewTypeConstraint = viewType;
    return info;
  }
  std::unique_ptr<Node> doClone() const final {
    return std::unique_ptr<Node>{new Sink{}};
  }
};

inline FormatConstraintInfo
constraint(unsigned bits, FormatConstraintInfo::NumericFormat numeric) {
  FormatConstraintInfo result;
  result.addChannelConstraint(FormatConstraintInfo::Channel::R, bits, numeric);
  return result;
}

inline size_t conversionCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<ConvertFormat>(&node); });
}

inline size_t imageTypeConversionCount(Workflow &workflow) {
  return std::ranges::count_if(workflow, [](Node &node) {
    auto *resize = dyn_cast<ResizeImage>(&node);
    return resize && resize->getImageType();
  });
}

inline size_t resizeCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<ResizeImage>(&node); });
}

inline size_t combinedConversionCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<ConvertResizeImage>(&node); });
}

inline size_t makeImageCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<MakeImage>(&node); });
}

inline MakeImage *createImage(WorkflowBuilder &builder, VkExtent3D extents,
                              VkFormat format, size_t layers = 1,
                              size_t levels = 1) {
  auto &extentsValue =
      builder.create<Constant<ExtentsTy>>(extents)->results().front();
  auto &formatValue =
      builder.create<Constant<FormatTy>>(format)->results().front();
  auto &layersValue =
      builder.create<Constant<IntegerScalarTy>>(layers)->results().front();
  auto &levelsValue =
      builder.create<Constant<IntegerScalarTy>>(levels)->results().front();
  return builder.create<MakeImage>(extentsValue, formatValue, layersValue,
                                   levelsValue);
}

inline size_t assumptionCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<AssumeCompatibleFormat>(&node); });
}

inline size_t extentsAssumptionCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<AssumeCompatibleExtents>(&node); });
}

inline bool check(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

} // namespace
} // namespace imvk::graph
