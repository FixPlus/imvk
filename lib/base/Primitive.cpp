#include "imvk/base/Primitive.hpp"

namespace imvk {

void PrimitiveHandle::m_implNotProvided(std::string_view name) {
  throw std::runtime_error(
      "Calling write() on a primitive without specified traits");
}

} // namespace imvk