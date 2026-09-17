#include "IMVKModel.hpp"
#include "IMVKBuffers.hpp"
#include "IMVKShaderLoader.hpp"
#include "IMVKTexture.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace imvk::examples {

static std::optional<std::vector<uint8_t>>
readFile(const std::filesystem::path &path);

namespace {

struct ModelVertex
    : vkw::AttributeBase<
          vkw::VertexAttributeType::VEC3F, vkw::VertexAttributeType::VEC3F,
          vkw::VertexAttributeType::VEC4F, vkw::VertexAttributeType::VEC2F,
          vkw::VertexAttributeType::VEC4F> {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec4 tangent;
  glm::vec2 uv;
  glm::vec4 color;
};

static_assert(sizeof(ModelVertex) == sizeof(float) * 16);

struct MaterialData {
  glm::vec4 baseColorFactor{1.0f};
  glm::vec4 emissiveFactor{0.0f};
  glm::vec4 pbrFactors{1.0f, 1.0f, 1.0f, 1.0f};
  glm::vec4 alpha{0.0f, 0.5f, 0.0f, 0.0f};
};

struct CPUPrimitive {
  uint32_t firstIndex;
  uint32_t indexCount;
  int32_t material;
};

struct CPUModel {
  std::vector<ModelVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<CPUPrimitive> primitives;
};

[[noreturn]] void modelError(const std::filesystem::path &path,
                             std::string_view message) {
  std::stringstream ss;
  ss << "Failed to materialize glTF model " << path << ": " << message;
  throw std::runtime_error(ss.str());
}

template <typename T> T readUnaligned(const uint8_t *data) {
  T value;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

uint64_t checkedAdd(uint64_t left, uint64_t right,
                    const std::filesystem::path &path,
                    std::string_view context) {
  if (right > std::numeric_limits<uint64_t>::max() - left)
    modelError(path, std::string(context) + " byte offset overflows");
  return left + right;
}

uint64_t checkedMultiply(uint64_t left, uint64_t right,
                         const std::filesystem::path &path,
                         std::string_view context) {
  if (left != 0 && right > std::numeric_limits<uint64_t>::max() / left)
    modelError(path, std::string(context) + " byte size overflows");
  return left * right;
}

const uint8_t *bufferViewData(const tg3_model &model, int32_t bufferView,
                              uint64_t offset, uint64_t size,
                              const std::filesystem::path &path,
                              std::string_view context) {
  if (bufferView < 0 ||
      static_cast<uint32_t>(bufferView) >= model.buffer_views_count)
    modelError(path,
               std::string(context) + " references an invalid bufferView");
  const auto &view = model.buffer_views[bufferView];
  if (view.buffer < 0 ||
      static_cast<uint32_t>(view.buffer) >= model.buffers_count)
    modelError(path, std::string(context) + " references an invalid buffer");
  const auto &buffer = model.buffers[view.buffer];
  if (!buffer.data.data)
    modelError(path, std::string(context) + " references unloaded buffer data");
  if (offset > view.byte_length || size > view.byte_length - offset)
    modelError(path, std::string(context) + " exceeds its bufferView bounds");
  if (view.byte_offset > buffer.data.count ||
      offset > buffer.data.count - view.byte_offset ||
      size > buffer.data.count - view.byte_offset - offset)
    modelError(path, std::string(context) + " exceeds its buffer bounds");
  return buffer.data.data + view.byte_offset + offset;
}

double componentAsDouble(const uint8_t *data, int32_t componentType,
                         bool normalized, const std::filesystem::path &path) {
  switch (componentType) {
  case TG3_COMPONENT_TYPE_BYTE: {
    const auto value = readUnaligned<int8_t>(data);
    return normalized ? std::max(-1.0, static_cast<double>(value) / 127.0)
                      : value;
  }
  case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: {
    const auto value = readUnaligned<uint8_t>(data);
    return normalized ? static_cast<double>(value) / 255.0 : value;
  }
  case TG3_COMPONENT_TYPE_SHORT: {
    const auto value = readUnaligned<int16_t>(data);
    return normalized ? std::max(-1.0, static_cast<double>(value) / 32767.0)
                      : value;
  }
  case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: {
    const auto value = readUnaligned<uint16_t>(data);
    return normalized ? static_cast<double>(value) / 65535.0 : value;
  }
  case TG3_COMPONENT_TYPE_INT:
    return readUnaligned<int32_t>(data);
  case TG3_COMPONENT_TYPE_UNSIGNED_INT:
    return readUnaligned<uint32_t>(data);
  case TG3_COMPONENT_TYPE_FLOAT:
    return readUnaligned<float>(data);
  case TG3_COMPONENT_TYPE_DOUBLE:
    return readUnaligned<double>(data);
  default:
    modelError(path, "accessor has an unsupported component type");
  }
}

uint64_t sparseIndex(const uint8_t *data, int32_t componentType,
                     const std::filesystem::path &path) {
  switch (componentType) {
  case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
    return readUnaligned<uint8_t>(data);
  case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
    return readUnaligned<uint16_t>(data);
  case TG3_COMPONENT_TYPE_UNSIGNED_INT:
    return readUnaligned<uint32_t>(data);
  default:
    modelError(path, "sparse accessor has an invalid index component type");
  }
}

class AccessorReader {
public:
  AccessorReader(const tg3_model &model, int32_t accessorIndex,
                 const std::filesystem::path &path, std::string_view context)
      : m_model(model), m_path(path), m_context(context) {
    if (accessorIndex < 0 ||
        static_cast<uint32_t>(accessorIndex) >= model.accessors_count)
      modelError(path,
                 std::string(context) + " references an invalid accessor");
    m_accessor = &model.accessors[accessorIndex];
    m_componentSize = tg3_component_size(m_accessor->component_type);
    m_components = tg3_num_components(m_accessor->type);
    if (m_componentSize <= 0 || m_components <= 0)
      modelError(path,
                 std::string(context) + " has an invalid accessor format");
    m_elementSize = static_cast<uint64_t>(m_componentSize) * m_components;

    if (m_accessor->buffer_view >= 0) {
      if (static_cast<uint32_t>(m_accessor->buffer_view) >=
          model.buffer_views_count)
        modelError(path,
                   std::string(context) + " references an invalid bufferView");
      const auto &view = model.buffer_views[m_accessor->buffer_view];
      const auto stride = tg3_accessor_byte_stride(m_accessor, &view);
      if (stride < 0 || static_cast<uint64_t>(stride) < m_elementSize)
        modelError(path, std::string(context) + " has an invalid byte stride");
      m_stride = stride;
      if (m_accessor->count > 0) {
        const auto lastOffset = checkedAdd(
            m_accessor->byte_offset,
            checkedMultiply(m_accessor->count - 1, m_stride, path, context),
            path, context);
        bufferViewData(model, m_accessor->buffer_view, lastOffset,
                       m_elementSize, path, context);
      }
    }

    const auto &sparse = m_accessor->sparse;
    if (!sparse.is_sparse)
      return;
    if (sparse.count < 0 ||
        static_cast<uint64_t>(sparse.count) > m_accessor->count)
      modelError(path, std::string(context) + " has an invalid sparse count");
    const auto indexSize = tg3_component_size(sparse.indices.component_type);
    if (indexSize <= 0)
      modelError(path, std::string(context) + " has invalid sparse indices");
    for (int32_t i = 0; i < sparse.count; ++i) {
      const auto *indexData = bufferViewData(
          model, sparse.indices.buffer_view,
          checkedAdd(sparse.indices.byte_offset,
                     checkedMultiply(i, indexSize, path, context), path,
                     context),
          indexSize, path, context);
      const auto index =
          sparseIndex(indexData, sparse.indices.component_type, path);
      if (index >= m_accessor->count)
        modelError(path,
                   std::string(context) + " has an out-of-range sparse index");
      m_sparse[index] = i;
    }
    if (sparse.count > 0)
      bufferViewData(
          model, sparse.values.buffer_view, sparse.values.byte_offset,
          checkedMultiply(sparse.count, m_elementSize, path, context), path,
          context);
  }

  uint64_t count() const { return m_accessor->count; }
  int components() const { return m_components; }
  int componentType() const { return m_accessor->component_type; }
  int type() const { return m_accessor->type; }

  double read(uint64_t element, int component) const {
    if (element >= count() || component < 0 || component >= m_components)
      modelError(m_path, std::string(m_context) + " read is out of bounds");
    const uint8_t *data = nullptr;
    if (const auto sparse = m_sparse.find(element); sparse != m_sparse.end()) {
      data = bufferViewData(
          m_model, m_accessor->sparse.values.buffer_view,
          checkedAdd(
              m_accessor->sparse.values.byte_offset,
              checkedMultiply(sparse->second, m_elementSize, m_path, m_context),
              m_path, m_context),
          m_elementSize, m_path, m_context);
    } else if (m_accessor->buffer_view >= 0) {
      data = bufferViewData(
          m_model, m_accessor->buffer_view,
          checkedAdd(m_accessor->byte_offset,
                     checkedMultiply(element, m_stride, m_path, m_context),
                     m_path, m_context),
          m_elementSize, m_path, m_context);
    } else {
      return 0.0;
    }
    return componentAsDouble(data + component * m_componentSize,
                             m_accessor->component_type,
                             m_accessor->normalized != 0, m_path);
  }

private:
  const tg3_model &m_model;
  const std::filesystem::path &m_path;
  std::string m_context;
  const tg3_accessor *m_accessor = nullptr;
  int m_componentSize = 0;
  int m_components = 0;
  uint64_t m_elementSize = 0;
  uint64_t m_stride = 0;
  std::unordered_map<uint64_t, uint64_t> m_sparse;
};

int32_t attributeAccessor(const tg3_primitive &primitive, const char *name) {
  for (uint32_t i = 0; i < primitive.attributes_count; ++i)
    if (tg3_str_equals_cstr(primitive.attributes[i].key, name))
      return primitive.attributes[i].value;
  return -1;
}

glm::mat4 nodeTransform(const tg3_node &node) {
  if (node.has_matrix) {
    glm::mat4 result{1.0f};
    for (int column = 0; column < 4; ++column)
      for (int row = 0; row < 4; ++row)
        result[column][row] = static_cast<float>(node.matrix[column * 4 + row]);
    return result;
  }
  const glm::vec3 translation{node.translation[0], node.translation[1],
                              node.translation[2]};
  const glm::quat rotation{static_cast<float>(node.rotation[3]),
                           static_cast<float>(node.rotation[0]),
                           static_cast<float>(node.rotation[1]),
                           static_cast<float>(node.rotation[2])};
  const glm::vec3 scale{node.scale[0], node.scale[1], node.scale[2]};
  return glm::translate(glm::mat4{1.0f}, translation) *
         glm::mat4_cast(rotation) * glm::scale(glm::mat4{1.0f}, scale);
}

std::vector<uint32_t> triangleIndices(const tg3_model &model,
                                      const tg3_primitive &primitive,
                                      uint64_t vertexCount,
                                      const std::filesystem::path &path) {
  std::vector<uint32_t> source;
  if (primitive.indices >= 0) {
    AccessorReader reader{model, primitive.indices, path, "index accessor"};
    if (reader.type() != TG3_TYPE_SCALAR ||
        (reader.componentType() != TG3_COMPONENT_TYPE_UNSIGNED_BYTE &&
         reader.componentType() != TG3_COMPONENT_TYPE_UNSIGNED_SHORT &&
         reader.componentType() != TG3_COMPONENT_TYPE_UNSIGNED_INT))
      modelError(path, "primitive indices must be unsigned scalar values");
    source.reserve(static_cast<size_t>(reader.count()));
    for (uint64_t i = 0; i < reader.count(); ++i) {
      const auto value = static_cast<uint64_t>(reader.read(i, 0));
      if (value >= vertexCount || value > std::numeric_limits<uint32_t>::max())
        modelError(path, "primitive contains an out-of-range vertex index");
      source.push_back(static_cast<uint32_t>(value));
    }
  } else {
    if (vertexCount > std::numeric_limits<uint32_t>::max())
      modelError(path, "non-indexed primitive has too many vertices");
    source.resize(static_cast<size_t>(vertexCount));
    std::ranges::iota(source, 0u);
  }

  const auto mode = primitive.mode < 0 ? TG3_MODE_TRIANGLES : primitive.mode;
  if (mode == TG3_MODE_TRIANGLES) {
    if (source.size() % 3 != 0)
      modelError(path, "triangle primitive index count is not divisible by 3");
    return source;
  }

  std::vector<uint32_t> result;
  if (mode == TG3_MODE_TRIANGLE_STRIP) {
    if (source.size() < 3)
      return result;
    result.reserve((source.size() - 2) * 3);
    for (size_t i = 2; i < source.size(); ++i) {
      if ((i & 1u) == 0)
        result.insert(result.end(), {source[i - 2], source[i - 1], source[i]});
      else
        result.insert(result.end(), {source[i - 1], source[i - 2], source[i]});
    }
    return result;
  }
  if (mode == TG3_MODE_TRIANGLE_FAN) {
    if (source.size() < 3)
      return result;
    result.reserve((source.size() - 2) * 3);
    for (size_t i = 2; i < source.size(); ++i)
      result.insert(result.end(), {source[0], source[i - 1], source[i]});
    return result;
  }
  modelError(path, "points and line primitive modes are not supported");
}

glm::vec3 fallbackTangent(glm::vec3 normal) {
  const auto axis = std::abs(normal.z) < 0.999f ? glm::vec3{0.0f, 0.0f, 1.0f}
                                                : glm::vec3{0.0f, 1.0f, 0.0f};
  return glm::normalize(glm::cross(axis, normal));
}

void normalizeOrDefault(glm::vec3 &value, glm::vec3 fallback) {
  if (glm::dot(value, value) <= std::numeric_limits<float>::epsilon())
    value = fallback;
  else
    value = glm::normalize(value);
}

void generateNormals(std::vector<ModelVertex> &vertices,
                     std::span<const uint32_t> indices) {
  std::vector<glm::vec3> sums(vertices.size(), glm::vec3{0.0f});
  for (size_t i = 0; i < indices.size(); i += 3) {
    const auto i0 = indices[i];
    const auto i1 = indices[i + 1];
    const auto i2 = indices[i + 2];
    const auto normal =
        glm::cross(vertices[i1].position - vertices[i0].position,
                   vertices[i2].position - vertices[i0].position);
    sums[i0] += normal;
    sums[i1] += normal;
    sums[i2] += normal;
  }
  for (size_t i = 0; i < vertices.size(); ++i) {
    normalizeOrDefault(sums[i], {0.0f, 0.0f, 1.0f});
    vertices[i].normal = sums[i];
  }
}

void generateTangents(std::vector<ModelVertex> &vertices,
                      std::span<const uint32_t> indices) {
  std::vector<glm::vec3> tangentSums(vertices.size(), glm::vec3{0.0f});
  std::vector<glm::vec3> bitangentSums(vertices.size(), glm::vec3{0.0f});
  for (size_t i = 0; i < indices.size(); i += 3) {
    const auto i0 = indices[i];
    const auto i1 = indices[i + 1];
    const auto i2 = indices[i + 2];
    const auto edge1 = vertices[i1].position - vertices[i0].position;
    const auto edge2 = vertices[i2].position - vertices[i0].position;
    const auto deltaUV1 = vertices[i1].uv - vertices[i0].uv;
    const auto deltaUV2 = vertices[i2].uv - vertices[i0].uv;
    const auto denominator = deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x;
    if (std::abs(denominator) <= std::numeric_limits<float>::epsilon())
      continue;
    const auto factor = 1.0f / denominator;
    const auto tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * factor;
    const auto bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * factor;
    for (const auto index : {i0, i1, i2}) {
      tangentSums[index] += tangent;
      bitangentSums[index] += bitangent;
    }
  }

  for (size_t i = 0; i < vertices.size(); ++i) {
    auto tangent =
        tangentSums[i] -
        vertices[i].normal * glm::dot(vertices[i].normal, tangentSums[i]);
    normalizeOrDefault(tangent, fallbackTangent(vertices[i].normal));
    const auto handedness = glm::dot(glm::cross(vertices[i].normal, tangent),
                                     bitangentSums[i]) < 0.0f
                                ? -1.0f
                                : 1.0f;
    vertices[i].tangent = glm::vec4{tangent, handedness};
  }
}

void appendPrimitive(CPUModel &result, const tg3_model &model,
                     const tg3_primitive &primitive, const glm::mat4 &transform,
                     const std::filesystem::path &path) {
  const auto positionIndex = attributeAccessor(primitive, "POSITION");
  if (positionIndex < 0)
    modelError(path, "mesh primitive does not have a POSITION attribute");
  AccessorReader positions{model, positionIndex, path, "POSITION accessor"};
  if (positions.type() != TG3_TYPE_VEC3)
    modelError(path, "POSITION accessor must be VEC3");
  if (positions.count() > std::numeric_limits<size_t>::max())
    modelError(path, "POSITION accessor is too large for this platform");

  const auto makeOptionalReader = [&](const char *name) {
    const auto index = attributeAccessor(primitive, name);
    if (index < 0)
      return std::unique_ptr<AccessorReader>{};
    auto reader = std::make_unique<AccessorReader>(
        model, index, path, std::string{name} + " accessor");
    if (reader->count() != positions.count())
      modelError(path, std::string{name} +
                           " accessor count differs from POSITION count");
    return reader;
  };
  auto normals = makeOptionalReader("NORMAL");
  auto tangents = makeOptionalReader("TANGENT");
  auto texcoords = makeOptionalReader("TEXCOORD_0");
  auto colors = makeOptionalReader("COLOR_0");
  if (normals && normals->type() != TG3_TYPE_VEC3)
    modelError(path, "NORMAL accessor must be VEC3");
  if (tangents && tangents->type() != TG3_TYPE_VEC4)
    modelError(path, "TANGENT accessor must be VEC4");
  if (texcoords && texcoords->type() != TG3_TYPE_VEC2)
    modelError(path, "TEXCOORD_0 accessor must be VEC2");
  if (colors && colors->type() != TG3_TYPE_VEC3 &&
      colors->type() != TG3_TYPE_VEC4)
    modelError(path, "COLOR_0 accessor must be VEC3 or VEC4");
  if (tangents && !normals)
    tangents.reset();

  auto localIndices =
      triangleIndices(model, primitive, positions.count(), path);
  if (localIndices.size() > std::numeric_limits<uint32_t>::max())
    modelError(path, "primitive has too many indices for a Vulkan draw call");
  std::vector<ModelVertex> localVertices(
      static_cast<size_t>(positions.count()));
  const auto linearTransform = glm::mat3{transform};
  const auto determinant = glm::determinant(linearTransform);
  if (std::abs(determinant) <= std::numeric_limits<float>::epsilon())
    modelError(path, "mesh node has a singular transform");
  const auto normalTransform = glm::inverseTranspose(linearTransform);
  const auto mirrored = determinant < 0.0f ? -1.0f : 1.0f;
  if (mirrored < 0.0f)
    for (size_t i = 0; i < localIndices.size(); i += 3)
      std::swap(localIndices[i + 1], localIndices[i + 2]);
  for (uint64_t i = 0; i < positions.count(); ++i) {
    auto &vertex = localVertices[static_cast<size_t>(i)];
    vertex.position = glm::vec3{
        transform * glm::vec4{positions.read(i, 0), positions.read(i, 1),
                              positions.read(i, 2), 1.0f}};
    if (normals) {
      vertex.normal =
          normalTransform * glm::vec3{normals->read(i, 0), normals->read(i, 1),
                                      normals->read(i, 2)};
      normalizeOrDefault(vertex.normal, {0.0f, 0.0f, 1.0f});
    }
    if (tangents) {
      auto tangent = linearTransform * glm::vec3{tangents->read(i, 0),
                                                 tangents->read(i, 1),
                                                 tangents->read(i, 2)};
      normalizeOrDefault(tangent, fallbackTangent(vertex.normal));
      vertex.tangent = glm::vec4{
          tangent, static_cast<float>(tangents->read(i, 3)) * mirrored};
    }
    if (texcoords)
      vertex.uv = {texcoords->read(i, 0), texcoords->read(i, 1)};
    vertex.color =
        colors ? glm::vec4{colors->read(i, 0), colors->read(i, 1),
                           colors->read(i, 2),
                           colors->components() == 4 ? colors->read(i, 3) : 1.0}
               : glm::vec4{1.0f};
  }

  if (!normals)
    generateNormals(localVertices, localIndices);
  if (!tangents)
    generateTangents(localVertices, localIndices);

  if (result.vertices.size() + localVertices.size() >
      std::numeric_limits<uint32_t>::max())
    modelError(path, "model has too many vertices for 32-bit indices");
  if (result.indices.size() >
      std::numeric_limits<uint32_t>::max() - localIndices.size())
    modelError(path, "model has too many indices for 32-bit draw offsets");
  const auto vertexOffset = static_cast<uint32_t>(result.vertices.size());
  const auto firstIndex = static_cast<uint32_t>(result.indices.size());
  result.vertices.insert(result.vertices.end(), localVertices.begin(),
                         localVertices.end());
  result.indices.reserve(result.indices.size() + localIndices.size());
  for (const auto index : localIndices)
    result.indices.push_back(vertexOffset + index);
  result.primitives.push_back({firstIndex,
                               static_cast<uint32_t>(localIndices.size()),
                               primitive.material});
}

void traverseNode(CPUModel &result, const tg3_model &model, int32_t nodeIndex,
                  const glm::mat4 &parentTransform,
                  std::vector<bool> &recursionStack,
                  const std::filesystem::path &path) {
  if (nodeIndex < 0 || static_cast<uint32_t>(nodeIndex) >= model.nodes_count)
    modelError(path, "scene references an invalid node");
  if (recursionStack[nodeIndex])
    modelError(path, "scene graph contains a node cycle");
  recursionStack[nodeIndex] = true;
  const auto &node = model.nodes[nodeIndex];
  const auto transform = parentTransform * nodeTransform(node);
  if (node.mesh >= 0) {
    if (static_cast<uint32_t>(node.mesh) >= model.meshes_count)
      modelError(path, "node references an invalid mesh");
    const auto &mesh = model.meshes[node.mesh];
    for (uint32_t i = 0; i < mesh.primitives_count; ++i)
      appendPrimitive(result, model, mesh.primitives[i], transform, path);
  }
  for (uint32_t i = 0; i < node.children_count; ++i)
    traverseNode(result, model, node.children[i], transform, recursionStack,
                 path);
  recursionStack[nodeIndex] = false;
}

CPUModel buildCPUModel(const tg3_model &model,
                       const std::filesystem::path &path) {
  CPUModel result;
  std::vector<bool> recursionStack(model.nodes_count, false);
  if (model.scenes_count > 0) {
    const auto sceneIndex = model.default_scene >= 0 ? model.default_scene : 0;
    if (static_cast<uint32_t>(sceneIndex) >= model.scenes_count)
      modelError(path, "default scene index is invalid");
    const auto &scene = model.scenes[sceneIndex];
    for (uint32_t i = 0; i < scene.nodes_count; ++i)
      traverseNode(result, model, scene.nodes[i], glm::mat4{1.0f},
                   recursionStack, path);
  } else {
    std::vector<bool> childNodes(model.nodes_count, false);
    for (uint32_t i = 0; i < model.nodes_count; ++i)
      for (uint32_t j = 0; j < model.nodes[i].children_count; ++j) {
        const auto child = model.nodes[i].children[j];
        if (child < 0 || static_cast<uint32_t>(child) >= model.nodes_count)
          modelError(path, "node references an invalid child");
        childNodes[child] = true;
      }
    for (uint32_t i = 0; i < model.nodes_count; ++i)
      if (!childNodes[i])
        traverseNode(result, model, i, glm::mat4{1.0f}, recursionStack, path);
  }
  if (result.primitives.empty())
    modelError(path, "selected scene does not contain any mesh primitives");
  return result;
}

std::string stringFrom(const tg3_str &value) {
  return value.data ? std::string{value.data, value.len} : std::string{};
}

int base64Value(char c) {
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

std::vector<uint8_t> decodeBase64(std::string_view encoded,
                                  const std::filesystem::path &path) {
  std::vector<uint8_t> result;
  result.reserve(encoded.size() * 3 / 4);
  uint32_t accumulator = 0;
  unsigned bits = 0;
  bool padded = false;
  for (const auto c : encoded) {
    if (std::isspace(static_cast<unsigned char>(c)))
      continue;
    if (c == '=') {
      padded = true;
      continue;
    }
    if (padded)
      modelError(path, "image data URI has data after base64 padding");
    const auto value = base64Value(c);
    if (value < 0)
      modelError(path, "image data URI contains invalid base64 data");
    accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      result.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xffu));
    }
  }
  return result;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

std::vector<uint8_t> percentDecode(std::string_view encoded,
                                   const std::filesystem::path &path) {
  std::vector<uint8_t> result;
  result.reserve(encoded.size());
  for (size_t i = 0; i < encoded.size(); ++i) {
    if (encoded[i] != '%') {
      result.push_back(static_cast<uint8_t>(encoded[i]));
      continue;
    }
    if (i + 2 >= encoded.size())
      modelError(path, "URI ends in an incomplete percent escape");
    const auto high = hexValue(encoded[i + 1]);
    const auto low = hexValue(encoded[i + 2]);
    if (high < 0 || low < 0)
      modelError(path, "URI contains an invalid percent escape");
    result.push_back(static_cast<uint8_t>((high << 4) | low));
    i += 2;
  }
  return result;
}

std::vector<uint8_t> imageBytes(const tg3_model &model, const tg3_image &image,
                                const std::filesystem::path &path) {
  if (image.image.data && image.image.count > 0)
    return {image.image.data, image.image.data + image.image.count};
  if (image.buffer_view >= 0) {
    if (static_cast<uint32_t>(image.buffer_view) >= model.buffer_views_count)
      modelError(path, "image references an invalid bufferView");
    const auto size = model.buffer_views[image.buffer_view].byte_length;
    const auto *data = bufferViewData(model, image.buffer_view, 0, size, path,
                                      "image bufferView");
    return {data, data + size};
  }

  const auto uri = stringFrom(image.uri);
  if (uri.empty())
    modelError(path, "image has neither a URI nor a bufferView");
  if (tg3_is_data_uri(uri.data(), static_cast<uint32_t>(uri.size()))) {
    const auto comma = uri.find(',');
    if (comma == std::string::npos)
      modelError(path, "image data URI does not contain a payload separator");
    const auto metadata = std::string_view{uri}.substr(0, comma);
    const auto payload = std::string_view{uri}.substr(comma + 1);
    return metadata.ends_with(";base64") ? decodeBase64(payload, path)
                                         : percentDecode(payload, path);
  }

  const auto decodedUri = percentDecode(uri, path);
  const std::string relativePath{decodedUri.begin(), decodedUri.end()};
  const auto relative = std::filesystem::u8path(relativePath);
  const auto hasParentTraversal = std::ranges::any_of(
      relative, [](const auto &component) { return component == ".."; });
  if (relative.has_root_name() || relative.has_root_directory() ||
      hasParentTraversal)
    modelError(path, "image URI escapes the model directory");
  const auto imagePath = path.parent_path() / relative;
  auto data = readFile(imagePath);
  if (!data) {
    std::stringstream ss;
    ss << "could not read image " << imagePath;
    modelError(path, ss.str());
  }
  return std::move(*data);
}

VkSamplerAddressMode addressMode(int32_t mode,
                                 const std::filesystem::path &path) {
  switch (mode) {
  case TG3_TEXTURE_WRAP_REPEAT:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case TG3_TEXTURE_WRAP_CLAMP_TO_EDGE:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  case TG3_TEXTURE_WRAP_MIRRORED_REPEAT:
    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  default:
    modelError(path, "sampler uses an unsupported wrapping mode");
  }
}

VkSamplerCreateInfo samplerInfo(const tg3_model &model, int32_t samplerIndex,
                                const std::filesystem::path &path) {
  VkSamplerCreateInfo result{};
  result.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  result.magFilter = VK_FILTER_LINEAR;
  result.minFilter = VK_FILTER_LINEAR;
  result.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  result.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  result.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  result.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  result.maxLod = VK_LOD_CLAMP_NONE;
  if (samplerIndex < 0)
    return result;
  if (static_cast<uint32_t>(samplerIndex) >= model.samplers_count)
    modelError(path, "texture references an invalid sampler");
  const auto &sampler = model.samplers[samplerIndex];
  switch (sampler.mag_filter) {
  case -1:
  case TG3_TEXTURE_FILTER_LINEAR:
    result.magFilter = VK_FILTER_LINEAR;
    break;
  case TG3_TEXTURE_FILTER_NEAREST:
    result.magFilter = VK_FILTER_NEAREST;
    break;
  default:
    modelError(path, "sampler uses an unsupported magnification filter");
  }
  switch (sampler.min_filter) {
  case -1:
  case TG3_TEXTURE_FILTER_LINEAR:
  case TG3_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
  case TG3_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
    result.minFilter = VK_FILTER_LINEAR;
    break;
  case TG3_TEXTURE_FILTER_NEAREST:
  case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
  case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
    result.minFilter = VK_FILTER_NEAREST;
    break;
  default:
    modelError(path, "sampler uses an unsupported minification filter");
  }
  result.mipmapMode =
      sampler.min_filter == TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
              sampler.min_filter == TG3_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST
          ? VK_SAMPLER_MIPMAP_MODE_NEAREST
          : VK_SAMPLER_MIPMAP_MODE_LINEAR;
  result.addressModeU = addressMode(sampler.wrap_s, path);
  result.addressModeV = addressMode(sampler.wrap_t, path);
  return result;
}

MaterialData materialData(const tg3_material *material) {
  MaterialData result;
  if (!material)
    return result;
  const auto &pbr = material->pbr_metallic_roughness;
  result.baseColorFactor =
      glm::vec4{pbr.base_color_factor[0], pbr.base_color_factor[1],
                pbr.base_color_factor[2], pbr.base_color_factor[3]};
  result.emissiveFactor =
      glm::vec4{material->emissive_factor[0], material->emissive_factor[1],
                material->emissive_factor[2], 0.0f};
  result.pbrFactors = glm::vec4{pbr.metallic_factor, pbr.roughness_factor,
                                material->normal_texture.scale,
                                material->occlusion_texture.strength};
  result.alpha.x =
      tg3_str_equals_cstr(material->alpha_mode, "MASK") ? 1.0f : 0.0f;
  result.alpha.y = static_cast<float>(material->alpha_cutoff);
  result.alpha.z =
      tg3_str_equals_cstr(material->alpha_mode, "BLEND") ? 1.0f : 0.0f;
  return result;
}

void requireTexcoordZero(const tg3_texture_info &info,
                         const std::filesystem::path &path,
                         std::string_view semantic) {
  if (info.index >= 0 && info.tex_coord != 0)
    modelError(path, std::string(semantic) +
                         " uses an unsupported TEXCOORD set "
                         "(only TEXCOORD_0 is supported)");
}

void requireTexcoordZero(const tg3_normal_texture_info &info,
                         const std::filesystem::path &path,
                         std::string_view semantic) {
  if (info.index >= 0 && info.tex_coord != 0)
    modelError(path, std::string(semantic) +
                         " uses an unsupported TEXCOORD set "
                         "(only TEXCOORD_0 is supported)");
}

void requireTexcoordZero(const tg3_occlusion_texture_info &info,
                         const std::filesystem::path &path,
                         std::string_view semantic) {
  if (info.index >= 0 && info.tex_coord != 0)
    modelError(path, std::string(semantic) +
                         " uses an unsupported TEXCOORD set "
                         "(only TEXCOORD_0 is supported)");
}

} // namespace

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
  const auto end = file.tellg();
  if (end < 0 ||
      static_cast<uintmax_t>(end) > std::numeric_limits<size_t>::max())
    return std::nullopt;
  const auto size = static_cast<size_t>(end);
  file.seekg(0, std::ios_base::beg);
  std::vector<uint8_t> ret;
  ret.resize(size);
  file.read(reinterpret_cast<char *>(ret.data()),
            static_cast<std::streamsize>(size));
  if (!file)
    return std::nullopt;
  return ret;
}

