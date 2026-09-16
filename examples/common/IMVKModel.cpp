#include "IMVKModel.hpp"
#include "IMVKTexture.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace imvk::examples {

static std::string_view errorCodeName(tg3_error_code code) {
  switch (code) {
  case TG3_OK:
    return "TG3_OK";
  case TG3_ERR_FILE_NOT_FOUND:
    return "TG3_ERR_FILE_NOT_FOUND";
  case TG3_ERR_FILE_READ:
    return "TG3_ERR_FILE_READ";
  case TG3_ERR_FILE_WRITE:
    return "TG3_ERR_FILE_WRITE";
  case TG3_ERR_FILE_TOO_LARGE:
    return "TG3_ERR_FILE_TOO_LARGE";
  case TG3_ERR_JSON_PARSE:
    return "TG3_ERR_JSON_PARSE";
  case TG3_ERR_JSON_TYPE_MISMATCH:
    return "TG3_ERR_JSON_TYPE_MISMATCH";
  case TG3_ERR_JSON_MISSING_FIELD:
    return "TG3_ERR_JSON_MISSING_FIELD";
  case TG3_ERR_JSON_INVALID_VALUE:
    return "TG3_ERR_JSON_INVALID_VALUE";
  case TG3_ERR_GLB_INVALID_MAGIC:
    return "TG3_ERR_GLB_INVALID_MAGIC";
  case TG3_ERR_GLB_INVALID_VERSION:
    return "TG3_ERR_GLB_INVALID_VERSION";
  case TG3_ERR_GLB_INVALID_HEADER:
    return "TG3_ERR_GLB_INVALID_HEADER";
  case TG3_ERR_GLB_CHUNK_ERROR:
    return "TG3_ERR_GLB_CHUNK_ERROR";
  case TG3_ERR_GLB_SIZE_MISMATCH:
    return "TG3_ERR_GLB_SIZE_MISMATCH";
  case TG3_ERR_MISSING_REQUIRED:
    return "TG3_ERR_MISSING_REQUIRED";
  case TG3_ERR_INVALID_INDEX:
    return "TG3_ERR_INVALID_INDEX";
  case TG3_ERR_INVALID_TYPE:
    return "TG3_ERR_INVALID_TYPE";
  case TG3_ERR_INVALID_VALUE:
    return "TG3_ERR_INVALID_VALUE";
  case TG3_ERR_INVALID_ACCESSOR:
    return "TG3_ERR_INVALID_ACCESSOR";
  case TG3_ERR_INVALID_BUFFER:
    return "TG3_ERR_INVALID_BUFFER";
  case TG3_ERR_INVALID_BUFFER_VIEW:
    return "TG3_ERR_INVALID_BUFFER_VIEW";
  case TG3_ERR_INVALID_IMAGE:
    return "TG3_ERR_INVALID_IMAGE";
  case TG3_ERR_INVALID_MATERIAL:
    return "TG3_ERR_INVALID_MATERIAL";
  case TG3_ERR_INVALID_MESH:
    return "TG3_ERR_INVALID_MESH";
  case TG3_ERR_INVALID_NODE:
    return "TG3_ERR_INVALID_NODE";
  case TG3_ERR_INVALID_ANIMATION:
    return "TG3_ERR_INVALID_ANIMATION";
  case TG3_ERR_INVALID_SKIN:
    return "TG3_ERR_INVALID_SKIN";
  case TG3_ERR_INVALID_CAMERA:
    return "TG3_ERR_INVALID_CAMERA";
  case TG3_ERR_INVALID_SCENE:
    return "TG3_ERR_INVALID_SCENE";
  case TG3_ERR_BUFFER_SIZE_MISMATCH:
    return "TG3_ERR_BUFFER_SIZE_MISMATCH";
  case TG3_ERR_OUT_OF_MEMORY:
    return "TG3_ERR_OUT_OF_MEMORY";
  case TG3_ERR_DATA_URI_DECODE:
    return "TG3_ERR_DATA_URI_DECODE";
  case TG3_ERR_BASE64_DECODE:
    return "TG3_ERR_BASE64_DECODE";
  case TG3_ERR_EXTERNAL_RESOURCE:
    return "TG3_ERR_EXTERNAL_RESOURCE";
  case TG3_ERR_IMAGE_DECODE:
    return "TG3_ERR_IMAGE_DECODE";
  case TG3_ERR_CALLBACK_FAILED:
    return "TG3_ERR_CALLBACK_FAILED";
  case TG3_ERR_FS_NOT_AVAILABLE:
    return "TG3_ERR_FS_NOT_AVAILABLE";
  case TG3_ERR_STREAM_ABORTED:
    return "TG3_ERR_STREAM_ABORTED";
  case TG3_ERR_WRITE_FAILED:
    return "TG3_ERR_WRITE_FAILED";
  case TG3_ERR_SERIALIZE_FAILED:
    return "TG3_ERR_SERIALIZE_FAILED";
  }
  return "unknown error code";
}

