/**
* This file is part of Batman "Fix".
*
* Batman Tweak is free software : you can redistribute it and / or modify
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
* along with Batman Tweak.If not, see <http://www.gnu.org/licenses/>.
**/

#define _CRT_NON_CONFORMING_SWPRINTFS
#define _CRT_SECURE_NO_WARNINGS

#include <d3d11.h>

#include "nvapi.h"
#include "nvapi/NvApiDriverSettings.h"
#include "nvapi/nvapi_lite_sli.h"

#include <Windows.h>
#include <dxgi.h>
#include <string>

//
// Undocumented Functions
//
//  ** (I am not breaking any NDA; I found these the hard way!)
//
NvAPI_GPU_GetRamType_t            NvAPI_GPU_GetRamType;
NvAPI_GPU_GetFBWidthAndLocation_t NvAPI_GPU_GetFBWidthAndLocation;
NvAPI_GPU_GetPCIEInfo_t           NvAPI_GPU_GetPCIEInfo;
NvAPI_GetPhysicalGPUFromGPUID_t   NvAPI_GetPhysicalGPUFromGPUID;
NvAPI_GetGPUIDFromPhysicalGPU_t   NvAPI_GetGPUIDFromPhysicalGPU;

#pragma comment (lib, "nvapi/amd64/nvapi64.lib")

using namespace bmf;
using namespace bmf::NVAPI;

static bool nvapi_silent = false;

#define NVAPI_SILENT()  { nvapi_silent = true;  }
#define NVAPI_VERBOSE() { nvapi_silent = false; }

#if 0
#define NVAPI_CALL(x) { NvAPI_Status ret = NvAPI_##x;        \
                        if (nvapi_silent != true &&          \
                                     ret != NVAPI_OK)        \
                          MessageBox (                       \
                            NVAPI_ErrorMessage (             \
                              ret, #x, __LINE__,             \
                              __FUNCTION__, __FILE__),       \
                            L"Error Calling NVAPI Function", \
                            MB_OK | MB_ICONASTERISK );       \
                      }
#else
#define NVAPI_CALL(x) { NvAPI_Status ret = NvAPI_##x; if (nvapi_silent !=    \
 true && ret != NVAPI_OK) MessageBox (NULL, ErrorMessage (ret, #x, __LINE__, \
__FUNCTION__, __FILE__).c_str (), L"Error Calling NVAPI Function", MB_OK     \
 | MB_ICONASTERISK ); }

#define NVAPI_CALL2(x,y) { ##y = NvAPI_##x; if (nvapi_silent != true &&     \
##y != NVAPI_OK) MessageBox (                                               \
  NULL, ErrorMessage (##y, #x, __LINE__, __FUNCTION__, __FILE__).c_str (),  \
L"Error Calling NVAPI Function", MB_OK | MB_ICONASTERISK); }

#endif

#define NVAPI_SET_DWORD(x,y,z) (x).version = NVDRS_SETTING_VER;       \
                               (x).settingId = (y); (x).settingType = \
                                 NVDRS_DWORD_TYPE;                    \
                               (x).u32CurrentValue = (z);


std::wstring
NVAPI::ErrorMessage (_NvAPI_Status err,
                     const char*   args,
                     UINT          line_no,
                     const char*   function_name,
                     const char*   file_name)
{
  char szError [64];

  NvAPI_GetErrorMessage (err, szError);

  wchar_t wszError          [64];
  wchar_t wszFile           [256];
  wchar_t wszFunction       [256];
  wchar_t wszArgs           [256];
  wchar_t wszFormattedError [1024];

  MultiByteToWideChar (CP_OEMCP, 0, szError,       -1, wszError,     64);
  MultiByteToWideChar (CP_OEMCP, 0, file_name,     -1, wszFile,     256);
  MultiByteToWideChar (CP_OEMCP, 0, function_name, -1, wszFunction, 256);
  MultiByteToWideChar (CP_OEMCP, 0, args,          -1, wszArgs,     256);
  *wszFormattedError = L'\0';

  swprintf ( wszFormattedError, 1024,
              L"Line %u of %s (in %s (...)):\n"
              L"------------------------\n\n"
              L"NvAPI_%s\n\n\t>> %s <<",
               line_no,
                wszFile,
                 wszFunction,
                  wszArgs,
                   wszError );

  return wszFormattedError;
}

