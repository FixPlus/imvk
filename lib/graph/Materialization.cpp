#include "imvk/graph/Materialization.hpp"
#include "imvk/graph/Nodes.hpp"
#include <iostream>
namespace imvk::graph {

class RegularImageView {
public:
  RegularImageView(const vkw::Device &device, VkImageView view)
      : m_imageView(view, ViewDestructor{device}) {}

  operator VkImageView() const noexcept { return m_imageView.get(); }

private:
  struct ViewDestructor {
    vkw::StrongReference<vkw::Device const> device;
    void operator()(VkImageView view) {
      if (!view)
        return;
      device.get().core<1, 0>().vkDestroyImageView(device.get(), view,
                                                   vkw::HostAllocator::get());
    }
  };
  std::unique_ptr<VkImageView_T, ViewDestructor> m_imageView;
};

class RegularImageViewNode : public MatRegularImageView {
public:
  RegularImageViewNode(FramedEngine &engine, const MaterializationContext &ctx,
                       MatRegularImage &image,
                       const VkImageViewCreateInfo &info)
      : MatRegularImageView(FOUses{image}), m_info(info) {}
  VkImageView view(FrameID id) const final {
    return get(id).as<RegularImageView>();
  }
  VkImageView useView(const Frame &id) final {
    return use(id).as<RegularImageView>();
  }
  const VkImageViewCreateInfo &info() const final { return m_info; }

private:
  static VkImageAspectFlags m_aspectFor(VkFormat format);
  static bool m_updateCreateInfo(VkImageViewCreateInfo &info,
                                 const MatIntegerScalar &format,
                                 const MatIntegerScalar &layers,
                                 const MatIntegerScalar &levels) {
    bool outdated = false;
    outdated |=
        info.format !=
        std::exchange(info.format,
                      static_cast<VkFormat>(static_cast<unsigned>(format)));
    outdated |= info.subresourceRange.layerCount !=
                std::exchange(info.subresourceRange.layerCount,
                              static_cast<unsigned>(layers));
    outdated |= info.subresourceRange.layerCount !=
                std::exchange(info.subresourceRange.levelCount,
                              static_cast<unsigned>(levels));
    outdated |=
        info.subresourceRange.aspectMask !=
        std::exchange(
            info.subresourceRange.aspectMask,
            m_aspectFor(static_cast<VkFormat>(static_cast<unsigned>(format))));
    if (outdated) {
      info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
      info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
      info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
      info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    }
    return outdated;
  }
  void onUseAction(const Frame &frame, FObject &obj) final {
    // do nothing
  }
  FObject::Ptr constructNew(FramedEngine &engine, FrameID frame) final {
    VkImageViewCreateInfo infoCopy = m_info;
    infoCopy.image = getUse<MatRegularImage>(0).image(frame);
    auto &device = engine.context().device();
    VkImageView ret{};
    // todo check result.
    device.core<1, 0>().vkCreateImageView(device, &infoCopy,
                                          vkw::HostAllocator::get(), &ret);
    return engine.createObject<RegularImageView>(device, ret);
  }
  bool keepAlive() final { return false; }
  VkImageViewCreateInfo m_info{};
};

class SwapchainImageViewNode : public MatSwapchainImageView {
public:
  SwapchainImageViewNode(FramedEngine &engine,
                         const MaterializationContext &ctx,
                         MatSwapchainImage &image,
                         const VkImageViewCreateInfo &info)
      : MatSwapchainImageView(FOUses{image}), m_info(info) {}
  VkImageView view(FrameID id) const final {
    return get(id).as<RegularImageView>();
  }
  VkImageView useView(const Frame &id) final {
    return use(id).as<RegularImageView>();
  }
  const VkImageViewCreateInfo &info() const final { return m_info; }

private:
  unsigned getExtIndex(const Frame &frame) const final {
    return static_cast<GraphicsEngine &>(frame.engine())
        .swapchain()
        .get()
        .currentImage();
  }
  void onUseAction(const Frame &frame, FObject &obj) final {
    // do nothing
  }
  void
  constructNew(FramedEngine &engine,
               boost::container::small_vector_base<FObject::Ptr> &res) final {
    auto &swap = static_cast<GraphicsEngine &>(engine).swapchain().get();
    auto images = swap.images();
    auto &parent = getUse<MatSwapchainImage>(0);
    std::ranges::transform(
        std::ranges::iota_view{0ul, std::ranges::size(images)},
        std::back_inserter(res), [&](auto index) {
          VkImageViewCreateInfo infoCopy = m_info;
          infoCopy.image = parent.image(index);
          auto &device = engine.context().device();
          VkImageView ret{};
          // todo check result.
          device.core<1, 0>().vkCreateImageView(
              device, &infoCopy, vkw::HostAllocator::get(), &ret);
          return engine.createObject<RegularImageView>(device, ret);
        });
  }
  VkImageViewCreateInfo m_info;
};

static MatImageView createImageView(const MaterializationContext &ctx,
                                    const MatImage &image,
                                    const VkImageViewCreateInfo &info) {
  if (std::holds_alternative<Ref<MatRegularImage>>(image))
    return ctx.engine().createNode<RegularImageViewNode>(
        ctx, *std::get<Ref<MatRegularImage>>(image), info);
  return ctx.engine().createNode<SwapchainImageViewNode>(
      ctx, *std::get<Ref<MatSwapchainImage>>(image), info);
}

namespace {
struct OrderCompare {
  const Workflow *pwf;
  OrderCompare(Workflow &wf) : pwf(&wf) {}
  bool operator()(const Node &a, const Node &b) const {
    return std::distance(pwf->begin(), pwf->iteratorTo(&a)) <
           std::distance(pwf->begin(), pwf->iteratorTo(&b));
  }
  bool operator()(const Use &a, const Use &b) const {
    return std::invoke(*this, a.user(), b.user());
  }
  bool operator()(const Use *a, const Use *b) const {
    return std::invoke(*this, a->user(), b->user());
  }
};

} // namespace

static const ImageDefInfo &imageDef(const Value &v) {
  return static_cast<const ImageDefInfo &>(v.info());
}

static const ImageUseInfo &imageUse(const Use &v) {
  return static_cast<const ImageUseInfo &>(*v.info());
}

static void fillInInfo(ImageValueChain &chain, const AttributesAnalysis &aa) {
  auto &imageInfo = chain.imageInfo;
  auto &definingOp = *chain.chain.front().def;
  auto &definingAttrs = aa.getAttributesFor<Attributes<ImageTy>>(definingOp);
  auto &defType = static_cast<const ImageTy &>(definingOp.type());
  auto &defInfo = imageDef(definingOp);

  VkImageViewCreateInfo commonView{};
  commonView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  commonView.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
  commonView.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
  commonView.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
  commonView.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
  commonView.viewType = [&]() {
    switch (defType.imageType) {
    case VK_IMAGE_TYPE_1D:
      return VK_IMAGE_VIEW_TYPE_1D;
    case VK_IMAGE_TYPE_2D:
      return VK_IMAGE_VIEW_TYPE_2D;
    case VK_IMAGE_TYPE_3D:
      return VK_IMAGE_VIEW_TYPE_3D;
    }
    return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
  }();

  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.pNext = nullptr;
  imageInfo.flags = 0;
  imageInfo.imageType = defType.imageType;
  if (auto format = definingAttrs.format.getConstant())
    imageInfo.format = *format;
  if (auto extents = definingAttrs.extents.getConstant())
    imageInfo.extent = *extents;
  if (auto levels = definingAttrs.levels.getConstant())
    imageInfo.mipLevels = *levels;
  if (auto layers = definingAttrs.layers.getConstant())
    imageInfo.arrayLayers = *layers;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // defInfo.access.layout;

  for (auto &binding : chain.chain) {
    const ImageDefInfo &def = imageDef(*binding.def);
    for (auto &use : binding.def->users()) {
      const ImageUseInfo &info = imageUse(use);
      imageInfo.usage |= info.access.usage;
    }
    imageInfo.usage |= def.access.usage;
    binding.viewInfo = commonView;
  }
}
std::vector<ImageValueChain> materializeImageValueChains(Workflow &wf) {
  auto aa = AttributesAnalysis{wf};
  std::unordered_set<Value *> visited;
  std::vector<ImageValueChain> chains;
  auto isImage = [](Value &val) { return isa<ImageTy>(&val.type()); };
  auto nonPassthrough = [](Value &val) { return !imageDef(val).passthrough; };
  auto nonEmptyUse = [](Use &use) { return !imageUse(use).access.empty(); };
  auto processValue = [&](Value &val, ImageValueChain &chain) -> Value * {
    boost::container::small_vector<Use *, 4> uses;
    std::ranges::transform(val.users() | std::views::filter(nonEmptyUse),
                           std::back_inserter(uses),
                           [](auto &use) { return &use; });
    std::sort(uses.begin(), uses.end(), OrderCompare{wf});

    auto lastBoundUse = uses.end();

    for (auto &use : uses | std::views::reverse) {
      auto &useInfo = imageUse(*use);
      if (!useInfo.passthrough)
        continue;
      if (*std::prev(lastBoundUse) == use)
        continue;
      // create a copy of object just before this use and relink further uses to
      // new copy.
      WorkflowBuilder bldr{wf, use->user()};
      Node *copyImage = bldr.create<Copy<ImageTy>>(val);
      auto &copyImageVal = copyImage->results().front();
      aa.insertValue(&copyImageVal, &aa.getAttributesFor(val));
      Use *copyUse = &copyImage->uses().front();
      for (Use *u : std::span<Use *>(&use + 1, &*lastBoundUse)) {
        u->replaceBy(&copyImageVal);
      }
      auto nextUse = decltype(lastBoundUse){&use + 1};
      *nextUse = copyUse;
      std::swap(use, *nextUse);
      lastBoundUse = std::next(nextUse);
    }

    uses.erase(lastBoundUse, uses.end());

    Value *currentValue = &val;
    chain.chain.emplace_back(currentValue);

    if (uses.empty())
      return nullptr;

    // if value is obtained not from barrier operation, we must insert barrier
    // between def and first use regardless of layout match.

    if (!isa<Barrier<ImageTy>>(&currentValue->node())) {
      WorkflowBuilder bldr{wf, uses.front()->user()};
      auto &defInfo = imageDef(*currentValue);
      auto &useInfo = imageUse(*uses.front());

      auto *attrs = &aa.getAttributesFor(*currentValue);
      currentValue =
          &bldr.create<Barrier<ImageTy>>(*currentValue, defInfo.access.layout,
                                         useInfo.access.layout)
               ->results()
               .front();
      chain.chain.emplace_back(currentValue);
      aa.insertValue(currentValue, attrs);
      for (auto &u : uses) {
        u->replaceBy(currentValue);
      }
    }
    const ImageDefInfo *currentDefInfo = &imageDef(*currentValue);
    for (auto useIt = std::next(uses.begin()); useIt != uses.end(); ++useIt) {
      auto *useInfo = &imageUse(**useIt);
      auto defLayout = currentDefInfo->access.layout;
      auto useLayout = useInfo->access.layout;
      if (defLayout == useLayout)
        continue;
      WorkflowBuilder bldr{wf, (*useIt)->user()};
      auto *attrs = &aa.getAttributesFor(*currentValue);
      currentValue =
          &bldr.create<Barrier<ImageTy>>(*currentValue, defLayout, useLayout)
               ->results()
               .front();
      aa.insertValue(currentValue, attrs);
      currentDefInfo = static_cast<const ImageDefInfo *>(&currentValue->info());
      chain.chain.emplace_back(currentValue);
      for (auto &u : std::ranges::subrange(useIt, std::end(uses))) {
        u->replaceBy(currentValue);
      }
    }
    auto *lastUse = uses.back();
    auto *lastUseInfo = static_cast<const ImageUseInfo *>(lastUse->info());
    if (auto nextIndex = lastUseInfo->passthrough) {
      return &lastUse->user().results()[*nextIndex];
    }
    return nullptr;
  };

  for (auto &&node : wf) {
    for (auto &&val : node.results() | std::views::filter([&](auto &v) {
                        return isImage(v) && nonPassthrough(v);
                      })) {
      auto &nextChain = chains.emplace_back();
      auto *currentValue = &val;
      while (currentValue = processValue(*currentValue, nextChain))
        ;
      fillInInfo(nextChain, aa);
    }
  }
  return chains;
}

bool MaterializationContext::startsImageChain(Value &val) {
  return m_chains.contains(&val);
}
const VkImageCreateInfo &
MaterializationContext::chainImageTemplate(Value &val) {
  assert(m_chains.contains(&val));
  return m_chains.at(&val).imageInfo;
}

static void completeImageViewInfo(VkImageViewCreateInfo &info,
                                  MatImage &image) {
  auto &imgInfo = std::visit(
      [](auto &pimg) -> decltype(auto) { return pimg->info(); }, image);
  info.format = imgInfo.format;
  info.subresourceRange.layerCount = imgInfo.arrayLayers;
  info.subresourceRange.levelCount = imgInfo.mipLevels;
  info.subresourceRange.aspectMask =
      vkw::ImageInterface::isColorFormat(imgInfo.format)
          ? VK_IMAGE_ASPECT_COLOR_BIT
          : VK_IMAGE_ASPECT_DEPTH_BIT;
}

void MaterializationContext::materializeImageChain(Value &val,
                                                   MatImage &&image) {
  assert(m_chains.contains(&val));
  auto &chain = m_chains.at(&val);
  assert(!chain.chain.empty());
  auto viewInfo = chain.chain.front().viewInfo;
  completeImageViewInfo(viewInfo, image);
  auto view = createImageView(*this, image, viewInfo);
  for (auto &&c : chain.chain) {
    std::get<MatMap<MatImage>>(m_mats)[c.def] = image;
    std::get<MatMap<MatImageView>>(m_mats)[c.def] = view;
  }
}
void MaterializationContext::materializeNode(Node &n, MatNode &&action) {
  m_nodes[&n] = std::move(action);
}

MaterializationContext::MaterializationContext(GraphicsEngine &ge, Workflow &wf)
    : m_engine(ge) {
  {
    auto imageChains = materializeImageValueChains(wf);
    for (auto &chain : imageChains) {
      assert(!chain.chain.empty());
      auto *def = chain.chain.front().def;
      m_chains[def] = std::move(chain);
    }
    imageChains.clear();
  }
  for (auto &&n : wf) {
    n.materialize(*this);
  }
  for (auto &&n : wf) {
    if (m_nodes.contains(&n)) {
      m_submissions.emplace_back(&m_nodes.at(&n));
    }
  }
}

} // namespace imvk::graph