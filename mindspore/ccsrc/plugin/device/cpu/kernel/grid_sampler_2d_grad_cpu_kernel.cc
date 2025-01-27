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
#include "plugin/device/cpu/kernel/grid_sampler_2d_grad_cpu_kernel.h"
#include "plugin/device/cpu/hal/device/cpu_device_address.h"

namespace {
const size_t kDataSizeThreshold = 64 * 1024;
const size_t kZero = 0;
const size_t kOne = 1;
const size_t kTwo = 2;
const size_t kThree = 3;
const size_t kFour = 4;
}  // namespace

namespace mindspore {
namespace kernel {
void GridSampler2DGradCpuKernelMod::InitKernel(const CNodePtr &kernel_node) {
  grad_shape_ = AnfAlgo::GetInputDeviceShape(kernel_node, kZero);
  x_shape_ = AnfAlgo::GetInputDeviceShape(kernel_node, kOne);
  grid_shape_ = AnfAlgo::GetInputDeviceShape(kernel_node, kTwo);
  dx_shape_ = AnfAlgo::GetOutputDeviceShape(kernel_node, kZero);
  dgrid_shape_ = AnfAlgo::GetOutputDeviceShape(kernel_node, kOne);
  if (AnfAlgo::IsShapesDynamic({x_shape_, grad_shape_, grid_shape_, dx_shape_, dgrid_shape_})) {
    return;
  }
  dx_size_ = LongToSize(dx_shape_[kZero] * dx_shape_[kOne] * dx_shape_[kTwo] * dx_shape_[kThree]);
  grid_size_ = LongToSize(grid_shape_[kZero] * grid_shape_[kOne] * grid_shape_[kTwo] * grid_shape_[kThree]);
  dtype_ = AnfAlgo::GetInputDeviceDataType(kernel_node, kZero);
  interpolation_mode_ = common::AnfAlgo::GetNodeAttr<std::string>(kernel_node, "interpolation_mode");
  padding_mode_ = common::AnfAlgo::GetNodeAttr<std::string>(kernel_node, "padding_mode");
  align_corners_ = common::AnfAlgo::GetNodeAttr<bool>(kernel_node, "align_corners");
}

bool GridSampler2DGradCpuKernelMod::Launch(const std::vector<kernel::AddressPtr> &inputs,
                                           const std::vector<kernel::AddressPtr> &,
                                           const std::vector<kernel::AddressPtr> &outputs) {
  if (dtype_ == kNumberTypeFloat16) {
    LaunchKernel<float16>(inputs, outputs);
  } else if (dtype_ == kNumberTypeFloat32) {
    LaunchKernel<float>(inputs, outputs);
  } else if (dtype_ == kNumberTypeFloat64) {
    LaunchKernel<double>(inputs, outputs);
  } else {
    MS_EXCEPTION(TypeError) << "Input dtype only support float16, float32, float64!, but got" << dtype_;
  }
  return true;
}

template <typename T>
void GridSampler2DGradCpuKernelMod::ComputeTask(const std::vector<AddressPtr> &inputs,
                                                const std::vector<AddressPtr> &outputs) {
  GridSamplerInterpolation interp = GridSamplerInterpolation::Bilinear;
  GridSamplerPadding padding;
  bool align_corners = align_corners_;

  auto grad_data_addr = static_cast<T *>(inputs[kZero]->addr);
  auto x_data_addr = static_cast<T *>(inputs[kOne]->addr);
  auto grid_data_addr = static_cast<T *>(inputs[kTwo]->addr);
  auto dx_data_addr = static_cast<T *>(outputs[kZero]->addr);
  auto dgrid_data_addr = static_cast<T *>(outputs[kOne]->addr);
  if (interpolation_mode_ == "bilinear") {
    interp = GridSamplerInterpolation::Bilinear;
  } else if (interpolation_mode_ == "nearest") {
    interp = GridSamplerInterpolation::Nearest;
  }
  if (padding_mode_ == "zeros") {
    padding = GridSamplerPadding::Zeros;
  } else if (padding_mode_ == "border") {
    padding = GridSamplerPadding::Border;
  } else {
    padding = GridSamplerPadding::Reflection;
  }

  int64_t N = x_shape_[kZero];

#define VARDEF                                                                                                       \
  for (int64_t n = 0; n < N; ++n) {                                                                                  \
    auto DXSlice = DXAcc[n];                                                                                         \
    auto DGridSlice = DGridAcc[n];                                                                                   \
    auto GradSlice = GradAcc[n];                                                                                     \
    auto XSlice = XAcc[n];                                                                                           \
    GridSampler2DGridSliceIterator(GridAcc[n], [&](const vec256::Vec256<T> &grid_x, const vec256::Vec256<T> &grid_y, \
                                                   int64_t spatial_offset, int64_t len) {                            \
      grid_sample.Backward(&DXSlice, &DGridSlice, GradSlice, XSlice, spatial_offset, grid_x, grid_y, len);           \
    });                                                                                                              \
  }
#define PROCESS_CASE(interp, padding, align_corners)                                \
  do {                                                                              \
    case padding: {                                                                 \
      ApplyGridSample2D<T, kTwo, interp, padding, align_corners> grid_sample(XAcc); \
      VARDEF;                                                                       \
    }                                                                               \
  } while (0);

#define PROCESS_INTERP(interp, align_corners)                                \
  do {                                                                       \
    case interp: {                                                           \
      switch (static_cast<GridSamplerPadding>(padding)) {                    \
        PROCESS_CASE(interp, GridSamplerPadding::Zeros, align_corners);      \
        PROCESS_CASE(interp, GridSamplerPadding::Border, align_corners);     \
        PROCESS_CASE(interp, GridSamplerPadding::Reflection, align_corners); \
      }                                                                      \
    }                                                                        \
  } while (0);

  auto DXAcc = accessor<T, 4>(dx_data_addr, dx_shape_);
  auto DGridAcc = accessor<T, 4>(dgrid_data_addr, dgrid_shape_);
  auto XAcc = accessor<T, 4>(x_data_addr, x_shape_);
  auto GridAcc = accessor<T, 4>(grid_data_addr, grid_shape_);
  auto GradAcc = accessor<T, 4>(grad_data_addr, grad_shape_);
  if (align_corners) {
    switch (static_cast<GridSamplerInterpolation>(interp)) {
      PROCESS_INTERP(GridSamplerInterpolation::Bilinear, true);
      PROCESS_INTERP(GridSamplerInterpolation::Nearest, true);
    }
  } else {
    switch (static_cast<GridSamplerInterpolation>(interp)) {
      PROCESS_INTERP(GridSamplerInterpolation::Bilinear, false);
      PROCESS_INTERP(GridSamplerInterpolation::Nearest, false);
    }
  }

#undef PROCESS_CASE
#undef PROCESS_INTERP
}

template <typename T>
void GridSampler2DGradCpuKernelMod::LaunchKernel(const std::vector<AddressPtr> &inputs,
                                                 const std::vector<AddressPtr> &outputs) {
  auto dx_data_addr = static_cast<T *>(outputs[kZero]->addr);
  auto dgrid_data_addr = static_cast<T *>(outputs[kOne]->addr);
  for (size_t i = kZero; i < dx_size_; i++) {
    dx_data_addr[i] = static_cast<T>(kZero);
  }
  for (size_t i = kZero; i < grid_size_; i++) {
    dgrid_data_addr[i] = static_cast<T>(kZero);
  }
  if (dtype_ == kNumberTypeFloat16) {
    //
  } else if (dtype_ == kNumberTypeFloat32) {
    ComputeTask<float>(inputs, outputs);
  } else if (dtype_ == kNumberTypeFloat64) {
    ComputeTask<double>(inputs, outputs);
  }
}

MS_KERNEL_FACTORY_REG(NativeCpuKernelMod, GridSampler2DGrad, GridSampler2DGradCpuKernelMod);
}  // namespace kernel
}  // namespace mindspore
