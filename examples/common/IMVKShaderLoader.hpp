#pragma once

#include <vkw/SPIRVModule.hpp>

#include <filesystem>

namespace imvk::examples {

struct ShaderLoaderCreateInfo {
  std::filesystem::path shaderDirectory;
};

// Just loads a shader from file in specified directory. Does not
// cache them currently.
class ShaderLoader final {
public:
  ShaderLoader(const ShaderLoaderCreateInfo &CI);

  std::shared_ptr<vkw::SPIRVModule> getModule(std::string_view name);

private:
  std::filesystem::path m_shaderDir;
};

} // namespace imvk::examples