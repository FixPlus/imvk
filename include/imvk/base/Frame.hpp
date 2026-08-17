#pragma once
#include "imvk/base/Utils.hpp"

#include "vkw/CommandBuffer.hpp"
#include "vkw/CommandPool.hpp"
#include "vkw/CommandRecorder.hpp"

#include <boost/compat/function_ref.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/intrusive_ptr.hpp>

#include <algorithm>
#include <ranges>
#include <vector>

namespace imvk {

class FramedEngine;
using FrameID = unsigned;

/// @brief Controls capture of commands to be submitted for frame render and
/// manages lifetimes of resources used in those commands.
class Frame final {
public:
  /// @brief Constructs frame #id for specified engine.
  /// @param engine
  /// @param id
  Frame(FramedEngine &engine, FrameID id) : m_engine(&engine), m_id(id) {}

  /// @return the engine this frame is registered in.
  FramedEngine &engine() const { return *m_engine; }

  /// @return id of this frame. Is unique for each frame.
  const FrameID &id() const { return m_id; }

  /// @return id of this frame. Is unique for each frame.
  const auto &ordinal() const { return m_ordinal; }
  /// @return id of this frame. Is unique for each frame.
  auto &ordinal() { return m_ordinal; }

private:
  FramedEngine *m_engine;
  const FrameID m_id;
  FrameID m_ordinal = 0;
};

template <typename T> class FObjectImpl;

/// @brief Base class for any frame objects.
class FObject {
public:
  /// @brief Mark object as used in next frame. This value must not be
  /// decremented.
  void useInFrame(size_t frameId) const {
    m_lastFrame.store(frameId, std::memory_order::release);
  }

  size_t lastFrame() const {
    return m_lastFrame.load(std::memory_order::acquire);
  }

  FObject(FObject &&) = delete;
  FObject(const FObject &) = delete;
  FObject &operator=(FObject &&) = delete;
  FObject &operator=(const FObject &) = delete;

  virtual ~FObject() = default;

  template <typename T> T &as() { return static_cast<FObjectImpl<T> &>(*this); }

  template <typename T> const T &as() const {
    return static_cast<const FObjectImpl<T> &>(*this);
  }
  class Deleter {
  public:
    Deleter() = default;
    Deleter(FramedEngine &engine) : m_engine(&engine) {}
    void operator()(FObject *obj) const;

  private:
    FramedEngine *m_engine = nullptr;
  };
  using Ptr = std::unique_ptr<FObject, Deleter>;

protected:
  FObject() = default;

private:
  mutable std::atomic<size_t> m_lastFrame = 0;
};

template <typename T> class FObjectImpl final : public FObject {
public:
  template <typename... Ts>
  FObjectImpl(Ts &&...args) : m_obj(std::forward<Ts>(args)...) {}

  operator T &() { return m_obj; }

  operator const T &() const { return m_obj; }

private:
  T m_obj;
};

template <> class FObjectImpl<void> final : public FObject {
public:
  FObjectImpl() = default;
};

class FONodeBase;
void intrusive_ptr_add_ref(FONodeBase *p);
void intrusive_ptr_release(FONodeBase *p);
using FONodeRef = boost::intrusive_ptr<FONodeBase>;

enum class fon_type { cow, swap, ext };

template <fon_type type> class FONodeImpl {};

class FOUses {
public:
  FOUses() = default;
  template <typename... Args> FOUses(Args &...args) {
    (m_uses.push_back(&args), ...);
  }
  template <std::ranges::range R> FOUses(const R &rng) {
    std::ranges::transform(rng, std::back_inserter(m_uses),
                           [](auto &&use) { return &use; });
  }

