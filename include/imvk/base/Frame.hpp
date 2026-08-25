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
#include <type_traits>
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
  Frame(Frame &&) = default;
  Frame &operator=(Frame &&) = default;

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
  FrameID m_id;
  FrameID m_ordinal = 0;
};

template <typename T> class FObjectImpl;

/// @brief Base class for any frame objects.
class FObject {
public:
  /// @brief Mark object as used in next frame. This value must not be
  /// decremented.
  void useInFrame(size_t frameId) const { m_lastFrame = frameId; }

  size_t lastFrame() const { return m_lastFrame; }

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
  mutable size_t m_lastFrame = 0;
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

enum class fon_type { cow, swap, ext, mut, swap_mut };
template <fon_type type> class FONodeBaseImpl {};

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

  std::span<FONodeRef const> get() { return m_uses; }
  FOUses &addUse(FONodeBase &use) & {
    m_uses.push_back(&use);
    return *this;
  }
  FOUses &&addUse(FONodeBase &use) && {
    m_uses.push_back(&use);
    return std::move(*this);
  }
  FOUses &addUse(FONodeRef use) & {
    m_uses.push_back(std::move(use));
    return *this;
  }
  FOUses &&addUse(FONodeRef use) && {
    m_uses.push_back(std::move(use));
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

  template <typename T> T &getUse(size_t index) const {
    return static_cast<T &>(*m_uses.at(index).ref);
  }

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

  void construct(FramedEngine &engine) {
    uses_topological_traverse(
        /* reverse */ true,
        [](auto &u, auto &v) {
          auto *rec = dynamic_cast<FOReconstructible *>(&v);
          return rec && rec->isDestroyed();
        },
        [&](auto &&node) {
          auto &rec = static_cast<FOReconstructible &>(node);
          rec.onConstruct(engine);
          rec.destroyed = false;
        });
  }
  bool isDestroyed() const { return destroyed; }

protected:
  friend class FONodeBaseImpl<fon_type::cow>;

  virtual void onConstruct(FramedEngine &engine) = 0;
  virtual void onDestruct(bool immediate) = 0;

  void earlyUse(const Frame &frame) override {
    if (destroyed)
      construct(frame.engine());
  }

private:
  bool destroyed = true;
};

template <> class FONodeBaseImpl<fon_type::swap> : public FOReconstructible {
public:
  FONodeBaseImpl(FOUses &&uses = FOUses{})
      : FOReconstructible(true, std::move(uses)) {}

  FObject &use(const Frame &frame) {
    FONodeBase::use(frame);
    return *m_objects[frame.id()];
  }
  const FObject &get(FrameID frameId) const {
    assert(!isDestroyed());
    return *m_objects[frameId];
  }

protected:
  /// @brief an action to do if object is used in frame. The purpose of those
  /// actions are to prepare inner state of object at the beginning of gpu frame
  /// or/and read current state of object after last frame was processed.
  /// @param frame context to do action for.
  /// @param obj reference to current object to prepare.
  virtual void onUseAction(const Frame &frame, FObject &obj) = 0;
  virtual FObject::Ptr constructNew(FramedEngine &engine, FrameID frame) = 0;
  virtual bool keepAlive() = 0;

  void onUse(const Frame &frame) final { onUseAction(frame, *getFor(frame)); }

  bool isUsed(const Frame &frame) const final {
    return getFor(frame)->lastFrame() == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    getFor(frame)->useInFrame(frame.ordinal());
  }

  /// FIXME: not very safe.
  FObject *getFor(const Frame &frame) const {
    return m_objects[frame.id()].get();
  }

private:
  void onDestruct(bool immediate) final {
    if (keepAlive())
      return;
    for (auto &obj : m_objects) {
      if (immediate)
        delete obj.release();
      else
        obj.reset();
    }
  }
  void onConstruct(FramedEngine &engine) final;
  // swap's objects references are immutable. Their inner state is mutable
  // though.
  boost::container::small_vector<FObject::Ptr, 2> m_objects;
};

template <> class FONodeBaseImpl<fon_type::swap_mut> : public FONodeBase {
public:
  FONodeBaseImpl(FramedEngine &engine, auto &&objectFactory,
                 FOUses &&uses = FOUses{})
      : FONodeBase(std::move(uses)), m_objects([&]() {
          boost::container::small_vector<FObject::Ptr, 2> ret;
          m_objectsInit(engine, objectFactory, ret);
          return ret;
        }()) {}

  FObject &use(const Frame &frame) {
    FONodeBase::use(frame);
    return *m_objects[frame.id()];
  }
  const FObject &get(FrameID frameId) const { return *m_objects[frameId]; }

protected:
  /// @brief an action to do if object is used in frame. The purpose of those
  /// actions are to prepare inner state of object at the beginning of gpu frame
  /// or/and read current state of object after last frame was processed.
  /// @param frame context to do action for.
  /// @param obj reference to current object to prepare.
  virtual void onUseAction(const Frame &frame, FObject &obj) = 0;

  void onUse(const Frame &frame) final { onUseAction(frame, *getFor(frame)); }

  bool isUsed(const Frame &frame) const final {
    return getFor(frame)->lastFrame() == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    getFor(frame)->useInFrame(frame.ordinal());
  }

  /// FIXME: not very safe.
  FObject *getFor(const Frame &frame) const {
    return m_objects[frame.id()].get();
  }

private:
  using ObjGen = boost::compat::function_ref<FObject::Ptr(FrameID)>;
  void m_objectsInit(FramedEngine &engine, ObjGen gen,
                     boost::container::small_vector_base<FObject::Ptr> &out);
  // swap's objects references are immutable. Their inner state is mutable
  // though.
  boost::container::small_vector<FObject::Ptr, 2> m_objects;
};

template <> class FONodeBaseImpl<fon_type::cow> : public FOReconstructible {
public:
  template <size_t n>
  using UserVec = boost::container::small_vector<FONodeBase *, n>;

  using UserVecBase = boost::container::small_vector_base<FONodeBase *>;
  FONodeBaseImpl(FObject::Ptr obj, FOUses &&uses = FOUses{})
      : FOReconstructible(false, std::move(uses)), m_current(std::move(obj)) {}

  void replace(FramedEngine &engine, FObject::Ptr obj) noexcept {
    for (auto &user : users())
      static_cast<FOReconstructible &>(user).destroy();
    m_current = std::move(obj);
  }

  const FObject &use(const Frame &frame) {
    FONodeBase::use(frame);
    return *m_current;
  }
  const FObject &get() const {
    assert(!isDestroyed());
    return *m_current;
  }

protected:
  /// @brief constructs new object using current uses as inputs. May be
  /// unimplemented(return null) for some objects but in this case those objects
  /// cannot have uses.
  /// FIXME: this is noexcept due to replace() being noexcept for now. See upper
  /// fixme.
  /// @note may return null if object does not contain any uses. UB if null
  /// may be returned with uses.
  virtual FObject::Ptr constructNew(FramedEngine &engine) noexcept = 0;
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

private:
  void onDestruct(bool immediate) final {
    if (immediate)
      delete m_current.release();
    else
      m_current.reset();
  }
  void onConstruct(FramedEngine &engine) final {
    m_current = constructNew(engine);
  }
  // cow's object references are mutable. The inner state of object is
  // immutable.
  FObject::Ptr m_current;
};

template <> class FONodeBaseImpl<fon_type::ext> : public FOReconstructible {
public:
  FONodeBaseImpl(FOUses &&uses = FOUses{})
      : FOReconstructible(true, std::move(uses)) {}
  FONodeBaseImpl() = default;

  FObject &use(const Frame &frame) {
    FONodeBase::use(frame);
    return *m_objects[getExtIndex(frame)];
  }
  const FObject &get(FrameID frameId) const { return *m_objects[frameId]; }

protected:
  virtual unsigned getExtIndex(const Frame &frame) const = 0;

  virtual void onUseAction(const Frame &frame, FObject &obj) = 0;
  virtual void
  constructNew(FramedEngine &engine,
               boost::container::small_vector_base<FObject::Ptr> &res) = 0;
  void onUse(const Frame &frame) final { onUseAction(frame, *getFor(frame)); }

  bool isUsed(const Frame &frame) const final {
    return getFor(frame)->lastFrame() == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    getFor(frame)->useInFrame(frame.ordinal());
  }

  /// FIXME: not very safe.
  FObject *getFor(const Frame &frame) const {
    return m_objects[getExtIndex(frame)].get();
  }

private:
  void onDestruct(bool immediate) final {
    if (!immediate)
      m_objects.clear();
    for (auto &&obj : m_objects)
      delete obj.release();
    m_objects.clear();
  }
  void onConstruct(FramedEngine &engine) final {
    constructNew(engine, m_objects);
  }
  // ext's objects references and inner state are mutable.
  boost::container::small_vector<FObject::Ptr, 2> m_objects;
};

template <> class FONodeBaseImpl<fon_type::mut> : public FONodeBase {
public:
  FONodeBaseImpl(FObject::Ptr obj, FOUses &&uses = FOUses{})
      : m_current(std::move(obj)), FONodeBase(std::move(uses)) {}

  FObject &use(const Frame &frame) {
    FONodeBase::use(frame);
    return *m_current;
  }
  FObject &get() const { return *m_current; }

protected:
  bool isUsed(const Frame &frame) const final {
    return m_current->lastFrame() == frame.id();
  }

  void markUsed(const Frame &frame) final {
    m_current->useInFrame(frame.ordinal());
  }

private:
  // cow's object references are mutable. The inner state of object is
  // immutable.
  FObject::Ptr m_current;
};

template <typename T, fon_type type> class FONode {};

template <typename T>
class FONode<T, fon_type::swap> : public FONodeBaseImpl<fon_type::swap> {
public:
  FONode(auto &&...args)
      : FONodeBaseImpl<fon_type::swap>(std::forward<decltype(args)>(args)...){};

