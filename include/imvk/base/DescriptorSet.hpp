#pragma once

#include "imvk/base/EngineBase.hpp"
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
               DescriptorPoolPimpl>(
            engine.createObject<std::unique_ptr<IDescriptorPoolState>>(
                std::move(state))) {}

  void onUse(const Frame &frame) final {
    // do nothing.
  }
};

class DescriptorPool : public FONodeView<DescriptorPoolPimpl> {
public:
  using SetHandle = IDescriptorPoolState::SetHandle;
  DescriptorPool(auto &&...args)
      : FONodeView<DescriptorPoolPimpl>(std::forward<decltype(args)>(args)...) {
  }
  const auto &descriptorLayout() const { return (*this)->get()->layout(); }

  SetHandle createSet() { return (*this)->get()->createSet(); }
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
  DescriptorSetImpl(FramedEngine &engine, DescriptorPool &pool,
                    std::span<const Descriptor> descriptors);

  void onUseAction(const Frame &frame, DescriptorPool::SetHandle &obj) {
    // nothing to do for now.
  }

  FObject::Ptr constructNew(FramedEngine &engine, FrameID id);

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
  DescriptorPool pool() { return (*this)->getUse<DescriptorPool>(0); }
};

class DescriptorSetBuilder {
public:
  DescriptorSetBuilder(FramedEngine &engine, DescriptorPool pool)
      : m_engine(engine), m_pool(std::move(pool)){};
  template <typename T>
  DescriptorSetBuilder &addDescriptor(const T &desc, unsigned binding) & {
    descriptors.emplace_back(&*desc, &T::descriptorWrite, binding);
    return *this;
  }
  template <typename T>
  DescriptorSetBuilder &&addDescriptor(const T &desc, unsigned binding) && {
    descriptors.emplace_back(&*desc, &T::descriptorWrite, binding);
    return std::move(*this);
  }
  operator DescriptorSet() && {
    return DescriptorSet(m_engine, m_pool, descriptors);
  }

  bool empty() const { return descriptors.empty(); }

private:
  FramedEngine &m_engine;
  DescriptorPool m_pool;
  boost::container::small_vector<DescriptorSetImpl::Descriptor, 2u> descriptors;
};

} // namespace imvk