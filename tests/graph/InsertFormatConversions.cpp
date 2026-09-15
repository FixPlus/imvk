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

FormatConstraintInfo constraint(unsigned bits,
                                FormatConstraintInfo::NumericFormat numeric) {
  FormatConstraintInfo result;
  result.addChannelConstraint(FormatConstraintInfo::Channel::R, bits, numeric);
  return result;
}

size_t conversionCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<ConvertFormat>(&node); });
}

size_t imageTypeConversionCount(Workflow &workflow) {
  return std::ranges::count_if(workflow, [](Node &node) {
    auto *resize = dyn_cast<ResizeImage>(&node);
    return resize && resize->getImageType();
  });
}

size_t resizeCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<ResizeImage>(&node); });
}

size_t combinedConversionCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<ConvertResizeImage>(&node); });
}

size_t makeImageCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<MakeImage>(&node); });
}

MakeImage *createImage(WorkflowBuilder &builder, VkExtent3D extents,
                       VkFormat format, size_t layers = 1, size_t levels = 1) {
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

size_t assumptionCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<AssumeCompatibleFormat>(&node); });
}

size_t extentsAssumptionCount(Workflow &workflow) {
  return std::ranges::count_if(
      workflow, [](Node &node) { return isa<AssumeCompatibleExtents>(&node); });
}

bool check(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

bool testCompatibleConstant() {
  using NumericFormat = FormatConstraintInfo::NumericFormat;
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image = builder.create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
                    ->results()
                    .front();
  builder.create<Sink>(image, constraint(8, NumericFormat::UNORM));

  return check(!InsertFormatConversionsPass{}.run(workflow),
               "compatible constant format changed the workflow") &&
         check(conversionCount(workflow) == 0,
               "compatible constant format inserted a conversion");
}

bool testSharedConversionAndIdempotence() {
  using NumericFormat = FormatConstraintInfo::NumericFormat;
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image = builder.create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
                    ->results()
                    .front();
  auto *first =
      builder.create<Sink>(image, constraint(16, NumericFormat::UNORM));
  auto *second =
      builder.create<Sink>(image, constraint(16, NumericFormat::UNORM));

  const bool changed = InsertFormatConversionsPass{}.run(workflow);
  const auto *sharedValue = &first->uses().front().value();
  return check(changed, "incompatible constant format was not converted") &&
         check(conversionCount(workflow) == 1,
               "identical constraints did not share one conversion") &&
         check(sharedValue == &second->uses().front().value(),
               "identical constraints use different converted values") &&
         check(!InsertFormatConversionsPass{}.run(workflow),
               "second pass run changed an already legal workflow") &&
         check(conversionCount(workflow) == 1,
               "second pass run inserted another conversion");
}

bool testDynamicFormatIsConverted() {
  using NumericFormat = FormatConstraintInfo::NumericFormat;
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image = builder.create<ImageSource>(std::nullopt)->results().front();
  builder.create<Sink>(image, constraint(8, NumericFormat::UNORM));

  return check(InsertFormatConversionsPass{}.run(workflow),
               "dynamic source format was assumed compatible") &&
         check(conversionCount(workflow) == 1,
               "dynamic source format did not get one conversion");
}

bool testIncompatibleGroups() {
  using NumericFormat = FormatConstraintInfo::NumericFormat;
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image = builder.create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
                    ->results()
                    .front();
  builder.create<Sink>(image, constraint(16, NumericFormat::UNORM));
  builder.create<Sink>(image, constraint(32, NumericFormat::SFLOAT));

  return check(InsertFormatConversionsPass{}.run(workflow),
               "incompatible constraint groups did not change the workflow") &&
         check(conversionCount(workflow) == 2,
               "incompatible constraint groups did not get two conversions");
}

bool testAssumeCompatibleFormat() {
  using NumericFormat = FormatConstraintInfo::NumericFormat;
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image = builder.create<ImageSource>(std::nullopt)->results().front();
  auto *assumption = builder.create<AssumeCompatibleFormat>(image);
  auto &assumedImage = assumption->results().front();
  auto *sink =
      builder.create<Sink>(assumedImage, constraint(32, NumericFormat::SFLOAT));

  AttributesAnalysis attributes{workflow};
  const auto &inputAttributes = attributes.getAttributesFor(image);
  const auto &outputAttributes = attributes.getAttributesFor(assumedImage);

  return check(&inputAttributes == &outputAttributes,
               "assumption did not preserve image attributes") &&
         check(!InsertFormatConversionsPass{}.run(workflow),
               "assumed-compatible image inserted a conversion") &&
         check(conversionCount(workflow) == 0,
               "assumed-compatible image has a conversion") &&
         check(RemoveAssumeCompatibleFormatsPass{}.run(workflow),
               "assumption lowering did not change the workflow") &&
         check(assumptionCount(workflow) == 0,
               "assumption lowering did not erase the marker") &&
         check(&sink->uses().front().value() == &image,
               "assumption lowering did not restore the original image") &&
         check(!RemoveAssumeCompatibleFormatsPass{}.run(workflow),
               "second assumption lowering changed the workflow");
}

bool testAssumeCompatibleExtents() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM},
                               VK_IMAGE_TYPE_1D)
          ->results()
          .front();
  auto *assumption = builder.create<AssumeCompatibleExtents>(image);
  auto &assumedImage = assumption->results().front();
  auto *sink = builder.create<Sink>(assumedImage, FormatConstraintInfo{},
                                    VK_IMAGE_VIEW_TYPE_2D);

  AttributesAnalysis attributes{workflow};
  const auto &inputAttributes = attributes.getAttributesFor(image);
  const auto &outputAttributes = attributes.getAttributesFor(assumedImage);
  Workflow cloned{workflow};

  return check(&inputAttributes == &outputAttributes,
               "extents assumption did not preserve image attributes") &&
         check(std::ranges::any_of(cloned,
                                   [](const Node &node) {
                                     return isa<AssumeCompatibleExtents>(&node);
                                   }),
               "extents assumption was not cloned") &&
         check(!InsertImageTypeConversionsPass{}.run(workflow),
               "assumed-compatible extents inserted a resize") &&
         check(imageTypeConversionCount(workflow) == 0,
               "assumed-compatible extents have a resize") &&
         check(RemoveAssumeCompatibleExtentsPass{}.run(workflow),
               "extents assumption lowering did not change the workflow") &&
         check(extentsAssumptionCount(workflow) == 0,
               "extents assumption lowering did not erase the marker") &&
         check(
             &sink->uses().front().value() == &image,
             "extents assumption lowering did not restore the source image") &&
         check(!RemoveAssumeCompatibleExtentsPass{}.run(workflow),
               "second extents assumption lowering changed the workflow");
}