  T &use(const Frame &frame) {
    return FONodeBaseImpl<fon_type::swap>::use(frame).as<T>();
  }
  const T &get(FrameID frame) const {
    return FONodeBaseImpl<fon_type::swap>::get(frame).as<T>();
  }
};

template <typename T>
class FONode<T, fon_type::swap_mut>
    : public FONodeBaseImpl<fon_type::swap_mut> {
public:
  FONode(auto &&...args)
      : FONodeBaseImpl<fon_type::swap_mut>(
            std::forward<decltype(args)>(args)...){};

  T &use(const Frame &frame) {
    return FONodeBaseImpl<fon_type::swap_mut>::use(frame).as<T>();
  }
  const T &get(FrameID frame) const {
    return FONodeBaseImpl<fon_type::swap_mut>::get(frame).as<T>();
  }
};

template <typename T>
class FONode<T, fon_type::cow> : public FONodeBaseImpl<fon_type::cow> {
public:
  FONode(auto &&...args)
      : FONodeBaseImpl<fon_type::cow>(std::forward<decltype(args)>(args)...){};

  const T &use(const Frame &frame) {
    return FONodeBaseImpl<fon_type::cow>::use(frame).as<T>();
  }
  const T &get() const { return FONodeBaseImpl<fon_type::cow>::get().as<T>(); }
};

template <typename T>
class FONode<T, fon_type::ext> : public FONodeBaseImpl<fon_type::ext> {
public:
  FONode(auto &&...args)
      : FONodeBaseImpl<fon_type::ext>(std::forward<decltype(args)>(args)...){};

