#pragma once

#include "imvk/base/EngineBase.hpp"
#include "vkw/DescriptorSet.hpp"

#include "boost/container/small_vector.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <span>

namespace imvk {

/// @brief Wrapper over vkw::DescriptorSet implementing
/// auto-resizing of pool. Sizes of descriptors are calculated based
/// on descriptor layout. This pool supports asynchronous descriptor
/// desctruction which enables sharing allocated sets to other threads.
class DescriptorPool {
public:
  DescriptorPool(vkw::Device &device, vkw::DescriptorSetLayout &&layout,
                 uint32_t setsPerPool);

  virtual ~DescriptorPool() = default;

private:
  struct PoolSettings {
    uint32_t setsPerPool;
    boost::container::small_vector<VkDescriptorPoolSize, 4> sizes;
  } m_settings;

  using PoolList = std::list<std::pair<vkw::DescriptorPool, std::mutex>>;

  struct State {
    State(vkw::Device &device, vkw::DescriptorSetLayout &&layout)
        : m_device(device), m_layout(std::move(layout)) {}
    vkw::StrongReference<vkw::Device> m_device;
    vkw::DescriptorSetLayout m_layout;
    PoolList pools;
    std::atomic<size_t> poolCount = 0u;
    std::mutex poolsMutex;
  };

  class SetDeleter {
  public:
    SetDeleter(std::shared_ptr<State> state, PoolList::iterator origPool)
        : m_state(std::move(state)), m_origPool(std::move(origPool)) {}

    void operator()(vkw::DescriptorSet *set) const;

  private:
    std::shared_ptr<State> m_state;
    PoolList::iterator m_origPool;
  };
  std::shared_ptr<State> m_state;

public:
  using SetHandle = std::unique_ptr<vkw::DescriptorSet, SetDeleter>;

  const auto &descriptorLayout() const { return m_state->m_layout; }

  /// @return range of pairs (binding id, binding info)
  auto bindingMap() const {
    auto &layout = m_state->m_layout;
    return std::ranges::iota_view{0u, layout.info().bindingCount} |
           std::views::transform([&layout](auto &&i) {
             return std::make_pair(i, layout.info().pBindings[i]);
           });
  }

  /// @brief get count of internal vkw::DescriptorPool objects.
  /// @return count of internal vkw::DescriptorPool objects.
  auto poolCount() const {
    return m_state->poolCount.load(std::memory_order_relaxed);
  }

  /// @return count of sets per pool
  auto setsPerPool() const { return m_settings.setsPerPool; }

  /// @brief Allocates new descriptor set. Thread safe.
  /// @return handle to allocated set.
  SetHandle get();
};

class Descriptable {
public:
  virtual void descriptorWrite(FrameID frame, vkw::DescriptorSet &set,
                               unsigned binding) const = 0;
  virtual ~Descriptable() = default;
};

class FramedEngine;

class Frame;

/// @brief Frame-aware descriptor set wrapper.
class DescriptorSet final
    : public FONode<DescriptorPool::SetHandle, fon_type::swap> {
public:
  DescriptorSet(FramedEngine &engine, DescriptorPool &pool,
                std::span<std::pair<Descriptable *, unsigned>> descriptors);

  vkw::DescriptorSet &use(const Frame &frame) {
    return *FONode<DescriptorPool::SetHandle, fon_type::swap>::use(frame);
  }

private:
  void onCowExpire(const Frame &frame) override;
  void onUseAction(const Frame &frame, FObject &obj) override {
    // nothing to do for now
  }
  void writeDescriptors(FrameID frame);
  boost::container::small_vector<std::pair<Descriptable *, unsigned>, 2u>
      m_bindings;
};

class DescriptorSetBuilder {
public:
  DescriptorSetBuilder(FramedEngine &engine, DescriptorPool &pool)
      : m_engine(engine), m_pool(pool){};
  void addDescriptor(Descriptable &desc, unsigned binding) {
    descriptors.emplace_back(&desc, binding);
  }
  operator Ref<DescriptorSet>() && {
    return m_engine.createNode<DescriptorSet>(m_pool, descriptors);
  }

private:
  FramedEngine &m_engine;
  DescriptorPool &m_pool;
  boost::container::small_vector<std::pair<Descriptable *, unsigned>, 2u>
      descriptors;
};

} // namespace imvk