#include "imvk/base/Frame.hpp"
#include "imvk/base/EngineBase.hpp"

namespace imvk {

void intrusive_ptr_add_ref(FONodeBase *p) { p->m_refCount++; }
void intrusive_ptr_release(FONodeBase *p) {
  if (--(p->m_refCount) == 0)
    delete p;
}

void FObject::Deleter::operator()(FObject *obj) const {
  if (!m_engine || !obj)
    return;
  m_engine->destroyObject(obj);
}

namespace __detail {
unsigned getFIFCount(FramedEngine &e) { return e.getFIFCount(); }
} // namespace __detail
} // namespace imvk