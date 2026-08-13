#pragma once
#include "imvk/base/Utils.hpp"

#include "vkw/CommandBuffer.hpp"
#include "vkw/CommandPool.hpp"
#include "vkw/CommandRecorder.hpp"

#include <boost/compat/function_ref.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/intrusive_ptr.hpp>

#include <algorithm>
#include <vector>

namespace imvk {

class FramedEngine;
using FrameID = unsigned;

/// @brief Controls capture of commands to be submitted for frame render and
/// manages lifetimes of resources used in those commands.
class Frame final {
public:
  /// @brief Ends the previous scope of this frame and immediately starts a new
  /// scope. Frame objects that were registered for previous scope as used are
  /// unmarked but may remain registered. Registered objects that were not
  /// marked as used may be disposed here.
  /// @param ordinal - index in total frame order of this submission.
  vkw::BufferRecorder begin(unsigned ordinal);

  /// @brief Frees up all registered objects.
  void terminate() {}

  /// @return the engine this frame is registered in.
  FramedEngine &engine() const { return m_engine; }

  /// @return id of this frame. Is unique for each frame.
  const FrameID &id() const { return m_id; }

  /// @return id of this frame. Is unique for each frame.
  const auto &ordinal() const { return m_ordinal; }

  /// @return Command buffer that is used to capture current frame work.
  vkw::PrimaryCommandBuffer &commands() const { return m_commandBuffer; }
  ~Frame();

private:
  /// @brief Constructs frame #id for specified engine.
  /// @param engine
  /// @param id
  Frame(FramedEngine &engine, FrameID id);
  friend class FrameCreator;

  FramedEngine &m_engine;
  FrameID m_id;
  unsigned m_ordinal;
  mutable vkw::PrimaryCommandBuffer m_commandBuffer;
};

/// @brief Interface factory for Frame creation. Only friends of this class may
/// create frames.
class FrameCreator {
private:
  static Frame *create(FramedEngine &engine, FrameID id) {
    return new Frame(engine, id);
  }
  /// Currently, only FramedEngine is allowed to create Frame objects.
  friend class FramedEngine;
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

class FONodeBase {
public:
  FONodeBase() = default;
  FONodeBase(auto &&children)
      : m_children([&]() {
          boost::container::small_vector<FONodeRef, 2> ret;
          std::ranges::copy(children, std::back_inserter(ret));
          return ret;
        }()) {
    for (auto &&child : m_children) {
      child->addParent(*this);
    }
  }
  // This object is intrusively reference counter. Therefore no copy/moves.
  FONodeBase(FONodeBase &&) = delete;
  FONodeBase(const FONodeBase &) = delete;
  FONodeBase &operator=(FONodeBase &&) = delete;
  FONodeBase &operator=(const FONodeBase &) = delete;

  virtual ~FONodeBase() {
    for (auto &&child : m_children) {
      child->removeParent(*this);
    }
  }

  /// @brief this method marks this object and all it's subobjects as used in
  /// this frame.
  void use(const Frame &frame) {
    if (isUsed(frame))
      return;
    markUsed(frame);
    onUse(frame);
    for (auto &&child : m_children)
      child->use(frame);
  }

  virtual void onCowChildReplace(FramedEngine &engine,
                                 FONodeImpl<fon_type::cow> &cowp) noexcept = 0;

protected:
  virtual bool addParent(FONodeBase &handle) = 0;
  virtual void removeParent(FONodeBase &handle) = 0;
  virtual void onUse(const Frame &frame) = 0;
  virtual bool isUsed(const Frame &frame) const = 0;
  virtual void markUsed(const Frame &frame) = 0;

  const boost::container::small_vector<FONodeRef, 2> m_children;

private:
  friend void intrusive_ptr_add_ref(FONodeBase *p);
  friend void intrusive_ptr_release(FONodeBase *p);

