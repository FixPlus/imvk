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

class DescriptorPool final
    : public FONode<std::unique_ptr<IDescriptorPoolState>, fon_type::mut> {
public:
  using SetHandle = IDescriptorPoolState::SetHandle;

  DescriptorPool(FramedEngine &engine,
                 std::unique_ptr<IDescriptorPoolState> state)
      : FONode<std::unique_ptr<IDescriptorPoolState>, fon_type::mut>(
            engine.createObject<std::unique_ptr<IDescriptorPoolState>>(
                std::move(state))) {}
  const auto &descriptorLayout() const { return get()->layout(); }

  SetHandle createSet() { return get()->createSet(); }

private:
  void onUse(const Frame &frame) final {
    // do nothing.
  }
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

class Descriptable {
public:
  virtual void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                               unsigned binding) const = 0;
  virtual ~Descriptable() = default;
};

class FramedEngine;

class Frame;

template <fon_rec = fon_rec::rec> class DescriptorSet {};

/// @brief Frame-aware descriptor set wrapper.
template <>
class DescriptorSet<fon_rec::rec> final
    : public FONode<DescriptorPool::SetHandle, fon_type::swap, fon_rec::rec> {
public:
  DescriptorSet(FramedEngine &engine, DescriptorPool &pool,
                std::span<std::pair<Descriptable *, unsigned>> descriptors);

  vkw::DescriptorSet &use(const Frame &frame) {
    return *FONode<DescriptorPool::SetHandle, fon_type::swap,
                   fon_rec::rec>::use(frame);
  }
  DescriptorPool &pool() { return getUse<DescriptorPool &>(0); }

private:
  bool keepAlive() final { return true; }
  void onUseAction(const Frame &frame, FObject &obj) final;

  FObject::Ptr constructNew(FramedEngine &engine, FrameID id) final;
  void writeDescriptors(vkw::DescriptorSet &set, FrameID frame);
  void writeDescriptors(FrameID frame);

  boost::container::small_vector<std::pair<Descriptable *, unsigned>, 2u>
      m_bindings;
  std::bitset<8> m_pendingWrites;
};

template <>
class DescriptorSet<fon_rec::expir> final
    : public FONode<DescriptorPool::SetHandle, fon_type::swap, fon_rec::expir> {
public:
  DescriptorSet(FramedEngine &engine, DescriptorPool &pool,
                std::span<std::pair<Descriptable *, unsigned>> descriptors);

  vkw::DescriptorSet &use(const Frame &frame) {
    return *FONode<DescriptorPool::SetHandle, fon_type::swap,
                   fon_rec::expir>::use(frame);
  }
  DescriptorPool &pool() { return getUse<DescriptorPool &>(0); }

private:
  void onUseAction(const Frame &frame, FObject &obj) final {
    // do nothing.
  }
};

template <fon_rec F = fon_rec::rec> class DescriptorSetBuilder {
public:
  DescriptorSetBuilder(FramedEngine &engine, DescriptorPool &pool)
      : m_engine(engine), m_pool(&pool){};
  DescriptorSetBuilder &addDescriptor(Descriptable &desc, unsigned binding) & {
    descriptors.emplace_back(&desc, binding);
    return *this;
  }
  DescriptorSetBuilder &&addDescriptor(Descriptable &desc,
                                       unsigned binding) && {
    descriptors.emplace_back(&desc, binding);
    return std::move(*this);
  }
  operator Ref<DescriptorSet<F>>() && {
    return m_engine.createNode<DescriptorSet<F>>(*m_pool, descriptors);
  }

private:
  FramedEngine &m_engine;
  Ref<DescriptorPool> m_pool;
  boost::container::small_vector<std::pair<Descriptable *, unsigned>, 2u>
      descriptors;
};

} // namespace imvk