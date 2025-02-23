#pragma once

#include "vkw/Image.hpp"
#include "vkw/SwapChain.hpp"

#include "imvk/base/Queue.hpp"

#include <span>
#include <vector>

namespace imvk {

class Swapchain : public vkw::SwapChain {
public:
  Swapchain(vkw::Device &device, Queue &queue,
            const VkSwapchainCreateInfoKHR &CI);

  std::span<const vkw::ImageView<vkw::COLOR, vkw::V2DA>> attachments() const {
    return m_image_views;
  }

private:
  std::vector<vkw::ImageView<vkw::COLOR, vkw::V2DA>> m_image_views;
};

} // namespace imvk