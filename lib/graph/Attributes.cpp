#include "imvk/graph/Attributes.hpp"
#include "imvk/graph/Context.hpp"
#include <algorithm>
#include <boost/container/small_vector.hpp>
#include <ranges>

namespace imvk::graph {

AttributesAnalysis::AttributesAnalysis(Workflow &wf) {
  for (auto &node : wf) {
    boost::container::small_vector<const AttributesBase *, 3> usesAttrs;
    std::ranges::transform(
        node.uses(), std::back_inserter(usesAttrs),
        [this](auto &val) { return &getAttributesFor(val.value()); });
    for (auto &val : node.results()) {
      m_attributeMap[&val] = node.getAttributes(wf.context(), val, usesAttrs);
    }
  }
}
void AttributesAnalysis::dump(std::ostream &os) const {
  for (auto &&[val, attrs] : m_attributeMap) {
    os << *val << " attrs:\n" << *attrs << "\n";
  }
}
} // namespace imvk::graph