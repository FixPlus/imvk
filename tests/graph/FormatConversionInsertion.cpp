#include "GraphTestUtils.hpp"

namespace imvk::graph {
namespace {

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

} // namespace
} // namespace imvk::graph

int main() {
  using namespace imvk::graph;
  return testCompatibleConstant() && testSharedConversionAndIdempotence() &&
                 testDynamicFormatIsConverted() && testIncompatibleGroups() &&
                 testAssumeCompatibleFormat()
             ? 0
             : 1;
}