int
bmf::NVAPI::CountPhysicalGPUs (void)
{
  static int nv_gpu_count = -1;

  if (nv_gpu_count == -1) {
    if (nv_hardware) {
      NvPhysicalGpuHandle gpus [64];
      NvU32               gpu_count = 0;

      NVAPI_CALL (EnumPhysicalGPUs (gpus, &gpu_count));

      nv_gpu_count = gpu_count;
    }
    else {
      nv_gpu_count = 0;
    }
  }

  return nv_gpu_count;
}

int
NVAPI::CountSLIGPUs (void)
{
  static int nv_sli_count = -1;

  DXGI_ADAPTER_DESC* adapters = NVAPI::EnumGPUs_DXGI ();

  if (nv_sli_count == -1) {
    if (nv_hardware) {
      nv_sli_count = 0;

      while (adapters != nullptr) {
        if (adapters->AdapterLuid.LowPart > 1)
          nv_sli_count++;

        ++adapters;

        if (*adapters->Description == '\0')
          break;
      }
    }
  }

  return nv_sli_count;
}

static DXGI_ADAPTER_DESC   _nv_sli_adapters [64];

DXGI_ADAPTER_DESC*
NVAPI::EnumSLIGPUs (void)
{
  static int nv_sli_count = -1;

  *_nv_sli_adapters [0].Description = L'\0';

  DXGI_ADAPTER_DESC* adapters = NVAPI::EnumGPUs_DXGI ();

  if (nv_sli_count == -1) {
    if (nv_hardware) {
      nv_sli_count = 0;

      while (adapters != nullptr) {
        if (adapters->AdapterLuid.LowPart > 1)
          memcpy (&_nv_sli_adapters [nv_sli_count++], adapters,
                  sizeof (DXGI_ADAPTER_DESC));

        ++adapters;

        if (*adapters->Description == '\0')
          break;
      }

      *_nv_sli_adapters [nv_sli_count].Description = L'\0';
    }
  }

  return _nv_sli_adapters;
}

/**
 * These were hoisted out of EnumGPUs_DXGI (...) to reduce stack size.
 *
 *  ... but, it very likely has caused thread safety issues.
 **/
static DXGI_ADAPTER_DESC   _nv_dxgi_adapters [64];
static NvPhysicalGpuHandle _nv_dxgi_gpus     [64];
static NvPhysicalGpuHandle phys [64];

// This function does much more than it's supposed to -- consider fixing that!
DXGI_ADAPTER_DESC*
bmf::NVAPI::EnumGPUs_DXGI (void)
{
  // Only do this once...
  static bool enumerated = false;

  // Early-out if this was already called once before.
  if (enumerated)
    return _nv_dxgi_adapters;

  if (! nv_hardware) {
    enumerated = true;
    *_nv_dxgi_adapters [0].Description = L'\0';
    return _nv_dxgi_adapters;
  }

  NvU32 gpu_count     = 0;

  NVAPI_CALL (EnumPhysicalGPUs (_nv_dxgi_gpus, &gpu_count));

  for (int i = 0; i < CountPhysicalGPUs (); i++) {
    DXGI_ADAPTER_DESC adapterDesc;

    NvAPI_ShortString name;

    int   sli_group = 0;
    int   sli_size  = 0;

    NVAPI_CALL (EnumPhysicalGPUs (_nv_dxgi_gpus,     &gpu_count));

    NvU32              phys_count;
    NvLogicalGpuHandle logical;

    NVAPI_CALL (GetLogicalGPUFromPhysicalGPU  (_nv_dxgi_gpus [i], &logical));
    NVAPI_CALL (GetPhysicalGPUsFromLogicalGPU (logical, phys, &phys_count));

    sli_group = (size_t)logical & 0xffffffff;
    sli_size  = phys_count;

    NVAPI_CALL (GPU_GetFullName (_nv_dxgi_gpus [i], name));

    NV_DISPLAY_DRIVER_MEMORY_INFO meminfo;
    meminfo.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER;

    NVAPI_CALL (GPU_GetMemoryInfo (_nv_dxgi_gpus [i], &meminfo));

    MultiByteToWideChar (CP_OEMCP, 0, name, -1, adapterDesc.Description, 64);

    adapterDesc.VendorId = 0x10de;

    adapterDesc.AdapterLuid.HighPart = sli_group;
    adapterDesc.AdapterLuid.LowPart  = sli_size;

    // NVIDIA's driver measures these numbers in KiB (to store as a 32-bit int)
    //  * We want the numbers in bytes (64-bit)
    adapterDesc.DedicatedVideoMemory  =
      (size_t)meminfo.dedicatedVideoMemory << 10;
    adapterDesc.DedicatedSystemMemory =
      (size_t)meminfo.systemVideoMemory    << 10;
    adapterDesc.SharedSystemMemory    = 
      (size_t)meminfo.sharedSystemMemory   << 10;

    _nv_dxgi_adapters [i] = adapterDesc;
  }

  *_nv_dxgi_adapters [gpu_count].Description = L'\0';

  enumerated = true;

  return _nv_dxgi_adapters;
}

