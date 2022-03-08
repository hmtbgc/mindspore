/**
 * Copyright 2020-2022 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_LITE_SRC_INNER_CONTEXT_H
#define MINDSPORE_LITE_SRC_INNER_CONTEXT_H
#include <set>
#include <string>
#include <unordered_map>
#include "include/context.h"
#ifdef SERVER_INFERENCE
#include "src/runtime/dynamic_mem_allocator.h"
#else
#include "src/runtime/inner_allocator.h"
#endif
#include "thread/threadpool.h"
#include "nnacl/op_base.h"
#ifdef ENABLE_ARM
#include "src/cpu_info.h"
#endif

namespace mindspore::lite {
struct InnerContext : public Context {
 public:
  InnerContext() { InitDeviceFp16(); }

  explicit InnerContext(const Context *context);

  int Init();

  bool IsCpuFloat16Enabled() const;

  bool IsGpuFloat16Enabled() const;

#ifdef ENABLE_OPENGL_TEXTURE
  bool IsGLTextureEnabled() const;
#endif

  bool IsCpuEnabled() const;

  const CpuDeviceInfo *GetCpuDeviceInfo() const;

  bool IsGpuEnabled() const;

  bool IsNpuEnabled() const;

  bool IsProviderEnabled() const;

  std::set<std::string> GetProviders() const;

  CpuDeviceInfo GetCpuInfo() const;

  GpuDeviceInfo GetGpuInfo() const;

  NpuDeviceInfo GetNpuInfo() const;

  int IsValid() const;

  ThreadPool *thread_pool() const;

  virtual ~InnerContext();

  bool device_and_pkg_support_fp16() const;

  std::set<void *> GetLinkInfo(void *pre) const;

  std::unordered_map<void *, std::set<void *>> GetAllLinkInfo() const;

  void SetLinkInfo(void *pre, void *suc);

  void SetAllLinkInfo(const std::unordered_map<void *, std::set<void *>> &all_link_info);

  void ReplaceLinkInfoReceiverWithNewOne(void *new_receiver, void *old_receiver);

  void ReplaceLinkInfoSenderWithNewOne(void *new_sender, void *old_sender);

#ifdef SERVER_INFERENCE
  /// \brief Set NUMA node id.
  ///
  /// \param[in] node Define the NUMA node id.
  inline void SetNodeId(int node_id) { node_id_ = node_id; }
#endif

 private:
  bool IsAllDeviceTypeValid() const;

  bool IsCpuBindModeInvalid() const;

  bool IsUserSetCpu() const;

  bool IsUserSetGpu() const;

  bool IsUserSetNpu() const;

  void SetContextDevice(const Context *context);

  void InitDeviceFp16();

  bool device_and_pkg_support_fp16_ = false;

#ifdef SERVER_INFERENCE
  int node_id_ = -1;
#endif

  ThreadPool *thread_pool_{nullptr};

  // key is the precursor tensor's pointer, value is the group of successors' pointer.
  std::unordered_map<void *, std::set<void *>> link_info_{};
};

#ifdef SERVER_INFERENCE
struct DtCostModel {
  static float unit_cost(const ThreadCostContext *dt_cost_context) {
    return per_unit_load_cost_ * dt_cost_context->per_unit_load_num_ +
           per_unit_store_cost_ * dt_cost_context->per_unit_store_num_ +
           dt_cost_context->per_unit_compute_cost_ * per_unit_compute_num_;
  }

  static float total_cost(const ThreadCostContext *dt_cost_context) {
    return dt_cost_context->total_unit_num_ * unit_cost(dt_cost_context);
  }

  // thread_num assesses parallel thread num. Value of 1.0 means ideal parallel task size. Values < 1.0 mean that task
  // granularity needs to be increased to mitigate parallelization overheads.
  static float parallel_degree(const ThreadCostContext *dt_cost_context) {
    return total_cost(dt_cost_context) / parallel_thread_cost_;
  }

  static int thread_num(const ThreadCostContext *dt_cost_context) {
    return MSMAX(1, static_cast<int>((total_cost(dt_cost_context) - thread_startup_cost_) / single_thread_cost_ + 0.9));
  }

  static int64_t thread_block_size(const ThreadCostContext *dt_cost_context) {
    return static_cast<int64_t>(parallel_thread_cost_ / unit_cost(dt_cost_context));
  }
  static int get_optimal_thread_num(const ThreadCostContext *dt_cost_context, const int thread_num);

  static float per_unit_load_cost_;      // per unit load cost
  static float per_unit_store_cost_;     // per unit store cost
  static int64_t per_unit_compute_num_;  // per unit compute num

  static float thread_startup_cost_;   // thread startup inherent cost
  static float single_thread_cost_;    // Minimum cost of single-threaded
  static float parallel_thread_cost_;  // Minimum cost of per thread in parallel-thread
};

int UpdateThreadNum(const Context *context, const ThreadCostContext *dt_cost_context, int task_num);
#endif

int ParallelLaunch(const Context *context, const Func &func, Content content, int task_num);
}  // namespace mindspore::lite

#endif  // MINDSPORE_LITE_SRC_INNER_CONTEXT_H
