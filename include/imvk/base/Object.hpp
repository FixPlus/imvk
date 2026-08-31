#pragma once
#include "imvk/base/EngineBase.hpp"

#include <boost/compat/function_ref.hpp>
#include <boost/container/flat_set.hpp>

#include <algorithm>
#include <ranges>
#include <type_traits>
#include <vector>

namespace imvk {

class FONodeBase;
void intrusive_ptr_add_ref(FONodeBase *p);
void intrusive_ptr_release(FONodeBase *p);
using FONodeRef = Ref<FONodeBase>;

enum class fon_type { cow, swap, ext, mut, swap_mut };
template <typename T, fon_type type, typename Derived> class FONode {};

class FOUses {
public:
  FOUses() = default;
  template <typename... Args> FOUses(Args &&...args) {
    (m_uses.push_back(std::forward<decltype(args)>(args)), ...);
  }
  template <std::ranges::range R> FOUses(const R &rng) {
    std::ranges::transform(rng, std::back_inserter(m_uses),
                           [](auto &&use) { return use; });
  }

  std::span<FONodeRef const> get() { return m_uses; }

  FOUses &addUse(auto &&use) & {
    m_uses.push_back(std::forward<decltype(use)>(use));
    return *this;
  }
  FOUses &&addUse(auto &&use) && {
    m_uses.push_back(std::forward<decltype(use)>(use));
    return std::move(*this);
  }
  template <std::ranges::range R> FOUses &addUses(const R &uses) & {
    std::ranges::transform(uses, std::back_inserter(m_uses),
                           [](auto &&use) { return use; });
    return *this;
  }
  template <std::ranges::range R> FOUses &&addUses(const R &uses) && {
    std::ranges::transform(uses, std::back_inserter(m_uses),
                           [](auto &&use) { return use; });
    return std::move(*this);
  }
  FOUses operator|(const FOUses &another) const {
    auto ret = FOUses{*this};
    std::ranges::copy(another.m_uses, std::back_inserter(ret.m_uses));
    return ret;
  }

private:
  boost::container::small_vector<FONodeRef, 2> m_uses;
};

struct FOUse {
  FOUse(FONodeBase *r) : ref(r){};
  FOUse(FONodeRef r) : ref(std::move(r)){};
  FONodeRef ref;
  FONodeBase *user = nullptr;
  FOUse *next = nullptr;
  FOUse *prev = nullptr;
};

class FOUseIterator {
public:
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = FONodeBase;
  using difference_type = std::ptrdiff_t;
  using pointer = FONodeBase *;
  using reference = FONodeBase &;

  FOUseIterator(FOUse *ptr = nullptr) : current(ptr) {}

  // Dereference operators
  reference operator*() const { return *current->user; }
  pointer operator->() const { return current->user; }

  // Prefix increment
  FOUseIterator &operator++() {
    if (current)
      current = current->next;
    return *this;
  }

  // Postfix increment
  FOUseIterator operator++(int) {
    FOUseIterator tmp = *this;
    ++(*this);
    return tmp;
  }

  // Prefix increment
  FOUseIterator &operator--() {
    if (current)
      current = current->prev;
    return *this;
  }

  // Postfix increment
  FOUseIterator operator--(int) {
    FOUseIterator tmp = *this;
    --(*this);
    return tmp;
  }

  friend bool operator==(const FOUseIterator &a,
                         const FOUseIterator &b) = default;

private:
  FOUse *current;
};

class FONodeBase {
public:
  FONodeBase(FOUses &&uses = FOUses{})
      : m_uses([&]() {
          boost::container::small_vector<FOUse, 2> ret;
          std::ranges::copy(uses.get(), std::back_inserter(ret));
          return ret;
        }()) {
    for (auto &&use : m_uses) {
      use.ref->addUser(&use);
      use.user = this;
    }
  }
  // This object is intrusively reference counter. Therefore no copy/moves.
  FONodeBase(FONodeBase &&) = delete;
  FONodeBase(const FONodeBase &) = delete;
  FONodeBase &operator=(FONodeBase &&) = delete;
  FONodeBase &operator=(const FONodeBase &) = delete;

