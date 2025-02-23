#include "imvk/base/Context.hpp"
#include "imvk/graphics/Engine.hpp"

#include "IMVKBasicRenderPass.hpp"
#include "IMVKDevice.hpp"
#include "IMVKShaderLoader.hpp"
#include "IMVKWindow.hpp"

#include <iostream>

int main() try {
  // Open vulkan loader library, construct vulkan instance, pick
  // physical device and construct logical device.
  imvk::examples::Device imvkDevice{
      imvk::examples::DeviceCreateInfo{.enableValidation = true}};

  // Create presentable window and it's surface. This will be used as swapchain
  // factory.
  imvk::examples::WindowSettings windowSettings{
      .title = "Hello world", .width = 800, .height = 600};
  imvk::examples::Window window{windowSettings, imvkDevice.get().parent()};

  // Create shader loader
  imvk::examples::ShaderLoaderCreateInfo shaderLoaderCI{.shaderDirectory =
                                                            "assets/shaders"};
  imvk::examples::ShaderLoader shaderLoader{shaderLoaderCI};

  // Create instance of imvk context.
  imvk::ContextCreateInfo imvkCCI{.device = imvkDevice.get(),
                                  .shaderFactory = shaderLoader};
  imvk::Context imvkContext{imvkCCI};

  // Create graphics engine.
  imvk::GraphicsEngineCreateInfo geCI{.swapchainFactory = &window,
                                      .maxFramesInFlight = 2};
  auto graphicsEngine = imvkContext.createGraphicsEngine(geCI);

  // Create basic render pass.
  auto renderPass = imvk::examples::BasicRenderPass{*graphicsEngine};

  // Main application loop.
  graphicsEngine->run(
      [&](auto &frame) {
        renderPass.run(frame, []() {
          // TODO
        });
      },
      [&]() {
        window.pollEvents();
        return !window.shouldClose();
      });

  return 0;
} catch (std::runtime_error &e) {
  std::cerr << "RUNTIME ERROR: " << e.what() << std::endl;
  return 1;
} catch (std::logic_error &e) {
  std::cerr << "LOGIC ERROR: " << e.what() << std::endl;
  return 1;
} catch (...) {
  std::cerr << "UNKNOWN ERROR" << std::endl;
  return 1;
}