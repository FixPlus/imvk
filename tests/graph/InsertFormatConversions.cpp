#include "imvk/graph/Attributes.hpp"
#include "imvk/graph/Nodes.hpp"
#include "imvk/graph/Passes.hpp"

#include <iostream>

namespace imvk::graph {
namespace {

class ImageSource final : public Node {
public:
  ImageSource(Context &ctx, std::optional<VkFormat> format)
      : Node(ctx, Node::EmptyUses,
             std::array{Node::Def{
                 &ctx.types().get<ImageTy>(VK_IMAGE_TYPE_2D),
                 new ImageDefInfo{ImageAccessInfo{}, "image"}}}),
        m_format(format) {}

  const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *>) const final {
    auto format = m_format ? constant(*m_format) : dynamic<VkFormat>(result);
    return &ctx.attributes().get<Attributes<ImageTy>>(
        constant(VkExtent3D{16, 16, 1}), format, constant<size_t>(1),
        constant<size_t>(1));
  }
  std::optional<VerifyError>
  verify(Context &, const AttributesAnalysis &) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "image_source"; }
  void dumpAttributes(std::ostream &) const final {}
  bool hasVisibleSideEffects() const final { return false; }
  bool materialize(MaterializationContext &) final { return false; }

private:
  explicit ImageSource(std::optional<VkFormat> format) : m_format(format) {}
  std::unique_ptr<Node> doClone() const final {
    return std::unique_ptr<Node>{new ImageSource{m_format}};
  }
  std::optional<VkFormat> m_format;
};

class Sink final : public Node {
public:
  Sink(Context &ctx, Value &image, const FormatConstraintInfo &constraint)
      : Node(ctx, std::array{Node::Use{&image, makeInfo(constraint)}},
             Node::EmptyResults) {}

  const AttributesBase *
  getAttributes(Context &, const Value &,
                std::span<const AttributesBase *>) const final {
    return nullptr;
  }
  std::optional<VerifyError>
  verify(Context &, const AttributesAnalysis &) const final {
    return std::nullopt;
  }
  std::string_view name() const final { return "sink"; }
  void dumpAttributes(std::ostream &) const final {}
  bool hasVisibleSideEffects() const final { return true; }
  bool materialize(MaterializationContext &) final { return false; }

private:
  Sink() = default;
  static ImageUseInfo *makeInfo(const FormatConstraintInfo &constraint) {
    auto *info = new ImageUseInfo{};
    info->formatConstraint = constraint;
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

size_t assumptionCount(Workflow &workflow) {
  return std::ranges::count_if(workflow, [](Node &node) {
    return isa<AssumeCompatibleFormat>(&node);
  });
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
  auto &image =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
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
  auto &image =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
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
  auto &image =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8_UNORM})
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
               "screen extents dynamic attribute references another value");
}

bool testPresentFormatConstraint() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();
  auto *present = builder.create<Present>(image);
  const auto *info = dyn_cast<const ImageUseInfo>(present->uses().front().info());

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
               "present accepts a depth format");
}

bool testPresentInsertsConversion() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image = builder
                    .create<ImageSource>(std::optional{VK_FORMAT_D32_SFLOAT})
                    ->results()
                    .front();
  auto *present = builder.create<Present>(image);

  const bool changed = InsertFormatConversionsPass{}.run(workflow);
  auto &presentedValue = present->uses().front().value();
  const AttributesAnalysis attributes{workflow};
  const auto &presentedAttributes =
      attributes.getAttributesFor<Attributes<ImageTy>>(presentedValue);
  const auto format = presentedAttributes.format.getConstant();
  const auto &constraint =
      static_cast<const ImageUseInfo &>(*present->uses().front().info())
          .formatConstraint;

  return check(changed, "present did not convert an incompatible format") &&
         check(conversionCount(workflow) == 1,
               "present inserted an unexpected conversion count") &&
         check(isa<ConvertFormat>(&presentedValue.node()),
               "present does not consume the converted value") &&
         check(format.has_value(), "present conversion format is not constant") &&
         check(format && constraint.isCompatible(*format),
               "present conversion result does not satisfy its constraint");
}

bool testPresentedImageChain() {
  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &presentedImage =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();
  auto &offscreenImage =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
          ->results()
          .front();
  builder.create<Present>(presentedImage);

  auto chains = materializeImageValueChains(workflow);
  const auto presentedCount =
      std::ranges::count_if(chains, [](const auto &chain) {
        return chain.presented;
      });
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
    builder.create<ImageSource>(
        std::optional{VK_FORMAT_R8G8B8A8_UNORM});
    if (!check(!verifyWorkflow(workflow),
               "headless workflow failed verification"))
      return false;
  }

  Context ctx;
  Workflow workflow{ctx};
  WorkflowBuilder builder{workflow, workflow.end()};
  auto &image =
      builder
          .create<ImageSource>(std::optional{VK_FORMAT_R8G8B8A8_UNORM})
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
  return testCompatibleConstant() && testSharedConversionAndIdempotence() &&
                 testDynamicFormatIsConverted() && testIncompatibleGroups() &&
                 testAssumeCompatibleFormat() && testScreenExtentsAttributes() &&
                 testPresentFormatConstraint() &&
                 testPresentInsertsConversion() && testPresentedImageChain() &&
                 testPresentVerification()
             ? 0
             : 1;
}
