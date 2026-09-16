#include "imvk/graph/Attributes.hpp"
#include "imvk/graph/Nodes.hpp"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <string_view>

namespace imvk::graph {
namespace {

bool check(bool condition, std::string_view message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

bool testConstantExtents() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &input = builder.create<Constant<ExtentsTy>>(VkExtent3D{15, 8, 3})
                    ->results()
                    .front();
  auto *half = builder.create<HalfExtents>(input);

  const AttributesAnalysis attributes{workflow};
  const auto &output = attributes.getAttributesFor<Attributes<ExtentsTy>>(
      half->results().front());
  return check(output.extents.getConstant() ==
                   std::optional{VkExtent3D{7, 4, 1}},
               "half extents did not use Vulkan mip rounding") &&
         check(output.imageType.getConstant() == VK_IMAGE_TYPE_3D,
               "half extents did not preserve the input image type") &&
         check(!verifyWorkflow(workflow),
               "valid half extents workflow failed verification");
}

bool testMinimumExtents() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &input = builder.create<Constant<ExtentsTy>>(VkExtent3D{1, 1, 1})
                    ->results()
                    .front();
  auto *half = builder.create<HalfExtents>(input);

  const AttributesAnalysis attributes{workflow};
  const auto &output = attributes.getAttributesFor<Attributes<ExtentsTy>>(
      half->results().front());
  return check(output.extents.getConstant() ==
                   std::optional{VkExtent3D{1, 1, 1}},
               "half extents produced a zero-sized dimension");
}

bool testDynamicExtents() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &input = builder.create<ScreenExtents>()->results().front();
  auto *half = builder.create<HalfExtents>(input);
  auto &result = half->results().front();

  const AttributesAnalysis attributes{workflow};
  const auto &output =
      attributes.getAttributesFor<Attributes<ExtentsTy>>(result);
  return check(output.extents.getDynamicValue() == &result,
               "dynamic half extents did not identify its result") &&
         check(output.imageType.getConstant() == VK_IMAGE_TYPE_2D,
               "dynamic half extents did not preserve the input image type");
}

bool testRepeatedHalvingAndClone() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &input = builder.create<Constant<ExtentsTy>>(VkExtent3D{9, 7, 1})
                    ->results()
                    .front();
  Value *current = &input;
  for (unsigned level = 0; level < 4; ++level)
    current = &builder.create<HalfExtents>(*current)->results().front();

  const AttributesAnalysis attributes{workflow};
  const auto &output =
      attributes.getAttributesFor<Attributes<ExtentsTy>>(*current);
  if (!check(output.extents.getConstant() == std::optional{VkExtent3D{1, 1, 1}},
             "repeated halving did not clamp all dimensions to one"))
    return false;

  Workflow cloned{workflow};
  const auto halfCount = std::ranges::count_if(
      cloned, [](const Node &node) { return isa<HalfExtents>(&node); });
  const AttributesAnalysis clonedAttributes{cloned};
  const auto &clonedResult = std::prev(cloned.end())->results().front();
  const auto &clonedOutput =
      clonedAttributes.getAttributesFor<Attributes<ExtentsTy>>(clonedResult);
  return check(halfCount == 4, "workflow clone lost half extents nodes") &&
         check(clonedOutput.extents.getConstant() ==
                   std::optional{VkExtent3D{1, 1, 1}},
               "cloned half extents workflow produced different attributes");
}

} // namespace
} // namespace imvk::graph

int main() {
  using namespace imvk::graph;
  return testConstantExtents() && testMinimumExtents() &&
                 testDynamicExtents() && testRepeatedHalvingAndClone()
             ? 0
             : 1;
}