  std::span<FONodeBase *const> get() { return m_uses; }
  FOUses &addUse(FONodeBase &use) & {
    m_uses.push_back(&use);
    return *this;
  }
  FOUses &&addUse(FONodeBase &use) && {
    m_uses.push_back(&use);
    return std::move(*this);
  }
  template <std::ranges::range R> FOUses &addUses(const R &uses) & {
    std::ranges::transform(uses, std::back_inserter(m_uses),
                           [](auto &&use) { return &use; });
    return *this;
  }
  template <std::ranges::range R> FOUses &&addUses(const R &uses) && {
    std::ranges::transform(uses, std::back_inserter(m_uses),
                           [](auto &&use) { return &use; });
    return *this;
  }
  FOUses operator|(const FOUses &another) const {
    auto ret = FOUses{*this};
    std::ranges::copy(another.m_uses, std::back_inserter(ret.m_uses));
    return ret;
  }

private:
  boost::container::small_vector<FONodeBase *, 2> m_uses;
};

struct FOUse {
  FOUse(FONodeBase *r) : ref(r){};
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

  virtual ~FONodeBase() {
    assert(m_firstUser.next == nullptr);
    for (auto &&use : m_uses) {
      auto *prev = use.prev;
      auto *next = use.next;
      if (prev)
        prev->next = next;
      if (next)
        next->prev = prev;
    }
  }

  auto users() const {
    return std::ranges::subrange(FOUseIterator{m_firstUser.next},
                                 FOUseIterator{nullptr});
  }

  auto uses() const {
    return m_uses | std::views::transform(
                        [](auto &&use) -> decltype(auto) { return *use.ref; });
  }

  template <typename T> T &getUse(size_t index) const {
    return static_cast<T &>(*m_uses.at(index).ref);
  }

  void users_topological_traverse(bool reverse, auto &&userFilter,
                                  auto &&visitAction) {
    boost::container::small_flat_set<FONodeBase *, 20> visited;
    boost::container::small_vector<FONodeBase *, 20> res;
    boost::container::small_vector<std::pair<FONodeBase *, bool>, 20> stack;
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
        if (!userFilter(*next, p) || visited.contains(&p))
          continue;
        stack.emplace_back(&p, false);
      }
    }
    if (!reverse)
      std::reverse(res.begin(), res.end());
    for (auto *node : res)
      visitAction(*node);
  }

  /// @brief this method marks this object and all it's subobjects as used in
  /// this frame.
  void use(const Frame &frame) {
    if (isUsed(frame))
      return;
    markUsed(frame);
    onUse(frame);
    for (auto &&use : m_uses)
      use.ref->use(frame);
  }

  virtual void onCowUseReplace(FramedEngine &engine,
                               FONodeImpl<fon_type::cow> &cowp) noexcept = 0;

  // reconstructs object and its users. destroyed objects are not placed in
  // free queue and are destroyed in-place.
  void reconstruct(FramedEngine &engine) {
    destruct(engine);
    construct(engine);
  }

protected:
  virtual void onUse(const Frame &frame) = 0;
  virtual bool isUsed(const Frame &frame) const = 0;
  virtual void markUsed(const Frame &frame) = 0;

  virtual void onDestruct(FramedEngine &engine) = 0;
  virtual void onConstruct(FramedEngine &engine) = 0;

private:
  void destruct(FramedEngine &engine) {
    users_topological_traverse(
        /* reverse */ true, [](auto &u, auto &v) { return true; },
        [&](auto &&node) { node.onDestruct(engine); });
  }

  void construct(FramedEngine &engine) {
    users_topological_traverse(
        /* reverse */ false, [](auto &u, auto &v) { return true; },
        [&](auto &&node) { node.onConstruct(engine); });
  }
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

