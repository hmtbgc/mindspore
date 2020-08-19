/**
 * Copyright 2020 Huawei Technologies Co., Ltd
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

#include "src/runtime/kernel/arm/int8/deconvolution_int8.h"
#include "src/runtime/kernel/arm/nnacl/quantization/fixed_point.h"
#include "src/runtime/runtime_api.h"
#include "src/kernel_registry.h"

using mindspore::kernel::KERNEL_ARCH::kCPU;
using mindspore::lite::KernelRegistrar;
using mindspore::lite::RET_ERROR;
using mindspore::lite::RET_MEMORY_FAILED;
using mindspore::lite::RET_OK;
using mindspore::schema::PrimitiveType_DeConv2D;

namespace mindspore::kernel {
DeConvInt8CPUKernel::~DeConvInt8CPUKernel() {
  FreeTmpBuffer();
  ConvolutionBaseCPUKernel::FreeQuantParam();
}

void DeConvInt8CPUKernel::FreeTmpBuffer() {
  if (weight_ptr_ != nullptr) {
    free(weight_ptr_);
    weight_ptr_ = nullptr;
  }
  if (tmp_buffer_ != nullptr) {
    free(tmp_buffer_);
    tmp_buffer_ = nullptr;
  }
  if (input_ptr_ != nullptr) {
    free(input_ptr_);
    input_ptr_ = nullptr;
  }
  if (tmp_output_ != nullptr) {
    free(tmp_output_);
    tmp_output_ = nullptr;
  }
  if (input_sum_ != nullptr) {
    free(input_sum_);
    input_sum_ = nullptr;
  }
  return;
}

int DeConvInt8CPUKernel::ReSize() {
  FreeTmpBuffer();

  ConvolutionBaseCPUKernel::Init();
  int error_code = InitParam();
  if (error_code != RET_OK) {
    MS_LOG(ERROR) << "deconv int8 InitParam error!";
    return error_code;
  }

  error_code = InitBiasWeight();
  if (error_code != RET_OK) {
    MS_LOG(ERROR) << "deconv int8 InitBiasWeight error!";
    return error_code;
  }

  error_code = InitData();
  if (error_code != RET_OK) {
    MS_LOG(ERROR) << "deconv int8 InitData error!";
    return error_code;
  }
  return RET_OK;
}

int DeConvInt8CPUKernel::Init() {
  if (!InferShapeDone()) {
    return RET_OK;
  }

  CheckSupportOptimize();

  int error_code = ConvolutionBaseCPUKernel::SetQuantParam();
  if (error_code != RET_OK) {
    MS_LOG(ERROR) << "deconv int8 SetQuantParam error!";
    return error_code;
  }
  return ReSize();
}

void DeConvInt8CPUKernel::CheckSupportOptimize() {
  matmul_func_ = nullptr;
  support_optimize_ = false;

#ifdef ENABLE_ARM64
  /* todo */
#endif

  support_optimize_ = true;
  matmul_func_ = MatMulOptR4Int8;
}

int DeConvInt8CPUKernel::InitParam() {
  matmul_param_ = new MatMulParameter();
  matmul_param_->row_ = conv_param_->input_h_ * conv_param_->input_w_;
  matmul_param_->deep_ = conv_param_->input_channel_;
  matmul_param_->col_ = conv_param_->output_channel_ * conv_param_->kernel_h_ * conv_param_->kernel_w_;

  if (support_optimize_) {
    input_trans_func_ = RowMajor2Row16x4MajorInt8;
    size_t oc4 = UP_DIV(conv_param_->output_channel_, C4NUM);
    thread_count_ = MSMIN(op_parameter_->thread_num_, oc4);
    thread_stride_ = UP_DIV(oc4, thread_count_);
  } else {
    /*todo */
  }
  return RET_OK;
}