  virtual ~FONodeBase() { unlink(); }

  auto users() const {
    return std::ranges::subrange(FOUseIterator{m_firstUser.next},
                                 FOUseIterator{nullptr});
  }

  auto uses() const {
    return m_uses | std::views::transform(
                        [](auto &&use) -> decltype(auto) { return *use.ref; });
  }

  void replaceUseBy(size_t useIndex, FONodeRef another) {
    auto &use = m_uses.at(useIndex);
    auto tmpPrev = use.prev;
    if (use.prev)
      use.prev->next = use.next;
    if (use.next)
      use.next->prev = tmpPrev;
    use.ref = another;
    use.ref->addUser(&use);
    onUseReplace(useIndex);
  }

  template <typename T> T getUse(size_t index) const {
    return T{static_cast<typename T::BaseNode *>(&*m_uses.at(index).ref)};
  }
  FONodeBase &getUseRaw(size_t index) const { return *m_uses.at(index).ref; }
  template <std::derived_from<FONodeBase> T = FONodeBase>
  void users_topological_traverse(bool reverse, auto &&userFilter,
                                  auto &&visitAction) {
    boost::container::small_flat_set<FONodeBase *, 20> visited;
    boost::container::small_vector<FONodeBase *, 20> res;
    boost::container::small_vector<std::pair<FONodeBase *, bool>, 20> stack;
    auto cast = [](auto &node) -> T & { return static_cast<T &>(node); };
    stack.emplace_back(this, false);
    while (!stack.empty()) {
      auto &&[next, processed_nei] = stack.back();
      if (visited.contains(next) && !processed_nei) {
        stack.pop_back();
        continue;
      }
      if (processed_nei) {
        res.push_back(next);
        stack.pop_back();
        continue;
      }
      processed_nei = true;
      visited.insert(next);
      for (auto &p : next->users()) {
        if (!userFilter(cast(*next), cast(p)) || visited.contains(&p))
          continue;
        stack.emplace_back(&p, false);
      }
    }
    if (!reverse)
      std::reverse(res.begin(), res.end());
    for (auto *node : res)
      visitAction(cast(*node));
  }

  template <std::derived_from<FONodeBase> T = FONodeBase>
  void uses_topological_traverse(bool reverse, auto &&useFilter,
                                 auto &&visitAction) {
    boost::container::small_flat_set<FONodeBase *, 20> visited;
    boost::container::small_vector<FONodeBase *, 20> res;
    boost::container::small_vector<std::pair<FONodeBase *, bool>, 20> stack;
    auto cast = [](auto &node) -> T & { return static_cast<T &>(node); };
    stack.emplace_back(this, false);
    while (!stack.empty()) {
      auto &&[next, processed_nei] = stack.back();
      if (visited.contains(next) && !processed_nei) {
        stack.pop_back();
        continue;
      }
      if (processed_nei) {
        res.push_back(next);
        stack.pop_back();
        continue;
      }
      processed_nei = true;
      visited.insert(next);
      for (auto &p : next->uses()) {
        if (!useFilter(cast(*next), cast(p)) || visited.contains(&p))
          continue;
        stack.emplace_back(&p, false);
      }
    }
    if (!reverse)
      std::reverse(res.begin(), res.end());
    for (auto *node : res)
      visitAction(cast(*node));
  }