static std::string_view severityName(tg3_severity severity) {
  switch (severity) {
  case TG3_SEVERITY_INFO:
    return "info";
  case TG3_SEVERITY_WARNING:
    return "warning";
  case TG3_SEVERITY_ERROR:
    return "error";
  }
  return "unknown severity";
}

static std::optional<std::vector<uint8_t>>
readFile(const std::filesystem::path &path) {
  auto file =
      std::ifstream{path.c_str(), std::ios_base::binary | std::ios_base::ate};
  if (!file)
    return std::nullopt;
  auto size = file.tellg();
  file.seekg(0, std::ios_base::beg);
  std::vector<uint8_t> ret;
  ret.resize(size);
  file.read(reinterpret_cast<char *>(ret.data()), size);
  if (!file)
    return std::nullopt;
  return ret;
}

GLTFModel::GLTFModel(std::string_view name) {
  auto path = assetsDir() / name / "glTF" / name;
  auto extensions = std::array{".gltf", ".glb"};
  for (auto &&ext : extensions) {
    auto fullPath = path;
    fullPath += ext;
    if (!std::filesystem::is_regular_file(fullPath))
      continue;
    auto dataOpt = readFile(fullPath);
    if (dataOpt) {
      m_init(*dataOpt, fullPath);
      return;
    }
  }
  std::stringstream ss;
  ss << "Could not find '" << name
     << "' model file in asset directory: " << assetsDir() << std::endl;
  throw std::runtime_error(ss.str());
}
void GLTFModel::m_init(std::span<const uint8_t> data,
                       const std::filesystem::path &sourcePath) {
  tg3_parse_options opts;
  tinygltf3::ErrorStack errorStack;
  auto dir = sourcePath.parent_path();
  std::string str;
  std::wstring wpath = sourcePath.parent_path();
  std::string ascii_path{wpath.begin(), wpath.end()};
  auto errCode = tinygltf3::parse(m_handle, errorStack, data.data(),
                                  data.size(), ascii_path.c_str());
  if (errCode != TG3_OK || errorStack.has_error()) {
    std::stringstream ss;
    ss << "Failed to parse glTF model: " << sourcePath << "\n"
       << "TinyGLTF result: " << errorCodeName(errCode) << " ("
       << static_cast<int>(errCode) << ").\n";

    if (errorStack.count() == 0) {
      ss << "TinyGLTF did not provide any additional diagnostics.";
    } else {
      ss << "TinyGLTF diagnostics (" << errorStack.count() << "):";
      for (uint32_t i = 0; i < errorStack.count(); ++i) {
        const auto *entry = errorStack.entry(i);
        if (!entry) {
          ss << "\n  " << i + 1 << ". unavailable diagnostic entry";
          continue;
        }

        ss << "\n  " << i + 1 << ". " << severityName(entry->severity) << " ["
           << errorCodeName(entry->code) << " ("
           << static_cast<int>(entry->code) << ")] "
           << (entry->message ? entry->message : "no message provided");
        if (entry->json_path && entry->json_path[0] != '\0')
          ss << "\n     JSON path: " << entry->json_path;
        if (entry->byte_offset >= 0)
          ss << "\n     Byte offset: " << entry->byte_offset;
      }
    }
    throw std::runtime_error(ss.str());
  }
}
} // namespace imvk::examples