  // ref count is not synchronized, passing handles to other threads is not
  // allowed.
  size_t m_refCount = 0;
};

template <> class FONodeImpl<fon_type::swap> : public FONodeBase {
public:
  FONodeImpl(FramedEngine &engine, auto &&objectFactory, auto &&children)
      : FONodeBase(std::forward<decltype(children)>(children)),
        m_objects([&]() {
          boost::container::small_vector<FObject::Ptr, 2> ret;
          m_objectsInit(engine, objectFactory, ret);
          return ret;
        }()),
        m_cowFlags(m_objects.size()) {}
  FONodeImpl(FramedEngine &engine, auto &&objectFactory)
      : m_objects([&]() {
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
  /// child was replaced.
  virtual void onCowExpire(const Frame &frame) = 0;

  /// @brief an action to do if object is used in frame. The purpose of those
  /// actions are to prepare inner state of object at the beginning of gpu frame
  /// or/and read current state of object after last frame was processed.
  /// @param frame context to do action for.
  /// @param obj reference to current object to prepare.
  virtual void onUseAction(const Frame &frame, FObject &obj) = 0;

  void onCowChildReplace(FramedEngine &engine,
                         FONodeImpl<fon_type::cow> &cowp) noexcept final {
    setCowExpired();
  }

  bool addParent(FONodeBase &handle) final {
    // do nothing. swap objects have no use for parent tracking.
    return false;
  }
  void removeParent(FONodeBase &handle) final {
    // do nothing. swap objects have no use for parent tracking.
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
  void setCowExpired() {
    for (auto &&flag : m_cowFlags)
      flag = true;
  }
  // swap's objects references are immutable. Their inner state is mutable
  // though.
  const boost::container::small_vector<FObject::Ptr, 2> m_objects;
  boost::container::small_vector<bool, 2> m_cowFlags;
  bool haveExpiredCows = false;
};

template <> class FONodeImpl<fon_type::cow> : public FONodeBase {
public:
  template <size_t n>
  using ParentVec = boost::container::small_vector<FONodeBase *, n>;

  using ParentVecBase = boost::container::small_vector_base<FONodeBase *>;
  FONodeImpl(FObject::Ptr obj, auto &&children)
      : FONodeBase(std::forward<decltype(children)>(children)),
        m_current(std::move(obj)) {}

  FONodeImpl(FObject::Ptr obj) : m_current(std::move(obj)) {}
  void replace(FramedEngine &engine, FObject::Ptr obj) noexcept {
    // FIXME: this is not exception safe at all. need to rethink.
    ParentVec<20> traverseQueue;
    m_parent_topology_sort(traverseQueue, /* reverse */ false);

    m_current = std::move(obj);

    for (auto &&parent : traverseQueue | std::views::drop(1)) {
      parent->onCowChildReplace(engine, *this);
    }
  }

  const FObject &use(const Frame &frame) {
    FONodeBase::use(frame);
    return *m_current;
  }
  const FObject &get() const { return *m_current; }

protected:
  /// @brief constructs new object using current children as inputs. May be
  /// unimplemented(return null) for some objects but in this case those objects
  /// cannot have children.
  /// FIXME: this is noexcept due to replace() being noexcept for now. See upper
  /// fixme.
  /// @note may return null if object does not contain any children. UB if null
  /// may be returned with children.
  virtual FObject::Ptr constructNew(FramedEngine &engine) noexcept = 0;

  void onCowChildReplace(FramedEngine &engine,
                         FONodeImpl<fon_type::cow> &cowp) noexcept final {
    m_current = constructNew(engine);
  }

  bool addParent(FONodeBase &handle) final {
    m_parents.emplace_back(&handle);
    return true;
  }
  void removeParent(FONodeBase &handle) final {
    auto nend = std::remove_if(m_parents.begin(), m_parents.end(),
                               [&](auto &ref) { return ref == &handle; });
    m_parents.erase(nend, m_parents.end());
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

private:
  void m_parent_topology_sort(ParentVecBase &res, bool reverse) {
    // dfs algo.
    boost::container::small_flat_set<FONodeBase *, 20> visited;
    boost::container::small_vector<std::pair<FONodeBase *, bool>, 20> stack;
    stack.emplace_back(this, false);
    while (!stack.empty()) {
      auto &&[next, processed_nei] = stack.back();
      if (processed_nei) {
        res.push_back(next);
        stack.pop_back();
        continue;
      }
      processed_nei = true;
      visited.insert(next);
      if (auto *next_cow = dynamic_cast<FONodeImpl<fon_type::cow> *>(next)) {
        for (auto *p : next_cow->m_parents) {
          if (visited.contains(p))
            continue;
          stack.emplace_back(p, false);
        }
      }
    }
    if (!reverse)
      std::reverse(res.begin(), res.end());
  }
  boost::container::small_vector<FONodeBase *, 2> m_parents;
  // cow's object references are mutable. The inner state of object is
  // immutable.
  FObject::Ptr m_current;
};

template <> class FONodeImpl<fon_type::ext> : public FONodeBase {
public:
  FONodeImpl(auto &&children)
      : FONodeBase(std::forward<decltype(children)>(children)) {}
  FONodeImpl() = default;

  ~FONodeImpl() override = default;

  // reconstructs object and its parents. destroyed objects are not placed in
  // free queue and are destroyed in-place.
  void reconstruct(FramedEngine &engine) {
    destruct(engine);
    construct(engine);
  }

protected:
  template <size_t n>
  using ParentVec =
      boost::container::small_vector<FONodeImpl<fon_type::ext> *, n>;

  using ParentVecBase =
      boost::container::small_vector_base<FONodeImpl<fon_type::ext> *>;

  void onCowChildReplace(FramedEngine &engine,
                         FONodeImpl<fon_type::cow> &cowp) noexcept final {
    reconstruct(engine);
  }

  bool addParent(FONodeBase &handle) final {
    m_parents.emplace_back(static_cast<FONodeImpl<fon_type::ext> *>(&handle));
    return true;
  }
  void removeParent(FONodeBase &handle) final {
    auto nend = std::remove_if(m_parents.begin(), m_parents.end(),
                               [&](auto &ref) { return ref == &handle; });
    m_parents.erase(nend, m_parents.end());
  }
  void destruct(FramedEngine &engine) {
    ParentVec<20> destructQueue;
    m_parent_topology_sort(destructQueue, /* reverse */ true);
    for (auto &obj : destructQueue)
      obj->onDestruct(engine);
  }

  void construct(FramedEngine &engine) {
    ParentVec<20> constructQueue;
    m_parent_topology_sort(constructQueue, /* reverse */ false);
    for (auto &obj : constructQueue)
      obj->onConstruct(engine);
  }

  virtual void onDestruct(FramedEngine &engine) = 0;
  virtual void onConstruct(FramedEngine &engine) = 0;

private:
  void m_parent_topology_sort(ParentVecBase &res, bool reverse) {
    // dfs algo.
    boost::container::small_flat_set<FONodeImpl<fon_type::ext> *, 20> visited;
    boost::container::small_vector<std::pair<FONodeImpl<fon_type::ext> *, bool>,
                                   20>
        stack;
    stack.emplace_back(this, false);
    while (!stack.empty()) {
      auto &&[next, processed_nei] = stack.back();
      if (processed_nei) {
        res.push_back(next);
        stack.pop_back();
        continue;
      }
      processed_nei = true;
      visited.insert(next);
      for (auto *p : next->m_parents) {
        if (visited.contains(p))
          continue;
        stack.emplace_back(p, false);
      }
    }
    if (!reverse)
      std::reverse(res.begin(), res.end());
  }
  // only ext objects can be a parent of another ext object.
  boost::container::small_vector<FONodeImpl<fon_type::ext> *, 2> m_parents;
};

template <fon_type type> class FOExtImpl {};

template <> class FOExtImpl<fon_type::swap> : public FONodeImpl<fon_type::ext> {
public:
  FOExtImpl(FramedEngine &engine, auto &&objectFactory, auto &&children)
      : FONodeImpl<fon_type::ext>(std::forward<decltype(children)>(children)) {
    m_objectsInit(engine, objectFactory);
  }
  FOExtImpl(FramedEngine &engine, auto &&objectFactory) {
    m_objectsInit(engine, objectFactory);
  }

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

  virtual FObject::Ptr constructNew(FramedEngine &engine) = 0;

  void onUse(const Frame &frame) final { onUseAction(frame, *getFor(frame)); }

  bool isUsed(const Frame &frame) const final {
    return getFor(frame)->lastFrame() == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    getFor(frame)->useInFrame(frame.ordinal());
  }

  void onDestruct(FramedEngine &engine) final {
    for (auto &obj : m_objects) {
      delete obj.release();
    }
  }
  void onConstruct(FramedEngine &engine) final {
    for (auto &obj : m_objects) {
      obj = constructNew(engine);
    }
  }
  /// FIXME: not very safe.
  FObject *getFor(const Frame &frame) const {
    return m_objects[frame.id()].get();
  }

private:
  using ObjGen = boost::compat::function_ref<FObject::Ptr(FrameID)>;
  void m_objectsInit(FramedEngine &engine, ObjGen gen);
  // ext's objects references and inner state are mutable.
  boost::container::small_vector<FObject::Ptr, 2> m_objects;
};

template <> class FOExtImpl<fon_type::cow> : public FONodeImpl<fon_type::ext> {
public:
  FOExtImpl(FObject::Ptr obj, auto &&children)
      : FONodeImpl<fon_type::ext>(std::forward<decltype(children)>(children)),
        m_current(std::move(obj)) {}
  FOExtImpl(FObject::Ptr obj) : m_current(std::move(obj)) {}

  const FObject &use(const Frame &frame) {
    FONodeBase::use(frame);
    return *m_current;
  }
  const FObject &get() const { return *m_current; }

protected:
  virtual FObject::Ptr constructNew(FramedEngine &engine) = 0;

  void onUse(const Frame &frame) final {
    // do nothing
  }

  bool isUsed(const Frame &frame) const final {
    return m_current->lastFrame() == frame.ordinal();
  }

  void markUsed(const Frame &frame) final {
    m_current->useInFrame(frame.ordinal());
  }

  void onDestruct(FramedEngine &engine) final { delete m_current.release(); }
  void onConstruct(FramedEngine &engine) final {
    m_current = constructNew(engine);
  }

private:
  // ext's objects references are mutable, but inner state is immutable.
  FObject::Ptr m_current;
};

template <> class FOExtImpl<fon_type::ext> : public FONodeImpl<fon_type::ext> {
public:
  FOExtImpl(auto &&children)
      : FONodeImpl<fon_type::ext>(std::forward<decltype(children)>(children)) {}
  FOExtImpl() = default;

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
  /// FIXME: not very safe.
  FObject *getFor(const Frame &frame) const {
    return m_objects[getExtIndex(frame)].get();
  }

private:
  // ext's objects references and inner state are mutable.
  boost::container::small_vector<FObject::Ptr, 2> m_objects;
};

template <typename T, fon_type type> class FONode {};
template <typename T, fon_type type> class FOENode {};

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
class FOENode<T, fon_type::swap> : public FOExtImpl<fon_type::swap> {
public:
  FOENode(auto &&...args)
      : FOExtImpl<fon_type::swap>(std::forward<decltype(args)>(args)...){};

  T &use(const Frame &frame) {
    return FOExtImpl<fon_type::swap>::use(frame).as<T>();
  }
  const T &get(FrameID frame) const {
    return FOExtImpl<fon_type::swap>::get(frame).as<T>();
  }
};

template <typename T>
class FOENode<T, fon_type::cow> : public FOExtImpl<fon_type::cow> {
public:
  FOENode(auto &&...args)
      : FOExtImpl<fon_type::cow>(std::forward<decltype(args)>(args)...){};

  const T &use(const Frame &frame) {
    return FOExtImpl<fon_type::cow>::use(frame).as<T>();
  }
  const T &get() const { return FOExtImpl<fon_type::cow>::get().as<T>(); }
};

template <typename T>
class FOENode<T, fon_type::ext> : public FOExtImpl<fon_type::ext> {
public:
  FOENode(auto &&...args)
      : FOExtImpl<fon_type::ext>(std::forward<decltype(args)>(args)...){};

  T &use(const Frame &frame) {
    return FOExtImpl<fon_type::ext>::use(frame).as<T>();
  }
  const T &get(FrameID frame) const {
    return FOExtImpl<fon_type::ext>::get(frame).as<T>();
  }
};

template <typename T> using Ref = boost::intrusive_ptr<T>;

} // namespace imvk