struct GLTFModel::Materialized::Impl {
  using VertexBufferTy = VertexBuffer<ModelVertex, fon_type::cow>;
  using IndexBufferTy = IndexBuffer<VK_INDEX_TYPE_UINT32, fon_type::cow>;

  struct Primitive {
    uint32_t firstIndex;
    uint32_t indexCount;
    int32_t material;
  };

  struct Material {
    StageSet<MaterialStage> stage;
    std::vector<Texture> textures;
  };

  Impl(const GLTFModel &source, GraphicsEngine &engine, CopyEngine &copyEngine,
       ShaderLoader &shaderLoader)
      : geometry(engine, StageLayout<GeometryStage>(
                             engine, shaderLoader, "gltf",
                             std::make_unique<vkw::VertexInputStateCreateInfo<
                                 vkw::per_vertex<ModelVertex, 0>>>())),
        opaqueSingleSided(engine, shaderLoader, "gltf", rasterization(false),
                          depthTest(true)),
        opaqueDoubleSided(engine, shaderLoader, "gltf", rasterization(true),
                          depthTest(true)),
        blendSingleSided(engine, shaderLoader, "gltf", rasterization(false),
                         depthTest(false), alphaBlend()),
        blendDoubleSided(engine, shaderLoader, "gltf", rasterization(true),
                         depthTest(false), alphaBlend()),
        vertices(nullptr), indices(nullptr), sourcePath(source.m_sourcePath) {
    const auto &model = *source.m_handle.get();
    auto cpu = buildCPUModel(model, source.m_sourcePath);
    vertices = VertexBufferTy(engine, copyEngine, cpu.vertices);
    indices = IndexBufferTy(engine, copyEngine, cpu.indices);
    primitives.reserve(cpu.primitives.size());
    for (const auto &primitive : cpu.primitives)
      primitives.push_back(
          {primitive.firstIndex, primitive.indexCount, primitive.material});

    const auto defaultMaterial =
        createMaterial(source, engine, copyEngine, shaderLoader, nullptr);
    materials.reserve(model.materials_count + 1);
    materials.push_back(defaultMaterial);
    for (uint32_t i = 0; i < model.materials_count; ++i)
      materials.push_back(createMaterial(source, engine, copyEngine,
                                         shaderLoader, &model.materials[i]));
  }