DXGI_ADAPTER_DESC*
NVAPI::FindGPUByDXGIName (const wchar_t* wszName)
{
  DXGI_ADAPTER_DESC* adapters = EnumGPUs_DXGI ();

  //"NVIDIA "
  // 01234567

  wchar_t* wszFixedName = _wcsdup (wszName + 7);
  int      fixed_len    = lstrlenW (wszFixedName);

  // Remove trailing whitespace some NV GPUs inexplicably have in their names
  for (int i = fixed_len; i > 0; i--) {
    if (wszFixedName [i - 1] == L' ')
      wszFixedName [i - 1] = L'\0';
    else
      break;
  }

  while (*adapters->Description != L'\0') {
    //strstrw (lstrcmpiW () == 0) { // More accurate, but some GPUs have
                                    //   trailing spaces in their names.
                                    // -----
                                    // What the heck?!

    if (wcsstr (adapters->Description, wszFixedName) != NULL) {
      delete [] wszFixedName;
      return adapters;
    }

    ++adapters;
  }

  delete [] wszFixedName;

  return NULL;
}

std::wstring
NVAPI::GetDriverVersion (NvU32* pVer)
{
  NvU32             ver;
  NvAPI_ShortString ver_str;       // ANSI
  wchar_t           ver_wstr [64]; // Unicode

  NvAPI_SYS_GetDriverAndBranchVersion (&ver, ver_str);

  // The driver-branch string's not particularly user frieldy,
  //   let's do this the right way and report a number the end-user
  //     is actually going to recognize...
  swprintf (ver_wstr, 64, L"%u.%u", ver / 100, ver % 100);

  if (pVer != NULL)
    *pVer = ver;

  return ver_wstr;
}


BOOL bLibShutdown = FALSE;
BOOL bLibInit = FALSE;

BOOL
NVAPI::UnloadLibrary (void)
{
  if (bLibInit == TRUE && bLibShutdown == FALSE) {
    // Whine very loudly if this fails, because that's not
    //   supposed to happen!
    NVAPI_VERBOSE ()

      NvAPI_Status ret;

    NVAPI_CALL2 (Unload (), ret);

    if (ret == NVAPI_OK) {
      bLibShutdown = TRUE;
      bLibInit     = FALSE;
    }
  }

  return bLibShutdown;
}

#include "log.h"

