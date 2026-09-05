#pragma once

#include "imvk/graph/Types.hpp"
#include <boost/container_hash/hash.hpp>

#include <ostream>
#include <variant>
#include <vkw/Image.hpp>

namespace std {
template <> struct hash<VkExtent3D> {
  size_t operator()(VkExtent3D exts) const {
    auto ret = std::hash<unsigned>{}(exts.width);
    boost::hash_combine(ret, exts.height);
    boost::hash_combine(ret, exts.depth);
    return ret;
  }
};

template <> struct equal_to<VkExtent3D> {
  size_t operator()(VkExtent3D a, VkExtent3D b) const {
    return a.width == b.width && a.height == b.height && a.depth == b.depth;
  }
};

} // namespace std
inline bool operator==(VkExtent3D a, VkExtent3D b) {
  return std::equal_to<VkExtent3D>{}(a, b);
}

namespace imvk::graph {

template <typename T> class Attribute {
public:
  enum class Status { undefined, constant, dynamic, overdefined };
  Attribute() = default;

  auto status() const { return m_status; }

  std::optional<T> getConstant() const {
    if (m_status == Status::constant)
      return std::get<T>(m_source);
    else
      return std::nullopt;
  }

  const Value *getDynamicValue() const {
    if (m_status == Status::dynamic)
      return std::get<const Value *>(m_source);
    else
      return nullptr;
  }

  void setConstant(T cnst) {
    m_status = Status::constant;
    m_source = cnst;
  }

  void setDynamic(const Value &val) {
    m_status = Status::dynamic;
    m_source = &val;
  }
  void setOverdefined() {
    m_status = Status::overdefined;
    m_source = nullptr;
  }

  bool operator==(const Attribute &another) const = default;
  static std::string_view statusStr(Status status) {
    switch (status) {
    case Status::undefined:
      return "undef";
    case Status::constant:
      return "const";
    case Status::dynamic:
      return "dynam";
    case Status::overdefined:
      return "overd";
    }
    return "err";
  }

  size_t hash() const {
    auto ret = static_cast<size_t>(m_status);
    boost::hash_combine(ret,
                        std::hash<std::variant<T, const Value *>>{}(m_source));
    return ret;
  }

  template <typename U> operator Attribute<U>() const {
    Attribute<U> ret;
    if (m_status == Status::undefined)
      return ret;
    if (m_status == Status::constant) {
      ret.setConstant(static_cast<U>(std::get<T>(m_source)));
      return ret;
    }
    if (m_status == Status::dynamic) {
      ret.setDynamic(*std::get<const Value *>(m_source));
      return ret;
    }
    ret.setOverdefined();
    return ret;
  }

private:
  Status m_status = Status::undefined;
  std::variant<T, const Value *> m_source{nullptr};
};

template <typename T> Attribute<T> undefined() { return {}; }
template <typename T> Attribute<T> constant(T value) {
  Attribute<T> def;
  def.setConstant(value);
  return def;
}
template <typename T> Attribute<T> dynamic(const Value &value) {
  Attribute<T> def;
  def.setDynamic(value);
  return def;
}

template <typename T> Attribute<T> overdefined() {
  Attribute<T> def;
  def.setOverdefined();
  return def;
}

template <typename T>
inline std::ostream &operator<<(std::ostream &os, const Attribute<T> &attr) {
  using enum Attribute<T>::Status;
  os << attr.statusStr(attr.status());
  switch (attr.status()) {
  case Attribute<T>::Status::undefined:
  case Attribute<T>::Status::overdefined:
    break;
  case Attribute<T>::Status::constant:
    os << " = " << *attr.getConstant() << " ";
    break;
  case Attribute<T>::Status::dynamic:
    os << " -> " << *attr.getDynamicValue() << " ";
  }
  return os;
}

template <typename T> class Attributes {};

template <> class Attributes<ImageTy> : public AttributesBase {
public:
  Attribute<VkExtent3D> extents;
  Attribute<VkFormat> format;
  Attribute<size_t> layers;
  Attribute<size_t> levels;

  Attributes() = default;
  Attributes(Attribute<VkExtent3D> exts, Attribute<VkFormat> fmt,
             Attribute<size_t> lays, Attribute<size_t> lvls)
      : extents(exts), format(fmt), layers(lays), levels(lvls) {}
  void dump(std::ostream &os) const final {
    os << "extents: " << extents << "\n";
    os << "format: " << format << "\n";
    os << "layers: " << layers << "\n";
    os << "levels: " << levels << "\n";
  }
  std::size_t hash() const final {
    auto ret = typeid(Attributes<ImageTy>).hash_code();
    boost::hash_combine(ret, extents.hash());
    boost::hash_combine(ret, format.hash());
    boost::hash_combine(ret, layers.hash());
    boost::hash_combine(ret, levels.hash());
    return ret;
  }
  bool operator==(const Attributes<ImageTy> &another) const {
    return extents == another.extents && format == another.format &&
           layers == another.layers && levels == another.levels;
  }

  bool compatibleWith(const Attributes<ImageTy> &another) const {
    auto isCompatible = [](const auto &a, const auto &b) {
      if (a == b)
        return true;
      if (a.getConstant() && b.getConstant())
        return *a.getConstant() <= *b.getConstant();
      return false;
    };
    return extents == another.extents && format == another.format &&
           isCompatible(layers, another.layers) &&
           isCompatible(levels, another.levels);
  }

  bool operator==(const AttributesBase &another) const final {
    if (auto *casted = dyn_cast<Attributes<ImageTy>>(&another))
      return *this == *casted;
    return false;
  }
};

template <> class Attributes<IntegerScalarTy> : public AttributesBase {
public:
  Attribute<size_t> value;
  Attributes() = default;
  Attributes(Attribute<size_t> val) : value(val) {}
  void dump(std::ostream &os) const final { os << value << "\n"; }
  std::size_t hash() const final {
    auto ret = typeid(Attributes<IntegerScalarTy>).hash_code();
    boost::hash_combine(ret, value.hash());
    return ret;
  }
  bool operator==(const Attributes<IntegerScalarTy> &another) const {
    return value == another.value;
  }
  bool operator==(const AttributesBase &another) const final {
    if (auto *casted = dyn_cast<Attributes<IntegerScalarTy>>(&another))
      return *this == *casted;
    return false;
  }
};

template <> class Attributes<FormatTy> : public AttributesBase {
public:
  Attribute<VkFormat> value;
  Attributes() = default;
  Attributes(Attribute<VkFormat> val) : value(val) {}
  void dump(std::ostream &os) const final { os << value << "\n"; }
  std::size_t hash() const final {
    auto ret = typeid(Attributes<FormatTy>).hash_code();
    boost::hash_combine(ret, value.hash());
    return ret;
  }
  bool operator==(const Attributes<FormatTy> &another) const {
    return value == another.value;
  }
  bool operator==(const AttributesBase &another) const final {
    if (auto *casted = dyn_cast<Attributes<FormatTy>>(&another))
      return *this == *casted;
    return false;
  }
};

template <> class Attributes<ExtentsTy> : public AttributesBase {
public:
  Attribute<VkExtent3D> extents;

  Attributes() = default;
  Attributes(Attribute<VkExtent3D> val) : extents(val) {}
  void dump(std::ostream &os) const final { os << extents << "\n"; }
  std::size_t hash() const final {
    auto ret = typeid(Attributes<ExtentsTy>).hash_code();
    boost::hash_combine(ret, extents.hash());
    return ret;
  }
  bool operator==(const Attributes<ExtentsTy> &another) const {
    return extents == another.extents;
  }
  bool operator==(const AttributesBase &another) const final {
    if (auto *casted = dyn_cast<Attributes<ExtentsTy>>(&another))
      return *this == *casted;
    return false;
  }
};

template <> class Attributes<BufferTy> : public AttributesBase {
public:
  Attribute<size_t> size;

  Attributes() = default;
  Attributes(Attribute<size_t> val) : size(val) {}
  void dump(std::ostream &os) const final { os << size << "\n"; }
  std::size_t hash() const final {
    auto ret = typeid(Attributes<BufferTy>).hash_code();
    boost::hash_combine(ret, size.hash());
    return ret;
  }
  bool operator==(const Attributes<BufferTy> &another) const {
    return size == another.size;
  }
  bool operator==(const AttributesBase &another) const final {
    if (auto *casted = dyn_cast<Attributes<BufferTy>>(&another))
      return *this == *casted;
    return false;
  }
};

class AttributesAnalysis {
public:
  AttributesAnalysis(Workflow &wf);

  template <typename T = AttributesBase>
  const T &getAttributesFor(const Value &val) const {
    return static_cast<const T &>(*m_attributeMap.at(&val));
  }

  void insertValue(const Value *v, const AttributesBase *a) {
    m_attributeMap.insert({v, a});
  }

  void dump(std::ostream &os) const;

private:
  std::unordered_map<const Value *, const AttributesBase *> m_attributeMap;
};

} // namespace imvk::graph