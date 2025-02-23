#pragma once
#include "vkw/SPIRVModule.hpp"
#include <memory>
#include <string_view>

namespace imvk {

class ShaderFactory {
public:
  /// @brief Get shader module with specified name. It may be called mutiple
  /// times for same name, so caching them somewhere is a good idea.
  /// IMPORTANT: this procedure may be called asynchronously from multiple
  /// thread, and therefore must be internally synchronized.
  virtual std::shared_ptr<vkw::SPIRVModule>
  getModule(std::string_view name) = 0;

  virtual ~ShaderFactory() = default;
};

} // namespace imvk