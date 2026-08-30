#pragma once

#include "imvk/base/Object.hpp"

#include "vkw/DescriptorSet.hpp"

#include "boost/container/small_vector.hpp"
#include "boost/intrusive/list.hpp"
#include <atomic>
#include <bitset>
#include <memory>
#include <mutex>
#include <span>

namespace imvk {
struct IDescriptorPoolState {
  class SetDeleter {
  public:
    SetDeleter(IDescriptorPoolState &pstate, void *ctx)
        : m_pimpl(&pstate), m_ctx(ctx) {}

    void operator()(vkw::DescriptorSet *set) const {
      m_pimpl->destroySet(set, m_ctx);
    }

  private:
    IDescriptorPoolState *m_pimpl;
    void *m_ctx;
  };
  using SetHandle = std::unique_ptr<vkw::DescriptorSet, SetDeleter>;
  virtual SetHandle createSet() = 0;
  virtual void destroySet(vkw::DescriptorSet *set, void *ctx) = 0;
  virtual const vkw::DescriptorSetLayout &layout() = 0;
  virtual ~IDescriptorPoolState() = default;
};

class DescriptorPoolPimpl final
    : public FONode<std::unique_ptr<IDescriptorPoolState>, fon_type::mut,
                    DescriptorPoolPimpl> {
public:
  DescriptorPoolPimpl(FramedEngine &engine,
                      std::unique_ptr<IDescriptorPoolState> state)
      : FONode<std::unique_ptr<IDescriptorPoolState>, fon_type::mut,
               DescriptorPoolPimpl>(engine, std::move(state)) {}
};

class DescriptorPool : public FONodeView<DescriptorPoolPimpl> {
public:
  using SetHandle = IDescriptorPoolState::SetHandle;
  DescriptorPool(auto &&...args)
      : FONodeView<DescriptorPoolPimpl>(std::forward<decltype(args)>(args)...) {
  }
  const auto &descriptorLayout() const { return (*this)->get()->layout(); }

  SetHandle createSet() const { return (*this)->get()->createSet(); }
};

/// @brief Wrapper over vkw::DescriptorSet implementing
/// auto-resizing of pool. Sizes of descriptors are calculated based
/// on descriptor layout. This pool supports asynchronous descriptor
/// desctruction which enables sharing allocated sets to other threads.
class DescriptorPoolImpl final : public IDescriptorPoolState {
public:
  DescriptorPoolImpl(vkw::Device &device, vkw::DescriptorSetLayout &&layout,
                     uint32_t setsPerPool);
  /// @brief get count of internal vkw::DescriptorPool objects.
  /// @return count of internal vkw::DescriptorPool objects.
  auto poolCount() const { return m_poolCount.load(std::memory_order_relaxed); }

  /// @return count of sets per pool
  auto setsPerPool() const { return m_settings.setsPerPool; }

  SetHandle createSet() final;
  void destroySet(vkw::DescriptorSet *set, void *ctx) final;
  const vkw::DescriptorSetLayout &layout() final;

private:
  struct PoolSettings {
    uint32_t setsPerPool;
    boost::container::small_vector<VkDescriptorPoolSize, 4> sizes;
  } m_settings;

  using PoolList = std::list<std::pair<vkw::DescriptorPool, std::mutex>>;