bool testAssumeCompatibleExtentsThroughFormatConversion() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image = builder
                    .create<ImageSource>(std::optional{VK_FORMAT_D32_SFLOAT},
                                         VK_IMAGE_TYPE_1D)
                    ->results()
                    .front();
  auto &assumedImage =
      builder.create<AssumeCompatibleExtents>(image)->results().front();
  builder.create<Present>(assumedImage);

  return check(
             InsertFormatConversionsPass{}.run(workflow),
             "format conversion was not inserted through extents assumption") &&
         check(!InsertImageTypeConversionsPass{}.run(workflow),
               "format conversion hid the extents assumption") &&
         check(conversionCount(workflow) == 1,
               "extents assumption suppressed format conversion") &&
         check(imageTypeConversionCount(workflow) == 0,
               "extents assumption inserted a resize after format conversion");
}

bool testNestedCompatibilityAssumptions() {
  using NumericFormat = FormatConstraintInfo::NumericFormat;
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image = builder.create<ImageSource>(std::nullopt, VK_IMAGE_TYPE_1D)
                    ->results()
                    .front();
  auto &formatAssumed =
      builder.create<AssumeCompatibleFormat>(image)->results().front();
  auto &extentsAssumed =
      builder.create<AssumeCompatibleExtents>(formatAssumed)->results().front();
  builder.create<Sink>(extentsAssumed, constraint(32, NumericFormat::SFLOAT),
                       VK_IMAGE_VIEW_TYPE_2D);

  return check(!InsertFormatConversionsPass{}.run(workflow),
               "extents assumption hid a nested format assumption") &&
         check(!InsertImageTypeConversionsPass{}.run(workflow),
               "format assumption hid a nested extents assumption") &&
         check(conversionCount(workflow) == 0,
               "nested assumptions inserted a format conversion") &&
         check(imageTypeConversionCount(workflow) == 0,
               "nested assumptions inserted a resize");
}

bool testScreenExtentsAttributes() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto *screenExtents = builder.create<ScreenExtents>();
  auto &result = screenExtents->results().front();
  AttributesAnalysis attributes{workflow};
  const auto &resultAttributes =
      attributes.getAttributesFor<Attributes<ExtentsTy>>(result);

  return check(screenExtents->uses().empty(),
               "screen extents unexpectedly has inputs") &&
         check(screenExtents->results().size() == 1,
               "screen extents does not have one result") &&
         check(isa<ExtentsTy>(&result.type()),
               "screen extents result has the wrong type") &&
         check(resultAttributes.extents.status() ==
                   Attribute<VkExtent3D>::Status::dynamic,
               "screen extents attribute is not dynamic") &&
         check(resultAttributes.extents.getDynamicValue() == &result,
               "screen extents dynamic attribute references another value") &&
         check(resultAttributes.imageType.getConstant() == VK_IMAGE_TYPE_2D,
               "screen extents do not carry a 2D image type hint");
}

bool testScreenImageDoesNotNeedTypeConversion() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &extents = builder.create<ScreenExtents>()->results().front();
  auto &format = builder.create<Constant<FormatTy>>(VK_FORMAT_R8G8B8A8_UNORM)
                     ->results()
                     .front();
  auto &one = builder.create<Constant<IntegerScalarTy>>(1)->results().front();
  auto &image =
      builder.create<MakeImage>(extents, format, one, one)->results().front();
  Scene scene;
  scene.attachments.emplace_back(ImageAttachmentUseInfo::Kind::color,
                                 ImageAttachmentUseInfo::LoadOp::clear);
  auto &rendered =
      builder.create<RenderPass>(std::array{&image}, Node::EmptyValues, scene)
          ->results()
          .front();
  builder.create<Present>(rendered);

  const AttributesAnalysis attributes{workflow};
  const bool converted = InsertImageTypeConversionsPass{}.run(workflow);
  auto chains = materializeImageValueChains(workflow);
  const auto presentedChain =
      std::ranges::find_if(chains, [&](const auto &chain) {
        return chain.chain.front().def == &image;
      });
  return check(attributes.getAttributesFor<Attributes<ImageTy>>(rendered)
                       .imageType.getConstant() == VK_IMAGE_TYPE_2D,
               "screen render pass did not preserve the 2D extent hint") &&
         check(!converted,
               "screen render pass inserted a spurious type conversion") &&
         check(imageTypeConversionCount(workflow) == 0,
               "screen render pass has a type conversion") &&
         check(presentedChain != chains.end() && presentedChain->presented,
               "screen render pass was detached from the presented chain");
}

bool testPresentFormatConstraint() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image =
      builder.create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();
  auto *present = builder.create<Present>(image);
  const auto *info =
      dyn_cast<const ImageUseInfo>(present->uses().front().info());
  Workflow cloned{workflow};
  const auto clonedPresent = std::ranges::find_if(
      cloned, [](const Node &node) { return isa<Present>(&node); });
  const auto *clonedInfo =
      clonedPresent == cloned.end()
          ? nullptr
          : dyn_cast<const ImageUseInfo>(clonedPresent->uses().front().info());

  return check(info, "present image use has no image information") &&
         check(info->formatConstraint.isCompatible(VK_FORMAT_R8G8B8A8_UNORM),
               "present rejects R8G8B8A8 UNORM") &&
         check(info->formatConstraint.isCompatible(VK_FORMAT_B8G8R8A8_UNORM),
               "present rejects B8G8R8A8 UNORM") &&
         check(!info->formatConstraint.isCompatible(VK_FORMAT_R8G8B8A8_UINT),
               "present accepts an integer color format") &&
         check(!info->formatConstraint.isCompatible(VK_FORMAT_R8G8B8_UNORM),
               "present accepts a format without alpha") &&
         check(!info->formatConstraint.isCompatible(VK_FORMAT_D32_SFLOAT),
               "present accepts a depth format") &&
         check(info->viewTypeConstraint == VK_IMAGE_VIEW_TYPE_2D,
               "present does not require a 2D image view") &&
         check(clonedInfo &&
                   clonedInfo->viewTypeConstraint == VK_IMAGE_VIEW_TYPE_2D,
               "present clone lost its image view type constraint");
}

