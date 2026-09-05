#pragma once

#include "imvk/graph/Node.hpp"
#include <boost/compat/move_only_function.hpp>
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
  ImageTy(VkImageType type)
      : Type([&]() {
          std::stringstream ss;
          ss << "image<" << imageTypeToStr(type) << ">";
          return ss.str();
        }()),
        imageType(type) {}

  const AttributesBase *getUndefined(Context &ctx) const final;

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
  ImageUseInfo(ImageAccessInfo acc, std::string name = {})
      : UseInfo(std::move(name)), access(acc) {}
  ImageUseInfo(ImageAccessInfo acc, size_t pass, std::string name = {})
      : UseInfo(std::move(name)), access(acc), passthrough(pass) {}
  ImageAccessInfo access;
  std::optional<size_t> passthrough;
  ImageUseInfo *clone() const override { return new ImageUseInfo(*this); }
};

class ImageDefInfo : public DefInfo {
public:
  ImageDefInfo() = default;
  ImageDefInfo(ImageAccessInfo acc, std::string name = {})
      : DefInfo(std::move(name)), access(acc) {}
  ImageDefInfo(ImageAccessInfo acc, size_t pass, std::string name = {})
      : DefInfo(std::move(name)), access(acc), passthrough(pass) {}
  ImageAccessInfo access;
  std::optional<size_t> passthrough;
  ImageDefInfo *clone() const override { return new ImageDefInfo(*this); }
};

class ImageAttachmentUseInfo : public ImageUseInfo {
public:
  enum class Kind { color, depth, input } kind;
  enum class LoadOp { load, clear, dc } load;
  ImageAttachmentUseInfo(Kind kind, LoadOp load);
  ImageAttachmentUseInfo *clone() const override {
    return new ImageAttachmentUseInfo{*this};
  }
  ImageDefInfo defFromThis() const;
};

class ImageDescriptorUseInfo : public ImageUseInfo {
public:
  ImageDescriptorUseInfo(VkDescriptorType type);
  vkw::DescriptorSetLayoutBinding descriptorInfo() const;
  VkDescriptorType type() const { return m_type; }
  ImageDescriptorUseInfo *clone() const override {
    return new ImageDescriptorUseInfo{m_type};
  }

private:
  VkDescriptorType m_type;
};

class DescriptorUseInfo {
public:
  DescriptorUseInfo(std::unique_ptr<UseInfo> &&info)
      : m_useInfo(std::move(info)) {}
  const UseInfo &useInfo() const { return *m_useInfo; }

  static DescriptorUseInfo sampledImage() {
    return DescriptorUseInfo(std::make_unique<ImageDescriptorUseInfo>(
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE));
  }
  static DescriptorUseInfo combinedImageSampler() {
    return DescriptorUseInfo(std::make_unique<ImageDescriptorUseInfo>(
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER));
  }

private:
  std::unique_ptr<UseInfo> m_useInfo;
};

class IntegerScalarTy : public Type {
public:
  IntegerScalarTy() : Type("int64") {}
  const AttributesBase *getUndefined(Context &ctx) const final;
  std::size_t hash() const final { return typeid(IntegerScalarTy).hash_code(); }
  bool operator==(const Type &another) const final {
    return dynamic_cast<const IntegerScalarTy *>(&another);
  }
};

class ExtentsTy : public Type {
public:
  ExtentsTy() : Type("ext3d") {}
  const AttributesBase *getUndefined(Context &ctx) const final;
  std::size_t hash() const final { return typeid(ExtentsTy).hash_code(); }
  bool operator==(const Type &another) const final {
    return dynamic_cast<const ExtentsTy *>(&another);
  }
};

class BufferTy : public Type {
public:
  BufferTy() : Type("buffer") {}
  const AttributesBase *getUndefined(Context &ctx) const final;
  std::size_t hash() const final { return typeid(BufferTy).hash_code(); }
  bool operator==(const Type &another) const final {
    return dynamic_cast<const BufferTy *>(&another);
  }
};

} // namespace imvk::graph