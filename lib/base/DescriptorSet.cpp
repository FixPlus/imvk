
#include "imvk/base/DescriptorSet.hpp"
#include "imvk/base/EngineBase.hpp"

#include <boost/container/flat_map.hpp>

namespace imvk {
DescriptorPool::DescriptorPool(vkw::Device &device,
                               vkw::DescriptorSetLayout &&layout,
                               uint32_t setsPerPool)
    : m_settings([&]() {
        PoolSettings ret{};
        ret.setsPerPool = setsPerPool;
        boost::container::small_flat_map<VkDescriptorType, size_t, 4> counts;
        for (auto &&binding : layout.bindings()) {
          counts.try_emplace(binding.descriptorType, 0u).first->second +=
              setsPerPool * binding.descriptorCount;
        }
        std::ranges::transform(counts, std::back_inserter(ret.sizes),
                               [&setsPerPool](auto &&pair) {
                                 VkDescriptorPoolSize ret{};
                                 ret.type = pair.first;
                                 ret.descriptorCount = pair.second;
                                 return ret;
                               });
        return ret;
      }()),
      m_state(std::make_shared<State>(device, std::move(layout))) {}

void DescriptorPool::SetDeleter::operator()(vkw::DescriptorSet *set) const {
  auto lock = std::unique_lock(m_origPool->second);
  delete set;
  if (m_origPool->first.currentSetsCount() != 0u)
    return;
  lock.unlock();
  auto listLock = std::unique_lock(m_state->poolsMutex);
  auto &list = m_state->pools;
  // checks list.size() == 1, but faster.
  if (std::prev(list.end()) == list.begin())
    return;
  PoolList localList;
  localList.splice(localList.end(), list, m_origPool);
  listLock.unlock();
  m_origPool->second.lock();
  if (m_origPool->first.currentSetsCount() == 0u) {
    m_state->poolCount.fetch_sub(1u);
    return;
  }
  // erase failed. return pool back to list.
  m_origPool->second.unlock();
  listLock.lock();
  list.splice(list.begin(), localList);
}

DescriptorPool::SetHandle DescriptorPool::get() {
  auto listLock = std::lock_guard(m_state->poolsMutex);
  auto &list = m_state->pools;
  auto startIt = list.begin();
  auto curIt = startIt;
  if (startIt != list.end())
    do {
      auto &pool = *curIt;
      auto poolLock = std::unique_lock(pool.second);
      if (pool.first.currentSetsCount() == m_settings.setsPerPool) {
        poolLock.unlock();
        // swing node to end.
        list.splice(list.end(), list, curIt);
        curIt = list.begin();
        continue;
      }
      return {new vkw::DescriptorSet(pool.first, m_state->m_layout),
              SetDeleter(m_state, curIt)};
    } while (startIt != curIt);

  // no space in current pools, allocate new
  list.emplace_front(
      std::piecewise_construct,
      std::make_tuple(std::ref(m_state->m_device.get()), m_settings.setsPerPool,
                      std::span<const VkDescriptorPoolSize>(m_settings.sizes),
                      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT),
      std::make_tuple());
  m_state->poolCount.fetch_add(1u);
  auto poolIt = list.begin();
  return {new vkw::DescriptorSet(poolIt->first, m_state->m_layout),
          SetDeleter(m_state, poolIt)};
}

void DescriptorSet::onCowExpire(const Frame &frame) {
  // need to rewrite all bindings.
  writeDescriptors(frame.id());
}
void DescriptorSet::writeDescriptors(FrameID frame) {
  auto &set = get(frame);
  for (auto &&[child, binding] : m_bindings) {
    child->descriptorWrite(frame, *set, binding);
  }
}

DescriptorSet::DescriptorSet(
    FramedEngine &engine, DescriptorPool &pool,
    std::span<std::pair<Descriptable *, unsigned>> bindings)
    : FONode<DescriptorPool::SetHandle, fon_type::swap>(
          engine,
          [&](FrameID id) {
            return engine.createObject<DescriptorPool::SetHandle>(pool.get());
          },
          [&]() {
            boost::container::small_vector<FONodeBase *, 2u> binds;
            std::ranges::transform(
                bindings, std::back_inserter(binds),
                [](auto &&p) { return dynamic_cast<FONodeBase *>(p.first); });
            return binds;
          }()) {
  std::ranges::copy(bindings, std::back_inserter(m_bindings));
  std::ranges::for_each(engine.frameIds(),
                        [this](FrameID frame) { writeDescriptors(frame); });
}

} // namespace imvk