bool testPresentInsertsConversion() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image =
      builder.create<ImageSource>(std::optional{VK_FORMAT_D32_SFLOAT})
          ->results()
          .front();
  auto *present = builder.create<Present>(image);

  const bool changed = InsertFormatConversionsPass{}.run(workflow);
  auto &presentedValue = present->uses().front().value();
  const AttributesAnalysis attributes{workflow};
  const auto &presentedAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(presentedValue);
  const auto format = presentedAttributes.format.getConstant();
  const auto imageType = presentedAttributes.imageType.getConstant();
  const auto &constraint =
      static_cast<const ImageUseInfo &>(*present->uses().front().info())
          .formatConstraint;

  return check(changed, "present did not convert an incompatible format") &&
         check(conversionCount(workflow) == 1,
               "present inserted an unexpected conversion count") &&
         check(isa<ConvertFormat>(&presentedValue.node()),
               "present does not consume the converted value") &&
         check(format.has_value(),
               "present conversion format is not constant") &&
         check(format && constraint.isCompatible(*format),
               "present conversion result does not satisfy its constraint") &&
         check(imageType == VK_IMAGE_TYPE_2D,
               "format conversion changed the image type");
}

bool testCompatibleImageType() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM},
                               VK_IMAGE_TYPE_2D)
          ->results()
          .front();
  builder.create<Sink>(image, FormatConstraintInfo{}, VK_IMAGE_VIEW_TYPE_2D);

  return check(!InsertImageTypeConversionsPass{}.run(workflow),
               "compatible image type changed the workflow") &&
         check(imageTypeConversionCount(workflow) == 0,
               "compatible image type inserted a conversion");
}

bool testSharedImageTypeConversionAndIdempotence() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM},
                               VK_IMAGE_TYPE_3D)
          ->results()
          .front();
  auto *first = builder.create<Sink>(image, FormatConstraintInfo{},
                                     VK_IMAGE_VIEW_TYPE_2D);
  auto *second = builder.create<Sink>(image, FormatConstraintInfo{},
                                      VK_IMAGE_VIEW_TYPE_2D);

  const bool changed = InsertImageTypeConversionsPass{}.run(workflow);
  const auto *sharedValue = &first->uses().front().value();
  const AttributesAnalysis attributes{workflow};
  const auto &convertedAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(*sharedValue);

  return check(changed, "incompatible image type was not converted") &&
         check(imageTypeConversionCount(workflow) == 1,
               "identical view constraints did not share one conversion") &&
         check(sharedValue == &second->uses().front().value(),
               "identical view constraints use different converted values") &&
         check(isa<ResizeImage>(&sharedValue->node()),
               "image type conversion did not use resize image") &&
         check(convertedAttributes.imageType.getConstant() == VK_IMAGE_TYPE_2D,
               "image type conversion has the wrong result type") &&
         check(convertedAttributes.extents.getConstant() ==
                   std::optional{VkExtent3D{16, 1, 1}},
               "image type conversion changed image extents") &&
         check(!InsertImageTypeConversionsPass{}.run(workflow),
               "second type-conversion pass changed a legal workflow") &&
         check(imageTypeConversionCount(workflow) == 1,
               "second type-conversion pass inserted another conversion");
}

bool testIncompatibleImageTypeGroups() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM},
                               VK_IMAGE_TYPE_3D)
          ->results()
          .front();
  builder.create<Sink>(image, FormatConstraintInfo{}, VK_IMAGE_VIEW_TYPE_1D);
  builder.create<Sink>(image, FormatConstraintInfo{}, VK_IMAGE_VIEW_TYPE_2D);

  return check(InsertImageTypeConversionsPass{}.run(workflow),
               "incompatible view constraint groups did not change the "
               "workflow") &&
         check(
             imageTypeConversionCount(workflow) == 2,
             "incompatible view constraint groups did not get two conversions");
}

bool testDynamicImageTypeIsConverted() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image = builder
                    .create<ImageSource>(
                        std::optional{VK_FORMAT_R8G8B8A8_UNORM}, std::nullopt)
                    ->results()
                    .front();
  builder.create<Sink>(image, FormatConstraintInfo{}, VK_IMAGE_VIEW_TYPE_2D);

  return check(InsertImageTypeConversionsPass{}.run(workflow),
               "dynamic source image type was assumed compatible") &&
         check(imageTypeConversionCount(workflow) == 1,
               "dynamic source image type did not get one conversion");
}

bool testPresentInsertsImageTypeConversion() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM},
                               VK_IMAGE_TYPE_1D)
          ->results()
          .front();
  auto *present = builder.create<Present>(image);

  const bool changed = InsertImageTypeConversionsPass{}.run(workflow);
  auto &presentedValue = present->uses().front().value();
  const AttributesAnalysis attributes{workflow};
  const auto imageType =
      attributes.getAttributesFor<Attributes<ImageTy>>(presentedValue)
          .imageType.getConstant();

  return check(changed, "present did not convert an incompatible image type") &&
         check(imageTypeConversionCount(workflow) == 1,
               "present inserted an unexpected image type conversion count") &&
         check(isa<ResizeImage>(&presentedValue.node()),
               "present does not consume the image type conversion") &&
         check(imageType == VK_IMAGE_TYPE_2D,
               "present image type conversion did not produce a 2D image");
}

bool testPresentInsertsFormatAndImageTypeConversions() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image = builder
                    .create<ImageSource>(std::optional{VK_FORMAT_D32_SFLOAT},
                                         VK_IMAGE_TYPE_1D)
                    ->results()
                    .front();
  auto *present = builder.create<Present>(image);

  const bool formatChanged = InsertFormatConversionsPass{}.run(workflow);
  const bool typeChanged = InsertImageTypeConversionsPass{}.run(workflow);
  auto &presentedValue = present->uses().front().value();
  const AttributesAnalysis attributes{workflow};
  const auto &presentedAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(presentedValue);

  return check(formatChanged && typeChanged,
               "present did not insert both image conversions") &&
         check(conversionCount(workflow) == 1,
               "combined conversion inserted the wrong format conversion "
               "count") &&
         check(
             imageTypeConversionCount(workflow) == 1,
             "combined conversion inserted the wrong type conversion count") &&
         check(isa<ResizeImage>(&presentedValue.node()),
               "combined conversion does not end with resize image") &&
         check(presentedAttributes.imageType.getConstant() == VK_IMAGE_TYPE_2D,
               "combined conversion has the wrong image type") &&
         check(presentedAttributes.format.getConstant().has_value() &&
                   static_cast<const ImageUseInfo &>(
                       *present->uses().front().info())
                       .formatConstraint.isCompatible(
                           *presentedAttributes.format.getConstant()),
               "combined conversion has an incompatible format") &&
         check(!InsertFormatConversionsPass{}.run(workflow) &&
                   !InsertImageTypeConversionsPass{}.run(workflow),
               "combined conversions are not idempotent");
}

