#pragma once

#include <gtest/gtest.h>
#include <torch/torch.h>
#include <torch/csrc/lazy/backend/backend_device.h>

#include <cmath>
#include <functional>
#include <string>
#include <unordered_set>

#include "lazy_tensor_core/csrc/debug_util.h"
#include "lazy_tensor_core/csrc/tensor.h"
#include "torch/csrc/lazy/core/ir.h"

#define XLA_CPP_TEST_ENABLED(name)                          \
  do {                                                      \
    if (!::torch_xla::DebugUtil::ExperimentEnabled(name)) { \
      GTEST_SKIP();                                         \
    }                                                       \
  } while (0)

namespace torch_lazy_tensors {
namespace cpp_test {

const std::unordered_set<std::string>* GetIgnoredCounters();

// Converts an at::Tensor(device=torch::kLazy) to at::Tensor(device=torch::kCPU)
// This at::Tensor can be torch::Tensor which is a Variable, or at::Tensor which
// know nothing about autograd. If the input tensor is already a CPU tensor, it
// will be returned. Needed because EqualValues and AllClose require CPU tensors
// on both sides.
at::Tensor ToCpuTensor(const at::Tensor& tensor);

// Helper function to copy a tensor to device.
torch::Tensor CopyToDevice(const torch::Tensor& tensor,
                           const torch::Device& device);

bool EqualValues(at::Tensor tensor1, at::Tensor tensor2);

bool EqualValuesNoElementTypeCheck(at::Tensor tensor1, at::Tensor tensor2);

bool CloseValues(at::Tensor tensor1, at::Tensor tensor2, double rtol = 1e-5,
                 double atol = 1e-8);

static inline void AllClose(at::Tensor tensor, at::Tensor xla_tensor,
                            double rtol = 1e-5, double atol = 1e-8) {
  EXPECT_TRUE(CloseValues(tensor, xla_tensor, rtol, atol));
}

static inline void AllClose(at::Tensor tensor, LazyTensor& xla_tensor,
                            double rtol = 1e-5, double atol = 1e-8) {
  EXPECT_TRUE(
      CloseValues(tensor, xla_tensor.ToTensor(/*detached=*/false), rtol, atol));
}

static inline void AllEqual(at::Tensor tensor, at::Tensor xla_tensor) {
  EXPECT_TRUE(EqualValues(tensor, xla_tensor));
}

void ForEachDevice(const std::function<void(const torch::Device&)>& devfn);

std::string GetTensorTextGraph(at::Tensor tensor);

std::string GetTensorDotGraph(at::Tensor tensor);

std::string GetTensorHloGraph(at::Tensor tensor);

void TestBackward(
    const std::vector<torch::Tensor>& inputs, const torch::Device& device,
    const std::function<torch::Tensor(const std::vector<torch::Tensor>&)>&
        testfn,
    double rtol = 1e-5, double atol = 1e-8, int derivative_level = 1);

}  // namespace cpp_test
}  // namespace torch_lazy_tensors