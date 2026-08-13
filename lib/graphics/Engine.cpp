#include "imvk/graphics/Engine.hpp"
#include "imvk/graphics/Swapchain.hpp"

#include <array>

namespace imvk {

SSemaphore::SSemaphore(FramedEngine &engine, Swapchain &swapchain)
    : FOENode<vkw::Semaphore, fon_type::ext>(
          [&]() { return std::array<FONodeBase *, 1>{&swapchain}; }()) {
  onConstruct(engine);
}
void SSemaphore::doConstructNew(
    FramedEngine &engine, unsigned count,
    boost::container::small_vector_base<FObject::Ptr> &res) {
  std::ranges::transform(
      engine.frameIds(), std::back_inserter(res), [&](FrameID) {
        return engine.createObject<vkw::Semaphore>(engine.context().device());
      });
}
GraphicsEngine::GraphicsEngine(Context &context,
                               const GraphicsEngineCreateInfo &CI)
    : FramedEngine(context,
                   QueueCapsInfo{.present = true,
                                 .graphics = true,
                                 .compute = true,
                                 .transfer = true},
                   CI.maxFramesInFlight),
      m_swapchainFactory(*CI.swapchainFactory),
      m_swapchain(createNode<Swapchain>()),
      m_renderComplete(createNode<SSemaphore>(*m_swapchain)),
      m_presentComplete(createNode<Semaphore>()) {
  assert(CI.maxFramesInFlight);
}

std::optional<GraphicsEngine::SwapFrame> GraphicsEngine::m_beginFrame() {
  if (!m_pending) {
    m_pending.emplace(nextFrame()->get());
  }

  auto status = m_swapchain->get().acquireNextImage(
      m_presentComplete->use(m_pending->frame),
      /* timeout in milliseconds*/ 1000);
  if (status == vkw::SwapChain::AcquireStatus::TIMEOUT) {
    return std::nullopt;
  }
  if (status == vkw::SwapChain::AcquireStatus::OUT_OF_DATE ||
      status == vkw::SwapChain::AcquireStatus::SUBOPTIMAL) {
    if (!m_surface_minimized())
      m_recreate_swapchain();
    return std::nullopt;
  }

  return SwapFrame{*this, *std::exchange(m_pending, std::nullopt),
                   *m_renderComplete, *m_presentComplete};
}

void GraphicsEngine::m_recreate_swapchain() {
  queue().acquire().get().waitIdle();
  m_swapchain->reconstruct(*this);
}

bool GraphicsEngine::m_surface_minimized() {
  auto extents =
      m_swapchainFactory.getSurface()
          .getSurfaceCapabilities(context().device().physicalDevice())
          .currentExtent;
  return extents.width == 0 || extents.height == 0;
}

GraphicsEngine::~GraphicsEngine() = default;

void GraphicsEngine::SwapFrame::FrameEnder::operator()(
    GraphicsEngine *engine) const {
  if (!engine)
    return;
  auto id = recorder.frame.get().id();
  auto &pc = presentComplete->use(recorder.frame);
  auto &rc = renderComplete->use(recorder.frame);

  engine->submitFrame(std::move(recorder), [&](vkw::SubmitInfo &submitInfo) {
    submitInfo.addWaitCondition(pc,
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    submitInfo.addSignalTo(rc);
  });
  auto presentInfo = vkw::PresentInfo{swapchain->get(), rc};
  auto q = engine->queue().acquire();
  q.get().present(presentInfo);
}
} // namespace imvk