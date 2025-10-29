#pragma once
#include "imvk/base/Context.hpp"

namespace imvk {

struct ComputeEngineCreateInfo {
  // TODO
};

/// @brief Compute engine is used to perform compute operations. It also
/// supports transfer. It is suitable for compute tasks that are not directly
/// used by graphics pipeline.
class ComputeEngine {
public:
  ComputeEngine(Context &context, const ComputeEngineCreateInfo &CI);

private:
};

} // namespace imvk