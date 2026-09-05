#pragma once
#include "imvk/base/DescriptorSet.hpp"
#include "imvk/copy/Engine.hpp"
#include "imvk/graphics/Engine.hpp"

#include "vkw/Image.hpp"
#include <filesystem>

namespace imvk::examples {

std::filesystem::path assetsDir();

class TextureImpl : public FONode<vkw::Image<vkw::COLOR, vkw::I2D>,
                                  fon_type::cow, TextureImpl> {
public:
  TextureImpl(FramedEngine &eng, vkw::Image<vkw::COLOR, vkw::I2D> &&obj)
      : FONode<vkw::Image<vkw::COLOR, vkw::I2D>, fon_type::cow, TextureImpl>(
            eng, std::move(obj)) {}

  vkw::Image<vkw::COLOR, vkw::I2D> constructNew(imvk::FramedEngine &) {
    return std::move(*(vkw::Image<vkw::COLOR, vkw::I2D> *)(nullptr));
  }
};

class Texture : public FONodeView<TextureImpl> {
public:
  Texture(auto &&...args)
      : FONodeView<TextureImpl>(std::forward<decltype(args)>(args)...) {}
  static vkw::Image<vkw::COLOR, vkw::I2D>
  load(FramedEngine &engine, CopyEngine &, const std::filesystem::path &path);
  static vkw::Image<vkw::COLOR, vkw::I2D>
  load(FramedEngine &engine, CopyEngine &, std::span<const unsigned char> data,
       unsigned width, unsigned height);
};

class SampledViewImpl final
    : public FONode<
          std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>,
          fon_type::cow, SampledViewImpl> {
public:
  template <std::convertible_to<Texture> T>
  SampledViewImpl(FramedEngine &eng, T &&texture, bool noSampler = false,
                  VkFilter filter = VK_FILTER_LINEAR)
      : FONode<std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>,
               fon_type::cow, SampledViewImpl>(
            eng, doConstructNew(eng, texture->get(), filter), FOUses{texture}),
        m_filter(filter), m_noSampler(noSampler) {}

  void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                       unsigned binding) const;

  std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>
  constructNew(FramedEngine &engine);
  static std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>
  doConstructNew(FramedEngine &engine,
                 const vkw::Image<vkw::COLOR, vkw::I2D> &image,
                 VkFilter filter);

  bool noSampler() const { return m_noSampler; }

private:
  VkFilter m_filter;
  bool m_noSampler;
};

class SampledView : public FONodeView<SampledViewImpl> {
public:
  SampledView(auto &&...args)
      : FONodeView<SampledViewImpl>(std::forward<decltype(args)>(args)...) {}
  static void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                              FONodeBase &obj, unsigned binding);
};

} // namespace imvk::examples