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

enum class CachePolicy { LRU };

template <typename Key, std::movable T, CachePolicy Policy = CachePolicy::LRU,
          std::default_initializable KeyHash = std::hash<Key>>
class Cache {};

template <typename Key, std::movable T, std::default_initializable KeyHash>
class Cache<Key, T, CachePolicy::LRU, KeyHash> final {
public:
  Cache(size_t size) : m_size(size) {}

  template <std::convertible_to<Key> KeyLike>
  T &get(KeyLike &&key, auto &&...args) {
    if (!m_elementTable.contains(key)) {
      auto &ret = m_elementList.emplace_front(
          std::piecewise_construct, std::forward_as_tuple(key),
          std::forward_as_tuple(std::forward<decltype(args)>(args)...));
      m_elementTable.emplace(key, m_elementList.begin());
      if (m_elementTable.size() > m_size)
        m_remove_last();
      return ret.second;
    }
    auto elemIt = m_elementTable.at(key);
    m_move_forward(elemIt);
    return elemIt->second;
  }

  template <std::convertible_to<Key> KeyLike>
  T &replace(KeyLike &&key, auto &&...args) {
    assert(m_elementTable.contains(key));
    auto elemIt = m_elementTable.at(key);
    T Tmp{std::forward<decltype(args)>(args)...};
    std::swap(elemIt->second, Tmp);
    return elemIt->second;
  }

  void clear() {
    m_elementTable.clear();
    m_elementList.clear();
  }

  auto size() const { return m_elementTable.size(); }

private:
  using ListType = std::list<std::pair<Key, T>>;
  void m_move_forward(ListType::iterator it) {
    m_elementList.splice(m_elementList.begin(), m_elementList, it);
  }
  void m_remove_last() {
    auto last = std::prev(m_elementList.end());
    m_elementTable.erase(last->first);
    m_elementList.erase(last);
  }
  const size_t m_size;

  std::list<std::pair<Key, T>> m_elementList;
  std::unordered_map<Key, typename ListType::iterator, KeyHash> m_elementTable;
};

} // namespace imvk