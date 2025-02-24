#pragma once

#include <cassert>
#include <concepts>
#include <optional>
#include <ranges>
#include <vector>

namespace imvk {

/// @brief Index based table that monotonically allocates new index slots
/// starting from index 1. Upon slot deletion, index is stored in free list and
/// may be reused later to allocate slot again. Index zero is reserved and never
/// is allocated a slot and can be used as null indicator.
/// @tparam Mapped  mapped object type.
/// @tparam IT  integral type used for indices.
template <std::integral IT, typename Mapped> class LinearTable final {
public:
  LinearTable(size_t reserveChunkSize) : m_reserveChunkSize(reserveChunkSize) {
    // dummy node.
    m_map.emplace_back();
  };

  /// @brief Constructs new element in-place.
  /// @param args - parameters of Mapped object constructor.
  /// @return pair of slot index and reference to constructed Mapped object
  std::tuple<IT, Mapped &> emplace(auto &&...args) {
    auto index = m_getNextIndex();
    m_freeList.pop_back();
    return {index, m_map[index].emplace(std::forward<decltype(args)>(args)...)};
  }

  /// @brief Checks if slot with specified index is registered. For index zero
  /// always returns false.
  /// @param index to be examined.
  /// @return true if slot is registered.
  bool contains(IT index) const { return index < m_map.size() && m_map[index]; }

  /// @brief Erased slot with specified index. Slot must be registered.
  /// @param index
  void erase(IT index) {
    assert(contains(index));
    m_map[index].reset();
    m_freeList.push_back(index);
  }

  /// @brief Gets reference to object in #index slot. Slot must be registered.
  /// @param index
  /// @return reference to Mapped object
  Mapped &at(IT index) {
    assert(contains(index));
    return *m_map[index];
  }

  /// @brief Gets reference to object in #index slot. Slot must be registered.
  /// @param index
  /// @return const reference to Mapped object
  const Mapped &at(IT index) const {
    assert(contains(index));
    return *m_map[index];
  }

  /// @brief Gets range of all registered slots in order.
  /// @return range of pairs (index, reference to Mapped element)
  auto items() {
    return std::ranges::iota_view{0u, m_map.size()} |
           std::views::filter(
               [this](auto &&i) -> bool { return m_map[i].has_value(); }) |
           std::views::transform([this](auto &&i) -> std::tuple<IT, Mapped &> {
             return {i, *m_map[i]};
           });
  }

  /// @brief Gets range of all registered slots in order.
  /// @return range of pairs (index, constreference to Mapped element)
  auto items() const {
    return std::ranges::iota_view{0u, m_map.size()} |
           std::views::filter(
               [this](auto &&i) -> bool { return m_map[i].has_value(); }) |
           std::views::transform(
               [this](auto &&i) -> std::tuple<IT, const Mapped &> {
                 return {i, *m_map[i]};
               });
  }

  void clear() {
    m_map.clear();
    m_freeList.clear();
  }

private:
  IT m_getNextIndex() {
    if (!m_freeList.empty()) {
      auto index = m_freeList.back();
      m_freeList.pop_back();
      return index;
    }
    return m_growOnce();
  }
  IT m_growOnce() {
    if (m_map.size() == m_map.capacity())
      m_map.reserve(m_map.capacity() + m_reserveChunkSize);

    assert(m_map.size() < m_map.capacity());
    IT ret = m_map.size();
    m_map.resize(m_map.size() + 1u);
    return ret;
  }
  std::vector<std::optional<Mapped>> m_map;
  std::vector<IT> m_freeList;
  const size_t m_reserveChunkSize;
};

} // namespace imvk