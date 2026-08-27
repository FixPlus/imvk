#include "imvk/graphics/Swapchain.hpp"
#include "imvk/graphics/Engine.hpp"

#include "vkw/CommandRecorder.hpp"
#include "vkw/Fence.hpp"

#include <array>

namespace imvk {
vkw::SwapChain GraphicsEngine::m_createSwapchain() {
  auto &device = context().device();
  auto swapchain = vkw::SwapChain(device, [&]() {
    auto CICopy = m_swapchainFactory.getCreateInfo(device);
    CICopy.pNext = nullptr;
    CICopy.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CICopy.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    CICopy.oldSwapchain = nullptr;
    CICopy.pQueueFamilyIndices = nullptr;
    // TODO: amend info based on needs.
    CICopy.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    return CICopy;
  }());
  std::vector<VkImageMemoryBarrier> transitLayouts;

  for (auto &image : swapchain.images()) {
    VkImageMemoryBarrier transitLayout{};
    transitLayout.image = image.vkw::NonOwingImage::operator VkImage_T *();
    transitLayout.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    transitLayout.pNext = nullptr;
    transitLayout.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    transitLayout.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    transitLayout.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    transitLayout.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    transitLayout.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    transitLayout.subresourceRange.baseArrayLayer = 0;
    transitLayout.subresourceRange.baseMipLevel = 0;
    transitLayout.subresourceRange.layerCount = 1;
    transitLayout.subresourceRange.levelCount = 1;
    transitLayout.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    transitLayout.srcAccessMask = 0;

    transitLayouts.push_back(transitLayout);
  }

  auto q = queue().acquire();
  auto commandPool = vkw::CommandPool{device, 0, q.get().family().index()};
  auto commandBuffer = vkw::PrimaryCommandBuffer{commandPool};
  {
    vkw::BufferRecorder rcd{commandBuffer,
                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    auto transferPass = rcd.beginTransferPass();
    transferPass.imageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                    transitLayouts);
  }

  auto fence = vkw::Fence{device};

  vkw::SubmitInfo submitInfo;
  submitInfo.addCommands(commandBuffer);

  q.get().submit(submitInfo, fence);
  fence.wait();
  return swapchain;
}
SwapchainImpl::SwapchainImpl(GraphicsEngine &engine)
    : FONode<vkw::SwapChain, fon_type::cow, SwapchainImpl>(
          engine, constructNew(engine)) {}

vkw::SwapChain SwapchainImpl::constructNew(FramedEngine &engine) {
  return static_cast<GraphicsEngine &>(engine).m_createSwapchain();
}

vkw::ImageView<vkw::COLOR, vkw::V2DA>
SwapchainViewImpl::constructOne(FramedEngine &engine, unsigned id) {
  auto &image = swapchain().images()[id];
  VkComponentMapping mapping;
  mapping.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  mapping.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  mapping.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  mapping.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  return vkw::ImageView<vkw::COLOR, vkw::V2DA>(engine.context().device(), image,
                                               image.format(), 0u, 1u, 0u, 1u,
                                               mapping);
}

} // namespace imvk