  Material createMaterial(const GLTFModel &source, GraphicsEngine &engine,
                          CopyEngine &copyEngine, ShaderLoader &shaderLoader,
                          const tg3_material *material) {
    const auto &model = *source.m_handle.get();
    if (material) {
      requireTexcoordZero(material->pbr_metallic_roughness.base_color_texture,
                          source.m_sourcePath, "base-color texture");
      requireTexcoordZero(
          material->pbr_metallic_roughness.metallic_roughness_texture,
          source.m_sourcePath, "metallic-roughness texture");
      requireTexcoordZero(material->normal_texture, source.m_sourcePath,
                          "normal texture");
      requireTexcoordZero(material->occlusion_texture, source.m_sourcePath,
                          "occlusion texture");
      requireTexcoordZero(material->emissive_texture, source.m_sourcePath,
                          "emissive texture");
    }

    const auto baseColor =
        material ? material->pbr_metallic_roughness.base_color_texture.index
                 : -1;
    const auto metallicRoughness =
        material
            ? material->pbr_metallic_roughness.metallic_roughness_texture.index
            : -1;
    const auto normal = material ? material->normal_texture.index : -1;
    const auto occlusion = material ? material->occlusion_texture.index : -1;
    const auto emissive = material ? material->emissive_texture.index : -1;

    Material result;
    const auto addTexture = [&](int32_t index, glm::u8vec4 fallback,
                                VkFormat format) {
      result.textures.push_back(
          uploadTexture(source, engine, copyEngine, index, fallback, format));
      const auto sampler = index >= 0 ? model.textures[index].sampler : -1;
      return SampledView(engine, result.textures.back(),
                         samplerInfo(model, sampler, source.m_sourcePath));
    };
    auto baseColorView =
        addTexture(baseColor, {255, 255, 255, 255}, VK_FORMAT_R8G8B8A8_SRGB);
    auto metallicRoughnessView = addTexture(
        metallicRoughness, {255, 255, 255, 255}, VK_FORMAT_R8G8B8A8_UNORM);
    auto normalView =
        addTexture(normal, {128, 128, 255, 255}, VK_FORMAT_R8G8B8A8_UNORM);
    auto occlusionView =
        addTexture(occlusion, {255, 255, 255, 255}, VK_FORMAT_R8G8B8A8_UNORM);
    auto emissiveView =
        addTexture(emissive, {255, 255, 255, 255}, VK_FORMAT_R8G8B8A8_SRGB);
    auto uniform = UniformBuffer<MaterialData, fon_type::cow>(
        engine, copyEngine, materialData(material));

    const auto doubleSided = material && material->double_sided;
    const auto blend =
        material && tg3_str_equals_cstr(material->alpha_mode, "BLEND");
    const auto &layout =
        blend ? (doubleSided ? blendDoubleSided : blendSingleSided)
              : (doubleSided ? opaqueDoubleSided : opaqueSingleSided);
    auto builder = StageSetBuilder<MaterialStage>{engine, layout};
    builder.addDescriptorSet(3)
        .addDescriptor(baseColorView, 0)
        .addDescriptor(metallicRoughnessView, 1)
        .addDescriptor(normalView, 2)
        .addDescriptor(occlusionView, 3)
        .addDescriptor(emissiveView, 4)
        .addDescriptor(uniform, 5);
    result.stage = std::move(builder);
    return result;
  }

