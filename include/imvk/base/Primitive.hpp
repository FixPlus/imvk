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
class PrimitiveHandle : public FrameObject {
public:
  PrimitiveHandle(FramedEngine &engine) : FrameObject(engine) {}

  /// @brief Writes a primitive to the binding in specified descriptor set.
  /// Implementation is not required to provide this operation.
  /// @param set
  /// @param binding
  /// @param writeOpID operation identificator passed to descriptor set along
  /// the handle. implementation can you it to choose write operation.
  virtual void write(vkw::DescriptorSet &set, unsigned binding,
                     unsigned writeOpID) = 0;

  bool isDisowned() const { return m_disowned.load(std::memory_order_acquire); }

protected:
  /// @brief called when write() is invoked for primitive implementation that
  /// does not provide write method.
  static void m_implNotProvided(std::string_view name);

private:
  friend class PrimitiveBase;
  std::atomic<bool> m_disowned = false;
};

/// @brief Type-erased handle for set of allocatable objects used by frames.
/// Each frame has exactly one object designated to it. However
/// same object may be used in multiple frames and it's up to
/// derived implementation to decide.
class PrimitiveBase {
public:
  /// @brief Types of primitive implementations
  enum class Type { cow, swap };

  PrimitiveBase(Type type) : m_type(type) {}

  /// @brief retrieves handle to primitive object for specified frame.
  /// @param frame
  /// @return shared reference to primitive object.
  virtual std::shared_ptr<PrimitiveHandle> get(const Frame &frame) const = 0;

  /// @brief Returns true if this primitive shares single handle for every
  /// frame.
  /// @return bool
  virtual bool hasOnePrimitive() const = 0;

  const auto &type() const { return m_type; }

  virtual ~PrimitiveBase() = default;

protected:
  static void disown(PrimitiveHandle &prim) {
    prim.m_disowned.store(true, std::memory_order_release);
  }

private:
  Type m_type;
};

template <typename T, PrimitiveBase::Type PrimType>
concept IsPrimitiveAllocator = requires {
                                 /// Allocator must provide definition of
                                 /// HandleType that it returns on allocate()
                                 typename T::HandleType;
                                 { *std::declval<T::HandleType>() };
                               };

template <typename T, PrimitiveBase::Type PrimType>
concept IsPrimitiveTrait =
    requires {
      // Trait must provide an allocator type.
      typename T::template Allocator<PrimType>;
      IsPrimitiveAllocator<typename T::template Allocator<PrimType>, PrimType>;
    };

template <typename T>
concept AnyPrimitiveTrait = IsPrimitiveTrait<T, PrimitiveBase::Type::cow> ||
                            IsPrimitiveTrait<T, PrimitiveBase::Type::swap>;

template <typename T, PrimitiveBase::Type PrimType>
concept WritablePrimitive =
    IsPrimitiveTrait<T, PrimType> &&
    requires(typename T::template Allocator<PrimType>::HandleType &handle,
             vkw::DescriptorSet &set, unsigned binding, unsigned writeOpID) {
      { std::invoke(T::write, *handle, set, binding, writeOpID) };
    };

/// @brief Type-aware implementation for any allocatable object used by frame.
/// @tparam PrimitiveTraits - trait class, defining primitive behaviour
/// @tparam PrimType - primitive strategy.
template <AnyPrimitiveTrait PrimitiveTraits, PrimitiveBase::Type PrimType>
  requires IsPrimitiveTrait<PrimitiveTraits, PrimType>
class PrimitiveHandleImpl final : public PrimitiveHandle {
private:
  using Allocator = typename PrimitiveTraits::template Allocator<PrimType>;
  using HandleType = typename Allocator::HandleType;
  using ValueType =
      std::remove_reference_t<decltype(*std::declval<HandleType>())>;

public:
  PrimitiveHandleImpl(FramedEngine &engine, auto &&...args)
      : PrimitiveHandle(engine),
        m_handle(std::forward<decltype(args)>(args)...){};

