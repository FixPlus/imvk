#pragma once

#include <vkw/Image.hpp>
#include <vkw/Surface.hpp>
#include <vkw/SwapChain.hpp>

#include "imvk/base/Queue.hpp"

#include "imvk/base/Frame.hpp"

#include <span>
#include <vector>

namespace imvk {

class GraphicsEngine;

/// @brief abstracts away external surface and swapchain creation.
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

/// @brief Thin wrapper over vkw::SwapChain that creates image views for
/// swapchain images and transits image layout to present_src.
class Swapchain final : public FOENode<vkw::SwapChain, fon_type::cow> {
public:
  Swapchain(GraphicsEngine &engine);

  // Although a swapchain is a cow, we may still modify its state.
  vkw::SwapChain &get() {
    return const_cast<vkw::SwapChain &>(
        FOENode<vkw::SwapChain, fon_type::cow>::get());
  }
  const vkw::SwapChain &get() const {
    return FOENode<vkw::SwapChain, fon_type::cow>::get();
  }

private:
  FObject::Ptr constructNew(FramedEngine &engine) final;

  FObject::Ptr doConstructNew(GraphicsEngine &engine);
};

class SwapchainView final
    : public FOENode<vkw::ImageView<vkw::COLOR, vkw::V2DA>, fon_type::ext> {
public:
  SwapchainView(GraphicsEngine &engine, Swapchain &swapchain);

  const vkw::SwapChain &swapchain() const {
    return getUse<const Swapchain>(0).get();
  }

private:
  unsigned getExtIndex(const Frame &frame) const final;
  void onUseAction(const Frame &frame, FObject &obj) final {
    // nothing to do.
  }

  void
  constructNew(FramedEngine &engine,
               boost::container::small_vector_base<FObject::Ptr> &res) final {
    doConstructNew(engine, res, swapchain());
  }

  static void
  doConstructNew(FramedEngine &engine,
                 boost::container::small_vector_base<FObject::Ptr> &res,
                 const vkw::SwapChain &);
};

} // namespace imvk