#include "imvk/graph/Context.hpp"
#include <iostream>
#include <sstream>

#include "imvk/graph/Attributes.hpp"

namespace imvk::graph {
void Workflow::dump(std::ostream &os) const {
  auto dumpValue = [&](std::ostream &os, const Value &val, bool dumpType) {
    os << val;
    if (dumpType)
      os << " -> " << val.type();
  };
  for (auto &node : m_workflow) {
    auto res = node.results();
    if (!std::ranges::empty(res)) {

      for (const Value &use :
           std::ranges::subrange{std::begin(res), std::prev(std::end(res))}) {
        dumpValue(os, use, true);
        os << ", ";
      }
      dumpValue(os, res.back(), true);
      os << " = ";
    }
    os << node.name() << " ";
    auto uses = node.uses();
    if (!std::ranges::empty(uses)) {
      for (const Use &use :
           std::ranges::subrange{std::begin(uses), std::prev(std::end(uses))}) {
        dumpValue(os, use.value(), false);
        os << ", ";
      }
      dumpValue(os, std::prev(std::end(uses))->value(), false);
    }
    std::stringstream ss;
    node.dumpAttributes(ss);
    if (!ss.str().empty()) {
      os << " { " << ss.str() << " }";
    }
    os << "\n";
  }
}

Value::Value(Context &ctx, Node *node, const Type *type, DefInfo *info)
    : m_node(node), m_type(type), m_info(info), m_index(ctx.addValue()) {}
std::ostream &operator<<(std::ostream &os, const Value &v) {
  return os << "%" << v.index();
}
} // namespace imvk::graph