  static vkw::RasterizationStateCreateInfo rasterization(bool doubleSided) {
    return {VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL,
            static_cast<VkCullModeFlags>(doubleSided ? VK_CULL_MODE_NONE
                                                     : VK_CULL_MODE_BACK_BIT),
            VK_FRONT_FACE_COUNTER_CLOCKWISE};
  }

  static vkw::DepthTestStateCreateInfo depthTest(bool write) {
    return {VK_COMPARE_OP_LESS_OR_EQUAL, write};
  }

  static VkPipelineColorBlendAttachmentState alphaBlend() {
    VkPipelineColorBlendAttachmentState result{};
    result.blendEnable = VK_TRUE;
    result.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    result.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    result.colorBlendOp = VK_BLEND_OP_ADD;
    result.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    result.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    result.alphaBlendOp = VK_BLEND_OP_ADD;
    result.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                            VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    return result;
  }

  Texture uploadTexture(const GLTFModel &source, GraphicsEngine &engine,
                        CopyEngine &copyEngine, int32_t textureIndex,
                        glm::u8vec4 fallback, VkFormat format) {
    const auto packedFallback = static_cast<uint32_t>(fallback.r) |
                                (static_cast<uint32_t>(fallback.g) << 8) |
                                (static_cast<uint32_t>(fallback.b) << 16) |
                                (static_cast<uint32_t>(fallback.a) << 24);
    const auto key = std::tuple{textureIndex, format, packedFallback};
    if (const auto found = textureCache.find(key); found != textureCache.end())
      return found->second;

    const auto &model = *source.m_handle.get();
    if (textureIndex < 0) {
      const std::array pixels{fallback.r, fallback.g, fallback.b, fallback.a};
      auto uploaded = Texture(
          engine, Texture::load(engine, copyEngine, pixels, 1, 1, format));
      textureCache.emplace(key, uploaded);
      return uploaded;
    }
    if (static_cast<uint32_t>(textureIndex) >= model.textures_count)
      modelError(source.m_sourcePath,
                 "material references an invalid texture index");
    const auto &texture = model.textures[textureIndex];
    if (texture.source < 0 ||
        static_cast<uint32_t>(texture.source) >= model.images_count)
      modelError(source.m_sourcePath,
                 "texture references an invalid image index");
    const auto encoded =
        imageBytes(model, model.images[texture.source], source.m_sourcePath);
    try {
      auto uploaded = Texture(
          engine, Texture::loadEncoded(engine, copyEngine, encoded, format));
      textureCache.emplace(key, uploaded);
      return uploaded;
    } catch (const std::runtime_error &error) {
      std::stringstream ss;
      ss << "could not upload texture " << textureIndex << " (image "
         << texture.source << "): " << error.what();
      modelError(source.m_sourcePath, ss.str());
    }
  }