int DeConvInt8CPUKernel::InitBiasWeight() {
  size_t size = UP_ROUND(conv_param_->output_channel_, C4NUM) * sizeof(int32_t);
  bias_data_ = malloc(size);
  if (bias_data_ == nullptr) {
    MS_LOG(ERROR) << "deconv int8 malloc bias_data_ error!";
    return RET_ERROR;
  }
  memset(bias_data_, 0, size);
  if (in_tensors_.size() == 3) {
    memcpy(bias_data_, in_tensors_[0]->Data(), conv_param_->output_channel_ * sizeof(int32_t));
  }

  size = UP_ROUND(conv_param_->output_channel_, C4NUM) * UP_ROUND(conv_param_->input_channel_, C16NUM) *
         conv_param_->kernel_w_ * conv_param_->kernel_h_ * sizeof(int8_t);
  weight_ptr_ = reinterpret_cast<int8_t *>(malloc(size));
  if (weight_ptr_ == nullptr) {
    MS_LOG(ERROR) << "deconv int8 malloc weight_ptr_ error!";
    return RET_ERROR;
  }
  memset(weight_ptr_, static_cast<int8_t>(conv_param_->conv_quant_arg_.filter_quant_args_[0].zp_), size);
  DeConvWeightTransInt8(reinterpret_cast<int8_t *>(in_tensors_[1]->Data()), weight_ptr_, conv_param_->input_channel_,
                        conv_param_->output_channel_, conv_param_->kernel_h_ * conv_param_->kernel_w_,
                        support_optimize_);

  size = UP_ROUND(conv_param_->output_channel_, C4NUM) * conv_param_->kernel_h_ * conv_param_->kernel_w_;
  weight_sum_ = reinterpret_cast<int32_t *>(malloc(size * sizeof(int32_t)));
  if (weight_sum_ == nullptr) {
    MS_LOG(ERROR) << "deconv int8 malloc weight_sum_ error!";
    return RET_ERROR;
  }
  memset(weight_sum_, 0, size * sizeof(int32_t));
  DeConvPackWeightSum(weight_ptr_, weight_sum_, conv_param_->conv_quant_arg_.input_quant_args_[0].zp_,
                      conv_param_->conv_quant_arg_.filter_quant_args_[0].zp_, UP_ROUND(matmul_param_->deep_, C16NUM),
                      size, support_optimize_);

  return RET_OK;
}

int DeConvInt8CPUKernel::InitData() {
  int size =
    UP_ROUND(conv_param_->input_h_ * conv_param_->input_w_, C4NUM) * UP_ROUND(conv_param_->input_channel_, C16NUM);
  input_ptr_ = reinterpret_cast<int8_t *>(malloc(size * sizeof(int8_t)));
  if (input_ptr_ == nullptr) {
    return RET_MEMORY_FAILED;
  }
  memset(input_ptr_, static_cast<int8_t>(conv_param_->conv_quant_arg_.input_quant_args_[0].zp_), size * sizeof(int8_t));

  size = UP_ROUND(conv_param_->input_h_ * conv_param_->input_w_, C4NUM) *
         UP_ROUND(conv_param_->output_channel_, C4NUM) * conv_param_->kernel_w_ * conv_param_->kernel_h_;
  tmp_buffer_ = reinterpret_cast<int32_t *>(malloc(size * sizeof(int32_t)));
  if (tmp_buffer_ == nullptr) {
    return RET_MEMORY_FAILED;
  }

  size = UP_ROUND(conv_param_->output_channel_, C4NUM) * conv_param_->output_h_ * conv_param_->output_w_;
  tmp_output_ = reinterpret_cast<int32_t *>(malloc(size * sizeof(int32_t)));
  if (tmp_output_ == nullptr) {
    return RET_MEMORY_FAILED;
  }

  size = UP_ROUND(matmul_param_->row_, C4NUM);
  input_sum_ = reinterpret_cast<int32_t *>(malloc(size * sizeof(int32_t)));
  if (input_sum_ == nullptr) {
    return RET_MEMORY_FAILED;
  }

  return RET_OK;
}

int DeConvInt8Run(int task_id, LiteParallelGroupEnv *penv, void *cdata) {
  auto deconv = reinterpret_cast<DeConvInt8CPUKernel *>(cdata);
  auto error_code = deconv->DoDeconv(task_id);
  if (error_code != RET_OK) {
    MS_LOG(ERROR) << "DeConvInt8Run error task_id[" << task_id << "] error_code[" << error_code << "]";
    return RET_ERROR;
  }
  return RET_OK;
}

