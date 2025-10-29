#pragma once
#include <vkw/Allocation.hpp>
#include <vkw/Device.hpp>

namespace imvk::examples {

struct DeviceCreateInfo {
  bool enableValidation;
};

class Validation;
class HostAllocator;

// Loads vulkan library, creates vulkan instance and
// picks suitable device.
// Prints message upon destruction, which manifests successful
// app termination.
class Device final {
public:
  Device(const DeviceCreateInfo &CI);

  vkw::Device &get() { return m_device; }
  vkw::DeviceAllocator &getAllocator() { return *m_allocator; }
  ~Device();

private:
  struct ExitPrinter {
    void operator()(Device *device);
  };
  std::unique_ptr<Device, ExitPrinter> m_exitPrinter;
  std::unique_ptr<HostAllocator> m_hostAlloc;
  vkw::Library m_vkLib;
  vkw::Instance m_instance;
  vkw::Device m_device;
  std::unique_ptr<vkw::DeviceAllocator> m_allocator;
  std::unique_ptr<Validation> m_validation;
};

} // namespace imvk::examples