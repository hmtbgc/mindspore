/**
 * Copyright 2020-2021 Huawei Technologies Co., Ltd
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

#include "src/delegate/npu/op/activation_npu.h"
#include "src/delegate/npu/npu_converter_utils.h"
namespace mindspore {
int ActivationNPUOp::IsSupport(const schema::Primitive *primitive, const std::vector<mindspore::MSTensor> &in_tensors,
                               const std::vector<mindspore::MSTensor> &out_tensors) {
  auto act_prim = primitive->value_as_Activation();
  if (act_prim == nullptr) {
    MS_LOG(ERROR) << "Get null primitive value for op ." << name_;
    return RET_ERROR;
  }
  act_type_ = act_prim->activation_type();
  if (act_type_ != schema::ActivationType_RELU && act_type_ != schema::ActivationType_RELU6 &&
      act_type_ != schema::ActivationType_SIGMOID && act_type_ != schema::ActivationType_TANH &&
      act_type_ != schema::ActivationType_HSIGMOID && act_type_ != schema::ActivationType_LEAKY_RELU) {
    MS_LOG(WARNING) << "Unsupported activation type for activation op " << name_ << "when running npu";
    return RET_NOT_SUPPORT;
  }
  return RET_OK;
}

int ActivationNPUOp::Init(const schema::Primitive *primitive, const std::vector<mindspore::MSTensor> &in_tensors,
                          const std::vector<mindspore::MSTensor> &out_tensors) {
  act_ = new (std::nothrow) hiai::op::Activation(name_);
  if (act_ == nullptr) {
    MS_LOG(ERROR) << "New activation npu operator for activation op " << name_ << " failed.";
    return RET_ERROR;
  }
  auto act_prim = primitive->value_as_Activation();
  if (act_prim == nullptr) {
    MS_LOG(ERROR) << "Get null primitive value for op ." << name_;
    return RET_ERROR;
  }
  auto act_mode = ConverterToNPUActivationMode(act_type_);
  if (act_mode == ACTIVATION_INVALID) {
    MS_LOG(ERROR) << "Unsupported activation type for activation op " << name_ << "when running npu";
    return RET_ERROR;
  }
  act_->set_attr_mode(act_mode);

  if (act_type_ == schema::ActivationType_LEAKY_RELU) {
    act_->set_attr_negative_slope(act_prim->alpha());
  }
  return RET_OK;
}

int ActivationNPUOp::SetNPUInputs(const std::vector<mindspore::MSTensor> &in_tensors,
                                  const std::vector<mindspore::MSTensor> &out_tensors,
                                  const std::vector<ge::Operator *> &npu_inputs) {
  act_->set_input_x(*npu_inputs[0]);
  return RET_OK;
}

int ActivationNPUOp::SetNPUInputs(
  const std::vector<mindspore::MSTensor> &in_tensors, const std::vector<mindspore::MSTensor> &out_tensors,
  const std::vector<ge::Operator *> &npu_inputs,
  const std::unordered_map<int, std::pair<ge::Operator *, int>> &index2_multi_out_index) {
  if (!index2_multi_out_index.empty()) {
    auto itr = index2_multi_out_index.begin();
    auto in_op = itr->second.first;
    MS_CHECK_TRUE_RET(in_op != nullptr, RET_ERROR);
    act_->SetInput(itr->first, *in_op, itr->second.second);
  } else {
    act_->set_input_x(*npu_inputs[0]);
  }
  return RET_OK;
}

ge::Operator *ActivationNPUOp::GetNPUOp() { return act_; }

ActivationNPUOp::~ActivationNPUOp() {
  if (act_ != nullptr) {
    delete act_;
    act_ = nullptr;
  }
}
}  // namespace mindspore