  StageSet<GeometryStage> geometry;
  StageLayout<MaterialStage> opaqueSingleSided;
  StageLayout<MaterialStage> opaqueDoubleSided;
  StageLayout<MaterialStage> blendSingleSided;
  StageLayout<MaterialStage> blendDoubleSided;
  VertexBufferTy vertices;
  IndexBufferTy indices;
  std::vector<Primitive> primitives;
  std::vector<Material> materials;
  std::map<std::tuple<int32_t, VkFormat, uint32_t>, Texture> textureCache;
  std::filesystem::path sourcePath;
};

GLTFModel::Materialized::Materialized(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl)) {}

GLTFModel::Materialized::Materialized(Materialized &&) noexcept = default;
GLTFModel::Materialized &
GLTFModel::Materialized::operator=(Materialized &&) noexcept = default;
GLTFModel::Materialized::~Materialized() = default;

void GLTFModel::Materialized::draw(PipelineManager &pipelineManager,
                                   vkw::RenderPassRecorder &commands,
                                   const Frame &frame,
                                   StageSet<ProjectionStage> projection,
                                   StageSet<LightingStage> lighting) const {
  auto &vertices = m_impl->vertices->use(frame);
  auto &indices = m_impl->indices->use(frame);
  commands.bindVertexBuffer(vertices, 0, 0);
  commands.bindIndexBuffer(indices, 0);
  pipelineManager.bind(m_impl->geometry, projection, lighting);
  for (const auto &primitive : m_impl->primitives) {
    const auto material = primitive.material >= 0
                              ? static_cast<size_t>(primitive.material) + 1
                              : 0;
    if (material >= m_impl->materials.size())
      modelError(m_impl->sourcePath,
                 "primitive references an invalid material");
    pipelineManager.bind(m_impl->materials[material].stage);
    pipelineManager.bindPipeline();
    commands.drawIndexed(primitive.indexCount, 1, primitive.firstIndex, 0, 0);
  }
}

GLTFModel::Materialized
GLTFModel::materialize(GraphicsEngine &engine, CopyEngine &copyEngine,
                       ShaderLoader &shaderLoader) const {
  return Materialized{std::make_unique<Materialized::Impl>(
      *this, engine, copyEngine, shaderLoader)};
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
  tinygltf3::ErrorStack errorStack;
  m_sourcePath = sourcePath;
  const auto ascii_path = sourcePath.parent_path().string();
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