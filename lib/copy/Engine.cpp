#include "imvk/copy/Engine.hpp"
#include "vkw/CommandRecorder.hpp"
#include "vkw/Fence.hpp"

namespace imvk {

CopyEngine::CopyEngine(Context &context, const CopyEngineCreateInfo &CI)
    : EngineBase(context, []() {
        imvk::QueueCapsInfo info;
        info.transfer = true;
        return info;
      }()) {}

std::future<void> CopyEngine::copy(std::unique_ptr<Workload> &&command) {

  /// TODO: this is temporary solution.

  vkw::PrimaryCommandBuffer commandBuffer{commandPool()};
  {
    vkw::BufferRecorder rcrd{commandBuffer,
                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    auto transferPass = rcrd.beginTransferPass();
    command->record(transferPass);
  }

  vkw::Fence fence{context().device()};
  vkw::SubmitInfo submitInfo;
  submitInfo.addCommands(commandBuffer);

  queue().acquire().get().submit(submitInfo, fence);

  return std::async(std::launch::deferred,
                    [fence = std::move(fence),
                     commandBuffer = std::move(commandBuffer),
                     command = std::move(command)]() mutable {
                      fence.wait();
                      return;
                    });
}

} // namespace imvk