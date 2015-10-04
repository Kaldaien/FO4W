/**
* This file is part of Batman "Fix".
*
* Batman "Fix" is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* The Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Batman Tweak is distributed in the hope that it will be useful,
* But WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Batman "Fix". If not, see <http://www.gnu.org/licenses/>.
**/

#ifndef __BMF__NVAPI_H__
#define __BMF__NVAPI_H__

#include "nvapi/nvapi.h"
#include <Windows.h>

#include <string>

struct DXGI_ADAPTER_DESC;

// Reverse Engineered NDA portions of NvAPI (hush, hush -- *wink*)
#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  NvU32                   version;                            //!< Structure version

  struct {
    NvU32                   pciLinkTransferRate;
    NVVIOPCILINKRATE        pciLinkRate;                      //!< specifies the the negotiated PCIE link rate.
    NVVIOPCILINKWIDTH       pciLinkWidth;                     //!< specifies the the negotiated PCIE link width.
    NVVIOPCILINKRATE        pciLinkVersion;                   //!< specifies the the negotiated PCIE link rate.
  } pstates [NVAPI_MAX_GPU_PERF_PSTATES];
} NV_GPU_PCIE_INFO_V2;

typedef NV_GPU_PCIE_INFO_V2 NV_GPU_PCIE_INFO;

#define NV_GPU_PCIE_INFO_VER_2  MAKE_NVAPI_VERSION(NV_GPU_PCIE_INFO_V2,2)
#define NV_GPU_PCIE_INFO_VER    NV_GPU_PCIE_INFO_VER_2

typedef NvAPI_Status (__cdecl *NvAPI_GPU_GetPCIEInfo_t)
    (NvPhysicalGpuHandle handle, NV_GPU_PCIE_INFO* info);
typedef NvAPI_Status (__cdecl *NvAPI_GPU_GetRamType_t)
    (NvPhysicalGpuHandle handle, NvU32* memtype);
typedef NvAPI_Status (__cdecl *NvAPI_GPU_GetFBWidthAndLocation_t)
    (NvPhysicalGpuHandle handle, NvU32* width, NvU32* loc);
typedef NvAPI_Status (__cdecl *NvAPI_GetPhysicalGPUFromGPUID_t)
    (NvU32 gpuid, NvPhysicalGpuHandle* gpu);
typedef NvAPI_Status (__cdecl *NvAPI_GetGPUIDFromPhysicalGPU_t)
    (NvPhysicalGpuHandle gpu, NvU32* gpuid);



extern NvAPI_GPU_GetRamType_t            NvAPI_GPU_GetRamType;
extern NvAPI_GPU_GetFBWidthAndLocation_t NvAPI_GPU_GetFBWidthAndLocation;
extern NvAPI_GPU_GetPCIEInfo_t           NvAPI_GPU_GetPCIEInfo;
extern NvAPI_GetPhysicalGPUFromGPUID_t   NvAPI_GetPhysicalGPUFromGPUID;
extern NvAPI_GetGPUIDFromPhysicalGPU_t   NvAPI_GetGPUIDFromPhysicalGPU;

#ifdef __cplusplus
}
#endif

namespace bmf {
namespace NVAPI {

// r353_23
#define MINIMUM_DRIVER_VERSION 35582

  BOOL InitializeLibrary (void);
  BOOL UnloadLibrary     (void);

  int  CountSLIGPUs      (void);
  int  CountPhysicalGPUs (void);
  DXGI_ADAPTER_DESC*
       EnumGPUs_DXGI     (void);
  DXGI_ADAPTER_DESC*
       EnumSLIGPUs       (void);

  NV_GET_CURRENT_SLI_STATE
       GetSLIState       (IUnknown* pDev);

  DXGI_ADAPTER_DESC*
       FindGPUByDXGIName (const wchar_t* wszName);

  std::wstring
       GetDriverVersion  (NvU32* pVer = NULL);

  // In typical NVIDIA fashion, you literally cannot
  //   run this game without updating your driver first.
  //
  //   The game doesn't ever check this, so we might as well do
  //     that too.
  bool
    CheckDriverVersion   (void);

  std::wstring
       ErrorMessage      (_NvAPI_Status err,
                          const char*   args,
                          UINT          line_no,
                          const char*   function_name,
                          const char*   file_name);

  // Guilty until proven innocent
  extern bool nv_hardware;

}
}

#endif /* __BMF__NVAPI_H__ */
