#include "GraphTestUtils.hpp"

#include <concepts>
#include <type_traits>

namespace imvk::graph {
namespace {

static_assert(std::same_as<decltype(Scene::MaterializedImageAttachment::image),
                           MatImage>);
static_assert(std::same_as<decltype(Scene::MaterializedDescriptor::descriptor),
                           Descriptor>);
static_assert(
    std::same_as<
        std::variant_alternative_t<0, Scene::MaterializedDescriptor::Resource>,
        MatImage>);
static_assert(
    std::same_as<
        decltype(std::declval<const Scene::MaterializedDescriptor &>().image()),
        const MatImage *>);
static_assert(std::same_as<ComputeContext::MaterializedDescriptor,
                           Scene::MaterializedDescriptor>);

class TestMatComputeContext final : public MatComputeContext {
public:
  void onCompute(vkw::ComputePassRecorder &commands, const Frame &frame) final {
  }
};

bool testComputePassDescriptorsAndPassthroughs() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &sampled =
      builder.create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();
  auto &firstStorage =
      builder.create<ImageSource>(std::optional{VK_FORMAT_R16_SFLOAT})
          ->results()
          .front();
  auto &secondStorage =
      builder.create<ImageSource>(std::optional{VK_FORMAT_R32_SFLOAT})
          ->results()
          .front();

  ComputeContext computeContext;
  computeContext.descriptors.emplace_back(DescriptorUseInfo::sampledImage());
  computeContext.descriptors.emplace_back(
      DescriptorUseInfo::storageImage(true));
  computeContext.descriptors.emplace_back(
      DescriptorUseInfo::storageImage(true));
  std::array descriptors{&sampled, &firstStorage, &secondStorage};
  auto *pass = builder.create<ComputePass>(descriptors, computeContext);

  if (!check(pass->uses().size() == 3,
             "compute pass does not have three descriptor inputs") ||
      !check(pass->results().size() == 2,
             "compute pass did not expose its passthrough descriptors"))
    return false;

  const auto *sampledUse =
      dyn_cast<const ImageDescriptorUseInfo>(pass->uses()[0].info());
  const auto *firstStorageUse =
      dyn_cast<const ImageDescriptorUseInfo>(pass->uses()[1].info());
  const auto *secondStorageUse =
      dyn_cast<const ImageDescriptorUseInfo>(pass->uses()[2].info());
  const auto *firstResult =
      dyn_cast<const ImageDefInfo>(&pass->results()[0].info());
  const auto *secondResult =
      dyn_cast<const ImageDefInfo>(&pass->results()[1].info());
  if (!check(sampledUse && firstStorageUse && secondStorageUse,
             "compute pass lost image descriptor metadata") ||
      !check(sampledUse->type() == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
                 sampledUse->shaderStages() == VK_SHADER_STAGE_COMPUTE_BIT &&
                 sampledUse->access.stageFlags ==
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT &&
                 !sampledUse->passthrough,
             "compute sampled image has incorrect access metadata") ||
      !check(firstStorageUse->type() == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE &&
                 firstStorageUse->access.layout == VK_IMAGE_LAYOUT_GENERAL &&
                 firstStorageUse->access.usage == VK_IMAGE_USAGE_STORAGE_BIT &&
                 firstStorageUse->access.accessFlags ==
                     (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT) &&
                 firstStorageUse->passthrough == 0,
             "first compute storage image has incorrect access metadata") ||
      !check(secondStorageUse->passthrough == 1,
             "second compute storage image has incorrect result index") ||
      !check(firstResult && firstResult->passthrough == 1,
             "first compute result does not map to its input") ||
      !check(secondResult && secondResult->passthrough == 2,
             "second compute result does not map to its input"))
    return false;

  const AttributesAnalysis attributes{workflow};
  const auto &firstInputAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(firstStorage);
  const auto &firstResultAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(pass->results()[0]);
  const auto &secondInputAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(secondStorage);
  const auto &secondResultAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(pass->results()[1]);
  return check(firstResultAttributes == firstInputAttributes,
               "first compute result did not preserve input attributes") &&
         check(secondResultAttributes == secondInputAttributes,
               "second compute result did not preserve input attributes") &&
         check(pass->acceptsComputeContext(computeContext),
               "compute pass rejected its compute context") &&
         check(pass->computeContext().descriptors.size() == 3,
               "compute pass did not retain its compute context");
}

bool testComputePassContextCompatibilityAndClone() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image =
      builder.create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();

  ComputeContext original;
  original.descriptors.emplace_back(DescriptorUseInfo::storageImage(true));
  ComputeContext replacement;
  replacement.descriptors.emplace_back(DescriptorUseInfo::storageImage(true));
  ComputeContext wrongType;
  wrongType.descriptors.emplace_back(DescriptorUseInfo::sampledImage());
  ComputeContext wrongPassthrough;
  wrongPassthrough.descriptors.emplace_back(DescriptorUseInfo::storageImage());

  auto *pass = builder.create<ComputePass>(std::array{&image}, original);
  if (!check(pass->acceptsComputeContext(replacement),
             "compute pass rejected a compatible context") ||
      !check(!pass->acceptsComputeContext(wrongType),
             "compute pass accepted an incompatible descriptor type") ||
      !check(!pass->acceptsComputeContext(wrongPassthrough),
             "compute pass accepted an incompatible passthrough layout") ||
      !check(pass->setComputeContext(replacement),
             "compute pass could not install a compatible context") ||
      !check(&pass->computeContext() == &replacement,
             "compute pass did not install the replacement context"))
    return false;

  Workflow cloned{workflow};
  const auto clonedPass = std::ranges::find_if(
      cloned, [](const Node &node) { return isa<ComputePass>(&node); });
  if (!check(clonedPass != cloned.end(),
             "compute pass was not cloned with the workflow"))
    return false;
  const auto &computeClone = static_cast<const ComputePass &>(*clonedPass);
  const auto *clonedUse =
      dyn_cast<const ImageDescriptorUseInfo>(computeClone.uses()[0].info());
  const auto *clonedResult =
      dyn_cast<const ImageDefInfo>(&computeClone.results()[0].info());
  return check(&computeClone.computeContext() == &replacement,
               "compute pass clone lost its compute context") &&
         check(clonedUse && clonedUse->passthrough == 0,
               "compute pass clone lost descriptor passthrough metadata") &&
         check(clonedResult && clonedResult->passthrough == 0,
               "compute pass clone lost result passthrough metadata");
}

} // namespace
} // namespace imvk::graph

int main() {
  using namespace imvk::graph;
  return testComputePassDescriptorsAndPassthroughs() &&
                 testComputePassContextCompatibilityAndClone()
             ? 0
             : 1;
}
