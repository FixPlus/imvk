#include "imvk/base/Frame.hpp"
#include "imvk/base/EngineBase.hpp"

namespace imvk {

Frame::Frame(FramedEngine &engine, unsigned id)
    : m_engine(engine), m_id(id), m_commandBuffer(engine.commandPool()) {}

vkw::BufferRecorder Frame::begin(unsigned ordinal) {
  m_ordinal = ordinal;
  m_commandBuffer.reset(0);
  return vkw::BufferRecorder{m_commandBuffer,
                             VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
}

Frame::~Frame() = default;

void intrusive_ptr_add_ref(FONodeBase *p) { p->m_refCount++; }
void intrusive_ptr_release(FONodeBase *p) {
  if (--(p->m_refCount) == 0)
    delete p;
}

void FONodeImpl<fon_type::swap>::m_objectsInit(
    FramedEngine &engine, ObjGen gen,
    boost::container::small_vector_base<FObject::Ptr> &out) {
  std::ranges::transform(engine.frameIds(), std::back_inserter(out), gen);
}

void FOExtImpl<fon_type::swap>::m_objectsInit(FramedEngine &engine,
                                              ObjGen gen) {
  m_objects.clear();
  std::ranges::transform(engine.frameIds(), std::back_inserter(m_objects), gen);
}
void FObject::Deleter::operator()(FObject *obj) const {
  if (!m_engine || !obj)
    return;
  m_engine->destroyObject(obj);
}
} // namespace imvk