template <> class FONodeImpl<fon_type::swap> : public FONodeBase {
public:
  FONodeImpl(FramedEngine &engine, auto &&objectFactory,
             FOUses &&uses = FOUses{})
      : FONodeBase(std::move(uses)), m_objects([&]() {
          boost::container::small_vector<FObject::Ptr, 2> ret;
          m_objectsInit(engine, objectFactory, ret);
          return ret;
        }()),
        m_cowFlags(m_objects.size()) {}

  FObject &use(const Frame &frame) {
    FONodeBase::use(frame);
    return *m_objects[frame.id()];
  }
  const FObject &get(FrameID frameId) const { return *m_objects[frameId]; }

protected:
  /// @brief an action that should handle object restructure in case some cow
  /// use was replaced.
  virtual void onCowExpire(const Frame &frame) = 0;

  /// @brief an action to do if object is used in frame. The purpose of those
  /// actions are to prepare inner state of object at the beginning of gpu frame
  /// or/and read current state of object after last frame was processed.
  /// @param frame context to do action for.
  /// @param obj reference to current object to prepare.
  virtual void onUseAction(const Frame &frame, FObject &obj) = 0;

  virtual FObject::Ptr constructNew(FramedEngine &engine, FrameID frame) = 0;

  void onCowUseReplace(FramedEngine &engine,
                       FONodeImpl<fon_type::cow> &cowp) noexcept final {
    setCowExpired();
  }

  void onUse(const Frame &frame) final {
    checkCows(frame);
    onUseAction(frame, *getFor(frame));
  }

  bool isUsed(const Frame &frame) const final {
    return getFor(frame)->lastFrame() == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    getFor(frame)->useInFrame(frame.ordinal());
  }
  void onDestruct(FramedEngine &engine) final {
    resetCowExpired();
    for (auto &obj : m_objects) {
      delete obj.release();
    }
  }
  void onConstruct(FramedEngine &engine) final;
  /// FIXME: not very safe.
  FObject *getFor(const Frame &frame) const {
    return m_objects[frame.id()].get();
  }

private:
  using ObjGen = boost::compat::function_ref<FObject::Ptr(FrameID)>;
  void m_objectsInit(FramedEngine &engine, ObjGen gen,
                     boost::container::small_vector_base<FObject::Ptr> &out);
  void checkCows(const Frame &frame) {
    if (!isCowExpired(frame))
      return;
    haveExpiredCows = false;
    onCowExpire(frame);
  }

  bool isCowExpired(const Frame &frame) const { return m_cowFlags[frame.id()]; }
  void resetCowExpired(const Frame &frame) { m_cowFlags[frame.id()] = false; }
  void resetCowExpired() {
    for (auto &&flag : m_cowFlags)
      flag = true;
    haveExpiredCows = false;
  }
  void setCowExpired() {
    for (auto &&flag : m_cowFlags)
      flag = true;
  }
  // swap's objects references are immutable. Their inner state is mutable
  // though.
  boost::container::small_vector<FObject::Ptr, 2> m_objects;
  boost::container::small_vector<bool, 2> m_cowFlags;
  bool haveExpiredCows = false;
};

template <> class FONodeImpl<fon_type::cow> : public FONodeBase {
public:
  template <size_t n>
  using UserVec = boost::container::small_vector<FONodeBase *, n>;

  using UserVecBase = boost::container::small_vector_base<FONodeBase *>;
  FONodeImpl(FObject::Ptr obj, FOUses &&uses = FOUses{})
      : FONodeBase(std::move(uses)), m_current(std::move(obj)) {}

  void replace(FramedEngine &engine, FObject::Ptr obj) noexcept {
    // FIXME: this is not exception safe at all. need to rethink.

    m_current = std::move(obj);

    users_topological_traverse(
        /* reverse */ false,
        [](auto &&u, auto &v) {
          return !!dynamic_cast<FONodeImpl<fon_type::cow> *>(&u);
        },
        [&](auto &&node) {
          if (&node == this)
            return;
          node.onCowUseReplace(engine, *this);
        });
  }

  const FObject &use(const Frame &frame) {
    FONodeBase::use(frame);
    return *m_current;
  }
  const FObject &get() const { return *m_current; }

protected:
  /// @brief constructs new object using current uses as inputs. May be
  /// unimplemented(return null) for some objects but in this case those objects
  /// cannot have uses.
  /// FIXME: this is noexcept due to replace() being noexcept for now. See upper
  /// fixme.
  /// @note may return null if object does not contain any uses. UB if null
  /// may be returned with uses.
  virtual FObject::Ptr constructNew(FramedEngine &engine) noexcept = 0;

  void onCowUseReplace(FramedEngine &engine,
                       FONodeImpl<fon_type::cow> &cowp) noexcept final {
    m_current = constructNew(engine);
  }

  void onUse(const Frame &frame) final {
    // do nothing. cow objects are immutable and do not require any per-frame
    // work.
  }

  bool isUsed(const Frame &frame) const final {
    return m_current->lastFrame() == frame.id();
  }

  void markUsed(const Frame &frame) final {
    m_current->useInFrame(frame.ordinal());
  }
  void onDestruct(FramedEngine &engine) final { delete m_current.release(); }
  void onConstruct(FramedEngine &engine) final {
    m_current = constructNew(engine);
  }

private:
  // cow's object references are mutable. The inner state of object is
  // immutable.
  FObject::Ptr m_current;
};

