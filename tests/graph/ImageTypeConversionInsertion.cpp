#include "GraphTestUtils.hpp"

namespace imvk::graph {
namespace {

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

} // namespace
} // namespace imvk::graph

int main() {
  using namespace imvk::graph;
  return testAssumeCompatibleExtents() &&
                 testAssumeCompatibleExtentsThroughFormatConversion() &&
                 testNestedCompatibilityAssumptions() &&
                 testScreenExtentsAttributes() &&
                 testScreenImageDoesNotNeedTypeConversion() &&
                 testCompatibleImageType() &&
                 testSharedImageTypeConversionAndIdempotence() &&
                 testIncompatibleImageTypeGroups() &&
                 testDynamicImageTypeIsConverted()
             ? 0
             : 1;
}
