#pragma once
#include "imvk/base/DescriptorSet.hpp"
#include "imvk/copy/Engine.hpp"
#include "imvk/graphics/Engine.hpp"

#include <vkw/StagingBuffer.hpp>
#include <vkw/UniformBuffer.hpp>
#include <vkw/VertexBuffer.hpp>

namespace imvk::examples {

template <typename T, imvk::fon_type type, typename BufHandle = vkw::Buffer<T>>
class BufferImpl {};

template <typename T, typename Buf>
class BufferImpl<T, imvk::fon_type::cow, Buf>
    : public imvk::FONode<Buf, imvk::fon_type::cow,
                          BufferImpl<T, imvk::fon_type::cow, Buf>> {
public:
  imvk::FObject::Ptr constructNew(imvk::FramedEngine &) { return nullptr; }
  struct CopyWorkload : public imvk::CopyEngine::Workload {
    CopyWorkload(vkw::StagingBuffer<T> &&src, Buf &dst)
        : src(std::move(src)), dst(dst){};

    void record(vkw::TransferPassRecorder &commands) const override {
      VkBufferCopy region{0, 0, src.size() * sizeof(T)};
      commands.copyBufferToBuffer(src, dst, {&region, 1u});
    }
    vkw::StagingBuffer<T> src;
    Buf &dst;
  };

  template <typename U>
    requires std::convertible_to<std::ranges::range_value_t<U>, T>
  static imvk::FObject::Ptr create(imvk::FramedEngine &engine,
                                   imvk::CopyEngine &copyEngine, U &&data) {
    auto ret = engine.createObject<Buf>(
        engine.context().getDeviceAllocator(), data.size(),
        VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                .usage = VMA_MEMORY_USAGE_GPU_ONLY,
                                .requiredFlags =
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT},
        VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    vkw::StagingBuffer<T> staging{engine.context().getDeviceAllocator(), data};
    Buf &bufRef = ret->as<Buf>();
    auto copyFuture = copyEngine.copy(
        std::make_unique<CopyWorkload>(std::move(staging), bufRef));
    copyFuture.wait();
    return ret;
  }

  template <std::convertible_to<T> U>
  static imvk::FObject::Ptr create(imvk::FramedEngine &engine,
                                   imvk::CopyEngine &copyEngine, U data) {
    auto ret = engine.createObject<Buf>(
        engine.context().getDeviceAllocator(),
        VmaAllocationCreateInfo{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                .usage = VMA_MEMORY_USAGE_GPU_ONLY,
                                .requiredFlags =
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT},
        VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    vkw::StagingBuffer<T> staging{engine.context().getDeviceAllocator(), data};
    Buf &bufRef = ret->as<Buf>();
    auto copyFuture = copyEngine.copy(
        std::make_unique<CopyWorkload>(std::move(staging), bufRef));
    copyFuture.wait();
    return ret;
  }

  template <typename U>
  BufferImpl(imvk::FramedEngine &engine, imvk::CopyEngine &copyEngine, U &&data)
      : imvk::FONode<Buf, imvk::fon_type::cow,
                     BufferImpl<T, imvk::fon_type::cow, Buf>>(
            create(engine, copyEngine, std::forward<U>(data))) {}
};

template <typename T, typename Buf>
class BufferImpl<T, imvk::fon_type::swap_mut, Buf>
    : public imvk::FONode<Buf, imvk::fon_type::swap_mut,
                          BufferImpl<T, imvk::fon_type::swap_mut, Buf>> {
public:
  using Base = imvk::FONode<Buf, imvk::fon_type::swap_mut,
                            BufferImpl<T, imvk::fon_type::swap_mut, Buf>>;
  BufferImpl(imvk::FramedEngine &engine, size_t size, auto &&action)
      : Base(engine,
             [&](imvk::FrameID id) {
               return engine.createObject<Buf>(
                   engine.context().getDeviceAllocator(), size,
                   VmaAllocationCreateInfo{
                       .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                       .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
                       .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT});
             }),
        m_action(std::forward<decltype(action)>(action)) {}

  BufferImpl(imvk::FramedEngine &engine, auto &&action)
      : Base(engine,
             [&](imvk::FrameID id) {
               return engine.createObject<Buf>(
                   engine.context().getDeviceAllocator(),
                   VmaAllocationCreateInfo{
                       .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
                       .usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
                       .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT});
             }),
        m_action(std::forward<decltype(action)>(action)) {}

  void onUseAction(const imvk::Frame &frame, Buf &obj) {
    std::invoke(m_action, frame, obj);
  }
  std::function<void(const imvk::Frame &, Buf &)> m_action;
};

template <typename T, imvk::fon_type type>
class UniformBuffer
    : public imvk::FONodeView<BufferImpl<T, type, vkw::UniformBuffer<T>>> {
public:
  UniformBuffer(auto &&...args)
      : imvk::FONodeView<BufferImpl<T, type, vkw::UniformBuffer<T>>>(
            std::forward<decltype(args)>(args)...) {}
  static void descriptorWrite(imvk::FrameID frame, vkw::DescriptorSet &set,
                              imvk::FONodeBase &obj, unsigned binding) {
    auto &casted =
        static_cast<BufferImpl<T, type, vkw::UniformBuffer<T>> &>(obj);
    if constexpr (type == imvk::fon_type::swap_mut) {
      set.write(binding, casted.get(frame));
    } else {
      set.write(binding, casted.get());
    }
  }
};

template <typename T, imvk::fon_type PType>
class VertexBuffer
    : public imvk::FONodeView<BufferImpl<T, PType, vkw::VertexBuffer<T>>> {
public:
  VertexBuffer(auto &&...args)
      : imvk::FONodeView<BufferImpl<T, PType, vkw::VertexBuffer<T>>>(
            std::forward<decltype(args)>(args)...) {}
};

} // namespace imvk::examples