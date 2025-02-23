#include "imvk/base/ContextImpl.hpp"

#include <numeric>

#undef max

namespace imvk {

ContextImpl::ContextImpl(const ContextCreateInfo &CI)
    : m_device(CI.device), m_shaderFactory(CI.shaderFactory) {
  // pre-initialize queue map
  for (auto &&index : m_device.physicalDevice().queueFamilies() |
                          std::views::transform(
                              [](auto &&family) { return family.index(); })) {
    m_queueMap.emplace(std::piecewise_construct, std::make_tuple(index),
                       std::make_tuple());
  }
}

Queue &ContextImpl::allocateQueue(const QueueCapsInfo &queueInfo) {
  auto &physDevice = m_device.physicalDevice();

  // collect information about viable candidates that were not chosen due to
  // family queue overcroud.
  std::vector<const vkw::QueueFamily *> viableCandidates;

  auto exactMatch = [&](auto &&family) {
    return queueInfo.graphics == family.graphics() &&
           queueInfo.compute == family.compute() &&
           queueInfo.transfer == family.transfer();
  };
  auto viableMatch = [&](auto &&family) {
    return (!queueInfo.graphics || family.graphics()) &&
           (!queueInfo.compute == family.compute()) &&
           (!queueInfo.transfer == family.transfer());
  };

  auto tryAllocateNew = [&](auto &&queueFamily) -> Queue * {
    unsigned queueCount = queueFamily.queueRequestedCount();
    if (!queueCount)
      return nullptr;
    auto &queueMap = m_queueMap.at(queueFamily.index());
    auto indexRange = std::ranges::iota_view{0u, queueCount};
    auto findNotYetAllocated = std::ranges::find_if(
        indexRange, [&](auto &&i) { return !queueMap.contains(i); });
    if (findNotYetAllocated == indexRange.end()) {
      viableCandidates.emplace_back(&queueFamily);
      return nullptr;
    }
    unsigned queueId = *findNotYetAllocated;
    return &m_allocateQueue(queueFamily.index(), queueId);
  };

  // First, search through exact match queues.
  for (auto &&queueFamily :
       physDevice.queueFamilies() | std::views::filter(exactMatch)) {
    if (auto *pQueue = tryAllocateNew(queueFamily))
      return *pQueue;
  }

  // Next, try viable matches.
  for (auto &&queueFamily :
       physDevice.queueFamilies() | std::views::filter(viableMatch)) {
    if (auto *pQueue = tryAllocateNew(queueFamily))
      return *pQueue;
  }

  // Finally, pick least crowded queue in viable candidates.

  if (viableCandidates.empty())
    throw std::runtime_error(
        "Failed to allocate a queue - no viable candidates found.");

  std::pair<std::pair<unsigned, unsigned>, unsigned> leastCrowded{
      std::make_pair(0u, 0u), std::numeric_limits<unsigned>::max()};

  leastCrowded = std::accumulate(
      viableCandidates.begin(), viableCandidates.end(), leastCrowded,
      [&](auto acc, auto *pFamily) {
        std::pair<unsigned, unsigned> lcInFamily =
            std::make_pair(0u, std::numeric_limits<unsigned>::max());
        unsigned queueCount = pFamily->queueRequestedCount();
        auto &queueMap = m_queueMap.at(pFamily->index());
        auto indexRange = std::ranges::iota_view{0u, queueCount};
        lcInFamily =
            std::accumulate(indexRange.begin(), indexRange.end(), lcInFamily,
                            [&](auto acc2, auto &&i) {
                              auto queueRefCount = queueMap.at(i).second;
                              return queueRefCount < acc2.second
                                         ? std::make_pair(i, queueRefCount)
                                         : acc2;
                            });
        return lcInFamily.second < acc.second
                   ? std::make_pair(
                         std::make_pair(static_cast<unsigned>(pFamily->index()),
                                        lcInFamily.first),
                         lcInFamily.second)
                   : acc;
      });
  return m_allocateQueue(leastCrowded.first.first, leastCrowded.first.second);
}

void ContextImpl::freeQueue(Queue &queue) {
  auto handedQueue = queue.acquire();
  auto &queueRef = handedQueue.get();
  auto queueIndex = queueRef.index();
  auto familyIndex = queueRef.family().index();
  auto &queueMap = m_queueMap.at(familyIndex);

  auto &[pQueue, refCount] = queueMap.at(queueIndex);
  assert(pQueue == &queue);
  if (refCount == 0) {
    queueMap.erase(queueIndex);
    m_queueStorage.erase(pQueue);
    return;
  }
  --refCount;
}

Queue &ContextImpl::m_allocateQueue(unsigned queueFamilyIndex,
                                    unsigned queueIndex) {
  auto &queueMap = m_queueMap.at(queueFamilyIndex);
  if (queueMap.contains(queueIndex)) {
    auto &[pQueue, refCount] = queueMap.at(queueIndex);
    if (refCount == 1u)
      pQueue->introduceLock();
    ++refCount;
    return *pQueue;
  }
  auto pQueue = new Queue{m_device.getQueue(queueFamilyIndex, queueIndex),
                          /* sync */ false};
  m_queueStorage.emplace(pQueue, pQueue);
  queueMap.emplace(std::piecewise_construct, std::make_tuple(queueIndex),
                   std::make_tuple(pQueue, 1u));
  return *pQueue;
}

} // namespace imvk