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

#include "plugin/device/cpu/kernel/sparse_tensor_dense_add_cpu_kernel.h"
#include <algorithm>
#include <utility>
#include <complex>
#include <functional>
#include <type_traits>
#include "plugin/device/cpu/hal/device/cpu_device_address.h"

namespace mindspore {
namespace kernel {
namespace {
constexpr size_t kIndicesShapeSize = 2;
constexpr size_t kSparseTensorDenseAddInputsNum = 4;
constexpr size_t kSparseTensorDenseAddOutputsNum = 1;
using complex64 = std::complex<float>;
using complex128 = std::complex<double>;
}  // namespace

void SparseTensorDenseAddCpuKernelMod::InitKernel(const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  kernel_name_ = common::AnfAlgo::GetCNodeName(kernel_node);
  auto indices_shape = common::AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, INDICES);
  if (indices_shape.size() != kIndicesShapeSize) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', it requires 'x1_indices' must be a " << kIndicesShapeSize
                      << "-D Tensor, but got " << indices_shape.size() << "-D";
  }
  auto values_shape = common::AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, VALUES);
  if (values_shape.size() != 1 || values_shape[0] != indices_shape[0]) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_
                      << "', it requires 'x1_values' must be a 1-D Tensor and the first dimension length "
                      << "must be equal to the first dimension length of 'indices', but got 'x1_values' shape: "
                      << Vector2Str(values_shape) << " and 'x1_indices' shape: " << Vector2Str(indices_shape);
  }
  auto shape_shape_ = common::AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, SPARSE_SHAPE);
  x2_shape_ = common::AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, DENSE);
  size_t x1_rank = shape_shape_[0];
  size_t x2_rank = x2_shape_.size();
  if (x1_rank != x2_rank) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_
                      << "', x1 and x2 must have same ranks, but got 'x1' shape: " << Vector2Str(shape_shape_)
                      << "and 'x2' shape: " << Vector2Str(x2_shape_);
  }
  values_size_ = values_shape[0];
  output_shape_ = common::AnfAlgo::GetOutputInferShape(kernel_node, 0);
  auto kernel_attr = GetKernelAttrFromNode(kernel_node);
  auto [is_match, index] = MatchKernelAttr(kernel_attr, GetOpSupport());
  if (!is_match) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_
                      << "SparseTensorDenseAdd does not support this kernel data type: " << kernel_attr;
  }
  kernel_func_ = func_list_[index].second;
}

template <typename I, typename T>
bool SparseTensorDenseAddCpuKernelMod::LaunchKernel(const std::vector<kernel::AddressPtr> &inputs,
                                                    const std::vector<kernel::AddressPtr> &outputs) {
  CHECK_KERNEL_INPUTS_NUM(inputs.size(), kSparseTensorDenseAddInputsNum, kernel_name_);
  CHECK_KERNEL_OUTPUTS_NUM(outputs.size(), kSparseTensorDenseAddOutputsNum, kernel_name_);
  if (outputs[0]->size == 0) {
    MS_LOG(WARNING) << "For '" << kernel_name_ << "', output memory size must be greater than 0, but got 0.";
    return true;
  }
  auto ret = memset_s(outputs[0]->addr, outputs[0]->size, 0, outputs[0]->size);
  if (ret != EOK) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', memset output failed. Error no: " << ret;
  }
  const auto *indices_addr = reinterpret_cast<I *>(inputs[0]->addr);
  const auto *values_addr = reinterpret_cast<T *>(inputs[1]->addr);
  const auto *shape_addr = reinterpret_cast<I *>(inputs[2]->addr);
  const auto *x2_addr = reinterpret_cast<T *>(inputs[3]->addr);
  auto *output_addr = reinterpret_cast<T *>(outputs[0]->addr);
  const size_t indices_length = inputs[0]->size / sizeof(I);
  const size_t values_length = inputs[1]->size / sizeof(T);
  const size_t x2_length = inputs[3]->size / sizeof(T);
  const size_t out_length = outputs[0]->size / sizeof(T);
  size_t rank = output_shape_.size();
  for (size_t i = 0; i < x2_length; i++) {
    if (i > out_length) {
      MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the index of 'x2' out of bounds.";
    }
    output_addr[i] = x2_addr[i];
  }
  for (size_t i = 0; i < rank; i++) {
    size_t x1_shape_i = shape_addr[i];
    size_t x2_shape_i = x2_shape_[i];
    if (x1_shape_i != x2_shape_i) {
      MS_EXCEPTION(RuntimeError) << "For '" << kernel_name_ << "', Dimension [" << i << "] does not equal"
                                 << "(no broadcasting is supported): x1_shape side " << x1_shape_i
                                 << " vs x2_shape side " << x2_shape_i << ".";
    }
  }
  for (size_t i = 0; i < values_size_; ++i) {
    if (i >= values_length) {
      MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the index of 'x1_values' out of bounds.";
    }
    size_t out_index = 0;
    for (size_t j = 0; j < rank; j++) {
      if (i * rank + j >= indices_length) {
        MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the index of 'x1_indices' out of bounds.";
      }
      int index = indices_addr[i * rank + j];
      if (index >= SizeToInt(output_shape_[j]) || index < 0) {
        MS_EXCEPTION(ValueError) << "For '" << kernel_name_ << "', the " << i << "th x1_value in " << j
                                 << "th dimension index: " << index << " of 'output' out of bounds: [0, "
                                 << output_shape_[j] << ")";
      }
      size_t count = 1;
      for (size_t k = j + 1; k < rank; k++) {
        count *= output_shape_[k];
      }
      out_index += IntToSize(index) * count;
    }
    output_addr[out_index] += values_addr[i];
  }
  return true;
}

