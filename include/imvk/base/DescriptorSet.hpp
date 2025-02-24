#pragma once

#include "imvk/base/Frame.hpp"

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
  auto poolCount() const { return m_state->poolCount.load(); }

  /// @return count of sets per pool
  auto setsPerPool() const { return m_settings.setsPerPool; }

  /// @brief Allocates new descriptor set. Thread safe.
  /// @return handle to allocated set.
  SetHandle get();
};

class PrimitiveHandleBase;
class Primitive;
class FramedEngine;

/// @brief Wrapper over vkw::DescriptorSet that saves references to
/// bound resources that prolongs their life. That way frame object can keep
/// bound resources by just storing a reference to this set.
class DescriptorSetHandle final : public FrameObject {
public:
  DescriptorSetHandle(FramedEngine &engine, DescriptorPool &pool);

  /// @brief Writes new primitive to descriptor.
  /// @param Primitive to be written over current one.
  /// @param binding number of binding point
  void write(std::shared_ptr<PrimitiveHandleBase> Primitive, unsigned binding,
             unsigned writeOpID);

  auto &primitive(unsigned binding) const {
    return m_boundPrimitives.at(binding);
  }

  auto &set() const { return *m_set; }

  ~DescriptorSetHandle();

private:
  boost::container::small_vector<std::shared_ptr<PrimitiveHandleBase>, 3>
      m_boundPrimitives;
  DescriptorPool::SetHandle m_set;
};

class Frame;

/// @brief Frame-aware descriptor set wrapper. Behaves in similar fashion as
/// Primitive class.
class DescriptorSet final {
public:
  DescriptorSet(FramedEngine &engine, DescriptorPool &pool,
                std::span<std::pair<Primitive *, unsigned>> primitives);

  DescriptorSet(const DescriptorSet &) = delete;
  DescriptorSet(DescriptorSet &&) noexcept = default;

  DescriptorSet &operator=(const DescriptorSet &) = delete;
  DescriptorSet &operator=(DescriptorSet &&) noexcept = default;

  const std::shared_ptr<DescriptorSetHandle> &get(const Frame &frame) const;

  ~DescriptorSet();

private:
  boost::container::small_vector<std::pair<Primitive *, unsigned>, 3>
      m_primitives;
  boost::container::small_vector<std::shared_ptr<DescriptorSetHandle>, 3>
      m_sets;
  bool m_fullCow = true;
};

} // namespace imvk