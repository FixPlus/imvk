#include "GraphTestUtils.hpp"

namespace imvk::graph {
namespace {

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
  return testCombineResizeThenFormatConversion() &&
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
