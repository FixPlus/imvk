#if 0
#include "imvk/graph/Graph.hpp"

#undef min
#undef max

namespace imvk::graph {

static VkImageSubresourceRange mergeRanges(const VkImageSubresourceRange &a,
                                           const VkImageSubresourceRange &b) {
  VkImageSubresourceRange ret{};
  ret.aspectMask = a.aspectMask | b.aspectMask;
  ret.baseArrayLayer = std::min(a.baseArrayLayer, b.baseArrayLayer);
  ret.baseMipLevel = std::min(a.baseMipLevel, b.baseMipLevel);

  auto endLayer = std::max(a.baseArrayLayer + a.layerCount,
                           b.baseArrayLayer + b.layerCount);
  auto endLevel =
      std::max(a.baseMipLevel + a.levelCount, b.baseMipLevel + b.levelCount);
  ret.layerCount = endLayer - ret.baseArrayLayer;
  ret.levelCount = endLevel - ret.baseMipLevel;
  return ret;
}

static VkImageSubresourceRange getFullSubresource(VkImageAspectFlags aspect) {
  VkImageSubresourceRange ret{};
  ret.baseArrayLayer = 0;
  ret.baseMipLevel = 0;
  ret.layerCount = VK_REMAINING_ARRAY_LAYERS;
  ret.levelCount = VK_REMAINING_MIP_LEVELS;
  ret.aspectMask = aspect;
  return ret;
}

static bool rangesIntersect(const VkImageSubresourceRange &a,
                            const VkImageSubresourceRange &b) {
  /// FIXME: currently changing aspect masks is not allowed.
  assert(a.aspectMask == b.aspectMask);
  // either different array layers.
  if (a.baseArrayLayer + a.layerCount <= b.baseArrayLayer ||
      b.baseArrayLayer + b.layerCount <= a.baseArrayLayer)
    return false;
  // or different mip levels within same array layers.
  if (a.baseMipLevel + a.levelCount <= b.baseMipLevel ||
      b.baseMipLevel + b.levelCount <= a.baseMipLevel)
    return false;
  return true;
}

static VkImageSubresourceRange
getRangeIntersection(const VkImageSubresourceRange &a,
                     const VkImageSubresourceRange &b) {
  /// FIXME: currently changing aspect masks is not allowed.
  assert(a.aspectMask == b.aspectMask);
  VkImageSubresourceRange ret{};

  ret.aspectMask = a.aspectMask;
  ret.baseArrayLayer = std::max(a.baseArrayLayer, b.baseArrayLayer);
  ret.baseMipLevel = std::max(a.baseMipLevel, b.baseMipLevel);
  auto layerEnd = std::min(a.baseArrayLayer + a.layerCount,
                           b.baseArrayLayer + b.layerCount);
  auto levelEnd =
      std::min(a.baseMipLevel + a.levelCount, b.baseMipLevel + b.levelCount);
  ret.layerCount = layerEnd - ret.baseArrayLayer;
  ret.levelCount = levelEnd - ret.baseMipLevel;
  return ret;
}

static bool isReadAccess(VkAccessFlags2 flags) {
  static const auto ReadMask =
      VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
      VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
      VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
      VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT |
      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
      VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_HOST_READ_BIT |
      VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
      VK_ACCESS_2_SHADER_STORAGE_READ_BIT;

  return (flags & ReadMask) && !(flags & ~ReadMask);
}

static bool isWriteAccess(VkAccessFlags2 flags) {
  static const auto WriteMask =
      VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
      VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT |
      VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
  return (flags & WriteMask) && !(flags & ~WriteMask);
}

ImageResourceTraits::UseInfo
ImageResourceTraits::mergeUseInfo(const UseInfo &a, const UseInfo &b) {
  auto ret = a;
  ret.useFlags |= b.useFlags;
  return ret;
}

bool ImageResourceTraits::needsExplicitBarrier(const AccessInfo &firstScope,
                                               const AccessInfo &secondScope) {
  /// TODO: rewrite this
#if 0
  /// FIXME: currently changing aspect masks is not allowed.
  assert(firstScope.subresourceRange.aspectMask ==
         secondScope.subresourceRange.aspectMask);


  // layout transitions always require a barrier.
  if (firstScope.layout != secondScope.layout)
    return true;
  // if subresource ranges are not intersecting, there is no need in barriers.
  if (!rangesIntersect(firstScope.subresourceRange,
                       secondScope.subresourceRange))
    return false;

  // read after read is allowed to be done without barriers.
  if (isReadAccess(firstScope.accessFlags) &&
      isReadAccess(secondScope.accessFlags))
    return false;

  // all other operations require a barrier.
#endif
  return true;
}

ImageResourceTraits::AccessInfo
ImageResourceTraits::advanceScope(const AccessInfo &firstScope,
                                  const AccessInfo &secondScope) {
/// TODO: rewrite this
#if 0
  assert(firstScope.layout == secondScope.layout);
  auto ret = firstScope;
  ret.stageFlags |= secondScope.stageFlags;
  ret.subresourceRange =
      mergeRanges(firstScope.subresourceRange, secondScope.subresourceRange);
  ret.accessFlags |= secondScope.accessFlags;
  
  return ret;
#endif
  return secondScope;
}

std::vector<ImageResourceTraits::BarrierType>
ImageResourceTraits::getBarriers(const AccessInfo &firstScope,
                                 const AccessInfo &secondScope) {
  std::vector<BarrierType> ret{};
/// TODO: rewrite this
#if 0 
  /// FIXME: currently changing aspect masks is not allowed.
  assert(firstScope.subresourceRange.aspectMask ==
         secondScope.subresourceRange.aspectMask);
  ImageResourceTraits::BarrierType ret{};
  ret.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  ret.pNext = nullptr;
  ret.srcAccessMask = firstScope.accessFlags;
  ret.dstAccessMask = secondScope.accessFlags;
  ret.oldLayout = firstScope.layout;
  ret.newLayout = secondScope.layout;
  ret.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  ret.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

  /// FIXME: on layout transitions we transit full image, because
  /// we don't track layout for every subresource.
  if (firstScope.layout != secondScope.layout) {
    ret.subresourceRange =
        getFullSubresource(firstScope.subresourceRange.aspectMask);
  } else {
    ret.subresourceRange = getRangeIntersection(firstScope.subresourceRange,
                                                secondScope.subresourceRange);
  }
#endif
  return ret;
}

uint32_t &ImageResourceTraits::getSizeField(VkDependencyInfo &depInfo) {
  return depInfo.imageMemoryBarrierCount;
}

const ImageResourceTraits::BarrierType *&
ImageResourceTraits::getDataField(VkDependencyInfo &depInfo) {
  return depInfo.pImageMemoryBarriers;
}

void ImageResourceTraits::amendBarrierWithHandle(BarrierType &barrier,
                                                 ResourceHandle handle) {
  barrier.image = handle;
}

BufferResourceTraits::UseInfo
BufferResourceTraits::mergeUseInfo(const UseInfo &a, const UseInfo &b) {
  auto ret = a;
  ret.useFlags | b.useFlags;
  return ret;
}

bool BufferResourceTraits::needsExplicitBarrier(const AccessInfo &firstScope,
                                                const AccessInfo &secondScope) {
  /// TODO: rewrite this
#if 0
  // If accesses are made to different regions - no need for barrier.
  if (firstScope.size + firstScope.offset <= secondScope.offset ||
      secondScope.size + secondScope.offset <= firstScope.offset)
    return false;
  // If any of scopes writes in buffer - barrier is needed.
  return !isReadAccess(firstScope.accessFlags) ||
         !isReadAccess(secondScope.accessFlags);
#endif
  return true;
}

BufferResourceTraits::AccessInfo
BufferResourceTraits::advanceScope(const AccessInfo &firstScope,
                                   const AccessInfo &secondScope) {
  /// TODO: rewrite this
#if 0
  auto ret = firstScope;
  ret.accessFlags |= secondScope.accessFlags;
  ret.stageFlags |= secondScope.stageFlags;
  auto newOffset = std::min(ret.offset, secondScope.offset);
  auto newEnd =
      std::max(ret.offset + ret.size, secondScope.offset + secondScope.size);
  ret.offset = newOffset;
  ret.size = newEnd - newOffset;
  return ret;
#endif
  return secondScope;
}

std::vector<BufferResourceTraits::BarrierType>
BufferResourceTraits::getBarriers(const AccessInfo &firstScope,
                                  const AccessInfo &secondScope) {
  std::vector<BarrierType> ret{};
  /// TODO: rewrite this
#if 0
  ret.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
  ret.pNext = nullptr;
  ret.srcStageMask = firstScope.stageFlags;
  ret.srcAccessMask = firstScope.accessFlags;
  ret.dstStageMask = secondScope.stageFlags;
  ret.dstAccessMask = secondScope.accessFlags;
  ret.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  ret.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  auto commonOffset = std::max(firstScope.offset, secondScope.offset);
  auto commonEnd = std::min(firstScope.offset + firstScope.size,
                            secondScope.offset + secondScope.size);
  assert(commonEnd > commonOffset);
  ret.offset = commonOffset;
  ret.size = commonEnd - commonOffset;
#endif
  return ret;
}

uint32_t &BufferResourceTraits::getSizeField(VkDependencyInfo &depInfo) {
  return depInfo.bufferMemoryBarrierCount;
}
const BufferResourceTraits::BarrierType *&
BufferResourceTraits::getDataField(VkDependencyInfo &depInfo) {
  return depInfo.pBufferMemoryBarriers;
}
void BufferResourceTraits::amendBarrierWithHandle(BarrierType &barrier,
                                                  ResourceHandle handle) {
  barrier.buffer = handle;
}

} // namespace imvk::graph
#endif