/**
 * Copyright 2022 Huawei Technologies Co., Ltd
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

#include "plugin/device/cpu/kernel/col2im_cpu_kernel.h"
#include <algorithm>
#include <functional>
#include <string>
#include "plugin/device/cpu/kernel/eigen/eigen_common_utils.h"

namespace mindspore {
namespace kernel {
namespace {
constexpr size_t kCol2ImInputsNum = 2;
constexpr size_t kCol2ImOutputsNum = 1;
constexpr int64_t kInt64Number2 = 2;
}  // namespace

void Col2ImCpuKernelMod::InitKernel(const CNodePtr &kernel_node) {
  MS_EXCEPTION_IF_NULL(kernel_node);
  cnode_ptr_ = kernel_node;
  kernel_name_ = common::AnfAlgo::GetCNodeName(kernel_node);

  x_shape_ = AnfAlgo::GetInputDeviceShape(kernel_node, kIndex0);
  y_shape_ = common::AnfAlgo::GetOutputInferShape(kernel_node, kIndex0);
  y_type_ = AnfAlgo::GetOutputDeviceDataType(kernel_node, kIndex0);

  kernel_size_ = common::AnfAlgo::GetNodeAttr<std::vector<int64_t>>(kernel_node, kAttrKernelSize);
  dilation_ = common::AnfAlgo::GetNodeAttr<std::vector<int64_t>>(kernel_node, kAttrDilation);
  padding_ = common::AnfAlgo::GetNodeAttr<std::vector<int64_t>>(kernel_node, kAttrPadding);
  stride_ = common::AnfAlgo::GetNodeAttr<std::vector<int64_t>>(kernel_node, kAttrStride);
}

bool Col2ImCpuKernelMod::Launch(const std::vector<kernel::AddressPtr> &inputs, const std::vector<kernel::AddressPtr> &,
                                const std::vector<kernel::AddressPtr> &outputs) {
  CHECK_KERNEL_INPUTS_NUM(inputs.size(), kCol2ImInputsNum, kernel_name_);
  CHECK_KERNEL_OUTPUTS_NUM(outputs.size(), kCol2ImOutputsNum, kernel_name_);
  bool res = false;
  switch (y_type_) {
    case kNumberTypeFloat16: {
      res = LaunchKernel<float16>(inputs, outputs);
      break;
    }
    case kNumberTypeFloat32: {
      res = LaunchKernel<float>(inputs, outputs);
      break;
    }
    default:
      MS_LOG(EXCEPTION) << "For '" << kernel_name_ << "', the dtype of 'x' should be float16, float32, but got "
                        << TypeIdLabel(y_type_) << ".";
  }
  return res;
}

template <typename T>
bool Col2ImCpuKernelMod::LaunchKernel(const std::vector<kernel::AddressPtr> &inputs,
                                      const std::vector<kernel::AddressPtr> &outputs) {
  auto x_data_ptr = reinterpret_cast<T *>(inputs[kIndex0]->addr);
  auto output_size_ptr = reinterpret_cast<int32_t *>(inputs[kIndex1]->addr);
  auto y_data_ptr = reinterpret_cast<T *>(outputs[kIndex0]->addr);
  (void)std::fill_n(y_data_ptr, CPUKernelUtils::CalcElementNum(y_shape_), static_cast<T>(0));

  const int64_t output_height = static_cast<int64_t>(output_size_ptr[kIndex0]);
  const int64_t output_width = static_cast<int64_t>(output_size_ptr[kIndex1]);
  if (output_height != y_shape_[kIndex2] || output_width != y_shape_[kIndex3]) {
    MS_LOG(EXCEPTION) << "For '" << kernel_name_ << " input Tensor output_size = (" << output_height << ", "
                      << output_width << "), but get output device shape = (" << y_shape_[kIndex0] << ", "
                      << y_shape_[kIndex1] << ", " << y_shape_[kIndex2] << ", " << y_shape_[kIndex3] << ").";
  }

  const int64_t kernel_height = kernel_size_.front();
  MS_EXCEPTION_IF_ZERO("kernel_height", kernel_height);
  const int64_t kernel_width = kernel_size_.back();
  MS_EXCEPTION_IF_ZERO("kernel_width", kernel_width);
  const int64_t dilation_height = dilation_.front();
  MS_EXCEPTION_IF_ZERO("dilation_height", dilation_height);
  const int64_t dilation_width = dilation_.back();
  MS_EXCEPTION_IF_ZERO("dilation_width", dilation_width);
  const int64_t pad_height = padding_.front();
  const int64_t pad_width = padding_.back();
  const int64_t stride_height = stride_.front();
  MS_EXCEPTION_IF_ZERO("stride_height", stride_height);
  const int64_t stride_width = stride_.back();
  MS_EXCEPTION_IF_ZERO("stride_width", stride_width);

  const int64_t batch_size = x_shape_[kIndex0];
  const int64_t n_input_plane = x_shape_[kIndex1];

  int64_t height_col =
    (output_height + kInt64Number2 * pad_height - (dilation_height * (kernel_height - 1) + 1)) / stride_height + 1;
  int64_t width_col =
    (output_width + kInt64Number2 * pad_width - (dilation_width * (kernel_width - 1) + 1)) / stride_width + 1;

  const int64_t channels_col = n_input_plane * kernel_height * kernel_width;
  const int64_t batch_input_size = n_input_plane * kernel_height * kernel_width * height_col * width_col;
  const int64_t batch_output_size = n_input_plane * output_height * output_width;

  for (int64_t elt = 0; elt < batch_size; ++elt) {
    int64_t input_offset = batch_input_size * elt;
    int64_t output_offset = batch_output_size * elt;

    for (int64_t c_col = 0; c_col < channels_col; ++c_col) {
      int64_t w_offset = c_col % kernel_width;
      int64_t h_offset = (c_col / kernel_width) % kernel_height;
      int64_t c_im = c_col / kernel_height / kernel_width;

      for (int64_t h_col = 0; h_col < height_col; ++h_col) {
        int64_t h_im = h_col * stride_height - pad_height + h_offset * dilation_height;

        for (int64_t w_col = 0; w_col < width_col; ++w_col) {
          int64_t w_im = w_col * stride_width - pad_width + w_offset * dilation_width;

          if (h_im >= 0 && h_im < output_height && w_im >= 0 && w_im < output_width) {
            y_data_ptr[output_offset + (c_im * output_height + h_im) * output_width + w_im] +=
              x_data_ptr[input_offset + (c_col * height_col + h_col) * width_col + w_col];
          }
        }
      }
    }
  }

  return true;
}
std::vector<KernelAttr> Col2ImCpuKernelMod::GetOpSupport() {
  static std::vector<KernelAttr> support_list = {
    KernelAttr().AddInputAttr(kNumberTypeFloat16).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeFloat16),
    KernelAttr().AddInputAttr(kNumberTypeFloat32).AddInputAttr(kNumberTypeInt32).AddOutputAttr(kNumberTypeFloat32)};
  return support_list;
}
MS_KERNEL_FACTORY_REG(NativeCpuKernelMod, Col2Im, Col2ImCpuKernelMod);
}  // namespace kernel
}  // namespace mindspore