BOOL
NVAPI::InitializeLibrary (void)
{
  // It's silly to call this more than once, but not necessarily
  //  an error... just ignore repeated calls.
  if (bLibInit == TRUE)
    return TRUE;

  // If init is not false and not true, it's because we failed to
  //   initialize the API once before. Just return the failure status
  //     again.
  if (bLibInit != FALSE)
    return FALSE;

  NvAPI_Status ret;

  // We want this error to be silent, because this tool works on AMD GPUs too!
  NVAPI_SILENT ()
  {
    NVAPI_CALL2 (Initialize (), ret);
  }
  NVAPI_VERBOSE ()

    if (ret != NVAPI_OK) {
      nv_hardware = false;
      bLibInit    = TRUE + 1; // Clearly this isn't a boolean; just for looks
      return FALSE;
    }
    else {
      //
      // Time to initialize a few undocumented (if you do not sign an NDA)
      //   parts of NvAPI, hurray!
      //
      static HMODULE hLib = LoadLibrary (L"nvapi64.dll");

      typedef void* (*NvAPI_QueryInterface_t)(unsigned int ordinal);

      static NvAPI_QueryInterface_t NvAPI_QueryInterface =
        (NvAPI_QueryInterface_t)GetProcAddress (hLib, "nvapi_QueryInterface");

      NvAPI_GPU_GetRamType =
        (NvAPI_GPU_GetRamType_t)NvAPI_QueryInterface (0x57F7CAAC);
      NvAPI_GPU_GetFBWidthAndLocation =
        (NvAPI_GPU_GetFBWidthAndLocation_t)NvAPI_QueryInterface (0x11104158);
      NvAPI_GPU_GetPCIEInfo =
        (NvAPI_GPU_GetPCIEInfo_t)NvAPI_QueryInterface (0xE3795199);
      NvAPI_GetPhysicalGPUFromGPUID =
        (NvAPI_GetPhysicalGPUFromGPUID_t)NvAPI_QueryInterface (0x5380AD1A);
      NvAPI_GetGPUIDFromPhysicalGPU =
        (NvAPI_GetGPUIDFromPhysicalGPU_t)NvAPI_QueryInterface (0x6533EA3E);

#if 0
      NvPhysicalGpuHandle gpus[64];
      NvU32 cnt = 4;
      NvAPI_EnumPhysicalGPUs (gpus, &cnt);

      NV_GPU_PCIE_INFO pcieinfo;
      memset (&pcieinfo, 0, sizeof (NV_GPU_PCIE_INFO));

      NV_GPU_PERF_PSTATE_ID current_pstate;

      NvAPI_GPU_GetCurrentPstate (gpus[2], &current_pstate);

      pcieinfo.version = NV_GPU_PCIE_INFO_VER;
      if (NVAPI_OK == NvAPI_GPU_GetPCIEInfo(gpus[2], &pcieinfo)) {
        bmf_logger_t nvapi_log;
        nvapi_log.init ("nvapi.log", "w");
        //nvapi_log.Log (L" Size: %d Bytes\n", i);
        nvapi_log.Log (L" Version: %lu\n", pcieinfo.version);
        nvapi_log.Log (L" Current: %lu\n", current_pstate);
        for (int i = 0; i < 20; i++) {
          nvapi_log.Log (L" PSTATE %d\n", i);
          nvapi_log.Log (L" Rate:     %d\n", pcieinfo.pstates [i].pciLinkTransferRate);
          nvapi_log.Log (L" Version:  %d\n", pcieinfo.pstates [i].pciLinkVersion);
          nvapi_log.Log (L" Width:    %d\n", pcieinfo.pstates [i].pciLinkWidth);
          nvapi_log.Log (L" Rate:     %d\n", pcieinfo.pstates [i].pciLinkRate);
        }
        nvapi_log.close ();
      }
#endif

    nv_hardware = true;
  }

  if (! CheckDriverVersion ()) {
    MessageBox (NULL,
                L"WARNING:  Your display drivers are too old to play this game!\n",
                L"Please update your display drivers (Minimum Version = 355.82)",
                MB_OK | MB_ICONEXCLAMATION);
  }

  return (bLibInit = TRUE);
}

bool
NVAPI::CheckDriverVersion (void)
{
  NvU32 ver;
  GetDriverVersion (&ver);

  return ver >= 35582;
  if (ver < 35582) {
    return false;
  }

  return true;
}

NV_GET_CURRENT_SLI_STATE
NVAPI::GetSLIState (IUnknown* pDev)
{
  NV_GET_CURRENT_SLI_STATE state;
  state.version = NV_GET_CURRENT_SLI_STATE_VER;

  NvAPI_D3D_GetCurrentSLIState (pDev, &state);

  return state;
}

bool NVAPI::nv_hardware = false;