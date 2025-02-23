#include "imvk/graphics/Frame.hpp"
#include "imvk/graphics/Engine.hpp"
namespace imvk {

FrameSyncObjects::FrameSyncObjects(GraphicsEngine &engine)
    : renderComplete(engine.context().device()),
      presentComplete(engine.context().device()),
      fence(engine.context().device()) {}

void FrameSyncObjects::waitIfNeeded() {
  if (needFenceWait) {
    fence.wait();
    fence.reset();
    needFenceWait = false;
  }
}
} // namespace imvk