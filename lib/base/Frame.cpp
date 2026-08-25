#include "imvk/base/Frame.hpp"
#include "imvk/base/EngineBase.hpp"

namespace imvk {

void intrusive_ptr_add_ref(FONodeBase *p) { p->m_refCount++; }
void intrusive_ptr_release(FONodeBase *p) {
  if (--(p->m_refCount) == 0)
    delete p;
}

void FONodeBaseImpl<fon_type::swap_mut>::m_objectsInit(
    FramedEngine &engine, ObjGen gen,
    boost::container::small_vector_base<FObject::Ptr> &out) {
  std::ranges::transform(engine.frameIds(), std::back_inserter(out), gen);
}

void FONodeBaseImpl<fon_type::swap>::onConstruct(FramedEngine &engine) {
  m_objects.resize(engine.getFIFCount());
  for (auto &&id : engine.frameIds()) {
    auto optNew = constructNew(engine, id);
    if (!keepAlive() || !m_objects[id])
      m_objects[id] = std::move(optNew);
  }
}

void FObject::Deleter::operator()(FObject *obj) const {
  if (!m_engine || !obj)
    return;
  m_engine->destroyObject(obj);
}
} // namespace imvk