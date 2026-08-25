#pragma once
#include "imvk/base/DescriptorSet.hpp"
#include "imvk/copy/Engine.hpp"
#include "imvk/graphics/Engine.hpp"

#include "vkw/Image.hpp"
#include <filesystem>

namespace imvk::examples {

std::filesystem::path assetsDir();

class Texture : public FONode<vkw::Image<vkw::COLOR, vkw::I2D>, fon_type::cow> {
public:
  Texture(FramedEngine &eng, FObject::Ptr obj)
      : FONode<vkw::Image<vkw::COLOR, vkw::I2D>, fon_type::cow>(
            std::move(obj)) {}
  static FObject::Ptr load(FramedEngine &engine, CopyEngine &,
                           const std::filesystem::path &path);
  imvk::FObject::Ptr constructNew(imvk::FramedEngine &) noexcept final {
    return nullptr;
  }
};

class SampledView final
    : public FONode<
          std::pair<vkw::ImageView<vkw::COLOR, vkw::V2D>, vkw::Sampler>,
          fon_type::cow>,
      public Descriptable {
public:
  SampledView(FramedEngine &eng, Texture &texture);

  void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                       unsigned binding) const final;

private:
  FObject::Ptr constructNew(FramedEngine &engine) noexcept final;
  static FObject::Ptr
  doConstructNew(FramedEngine &engine,
                 const vkw::Image<vkw::COLOR, vkw::I2D> &image);
};

} // namespace imvk::examples