  void write(vkw::DescriptorSet &set, unsigned binding,
             unsigned writeOpID) final {
    if constexpr (WritablePrimitive<PrimitiveTraits, PrimType>)
      std::invoke(PrimitiveTraits::write, *m_handle, set, binding, writeOpID);
    else
      m_implNotProvided("write");
  }

  /// @return mutable reference to value pointed by handle.
  ValueType &get() { return *m_handle; }

  /// @return constant reference to value pointed by handle.
  const ValueType &get() const { return *m_handle; }

private:
  HandleType m_handle;
};

/// @brief Type-aware interface for primitive. Is used to get references to
/// fully typed primitive objects. Template parameters are the same as in
/// PrimitiveHandleImpl.
/// @tparam PrimitiveTraits
/// @tparam PrimType
template <AnyPrimitiveTrait PrimitiveTraits, PrimitiveBase::Type PrimType>
  requires IsPrimitiveTrait<PrimitiveTraits, PrimType>
class PrimitiveImpl : public PrimitiveBase {
public:
  using Allocator = typename PrimitiveTraits::template Allocator<PrimType>;
  using HandleType = PrimitiveHandleImpl<PrimitiveTraits, PrimType>;

  PrimitiveImpl() : PrimitiveBase(PrimType) {}

  /// @brief Type-aware wrapper for get()
  /// @param frame
  /// @return Typed shared reference to primitive object.
  std::shared_ptr<HandleType> getImpl(const Frame &frame) const {
    return std::static_pointer_cast<HandleType>(get(frame));
  }
};

/// @brief Main template used to construct implementation for any primitive
/// type with any strategy.
/// @tparam PrimitiveTraits
/// @tparam PrimType
template <AnyPrimitiveTrait PrimitiveTraits, PrimitiveBase::Type PrimType>
  requires IsPrimitiveTrait<PrimitiveTraits, PrimType>
class Primitive : PrimitiveImpl<PrimitiveTraits, PrimType> {};

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
/// @tparam PrimitiveTraits - traits of primitive
template <AnyPrimitiveTrait PrimitiveTraits>
  requires IsPrimitiveTrait<PrimitiveTraits, PrimitiveBase::Type::cow>
