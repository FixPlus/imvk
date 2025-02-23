#include "imvk/graphics/Swapchain.hpp"

#include "vkw/Fence.hpp"

namespace imvk {

Swapchain::Swapchain(vkw::Device &device, Queue &q,
                     const VkSwapchainCreateInfoKHR &CI)
    : vkw::SwapChain(device, [&]() {
        auto CICopy = CI;
        CICopy.pNext = nullptr;
        CICopy.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CICopy.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        CICopy.oldSwapchain = nullptr;
        CICopy.pQueueFamilyIndices = nullptr;
        // TODO: amend info based on needs.
        CICopy.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        return CICopy;
      }()) {
  std::vector<VkImageMemoryBarrier> transitLayouts;

  for (auto &image : images()) {
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

  auto queue = q.acquire();
  auto commandPool = vkw::CommandPool{device, 0, queue.get().family().index()};
  auto commandBuffer = vkw::PrimaryCommandBuffer{commandPool};

  commandBuffer.begin(0);

  commandBuffer.imageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                   transitLayouts);

  commandBuffer.end();

  auto fence = vkw::Fence{device};

  auto submitInfo = vkw::SubmitInfo(commandBuffer);

  queue.get().submit(submitInfo, fence);
  fence.wait();

  VkComponentMapping mapping;
  mapping.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  mapping.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  mapping.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  mapping.a = VK_COMPONENT_SWIZZLE_IDENTITY;

  for (auto &image : images()) {
    m_image_views.emplace_back(device, image, image.format(), 0u, 1u, 0u, 1u,
                               mapping);
  }
}
} // namespace imvk