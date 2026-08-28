#include "IMVKShaderLoader.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace imvk::examples {

ShaderLoader::ShaderLoader(FramedEngine &engine,
                           const ShaderLoaderCreateInfo &CI)
    : m_engine(engine), m_shaderDir(CI.shaderDirectory), m_shaderCache(20) {}

imvk::ShaderFragment ShaderLoader::getModule(std::string_view name) {
  auto key = std::string(name);
  auto &ret = m_shaderCache.get(key, nullptr);
  if (ret) {
    return ret;
  }
  auto shaderPath = m_shaderDir / (std::string(name) + ".spv");

  if (!std::filesystem::exists(shaderPath))
    throw std::runtime_error([&]() {
      std::stringstream ss;
      ss << "Could not load shader '" << name << "':\n";
      ss << "no such file: " << shaderPath << "\n";
      return ss.str();
    }());
  if (!std::filesystem::is_regular_file(shaderPath))
    throw std::runtime_error([&]() {
      std::stringstream ss;
      ss << "Could not load shader '" << name << "':\n";
      ss << "is not a regular file: " << shaderPath << "\n";
      return ss.str();
    }());
  auto codeSize = std::filesystem::file_size(shaderPath);
  if (codeSize % 4)
    throw std::runtime_error([&]() {
      std::stringstream ss;
      ss << "Could not load shader '" << name << "':\n";
      ss << "file size is not multiple of 4 bytes: " << codeSize << "\n";
      return ss.str();
    }());

  std::ifstream is{shaderPath.c_str(), std::ios::binary};

  if (!is)
    throw std::runtime_error([&]() {
      std::stringstream ss;
      ss << "Could not load shader '" << name << "':\n";
      ss << "failed to open file for reading\n";
      return ss.str();
    }());
  std::vector<unsigned> code;
  code.resize(codeSize / 4);

  is.read(reinterpret_cast<char *>(code.data()), codeSize);
  if (is.bad())
    throw std::runtime_error([&]() {
      std::stringstream ss;
      ss << "Could not load shader '" << name << "':\n";
      ss << "error occured while reading " << codeSize << " bytes";
      return ss.str();
    }());

  ret = imvk::ShaderFragment(m_engine, vkw::SPIRVModule{code});
  return ret;
}
} // namespace imvk::examples