  /// @brief this method marks this object and all it's subobjects as used in
  /// this frame.
  void use(const Frame &frame) {
    earlyUse(frame);
    if (isUsed(frame))
      return;
    markUsed(frame);
    onUse(frame);
    for (auto &&use : m_uses)
      use.ref->use(frame);
  }

protected:
  virtual void earlyUse(const Frame &frame) {}
  virtual void onUse(const Frame &frame) = 0;
  virtual bool isUsed(const Frame &frame) const = 0;
  virtual void markUsed(const Frame &frame) = 0;
  virtual void onUseReplace(size_t index) {}
  void unlink() {
    assert(m_firstUser.next == nullptr);
    for (auto &&use : m_uses) {
      auto *prev = use.prev;
      auto *next = use.next;
      if (prev)
        prev->next = next;
      if (next)
        next->prev = prev;
    }
    m_uses.clear();
  }

private:
  void addUser(FOUse *user) {
    auto *next = m_firstUser.next;
    user->next = next;
    user->prev = &m_firstUser;
    if (next)
      next->prev = user;
    m_firstUser.next = user;
  }
  friend void intrusive_ptr_add_ref(FONodeBase *p);
  friend void intrusive_ptr_release(FONodeBase *p);

  boost::container::small_vector<FOUse, 2> m_uses;
  FOUse m_firstUser = nullptr;
  // ref count is not synchronized, passing handles to other threads is not
  // allowed.
  size_t m_refCount = 0;
};

class FOReconstructible : public FONodeBase {
public:
  FOReconstructible(bool initDestroyed, FOUses &&uses = FOUses{})
      : FONodeBase(std::move(uses)), destroyed(initDestroyed) {}

  void destroy(bool immediate = false) noexcept {
    users_topological_traverse<FOReconstructible>(
        /* reverse */ true, [](auto &u, auto &v) { return !v.isDestroyed(); },
        [&](auto &&node) {
          node.onDestruct(immediate);
          node.destroyed = true;
        });
  }

  void construct() {
    uses_topological_traverse(
        /* reverse */ true,
        [](auto &u, auto &v) {
          auto *rec = dynamic_cast<FOReconstructible *>(&v);
          return rec && rec->isDestroyed();
        },
        [](auto &&node) {
          auto &rec = static_cast<FOReconstructible &>(node);
          rec.onConstruct();
          rec.destroyed = false;
        });
  }
  bool isDestroyed() const { return destroyed; }

protected:
  virtual void onConstruct() = 0;
  virtual void onDestruct(bool immediate) = 0;

  void earlyUse(const Frame &frame) override {
    if (destroyed)
      construct();
  }
  void onUseReplace(size_t index) override {
    if (!destroyed)
      destroy();
  }

private:
  bool destroyed = true;
};

template <typename T, typename Derived>
class FONode<T, fon_type::swap, Derived> : public FOReconstructible {
public:
  constexpr static fon_type FonType = fon_type::swap;
  using ObjType = T;

  FONode(FramedEngine &parent, FOUses &&uses = FOUses{})
      : FOReconstructible(true, std::move(uses)), m_deleter(parent) {}

  T &use(const Frame &frame) {
    FONodeBase::use(frame);
    return m_objects[frame.id()]->first;
  }
  const T &get(FrameID frameId) const {
    assert(!isDestroyed());
    return m_objects[frameId]->first;
  }

  ~FONode() override {
    for (auto &obj : m_objects)
      m_deleter.destroyObject(*std::move(obj));
  }

protected:
  /// @brief an action to do if object is used in frame. The purpose of those
  /// actions are to prepare inner state of object at the beginning of gpu frame
  /// or/and read current state of object after last frame was processed.
  /// @param frame context to do action for.
  /// @param obj reference to current object to prepare.
  void onUseActionBase(const Frame &frame, T &obj) {
    static_cast<Derived &>(*this).onUseAction(frame, obj);
  }

  T constructNewBase(FrameID frame) {
    return static_cast<Derived &>(*this).constructNew(m_deleter, frame);
  }

  void onUse(const Frame &frame) final {
    onUseActionBase(frame, getFor(frame).first);
  }

