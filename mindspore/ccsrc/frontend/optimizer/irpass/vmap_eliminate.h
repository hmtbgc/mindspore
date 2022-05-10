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

#ifndef MINDSPORE_CCSRC_FRONTEND_OPTIMIZER_IRPASS_VMAP_ELIMINATE_H_
#define MINDSPORE_CCSRC_FRONTEND_OPTIMIZER_IRPASS_VMAP_ELIMINATE_H_

#include <vector>
#include <algorithm>
#include <memory>
#include <string>

#include "frontend/optimizer/optimizer.h"
#include "frontend/optimizer/irpass.h"
#include "frontend/optimizer/anf_visitor.h"
#include "utils/ms_utils.h"
#include "utils/hash_set.h"
#include "include/common/utils/primitive_utils.h"
#include "frontend/operator/ops.h"
#include "frontend/optimizer/irpass/meta_fg_prim_eliminate.h"

namespace mindspore {
namespace opt {
namespace irpass {
// {prim::kPrimVmap, C}
class ExpandVmapPrim : public ExpandMetaFGPrim {
 public:
  ExpandVmapPrim() { prim_ = prim::kPrimVmap; }
  virtual ~ExpandVmapPrim() = default;
  bool operator()(const FuncGraphPtr &func_graph, const OptimizerPtr &optimizer) override;
};
using ExpandVmapPrimPtr = std::shared_ptr<ExpandVmapPrim>;
namespace internal {
int GetAxisSizeByAbs(const AbstractBasePtr &abs, ValuePtr *const in_axes);

FuncGraphPtr ExpandVmapFunctor(const FuncGraphPtr &vmap_fg, const pipeline::ResourceBasePtr &resource,
                               mindspore::HashSet<FuncGraphPtr> *visited_graph,
                               mindspore::HashSet<AnfNodePtr> *visited_node, int axis_size);
}  // namespace internal
}  // namespace irpass
}  // namespace opt
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_FRONTEND_OPTIMIZER_IRPASS_VMAP_ELIMINATE_H_
