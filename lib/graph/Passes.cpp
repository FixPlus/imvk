#include "imvk/graph/Passes.hpp"

#include "imvk/graph/Attributes.hpp"
#include "imvk/graph/Nodes.hpp"
#include "imvk/graph/Types.hpp"

#include <boost/container/small_vector.hpp>
#include <stdexcept>
#include <vulkan/vulkan_format_traits.hpp>

namespace imvk::graph {

namespace {

bool canBeConversionTarget(vk::Format format) {
  if (format == vk::Format::eUndefined || vk::isCompressed(format) ||
      vk::planeCount(format) != 1)
    return false;
  const auto blockExtent = vk::blockExtent(format);
  return blockExtent == std::array<uint8_t, 3>{1, 1, 1};
}

const std::vector<VkFormat> &conversionTargetFormats() {
  static const auto formats = [] {
    std::vector<VkFormat> result;
    for (const auto format : vk::getAllFormats()) {
      if (canBeConversionTarget(format))
        result.push_back(static_cast<VkFormat>(format));
    }
    return result;
  }();
  return formats;
}

struct ConstrainedUse {
  Use *use;
  const FormatConstraintInfo *constraint;
};

struct TypeConstrainedUse {
  Use *use;
  VkImageViewType constraint;
};

std::optional<VkFormat>
selectFormat(std::span<const ConstrainedUse> constrainedUses) {
  std::optional<VkFormat> selected;
  size_t selectedCoverage = 0;
  for (const auto format : conversionTargetFormats()) {
    const auto coverage =
        std::ranges::count_if(constrainedUses, [&](const auto &constrainedUse) {
          return constrainedUse.constraint->isCompatible(format);
        });
    if (coverage > selectedCoverage) {
      selected = format;
      selectedCoverage = coverage;
    }
  }
  return selected;
}

std::optional<
    std::pair<Value *, boost::container::small_vector<ConstrainedUse, 4>>>
findConversionsToInsert(Workflow &workflow,
                        const AttributesAnalysis &attributes) {
  auto hasAssumedCompatibleFormat = [](const Value &value) {
    const Value *current = &value;
    while (true) {
      if (isa<AssumeCompatibleFormat>(&current->node()))
        return true;
      if (!isa<AssumeCompatibleExtents>(&current->node()))
        return false;
      current = &current->node().uses().front().value();
    }
  };
  for (auto &node : workflow) {
    for (auto &value : node.results()) {
      if (!isa<ImageTy>(&value.type()))
        continue;
      if (hasAssumedCompatibleFormat(value))
        continue;

      const auto sourceFormat =
          attributes.getAttributesFor<Attributes<ImageTy>>(value)
              .format.getConstant();
      boost::container::small_vector<ConstrainedUse, 4> incompatibleUses;
      for (auto &use : value.users()) {
        if (isa<ConvertFormat>(&use.user()) ||
            isa<ConvertResizeImage>(&use.user()))
          continue;
        const auto *info = dyn_cast<const ImageUseInfo>(use.info());
        if (!info || info->formatConstraint.empty())
          continue;
        if (sourceFormat && info->formatConstraint.isCompatible(*sourceFormat))
          continue;
        incompatibleUses.push_back({&use, &info->formatConstraint});
      }
      if (!incompatibleUses.empty())
        return std::pair{&value, std::move(incompatibleUses)};
    }
  }
  return std::nullopt;
}

std::optional<
    std::pair<Value *, boost::container::small_vector<TypeConstrainedUse, 4>>>
findTypeConversionsToInsert(Workflow &workflow,
                            const AttributesAnalysis &attributes) {
  auto hasAssumedCompatibleExtents = [](const Value &value) {
    const Value *current = &value;
    while (true) {
      if (isa<AssumeCompatibleExtents>(&current->node()))
        return true;
      if (!isa<AssumeCompatibleFormat>(&current->node()) &&
          !isa<ConvertFormat>(&current->node()))
        return false;
      current = &current->node().uses().front().value();
    }
  };
  for (auto &node : workflow) {
    for (auto &value : node.results()) {
      if (!isa<ImageTy>(&value.type()))
        continue;
      if (hasAssumedCompatibleExtents(value))
        continue;

      const auto &sourceAttributes =
          attributes.getAttributesFor<Attributes<ImageTy>>(value);
      const auto sourceType = sourceAttributes.imageType.getConstant();
      boost::container::small_vector<TypeConstrainedUse, 4> incompatibleUses;
      for (auto &use : value.users()) {
        const auto *info = dyn_cast<const ImageUseInfo>(use.info());
        if (!info || !info->viewTypeConstraint)
          continue;
        const auto requiredType =
            imageTypeForViewType(*info->viewTypeConstraint);
        if (!requiredType)
          throw std::runtime_error(
              "unsupported Vulkan image view type constraint "
              "on image use '" +
              std::string(info->name()) + "'");
        if (const auto layers = sourceAttributes.layers.getConstant();
            layers &&
            !imageViewTypeAcceptsLayers(*info->viewTypeConstraint, *layers))
          throw std::runtime_error(
              "image layer count cannot satisfy the Vulkan image view type "
              "constraint of image use '" +
              std::string(info->name()) + "'");
        if (sourceType == requiredType)
          continue;
        incompatibleUses.push_back({&use, *info->viewTypeConstraint});
      }
      if (!incompatibleUses.empty())
        return std::pair{&value, std::move(incompatibleUses)};
    }
  }
  return std::nullopt;
}

} // namespace

bool InsertFormatConversionsPass::run(Workflow &workflow) const {
  bool changed = false;
  while (true) {
    const AttributesAnalysis attributes{workflow};
    auto pending = findConversionsToInsert(workflow, attributes);
    if (!pending)
      return changed;

    auto &[source, incompatibleUses] = *pending;
    const auto selectedFormat = selectFormat(incompatibleUses);
    if (!selectedFormat)
      throw std::runtime_error(
          "no Vulkan image format satisfies the format "
          "constraint of image use '" +
          std::string(incompatibleUses.front().use->info()->name()) + "'");

    auto insertionPoint = std::next(workflow.iteratorTo(&source->node()));
    WorkflowBuilder builder{workflow, insertionPoint};
    auto &format =
        builder.create<Constant<FormatTy>>(*selectedFormat)->results().front();
    auto &converted =
        builder.create<ConvertFormat>(*source, format)->results().front();

    for (const auto &[use, constraint] : incompatibleUses) {
      if (constraint->isCompatible(*selectedFormat))
        use->replaceBy(&converted);
    }
    changed = true;
  }
}

bool InsertImageTypeConversionsPass::run(Workflow &workflow) const {
  bool changed = false;
  while (true) {
    const AttributesAnalysis attributes{workflow};
    auto pending = findTypeConversionsToInsert(workflow, attributes);
    if (!pending)
      return changed;

    auto &[source, incompatibleUses] = *pending;
    const auto selectedType =
        *imageTypeForViewType(incompatibleUses.front().constraint);
    auto insertionPoint = std::next(workflow.iteratorTo(&source->node()));
    WorkflowBuilder builder{workflow, insertionPoint};
    auto &extents = builder.create<GetExtents>(*source)->results().front();
    auto &converted =
        builder.create<ResizeImage>(*source, extents, selectedType)
            ->results()
            .front();

    for (const auto &[use, constraint] : incompatibleUses) {
      if (imageTypeForViewType(constraint) == selectedType)
        use->replaceBy(&converted);
    }
    changed = true;
  }
}

bool RemoveAssumeCompatibleFormatsPass::run(Workflow &workflow) const {
  boost::container::small_vector<AssumeCompatibleFormat *, 4> assumptions;
  for (auto &node : workflow) {
    if (auto *assumption = dyn_cast<AssumeCompatibleFormat>(&node))
      assumptions.push_back(assumption);
  }

  for (auto *assumption : assumptions) {
    assert(assumption->uses().size() == 1);
    assert(assumption->results().size() == 1);
    auto &input = assumption->uses().front().value();
    assumption->results().front().replaceAllUsesWith(&input);
    workflow.erase(assumption);
  }
  return !assumptions.empty();
}

bool RemoveAssumeCompatibleExtentsPass::run(Workflow &workflow) const {
  boost::container::small_vector<AssumeCompatibleExtents *, 4> assumptions;
  for (auto &node : workflow) {
    if (auto *assumption = dyn_cast<AssumeCompatibleExtents>(&node))
      assumptions.push_back(assumption);
  }

  for (auto *assumption : assumptions) {
    assert(assumption->uses().size() == 1);
    assert(assumption->results().size() == 1);
    auto &input = assumption->uses().front().value();
    assumption->results().front().replaceAllUsesWith(&input);
    workflow.erase(assumption);
  }
  return !assumptions.empty();
}

} // namespace imvk::graph
