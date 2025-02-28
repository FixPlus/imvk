#include "imvk/copy/Engine.hpp"
#include "vkw/Fence.hpp"

namespace imvk {

CopyEngine::CopyEngine(ContextImpl &context, const CopyEngineCreateInfo &CI)
    : EngineBase(context, []() {
        imvk::QueueCapsInfo info;
        info.transfer = true;
        return info;
      }()) {}

std::future<void> CopyEngine::copy(std::unique_ptr<Workload> &&command) {

  /// TODO: this is temporary solution.

  vkw::PrimaryCommandBuffer commandBuffer{commandPool()};
  commandBuffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  command->record(commandBuffer);

  commandBuffer.end();

  vkw::Fence fence{context().device()};

  queue().acquire().get().submit(vkw::SubmitInfo(commandBuffer), fence);

  return std::async(std::launch::deferred,
                    [fence = std::move(fence),
                     commandBuffer = std::move(commandBuffer),
                     command = std::move(command)]() mutable {
                      fence.wait();
                      return;
                    });
}

} // namespace imvk