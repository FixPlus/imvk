#include "IMVKTexture.hpp"

#include <vkw/StagingBuffer.hpp>

#include <limits>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace imvk::examples {

std::filesystem::path assetsDir() { return IMVK_ASSETS_PATH; }

void SampledView::descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                                  FONodeBase &obj, unsigned binding) {
  auto &impl = static_cast<SampledViewImpl &>(obj);
  auto &&[view, sampler] = impl.get();
  vkw::DescriptorWrite write{
      binding, impl.noSampler() ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
  write.addImage(!impl.noSampler() ? sampler : nullptr,
                 view.operator VkImageView(),
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  set.write(write);
}

std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>
SampledViewImpl::constructNew(FramedEngine &engine) {
  return doConstructNew(engine, getUse<Texture>(0)->get(), m_samplerInfo);
}

VkSamplerCreateInfo SampledViewImpl::samplerInfo(VkFilter filter) {
  VkSamplerCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.magFilter = filter;
  info.minFilter = filter;
  info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  info.maxLod = VK_LOD_CLAMP_NONE;
  return info;
}

std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>
SampledViewImpl::doConstructNew(FramedEngine &engine,
                                const vkw::Image<vkw::COLOR, vkw::I2D> &image,
                                VkFilter filter) {
  return doConstructNew(engine, image, samplerInfo(filter));
}

std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>
SampledViewImpl::doConstructNew(FramedEngine &engine,
                                const vkw::Image<vkw::COLOR, vkw::I2D> &image,
                                VkSamplerCreateInfo samplerInfo) {
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.pNext = nullptr;
  return std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>(
      vkw::ImageView<vkw::COLOR, vkw::V2D>(engine.context().device(), image,
                                           image.format()),
      vkw::Sampler(engine.context().device(), samplerInfo));
}

namespace {
struct ImageInfo {
  std::vector<unsigned char> data;
  int width;
  int height;
  int comp;
};
struct ImageInit : public imvk::CopyEngine::Workload {
  ImageInit(vkw::StagingBuffer<unsigned char> &&src,
            vkw::Image<vkw::COLOR, vkw::I2D> &image)
      : src(std::move(src)), dst(image){};

  void record(vkw::TransferPassRecorder &commands) const override;

  vkw::StagingBuffer<unsigned char> src;
  vkw::Image<vkw::COLOR, vkw::I2D> &dst;
};

void ImageInit::record(vkw::TransferPassRecorder &commands) const {
  VkImageMemoryBarrier transitLayout1{};
  transitLayout1.image = dst.vkw::AllocatedImage::operator VkImage_T *();
  transitLayout1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  transitLayout1.pNext = nullptr;
  transitLayout1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  transitLayout1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  transitLayout1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  transitLayout1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  transitLayout1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  transitLayout1.subresourceRange.baseArrayLayer = 0;
  transitLayout1.subresourceRange.baseMipLevel = 0;
  transitLayout1.subresourceRange.layerCount = 1;
  transitLayout1.subresourceRange.levelCount = 1;
  transitLayout1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  transitLayout1.srcAccessMask = 0;

  VkImageMemoryBarrier transitLayout2{};
  transitLayout2.image = dst.vkw::AllocatedImage::operator VkImage_T *();
  transitLayout2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  transitLayout2.pNext = nullptr;
  transitLayout2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  transitLayout2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  transitLayout2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  transitLayout2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  transitLayout2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  transitLayout2.subresourceRange.baseArrayLayer = 0;
  transitLayout2.subresourceRange.baseMipLevel = 0;
  transitLayout2.subresourceRange.layerCount = 1;
  transitLayout2.subresourceRange.levelCount = 1;
  transitLayout2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  transitLayout2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

  VkBufferImageCopy copy{};
  copy.imageExtent = {dst.width(), dst.height(), 1};
  copy.imageSubresource.mipLevel = 0;
  copy.imageSubresource.layerCount = 1;
  copy.imageSubresource.baseArrayLayer = 0;
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  commands.imageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              {&transitLayout1, 1});
  commands.copyBufferToImage(src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             {&copy, 1});
  commands.imageMemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              {&transitLayout2, 1});
}

} // namespace

