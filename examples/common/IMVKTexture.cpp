#include "IMVKTexture.hpp"

#include <vkw/StagingBuffer.hpp>

#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace imvk::examples {

std::filesystem::path assetsDir() { return IMVK_ASSETS_PATH; }
SampledView::SampledView(FramedEngine &eng, Texture &texture)
    : FONode<std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>,
             fon_type::cow>(doConstructNew(eng, texture.get()),
                            FOUses{texture}) {}

void SampledView::descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                                  unsigned binding) const {
  auto &&[view, sampler] = get();
  set.write(binding, view.operator VkImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler);
}
FObject::Ptr SampledView::constructNew(FramedEngine &engine) noexcept {
  return doConstructNew(engine, getUse<Texture>(0).get());
}
FObject::Ptr
SampledView::doConstructNew(FramedEngine &engine,
                            const vkw::Image<vkw::COLOR, vkw::I2D> &image) {
  VkSamplerCreateInfo info{};
  info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  info.magFilter = VK_FILTER_LINEAR;
  info.minFilter = VK_FILTER_LINEAR;
  info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.pNext = nullptr;

  return engine.createObject<
      std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>>(
      vkw::ImageView<vkw::COLOR, vkw::V2D>(engine.context().device(), image,
                                           image.format()),
      vkw::Sampler(engine.context().device(), info));
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
  transitLayout1.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
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
  transitLayout2.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  transitLayout2.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;

  VkBufferImageCopy copy{};
  copy.imageExtent = {dst.width(), dst.height(), 1};
  copy.imageSubresource.mipLevel = 0;
  copy.imageSubresource.layerCount = 1;
  copy.imageSubresource.baseArrayLayer = 0;
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  commands.imageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                              {&transitLayout1, 1});
  commands.copyBufferToImage(src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             {&copy, 1});
  commands.imageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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
  std::wstring wpath = std::filesystem::path(path) += *foundExisting;
  std::string ascii_path{wpath.begin(), wpath.end()};
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

FObject::Ptr Texture::load(FramedEngine &engine, CopyEngine &ce,
                           const std::filesystem::path &path) {
  auto imageInfo = readImageFile(path);
  VmaAllocationCreateInfo allocInfo{};

  auto &device = ce.context().device();
  allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  int transferUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  auto ret = engine.createObject<vkw::Image<vkw::COLOR, vkw::I2D>>(
      ce.context().getDeviceAllocator(), allocInfo, VK_FORMAT_R8G8B8A8_UNORM,
      static_cast<uint32_t>(imageInfo.width),
      static_cast<uint32_t>(imageInfo.height), 1, 1, 1,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  auto copyFuture = ce.copy(std::make_unique<ImageInit>(
      vkw::StagingBuffer<unsigned char>(ce.context().getDeviceAllocator(),
                                        imageInfo.data),
      ret->as<vkw::Image<vkw::COLOR, vkw::I2D>>()));
  copyFuture.wait();
  return ret;
}

} // namespace imvk::examples