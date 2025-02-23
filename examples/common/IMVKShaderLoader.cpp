#include "IMVKShaderLoader.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace imvk::examples {

ShaderLoader::ShaderLoader(const ShaderLoaderCreateInfo &CI)
    : m_shaderDir(CI.shaderDirectory) {}

std::shared_ptr<vkw::SPIRVModule>
ShaderLoader::getModule(std::string_view name) {
  auto shaderPath = m_shaderDir / name / ".spv";

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

  std::ifstream is{shaderPath.c_str()};

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

  return std::make_shared<vkw::SPIRVModule>(code);
}
} // namespace imvk::examples