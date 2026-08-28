#pragma once

#include <vkw/SPIRVModule.hpp>

#include "imvk/base/Pipeline.hpp"
#include "imvk/base/Utils.hpp"

#include <filesystem>

namespace imvk::examples {

struct ShaderLoaderCreateInfo {
  std::filesystem::path shaderDirectory;
};

// Just loads a shader from file in specified directory. Does not
// cache them currently.
class ShaderLoader final {
public:
  ShaderLoader(FramedEngine &engine, const ShaderLoaderCreateInfo &CI);

  imvk::ShaderFragment getModule(std::string_view name);

private:
  FramedEngine &m_engine;
  imvk::Cache<std::string, imvk::ShaderFragment, CachePolicy::LRU>
      m_shaderCache;
  std::filesystem::path m_shaderDir;
};

} // namespace imvk::examples