  bool isUsed(const Frame &frame) const final {
    return getFor(frame).second == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    getFor(frame).second = frame.ordinal();
  }

  /// FIXME: not very safe.
  FObject<T> &getFor(const Frame &frame) {
    assert(!isDestroyed());
    return *m_objects[frame.id()];
  }
  const FObject<T> &getFor(const Frame &frame) const {
    assert(!isDestroyed());
    return *m_objects[frame.id()];
  }

private:
  void onDestruct(bool immediate) final {
    for (auto &obj : m_objects) {
      if (immediate)
        obj.reset();
      else
        m_deleter.destroyObject(*std::exchange(obj, std::nullopt));
    }
  }
  void onConstruct() final {
    m_objects.resize(m_deleter.getFIFCount());
    for (auto &&[id, obj] : m_objects | std::views::enumerate) {
      obj.emplace(constructNewBase(id), FrameID{0});
    }
  }
  // swap's objects references are immutable. Their inner state is mutable
  // though.
  boost::container::small_vector<std::optional<FObject<T>>, 2> m_objects;
  FramedEngine &m_deleter;
};

template <typename T, typename Derived>
class FONode<T, fon_type::swap_mut, Derived> : public FONodeBase {
public:
  constexpr static fon_type FonType = fon_type::swap_mut;
  using ObjType = T;
  FONode(FramedEngine &engine, auto &&objectFactory, FOUses &&uses = FOUses{})
      : FONodeBase(std::move(uses)), m_deleter(engine) {
    std::ranges::transform(std::ranges::iota_view{0u, engine.getFIFCount()},
                           std::back_inserter(m_objects), [&](auto index) {
                             return std::make_pair(
                                 std::invoke(objectFactory, index), FrameID{0});
                           });
  }

  T &use(const Frame &frame) {
    FONodeBase::use(frame);
    return m_objects[frame.id()]->first;
  }
  const T &get(FrameID frameId) const { return m_objects[frameId]->first; }

  ~FONode() override {
    for (auto &obj : m_objects)
      m_deleter.destroyObject(*std::move(obj));
  }

protected:
  /// @brief an action to do if object is used in frame. The purpose of those
  /// actions are to prepare inner state of object at the beginning of gpu frame
  /// or/and read current state of object after last frame was processed.
  /// @param frame context to do action for.
  /// @param obj reference to current object to prepare.
  void onUseActionBase(const Frame &frame, T &obj) {
    static_cast<Derived &>(*this).onUseAction(frame, obj);
  }

  void onUse(const Frame &frame) final {
    onUseActionBase(frame, getFor(frame).first);
  }

  bool isUsed(const Frame &frame) const final {
    return getFor(frame).second == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    getFor(frame).second = frame.ordinal();
  }

  /// FIXME: not very safe.
  const FObject<T> &getFor(const Frame &frame) const {
    return *m_objects[frame.id()];
  }
  FObject<T> &getFor(const Frame &frame) { return *m_objects[frame.id()]; }

private:
  // swap's objects references are immutable. Their inner state is mutable
  // though.
  boost::container::small_vector<std::optional<FObject<T>>, 2> m_objects;
  FramedEngine &m_deleter;
};

template <typename T, typename Derived>
class FONode<T, fon_type::cow, Derived> : public FOReconstructible {
public:
  constexpr static fon_type FonType = fon_type::cow;
  using ObjType = T;
  FONode(FramedEngine &parent, auto &&obj, FOUses &&uses = FOUses{})
      : FOReconstructible(false, std::move(uses)),
        m_current({std::forward<decltype(obj)>(obj), FrameID{0}}),
        m_deleter(parent) {}
  FONode(FramedEngine &parent, FOUses &&uses = FOUses{})
      : FOReconstructible(true, std::move(uses)), m_current(std::nullopt),
        m_deleter(parent) {}

