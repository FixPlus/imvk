#pragma once
#include "vkw/Surface.hpp"
#include "vkw/SwapChain.hpp"


#include <memory>

namespace imvk {

class SwapchainFactory {
public:
  using RecreateCallbackType = void (*)(void);
  /// @brief returns reference to VkSwapchainCreateInfoKHR object which is
  /// prefilled with information needed to construct a swapchain object.
  ///
  /// Returned reference must remain valid until subsequent getCreateInfo()
  /// call. pNext, imageUsage, imageSharingMode, queueFamilyIndexCount,
  /// pQueueFamilyIndices fields are not used.
  /// This may throw if given device cannot present to selected surface.
  virtual const VkSwapchainCreateInfoKHR &
  getCreateInfo(vkw::Device &device) = 0;

  /// @brief returns a reference to surface the swapchain is being created on.
  virtual vkw::Surface &getSurface() noexcept = 0;

  /// @brief sets a callback for manual swapchain recreation.
  /// This callback should be called if factory decides forcibly
  /// recreate swapchain.
  /// IMPORTANT: caller of callback must be externally synchronized with
  /// swapchain producer entity (which in most cases is GraphicsEngine).
  virtual void setRecreateCallback(RecreateCallbackType callback) noexcept = 0;

  virtual ~SwapchainFactory() = default;
};

} // namespace imvk