#include "IMVKWindow.hpp"

#include <algorithm>

namespace imvk::examples {

std::unordered_map<GLFWwindow *, Window *> Window::m_windowMap;

class GLFWError : public std::runtime_error {
public:
  GLFWError()
      : std::runtime_error([&]() {
          const char *message;
          glfwGetError(&message);
          return message;
        }()){};
};

namespace {

class GLFWKeeper final {
public:
  GLFWKeeper() {
    auto code = glfwInit();
    if (code != GLFW_TRUE)
      throw GLFWError();
  }
  GLFWKeeper(const GLFWKeeper &) = delete;
  GLFWKeeper(GLFWKeeper &&) = delete;
  GLFWKeeper &operator=(const GLFWKeeper &) = delete;
  GLFWKeeper &operator=(GLFWKeeper &&) = delete;

  ~GLFWKeeper() { glfwTerminate(); }
};

void init() { static GLFWKeeper keeper; }

} // namespace

Window::Window(const WindowSettings &settings, const vkw::Instance &instance)
    : m_handle([&]() {
        init();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        auto *window = glfwCreateWindow(settings.width, settings.height,
                                        std::string(settings.title).data(),
                                        nullptr, nullptr);
        if (!window)
          throw GLFWError();
        return window;
      }()),
      m_surface(instance, [&]() {
        VkSurfaceKHR ret;
        glfwCreateWindowSurface(instance, m_handle.get(), NULL, &ret);
        return ret;
      }()) {
  glfwSetKeyCallback(m_handle.get(), m_key_callback);
  glfwSetCharCallback(m_handle.get(), m_char_callback);
  glfwSetMouseButtonCallback(m_handle.get(), m_mouse_button_callback);
  glfwSetCursorPosCallback(m_handle.get(), m_cursor_position_callback);
  glfwSetFramebufferSizeCallback(m_handle.get(), m_framebuffer_size_callback);
  glfwSetScrollCallback(m_handle.get(), m_mouse_scroll_callback);
  m_windowMap.emplace(m_handle.get(), this);
}

bool Window::shouldClose() const {
  return glfwWindowShouldClose(m_handle.get());
}

void Window::pollEvents() { glfwPollEvents(); }

std::vector<std::string> Window::surfaceExtensions() {
  init();
  std::vector<std::string> ret;
  uint32_t count = 0;
  auto *exts = glfwGetRequiredInstanceExtensions(&count);

  for (auto i = 0u; i < count; ++i)
    ret.emplace_back(exts[i]);
  return ret;
}

void Window::Disposer::operator()(GLFWwindow *handle) {
  glfwDestroyWindow(handle);
}
void Window::m_key_callback(GLFWwindow *handle, int key, int scancode,
                            int action, int mods) {
  auto *window = m_windowMap.at(handle);
  auto &callbacks = window->m_keyDownCallbacks;
  for (auto &callback : callbacks)
    std::invoke(callback, key, scancode, action, mods);
}
void Window::m_char_callback(GLFWwindow *handle, unsigned unicode) {
  auto *window = m_windowMap.at(handle);
  auto &callbacks = window->m_charEventCallbacks;
  for (auto &callback : callbacks)
    std::invoke(callback, unicode);
}

void Window::m_cursor_position_callback(GLFWwindow *handle, double xpos,
                                        double ypos) {
  auto *window = m_windowMap.at(handle);
  double deltaX = xpos - window->m_lastPos.first;
  double deltaY = ypos - window->m_lastPos.second;
  window->m_lastPos = std::make_pair(xpos, ypos);
  auto &callbacks = window->m_mouseMoveCallbacks;
  for (auto &callback : callbacks)
    std::invoke(callback, xpos, ypos, deltaX, deltaY);
}

void Window::m_mouse_scroll_callback(GLFWwindow *handle, double xoffset,
                                     double yoffset) {
  auto *window = m_windowMap.at(handle);
  auto &callbacks = window->m_mouseScrollCallbacks;
  for (auto &callback : callbacks)
    std::invoke(callback, xoffset, yoffset);
}

Window::~Window() { m_windowMap.erase(m_handle.get()); }
void Window::disableCursor() {
  if (!m_cursor_enabled)
    return;
  glfwSetInputMode(m_handle.get(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

  m_cursor_enabled = false;
}

void Window::enableCursor() {
  if (m_cursor_enabled)
    return;
  glfwSetInputMode(m_handle.get(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

  m_cursor_enabled = true;
}
void Window::m_framebuffer_size_callback(GLFWwindow *handle, int width,
                                         int height) {
  auto *window = m_windowMap.at(handle);

  auto &callbacks = window->m_windowResizeCallbacks;
  for (auto &callback : callbacks)
    std::invoke(callback, width, height);
}
std::pair<int, int> Window::getSize() const {
  int width, height;
  glfwGetFramebufferSize(m_handle.get(), &width, &height);
  return std::make_pair(width, height);
}
void Window::m_mouse_button_callback(GLFWwindow *handle, int button, int action,
                                     int mods) {
  auto *window = m_windowMap.at(handle);
  auto &callbacks = window->m_mouseButtonEventCallbacks;
  for (auto &callback : callbacks)
    std::invoke(callback, button, action, mods);
}

const VkSwapchainCreateInfoKHR &Window::getCreateInfo(vkw::Device &device) {
  auto &CI = m_swapchainCreateInfo;
  CI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  CI.pNext = nullptr;
  CI.surface = m_surface;
  CI.queueFamilyIndexCount = 0;
  CI.pQueueFamilyIndices = nullptr;
  auto caps = m_surface.getSurfaceCapabilities(device.physicalDevice());
  CI.imageExtent = caps.maxImageExtent;
  CI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  CI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  CI.imageArrayLayers = 1;
  CI.minImageCount = caps.minImageCount;
  CI.imageFormat =
      m_surface.getAvailableFormats(device.physicalDevice()).front().format;
  CI.imageColorSpace =
      m_surface.getAvailableFormats(device.physicalDevice()).front().colorSpace;
  auto presentModes =
      m_surface.getAvailablePresentModes(device.physicalDevice());
  auto presentModeCount = presentModes.size();

  if (std::any_of(presentModes.begin(), presentModes.end(), [](auto mode) {
        return mode == VK_PRESENT_MODE_MAILBOX_KHR;
      }))
    CI.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
  else if (std::any_of(presentModes.begin(), presentModes.end(), [](auto mode) {
             return mode == VK_PRESENT_MODE_IMMEDIATE_KHR;
           }))
    CI.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
  else
    CI.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  // FIXME: add configurable vsync
  if (false /*vsync*/)
    CI.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  // Find a supported composite alpha format (not all devices
  // support alpha opaque)
  VkCompositeAlphaFlagBitsKHR compositeAlpha =
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  // Simply select the first composite alpha format available
  std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
  };
  for (auto &compositeAlphaFlag : compositeAlphaFlags) {
    if (caps.supportedCompositeAlpha & compositeAlphaFlag) {
      compositeAlpha = compositeAlphaFlag;
      break;
    };
  }

  CI.compositeAlpha = compositeAlpha;
  CI.preTransform = caps.currentTransform;
  CI.clipped = VK_FALSE;

  return m_swapchainCreateInfo;
}

} // namespace imvk::examples