/**
 * Copyright 2021-2022 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_EIGEN_EIGH_CPU_KERNEL_H
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_EIGEN_EIGH_CPU_KERNEL_H

#include <vector>
#include <complex>
#include <tuple>
#include "plugin/device/cpu/kernel/cpu_kernel.h"
#include "plugin/factory/ms_factory.h"

namespace mindspore {
namespace kernel {

using float_complex = std::complex<float>;
using double_complex = std::complex<double>;
class EighCpuKernelMod : public DeprecatedNativeCpuKernelMod {
 public:
  EighCpuKernelMod() = default;
  ~EighCpuKernelMod() override = default;
  void InitKernel(const CNodePtr &kernel_node) override;
  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &workspace,
              const std::vector<AddressPtr> &outputs) override {
    return kernel_func_(this, inputs, workspace, outputs);
  }
  void InitInputOutputSize(const CNodePtr &kernel_node) override { init_io_func_(this, kernel_node); }

  std::vector<KernelAttr> GetOpSupport() override;

 private:
  /**
   * this is for Symmetric matrix eigenvalues & eigenvectors, can decompress the lower/upper triangle matrix
   * @tparam T , input Type
   */
  template <typename T>
  void InitIOFunc(const CNodePtr &kernel_node);
  template <typename T>
  bool LaunchKernel(const std::vector<kernel::AddressPtr> &inputs, const std::vector<kernel::AddressPtr> &workspace,
                    const std::vector<kernel::AddressPtr> &outputs);
  using EighFunc =
    std::function<bool(EighCpuKernelMod *, const std::vector<kernel::AddressPtr> &,
                       const std::vector<kernel::AddressPtr> &, const std::vector<kernel::AddressPtr> &)>;
  using EighInitFunc = std::function<void(EighCpuKernelMod *, const CNodePtr &)>;
  static std::vector<std::tuple<KernelAttr, EighFunc, EighInitFunc>> func_list_;
  EighFunc kernel_func_;
  EighInitFunc init_io_func_;

  size_t m_{1};
  bool compute_eigen_vectors_{false};
  bool lower_{true};
  TypeId dtype_{kNumberTypeFloat32};
};
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_EIGEN_EIGH_CPU_KERNEL_H
