
#include "tiny_gltf_v3.h"

#include <filesystem>
#include <span>
#include <string_view>

namespace imvk::examples {
class GLTFModel {
public:
  GLTFModel(std::string_view name);

private:
  void m_init(std::span<const uint8_t> data,
              const std::filesystem::path &sourcePath);
  tinygltf3::Model m_handle;
};
} // namespace imvk::examples