int DeConvInt8CPUKernel::DoDeconv(int task_id) {
  int cur_oc = MSMIN(thread_stride_, UP_DIV(conv_param_->output_channel_, C8NUM) - task_id * thread_stride_);
  int cur_oc_res = MSMIN(thread_stride_ * C4NUM, conv_param_->output_channel_ - task_id * thread_stride_ * C4NUM);
  if (cur_oc <= 0) {
    return RET_OK;
  }

  int input_plane = conv_param_->input_h_ * conv_param_->input_w_;
  int kernel_plane = conv_param_->kernel_w_ * conv_param_->kernel_h_;
  int output_plane = conv_param_->output_h_ * conv_param_->output_w_;

  DeConvInt8(input_ptr_, weight_ptr_ + task_id * thread_stride_ * C4NUM * kernel_plane * conv_param_->input_channel_,
             tmp_buffer_ + task_id * thread_stride_ * C4NUM * input_plane * kernel_plane, weight_sum_, input_sum_,
             UP_ROUND(matmul_param_->row_, C4NUM), cur_oc * C4NUM * kernel_plane,
             UP_ROUND(matmul_param_->deep_, C16NUM), conv_param_, matmul_func_);

  DeConvPostInt8(tmp_buffer_ + task_id * thread_stride_ * C4NUM * input_plane * kernel_plane,
                 reinterpret_cast<int32_t *>(bias_data_) + task_id * thread_stride_ * C4NUM,
                 tmp_output_ + task_id * thread_stride_ * C4NUM * output_plane,
                 output_ptr_ + task_id * thread_stride_ * C4NUM, cur_oc_res, conv_param_, support_optimize_);
  return RET_OK;
}

int DeConvInt8CPUKernel::Run() {
  auto ret = Prepare();
  if (ret != RET_OK) {
    MS_LOG(ERROR) << "Prepare failed.";
    return RET_ERROR;
  }
  int8_t *src_in = reinterpret_cast<int8_t *>(in_tensors_[0]->Data());
  int8_t *src_out = reinterpret_cast<int8_t *>(out_tensors_[0]->Data());

  for (int batch_index = 0; batch_index < conv_param_->input_batch_; batch_index++) {
    input_trans_func_(src_in + batch_index * matmul_param_->row_ * conv_param_->input_channel_, input_ptr_,
                      matmul_param_->row_, matmul_param_->deep_);
    output_ptr_ = src_out + batch_index * matmul_param_->col_;

    DeConvPackInputSum(input_ptr_, input_sum_, conv_param_->conv_quant_arg_.filter_quant_args_[0].zp_,
                       UP_ROUND(matmul_param_->row_, C4NUM), UP_ROUND(matmul_param_->deep_, C16NUM), support_optimize_);

    int error_code = LiteBackendParallelLaunch(DeConvInt8Run, this, thread_count_);
    if (error_code != RET_OK) {
      MS_LOG(ERROR) << "deconv int8 run error! error_code[" << error_code << "]";
      return RET_ERROR;
    }
  }

  return RET_OK;
}

kernel::LiteKernel *CpuDeConvInt8KernelCreator(const std::vector<lite::tensor::Tensor *> &inputs,
                                               const std::vector<lite::tensor::Tensor *> &outputs,
                                               OpParameter *opParameter, const lite::Context *ctx,
                                               const kernel::KernelKey &desc,
                                               const mindspore::lite::PrimitiveC *primitive) {
  MS_ASSERT(opParameter != nullptr);
  MS_ASSERT(desc.type == schema::PrimitiveType_DeConv2D);
  auto kernel = new (std::nothrow) kernel::DeConvInt8CPUKernel(opParameter, inputs, outputs, ctx, primitive);
  if (kernel == nullptr) {
    MS_LOG(ERROR) << "kernel is nullptr.";
    return nullptr;
  }
  auto ret = kernel->Init();
  if (ret != RET_OK) {
    delete kernel;
    MS_LOG(ERROR) << "Init kernel failed, name: " << opParameter->name_ << ", type: "
                  << schema::EnumNamePrimitiveType(static_cast<schema::PrimitiveType>(opParameter->type_));
    return nullptr;
  }
  return kernel;
}

REG_KERNEL(kCPU, kNumberTypeInt8, PrimitiveType_DeConv2D, CpuDeConvInt8KernelCreator)
}  // namespace mindspore::kernel