template <> class FONodeImpl<fon_type::ext> : public FONodeBase {
public:
  FONodeImpl(FOUses &&uses = FOUses{}) : FONodeBase(std::move(uses)) {}
  FONodeImpl() = default;

  FObject &use(const Frame &frame) {
    FONodeBase::use(frame);
    return *m_objects[getExtIndex(frame)];
  }
  const FObject &get(FrameID frameId) const { return *m_objects[frameId]; }

protected:
  virtual unsigned getExtIndex(const Frame &frame) const = 0;

  virtual void
  constructNew(FramedEngine &engine,
               boost::container::small_vector_base<FObject::Ptr> &res) = 0;

  virtual void onUseAction(const Frame &frame, FObject &obj) = 0;

  void onUse(const Frame &frame) final { onUseAction(frame, *getFor(frame)); }

  bool isUsed(const Frame &frame) const final {
    return getFor(frame)->lastFrame() == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    getFor(frame)->useInFrame(frame.ordinal());
  }

  void onDestruct(FramedEngine &engine) final {
    for (auto &&obj : m_objects)
      delete obj.release();
    m_objects.clear();
  }
  void onConstruct(FramedEngine &engine) final {
    constructNew(engine, m_objects);
  }
  void onCowUseReplace(FramedEngine &engine,
                       FONodeImpl<fon_type::cow> &cowp) noexcept final {
    reconstruct(engine);
  }
  /// FIXME: not very safe.
  FObject *getFor(const Frame &frame) const {
    return m_objects[getExtIndex(frame)].get();
  }

private:
  // ext's objects references and inner state are mutable.
  boost::container::small_vector<FObject::Ptr, 2> m_objects;
};

template <typename T, fon_type type> class FONode {};

template <typename T>
class FONode<T, fon_type::swap> : public FONodeImpl<fon_type::swap> {
public:
  FONode(auto &&...args)
      : FONodeImpl<fon_type::swap>(std::forward<decltype(args)>(args)...){};

  T &use(const Frame &frame) {
    return FONodeImpl<fon_type::swap>::use(frame).as<T>();
  }
  const T &get(FrameID frame) const {
    return FONodeImpl<fon_type::swap>::get(frame).as<T>();
  }
};

template <typename T>
class FONode<T, fon_type::cow> : public FONodeImpl<fon_type::cow> {
public:
  FONode(auto &&...args)
      : FONodeImpl<fon_type::cow>(std::forward<decltype(args)>(args)...){};

  const T &use(const Frame &frame) {
    return FONodeImpl<fon_type::cow>::use(frame).as<T>();
  }
  const T &get() const { return FONodeImpl<fon_type::cow>::get().as<T>(); }
};

template <typename T>
class FONode<T, fon_type::ext> : public FONodeImpl<fon_type::ext> {
public:
  FONode(auto &&...args)
      : FONodeImpl<fon_type::ext>(std::forward<decltype(args)>(args)...){};

  T &use(const Frame &frame) {
    return FONodeImpl<fon_type::ext>::use(frame).as<T>();
  }
  const T &get(FrameID frame) const {
    return FONodeImpl<fon_type::ext>::get(frame).as<T>();
  }
};

template <typename T> using Ref = boost::intrusive_ptr<T>;

} // namespace imvk