#include "imvk/base/Object.hpp"

namespace imvk {

void intrusive_ptr_add_ref(FONodeBase *p) { p->m_refCount++; }
void intrusive_ptr_release(FONodeBase *p) {
  if (--(p->m_refCount) == 0)
    delete p;
}

} // namespace imvk