std::vector<std::pair<KernelAttr, SparseTensorDenseAddCpuKernelMod::SparseTensorDenseAddFunc>>
  SparseTensorDenseAddCpuKernelMod::func_list_ = {
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt8)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt8)
       .AddOutputAttr(kNumberTypeInt8),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int32_t, int8_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt16)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt16)
       .AddOutputAttr(kNumberTypeInt16),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int32_t, int16_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt32)
       .AddOutputAttr(kNumberTypeInt32),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int32_t, int32_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt64)
       .AddOutputAttr(kNumberTypeInt64),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int32_t, int64_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeUInt8)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeUInt8)
       .AddOutputAttr(kNumberTypeUInt8),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int32_t, uint8_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeUInt16)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeUInt16)
       .AddOutputAttr(kNumberTypeUInt16),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int32_t, uint16_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeFloat16)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeFloat16)
       .AddOutputAttr(kNumberTypeFloat16),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int32_t, float16>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeFloat32)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeFloat32)
       .AddOutputAttr(kNumberTypeFloat32),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int32_t, float>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeFloat64)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeFloat64)
       .AddOutputAttr(kNumberTypeFloat64),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int32_t, double>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeComplex64)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeComplex64)
       .AddOutputAttr(kNumberTypeComplex64),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int32_t, complex64>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeComplex128)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeComplex128)
       .AddOutputAttr(kNumberTypeComplex128),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int32_t, complex128>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt8)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt8)
       .AddOutputAttr(kNumberTypeInt8),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int64_t, int8_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt16)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt16)
       .AddOutputAttr(kNumberTypeInt16),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int64_t, int16_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt32)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt32)
       .AddOutputAttr(kNumberTypeInt32),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int64_t, int32_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeInt64)
       .AddOutputAttr(kNumberTypeInt64),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int64_t, int64_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeUInt8)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeUInt8)
       .AddOutputAttr(kNumberTypeUInt8),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int64_t, uint8_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeUInt16)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeUInt16)
       .AddOutputAttr(kNumberTypeUInt16),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int64_t, uint16_t>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeFloat16)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeFloat16)
       .AddOutputAttr(kNumberTypeFloat16),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int64_t, float16>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeFloat32)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeFloat32)
       .AddOutputAttr(kNumberTypeFloat32),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int64_t, float>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeFloat64)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeFloat64)
       .AddOutputAttr(kNumberTypeFloat64),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int64_t, double>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeComplex64)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeComplex64)
       .AddOutputAttr(kNumberTypeComplex64),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int64_t, complex64>},
    {KernelAttr()
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeComplex128)
       .AddInputAttr(kNumberTypeInt64)
       .AddInputAttr(kNumberTypeComplex128)
       .AddOutputAttr(kNumberTypeComplex128),
     &SparseTensorDenseAddCpuKernelMod::LaunchKernel<int64_t, complex128>}};

std::vector<KernelAttr> SparseTensorDenseAddCpuKernelMod::GetOpSupport() {
  std::vector<KernelAttr> support_list;
  (void)std::transform(func_list_.begin(), func_list_.end(), std::back_inserter(support_list),
                       [](const std::pair<KernelAttr, SparseTensorDenseAddFunc> &pair) { return pair.first; });
  return support_list;
}

MS_KERNEL_FACTORY_REG(NativeCpuKernelMod, SparseTensorDenseAdd, SparseTensorDenseAddCpuKernelMod);
}  // namespace kernel
}  // namespace mindspore