bool testPresentedImageChain() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &presentedImage =
      builder.create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();
  auto &offscreenImage =
      builder.create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();
  builder.create<Present>(presentedImage);

  auto chains = materializeImageValueChains(workflow);
  const auto presentedCount = std::ranges::count_if(
      chains, [](const auto &chain) { return chain.presented; });
  const auto offscreenChain =
      std::ranges::find_if(chains, [&](const auto &chain) {
        return chain.chain.front().def == &offscreenImage;
      });

  return check(presentedCount == 1,
               "image-chain analysis did not find one presented chain") &&
         check(offscreenChain != chains.end(),
               "image-chain analysis lost the offscreen chain") &&
         check(offscreenChain != chains.end() && !offscreenChain->presented,
               "offscreen chain was marked as presented");
}

bool testPresentVerification() {
  {
    Context ctx;
    Workflow workflow{ctx};
    WorkflowBuilder builder{workflow, workflow.end()};
    builder.create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM});
    if (!check(!verifyWorkflow(workflow),
               "headless workflow failed verification"))
      return false;
  }

  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image =
      builder.create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();
  builder.create<Present>(image);
  builder.create<Present>(image);
  auto error = verifyWorkflow(workflow);
  return check(error.has_value(),
               "workflow with multiple present nodes passed verification") &&
         check(error && isa<MultiplePresentImageError>(error->error.get()),
               "multiple present nodes produced the wrong verification error");
}

bool testUnifiedImageType() {
  Context ctx;
  const auto &first = ctx.types().get<ImageTy>();
  const auto &second = ctx.types().get<ImageTy>();
  return check(&first == &second, "image type is not canonical") &&
         check(first.name() == "image", "image type retains dimensionality");
}

bool testImageTypeInference() {
  if (!check(imageTypeForExtents({32, 1, 1}) == VK_IMAGE_TYPE_1D,
             "1D image type was not inferred from extents") ||
      !check(imageTypeForExtents({32, 16, 1}) == VK_IMAGE_TYPE_2D,
             "2D image type was not inferred from extents") ||
      !check(imageTypeForExtents({32, 16, 8}) == VK_IMAGE_TYPE_3D,
             "3D image type was not inferred from extents"))
    return false;

  VkImageCreateInfo info{};
  info.imageType = VK_IMAGE_TYPE_1D;
  info.arrayLayers = 1;
  if (!check(imageViewTypeForImage(info) == VK_IMAGE_VIEW_TYPE_1D,
             "1D image view type was not inferred"))
    return false;
  info.arrayLayers = 3;
  if (!check(imageViewTypeForImage(info) == VK_IMAGE_VIEW_TYPE_1D_ARRAY,
             "1D array image view type was not inferred"))
    return false;
  info.imageType = VK_IMAGE_TYPE_2D;
  if (!check(imageViewTypeForImage(info) == VK_IMAGE_VIEW_TYPE_2D_ARRAY,
             "2D array image view type was not inferred"))
    return false;
  info.imageType = VK_IMAGE_TYPE_3D;
  return check(imageViewTypeForImage(info) == VK_IMAGE_VIEW_TYPE_3D,
               "3D image view type was not inferred");
}

bool testImageChainTypeInference() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &format =
      builder.create<Constant<FormatTy>>(VK_FORMAT_R8_UNORM)->results().front();
  auto &one = builder.create<Constant<IntegerScalarTy>>(1)->results().front();

  std::array extents{VkExtent3D{32, 1, 1}, VkExtent3D{32, 16, 1},
                     VkExtent3D{32, 16, 8}};
  std::array expectedTypes{VK_IMAGE_TYPE_1D, VK_IMAGE_TYPE_2D,
                           VK_IMAGE_TYPE_3D};
  std::array<Value *, 3> images{};
  for (auto &&[index, extent] : extents | std::views::enumerate) {
    auto &extentValue =
        builder.create<Constant<ExtentsTy>>(extent)->results().front();
    images[index] = &builder.create<MakeImage>(extentValue, format, one, one)
                         ->results()
                         .front();
  }

  auto chains = materializeImageValueChains(workflow);
  for (auto &&[image, expected] : std::views::zip(images, expectedTypes)) {
    const auto found = std::ranges::find_if(chains, [&](const auto &chain) {
      return chain.chain.front().def == image;
    });
    if (!check(found != chains.end(), "image chain was not materialized") ||
        !check(found->imageInfo.imageType == expected,
               "image chain has an incorrectly inferred image type"))
      return false;
  }
  return true;
}

bool testResizeImageAttributes() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &source =
      builder.create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();
  auto &extents = builder.create<Constant<ExtentsTy>>(VkExtent3D{64, 32, 1})
                      ->results()
                      .front();
  auto *resize = builder.create<ResizeImage>(source, extents);
  auto &resized = resize->results().front();

  AttributesAnalysis attributes{workflow};
  const auto &sourceAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(source);
  const auto &resizedAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(resized);
  const auto *sourceUse =
      dyn_cast<const ImageUseInfo>(resize->uses().front().info());
  const auto *definition =
      dyn_cast<const ImageDefInfo>(&resize->results().front().info());

  return check(resize->uses().size() == 2,
               "resize image does not have two inputs") &&
         check(resize->results().size() == 1,
               "resize image does not have one result") &&
         check(sourceUse && sourceUse->access.layout ==
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               "resize image source has the wrong layout") &&
         check(sourceUse &&
                   sourceUse->access.usage == VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
               "resize image source has the wrong usage") &&
         check(definition && definition->access.layout ==
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               "resize image result has the wrong layout") &&
         check(definition &&
                   definition->access.usage == VK_IMAGE_USAGE_TRANSFER_DST_BIT,
               "resize image result has the wrong usage") &&
         check(resizedAttributes.extents.getConstant() ==
                   std::optional{VkExtent3D{64, 32, 1}},
               "resize image did not replace the extents") &&
         check(resizedAttributes.format == sourceAttributes.format,
               "resize image changed the format") &&
         check(resizedAttributes.imageType.getConstant() == VK_IMAGE_TYPE_2D,
               "resize image did not infer the target image type") &&
         check(resizedAttributes.layers == sourceAttributes.layers,
               "resize image changed the layer count") &&
         check(resizedAttributes.levels == sourceAttributes.levels,
               "resize image changed the mip count") &&
         check(!InsertFormatConversionsPass{}.run(workflow),
               "resize image triggered a format conversion");
}