  T &use(const Frame &frame) {
    return FONodeBaseImpl<fon_type::ext>::use(frame).as<T>();
  }
  const T &get(FrameID frame) const {
    return FONodeBaseImpl<fon_type::ext>::get(frame).as<T>();
  }
};

template <typename T>
class FONode<T, fon_type::mut> : public FONodeBaseImpl<fon_type::mut> {
public:
  FONode(auto &&...args)
      : FONodeBaseImpl<fon_type::mut>(std::forward<decltype(args)>(args)...){};

  T &use(const Frame &frame) {
    return FONodeBaseImpl<fon_type::mut>::use(frame).as<T>();
  }
  T &get() const { return FONodeBaseImpl<fon_type::mut>::get().as<T>(); }
};

template <>
class FONode<void, fon_type::swap> : public FONodeBaseImpl<fon_type::swap> {
public:
  FONode(auto &&...args)
      : FONodeBaseImpl<fon_type::swap>(std::forward<decltype(args)>(args)...){};
};

template <>
class FONode<void, fon_type::swap_mut>
    : public FONodeBaseImpl<fon_type::swap_mut> {
public:
  FONode(auto &&...args)
      : FONodeBaseImpl<fon_type::swap_mut>(
            std::forward<decltype(args)>(args)...){};
};

template <>
class FONode<void, fon_type::cow> : public FONodeBaseImpl<fon_type::cow> {
public:
  FONode(auto &&...args)
      : FONodeBaseImpl<fon_type::cow>(std::forward<decltype(args)>(args)...){};
};

template <>
class FONode<void, fon_type::ext> : public FONodeBaseImpl<fon_type::ext> {
public:
  FONode(auto &&...args)
      : FONodeBaseImpl<fon_type::ext>(std::forward<decltype(args)>(args)...){};
};

template <>
class FONode<void, fon_type::mut> : public FONodeBaseImpl<fon_type::mut> {
public:
  FONode(auto &&...args)
      : FONodeBaseImpl<fon_type::mut>(std::forward<decltype(args)>(args)...){};
};

template <typename T> using Ref = boost::intrusive_ptr<T>;

} // namespace imvk