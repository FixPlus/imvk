#pragma once

#include "imvk/graph/Node.hpp"
#include <unordered_set>
#include <vector>

namespace imvk::graph {

template <typename T> struct PolyHash {
  std::size_t operator()(const T *t) const { return t->hash(); }
};

template <typename T> struct PolyEqual {
  bool operator()(const T *a, const T *b) const { return *a == *b; }
};

template <typename T> class Table final {
public:
  Table() = default;
  Table(Table &&another) { std::swap(m_types, another.m_types); }
  Table &operator=(Table &&another) {
    std::swap(m_types, another.m_types);
    return *this;
  }
  template <std::derived_from<T> U> const U &get(auto &&...args) {
    U tmp{std::forward<decltype(args)>(args)...};
    if (m_types.contains(&tmp))
      return *static_cast<U *>(*m_types.find(&tmp));
    auto &&[it, _] = m_types.insert(new U{std::move(tmp)});
    return *static_cast<U *>(*it);
  }
  ~Table() {
    for (auto *t : m_types)
      delete t;
  }

private:
  std::unordered_set<T *, PolyHash<T>, PolyEqual<T>> m_types;
};

class Context {
public:
  Table<Type> &types() { return m_tt; }
  Table<AttributesBase> &attributes() { return m_attrs; }

  size_t addValue() { return m_nextValueIndex++; }

private:
  Table<Type> m_tt;
  Table<AttributesBase> m_attrs;
  size_t m_nextValueIndex = 0;
};

class Workflow {
public:
  Workflow(Context &ctx) : m_ctx(&ctx) {}

  Workflow(Workflow &&) noexcept = default;
  Workflow &operator=(Workflow &&another) noexcept {
    std::swap(m_ctx, another.m_ctx);
    std::swap(m_nodes, another.m_nodes);
    std::swap(m_workflow, another.m_workflow);
    return *this;
  }

  Workflow(const Workflow &another);
  Workflow &operator=(const Workflow &another);

  auto begin() { return std::begin(m_workflow); }

  auto end() { return std::end(m_workflow); }

  auto begin() const { return std::begin(m_workflow); }

  auto end() const { return std::end(m_workflow); }

  auto iteratorTo(Node *node) { return m_workflow.iterator_to(*node); }
  auto iteratorTo(const Node *node) const {
    return m_workflow.iterator_to(*node);
  }
  auto insert(boost::intrusive::list<Node>::iterator point, Node *node) {
    m_nodes.emplace_back(node);
    return m_workflow.insert(point, *node);
  }
  void reorder(std::span<Node *const> order) {
    assert(order.size() == m_workflow.size());
    for (auto *node : order)
      m_workflow.splice(m_workflow.end(), m_workflow, iteratorTo(node));
  }
  void erase(Node *node) {
    for (auto &use : node->uses())
      use.replaceBy(nullptr);
    for (auto &result : node->results())
      result.replaceAllUsesWith(nullptr);
    m_workflow.erase(iteratorTo(node));
    std::erase_if(m_nodes,
                  [node](const auto &owned) { return owned.get() == node; });
  }
  void dump(std::ostream &os) const;
  Context &context() { return *m_ctx; }

private:
  Context *m_ctx;
  std::vector<std::unique_ptr<Node>> m_nodes;
  boost::intrusive::list<Node> m_workflow;
};

std::optional<VerifyError> verifyWorkflow(const Workflow &wf);

inline std::ostream &operator<<(std::ostream &os, const Workflow &flow) {
  flow.dump(os);
  return os;
}

class WorkflowBuilder {
public:
  WorkflowBuilder(Workflow &workflow,
                  boost::intrusive::list<Node>::iterator ins)
      : m_workflow(&workflow), m_insertionPoint(ins) {}
  WorkflowBuilder(Workflow &workflow, Node &ins)
      : m_workflow(&workflow), m_insertionPoint(workflow.iteratorTo(&ins)) {}
  template <std::derived_from<Node> T> T *create(auto &&...args) {
    T *node =
        new T{m_workflow->context(), std::forward<decltype(args)>(args)...};
    m_workflow->insert(m_insertionPoint, node);
    return node;
  }

private:
  Workflow *m_workflow;
  boost::intrusive::list<Node>::iterator m_insertionPoint;
};
} // namespace imvk::graph