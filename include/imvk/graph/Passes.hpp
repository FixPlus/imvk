#pragma once

#include "imvk/graph/Context.hpp"

namespace imvk::graph {

class WorkflowPass {
public:
  virtual bool run(Workflow &workflow) const = 0;
  virtual ~WorkflowPass() = default;
};

class InsertFormatConversionsPass final : public WorkflowPass {
public:
  bool run(Workflow &workflow) const final;
};

class RemoveAssumeCompatibleFormatsPass final : public WorkflowPass {
public:
  bool run(Workflow &workflow) const final;
};

} // namespace imvk::graph