  vkw::StrongReference<vkw::Device> m_device;
  vkw::DescriptorSetLayout m_layout;
  PoolList pools;
  std::atomic<size_t> m_poolCount = 0u;
  std::mutex poolsMutex;
};

class DescriptorSetImpl final
    : public FONode<DescriptorPool::SetHandle, fon_type::swap,
                    DescriptorSetImpl> {
public:
  using DescriptorFun = boost::compat::function_ref<void(
      FrameID, vkw::DescriptorSet &, FONodeBase &, unsigned)>;
  using Descriptor = std::tuple<FONodeRef, DescriptorFun, unsigned>;
  DescriptorSetImpl(FramedEngine &engine, DescriptorPool pool,
                    std::span<const Descriptor> descriptors);

  void onUseAction(const Frame &frame, DescriptorPool::SetHandle &obj) {
    // nothing to do for now.
  }

  template <typename Descriptor>
  void replaceDescriptor(Descriptor &&desc, unsigned index) {
    auto foundBinding =
        std::ranges::find_if(m_bindings, [index](auto &&binding) {
          return binding.second == index;
        });
    assert(foundBinding != m_bindings.end());
    foundBinding->first = std::remove_cvref_t<Descriptor>::descriptorWrite;
    replaceUseBy(index + 1, std::forward<Descriptor>(desc));
  }

  DescriptorPool::SetHandle constructNew(FramedEngine &engine, FrameID id);

private:
  void writeDescriptors(vkw::DescriptorSet &set, FrameID frame);
  void writeDescriptors(FrameID frame);

  boost::container::small_vector<std::pair<DescriptorFun, unsigned>, 2u>
      m_bindings;
};

/// @brief Frame-aware descriptor set wrapper.
class DescriptorSet : public FONodeView<DescriptorSetImpl> {
public:
  DescriptorSet(auto &&...args)
      : FONodeView<DescriptorSetImpl>(std::forward<decltype(args)>(args)...) {}
  DescriptorPool pool() const { return (*this)->getUse<DescriptorPool>(0); }

  template <typename Descriptor>
  void replaceDescriptor(Descriptor &&desc, unsigned index) {
    static_cast<DescriptorSetImpl &>(**this).replaceDescriptor(
        std::forward<Descriptor>(desc), index);
  }
};

class DescriptorImpl : public FONode<DescriptorSetImpl::DescriptorFun,
                                     fon_type::cow, DescriptorImpl> {
public:
  template <typename Descriptor>
  DescriptorImpl(FramedEngine &engine, Descriptor &&desc)
      : FONode<DescriptorSetImpl::DescriptorFun, fon_type::cow, DescriptorImpl>(
            engine, &std::remove_cvref_t<Descriptor>::descriptorWrite,
            FOUses{desc}),
        m_fun(&std::remove_cvref_t<Descriptor>::descriptorWrite) {}

  DescriptorSetImpl::DescriptorFun constructNew(FramedEngine &engine) {
    return m_fun;
  }
  DescriptorSetImpl::DescriptorFun m_fun;
};

class NullDescriptorImpl
    : public FONode<char, fon_type::mut, NullDescriptorImpl> {
public:
  NullDescriptorImpl(FramedEngine &engine)
      : FONode<char, fon_type::mut, NullDescriptorImpl>(engine, 0) {}
};

class NullDescriptor : public FONodeView<NullDescriptorImpl> {
public:
  NullDescriptor(auto &&...args)
      : FONodeView<NullDescriptorImpl>(std::forward<decltype(args)>(args)...) {}

  static void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                              FONodeBase &obj, unsigned binding) {
    assert(0 && "tried to bind null descriptor");
  }
};

// Type erased descriptor.
class Descriptor : public FONodeView<DescriptorImpl> {
public:
  Descriptor(auto &&...args)
      : FONodeView<DescriptorImpl>(std::forward<decltype(args)>(args)...) {}
  static void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                              FONodeBase &obj, unsigned binding) {
    auto &self = static_cast<DescriptorImpl &>(obj);
    std::invoke(self.get(), frame, set, self.getUseRaw(0), binding);
  }
};

class DescriptorSetBuilder {
public:
  DescriptorSetBuilder(FramedEngine &engine, DescriptorPool pool)
      : m_engine(engine), m_pool(std::move(pool)){};
  template <typename T>
  DescriptorSetBuilder &addDescriptor(T &&desc, unsigned binding) & {
    descriptors.emplace_back(&*desc, &std::remove_cvref_t<T>::descriptorWrite,
                             binding);
    return *this;
  }
  template <typename T>
  DescriptorSetBuilder &&addDescriptor(T &&desc, unsigned binding) && {
    descriptors.emplace_back(&*desc, &std::remove_cvref_t<T>::descriptorWrite,
                             binding);
    return std::move(*this);
  }
  operator DescriptorSet() && {
    return DescriptorSet(m_engine.get(), m_pool, descriptors);
  }

  bool empty() const { return descriptors.empty(); }

private:
  std::reference_wrapper<FramedEngine> m_engine;
  DescriptorPool m_pool;
  boost::container::small_vector<DescriptorSetImpl::Descriptor, 2u> descriptors;
};

} // namespace imvk