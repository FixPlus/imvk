#pragma once
#include "imvk/base/Frame.hpp"
#include "vkw/Fence.hpp"
#include "vkw/Semaphore.hpp"

namespace imvk {

class GraphicsEngine;
class Swapchain;

class SwapFrame {
public:
  SwapFrame(const Frame &frame, const Swapchain &swapchain)
      : m_frame(frame), m_swapchain(swapchain){};

  const auto &frame() const { return m_frame.get(); }

  const auto &swapchain() const { return m_swapchain.get(); }

private:
  std::reference_wrapper<const Frame> m_frame;
  std::reference_wrapper<const Swapchain> m_swapchain;
};

class FrameSyncObjects final {
public:
  FrameSyncObjects(GraphicsEngine &engine);
  vkw::Semaphore renderComplete, presentComplete;
  bool needFenceWait = false;
  vkw::Fence fence;
  void waitIfNeeded();
};

} // namespace imvk