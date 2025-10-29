#pragma once
#include "imvk/base/EngineBase.hpp"

#include <functional>
#include <future>
#include <memory>

namespace imvk {

struct CopyEngineCreateInfo {
  // TODO
};

/// @brief Copy engine is used for data transfer. It is suitable for
/// background data streaming operations asynchronous to graphics pipeline
/// operations.
class CopyEngine : public EngineBase {
public:
  struct Workload {
    virtual void record(vkw::TransferPassRecorder &) const = 0;
    virtual ~Workload() = default;
  };
  CopyEngine(Context &context, const CopyEngineCreateInfo &CI);

  std::future<void> copy(std::unique_ptr<Workload> &&command);
};

} // namespace imvk