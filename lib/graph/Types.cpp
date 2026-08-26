#include "imvk/graph/Types.hpp"
#include "imvk/graph/Attributes.hpp"
#include "imvk/graph/Context.hpp"

namespace imvk::graph {
const AttributesBase *ArrayTy::getUndefined(Context &ctx) const {
  return &ctx.attributes().get<Attributes<ArrayTy>>();
}

const AttributesBase *DescriptorTy::getUndefined(Context &ctx) const {
  return &ctx.attributes().get<Attributes<DescriptorTy>>();
}

const AttributesBase *BufferTy::getUndefined(Context &ctx) const {
  return &ctx.attributes().get<Attributes<BufferTy>>();
}

const AttributesBase *IntegerScalarTy::getUndefined(Context &ctx) const {
  return &ctx.attributes().get<Attributes<IntegerScalarTy>>();
}

const AttributesBase *ImageTy::getUndefined(Context &ctx) const {
  return &ctx.attributes().get<Attributes<ImageTy>>();
}
const AttributesBase *ExtentsTy::getUndefined(Context &ctx) const {
  return &ctx.attributes().get<Attributes<ExtentsTy>>();
}

ImageAttachmentUseInfo::ImageAttachmentUseInfo(Kind k)
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
          info.accessFlags = 0;
          info.stageFlags = 0;
          break;
        case Kind::depth:
          info.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
          info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
          info.accessFlags = 0;
          info.stageFlags = 0;
          break;
        }
        return info;
      }()),
      kind(k) {}
ImageDescriptorUseInfo::ImageDescriptorUseInfo()
    : ImageUseInfo([]() {
        ImageAccessInfo info{};
        info.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        info.accessFlags = VK_ACCESS_SHADER_READ_BIT;
        info.stageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                          VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        return info;
      }()) {}
vkw::DescriptorSetLayoutBinding ImageDescriptorUseInfo::descriptorInfo() const {
  return vkw::DescriptorSetLayoutBinding{
      0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT};
}
} // namespace imvk::graph