#include "GraphTestUtils.hpp"

namespace imvk::graph {
namespace {

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

} // namespace
} // namespace imvk::graph

int main() {
  using namespace imvk::graph;
  return testUnifiedImageType() && testImageTypeInference() &&
                 testImageChainTypeInference() && testResizeImageAttributes() &&
                 testResizeImageChain() && testExplicitResizeImageType() &&
                 testConvertResizeImageAttributes() &&
                 testExplicitConvertResizeImageTypeAndChain()
             ? 0
             : 1;
}
