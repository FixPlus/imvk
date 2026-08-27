#pragma once

#include <boost/intrusive_ptr.hpp>

#include <memory>

namespace imvk {

class FramedEngine;
using FrameID = unsigned;
class FONodeBase;

template <typename T> using Ref = boost::intrusive_ptr<T>;

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

template <typename T> using FObject = std::pair<T, FrameID>;

} // namespace imvk