bool testResizeImageChain() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &source =
      builder.create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();
  auto &extents = builder.create<Constant<ExtentsTy>>(VkExtent3D{16, 8, 4})
                      ->results()
                      .front();
  auto &resized =
      builder.create<ResizeImage>(source, extents)->results().front();

  auto chains = materializeImageValueChains(workflow);
  const auto resizedChain =
      std::ranges::find_if(chains, [&](const auto &chain) {
        return chain.chain.front().def == &resized;
      });

  return check(resizedChain != chains.end(),
               "resize image did not start an image chain") &&
         check(resizedChain->imageInfo.extent == VkExtent3D{16, 8, 4},
               "resize image chain has the wrong extents") &&
         check(resizedChain->imageInfo.imageType == VK_IMAGE_TYPE_3D,
               "resize image chain has the wrong inferred image type") &&
         check(resizedChain->imageInfo.format == VK_FORMAT_R8G8B8A8_UNORM,
               "resize image chain changed the format");
}

bool testExplicitResizeImageType() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &source =
      builder.create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();
  auto &extents = builder.create<Constant<ExtentsTy>>(VkExtent3D{1, 1, 1})
                      ->results()
                      .front();
  auto *resize = builder.create<ResizeImage>(source, extents, VK_IMAGE_TYPE_2D);

  Workflow cloned{workflow};
  const auto clonedResize = std::ranges::find_if(
      cloned, [](const Node &node) { return isa<ResizeImage>(&node); });

  auto chains = materializeImageValueChains(workflow);
  const auto resizedChain =
      std::ranges::find_if(chains, [&](const auto &chain) {
        return chain.chain.front().def == &resize->results().front();
      });

  return check(resize->getImageType() == VK_IMAGE_TYPE_2D,
               "resize image did not retain the explicit image type") &&
         check(AttributesAnalysis{workflow}
                       .getAttributesFor<Attributes<ImageTy>>(
                           resize->results().front())
                       .imageType.getConstant() == VK_IMAGE_TYPE_2D,
               "resize image attributes ignored the explicit image type") &&
         check(
             clonedResize != cloned.end() &&
                 dyn_cast<const ResizeImage>(&*clonedResize)->getImageType() ==
                     VK_IMAGE_TYPE_2D,
             "resize image clone did not retain the explicit image type") &&
         check(resizedChain != chains.end(),
               "explicitly typed resize image did not start an image chain") &&
         check(resizedChain->imageInfo.imageType == VK_IMAGE_TYPE_2D,
               "resize image chain ignored the explicit image type") &&
         check(resizedChain->chain.front().viewInfo.viewType ==
                   VK_IMAGE_VIEW_TYPE_2D,
               "resize image chain has the wrong image view type");
}

bool testConvertResizeImageAttributes() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &source = builder.create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
                     ->results()
                     .front();
  auto &extents = builder.create<Constant<ExtentsTy>>(VkExtent3D{64, 32, 1})
                      ->results()
                      .front();
  auto &format = builder.create<Constant<FormatTy>>(VK_FORMAT_R8G8B8A8_UNORM)
                     ->results()
                     .front();
  auto *convertResize =
      builder.create<ConvertResizeImage>(source, extents, format);
  auto &convertedResized = convertResize->results().front();
  builder.create<Present>(convertedResized);

  AttributesAnalysis attributes{workflow};
  const auto &sourceAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(source);
  const auto &resultAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(convertedResized);
  const auto *sourceUse =
      dyn_cast<const ImageUseInfo>(convertResize->uses().front().info());
  const auto *definition =
      dyn_cast<const ImageDefInfo>(&convertResize->results().front().info());

  return check(convertResize->uses().size() == 3,
               "convert resize image does not have three inputs") &&
         check(convertResize->results().size() == 1,
               "convert resize image does not have one result") &&
         check(sourceUse && sourceUse->access.layout ==
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               "convert resize image source has the wrong layout") &&
         check(sourceUse &&
                   sourceUse->access.usage == VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
               "convert resize image source has the wrong usage") &&
         check(definition && definition->access.layout ==
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               "convert resize image result has the wrong layout") &&
         check(definition &&
                   definition->access.usage == VK_IMAGE_USAGE_TRANSFER_DST_BIT,
               "convert resize image result has the wrong usage") &&
         check(resultAttributes.extents.getConstant() ==
                   std::optional{VkExtent3D{64, 32, 1}},
               "convert resize image did not replace the extents") &&
         check(resultAttributes.format.getConstant() ==
                   VK_FORMAT_R8G8B8A8_UNORM,
               "convert resize image did not replace the format") &&
         check(resultAttributes.imageType.getConstant() == VK_IMAGE_TYPE_2D,
               "convert resize image did not infer the target image type") &&
         check(resultAttributes.layers == sourceAttributes.layers,
               "convert resize image changed the layer count") &&
         check(resultAttributes.levels == sourceAttributes.levels,
               "convert resize image changed the mip count") &&
         check(!InsertFormatConversionsPass{}.run(workflow),
               "convert resize image triggered a format conversion") &&
         check(!InsertImageTypeConversionsPass{}.run(workflow),
               "convert resize image triggered an image type conversion");
}