  /// @brief Replaces current object with newly constructed using args. All
  /// users of this node are destroyed and after that replaced object is moved
  /// to destruction queue.
  /// @param args passed to in-place constructor of object T.
  /// NOTE: only leaf objects may be replaced in this way.
  void replace(auto &&...args) {
    assert(std::ranges::empty(uses()) && "cannot replace non-leaf object");
    for (auto &user : users())
      static_cast<FOReconstructible &>(user).destroy();
    if (isDestroyed()) {
      m_current.emplace(
          std::piecewise_construct,
          std::forward_as_tuple(std::forward<decltype(args)>(args)...),
          std::forward_as_tuple(FrameID{0}));
      return;
    }
    m_deleter.destroyObject(std::exchange(
        *m_current,
        FObject<T>(std::piecewise_construct,
                   std::forward_as_tuple(std::forward<decltype(args)>(args)...),
                   std::forward_as_tuple(FrameID{0}))));
  }

  /// @brief Replaces current object with newly constructed using args. In
  /// contrast to replace() method it does not move object to destruction queue,
  /// but instead wraps it in future object that waits for all frames this
  /// object is used in to retire. Received object handle from specified future
  /// is allowed to be used freely as it is not associated with any engine
  /// state any more. Notice that future wait operation must be externally
  /// synchronized with any other engine operation. Also this future should not
  /// be called before last frame this object was used in is submitted.
  /// @param args passed to in-place constructor of object T.
  /// @return future for T object that was replaced.
  /// NOTE: only leaf objects may be replaced in this way.
  [[nodiscard]] std::future<T> exchange(auto &&...args) {
    assert(std::ranges::empty(uses()) && "cannot replace non-leaf object");
    assert(!isDestroyed());
    for (auto &user : users())
      static_cast<FOReconstructible &>(user).destroy();
    auto oldObj = std::exchange(
        *m_current,
        FObject<T>(std::piecewise_construct,
                   std::forward_as_tuple(std::forward<decltype(args)>(args)...),
                   std::forward_as_tuple(FrameID{0})));
    return std::async(
        std::launch::deferred,
        [oldObj = std::move(oldObj), &m_deleter = m_deleter]() mutable {
          m_deleter.waitTill(oldObj.second);
          return std::move(oldObj.first);
        });
  }

  const T &use(const Frame &frame) {
    FONodeBase::use(frame);
    return m_current->first;
  }
  const T &get() const {
    assert(!isDestroyed());
    return m_current->first;
  }
  ~FONode() override { m_deleter.destroyObject(*std::move(m_current)); }

protected:
  /// @brief constructs new object using current uses as inputs. May be
  /// unimplemented(return null) for some objects but in this case those
  /// objects cannot have uses.
  /// @note may return null if object does not contain any uses. UB if
  /// null may be returned with uses.
  T constructNewBase() {
    return static_cast<Derived &>(*this).constructNew(m_deleter);
  }
  void onUse(const Frame &frame) final {
    // do nothing. cow objects are immutable and do not require any
    // per-frame work.
  }

  bool isUsed(const Frame &frame) const final {
    return m_current->second == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    m_current->second = frame.ordinal();
  }

private:
  void onDestruct(bool immediate) final {
    if (immediate)
      m_current.reset();
    else
      m_deleter.destroyObject(*std::exchange(m_current, std::nullopt));
  }
  void onConstruct() final {
    m_current.emplace(constructNewBase(), FrameID{0});
  }
  // cow's object references are mutable. The inner state of object is
  // immutable.
  std::optional<FObject<T>> m_current;
  FramedEngine &m_deleter;
};

template <typename T, typename Derived>
class FONode<T, fon_type::ext, Derived> : public FOReconstructible {
public:
  constexpr static fon_type FonType = fon_type::ext;
  using ObjType = T;
  FONode(FramedEngine &parent, FOUses &&uses = FOUses{})
      : FOReconstructible(true, std::move(uses)), m_deleter(parent) {}
  FONode(FramedEngine &parent) : FOReconstructible(true), m_deleter(parent) {}

