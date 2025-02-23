#pragma once
#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Frame.hpp"
#include "imvk/graphics/Swapchain.hpp"

#include "vkw/Framebuffer.hpp"
#include "vkw/Image.hpp"
#include "vkw/RenderPass.hpp"

namespace imvk::examples {

class BasicRenderPass {
public:
  BasicRenderPass(imvk::GraphicsEngine &engine);

  void run(const SwapFrame &frame, std::function<void(void)> callback);

private:
  void m_clearFramebuffers();
  void m_recreateFramebuffers();
  imvk::GraphicsEngine &m_engine;
  const imvk::Swapchain *m_currentSwapchain;
  vkw::RenderPass m_pass;
  std::vector<vkw::ImageView<vkw::COLOR, vkw::V2DA>> m_swapImageViews;
  std::vector<vkw::FrameBuffer> m_framebuffers;
};

} // namespace imvk::examples