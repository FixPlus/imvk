#pragma once

#include <algorithm>
#include <boost/container/small_vector.hpp>
#include <boost/intrusive/list.hpp>
#include <cassert>
#include <functional>
#include <memory>
#include <ranges>

namespace imvk::graph {

class Type;
class Value;
class Use;
class Node;
class Context;
class MaterializationContext;
class Workflow;

template <typename T, typename U> bool isa(const U *u) {
  return !!dynamic_cast<const T *>(u);
}

template <typename T, typename U> T *dyn_cast(U *u) {
  return dynamic_cast<T *>(u);
}

template <typename T, typename U> const T *dyn_cast(const U *u) {
  return dynamic_cast<const T *>(u);
}

class AttributesBase {
public:
  AttributesBase() = default;
  AttributesBase(AttributesBase &&) = default;
  AttributesBase &operator=(AttributesBase &&) = default;

  virtual void dump(std::ostream &os) const = 0;
  virtual std::size_t hash() const = 0;
  virtual bool operator==(const AttributesBase &another) const = 0;
  virtual ~AttributesBase() = default;
};

inline std::ostream &operator<<(std::ostream &os, const AttributesBase &attr) {
  attr.dump(os);
  return os;
}

class Type {
public:
  Type() = default;
  Type(Type &&) = default;
  Type &operator=(Type &&) = default;
  Type(const Type &) = delete;
  Type &operator=(const Type &) = delete;

  virtual void dump(std::ostream &os) const = 0;
  virtual const AttributesBase *getUndefined(Context &ctx) const = 0;
  virtual std::size_t hash() const = 0;
  virtual bool operator==(const Type &another) const = 0;
  virtual ~Type() = default;
};

inline std::ostream &operator<<(std::ostream &os, const Type &t) {
  t.dump(os);
  return os;
}

class UseInfo {
public:
  virtual UseInfo *clone() const = 0;
  virtual ~UseInfo() = default;
};

class Use final {
public:
  Use() = default;
  Use(Node *user, UseInfo *info) : m_user(user), m_info(info){};

  void replaceBy(Value *val);

  Value &value() const { return *m_value; }

  Node &user() const { return *m_user; }

  const UseInfo *info() const { return m_info.get(); }

private:
  friend class Value;
  friend class UserIterator;
  Use *next() const { return m_next; }
  Use *prev() const { return m_prev; }
  Value *m_value = nullptr;
  Node *m_user = nullptr;
  Use *m_prev = nullptr;
  Use *m_next = nullptr;
  std::unique_ptr<UseInfo> m_info = nullptr;
};

class UserIterator final {
public:
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = Use;
  using difference_type = std::ptrdiff_t;
  using pointer = Use *;
  using reference = Use &;

  UserIterator(Use *ptr = nullptr) : current(ptr) {}

  // Dereference operators
  reference operator*() const { return *current; }
  pointer operator->() const { return current; }

  // Prefix increment
  UserIterator &operator++() {
    if (current)
      current = current->next();
    return *this;
  }

  // Postfix increment
  UserIterator operator++(int) {
    UserIterator tmp = *this;
    ++(*this);
    return tmp;
  }

  // Prefix increment
  UserIterator &operator--() {
    if (current)
      current = current->prev();
    return *this;
  }

  // Postfix increment
  UserIterator operator--(int) {
    UserIterator tmp = *this;
    --(*this);
    return tmp;
  }

  friend bool operator==(const UserIterator &a,
                         const UserIterator &b) = default;

private:
  Use *current;
};

class DefInfo {
public:
  virtual DefInfo *clone() const = 0;
  virtual ~DefInfo() = default;
};

class Value final {
public:
  Value(Context &ctx, Node *node = nullptr, const Type *type = nullptr,
        DefInfo *info = nullptr);
  Value(Value &&) = default;
  Value &operator=(Value &&) = default;
  Value(const Value &) = delete;
  Value &operator=(const Value &) = delete;

  void addUse(Use &use) {
    use.m_value = this;
    auto *oldNext = m_firstUse.m_next;
    m_firstUse.m_next = &use;
    use.m_prev = &m_firstUse;
    use.m_next = oldNext;
    if (oldNext)
      oldNext->m_prev = &use;
  }