class Primitive<PrimitiveTraits, PrimitiveBase::Type::cow>
    : public PrimitiveImpl<PrimitiveTraits, PrimitiveBase::Type::cow> {
private:
  using BaseTy = PrimitiveImpl<PrimitiveTraits, PrimitiveBase::Type::cow>;
  using PrimitiveBase::disown;
  using typename BaseTy::Allocator;
  using typename BaseTy::HandleType;

  struct State {
    State(FramedEngine &engine, auto &&...args)
        : engine(engine), allocator(std::forward<decltype(args)>(args)...) {}
    FramedEngine &engine;
    Allocator allocator;
    std::atomic<std::shared_ptr<HandleType>> primitive = nullptr;
    ~State() {
      auto prim = primitive.load();
      if (prim)
        disown(*prim);
    }
  };

public:
  /// @brief Constructs cow primitive. Primitive object is in invalid state
  /// after construction.
  /// @param engine Frame engine this primitive object shall be used for.
  /// @param args parameters for constructor of Allocator object.
  Primitive(FramedEngine &engine, auto &&...args)
      : m_state(std::make_shared<State>(
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
  /// 3. Atomic substitution of handle.
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
      auto &engine = stateCopy->engine;
      // Allocate object and get initialization future.
      auto &&[newPrimitiveObject, initFuture] = stateCopy->allocator.allocate(
          engine, std::forward<decltype(args)>(args)...);
      auto newPrimitive =
          std::make_shared<HandleType>(engine, std::move(newPrimitiveObject));
      return std::async(
          std::launch::deferred,
          [stateCopy = std::move(stateCopy),
           newPrimitive = std::move(newPrimitive),
           initFuture = std::move(initFuture)]() mutable {
            // Wait for initialization process to complete.
            initFuture.get();
            return std::async(
                std::launch::deferred,
                [stateCopy = std::move(stateCopy),
                 newPrimitive = std::move(newPrimitive)]() mutable {
                  // safely insert new primitive.
                  auto stale = stateCopy->primitive.exchange(
                      std::move(newPrimitive), std::memory_order_relaxed);
                  if (stale)
                    disown(*stale);
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
    return std::async(
        std::launch::deferred, [stateCopy = std::move(stateCopy)]() {
          auto stale =
              stateCopy->primitive.exchange(nullptr, std::memory_order_relaxed);
          if (stale)
            disown(*stale);
        });
  }

  bool hasOnePrimitive() const override { return true; }

  std::shared_ptr<PrimitiveHandle> get(const Frame &frame) const override {
    return m_state->primitive.load();
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
template <AnyPrimitiveTrait PrimitiveTraits>
  requires IsPrimitiveTrait<PrimitiveTraits, PrimitiveBase::Type::cow>
class Primitive<PrimitiveTraits, PrimitiveBase::Type::swap>
    : public PrimitiveImpl<PrimitiveTraits, PrimitiveBase::Type::swap> {
private:
  using BaseTy = PrimitiveImpl<PrimitiveTraits, PrimitiveBase::Type::swap>;
  using PrimitiveBase::disown;
  using typename BaseTy::Allocator;
  using typename BaseTy::HandleType;

public:
  /// @brief Constructs swap primitive. All primitive object are in invalid
  /// state after construction.
  /// @param engine Frame engine this primitive object shall be used for.
  /// @param args parameters for constructor of Allocator object.
  Primitive(FramedEngine &engine, auto &&...args)
      : m_prims(engine.getFIFCount()), m_engine(engine),
        m_allocator(std::make_unique<Allocator>(
            std::forward<decltype(args)>(args)...)) {}

  Primitive(Primitive &&another) noexcept
      : m_prims(std::move(another.m_prims)),
        m_engine(std::move(another.m_engine)),
        m_allocator(std::move(another.m_allocator)) {
    another.m_moved_out = true;
  }
  Primitive(const Primitive &another) = delete;

  Primitive &operator=(Primitive &&another) noexcept {
    if (this == &another)
      return;
    std::swap(m_prims, another.m_prims);
    std::swap(m_engine, another.m_engine);
    std::swap(m_allocator, another.m_allocator);
    another.m_moved_out = true;
  }
  Primitive &operator=(const Primitive &another) = delete;
  ~Primitive() override {
    if (m_moved_out)
      return;
    for (auto &&prim : m_prims)
      if (prim)
        disown(*prim);
  }
  /// @brief Recreates object for specified frame. This method must only be
  /// called within specified frame scope.
  /// @param frame
  /// @param args additional arguments to pass to Allocator's 'allocate' method.
  void reset(const Frame &frame, auto &&...args) {
    auto stale = std::make_shared<HandleType>(
        m_engine.get(),
        m_allocator.allocate(m_engine.get(),
                             std::forward<decltype(args)>(args)...));
    std::swap(m_prims.at(frame.id()), stale);
    if (stale)
      disown(*stale);
  }

  /// @brief Recreated objects for every frame. This method must only be called
  /// outside scope of all frames.
  /// @param args additional arguments to pass to Allocator's 'allocate' method.
  /// Each object is constructed using same argument list.
  void resetAll(auto &&...args) {
    for (auto &prim : m_prims) {
      auto stale = std::make_shared<HandleType>(
          m_engine.get(),
          m_allocator->allocate(m_engine.get(),
                                std::forward<decltype(args)>(args)...));
      std::swap(stale, prim);
      if (stale)
        disown(*stale);
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
    m_allocator->write(prim.get(), frame,
                       std::forward<decltype(args)>(args)...);
  }

  bool hasOnePrimitive() const override { return false; }

  std::shared_ptr<PrimitiveHandle> get(const Frame &frame) const override {
    return m_prims.at(frame.id());
  }

private:
  boost::container::small_vector<std::shared_ptr<HandleType>, 3> m_prims;
  std::reference_wrapper<FramedEngine> m_engine;
  std::unique_ptr<Allocator> m_allocator;
  bool m_moved_out = false;
};

} // namespace imvk