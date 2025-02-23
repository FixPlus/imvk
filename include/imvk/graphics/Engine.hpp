#pragma once

#include "imvk/base/Context.hpp"
#include "imvk/base/EngineBase.hpp"
#include "imvk/graphics/Frame.hpp"

#include "vkw/CommandPool.hpp"
#include "vkw/Fence.hpp"
#include "vkw/Semaphore.hpp"

namespace imvk {

class Swapchain;
class FrameWithSync;
class SwapFrame;

class GraphicsEngine : public FramedEngine {
public:
  GraphicsEngine(ContextImpl &context, const GraphicsEngineCreateInfo &CI);

  std::optional<SwapFrame> beginFrame();

  void endFrame();

  const Swapchain &swapchain() const { return *m_swapchain; }

  ~GraphicsEngine() override;

private:
  void m_recreate_swapchain();
  bool m_surface_minimized();

  SwapchainFactory &m_swapchainFactory;
  std::unique_ptr<Swapchain> m_swapchain;
  std::vector<FrameSyncObjects> m_frameSyncs;
  std::optional<SwapFrame> m_currentFrame;
};

} // namespace imvk