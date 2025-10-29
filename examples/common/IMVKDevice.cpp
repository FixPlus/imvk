#include "IMVKDevice.hpp"
#include "IMVKWindow.hpp"
#include "vkw/Layers.hpp"
#include "vkw/Validation.hpp"
#include <stdlib.h>

#include <iostream>

namespace imvk::examples {

void Device::ExitPrinter::operator()(Device *device) {
  std::cout << "App exited successfully" << std::endl;
}

/// TODO: fix, this does not work.
class HostAllocator : public vkw::HostAllocator {
public:
  HostAllocator() : vkw::HostAllocator() {}
  virtual void *allocate(size_t size, size_t alignment,
                         VkSystemAllocationScope scope) noexcept {
    char *zero = nullptr;
    char *buf = new char[size + alignment + sizeof(uintptr_t)];
    uintptr_t ibuf = buf - zero;
    auto start = ibuf + sizeof(uintptr_t);
    start += (start % alignment) == 0 ? 0 : alignment - (start % alignment);
    auto origLoc = start - sizeof(uintptr_t);
    char *origLocPtr;
    memcpy(&origLocPtr, &origLoc, sizeof(uintptr_t));
    memcpy(origLocPtr, &buf, sizeof(uintptr_t));
    char *ret;
    memcpy(&ret, &start, sizeof(uintptr_t));
    return ret;
  }

  virtual void *reallocate(void *original, size_t size, size_t alignment,
                           VkSystemAllocationScope scope) noexcept {
    free(original);
    return allocate(size, alignment, scope);
  }

  virtual void free(void *memory) noexcept {
    char *origAddrLoc = reinterpret_cast<char *>(memory) - sizeof(uintptr_t);
    char *origAddr = *reinterpret_cast<char **>(origAddrLoc);
    delete[] origAddr;
  }
};

class Validation : vkw::debug::Validation {
public:
  explicit Validation(vkw::Instance &instance)
      : vkw::debug::Validation{instance, onValidationMessage} {};

  static void onValidationMessage(vkw::debug::MsgSeverity severity,
                                  vkw::debug::MsgType type,
                                  vkw::debug::Message const &message) {
    std::string severityStr;
    switch (severity) {
    case vkw::debug::MsgSeverity::Error:
      severityStr = "ERROR";
      break;
    case vkw::debug::MsgSeverity::Warning:
      severityStr = "WARNING";
      break;
    case vkw::debug::MsgSeverity::Info:
      severityStr = "INFO";
      break;
    case vkw::debug::MsgSeverity::Verbose:
      severityStr = "VERBOSE";
      break;
    }
    auto &stream =
        severity >= vkw::debug::MsgSeverity::Warning ? std::cerr : std::cout;
    stream << "[" << severityStr << "] " << message.name << "(" << message.id
           << "): " << message.what << std::endl;
    if (severity == vkw::debug::MsgSeverity::Error)
      throw std::runtime_error("Vulkan validation error");
  }
};

Device::Device(const DeviceCreateInfo &CI)
    : m_exitPrinter(this), m_hostAlloc(std::make_unique<HostAllocator>()),
      m_vkLib(nullptr),
      m_instance(m_vkLib,
                 [&]() {
                   if (m_vkLib.instanceAPIVersion() < vkw::ApiVersion{1, 2, 0})
                     throw std::runtime_error(
                         "Unsupported vulkan version. Required minimum: 1.2");
                   vkw::InstanceCreateInfo ICI;
                   ICI.apiVersion = vkw::ApiVersion{1, 2, 0};
                   auto surfaceExts = Window::surfaceExtensions();
                   for (auto &ext : surfaceExts)
                     ICI.requestExtension(vkw::Library::ExtensionId(ext));
                   if (CI.enableValidation &&
                       m_vkLib.hasLayer(vkw::layer::KHRONOS_validation)) {
                     ICI.requestLayer(vkw::layer::KHRONOS_validation);
                     ICI.requestExtension(vkw::ext::EXT_debug_utils);
                   }
                   return ICI;
                 }()),
      m_device(
          m_instance,
          [&]() {
            auto available = vkw::PhysicalDevice::enumerate(m_instance);
            if (available.empty())
              throw std::runtime_error("No available GPUs found");
            for (auto &&dev : available) {
              if (dev.supportedApiVersion() < vkw::ApiVersion{1, 2, 0})
                continue;
              if (!dev.extensionSupported(vkw::ext::KHR_swapchain))
                continue;
              dev.enableExtension(vkw::ext::KHR_swapchain);
              auto neededQueue =
                  std::ranges::find_if(dev.queueFamilies(), [&](auto &fam) {
                    return fam.graphics() && fam.transfer() && fam.compute();
                  });
              if (neededQueue == dev.queueFamilies().end())
                continue;
              neededQueue->requestQueue();
              return std::move(dev);
            }
            throw std::runtime_error("No suitable physical devices");
          }()),
      m_allocator(vkw::DeviceAllocator::createDefault(m_device)) {
  if (m_instance.isLayerEnabled(vkw::layer::KHRONOS_validation)) {
    std::cout << "Validation enabled" << std::endl;
    m_validation = std::make_unique<Validation>(m_instance);
  }
  vkw::addIrrecoverableErrorCallback([](const vkw::Error &e) {
    std::cerr << "[FATAL ERROR]: " << e.what() << std::endl;
  });
}

Device::~Device() = default;
} // namespace imvk::examples