#include "imvk/graph/Context.hpp"
#include <iostream>
#include <sstream>

#include "imvk/graph/Attributes.hpp"

namespace imvk::graph {

Workflow::Workflow(const Workflow &another) : m_ctx(another.m_ctx) {
  std::unordered_map<const Node *, Node *> nodeMap;
  auto insertionPoint = m_workflow.end();
  for (auto &&node : another.m_workflow) {
    auto clonedNode = node.clone(*m_ctx);
    nodeMap.insert({&node, clonedNode.get()});
    insert(insertionPoint, clonedNode.release());
  }
  for (auto &&node : m_workflow) {
    for (auto &&use : node.uses()) {
      auto &val = use.value();
      auto &origNode = val.node();
      auto resultNum = val.resultNum();
      use.replaceBy(&nodeMap.at(&origNode)->results()[resultNum]);
    }
  }
}

Workflow &Workflow::operator=(const Workflow &another) {
  Workflow tmp{another};

  return *this = std::move(tmp);
}

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