#pragma once

#include "imvk/base/Frame.hpp"

#include "boost/container/small_vector.hpp"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>

namespace imvk {

/// @brief Type-erased interface for any allocatable object used by frame.
/// Lifetime of this object is controlled via reference counting system.
class PrimitiveHandleBase : public FrameObject {
public:
  PrimitiveHandleBase(FramedEngine &engine) : FrameObject(engine) {}
  virtual ~PrimitiveHandleBase() = default;

  virtual void write(vkw::DescriptorSet &set, unsigned binding,
                     unsigned writeOpID) = 0;
};

/// @brief Type-aware implementation for any allocatable object used by frame.
/// @tparam T - type of primitive (usually vulkan allocatable object).
/// @tparam PrimitiveTraits - traits
template <typename T, typename PrimitiveTraits>
class PrimitiveHandleImpl : public PrimitiveHandleBase, public T {
public:
  PrimitiveHandleImpl(FramedEngine &engine, auto &&...args)
      : T(std::forward<decltype(args)...>(args)){};

  void write(vkw::DescriptorSet &set, unsigned binding,
             unsigned writeOpID) override {
    std::invoke(PrimitiveTraits::write, *this, set, binding, writeOpID);
  }
};

using PrimitiveHandle = std::shared_ptr<PrimitiveHandleBase>;

/// @brief Type-erased handle for set of allocatable objects used by frames.
/// Each frame has exactly one object designated to it. However
/// same object may be used in multiple frames and it's up to
/// derived implementation to decide.
class Primitive {
public:
  /// @brief Types of primitive implementations
  enum class Type { cow, swap };

  Primitive(Type type) : m_type(type) {}

  /// @brief retrieves handle to primitive object for specified frame.
  /// @param frame
  /// @return shared reference to primitive object.
  virtual PrimitiveHandle get(const Frame &frame) const = 0;

  const auto &type() const { return m_type; }

  virtual ~Primitive() = default;

private:
  Type m_type;
};

/// @brief Type-aware interface for primitive. Is used to get references to
/// fully typed primitive objects.
template <typename T, typename PrimitiveTraits>
class PrimitiveImpl : public Primitive {
public:
  PrimitiveImpl(Type type) : Primitive(type) {}

  /// @brief Type-aware wrapper for get()
  /// @param frame
  /// @return Typed shared reference to primitive object.
  std::shared_ptr<PrimitiveHandleImpl<T, PrimitiveTraits>>
  getImpl(const Frame &frame) const {
    return std::static_pointer_cast<PrimitiveHandleImpl<T, PrimitiveTraits>>(
        get(frame));
  }
};

/// @brief Copy-on-write strategy primitive implementation.
///        It has only one primitive object for each frame which can benefit
///        greatly if memory consumption is a concern. Downside of it is a
///        rather costly object modifications which involve allocating new
///        object. Any modification called by reset() method are not required to
///        become visible during a duration of current running frame and, on the
///        other hand, those modifications are not required to synchronize with
///        frame operation. That means - reset() method can be called on any
///        application's thread without external synchronization. Moreover,
///        mutiple concurrent calls to reset() for same primitive are allowed.
///        However, order of completion for such calls may be different than
///        order of their submission.
/// @tparam T - Type of primitive.
/// @tparam Traits - traits of primitive
/// @tparam Allocator - COWAllocator-like type that must implement
///         'allocate' method.
template <typename T, typename PrimitiveTraits, typename Allocator>
class COWPrimitive : public PrimitiveImpl<T, PrimitiveTraits> {
private:
  struct State {
    State(FramedEngine &engine, auto &&...args)
        : engine(engine), allocator(std::forward<decltype(args)>(args)...) {}
    FramedEngine &engine;
    Allocator allocator;
    std::shared_ptr<PrimitiveHandleImpl<T, PrimitiveTraits>> primitive =
        nullptr;
    ~State() {
      if (primitive)
        primitive->disown();
    }
    mutable std::mutex mutex;
  };

public:
  /// @brief Constructs cow primitive. Primitive object is in invalid state
  /// after construction.
  /// @param engine Frame engine this primitive object shall be used for.
  /// @param args parameters for constructor of Allocator object.
  COWPrimitive(FramedEngine &engine, auto &&...args)
      : PrimitiveImpl<T, PrimitiveTraits>(Primitive::Type::cow),
        m_state(std::make_shared<State>(
            engine, std::forward<decltype(args)>(args)...)) {}

