/**
 * Copyright 2021 Huawei Technologies Co., Ltd
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
#include "micro/coder/opcoders/nnacl/fp32/batchnorm_fp32_coder.h"
#include <string>
#include <vector>
#include "nnacl/fp32/batchnorm_fp32.h"
#include "src/ops/batch_norm.h"
#include "nnacl/op_base.h"
#include "micro/coder/opcoders/file_collector.h"
#include "micro/coder/opcoders/serializers/nnacl_serializer/nnacl_fp32_serializer.h"

using mindspore::schema::PrimitiveType_BatchNorm;

namespace mindspore::lite::micro::nnacl {

int BatchnormFP32Coder::Init() {
  auto bn_parameter = reinterpret_cast<BatchNormParameter *>(parameter_);
  auto bn_prim = reinterpret_cast<const mindspore::lite::BatchNorm *>(OperatorCoder::primitive());
  bn_parameter->epsilon_ = bn_prim->GetEpsilon();

  std::vector<int> input_shapes = input_tensor_->shape();
  if (input_shapes.empty()) {
    return RET_ERROR;
  }
  int n_dim = static_cast<int>(input_shapes.size());
  bn_parameter->channel_ = input_shapes.at(n_dim - 1);
  bn_parameter->unit_ = 1;
  for (int i = 0; i < n_dim - 1; i++) {
    bn_parameter->unit_ *= input_shapes.at(i);
  }
  bn_parameter->op_parameter_.thread_num_ = MSMIN(bn_parameter->op_parameter_.thread_num_, bn_parameter->unit_);
  return RET_OK;
}

int BatchnormFP32Coder::DoCode(CoderContext *const context) {
  // attribute
  int task_id = 0;
  auto bn_parameter = reinterpret_cast<BatchNormParameter *>(parameter_);
  if (Init() != RET_OK) {
    MS_LOG(ERROR) << "BatchnormFP32Coder Init error";
    return RET_ERROR;
  }
  MS_CHECK_TRUE(input_tensors_.size() == 3, "inputs size is not equal to three");
  Tensor *mean_tensor = input_tensors_.at(1);
  Tensor *var_tensor = input_tensors_.at(2);
  Collect(context, {"nnacl/fp32/batchnorm.h"}, {"nnacl/fp32/batchnorm.c"});
  NNaclFp32Serializer code;
  code.CodeStruct("bn_parameter", *bn_parameter);
  code.CodeFunction("BatchNorm", output_tensor_, input_tensor_, mean_tensor, var_tensor, task_id, "&bn_parameter");
  MS_LOG(INFO) << "BatchnormFP32Code has been called";
  context->AppendCode(code.str());
  return lite::RET_OK;
}

REG_OPERATOR_CODER(kAllTargets, kNumberTypeFloat32, PrimitiveType_BatchNorm, CPUOpCoderCreator<BatchnormFP32Coder>)
}  // namespace mindspore::lite::micro::nnacl
