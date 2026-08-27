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
class SwapchainImpl final
    : public FONode<vkw::SwapChain, fon_type::cow, SwapchainImpl> {
public:
  SwapchainImpl(GraphicsEngine &engine);

  FObject::Ptr constructNew(FramedEngine &engine);
};

class Swapchain : public FONodeView<SwapchainImpl> {
public:
  Swapchain(auto &&...args)
      : FONodeView<SwapchainImpl>(std::forward<decltype(args)>(args)...) {}
  // Although a swapchain is a cow, we may still modify its state.
  vkw::SwapChain &get() const {
    return const_cast<vkw::SwapChain &>((*this)->get());
  }
};

template <typename T, typename Derived>
class Swapchained : public FONode<T, fon_type::ext, Derived> {
public:
  Swapchained(Swapchain &swapchain, FOUses &&uses)
      : FONode<T, fon_type::ext, Derived>(FOUses{*swapchain} | uses) {}
  Swapchained(Swapchain &swapchain)
      : FONode<T, fon_type::ext, Derived>(FOUses{*swapchain}) {}

  template <typename U>
    requires !
             std::convertible_to<U, Swapchain> Swapchained(U & swapchained,
                                                           FOUses &&uses)
      : FONode<T, fon_type::ext, Derived>(
            FOUses{*swapchained.getUse<Swapchain>(0), *swapchained} | uses) {}
  template <typename U>
    requires !
             std::convertible_to<U, Swapchain> Swapchained(U & swapchained)
      : FONode<T, fon_type::ext, Derived>(
            FOUses{*swapchained.getUse<Swapchain>(0), *swapchained}) {}

  vkw::SwapChain &swapchain() { return this->getUse<Swapchain>(0).get(); }
  const vkw::SwapChain &swapchain() const {
    return this->getUse<Swapchain>(0).get();
  }
  unsigned getExtIndex(const Frame &frame) const {
    return swapchain().currentImage();
  }

  void constructNew(FramedEngine &engine,
                    boost::container::small_vector_base<FObject::Ptr> &res) {
    for (auto id :
         std::ranges::iota_view{0ul, std::ranges::size(swapchain().images())}) {
      res.push_back(constructOneBase(engine, id));
    }
  }

  FObject::Ptr constructOneBase(FramedEngine &engine, unsigned image) {
    return static_cast<Derived &>(*this).constructOne(engine, image);
  }
};

template <typename T> class SwapchainedView : public FONodeView<T> {
public:
  SwapchainedView(auto &&...args)
      : FONodeView<T>(std::forward<decltype(args)>(args)...) {}
  const vkw::SwapChain &swapchain() const {
    return (*this)->getUse<Swapchain>(0).get();
  }
};

class SwapchainViewImpl final
    : public Swapchained<vkw::ImageView<vkw::COLOR, vkw::V2DA>,
                         SwapchainViewImpl> {
public:
  SwapchainViewImpl(GraphicsEngine &engine, Swapchain &swapchain);

  void onUseAction(const Frame &frame,
                   vkw::ImageView<vkw::COLOR, vkw::V2DA> &obj) {
    // nothing to do.
  }
  FObject::Ptr constructOne(FramedEngine &engine, unsigned image);
};

class SwapchainView : public SwapchainedView<SwapchainViewImpl> {
public:
  SwapchainView(auto &&...args)
      : SwapchainedView<SwapchainViewImpl>(
            std::forward<decltype(args)>(args)...) {}
};

} // namespace imvk