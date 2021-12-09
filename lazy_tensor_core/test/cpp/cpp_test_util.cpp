#include "cpp_test_util.h"

#include <torch/csrc/lazy/backend/lowering_context.h>
#include <torch/csrc/lazy/core/internal_ops/device_data.h>
#include <torch/csrc/lazy/core/ir_dump_util.h>

#include <iostream>
#include <string>

#include "lazy_tensor_core/csrc/aten_ltc_bridge.h"
#include "lazy_tensor_core/csrc/tensor_impl.h"
#include "lazy_tensors/computation_client/sys_util.h"

namespace torch_lazy_tensors {
namespace cpp_test {
namespace {

bool IsLtcTensor(const at::Tensor& tensor) {
  return dynamic_cast<LTCTensorImpl*>(tensor.unsafeGetTensorImpl());
}

void DumpDifferences(const at::Tensor& tensor1, const at::Tensor& tensor2) {
  static bool dump_tensors =
      lazy_tensors::sys_util::GetEnvBool("XLA_TEST_DUMP_TENSORS", false);
  at::Tensor dtensor1 = tensor1;
  at::Tensor dtensor2 = tensor2;
  if (tensor1.dtype() == at::kBool) {
    dtensor1 = tensor1.toType(at::kByte);
  }
  if (tensor2.dtype() == at::kBool) {
    dtensor2 = tensor2.toType(at::kByte);
  }
  at::Tensor diff = dtensor1 - dtensor2;
  std::cerr << "Difference Tensor:\n" << diff << "\n";
  if (dump_tensors) {
    std::cerr << "Compared Tensors:\n"
              << tensor1 << "\n-vs-\n"
              << tensor2 << "\n";
  }
}

void MaybeDumpGraph(const at::Tensor& tensor) {
  static std::string dump_graph =
      lazy_tensors::sys_util::GetEnvString("XLA_TEST_DUMP_GRAPHS", "");
  if (!dump_graph.empty() && IsLtcTensor(tensor)) {
    std::string graph_str;
    if (dump_graph == "text") {
      graph_str = GetTensorTextGraph(tensor);
    } else if (dump_graph == "dot") {
      graph_str = GetTensorDotGraph(tensor);
    }
    if (!graph_str.empty()) {
      std::cerr << "\n>> Tensor Graph:\n" << graph_str << "\n\n";
    }
  }
}

std::unordered_set<std::string>* CreateIgnoredCounters() {
  std::unordered_set<std::string>* icounters =
      new std::unordered_set<std::string>();
  // Add below the counters whose name need to be ignored when doing
  // is-any-counter-changed assertins.
  icounters->insert("aten::rand");
  return icounters;
}

}  // namespace

const std::unordered_set<std::string>* GetIgnoredCounters() {
  static const std::unordered_set<std::string>* icounters =
      CreateIgnoredCounters();
  return icounters;
}

at::Tensor ToCpuTensor(const at::Tensor& tensor) {
  // tensor.to() implicitly triggers a sync if t.device=torch::kLazy.
  return tensor.to(torch::kCPU);
}

torch::Tensor CopyToDevice(const torch::Tensor& tensor,
                           const torch::Device& device) {
  return tensor.clone().to(device, /*non_blocking=*/false, /*copy=*/true);
}

bool EqualValues(at::Tensor tensor1, at::Tensor tensor2) {
  MaybeDumpGraph(tensor1);
  MaybeDumpGraph(tensor2);
  tensor1 = ToCpuTensor(tensor1);
  tensor2 = ToCpuTensor(tensor2);
  if (torch::isnan(tensor1).any().item<bool>()) {
    EXPECT_TRUE(EqualValues(torch::isnan(tensor1), torch::isnan(tensor2)));
    tensor1.nan_to_num_();
    tensor2.nan_to_num_();
  }
  if (tensor1.sizes() != tensor2.sizes() ||
      tensor1.dtype() != tensor2.dtype()) {
    std::cerr << "Different shape:\n"
              << tensor1.dtype() << " " << tensor1.sizes() << "\n-vs-\n"
              << tensor2.dtype() << " " << tensor2.sizes() << "\n";
    return false;
  }
  at::ScalarType type1 = tensor1.scalar_type();
  at::ScalarType type2 = tensor2.scalar_type();
  if (type1 != type2) {
    tensor1 = tensor1.toType(type2);
  }
  bool equal = tensor1.equal(tensor2);
  if (!equal) {
    DumpDifferences(tensor1, tensor2);
  }
  return equal;
}

bool EqualValuesNoElementTypeCheck(at::Tensor tensor1, at::Tensor tensor2) {
  MaybeDumpGraph(tensor1);
  MaybeDumpGraph(tensor2);
  tensor1 = ToCpuTensor(tensor1);
  tensor2 = ToCpuTensor(tensor2);
  if (tensor1.sizes() != tensor2.sizes()) {
    std::cerr << "Different shape:\n"
              << tensor1.dtype() << " " << tensor1.sizes() << "\n-vs-\n"
              << tensor2.dtype() << " " << tensor2.sizes() << "\n";
    return false;
  }
  at::ScalarType type1 = tensor1.scalar_type();
  at::ScalarType type2 = tensor2.scalar_type();
  if (type1 != type2) {
    tensor1 = tensor1.toType(type2);
  }
  bool equal = tensor1.equal(tensor2);
  if (!equal) {
    DumpDifferences(tensor1, tensor2);
  }
  return equal;
}

void ForEachDevice(const std::function<void(const torch::Device&)>& devfn) {
  // Currently TorchScript backend only supports one type of hardware per process,
  // which is set by env. And the ordinal is always 0 given distributed training/
  // multi-device is not supported yet.
  auto device = torch::lazy::BackendDevice();
  torch::Device torch_device = torch::lazy::backendDeviceToAtenDevice(device);
  devfn(torch_device);
}

bool CloseValues(at::Tensor tensor1, at::Tensor tensor2, double rtol,
                 double atol) {
  MaybeDumpGraph(tensor1);
  MaybeDumpGraph(tensor2);
  tensor1 = ToCpuTensor(tensor1);
  tensor2 = ToCpuTensor(tensor2);
  if (torch::isnan(tensor1).any().item<bool>()) {
    EXPECT_TRUE(EqualValues(torch::isnan(tensor1), torch::isnan(tensor2)));
    tensor1.nan_to_num_();
    tensor2.nan_to_num_();
  }
  if (tensor1.sizes() != tensor2.sizes() ||
      tensor1.dtype() != tensor2.dtype()) {
    std::cerr << "Different shape:\n"
              << tensor1.dtype() << " " << tensor1.sizes() << "\n-vs-\n"
              << tensor2.dtype() << " " << tensor2.sizes() << "\n";
    return false;
  }
  bool equal = tensor1.allclose(tensor2, rtol, atol);
  if (!equal) {
    DumpDifferences(tensor1, tensor2);
  }
  return equal;
}

std::string GetTensorTextGraph(at::Tensor tensor) {
  LazyTensor xtensor = TryGetLtcTensor(tensor);
  return torch::lazy::DumpUtil::ToText({xtensor.GetIrValue().node.get()});
}

std::string GetTensorDotGraph(at::Tensor tensor) {
  LazyTensor xtensor = TryGetLtcTensor(tensor);
  return torch::lazy::DumpUtil::ToDot({xtensor.GetIrValue().node.get()});
}

void TestBackward(
    const std::vector<torch::Tensor>& inputs, const torch::Device& device,
    const std::function<torch::Tensor(const std::vector<torch::Tensor>&)>&
        testfn,
    double rtol, double atol, int derivative_level) {
  std::vector<torch::Tensor> input_vars;
  std::vector<torch::Tensor> xinput_vars;
  std::vector<torch::Tensor> inputs_w_grad;
  std::vector<torch::Tensor> xinputs_w_grad;
  for (size_t i = 0; i < inputs.size(); ++i) {
    const torch::Tensor& input = inputs[i];
    if (input.defined()) {
      torch::Tensor oinput =
          input.clone().detach().set_requires_grad(input.requires_grad());
      input_vars.push_back(oinput);

      torch::Tensor xinput = CopyToDevice(input, device)
                                 .detach()
                                 .set_requires_grad(input.requires_grad());
      xinput_vars.push_back(xinput);
      if (input.requires_grad()) {
        inputs_w_grad.push_back(oinput);
        xinputs_w_grad.push_back(xinput);
      }
    } else {
      input_vars.emplace_back();
      xinput_vars.emplace_back();
    }
  }

  torch::Tensor output = testfn(input_vars);
  torch::Tensor xoutput = testfn(xinput_vars);
  AllClose(output, xoutput, rtol, atol);

  std::vector<torch::Tensor> outs = {output};
  std::vector<torch::Tensor> xouts = {xoutput};
  for (int d = 1; d <= derivative_level; ++d) {
    // Check grad of sum(outs) w.r.t inputs_w_grad.
    torch::Tensor sum = torch::zeros_like(outs[0]).sum();
    torch::Tensor xsum = torch::zeros_like(xouts[0]).sum();
    for (int i = 0; i < outs.size(); ++i) {
      if (outs[i].requires_grad()) {
        sum += outs[i].sum();
        xsum += xouts[i].sum();
      }
    }
    // Calculating higher order derivative requires create_graph=true
    bool create_graph = d != derivative_level;
    outs = torch::autograd::grad({sum}, inputs_w_grad, /*grad_outputs=*/{},
                                 /*retain_graph=*/c10::nullopt,
                                 /*create_graph=*/create_graph,
                                 /*allow_unused=*/true);
    xouts = torch::autograd::grad({xsum}, xinputs_w_grad, /*grad_outputs=*/{},
                                  /*retain_graph=*/c10::nullopt,
                                  /*create_graph=*/create_graph,
                                  /*allow_unused=*/true);
    for (size_t i = 0; i < outs.size(); ++i) {
      ASSERT_EQ(outs[i].defined(), xouts[i].defined());
      if (outs[i].defined()) {
        AllClose(outs[i], xouts[i], rtol, atol);
      }
    }
  }
}

}  // namespace cpp_test
}  // namespace torch_lazy_tensors
