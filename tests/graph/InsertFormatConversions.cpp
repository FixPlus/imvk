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

} // namespace
} // namespace imvk::graph

int main() {
  using namespace imvk::graph;
  return testCompatibleConstant() && testSharedConversionAndIdempotence() &&
                 testDynamicFormatIsConverted() && testIncompatibleGroups()
             ? 0
             : 1;
}