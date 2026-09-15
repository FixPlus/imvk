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
  ImageTy() : Type("image") {}

  const AttributesBase *getUndefined(Context &ctx) const final;

  std::size_t hash() const final { return typeid(ImageTy).hash_code(); }
  bool operator==(const Type &another) const final {
    return dynamic_cast<const ImageTy *>(&another);
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

/// @brief FormatConstraintInfo defines image format constraints that use
/// expects. It declares per-channel constraint regarding it's presense and
/// expected bitwidth and numeric format. Image value can only be used in
/// specified node's use if constraints of that use are compatible with
/// associated value's format. (e.g. depth image cannot be used as
/// color attachment).
struct FormatConstraintInfo {
  enum class Channel : unsigned { R = 0, G = 1, B = 2, A = 3, D = 4, S = 5 };
  enum class NumericFormat : unsigned {
    UNORM,
    SNORM,
    USCALED,
    SSCALED,
    UINT,
    SINT,
    UFLOAT,
    SFLOAT,
    SRGB,
    SFIXED5,
    BOOL
  };
  struct ChannelConstraint {
    std::optional<unsigned> bitwidth;
    std::optional<NumericFormat> numericFormat;
    ChannelConstraint() = default;
    ChannelConstraint(unsigned bw) : bitwidth(bw) {}
    ChannelConstraint(NumericFormat nf) : numericFormat(nf) {}
    ChannelConstraint(unsigned bw, NumericFormat nf)
        : bitwidth(bw), numericFormat(nf) {}
  };

  std::array<std::optional<ChannelConstraint>, 6> channels;

  void addChannelConstraint(Channel ch, NumericFormat numericFormat) {
    channels[static_cast<unsigned>(ch)].emplace(numericFormat);
  }

  void addChannelConstraint(Channel ch, unsigned bitwidth,
                            NumericFormat numericFormat) {
    channels[static_cast<unsigned>(ch)].emplace(bitwidth, numericFormat);
  }

  void addChannelConstraint(Channel ch, unsigned bitwidth) {
    channels[static_cast<unsigned>(ch)].emplace(bitwidth);
  }
  void addChannelConstraint(Channel ch) {
    channels[static_cast<unsigned>(ch)].emplace();
  }

  bool hasChannelContraint(Channel ch) const {
    return channels[static_cast<unsigned>(ch)].has_value();
  }
  /// @brief compatibility of constraints is based on compatibility rules for
  /// format in vulkan specification.
  bool isCompatible(VkFormat format) const;

  bool empty() const {
    return std::ranges::none_of(channels,
                                [](const auto &c) { return c.has_value(); });
  }

  bool isNullChannel(Channel ch) const {
    if (auto cnst = channels[static_cast<unsigned>(ch)]) {
      return cnst->bitwidth && *cnst->bitwidth == 0;
    }
    return false;
  }
  std::optional<unsigned> channelBitwidth(Channel ch) const {
    if (!hasChannelContraint(ch))
      return std::nullopt;
    return channels[static_cast<unsigned>(ch)]->bitwidth;
  }
};

class ImageUseInfo : public UseInfo {
public:
  ImageUseInfo() = default;
  ImageUseInfo(ImageAccessInfo acc, std::string name = {})
      : UseInfo(std::move(name)), access(acc) {}
  ImageUseInfo(ImageAccessInfo acc, size_t pass, std::string name = {})
      : UseInfo(std::move(name)), access(acc), passthrough(pass) {}
  ImageAccessInfo access;
  FormatConstraintInfo formatConstraint;
  /// Required view dimensionality. Cube views are currently unsupported.
  std::optional<VkImageViewType> viewTypeConstraint;
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
  ImageDescriptorUseInfo(
      VkDescriptorType type,
      VkShaderStageFlags shaderStages = VK_SHADER_STAGE_FRAGMENT_BIT |
                                        VK_SHADER_STAGE_VERTEX_BIT);
  vkw::DescriptorSetLayoutBinding descriptorInfo() const;
  VkDescriptorType type() const { return m_type; }
  VkShaderStageFlags shaderStages() const { return m_shaderStages; }
  void setShaderStages(VkShaderStageFlags shaderStages);
  ImageDescriptorUseInfo *clone() const override {
    return new ImageDescriptorUseInfo{*this};
  }

private:
  VkDescriptorType m_type;
  VkShaderStageFlags m_shaderStages;
};

class DescriptorUseInfo {
public:
  DescriptorUseInfo(std::unique_ptr<UseInfo> &&info, bool passthrough = false)
      : m_useInfo(std::move(info)), m_passthrough(passthrough) {}
  const UseInfo &useInfo() const { return *m_useInfo; }
  bool isPassthrough() const { return m_passthrough; }

  static DescriptorUseInfo sampledImage(bool passthrough = false) {
    return DescriptorUseInfo(std::make_unique<ImageDescriptorUseInfo>(
                                 VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
                             passthrough);
  }
  static DescriptorUseInfo combinedImageSampler(bool passthrough = false) {
    return DescriptorUseInfo(std::make_unique<ImageDescriptorUseInfo>(
                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER),
                             passthrough);
  }
  static DescriptorUseInfo storageImage(bool passthrough = false) {
    return DescriptorUseInfo(
        std::make_unique<ImageDescriptorUseInfo>(
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT),
        passthrough);
  }

private:
  std::unique_ptr<UseInfo> m_useInfo;
  bool m_passthrough;
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

class FormatTy : public Type {
public:
  FormatTy() : Type("format") {}
  const AttributesBase *getUndefined(Context &ctx) const final;
  std::size_t hash() const final { return typeid(FormatTy).hash_code(); }
  bool operator==(const Type &another) const final {
    return dynamic_cast<const FormatTy *>(&another);
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