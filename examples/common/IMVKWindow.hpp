#pragma once
#include "imvk/graphics/Engine.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>

using namespace std::chrono_literals;

namespace imvk::examples {

struct WindowSettings {
  std::string_view title;
  unsigned width;
  unsigned height;
};

class FrameClock {
public:
  using Milliseconds = std::chrono::duration<double, std::milli>;
  FrameClock() { tStart = std::chrono::high_resolution_clock::now(); }

  void frame() {
    tFinish = std::chrono::high_resolution_clock::now();
    auto frameTime =
        std::chrono::duration<double, std::milli>(tFinish - tStart);
    tStart = tFinish;
    m_total_time += frameTime;
    m_frames_elapsed++;
    m_time_elapsed += frameTime;
    if (m_time_elapsed > 1000ms) {
      m_fps = (double)m_frames_elapsed * 1000.0f / m_time_elapsed.count();
      m_time_elapsed = 0ms;
      m_frames_elapsed = 0u;
    }
    m_frame_times_log.push_back(frameTime);
    if (m_frame_times_log.size() > m_logLength)
      m_frame_times_log.pop_front();
    ++m_totalFrames;
  }

  Milliseconds currentFrameTime() const {
    auto cur = std::chrono::high_resolution_clock::now();
    return {cur - tStart};
  }

  Milliseconds frameTime() const { return m_frame_times_log.back(); }
  auto &frameTimeLog() const { return m_frame_times_log; }

  double fps() const { return m_fps; }

  auto totalFrames() const { return m_totalFrames; }
  Milliseconds totalTime() const { return m_total_time; }

private:
  std::chrono::time_point<std::chrono::high_resolution_clock,
                          std::chrono::duration<double>>
      tStart, tFinish;
  std::deque<Milliseconds> m_frame_times_log;
  size_t m_logLength = 200u;
  double m_fps = 0.0;
  Milliseconds m_total_time = 0ms;
  uint64_t m_totalFrames = 0ull;
  uint32_t m_frames_elapsed = 0u;
  Milliseconds m_time_elapsed = 0ms;
};

class Window : public imvk::SwapchainFactory {
public:
  using KeyEventCallback = std::function<void(int, int, int, int)>;
  using CharEventCallback = std::function<void(unsigned)>;
  using MouseButtonEventCallback = std::function<void(int, int, int)>;
  using MouseMoveEventCallback =
      std::function<void(double, double, double, double)>;
  using MouseScrollEventCallback = std::function<void(double, double)>;
  using WindowResizeCallback = std::function<void(int, int)>;
  explicit Window(const WindowSettings &settings,
                  const vkw::Instance &instance);

  Window(Window const &another) = delete;
  Window(Window &&another) = delete;
  Window &operator=(Window const &another) = delete;
  Window &operator=(Window &&another) = delete;

  static std::vector<std::string> surfaceExtensions();
  bool shouldClose() const;

  void pollEvents();

  const auto &clock() const { return m_clock; }

  bool cursorEnabled() const { return m_cursor_enabled; }

  void disableCursor();
  void enableCursor();
  void toggleCursor() {
    if (cursorEnabled())
      disableCursor();
    else
      enableCursor();
  }

  void addKeyCallback(KeyEventCallback &&keyEvent) {
    m_keyDownCallbacks.emplace_back(std::move(keyEvent));
  }
  void addCharCallback(CharEventCallback &&charEvent) {
    m_charEventCallbacks.emplace_back(std::move(charEvent));
  }

  void addMouseButtonEventCallback(
      MouseButtonEventCallback &&mouseButtonEventCallback) {
    m_mouseButtonEventCallbacks.emplace_back(
        std::move(mouseButtonEventCallback));
  }
  void addMouseMoveCallback(MouseMoveEventCallback &&mouseMoveEventCallback) {
    m_mouseMoveCallbacks.emplace_back(std::move(mouseMoveEventCallback));
  }
  void
  addMouseScrollCallback(MouseScrollEventCallback &&mouseScrollEventCallback) {
    m_mouseScrollCallbacks.emplace_back(std::move(mouseScrollEventCallback));
  }
  void addWindowResizeCallback(WindowResizeCallback &&windowResizeCallback) {
    m_windowResizeCallbacks.emplace_back(std::move(windowResizeCallback));
  }

  std::pair<int, int> getSize() const;

  const VkSwapchainCreateInfoKHR &getCreateInfo(vkw::Device &device) override;

  vkw::Surface &getSurface() noexcept override { return m_surface; }

  void setRecreateCallback(RecreateCallbackType callback) noexcept override {
    m_recreateCallback = callback;
  }

  virtual ~Window();

private:
  struct Disposer {
    void operator()(GLFWwindow *handle);
  };

  static void m_key_callback(GLFWwindow *window, int key, int scancode,
                             int action, int mods);
  static void m_char_callback(GLFWwindow *window, unsigned unicode);
  static void m_mouse_button_callback(GLFWwindow *window, int button,
                                      int action, int mods);
  static void m_mouse_scroll_callback(GLFWwindow *window, double xoffset,
                                      double yoffset);
  static void m_cursor_position_callback(GLFWwindow *window, double xpos,
                                         double ypos);

  static void m_framebuffer_size_callback(GLFWwindow *window, int width,
                                          int height);
  std::unique_ptr<GLFWwindow, Disposer> m_handle;
  vkw::Surface m_surface;
  RecreateCallbackType m_recreateCallback = nullptr;
  VkSwapchainCreateInfoKHR m_swapchainCreateInfo{};
  std::vector<KeyEventCallback> m_keyDownCallbacks{};
  std::vector<CharEventCallback> m_charEventCallbacks{};
  std::vector<MouseButtonEventCallback> m_mouseButtonEventCallbacks{};
  std::vector<MouseMoveEventCallback> m_mouseMoveCallbacks{};
  std::vector<MouseScrollEventCallback> m_mouseScrollCallbacks{};
  std::vector<WindowResizeCallback> m_windowResizeCallbacks{};
  std::pair<double, double> m_lastPos;
  bool m_cursor_enabled = true;
  FrameClock m_clock;
  static std::unordered_map<GLFWwindow *, Window *> m_windowMap;
};

} // namespace imvk::examples