  bool isNull() const { return m_node == nullptr; }
  Node &node() const {
    assert(!isNull());
    return *m_node;
  }
  const Type &type() const { return *m_type; }
  const DefInfo &info() const { return *m_info; }
  size_t index() const { return m_index; }
  size_t resultNum() const;
  auto users() const {
    return std::ranges::subrange(UserIterator{m_firstUse.m_next},
                                 UserIterator{});
  }

  void replaceAllUsesWith(Value *another) {
    assert(m_type = another->m_type);
    if (!m_firstUse.m_next)
      return;
    auto *use = m_firstUse.m_next;
    m_firstUse.m_next = nullptr;
    while (use) {
      auto *nextUse = use->m_next;
      use->replaceBy(another);
      use = nextUse;
    }
  }

private:
  Use m_firstUse;
  Node *m_node;
  const Type *m_type;
  size_t m_index;
  std::unique_ptr<DefInfo> m_info = nullptr;
};

std::ostream &operator<<(std::ostream &os, const Value &v);

inline void Use::replaceBy(Value *val) {
  if (m_prev)
    m_prev->m_next = m_next;
  if (m_next)
    m_next->m_prev = m_prev;
  val->addUse(*this);
}

class Node : public boost::intrusive::list_base_hook<> {
public:
  using Use = std::pair<Value *, UseInfo *>;
  using Def = std::pair<const Type *, DefInfo *>;
  static constexpr auto EmptyUses = std::span<Use>{(Use *)nullptr, 0};
  static constexpr auto EmptyValues = std::span<Value *>{(Value **)nullptr, 0};
  static constexpr auto EmptyResults = std::span<Def>{(Def *)nullptr, 0};
  Node(Context &ctx, auto &&values, auto &&types) {
    m_uses.resize(std::ranges::size(values));
    std::ranges::transform(values, std::begin(m_uses), [this](auto &&p) {
      return graph::Use{this, p.second};
    });
    for (auto &&[use, value] : std::views::zip(m_uses, values)) {
      value.first->addUse(use);
    }

    std::ranges::transform(types, std::back_inserter(m_results),
                           [this, &ctx](auto &&type) {
                             return Value{ctx, this, type.first, type.second};
                           });
  }

  std::span<const graph::Use> uses() const { return m_uses; }
  std::span<graph::Use> uses() { return m_uses; }
  std::span<Value> results() { return m_results; }
  std::span<const Value> results() const { return m_results; }

  /// @brief creates a clone of this node. the clone references same values as
  /// original.
  /// @return handle to allocated clone node.
  std::unique_ptr<Node> clone(Context &ctx) const {
    auto ret = doClone();
    ret->m_uses.resize(m_uses.size());
    std::ranges::transform(m_uses, ret->m_uses.begin(), [&](auto &&use) {
      return graph::Use(ret.get(), use.info() ? use.info()->clone() : nullptr);
    });
    for (auto &&[use, value] : std::views::zip(ret->m_uses, m_uses)) {
      value.value().addUse(use);
    }

    std::ranges::transform(
        m_results, std::back_inserter(ret->m_results), [&](auto &&result) {
          return Value{ctx, ret.get(), &result.type(),
                       &result.info() ? result.info().clone() : nullptr};
        });
    return ret;
  }
  virtual std::string_view name() const = 0;
  virtual void dumpAttributes(std::ostream &os) const = 0;
  virtual bool hasVisibleSideEffects() const = 0;
  virtual const AttributesBase *
  getAttributes(Context &ctx, const Value &result,
                std::span<const AttributesBase *> useAttributes) const = 0;
  virtual bool materialize(MaterializationContext &ctx) = 0;
  virtual ~Node() = default;

protected:
  Node() = default;
  /// @brief create a clone of node with same dynamic type. it is not required
  /// to create uses and results for this clone as it handled in clone() method.
  /// @return handle to allocated clone.
  virtual std::unique_ptr<Node> doClone() const = 0;

private:
  boost::container::small_vector<graph::Use, 2> m_uses;
  boost::container::small_vector<Value, 2> m_results;
};

inline size_t Value::resultNum() const {
  return std::distance(const_cast<const Value *>(node().results().data()),
                       this);
}

} // namespace imvk::graph

template <> struct std::hash<imvk::graph::Type *> {
  std::size_t operator()(imvk::graph::Type *s) const noexcept {
    return s->hash();
  }
};

template <> struct std::equal_to<imvk::graph::Type *> {
  bool operator()(imvk::graph::Type *a, imvk::graph::Type *b) const noexcept {
    return *a == *b;
  }
};