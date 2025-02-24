#pragma once
#include "imvk/base/EngineBase.hpp"
#include "imvk/base/Utils.hpp"

#include "vkw/CommandBuffer.hpp"
#include "vkw/CommandPool.hpp"

namespace imvk {

class ContextImpl;
class FrameObject;

/// @brief Controls capture of commands to be submitted for frame render and
/// manages lifetimes of resources used in those commands.
class Frame final {
public:
  /// @brief Ends the previous scope of this frame and immediately starts a new
  /// scope. Frame objects that were registered for previous scope as used are
  /// unmarked but may remain registered. Registered objects that were not
  /// marked as used may be disposed here.
  void begin();

  /// @brief Finishes frame capture and flushes pending synchronous write
  /// operations for used objects. After this point, captured work is ready for
  /// submission. The engine is expected to submit recorded command buffer and
  /// wait for it to finish execution before calling begin() again.
  void end();

  /// @brief Frees up all registered objects.
  void terminate();

  /// @return the engine this frame is registered in.
  FramedEngine &engine() const { return m_engine; }

  /// @return id of this frame. Is unique for each frame.
  const auto &id() const { return m_id; }

  /// @brief Register object as used in this frame. This ensures that lifetime
  /// of this object is prolonged enough to reach end of scope for this frame.
  /// @param object shared pointer to frame object.
  void use(const std::shared_ptr<FrameObject> &object) const;

  /// @return Command buffer that is used to capture current frame work.
  vkw::PrimaryCommandBuffer &commands() const { return m_commandBuffer; }
  ~Frame();

private:
  /// @brief Constructs frame #id for specified engine.
  /// @param engine
  /// @param id
  Frame(FramedEngine &engine, unsigned id);
  friend class FrameCreator;

  FramedEngine &m_engine;
  unsigned m_id;
  mutable vkw::PrimaryCommandBuffer m_commandBuffer;
  mutable LinearTable<unsigned, std::pair<std::shared_ptr<FrameObject>, bool>>
      m_registeredObjects;
  std::vector<unsigned> m_toBeDeleted;
};

/// @brief Interface factory for Frame creation. Only friends of this class may
/// create frames.
class FrameCreator {
private:
  static Frame *create(FramedEngine &engine, unsigned id) {
    return new Frame(engine, id);
  }
  /// Currently, only FramedEngine is allowed to create Frame objects.
  friend class FramedEngine;
};

/// @brief Base class for any frame objects. Frame objects are required to be
/// managed by shared_ptr to be used in frame. This allows frame to prolong
/// object's life till the end of frame scope they are used in, meanwhile their
/// respective owners could asynchronously 'disown' them. Act of disowning must
/// call 'disown' method.
class FrameObject {
public:
  /// @brief Create a frame object. This frame object must only be used within
  /// engine it was created from.
  /// @param engine
  FrameObject(FramedEngine &engine) {
    m_frameIds.resize(engine.getFIFCount(), 0u);
  }

  bool isDisowned() const { return m_disowned.load(); }

  /// @brief Disowns the object. Must be called by main owner of object before
  /// reference to it is disposed. Thread safe.
  void disown() { m_disowned.store(true); }

  virtual ~FrameObject() = default;

private:
  friend class Frame;
  void m_regiterForFrame(const Frame &frame, unsigned id) {
    auto frameID = frame.id();
    assert(m_frameIds.size() > frameID);
    m_frameIds[frameID] = id;
  }

  unsigned m_getID(const Frame &frame) const {
    auto frameID = frame.id();
    assert(m_frameIds.size() > frameID);
    return m_frameIds[frameID];
  }

  boost::container::small_vector<unsigned, 3> m_frameIds;
  std::atomic<bool> m_disowned = false;
};

} // namespace imvk