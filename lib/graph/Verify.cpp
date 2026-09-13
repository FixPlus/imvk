#include "imvk/graph/Attributes.hpp"
#include "imvk/graph/Nodes.hpp"

#include <memory>
#include <unordered_set>

namespace imvk::graph {

namespace {

template <std::derived_from<VerifyErrorBase> Error>
VerifyError makeVerifyError(Node *node = nullptr, Use *use = nullptr,
                            Value *value = nullptr) {
  return VerifyError{{node, use, value}, std::make_unique<Error>()};
}

} // namespace

std::optional<VerifyError> verifyWorkflow(const Workflow &wf) {
  // AttributesAnalysis and Node::verify currently require mutable handles even
  // though verification does not mutate the workflow's graph structure.
  auto &workflow = const_cast<Workflow &>(wf);

  std::unordered_set<const Node *> precedingNodes;
  for (auto &node : workflow) {
    for (auto &use : node.uses()) {
      if (!use.hasValue())
        return makeVerifyError<MissingUseValueError>(&node, &use);

      auto &value = use.value();
      if (value.isNull())
        return makeVerifyError<MissingUseValueError>(&node, &use, &value);

      if (!precedingNodes.contains(&value.node()))
        return makeVerifyError<InvalidTopologyError>(&node, &use, &value);

      if (&use.type() != &value.type())
        return makeVerifyError<UseTypeMismatchError>(&node, &use, &value);
    }
    precedingNodes.insert(&node);
  }

  AttributesAnalysis attributes{workflow};
  for (auto &node : workflow) {
    if (auto error = node.verify(workflow.context(), attributes))
      return error;
  }

  Present *present = nullptr;
  for (auto &node : workflow) {
    if (auto *currentPresent = dyn_cast<Present>(&node)) {
      if (present)
        return makeVerifyError<MultiplePresentImageError>(currentPresent);
      present = currentPresent;
    }
  }

  return std::nullopt;
}

std::optional<VerifyError>
RenderPass::verify(Context &ctx, const AttributesAnalysis &aa) const {
  // TODO
  return std::nullopt;
}

std::optional<VerifyError>
Copy<ImageTy>::verify(Context &ctx, const AttributesAnalysis &aa) const {
  // TODO
  return std::nullopt;
}

std::optional<VerifyError>
MakeImage::verify(Context &ctx, const AttributesAnalysis &aa) const {
  // TODO
  return std::nullopt;
}

} // namespace imvk::graph
