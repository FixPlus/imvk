#pragma once
#include "imvk/base/Context.hpp"
#include "imvk/base/EngineBase.hpp"

#include <functional>
#include <future>
#include <memory>

namespace imvk {

class CopyEngine : public EngineBase {
public:
  struct Workload {
    virtual void record(vkw::CommandBuffer &) const = 0;
    virtual ~Workload() = default;
  };
  CopyEngine(ContextImpl &context, const CopyEngineCreateInfo &CI);

  std::future<void> copy(std::unique_ptr<Workload> &&command);
};

} // namespace imvk