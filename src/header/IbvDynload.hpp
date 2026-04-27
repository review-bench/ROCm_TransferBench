/*
Copyright (c) Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/// @file IbvDynload.hpp
/// @brief libibverbs function pointers and optional dlopen/dlsym when not IBV_DIRECT.
/// @note Include when `NIC_EXEC_ENABLED` is defined (e.g. from `TransferBench.hpp` alongside other headers).

#pragma once

#include <dlfcn.h>
#include <infiniband/verbs.h>
#include <mutex>

#if IBV_DIRECT
#define IBV_FN(name, rettype, arglist) constexpr rettype(*pfn_##name)arglist = name;
#else
#define IBV_FN(name, rettype, arglist) rettype(*pfn_##name)arglist = nullptr;
#endif

namespace {

IBV_FN(ibv_alloc_pd, ibv_pd*, (ibv_context*))
IBV_FN(ibv_close_device, int, (ibv_context*))
IBV_FN(ibv_create_cq, ibv_cq*, (ibv_context*, int, void*, ibv_comp_channel*, int))
IBV_FN(ibv_create_qp, ibv_qp*, (ibv_pd*, ibv_qp_init_attr*))
IBV_FN(ibv_dealloc_pd, int, (ibv_pd*))
IBV_FN(ibv_dereg_mr, int, (ibv_mr*))
IBV_FN(ibv_destroy_cq, int, (ibv_cq*))
IBV_FN(ibv_destroy_qp, int, (ibv_qp*))
IBV_FN(ibv_free_device_list, void, (ibv_device**))
IBV_FN(ibv_get_device_list, ibv_device**, (int*))
IBV_FN(ibv_get_device_name, const char*, (ibv_device*))
IBV_FN(ibv_modify_qp, int, (ibv_qp*, ibv_qp_attr*, int))
IBV_FN(ibv_open_device, ibv_context*, (ibv_device*))
IBV_FN(ibv_poll_cq, int, (ibv_cq*, int, ibv_wc*))
IBV_FN(ibv_post_send, int, (ibv_qp*, ibv_send_wr*, ibv_send_wr**))
IBV_FN(ibv_query_device, int, (ibv_context*, ibv_device_attr*))
IBV_FN(ibv_query_gid, int, (ibv_context*, uint8_t, int, ibv_gid*))
#if IBV_DIRECT
// On older versions of libibverbs, ibv_query_port is not defined in the header file.
constexpr int (*pfn_ibv_query_port)(ibv_context*, uint8_t, ibv_port_attr*) = ___ibv_query_port;
#else
IBV_FN(ibv_query_port, int, (ibv_context*, uint8_t, ibv_port_attr*))
#endif
#ifdef HAVE_DMABUF_SUPPORT
IBV_FN(ibv_reg_dmabuf_mr, ibv_mr*, (ibv_pd*, uint64_t, size_t, uint64_t, int, int))
#endif
IBV_FN(ibv_reg_mr, ibv_mr*, (ibv_pd*, void*, size_t, int))

} // namespace

#if IBV_DIRECT

inline void TbIbvEnsureLoaded() {}
inline bool TbIbvSymbolsReady() { return true; }
inline void* TbIbvDlHandle() { return nullptr; }
inline void TbIbvUnload() {}

#else

struct IbvDynloadState {
  std::once_flag once{};
  void* handle = nullptr;
  bool loaded = false;

  void tryLoad()
  {
    handle = dlopen("libibverbs.so.1", RTLD_NOW);
    if (handle == nullptr)
      return;

    struct Symbol { void **ppfn; char const *name; };

    Symbol symbols[] = {
        {(void**)&pfn_ibv_alloc_pd, "ibv_alloc_pd"},
        {(void**)&pfn_ibv_close_device, "ibv_close_device"},
        {(void**)&pfn_ibv_create_cq, "ibv_create_cq"},
        {(void**)&pfn_ibv_create_qp, "ibv_create_qp"},
        {(void**)&pfn_ibv_dealloc_pd, "ibv_dealloc_pd"},
        {(void**)&pfn_ibv_dereg_mr, "ibv_dereg_mr"},
        {(void**)&pfn_ibv_destroy_cq, "ibv_destroy_cq"},
        {(void**)&pfn_ibv_destroy_qp, "ibv_destroy_qp"},
        {(void**)&pfn_ibv_free_device_list, "ibv_free_device_list"},
        {(void**)&pfn_ibv_get_device_list, "ibv_get_device_list"},
        {(void**)&pfn_ibv_get_device_name, "ibv_get_device_name"},
        {(void**)&pfn_ibv_modify_qp, "ibv_modify_qp"},
        {(void**)&pfn_ibv_open_device, "ibv_open_device"},
        {(void**)&pfn_ibv_poll_cq, "ibv_poll_cq"},
        {(void**)&pfn_ibv_post_send, "ibv_post_send"},
        {(void**)&pfn_ibv_query_device, "ibv_query_device"},
        {(void**)&pfn_ibv_query_gid, "ibv_query_gid"},
        {(void**)&pfn_ibv_query_port, "ibv_query_port"},
#ifdef HAVE_DMABUF_SUPPORT
        {(void**)&pfn_ibv_reg_dmabuf_mr, "ibv_reg_dmabuf_mr"},
#endif
        {(void**)&pfn_ibv_reg_mr, "ibv_reg_mr"},
    };

    for (Symbol const& s : symbols) {
      void* sym = dlsym(handle, s.name);
      if (sym == nullptr) {
        dlclose(handle);
        handle = nullptr;
        return;
      }
      *s.ppfn = sym;
    }
    loaded = true;
  }
};

inline IbvDynloadState& ibvDynloadState()
{
  static IbvDynloadState s;
  return s;
}

inline void TbIbvEnsureLoaded()
{
  IbvDynloadState& st = ibvDynloadState();
  std::call_once(st.once, [&]() { st.tryLoad(); });
}

inline bool TbIbvSymbolsReady()
{
  TbIbvEnsureLoaded();
  return ibvDynloadState().loaded;
}

inline void* TbIbvDlHandle()
{
  TbIbvEnsureLoaded();
  return ibvDynloadState().handle;
}

inline void TbIbvUnload()
{
  IbvDynloadState& st = ibvDynloadState();
  if (st.handle != nullptr) {
    dlclose(st.handle);
    st.handle = nullptr;
    st.loaded = false;
    pfn_ibv_alloc_pd = nullptr;
    pfn_ibv_close_device = nullptr;
    pfn_ibv_create_cq = nullptr;
    pfn_ibv_create_qp = nullptr;
    pfn_ibv_dealloc_pd = nullptr;
    pfn_ibv_dereg_mr = nullptr;
    pfn_ibv_destroy_cq = nullptr;
    pfn_ibv_destroy_qp = nullptr;
    pfn_ibv_free_device_list = nullptr;
    pfn_ibv_get_device_list = nullptr;
    pfn_ibv_get_device_name = nullptr;
    pfn_ibv_modify_qp = nullptr;
    pfn_ibv_open_device = nullptr;
    pfn_ibv_poll_cq = nullptr;
    pfn_ibv_post_send = nullptr;
    pfn_ibv_query_device = nullptr;
    pfn_ibv_query_gid = nullptr;
    pfn_ibv_query_port = nullptr;
#ifdef HAVE_DMABUF_SUPPORT
    pfn_ibv_reg_dmabuf_mr = nullptr;
#endif
    pfn_ibv_reg_mr = nullptr;
  }
}

#endif // !IBV_DIRECT