bool testExplicitConvertResizeImageTypeAndChain() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &source = builder.create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
                     ->results()
                     .front();
  auto &extents = builder.create<Constant<ExtentsTy>>(VkExtent3D{1, 1, 1})
                      ->results()
                      .front();
  auto &format = builder.create<Constant<FormatTy>>(VK_FORMAT_R16_SFLOAT)
                     ->results()
                     .front();
  auto *convertResize = builder.create<ConvertResizeImage>(
      source, extents, format, VK_IMAGE_TYPE_2D);
  auto &convertedResized = convertResize->results().front();

  Workflow cloned{workflow};
  const auto clonedConvertResize = std::ranges::find_if(
      cloned, [](const Node &node) { return isa<ConvertResizeImage>(&node); });
  auto chains = materializeImageValueChains(workflow);
  const auto resultChain = std::ranges::find_if(chains, [&](const auto &chain) {
    return chain.chain.front().def == &convertedResized;
  });

  return check(convertResize->getImageType() == VK_IMAGE_TYPE_2D,
               "convert resize image did not retain the explicit image type") &&
         check(clonedConvertResize != cloned.end() &&
                   dyn_cast<const ConvertResizeImage>(&*clonedConvertResize)
                           ->getImageType() == VK_IMAGE_TYPE_2D,
               "convert resize image clone lost the explicit image type") &&
         check(resultChain != chains.end(),
               "convert resize image did not start an image chain") &&
         check(resultChain->imageInfo.extent == VkExtent3D{1, 1, 1},
               "convert resize image chain has the wrong extents") &&
         check(resultChain->imageInfo.format == VK_FORMAT_R16_SFLOAT,
               "convert resize image chain has the wrong format") &&
         check(resultChain->imageInfo.imageType == VK_IMAGE_TYPE_2D,
               "convert resize image chain ignored the explicit image type") &&
         check(resultChain->chain.front().viewInfo.viewType ==
                   VK_IMAGE_VIEW_TYPE_2D,
               "convert resize image chain has the wrong image view type");
}

bool testCombineResizeThenFormatConversion() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &source = builder.create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
                     ->results()
                     .front();
  auto &extents = builder.create<Constant<ExtentsTy>>(VkExtent3D{32, 8, 1})
                      ->results()
                      .front();
  auto &format = builder.create<Constant<FormatTy>>(VK_FORMAT_R16_SFLOAT)
                     ->results()
                     .front();
  auto &resized =
      builder.create<ResizeImage>(source, extents, VK_IMAGE_TYPE_2D)
          ->results()
          .front();
  auto &converted =
      builder.create<ConvertFormat>(resized, format)->results().front();
  auto *sink = builder.create<Sink>(converted, FormatConstraintInfo{});

  const bool changed = CombineImageConversionsPass{}.run(workflow);
  auto &combinedValue = sink->uses().front().value();
  const auto *combined = dyn_cast<ConvertResizeImage>(&combinedValue.node());
  const AttributesAnalysis attributes{workflow};
  const auto &combinedAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(combinedValue);

  return check(changed, "resize then format conversion was not combined") &&
         check(resizeCount(workflow) == 0 && conversionCount(workflow) == 0,
               "combined resize then format left original conversion nodes") &&
         check(combinedConversionCount(workflow) == 1,
               "resize then format produced the wrong combined node count") &&
         check(combined && combined->getImageType() == VK_IMAGE_TYPE_2D,
               "combined node lost the resize image type") &&
         check(combinedAttributes.extents.getConstant() ==
                   std::optional{VkExtent3D{32, 8, 1}},
               "combined node has the wrong extents") &&
         check(combinedAttributes.format.getConstant() == VK_FORMAT_R16_SFLOAT,
               "combined node has the wrong format") &&
         check(!CombineImageConversionsPass{}.run(workflow),
               "second combine pass changed an optimized workflow");
}

bool testCombineFormatThenResizeConversion() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &source = builder.create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
                     ->results()
                     .front();
  auto &format = builder.create<Constant<FormatTy>>(VK_FORMAT_R16_SFLOAT)
                     ->results()
                     .front();
  auto &extents = builder.create<Constant<ExtentsTy>>(VkExtent3D{24, 12, 1})
                      ->results()
                      .front();
  auto &converted =
      builder.create<ConvertFormat>(source, format)->results().front();
  auto &resized =
      builder.create<ResizeImage>(converted, extents)->results().front();
  auto *sink = builder.create<Sink>(resized, FormatConstraintInfo{});

  const bool changed = CombineImageConversionsPass{}.run(workflow);
  auto &combinedValue = sink->uses().front().value();
  const auto &combinedAttributes =
      AttributesAnalysis{workflow}.getAttributesFor<Attributes<ImageTy>>(
          combinedValue);

  return check(changed, "format then resize conversion was not combined") &&
         check(isa<ConvertResizeImage>(&combinedValue.node()),
               "format then resize did not produce a combined node") &&
         check(resizeCount(workflow) == 0 && conversionCount(workflow) == 0 &&
                   combinedConversionCount(workflow) == 1,
               "format then resize left an incorrect conversion graph") &&
         check(combinedAttributes.extents.getConstant() ==
                   std::optional{VkExtent3D{24, 12, 1}},
               "format then resize combination has the wrong extents") &&
         check(combinedAttributes.format.getConstant() == VK_FORMAT_R16_SFLOAT,
               "format then resize combination has the wrong format");
}

bool testCombineAutomaticImageConversions() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &source = builder
                     .create<ImageSource>(std::optional{VK_FORMAT_D32_SFLOAT},
                                          VK_IMAGE_TYPE_1D)
                     ->results()
                     .front();
  auto *present = builder.create<Present>(source);

  const bool formatChanged = InsertFormatConversionsPass{}.run(workflow);
  const bool typeChanged = InsertImageTypeConversionsPass{}.run(workflow);
  const bool combined = CombineImageConversionsPass{}.run(workflow);
  auto &presented = present->uses().front().value();
  const auto &attributes =
      AttributesAnalysis{workflow}.getAttributesFor<Attributes<ImageTy>>(
          presented);
  const auto *combinedNode = dyn_cast<ConvertResizeImage>(&presented.node());
  const auto &presentConstraint =
      static_cast<const ImageUseInfo &>(*present->uses().front().info());

  return check(formatChanged && typeChanged && combined,
               "automatic format and type conversions were not combined") &&
         check(combinedNode && combinedNode->getImageType() == VK_IMAGE_TYPE_2D,
               "automatic combined conversion has the wrong image type") &&
         check(conversionCount(workflow) == 0 && resizeCount(workflow) == 0 &&
                   combinedConversionCount(workflow) == 1,
               "automatic combination left separate conversion nodes") &&
         check(attributes.extents.getConstant() ==
                   std::optional{VkExtent3D{16, 1, 1}},
               "automatic combination changed image extents") &&
         check(attributes.format.getConstant().has_value() &&
                   presentConstraint.formatConstraint.isCompatible(
                       *attributes.format.getConstant()),
               "automatic combination produced an incompatible format") &&
         check(attributes.imageType.getConstant() == VK_IMAGE_TYPE_2D,
               "automatic combination produced an incompatible image type") &&
         check(!CombineImageConversionsPass{}.run(workflow),
               "automatic combination is not idempotent");
}

