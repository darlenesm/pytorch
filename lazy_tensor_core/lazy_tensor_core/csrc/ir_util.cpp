#include "lazy_tensor_core/csrc/ir_util.h"

#include <c10/util/Logging.h>

namespace torch_lazy_tensors {
namespace ir {

std::vector<torch::lazy::Node*> Util::ComputePostOrder(
    const torch::lazy::Node* node, EmissionMap* emap) {
  std::vector<torch::lazy::Node*> post_order;
  std::vector<torch::lazy::Node*> queue;
  // std::vector<const T> to c10::ArrayRef<T> conversion is not supported,
  // so we need to drop const in the return vector and use const_cast here.
  queue.push_back(const_cast<torch::lazy::Node*>(node));
  while (!queue.empty()) {
    node = queue.back();
    auto it = emap->find(node);
    if (it == emap->end()) {
      (*emap)[node] = kEmitting;
      for (auto& output : node->operands()) {
        auto oit = emap->find(output.node);
        if (oit == emap->end()) {
          queue.push_back(const_cast<torch::lazy::Node*>(output.node));
        } else if (oit->second == kEmitting) {
          LOG(ERROR) << "Graph loop found at " << *output.node;
        }
      }
    } else if (it->second == kEmitting) {
      for (auto& output : node->operands()) {
        auto oit = emap->find(output.node);
        CHECK(oit != emap->end() && oit->second == kEmitted)
            << "Graph loop found at " << *output.node;
      }
      (*emap)[node] = kEmitted;
      post_order.push_back(const_cast<torch::lazy::Node*>(node));
      queue.pop_back();
    } else {
      CHECK_EQ(it->second, kEmitted);
      queue.pop_back();
    }
  }
  return post_order;
}

std::vector<torch::lazy::Node*> Util::ComputePostOrder(
    c10::ArrayRef<torch::lazy::Node*> nodes, EmissionMap* emap) {
  std::vector<torch::lazy::Node*> post_order;
  for (auto node : nodes) {
    auto node_post_order = ComputePostOrder(node, emap);
    post_order.insert(post_order.end(), node_post_order.begin(),
                      node_post_order.end());
  }
  return post_order;
}

std::vector<torch::lazy::Node*> Util::ComputePostOrder(
    c10::ArrayRef<torch::lazy::Node*> nodes) {
  EmissionMap emap;
  return ComputePostOrder(nodes, &emap);
}

size_t Util::GetGraphSize(c10::ArrayRef<torch::lazy::Node*> nodes) {
  return ComputePostOrder(nodes).size();
}

}  // namespace ir
}  // namespace torch_lazy_tensors