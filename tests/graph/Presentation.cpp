#include "GraphTestUtils.hpp"

namespace imvk::graph {
namespace {

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

} // namespace
} // namespace imvk::graph

int main() {
  using namespace imvk::graph;
  return testPresentFormatConstraint() && testPresentInsertsConversion() &&
                 testPresentInsertsImageTypeConversion() &&
                 testPresentInsertsFormatAndImageTypeConversions() &&
                 testPresentedImageChain() && testPresentVerification()
             ? 0
             : 1;
}