bool testSharedConversionIsNotCombined() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &source = builder.create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
                     ->results()
                     .front();
  auto &extents = builder.create<Constant<ExtentsTy>>(VkExtent3D{32, 8, 1})
                      ->results()
                      .front();
  auto &format = builder.create<Constant<FormatTy>>(VK_FORMAT_R16_SFLOAT)
                     ->results()
                     .front();
  auto &resized =
      builder.create<ResizeImage>(source, extents)->results().front();
  auto &converted =
      builder.create<ConvertFormat>(resized, format)->results().front();
  builder.create<Sink>(resized, FormatConstraintInfo{});
  builder.create<Sink>(converted, FormatConstraintInfo{});

  return check(!CombineImageConversionsPass{}.run(workflow),
               "shared resize result was incorrectly combined") &&
         check(resizeCount(workflow) == 1 && conversionCount(workflow) == 1 &&
                   combinedConversionCount(workflow) == 0,
               "shared conversion graph was modified");
}

bool testSharedFormatConversionIsNotCombined() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &source = builder.create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
                     ->results()
                     .front();
  auto &format = builder.create<Constant<FormatTy>>(VK_FORMAT_R16_SFLOAT)
                     ->results()
                     .front();
  auto &extents = builder.create<Constant<ExtentsTy>>(VkExtent3D{32, 8, 1})
                      ->results()
                      .front();
  auto &converted =
      builder.create<ConvertFormat>(source, format)->results().front();
  auto &resized =
      builder.create<ResizeImage>(converted, extents)->results().front();
  builder.create<Sink>(converted, FormatConstraintInfo{});
  builder.create<Sink>(resized, FormatConstraintInfo{});

  return check(!CombineImageConversionsPass{}.run(workflow),
               "shared format conversion was incorrectly combined") &&
         check(resizeCount(workflow) == 1 && conversionCount(workflow) == 1 &&
                   combinedConversionCount(workflow) == 0,
               "shared format conversion graph was modified");
}

bool testFoldMakeImageFormatConversion() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto *source = createImage(builder, {16, 8, 1}, VK_FORMAT_R8_UNORM, 3, 2);
  auto &targetFormat = builder.create<Constant<FormatTy>>(VK_FORMAT_R16_SFLOAT)
                           ->results()
                           .front();
  auto &converted =
      builder.create<ConvertFormat>(source->results().front(), targetFormat)
          ->results()
          .front();
  auto *sink = builder.create<Sink>(converted, FormatConstraintInfo{});

  const bool changed = CombineImageConversionsPass{}.run(workflow);
  auto &replacement = sink->uses().front().value();
  const auto &attributes =
      AttributesAnalysis{workflow}.getAttributesFor<Attributes<ImageTy>>(
          replacement);

  return check(changed, "make image format conversion was not folded") &&
         check(isa<MakeImage>(&replacement.node()),
               "format conversion was not replaced by make image") &&
         check(makeImageCount(workflow) == 1 && conversionCount(workflow) == 0,
               "format folding left the wrong producer graph") &&
         check(attributes.extents.getConstant() ==
                   std::optional{VkExtent3D{16, 8, 1}},
               "format folding changed image extents") &&
         check(attributes.format.getConstant() == VK_FORMAT_R16_SFLOAT,
               "format folding did not substitute image format") &&
         check(attributes.layers.getConstant() == 3 &&
                   attributes.levels.getConstant() == 2,
               "format folding changed layers or mip levels") &&
         check(!CombineImageConversionsPass{}.run(workflow),
               "format folding is not idempotent");
}

bool testFoldMakeImageResize() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto *source = createImage(builder, {16, 8, 1}, VK_FORMAT_R8_UNORM, 2, 3);
  auto &targetExtents =
      builder.create<Constant<ExtentsTy>>(VkExtent3D{32, 4, 1})
          ->results()
          .front();
  auto &resized =
      builder.create<ResizeImage>(source->results().front(), targetExtents)
          ->results()
          .front();
  auto *sink = builder.create<Sink>(resized, FormatConstraintInfo{});

  const bool changed = CombineImageConversionsPass{}.run(workflow);
  auto &replacement = sink->uses().front().value();
  const auto &attributes =
      AttributesAnalysis{workflow}.getAttributesFor<Attributes<ImageTy>>(
          replacement);

  return check(changed, "make image resize was not folded") &&
         check(isa<MakeImage>(&replacement.node()),
               "resize was not replaced by make image") &&
         check(makeImageCount(workflow) == 1 && resizeCount(workflow) == 0,
               "resize folding left the wrong producer graph") &&
         check(attributes.extents.getConstant() ==
                   std::optional{VkExtent3D{32, 4, 1}},
               "resize folding did not substitute image extents") &&
         check(attributes.format.getConstant() == VK_FORMAT_R8_UNORM,
               "resize folding changed image format") &&
         check(attributes.layers.getConstant() == 2 &&
                   attributes.levels.getConstant() == 3,
               "resize folding changed layers or mip levels");
}

bool testFoldMakeImageConvertResize() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto *source = createImage(builder, {16, 8, 1}, VK_FORMAT_R8_UNORM, 2, 2);
  auto &targetExtents =
      builder.create<Constant<ExtentsTy>>(VkExtent3D{8, 4, 1})
          ->results()
          .front();
  auto &targetFormat = builder.create<Constant<FormatTy>>(VK_FORMAT_R16_SFLOAT)
                           ->results()
                           .front();
  auto &convertedResized =
      builder
          .create<ConvertResizeImage>(source->results().front(), targetExtents,
                                      targetFormat)
          ->results()
          .front();
  auto *sink = builder.create<Sink>(convertedResized, FormatConstraintInfo{});

  const bool changed = CombineImageConversionsPass{}.run(workflow);
  auto &replacement = sink->uses().front().value();
  const auto &attributes =
      AttributesAnalysis{workflow}.getAttributesFor<Attributes<ImageTy>>(
          replacement);

  return check(changed, "make image convert resize was not folded") &&
         check(isa<MakeImage>(&replacement.node()),
               "convert resize was not replaced by make image") &&
         check(makeImageCount(workflow) == 1 &&
                   combinedConversionCount(workflow) == 0,
               "convert resize folding left the wrong producer graph") &&
         check(attributes.extents.getConstant() ==
                   std::optional{VkExtent3D{8, 4, 1}},
               "convert resize folding did not substitute extents") &&
         check(attributes.format.getConstant() == VK_FORMAT_R16_SFLOAT,
               "convert resize folding did not substitute format") &&
         check(attributes.layers.getConstant() == 2 &&
                   attributes.levels.getConstant() == 2,
               "convert resize folding changed layers or mip levels");
}

