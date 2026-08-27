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
  TextureImpl(FramedEngine &eng, FObject::Ptr obj)
      : FONode<vkw::Image<vkw::COLOR, vkw::I2D>, fon_type::cow, TextureImpl>(
            std::move(obj)) {}

  imvk::FObject::Ptr constructNew(imvk::FramedEngine &) { return nullptr; }
};

class Texture : public FONodeView<TextureImpl> {
public:
  Texture(auto &&...args)
      : FONodeView<TextureImpl>(std::forward<decltype(args)>(args)...) {}
  static FObject::Ptr load(FramedEngine &engine, CopyEngine &,
                           const std::filesystem::path &path);
};

class SampledViewImpl final
    : public FONode<
          std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>,
          fon_type::cow, SampledViewImpl> {
public:
  template <std::convertible_to<Texture> T>
  SampledViewImpl(FramedEngine &eng, T &&texture)
      : FONode<std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>,
               fon_type::cow, SampledViewImpl>(
            doConstructNew(eng, texture->get()), FOUses{texture}) {}

  void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                       unsigned binding) const;

  FObject::Ptr constructNew(FramedEngine &engine);
  static FObject::Ptr
  doConstructNew(FramedEngine &engine,
                 const vkw::Image<vkw::COLOR, vkw::I2D> &image);
};

class SampledView : public FONodeView<SampledViewImpl> {
public:
  SampledView(auto &&...args)
      : FONodeView<SampledViewImpl>(std::forward<decltype(args)>(args)...) {}
  static void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                              FONodeBase &obj, unsigned binding);
};

} // namespace imvk::examples