  T &use(const Frame &frame) {
    FONodeBase::use(frame);
    return m_objects[getExtIndexBase(frame)]->first;
  }
  const T &get(FrameID frameId) const {
    assert(!isDestroyed());
    return m_objects[frameId]->first;
  }

  ~FONode() override {
    for (auto &obj : m_objects)
      m_deleter.destroyObject(*std::move(obj));
  }

protected:
  unsigned getExtIndexBase(const Frame &frame) const {
    return static_cast<const Derived &>(*this).getExtIndex(frame);
  }

  void onUseActionBase(const Frame &frame, T &obj) {
    static_cast<Derived &>(*this).onUseAction(frame, obj);
  }

  void constructNewBase(boost::container::small_vector_base<T> &res) {
    static_cast<Derived &>(*this).constructNew(m_deleter, res);
  }
  void onUse(const Frame &frame) final {
    onUseActionBase(frame, getFor(frame).first);
  }

  bool isUsed(const Frame &frame) const final {
    return getFor(frame).second == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    getFor(frame).second = frame.ordinal();
  }

  /// FIXME: not very safe.
  const FObject<T> &getFor(const Frame &frame) const {
    return *m_objects[getExtIndexBase(frame)];
  }
  FObject<T> &getFor(const Frame &frame) {
    return *m_objects[getExtIndexBase(frame)];
  }

private:
  void onDestruct(bool immediate) final {
    if (immediate) {
      m_objects.clear();
      return;
    }
    for (auto &&obj : m_objects)
      m_deleter.destroyObject(*std::exchange(obj, std::nullopt));
    m_objects.clear();
  }
  void onConstruct() final {
    boost::container::small_vector<T, 2> tmp;
    constructNewBase(tmp);
    std::ranges::transform(tmp, std::back_inserter(m_objects), [](auto &obj) {
      return FObject<T>(std::move(obj), FrameID{0});
    });
  }
  // ext's objects references and inner state are mutable.
  boost::container::small_vector<std::optional<FObject<T>>, 2> m_objects;
  FramedEngine &m_deleter;
};

template <typename T, typename Derived>
class FONode<T, fon_type::mut, Derived> : public FONodeBase {
public:
  constexpr static fon_type FonType = fon_type::mut;
  using ObjType = T;
  FONode(FramedEngine &engine, T &&obj, FOUses &&uses = FOUses{})
      : FONodeBase(std::move(uses)), m_current(std::move(obj), FrameID{0}),
        m_deleter(engine) {}

  T &use(const Frame &frame) {
    FONodeBase::use(frame);
    return m_current.first;
  }
  T &get() const { return m_current.first; }
  ~FONode() override { m_deleter.destroyObject(std::move(m_current)); }

protected:
  bool isUsed(const Frame &frame) const final {
    return m_current.second == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    m_current.second = frame.ordinal();
  }

  void onUse(const Frame &frame) final {
    // do nothing.
  }

private:
  // cow's object references are mutable. The inner state of object is
  // immutable.
  mutable FObject<T> m_current;
  FramedEngine &m_deleter;
};

template <typename Derived>
class FONodeView
    : private Ref<
          FONode<typename Derived::ObjType, Derived::FonType, Derived>> {
public:
  using BaseNode = FONode<typename Derived::ObjType, Derived::FonType, Derived>;
  using Base = Ref<BaseNode>;

  FONodeView(std::derived_from<FramedEngine> auto &engine, auto &&...args)
      : Base(engine.template createNode<Derived>(
            std::forward<decltype(args)>(args)...)){};
  FONodeView(BaseNode *ptr = nullptr) : Base(ptr){};
  using Base::operator*;
  using Base::operator->;
  using Base::operator bool;

  operator FONodeRef() const { return &*(*this); }
};

} // namespace imvk