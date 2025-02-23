#pragma once
#include <vkw/Device.hpp>

namespace imvk::examples {

struct DeviceCreateInfo {
  bool enableValidation;
};

class Validation;

// Loads vulkan library, creates vulkan instance and
// picks suitable device.
// Prints message upon destruction, which manifests successful
// app termination.
class Device final {
public:
  Device(const DeviceCreateInfo &CI);

  auto &get() { return m_device; }

  ~Device();

private:
  struct ExitPrinter {
    void operator()(Device *device);
  };
  std::unique_ptr<Device, ExitPrinter> m_exitPrinter;
  vkw::Library m_vkLib;
  vkw::Instance m_instance;
  vkw::Device m_device;
  std::unique_ptr<Validation> m_validation;
};

} // namespace imvk::examples