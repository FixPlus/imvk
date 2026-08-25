
#include "imvk/base/DescriptorSet.hpp"
#include "imvk/base/EngineBase.hpp"

#include <boost/container/flat_map.hpp>

namespace imvk {

DescriptorPoolImpl::DescriptorPoolImpl(vkw::Device &device,
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
      m_device(device), m_layout(std::move(layout)) {}

DescriptorPoolImpl::SetHandle DescriptorPoolImpl::createSet() {
  auto listLock = std::lock_guard(poolsMutex);
  auto &list = pools;
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
      return {new vkw::DescriptorSet(pool.first, m_layout),
              SetDeleter(*this, &*curIt)};
    } while (startIt != curIt);

  // no space in current pools, allocate new
  list.emplace_front(
      std::piecewise_construct,
      std::make_tuple(std::ref(m_device.get()), m_settings.setsPerPool,
                      std::span<const VkDescriptorPoolSize>(m_settings.sizes),
                      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT),
      std::make_tuple());
  m_poolCount.fetch_add(1u);
  auto poolIt = list.begin();
  return {new vkw::DescriptorSet(poolIt->first, m_layout),
          SetDeleter(*this, &*poolIt)};
}
void DescriptorPoolImpl::destroySet(vkw::DescriptorSet *set, void *ctx) {
  auto &origPool =
      *reinterpret_cast<std::pair<vkw::DescriptorPool, std::mutex> *>(ctx);
  auto lock = std::unique_lock(origPool.second);
  delete set;
  if (origPool.first.currentSetsCount() != 0u)
    return;
  lock.unlock();
  auto listLock = std::unique_lock(poolsMutex);
  auto &list = pools;
  // checks list.size() == 1, but faster.
  if (std::prev(list.end()) == list.begin())
    return;
  PoolList localList;
  auto foundOrig = std::ranges::find_if(
      list, [&](auto &&elem) { return &elem == &origPool; });
  assert(foundOrig != list.end());
  localList.splice(localList.end(), list, foundOrig);
  listLock.unlock();
  foundOrig->second.lock();
  if (foundOrig->first.currentSetsCount() == 0u) {
    m_poolCount.fetch_sub(1u);
    return;
  }
  // erase failed. return pool back to list.
  foundOrig->second.unlock();
  listLock.lock();
  list.splice(list.begin(), localList);
}
const vkw::DescriptorSetLayout &DescriptorPoolImpl::layout() {
  return m_layout;
}

void DescriptorSet<fon_rec::rec>::writeDescriptors(vkw::DescriptorSet &set,
                                                   FrameID frame) {
  for (auto &&[child, binding] : m_bindings) {
    child->descriptorWrite(frame, set, binding);
  }
}
void DescriptorSet<fon_rec::rec>::writeDescriptors(FrameID frame) {
  auto &set = get(frame);
  writeDescriptors(*set, frame);
}

DescriptorSet<fon_rec::rec>::DescriptorSet(
    FramedEngine &engine, DescriptorPool &pool,
    std::span<std::pair<Descriptable *, unsigned>> bindings)
    : FONode<DescriptorPool::SetHandle, fon_type::swap, fon_rec::rec>(
          engine,
          [&](FrameID id) {
            return engine.createObject<DescriptorPool::SetHandle>(
                pool.createSet());
          },
          FOUses(pool).addUses(
              bindings | std::views::transform([](auto &&p) -> decltype(auto) {
                return dynamic_cast<FONodeBase &>(*p.first);
              }))) {
  std::ranges::copy(bindings, std::back_inserter(m_bindings));
  std::ranges::for_each(engine.frameIds(),
                        [this](FrameID frame) { writeDescriptors(frame); });
}

DescriptorSet<fon_rec::expir>::DescriptorSet(
    FramedEngine &engine, DescriptorPool &pool,
    std::span<std::pair<Descriptable *, unsigned>> bindings)
    : FONode<DescriptorPool::SetHandle, fon_type::swap, fon_rec::expir>(
          engine,
          [&](FrameID id) {
            return engine.createObject<DescriptorPool::SetHandle>(
                pool.createSet());
          },
          FOUses(pool).addUses(
              bindings | std::views::transform([](auto &&p) -> decltype(auto) {
                return dynamic_cast<FONodeBase &>(*p.first);
              }))) {
  std::ranges::for_each(engine.frameIds(), [this, &bindings](FrameID frame) {
    auto &set = get(frame);
    for (auto &&[child, binding] : bindings) {
      child->descriptorWrite(frame, *set, binding);
    }
  });
}

void DescriptorSet<fon_rec::rec>::onUseAction(const Frame &frame,
                                              FObject &obj) {
  auto &id = frame.id();
  if (m_pendingWrites.test(id)) {
    writeDescriptors(id);
    m_pendingWrites.set(id, false);
  }
}
FObject::Ptr DescriptorSet<fon_rec::rec>::constructNew(FramedEngine &engine,
                                                       FrameID id) {
  m_pendingWrites.set(id, true);

  return nullptr;
}

} // namespace imvk