static ImageInfo readImageFile(const std::filesystem::path &path) {
  std::set<std::string> fileExtensions = {".png", ".jpg", ".jpeg"};
  auto foundExisting =
      std::ranges::find_if(fileExtensions, [&](std::string ext) {
        return std::filesystem::is_regular_file(std::filesystem::path(path) +=
                                                ext);
      });
  if (foundExisting == fileExtensions.end())
    throw std::runtime_error([&]() {
      std::stringstream ss;
      ss << "Failed to open texture " << path << " with any known extension";
      return ss.str();
    }());
  ImageInfo ret;
  const auto ascii_path =
      (std::filesystem::path(path) += *foundExisting).string();
  std::unique_ptr<unsigned char,
                  decltype([](unsigned char *data) { stbi_image_free(data); })>
      rawData{
          stbi_load(ascii_path.c_str(), &ret.width, &ret.height, &ret.comp, 4)};
  if (!rawData)
    throw std::runtime_error([&]() {
      std::stringstream ss;
      ss << "Failed to read texture '" << ascii_path << "'";
      return ss.str();
    }());
  ret.data.resize(ret.height * ret.width * 4);
  std::copy(rawData.get(), rawData.get() + ret.data.size(), ret.data.begin());
  return ret;
}

vkw::Image<vkw::COLOR, vkw::I2D>
Texture::load(FramedEngine &engine, CopyEngine &ce,
              const std::filesystem::path &path) {
  auto imageInfo = readImageFile(path);
  return load(engine, ce, imageInfo.data, imageInfo.width, imageInfo.height);
}

vkw::Image<vkw::COLOR, vkw::I2D>
Texture::load(FramedEngine &engine, CopyEngine &ce,
              std::span<const unsigned char> data, unsigned width,
              unsigned height, VkFormat format) {
  VmaAllocationCreateInfo allocInfo{};

  auto &device = ce.context().device();
  allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  auto ret = vkw::Image<vkw::COLOR, vkw::I2D>(
      ce.context().getDeviceAllocator(), allocInfo, format,
      static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1, 1, 1,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  auto copyFuture = ce.copy(
      std::make_unique<ImageInit>(vkw::StagingBuffer<unsigned char>(
                                      ce.context().getDeviceAllocator(), data),
                                  ret));
  copyFuture.wait();
  return ret;
}

vkw::Image<vkw::COLOR, vkw::I2D>
Texture::loadEncoded(FramedEngine &engine, CopyEngine &ce,
                     std::span<const unsigned char> data, VkFormat format) {
  if (data.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    throw std::runtime_error("Encoded texture is too large for stb_image");

  int width = 0;
  int height = 0;
  int components = 0;
  std::unique_ptr<unsigned char, decltype([](unsigned char *pixels) {
                    stbi_image_free(pixels);
                  })>
      pixels{stbi_load_from_memory(data.data(), static_cast<int>(data.size()),
                                   &width, &height, &components,
                                   STBI_rgb_alpha)};
  if (!pixels) {
    std::stringstream ss;
    ss << "Failed to decode texture: " << stbi_failure_reason();
    throw std::runtime_error(ss.str());
  }
  if (width <= 0 || height <= 0)
    throw std::runtime_error("Decoded texture has invalid dimensions");
  const auto pixelCount =
      static_cast<size_t>(width) * static_cast<size_t>(height);
  if (pixelCount > std::numeric_limits<size_t>::max() / 4)
    throw std::runtime_error("Decoded texture dimensions are too large");

  return load(engine, ce,
              std::span<const unsigned char>{pixels.get(), pixelCount * 4},
              width, height, format);
}

} // namespace imvk::examples