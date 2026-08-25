#pragma once

#include "imvk/base/Frame.hpp"
#include "imvk/graph/Attributes.hpp"
#include "imvk/graph/Context.hpp"
#include "imvk/graphics/Engine.hpp"
#include <boost/compat/function_ref.hpp>
#include <boost/compat/move_only_function.hpp>

#include <variant>
#include <vkw/Image.hpp>

namespace imvk::graph {
class MaterializationContext;

using MatNode = boost::compat::move_only_function<void(
    vkw::BufferRecorder &recorder, const imvk::Frame &frame)>;

class MatRegularImage
    : public FONodeImpl<imvk::fon_type::swap, imvk::fon_rec::expir> {
public:
  MatRegularImage(auto &&...args)
      : FONodeImpl<imvk::fon_type::swap, imvk::fon_rec::expir>(
            std::forward<decltype(args)>(args)...) {}
  virtual VkImage image(FrameID id) const = 0;
  virtual VkImage useImage(const Frame &id) = 0;
  virtual const VkImageCreateInfo &info() const = 0;
};

class MatSwapchainImage
    : public FONodeImpl<imvk::fon_type::ext, imvk::fon_rec::expir> {
public:
  MatSwapchainImage(auto &&...args)
      : FONodeImpl<imvk::fon_type::ext, imvk::fon_rec::expir>(
            std::forward<decltype(args)>(args)...) {}
  virtual VkImage image(FrameID id) const = 0;
  virtual VkImage useImage(const Frame &id) = 0;
  virtual const VkImageCreateInfo &info() const = 0;
};

using MatImage = std::variant<Ref<MatRegularImage>, Ref<MatSwapchainImage>>;

class MatRegularImageView
    : public FONodeImpl<imvk::fon_type::swap, imvk::fon_rec::expir> {
public:
  MatRegularImageView(auto &&...args)
      : FONodeImpl<imvk::fon_type::swap, imvk::fon_rec::expir>(
            std::forward<decltype(args)>(args)...) {}
  virtual VkImageView view(FrameID id) const = 0;
  virtual VkImageView useView(const Frame &id) = 0;
  virtual const VkImageViewCreateInfo &info() const = 0;
};

class MatSwapchainImageView
    : public FONodeImpl<imvk::fon_type::ext, imvk::fon_rec::expir> {
public:
  MatSwapchainImageView(auto &&...args)
      : FONodeImpl<imvk::fon_type::ext, imvk::fon_rec::expir>(
            std::forward<decltype(args)>(args)...) {}
  virtual VkImageView view(FrameID id) const = 0;
  virtual VkImageView useView(const Frame &id) = 0;
  virtual const VkImageViewCreateInfo &info() const = 0;
};

using MatImageView =
    std::variant<Ref<MatRegularImageView>, Ref<MatSwapchainImageView>>;

using MatIntegerScalar = size_t;

using MatExtents = VkExtent3D;

class ExtentsProducer {
public:
  virtual std::optional<MatExtents>
  getExtents(const MaterializationContext &ctx, Value &result) = 0;

  virtual ~ExtentsProducer() = default;
};

struct ImageValueBinding {
  VkImageViewCreateInfo viewInfo{};
  Value *def;
  ImageValueBinding(Value *d = nullptr) : def(d) {}
};

struct ImageValueChain {
  VkImageCreateInfo imageInfo{};
  boost::container::small_vector<ImageValueBinding, 2> chain;
};

std::vector<ImageValueChain> materializeImageValueChains(Workflow &wf);

class MaterializationContext {
public:
  MaterializationContext(GraphicsEngine &ge, Workflow &wf);
  template <typename T> using MatMap = std::unordered_map<Value *, T>;

  bool startsImageChain(Value &val);
  const VkImageCreateInfo &chainImageTemplate(Value &val);
  void materializeImageChain(Value &val, MatImage &&image);
  void materializeNode(Node &n, MatNode &&action);

  template <typename T> const T &get(Value &v) const {
    return std::get<MatMap<T>>(m_mats).at(&v);
  }
  template <typename T> bool has(Value &v) const {
    return std::get<MatMap<T>>(m_mats).contains(&v);
  }
  template <typename T> void materialize(Value &v, T &&obj) {
    std::get<MatMap<std::remove_reference_t<T>>>(m_mats).insert(
        {&v, std::forward<T>(obj)});
  }

  void run(vkw::BufferRecorder &recorder, const imvk::Frame &frame) {
    for (auto &&sub : m_submissions)
      std::invoke(*sub, recorder, frame);
  }
  GraphicsEngine &engine() const { return m_engine; }

private:
  GraphicsEngine &m_engine;
  std::tuple<MatMap<MatImage>, MatMap<MatImageView>, MatMap<MatIntegerScalar>,
             MatMap<MatExtents>>
      m_mats;
  std::unordered_map<Value *, ImageValueChain> m_chains;
  std::unordered_map<Node *, MatNode> m_nodes;
  std::vector<MatNode *> m_submissions;
};

} // namespace imvk::graph