bool testFoldMakeImageConversionChain() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto *source = createImage(builder, {16, 8, 1}, VK_FORMAT_R8_UNORM);
  auto &targetExtents =
      builder.create<Constant<ExtentsTy>>(VkExtent3D{8, 4, 1})
          ->results()
          .front();
  auto &targetFormat = builder.create<Constant<FormatTy>>(VK_FORMAT_R16_SFLOAT)
                           ->results()
                           .front();
  auto &resized =
      builder.create<ResizeImage>(source->results().front(), targetExtents)
          ->results()
          .front();
  auto &converted =
      builder.create<ConvertFormat>(resized, targetFormat)->results().front();
  auto *sink = builder.create<Sink>(converted, FormatConstraintInfo{});

  const bool changed = CombineImageConversionsPass{}.run(workflow);
  auto &replacement = sink->uses().front().value();
  const auto &attributes =
      AttributesAnalysis{workflow}.getAttributesFor<Attributes<ImageTy>>(
          replacement);

  return check(changed, "make image conversion chain was not folded") &&
         check(isa<MakeImage>(&replacement.node()),
               "conversion chain was not replaced by make image") &&
         check(makeImageCount(workflow) == 1 && resizeCount(workflow) == 0 &&
                   conversionCount(workflow) == 0 &&
                   combinedConversionCount(workflow) == 0,
               "conversion chain folding left conversion nodes") &&
         check(attributes.extents.getConstant() ==
                       std::optional{VkExtent3D{8, 4, 1}} &&
                   attributes.format.getConstant() == VK_FORMAT_R16_SFLOAT,
               "conversion chain folding produced wrong attributes");
}

bool testFoldSharedMakeImageConversion() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto *source = createImage(builder, {16, 8, 1}, VK_FORMAT_R8_UNORM);
  auto &targetFormat = builder.create<Constant<FormatTy>>(VK_FORMAT_R16_SFLOAT)
                           ->results()
                           .front();
  auto &converted =
      builder.create<ConvertFormat>(source->results().front(), targetFormat)
          ->results()
          .front();
  auto *sourceSink =
      builder.create<Sink>(source->results().front(), FormatConstraintInfo{});
  auto *convertedSink = builder.create<Sink>(converted, FormatConstraintInfo{});

  const bool changed = CombineImageConversionsPass{}.run(workflow);

  return check(changed, "shared make image conversion was not folded") &&
         check(&sourceSink->uses().front().value() ==
                   &source->results().front(),
               "shared source consumer was rewired") &&
         check(isa<MakeImage>(&convertedSink->uses().front().value().node()),
               "converted branch did not receive a make image") &&
         check(&convertedSink->uses().front().value() !=
                   &source->results().front(),
               "converted branch reused the unconverted image") &&
         check(makeImageCount(workflow) == 2 && conversionCount(workflow) == 0,
               "shared make image folding produced the wrong graph");
}

bool testExplicitImageTypePreventsMakeImageFold() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto *source = createImage(builder, {1, 1, 1}, VK_FORMAT_R8_UNORM);
  auto &targetExtents =
      builder.create<Constant<ExtentsTy>>(VkExtent3D{1, 1, 1})
          ->results()
          .front();
  auto &targetFormat = builder.create<Constant<FormatTy>>(VK_FORMAT_R16_SFLOAT)
                           ->results()
                           .front();
  auto &convertedResized =
      builder
          .create<ConvertResizeImage>(source->results().front(), targetExtents,
                                      targetFormat, VK_IMAGE_TYPE_2D)
          ->results()
          .front();
  builder.create<Sink>(convertedResized, FormatConstraintInfo{});

  return check(!CombineImageConversionsPass{}.run(workflow),
               "unrepresentable explicit image type was folded") &&
         check(makeImageCount(workflow) == 1 &&
                   combinedConversionCount(workflow) == 1,
               "explicit image type guard changed the graph");
}

} // namespace
} // namespace imvk::graph

int main() {
  using namespace imvk::graph;
  return testCompatibleConstant() && testSharedConversionAndIdempotence() &&
                 testDynamicFormatIsConverted() && testIncompatibleGroups() &&
                 testAssumeCompatibleFormat() &&
                 testAssumeCompatibleExtents() &&
                 testAssumeCompatibleExtentsThroughFormatConversion() &&
                 testNestedCompatibilityAssumptions() &&
                 testScreenExtentsAttributes() &&
                 testScreenImageDoesNotNeedTypeConversion() &&
                 testPresentFormatConstraint() &&
                 testPresentInsertsConversion() && testCompatibleImageType() &&
                 testSharedImageTypeConversionAndIdempotence() &&
                 testIncompatibleImageTypeGroups() &&
                 testDynamicImageTypeIsConverted() &&
                 testPresentInsertsImageTypeConversion() &&
                 testPresentInsertsFormatAndImageTypeConversions() &&
                 testPresentedImageChain() && testPresentVerification() &&
                 testUnifiedImageType() && testImageTypeInference() &&
                 testImageChainTypeInference() && testResizeImageAttributes() &&
                 testResizeImageChain() && testExplicitResizeImageType() &&
                 testConvertResizeImageAttributes() &&
                 testExplicitConvertResizeImageTypeAndChain() &&
                 testCombineResizeThenFormatConversion() &&
                 testCombineFormatThenResizeConversion() &&
                 testCombineAutomaticImageConversions() &&
                 testSharedConversionIsNotCombined() &&
                 testSharedFormatConversionIsNotCombined() &&
                 testFoldMakeImageFormatConversion() &&
                 testFoldMakeImageResize() &&
                 testFoldMakeImageConvertResize() &&
                 testFoldMakeImageConversionChain() &&
                 testFoldSharedMakeImageConversion() &&
                 testExplicitImageTypePreventsMakeImageFold()
             ? 0
             : 1;
}