  /// @brief Write new data in primitive.
  /// This is done via three steps:
  /// 1. Call allocator's allocate method to get handle to newly constructed
  /// primitive and
  ///    a future for initialization process completion. This is done because
  ///    initialization process may involve lengthy operation on cpu or gpu side
  ///    (like copying, layout transitions etc.). If allocator initializes
  ///    object in-place, it still needs to provide a future that is set to
  ///    ready state.
  /// 2. Initialization completion wait.
  /// 3. Atomic substitution of handle - done by holding a unique lock.
  ///
  /// This method defers execution of this three steps in async::deferred
  /// fashion. It does not mutate primitive right away, that's why it is marked
  /// const and does not block - safe to call in time critical sections. Each
  /// of this steps may be executed on any executor, everything is internally
  /// synchronized.
  /// @param args arguments consumed by allocator's allocate method.
  /// @return a future to a future to a future of void. Each future is
  /// responsible for one step of a process, described above.
  [[nodiscard("remember the futures")]] auto reset(auto &&...args) const {
    auto stateCopy = m_state;
    return std::async(std::launch::deferred, [stateCopy = std::move(stateCopy),
                                              ... args =
                                                  std::move(args)]() mutable {
      // Allocate object and get initialization future.
      auto &&[newPrimitive, initFuture] =
          std::shared_ptr<PrimitiveHandleImpl<T, PrimitiveTraits>>(
              stateCopy->m_allocator.allocate(
                  stateCopy->engine, std::forward<decltype(args)>(args)...));
      return std::async(std::launch::deferred,
                        [stateCopy = std::move(stateCopy),
                         newPrimitive = std::move(newPrimitive),
                         initFuture = std::move(initFuture)]() mutable {
                          // Wait for initialization process to complete.
                          initFuture.get();
                          return std::async(
                              std::launch::deferred,
                              [stateCopy = std::move(stateCopy),
                               newPrimitive = std::move(newPrimitive)]() {
                                // safely insert new primitive.
                                auto lock = std::unique_lock{stateCopy->mutex};
                                auto stale = stateCopy->primitive;
                                stateCopy->primitive = newPrimitive;
                                lock.unlock();
                                // delete old primitive after mutex unlock.
                                stale->disown();
                                stale.reset();
                              });
                        });
    });
  }

  /// @brief Invalidate primitive.
  /// Unlike the reset method above it executes only 3rd step (still deferred)
  /// This step atomically replaces primitive handle with nullptr.
  /// @return future of void
  auto reset() const {
    auto stateCopy = m_state;
    return std::async(std::launch::deferred,
                      [stateCopy = std::move(stateCopy)]() {
                        // safely erase primitive.
                        auto lock = std::unique_lock{stateCopy->mutex};
                        auto stale = stateCopy->primitive;
                        stateCopy->primitive = nullptr;
                        lock.unlock();
                        // delete old primitive after mutex unlock.
                        stale->disown();
                        stale.reset();
                      });
  }

  PrimitiveHandle get(const Frame &frame) const override {
    auto lock = std::unique_lock{m_state->mutex};
    return m_state->primitive;
  }

private:
  std::shared_ptr<State> m_state;
};

/// @brief Swap strategy primitive implementation.
///        It has one copy of object per frame. That demands more memory,
///        however any changes made to this primitive are certain to be visible
///        within this frame. All accesses to this primitive must be externally
///        synchronized with frame operation.
/// @tparam T - Type of primitive.
/// @tparam Traits - traits of primitive
/// @tparam Allocator - SwapAllocator-like type that must implement
///         'allocate' and 'write' methods.
template <typename T, typename PrimitiveTraits, typename Allocator>
class SwapPrimitive : public PrimitiveImpl<T, PrimitiveTraits> {
private:
  struct Disowner {
    void operator()(SwapPrimitive<T, PrimitiveTraits, Allocator> *prim) {
      for (auto &&prim : prim->m_prims)
        prim.disown();
    }
  };

public:
  /// @brief Constructs swap primitive. All primitive object are in invalid
  /// state after construction.
  /// @param engine Frame engine this primitive object shall be used for.
  /// @param args parameters for constructor of Allocator object.
  SwapPrimitive(FramedEngine &engine, auto &&...args)
      : PrimitiveImpl<T, PrimitiveTraits>(Primitive::Type::cow),
        m_engine(engine), m_allocator(std::forward<decltype(args)>(args)...),
        m_prims(m_engine.get().getFIFCount()), m_disowner(this) {}

  /// @brief Recreates object for specified frame. This method must only be
  /// called within specified frame scope.
  /// @param frame
  /// @param args additional arguments to pass to Allocator's 'allocate' method.
  void reset(const Frame &frame, auto &&...args) {
    auto stale = m_prims.at(frame.id());
    m_prims.at(frame.id()) =
        std::shared_ptr<PrimitiveHandleImpl<T, PrimitiveTraits>>(
            m_allocator.allocate(m_engine.get(),
                                 std::forward<decltype(args)>(args)...));
    stale.disown();
  }

  /// @brief Recreated objects for every frame. This method must only be called
  /// outside scope of all frames.
  /// @param args additional arguments to pass to Allocator's 'allocate' method.
  /// Each object is constructed using same argument list.
  void resetAll(auto &&...args) {
    for (auto &prim : m_prims) {
      auto stale = prim;
      prim = std::shared_ptr<PrimitiveHandleImpl<T, PrimitiveTraits>>(
          m_allocator.allocate(m_engine.get(),
                               std::forward<decltype(args)>(args)...));
      stale->disown();
    }
  }

  /// @brief Write new data to object for specified frame. This method must only
  /// be called within specified frame scope. Allocator must notify frame about
  /// changes made to this primitive and synchronize this write with subsequent
  /// frame submission.
  /// @param frame
  /// @param args additional arguments to pass to Allocator's 'write' method.
  void write(const Frame &frame, auto &&...args) {
    auto &prim = *m_prims.at(frame.id());
    m_allocator.write(prim, frame, std::forward<decltype(args)>(args)...);
  }

  std::shared_ptr<PrimitiveHandle> get(const Frame &frame) const override {
    return m_prims.at(frame.id());
  }

private:
  std::reference_wrapper<FramedEngine> m_engine;
  Allocator m_allocator;
  std::vector<std::shared_ptr<PrimitiveHandleImpl<T, PrimitiveTraits>>> m_prims;
  std::unique_ptr<SwapPrimitive<T, PrimitiveTraits, Allocator>, Disowner>
      m_disowner;
};

} // namespace imvk