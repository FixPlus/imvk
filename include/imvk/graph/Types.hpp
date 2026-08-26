#pragma once

#include "imvk/graph/Node.hpp"
#include <vkw/DescriptorSet.hpp>

#include <boost/container_hash/hash.hpp>
#include <unordered_set>
#include <vector>

#include <vkw/Image.hpp>

inline std::ostream &operator<<(std::ostream &os, VkExtent3D extents) {
  return os << "[ " << extents.width << ", " << extents.height << ", "
            << extents.depth << " ]";
}
namespace imvk::graph {
class ImageTy : public Type {
public:
  VkImageType imageType;
  static std::string_view imageTypeToStr(VkImageType t) {
    switch (t) {
    case VK_IMAGE_TYPE_1D:
      return "1d";
    case VK_IMAGE_TYPE_2D:
      return "2d";
    case VK_IMAGE_TYPE_3D:
      return "3d";
    default:
      return "undef";
    }
  }
  ImageTy(VkImageType type) : imageType(type) {}

  const AttributesBase *getUndefined(Context &ctx) const final;
  void dump(std::ostream &os) const final {
    os << "image<" << imageTypeToStr(imageType) << ">";
  }
  std::size_t hash() const final {
    std::size_t ret = typeid(ImageTy).hash_code();
    boost::hash_combine(ret, imageType);
    return ret;
  }
  bool operator==(const Type &another) const final {
    auto *rhs = dynamic_cast<const ImageTy *>(&another);
    if (!rhs)
      return false;
    return imageType == rhs->imageType;
  }
};

struct ImageAccessInfo {
  VkAccessFlags accessFlags = 0;
  VkPipelineStageFlags stageFlags = 0;
  VkImageUsageFlags usage = 0;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  bool empty() const { return *this == ImageAccessInfo{}; }
  bool operator==(const ImageAccessInfo &) const = default;
};

class ImageUseInfo : public UseInfo {
public:
  ImageUseInfo() = default;
  ImageUseInfo(ImageAccessInfo acc) : access(acc) {}
  ImageUseInfo(ImageAccessInfo acc, size_t pass)
      : access(acc), passthrough(pass) {}
  ImageAccessInfo access;
  std::optional<size_t> passthrough;
};

class ImageAttachmentUseInfo : public ImageUseInfo {
public:
  enum class Kind { color, depth, input } kind;
  ImageAttachmentUseInfo(Kind kind);
};

class ImageDescriptorUseInfo : public ImageUseInfo {
public:
  ImageDescriptorUseInfo();
  vkw::DescriptorSetLayoutBinding descriptorInfo() const;
};

class ImageDefInfo : public DefInfo {
public:
  ImageDefInfo() = default;
  ImageDefInfo(ImageAccessInfo acc) : access(acc) {}
  ImageDefInfo(ImageAccessInfo acc, size_t pass)
      : access(acc), passthrough(pass) {}
  ImageAccessInfo access;
  std::optional<size_t> passthrough;
};

class IntegerScalarTy : public Type {
public:
  const AttributesBase *getUndefined(Context &ctx) const final;
  std::size_t hash() const final { return typeid(IntegerScalarTy).hash_code(); }
  bool operator==(const Type &another) const final {
    return dynamic_cast<const IntegerScalarTy *>(&another);
  }
  void dump(std::ostream &os) const final { os << "int64"; }
};

class ExtentsTy : public Type {
public:
  const AttributesBase *getUndefined(Context &ctx) const final;
  std::size_t hash() const final { return typeid(ExtentsTy).hash_code(); }
  bool operator==(const Type &another) const final {
    return dynamic_cast<const ExtentsTy *>(&another);
  }
  void dump(std::ostream &os) const final { os << "ext3d"; }
};

class BufferTy : public Type {
public:
  const AttributesBase *getUndefined(Context &ctx) const final;
  std::size_t hash() const final { return typeid(BufferTy).hash_code(); }
  bool operator==(const Type &another) const final {
    return dynamic_cast<const BufferTy *>(&another);
  }
  void dump(std::ostream &os) const final { os << "buffer"; }
};

class DescriptorTy : public Type {
public:
  const AttributesBase *getUndefined(Context &ctx) const final;
  std::size_t hash() const final { return typeid(DescriptorTy).hash_code(); }
  bool operator==(const Type &another) const final {
    return dynamic_cast<const DescriptorTy *>(&another);
  }
  void dump(std::ostream &os) const final { os << "descriptor"; }
};

class ArrayTy : public Type {
public:
  const Type *elementType;

  ArrayTy(const Type &et) : elementType(&et) {}
  const AttributesBase *getUndefined(Context &ctx) const final;
  void dump(std::ostream &os) const final {
    os << "array<" << *elementType << ">";
  }

  std::size_t hash() const final {
    std::size_t ret = typeid(ArrayTy).hash_code();
    boost::hash_combine(ret, elementType->hash());

    return ret;
  }
  bool operator==(const Type &another) const final {
    auto *rhs = dynamic_cast<const ArrayTy *>(&another);
    if (!rhs)
      return false;
    return *elementType == *rhs->elementType;
  }
};

} // namespace imvk::graph