#include "imvk/graph/Types.hpp"
#include "imvk/graph/Attributes.hpp"
#include "imvk/graph/Context.hpp"

#include <vulkan/vulkan_format_traits.hpp>

namespace imvk::graph {

namespace {

using Channel = FormatConstraintInfo::Channel;
using NumericFormat = FormatConstraintInfo::NumericFormat;

std::string_view channelName(Channel channel) {
  switch (channel) {
  case Channel::R:
    return "R";
  case Channel::G:
    return "G";
  case Channel::B:
    return "B";
  case Channel::A:
    return "A";
  case Channel::D:
    return "D";
  case Channel::S:
    return "S";
  }
  return {};
}

std::string_view numericFormatName(NumericFormat format) {
  switch (format) {
  case NumericFormat::UNORM:
    return "UNORM";
  case NumericFormat::SNORM:
    return "SNORM";
  case NumericFormat::USCALED:
    return "USCALED";
  case NumericFormat::SSCALED:
    return "SSCALED";
  case NumericFormat::UINT:
    return "UINT";
  case NumericFormat::SINT:
    return "SINT";
  case NumericFormat::UFLOAT:
    return "UFLOAT";
  case NumericFormat::SFLOAT:
    return "SFLOAT";
  case NumericFormat::SRGB:
    return "SRGB";
  case NumericFormat::SFIXED5:
    return "SFIXED5";
  case NumericFormat::BOOL:
    return "BOOL";
  }
  return {};
}

} // namespace

bool FormatConstraintInfo::isCompatible(VkFormat format) const {
  const auto vkFormat = static_cast<vk::Format>(format);
  const auto componentCount = vk::componentCount(vkFormat);

  for (unsigned channelIndex = 0; channelIndex < channels.size();
       ++channelIndex) {
    const auto &constraint = channels[channelIndex];
    if (!constraint)
      continue;

    const auto name = channelName(static_cast<Channel>(channelIndex));
    std::optional<unsigned> componentIndex;
    for (unsigned i = 0; i < componentCount; ++i) {
      if (name == vk::componentName(vkFormat, static_cast<uint8_t>(i))) {
        componentIndex = i;
        break;
      }
    }

    if (constraint->bitwidth && *constraint->bitwidth == 0) {
      if (componentIndex)
        return false;
      continue;
    }
    if (!componentIndex)
      return false;
    if (constraint->bitwidth &&
        vk::componentBits(vkFormat, static_cast<uint8_t>(*componentIndex)) !=
            *constraint->bitwidth)
      return false;
    if (constraint->numericFormat &&
        numericFormatName(*constraint->numericFormat) !=
            vk::componentNumericFormat(
                vkFormat, static_cast<uint8_t>(*componentIndex)))
      return false;
  }
  return true;
}

const AttributesBase *BufferTy::getUndefined(Context &ctx) const {
  return &ctx.attributes().get<Attributes<BufferTy>>();
}

const AttributesBase *IntegerScalarTy::getUndefined(Context &ctx) const {
  return &ctx.attributes().get<Attributes<IntegerScalarTy>>();
}

const AttributesBase *FormatTy::getUndefined(Context &ctx) const {
  return &ctx.attributes().get<Attributes<FormatTy>>();
}

const AttributesBase *ImageTy::getUndefined(Context &ctx) const {
  return &ctx.attributes().get<Attributes<ImageTy>>();
}
const AttributesBase *ExtentsTy::getUndefined(Context &ctx) const {
  return &ctx.attributes().get<Attributes<ExtentsTy>>();
}

ImageAttachmentUseInfo::ImageAttachmentUseInfo(Kind k, LoadOp l)
    : ImageUseInfo([&]() {
        ImageAccessInfo info{};
        switch (k) {
        case Kind::input:
          info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
          info.usage = VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
          info.accessFlags = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
          info.stageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
          break;
        case Kind::color:
          info.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
          info.accessFlags =
              l == LoadOp::load ? VK_ACCESS_COLOR_ATTACHMENT_READ_BIT : 0;
          info.stageFlags =
              l == LoadOp::load ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : 0;
          break;
        case Kind::depth:
          info.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
          info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
          info.accessFlags = l == LoadOp::load
                                 ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                 : 0;
          info.stageFlags = l == LoadOp::load
                                ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                : 0;
          break;
        }
        return info;
      }()),
      kind(k), load(l) {}

ImageDefInfo ImageAttachmentUseInfo::defFromThis() const {
  ImageAccessInfo info{};
  switch (kind) {
  case ImageAttachmentUseInfo::Kind::color:
    info.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.stageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    info.accessFlags = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;
  case ImageAttachmentUseInfo::Kind::depth:
    info.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    info.stageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; //?
    info.accessFlags = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;
  }
  return ImageDefInfo{info};
}

ImageDescriptorUseInfo::ImageDescriptorUseInfo(VkDescriptorType type)
    : ImageUseInfo([=]() {
        assert(type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
               type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE &&
                   "unsupported image descriptor");
        ImageAccessInfo info{};
        info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.accessFlags = VK_ACCESS_SHADER_READ_BIT;
        info.stageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                          VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        return info;
      }()),
      m_type(type) {}
vkw::DescriptorSetLayoutBinding ImageDescriptorUseInfo::descriptorInfo() const {
  return vkw::DescriptorSetLayoutBinding{
      0, m_type, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT};
}
} // namespace imvk::graph