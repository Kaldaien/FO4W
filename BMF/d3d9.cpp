/**
* This file is part of Batman "Fix".
*
* Batman "Fix" is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* The Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Batman "Fix" is distributed in the hope that it will be useful,
* But WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Batman "Fix". If not, see <http://www.gnu.org/licenses/>.
**/

#include "d3d9_backend.h"
#include "log.h"

#include "stdafx.h"
#include "nvapi.h"
#include "config.h"

#include "import.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#pragma warning   (push)
#pragma warning   (disable: 4091)
#  include <Psapi.h>
#  include <DbgHelp.h>
#
#  pragma comment (lib, "psapi.lib")
#  pragma comment (lib, "dbghelp.lib")
#pragma warning   (pop)

#include "log.h"
#include "osd.h"
#include "import.h"
#include "io_monitor.h"
#include "memory_monitor.h"

#undef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE __cdecl

extern BOOL nvapi_init;

#include <d3d9types.h>

typedef IUnknown IDirect3D9;
typedef IUnknown IDirect3D9Ex;
typedef IUnknown IDirect3DSwapChain9;
typedef IUnknown IDirect3DDevice9;

typedef IDirect3D9*
  (STDMETHODCALLTYPE *Direct3DCreate9PROC)(  UINT          SDKVersion);
typedef HRESULT
  (STDMETHODCALLTYPE *Direct3DCreate9ExPROC)(UINT          SDKVersion,
                                             IDirect3D9Ex* d3d9ex);

typedef HRESULT (STDMETHODCALLTYPE *D3D9PresentSwapChain_t)(
           IDirect3DSwapChain9 *This,
_In_ const RECT                *pSourceRect,
_In_ const RECT                *pDestRect,
_In_       HWND                 hDestWindowOverride,
_In_ const RGNDATA             *pDirtyRegion,
_In_       DWORD                dwFlags);

typedef HRESULT (STDMETHODCALLTYPE *D3D9CreateDevice_t)(
           IDirect3D9             *This,
           UINT                    Adapter,
           D3DDEVTYPE              DeviceType,
           HWND                    hFocusWindow,
           DWORD                   BehaviorFlags,
           D3DPRESENT_PARAMETERS  *pPresentationParameters,
           IDirect3DDevice9      **ppReturnedDeviceInterface);

D3D9PresentSwapChain_t   D3D9Present_Original      = nullptr;
D3D9CreateDevice_t       D3D9CreateDevice_Original = nullptr;

Direct3DCreate9PROC   Direct3DCreate9_Import   = nullptr;
Direct3DCreate9ExPROC Direct3DCreate9Ex_Import = nullptr;

bmf_logger_t d3d9_log;
HMODULE      hD3D9 = NULL;

static          HANDLE           dll_heap    = { 0 };
static          CRITICAL_SECTION init_mutex  = { 0 };
static volatile HANDLE           hInitThread = { 0 };

void
BMF_Init_D3D9 (void)
{
  EnterCriticalSection (&init_mutex);
  if (hD3D9 != NULL) {
    LeaveCriticalSection (&init_mutex);
    return;
  }

  d3d9_log.init ("d3d9.log", "w");
  d3d9_log.Log  (L"d3d9.log created");

  d3d9_log.LogEx (false,
    L"------------------------------------------------------------------------"
    L"-----------\n");

  DWORD   dwProcessSize = MAX_PATH;
  wchar_t wszProcessName [MAX_PATH];

  HANDLE hProc = GetCurrentProcess ();

  QueryFullProcessImageName (hProc, 0, wszProcessName, &dwProcessSize);

  wchar_t* pwszShortName = wszProcessName + lstrlenW (wszProcessName);

  while (  pwszShortName      >  wszProcessName &&
    *(pwszShortName - 1) != L'\\')
    --pwszShortName;

  d3d9_log.Log (L">> (%s) <<", pwszShortName);

  d3d9_log.LogEx (false,
    L"----------------------------------------------------------------------"
    L"-------------\n");

  wchar_t wszD3d9DLL [MAX_PATH] = { L'\0' };
#ifdef _WIN64
  GetSystemDirectory (wszD3d9DLL, MAX_PATH);
#else
  BOOL bWOW64;
  ::IsWow64Process (hProc, &bWOW64);

  if (bWOW64)
    GetSystemWow64Directory (wszD3d9DLL, MAX_PATH);
  else
    GetSystemDirectory (wszD3d9DLL, MAX_PATH);
#endif

  d3d9_log.Log (L" System Directory:           %s", wszD3d9DLL);

  lstrcatW (wszD3d9DLL, L"\\d3d9.dll");

  d3d9_log.LogEx (true, L" Loading default d3d9.dll: ");

  hD3D9 = LoadLibrary (wszD3d9DLL);

  //CreateThread (NULL, 0, LoadD3D11, NULL, 0, NULL);

  if (hD3D9 != NULL)
    d3d9_log.LogEx (false, L" (%s)\n", wszD3d9DLL);
  else
    d3d9_log.LogEx (false, L" FAILED (%s)!\n", wszD3d9DLL);

  d3d9_log.LogEx (false,
    L"----------------------------------------------------------------------"
    L"-------------\n");


  d3d9_log.LogEx (true, L"Loading user preferences from d3d9.ini... ");
  if (BMF_LoadConfig (L"d3d9.ini")) {
    d3d9_log.LogEx (false, L"done!\n\n");
  } else {
    d3d9_log.LogEx (false, L"failed!\n");
    // If no INI file exists, write one immediately.
    d3d9_log.LogEx (true, L"  >> Writing base INI file, because none existed... ");
    BMF_SaveConfig (L"d3d9.ini");
    d3d9_log.LogEx (false, L"done!\n\n");
  }

  // Load user-defined DLLs (Early)
#ifdef _WIN64
  BMF_LoadEarlyImports64 ();
#else
  BMF_LoadEarlyImports32 ();
#endif

  if (config.system.silent) {
    d3d9_log.silent = true;
    DeleteFile     (L"d3d9.log");
  } else {
    d3d9_log.silent = false;
  }

  d3d9_log.LogEx (true, L"Initializing NvAPI: ");

  nvapi_init = bmf::NVAPI::InitializeLibrary ();

  d3d9_log.LogEx (false, L" %s\n\n", nvapi_init ? L"Success" : L"Failed");

  if (nvapi_init) {
    int num_sli_gpus = bmf::NVAPI::CountSLIGPUs ();

    d3d9_log.Log (L" >> NVIDIA Driver Version: %s",
      bmf::NVAPI::GetDriverVersion ().c_str ());

    d3d9_log.Log (L"  * Number of Installed NVIDIA GPUs: %u "
      L"(%u are in SLI mode)",
      bmf::NVAPI::CountPhysicalGPUs (), num_sli_gpus);

    if (num_sli_gpus > 0) {
      d3d9_log.LogEx (false, L"\n");

      DXGI_ADAPTER_DESC* sli_adapters =
        bmf::NVAPI::EnumSLIGPUs ();

      int sli_gpu_idx = 0;

      while (*sli_adapters->Description != L'\0') {
        d3d9_log.Log ( L"   + SLI GPU %d: %s",
          sli_gpu_idx++,
          (sli_adapters++)->Description );
      }
    }

    d3d9_log.LogEx (false, L"\n");
  }

  HMODULE hMod = GetModuleHandle (pwszShortName);

  if (hMod != NULL) {
    DWORD* dwOptimus = (DWORD *)GetProcAddress (hMod, "NvOptimusEnablement");

    if (dwOptimus != NULL) {
      d3d9_log.Log (L"  NvOptimusEnablement..................: 0x%02X (%s)",
        *dwOptimus,
        (*dwOptimus & 0x1 ? L"Max Perf." :
          L"Don't Care"));
    } else {
      d3d9_log.Log (L"  NvOptimusEnablement..................: UNDEFINED");
    }

    DWORD* dwPowerXpress =
      (DWORD *)GetProcAddress (hMod, "AmdPowerXpressRequestHighPerformance");

    if (dwPowerXpress != NULL) {
      d3d9_log.Log (L"  AmdPowerXpressRequestHighPerformance.: 0x%02X (%s)",
        *dwPowerXpress,
        (*dwPowerXpress & 0x1 ? L"High Perf." :
          L"Don't Care"));
    }
    else
      d3d9_log.Log (L"  AmdPowerXpressRequestHighPerformance.: UNDEFINED");

    d3d9_log.LogEx (false, L"\n");
  }

  d3d9_log.Log (L"Importing Direct3DCreate9{Ex}...");
  d3d9_log.Log (L"================================");

  d3d9_log.Log (L"  Direct3DCreate9:   %08Xh", 
    (Direct3DCreate9_Import) =  \
      (Direct3DCreate9PROC)GetProcAddress (hD3D9, "Direct3DCreate9"));
  d3d9_log.Log (L"  Direct3DCreate9Ex: %08Xh",
    (Direct3DCreate9Ex_Import) =  \
      (Direct3DCreate9ExPROC)GetProcAddress (hD3D9, "Direct3DCreate9Ex"));

  d3d9_log.LogEx (true, L" @ Loading Debug Symbols: ");

  SymInitializeW       (GetCurrentProcess (), NULL, TRUE);
  SymRefreshModuleList (GetCurrentProcess ());

  d3d9_log.LogEx (false, L"done!\n");

  d3d9_log.Log (L"=== Initialization Finished! ===\n");

  //
  // Spawn CPU Refresh Thread
  //
  if (cpu_stats.hThread == 0) {
    d3d9_log.LogEx (true, L" [WMI] Spawning CPU Monitor...      ");
    cpu_stats.hThread = CreateThread (NULL, 0, BMF_MonitorCPU, NULL, 0, NULL);
    if (cpu_stats.hThread != 0)
      d3d9_log.LogEx (false, L"tid=0x%04x\n", GetThreadId (cpu_stats.hThread));
    else
      d3d9_log.LogEx (false, L"Failed!\n");
  }

  Sleep (333);

  if (disk_stats.hThread == 0) {
    d3d9_log.LogEx (true, L" [WMI] Spawning Disk Monitor...     ");
    disk_stats.hThread =
      CreateThread (NULL, 0, BMF_MonitorDisk, NULL, 0, NULL);
    if (disk_stats.hThread != 0)
      d3d9_log.LogEx (false, L"tid=0x%04x\n", GetThreadId (disk_stats.hThread));
    else
      d3d9_log.LogEx (false, L"failed!\n");
  }

  Sleep (333);

  if (pagefile_stats.hThread == 0) {
    d3d9_log.LogEx (true, L" [WMI] Spawning Pagefile Monitor... ");
    pagefile_stats.hThread =
      CreateThread (NULL, 0, BMF_MonitorPagefile, NULL, 0, NULL);
    if (pagefile_stats.hThread != 0)
      d3d9_log.LogEx (false, L"tid=0x%04x\n",
        GetThreadId (pagefile_stats.hThread));
    else
      d3d9_log.LogEx (false, L"failed!\n");
  }

  Sleep (333);

  //
  // Spawn Process Monitor Thread
  //
  if (process_stats.hThread == 0) {
    d3d9_log.LogEx (true, L" [WMI] Spawning Process Monitor...  ");
    process_stats.hThread = CreateThread (NULL, 0, BMF_MonitorProcess, NULL, 0, NULL);
    if (process_stats.hThread != 0)
      d3d9_log.LogEx (false, L"tid=0x%04x\n", GetThreadId (process_stats.hThread));
    else
      d3d9_log.LogEx (false, L"Failed!\n");
  }

  d3d9_log.LogEx (false, L"\n");

  LeaveCriticalSection (&init_mutex);
}

DWORD
WINAPI D3D9_DllThread (LPVOID param)
{
  EnterCriticalSection (&init_mutex);
  {
    BMF_Init_D3D9 ();
  }
  LeaveCriticalSection (&init_mutex);

  return 0;
}


bool
BMF::D3D9::Startup (void)
{
  dll_heap = HeapCreate (0, 0, 0);
  //BOOL bRet = InitializeCriticalSectionAndSpinCount (&budget_mutex,  4000);
#ifdef HOOK_D3D11_DEVICE_CREATION
  bRet = InitializeCriticalSectionAndSpinCount (&d3dhook_mutex, 500);
#endif
  BOOL bRet = InitializeCriticalSectionAndSpinCount (&init_mutex,    50000);

  hInitThread = CreateThread (NULL, 0, D3D9_DllThread, NULL, 0, NULL);

  // Give other D3D9 hookers time to queue up before processing any calls
  //   that they make. But don't wait here infinitely, or we will deadlock!

  /* Default: 0.25 secs seems adequate */
  if (hInitThread != 0)
    WaitForSingleObject (hInitThread, config.system.init_delay);

  return true;
}

bool
BMF::D3D9::Shutdown (void)
{
  d3d9_log.LogEx (true,
    L"Closing RivaTuner Statistics Server connection... ");
  // Shutdown the OSD as early as possible to avoid complications
  BMF_ReleaseOSD ();
  d3d9_log.LogEx (false, L"done!\n");

#if 0
  if (budget_thread != nullptr) {
    config.load_balance.use = false; // Turn this off while shutting down

    dxgi_log.LogEx (
      true,
      L"Shutting down DXGI 1.4 Memory Budget Change Thread... "
      );

    budget_thread->ready = false;

    SignalObjectAndWait (budget_thread->event, budget_thread->handle,
      INFINITE, TRUE);

    dxgi_log.LogEx (false, L"done!\n");

    // Record the final statistics always
    budget_log.silent = false;

    budget_log.LogEx (false, L"\n");
    budget_log.Log   (L"--------------------");
    budget_log.Log   (L"Shutdown Statistics:");
    budget_log.Log   (L"--------------------\n");

    // in %10u seconds\n",
    budget_log.Log (L" Memory Budget Changed %llu times\n",
      mem_stats [0].budget_changes);

    for (int i = 0; i < 4; i++) {
      if (mem_stats [i].budget_changes > 0) {
        if (mem_stats [i].min_reserve == UINT64_MAX)
          mem_stats [i].min_reserve = 0ULL;

        if (mem_stats [i].min_over_budget == UINT64_MAX)
          mem_stats [i].min_over_budget = 0ULL;

        budget_log.LogEx (true, L" GPU%u: Min Budget:        %05llu MiB\n", i,
          mem_stats [i].min_budget >> 20ULL);
        budget_log.LogEx (true, L"       Max Budget:        %05llu MiB\n",
          mem_stats [i].max_budget >> 20ULL);

        budget_log.LogEx (true, L"       Min Usage:         %05llu MiB\n",
          mem_stats [i].min_usage >> 20ULL);
        budget_log.LogEx (true, L"       Max Usage:         %05llu MiB\n",
          mem_stats [i].max_usage >> 20ULL);

        /*
        BMF_BLogEx (params, true, L"       Min Reserve:       %05u MiB\n",
        mem_stats [i].min_reserve >> 20ULL);
        BMF_BLogEx (params, true, L"       Max Reserve:       %05u MiB\n",
        mem_stats [i].max_reserve >> 20ULL);
        BMF_BLogEx (params, true, L"       Min Avail Reserve: %05u MiB\n",
        mem_stats [i].min_avail_reserve >> 20ULL);
        BMF_BLogEx (params, true, L"       Max Avail Reserve: %05u MiB\n",
        mem_stats [i].max_avail_reserve >> 20ULL);
        */

        budget_log.LogEx (true, L"------------------------------------\n");
        budget_log.LogEx (true, L" Minimum Over Budget:     %05llu MiB\n",
          mem_stats [i].min_over_budget >> 20ULL);
        budget_log.LogEx (true, L" Maximum Over Budget:     %05llu MiB\n",
          mem_stats [i].max_over_budget >> 20ULL);
        budget_log.LogEx (true, L"------------------------------------\n");

        budget_log.LogEx (false, L"\n");
      }
    }

    //ULONG refs = 0;
    //if (budget_thread->pAdapter != nullptr) {
    //refs = budget_thread->pAdapter->AddRef ();
    //budget_log.LogEx (true, L" >> Budget Adapter has %u refs. left...\n",
    //refs - 1);

    //budget_thread->pAdapter->
    //UnregisterVideoMemoryBudgetChangeNotification
    //(budget_thread->cookie);

    //if (refs > 2)
    //budget_thread->pAdapter->Release ();
    //budget_thread->pAdapter->Release   ();
    //}

    HeapFree (dll_heap, NULL, budget_thread);
    budget_thread = nullptr;
  }
#endif

  if (process_stats.hThread != 0) {
    d3d9_log.LogEx (true,L"[WMI] Shutting down Process Monitor... ");
    // Signal the thread to shutdown
    process_stats.lID = 0;
    WaitForSingleObject
      (process_stats.hThread, 1000UL); // Give 1 second, and
                                       // then we're killing
                                       // the thing!
    TerminateThread (process_stats.hThread, 0);
    process_stats.hThread  = 0;
    d3d9_log.LogEx (false, L"done!\n");
  }

  if (cpu_stats.hThread != 0) {
    d3d9_log.LogEx (true,L"[WMI] Shutting down CPU Monitor... ");
    // Signal the thread to shutdown
    cpu_stats.lID = 0;
    WaitForSingleObject (cpu_stats.hThread, 1000UL); // Give 1 second, and
                                                     // then we're killing
                                                     // the thing!
    TerminateThread (cpu_stats.hThread, 0);
    cpu_stats.hThread  = 0;
    cpu_stats.num_cpus = 0;
    d3d9_log.LogEx (false, L"done!\n");
  }

  if (disk_stats.hThread != 0) {
    d3d9_log.LogEx (true, L"[WMI] Shutting down Disk Monitor... ");
    // Signal the thread to shutdown
    disk_stats.lID = 0;
    WaitForSingleObject (disk_stats.hThread, 1000UL); // Give 1 second, and
                                                      // then we're killing
                                                      // the thing!
    TerminateThread (disk_stats.hThread, 0);
    disk_stats.hThread   = 0;
    disk_stats.num_disks = 0;
    d3d9_log.LogEx (false, L"done!\n");
  }

  if (pagefile_stats.hThread != 0) {
    d3d9_log.LogEx (true, L"[WMI] Shutting down Pagefile Monitor... ");
    // Signal the thread to shutdown
    pagefile_stats.lID = 0;
    WaitForSingleObject (
      pagefile_stats.hThread, 1000UL); // Give 1 second, and
                                       // then we're killing
                                       // the thing!
    TerminateThread (pagefile_stats.hThread, 0);
    pagefile_stats.hThread       = 0;
    pagefile_stats.num_pagefiles = 0;
    d3d9_log.LogEx (false, L"done!\n");
  }

  d3d9_log.LogEx (true, L"Saving user preferences to d3d9.ini... ");
  BMF_SaveConfig (L"d3d9.ini");
  d3d9_log.LogEx (false, L"done!\n");

  // Hopefully these things are done by now...
  DeleteCriticalSection (&init_mutex);
#ifdef HOOK_D3D11_DEVICE_CREATION
  DeleteCriticalSection (&d3dhook_mutex);
#endif
  //DeleteCriticalSection (&budget_mutex);

#ifdef HOOK_D3D11_DEVICE_CREATION
  MH_Uninitialize ();
#endif

  if (nvapi_init)
    bmf::NVAPI::UnloadLibrary ();

  d3d9_log.Log (L"Custom d3d9.dll Detached (pid=0x%04x)",
    GetCurrentProcessId ());

  budget_log.close ();
  d3d9_log.close   ();

  HeapDestroy (dll_heap);

  SymCleanup (GetCurrentProcess ());

  return true;
}

  HRESULT
  __cdecl D3D9PresentCallback (IDirect3DSwapChain9 *This,
                    _In_ const RECT                *pSourceRect,
                    _In_ const RECT                *pDestRect,
                    _In_       HWND                 hDestWindowOverride,
                    _In_ const RGNDATA             *pDirtyRegion,
                    _In_       DWORD                dwFlags)
  {
    // Load user-defined DLLs (Late)
#ifdef _WIN64
    BMF_LoadLateImports64 ();
#else
    BMF_LoadLateImports32 ();
#endif

    BOOL osd = false;

    HRESULT hr  = D3D9Present_Original (This,
                                        pSourceRect,
                                        pDestRect,
                                        hDestWindowOverride,
                                        pDirtyRegion,
                                        dwFlags);

    // Draw after present, this may make stuff 1 frame late, but... it
    //   helps with VSYNC performance.
    if (This != nullptr)
      osd = BMF_DrawOSD ();

    // Early-out if the OSD is not functional
    if (! osd)
      return hr;

    if (FAILED (hr))
      return hr;

    static ULONGLONG last_osd_scale { 0ULL };

    SYSTEMTIME    stNow;
    FILETIME      ftNow;
    LARGE_INTEGER ullNow;

    GetSystemTime        (&stNow);
    SystemTimeToFileTime (&stNow, &ftNow);

    ullNow.HighPart = ftNow.dwHighDateTime;
    ullNow.LowPart  = ftNow.dwLowDateTime;

    if (ullNow.QuadPart - last_osd_scale > 1000000ULL) {
      if (HIWORD (GetAsyncKeyState (config.osd.keys.expand [0])) &&
          HIWORD (GetAsyncKeyState (config.osd.keys.expand [1])) &&
          HIWORD (GetAsyncKeyState (config.osd.keys.expand [2])))
      {
        last_osd_scale = ullNow.QuadPart;
        BMF_ResizeOSD (+1);
      }

      if (HIWORD (GetAsyncKeyState (config.osd.keys.shrink [0])) &&
          HIWORD (GetAsyncKeyState (config.osd.keys.shrink [1])) &&
          HIWORD (GetAsyncKeyState (config.osd.keys.shrink [2])))
      {
        last_osd_scale = ullNow.QuadPart;
        BMF_ResizeOSD (-1);
      }
    }

    static bool toggle_time = false;
    if (HIWORD (GetAsyncKeyState (config.time.keys.toggle [0])) &&
        HIWORD (GetAsyncKeyState (config.time.keys.toggle [1])) &&
        HIWORD (GetAsyncKeyState (config.time.keys.toggle [2])))
    {
      if (! toggle_time)
        config.time.show = (! config.time.show);
      toggle_time = true;
    } else {
      toggle_time = false;
    }

    static bool toggle_mem = false;
    if (HIWORD (GetAsyncKeyState (config.mem.keys.toggle [0])) &&
        HIWORD (GetAsyncKeyState (config.mem.keys.toggle [1])) &&
        HIWORD (GetAsyncKeyState (config.mem.keys.toggle [2])))
    {
      if (! toggle_mem)
        config.mem.show = (! config.mem.show);
      toggle_mem = true;
    } else {
      toggle_mem = false;
    }

    static bool toggle_balance = false;
    if (HIWORD (GetAsyncKeyState (config.load_balance.keys.toggle [0])) &&
        HIWORD (GetAsyncKeyState (config.load_balance.keys.toggle [1])) &&
        HIWORD (GetAsyncKeyState (config.load_balance.keys.toggle [2])))
    {
      if (! toggle_balance)
        config.load_balance.use = (! config.load_balance.use);
      toggle_balance = true;
    } else {
      toggle_balance = false;
    }

    static bool toggle_sli = false;
    if (HIWORD (GetAsyncKeyState (config.sli.keys.toggle [0])) &&
        HIWORD (GetAsyncKeyState (config.sli.keys.toggle [1])) &&
        HIWORD (GetAsyncKeyState (config.sli.keys.toggle [2])))
    {
      if (! toggle_sli)
        config.sli.show = (! config.sli.show);
      toggle_sli = true;
    } else {
      toggle_sli = false;
    }

    static bool toggle_io = false;
    if (HIWORD (GetAsyncKeyState (config.io.keys.toggle [0])) &&
        HIWORD (GetAsyncKeyState (config.io.keys.toggle [1])) &&
        HIWORD (GetAsyncKeyState (config.io.keys.toggle [2])))
    {
      if (! toggle_io)
        config.io.show = (! config.io.show);
      toggle_io = true;
    } else {
      toggle_io = false;
    }

    static bool toggle_cpu = false;
    if (HIWORD (GetAsyncKeyState (config.cpu.keys.toggle [0])) &&
        HIWORD (GetAsyncKeyState (config.cpu.keys.toggle [1])) &&
        HIWORD (GetAsyncKeyState (config.cpu.keys.toggle [2])))
    {
      if (! toggle_cpu)
        config.cpu.show = (! config.cpu.show);
      toggle_cpu = true;
    } else {
      toggle_cpu = false;
    }

    static bool toggle_gpu = false;
    if (HIWORD (GetAsyncKeyState (config.gpu.keys.toggle [0])) &&
        HIWORD (GetAsyncKeyState (config.gpu.keys.toggle [1])) &&
        HIWORD (GetAsyncKeyState (config.gpu.keys.toggle [2])))
    {
      if (! toggle_gpu)
        config.gpu.show = (! config.gpu.show);
      toggle_gpu = true;
    } else {
      toggle_gpu = false;
    }

    static bool toggle_fps = false;
    if (HIWORD (GetAsyncKeyState (config.fps.keys.toggle [0])) &&
        HIWORD (GetAsyncKeyState (config.fps.keys.toggle [1])) &&
        HIWORD (GetAsyncKeyState (config.fps.keys.toggle [2])))
    {
      if (! toggle_fps)
        config.fps.show = (! config.fps.show);
      toggle_fps = true;
    } else {
      toggle_fps = false;
    }

    static bool toggle_disk = false;
    if (HIWORD (GetAsyncKeyState (config.disk.keys.toggle [0])) &&
        HIWORD (GetAsyncKeyState (config.disk.keys.toggle [1])) &&
        HIWORD (GetAsyncKeyState (config.disk.keys.toggle [2])))
    {
      if (! toggle_disk)
        config.disk.show = (! config.disk.show);
      toggle_disk = true;
    } else {
      toggle_disk = false;
    }

    static bool toggle_pagefile = false;
    if (HIWORD (GetAsyncKeyState (config.pagefile.keys.toggle [0])) &&
        HIWORD (GetAsyncKeyState (config.pagefile.keys.toggle [1])) &&
        HIWORD (GetAsyncKeyState (config.pagefile.keys.toggle [2])))
    {
      if (! toggle_pagefile)
        config.pagefile.show = (! config.pagefile.show);
      toggle_pagefile = true;
    } else {
      toggle_pagefile = false;
    }

    static bool toggle_osd = false;
    if (HIWORD (GetAsyncKeyState (config.osd.keys.toggle [0])) &&
        HIWORD (GetAsyncKeyState (config.osd.keys.toggle [1])) &&
        HIWORD (GetAsyncKeyState (config.osd.keys.toggle [2])))
    {
      if (! toggle_osd)
        config.osd.show = (! config.osd.show);
      toggle_osd = true;
    } else {
      toggle_osd = false;
    }

#if 0
    if (config.sli.show)
    {
      // Get SLI status for the frame we just displayed... this will show up
      //   one frame late, but this is the safest approach.
      if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
        IUnknown* pDev = nullptr;

        if (SUCCEEDED (This->GetDevice(__uuidof (IDirect3D9), (void **)&pDev)))
        {
          //sli_state = bmf::NVAPI::GetSLIState (pDev);
          pDev->Release ();
        }
      }
    }
#endif

    return hr;
  }

static void
WaitForInit (void)
{
  while (! hInitThread) ;

  if (hInitThread)
    WaitForSingleObject (hInitThread, INFINITE);

  // Load user-defined DLLs (Lazy)
#ifdef _WIN64
  BMF_LoadLazyImports64 ();
#else
  BMF_LoadLazyImports32 ();
#endif
}


#define D3D9_STUB_HRESULT(_Return, _Name, _Proto, _Args)            \
  __declspec (dllexport) _Return STDMETHODCALLTYPE                  \
  _Name _Proto {                                                    \
    WaitForInit ();                                                 \
                                                                    \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;      \
    static passthrough_t _default_impl = nullptr;                   \
                                                                    \
    if (_default_impl == nullptr) {                                 \
      static const char* szName = #_Name;                           \
      _default_impl = (passthrough_t)GetProcAddress (hD3D9, szName);\
                                                                    \
      if (_default_impl == nullptr) {                               \
        d3d9_log.Log (                                              \
          L"Unable to locate symbol  %s in d3d9.dll",               \
          L#_Name);                                                 \
        return E_NOTIMPL;                                           \
      }                                                             \
    }                                                               \
                                                                    \
    d3d9_log.Log (L"[!] %s (%s) - "                                 \
             L"[Calling Thread: 0x%04x]",                           \
      L#_Name, L#_Proto, GetCurrentThreadId ());                    \
                                                                    \
    return _default_impl _Args;                                     \
}

#define D3D9_STUB_VOIDP(_Return, _Name, _Proto, _Args)              \
  __declspec (dllexport) _Return STDMETHODCALLTYPE                  \
  _Name _Proto {                                                    \
    WaitForInit ();                                                 \
                                                                    \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;      \
    static passthrough_t _default_impl = nullptr;                   \
                                                                    \
    if (_default_impl == nullptr) {                                 \
      static const char* szName = #_Name;                           \
      _default_impl = (passthrough_t)GetProcAddress (hD3D9, szName);\
                                                                    \
      if (_default_impl == nullptr) {                               \
        d3d9_log.Log (                                              \
          L"Unable to locate symbol  %s in d3d9.dll",               \
          L#_Name);                                                 \
        return nullptr;                                             \
      }                                                             \
    }                                                               \
                                                                    \
    d3d9_log.Log (L"[!] %s (%s) - "                                 \
             L"[Calling Thread: 0x%04x]",                           \
      L#_Name, L#_Proto, GetCurrentThreadId ());                    \
                                                                    \
    return _default_impl _Args;                                     \
}

#define D3D9_STUB_VOID(_Return, _Name, _Proto, _Args)               \
  __declspec (dllexport) _Return STDMETHODCALLTYPE                  \
  _Name _Proto {                                                    \
    WaitForInit ();                                                 \
                                                                    \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;      \
    static passthrough_t _default_impl = nullptr;                   \
                                                                    \
    if (_default_impl == nullptr) {                                 \
      static const char* szName = #_Name;                           \
      _default_impl = (passthrough_t)GetProcAddress (hD3D9, szName);\
                                                                    \
      if (_default_impl == nullptr) {                               \
        d3d9_log.Log (                                              \
          L"Unable to locate symbol  %s in d3d9.dll",               \
          L#_Name);                                                 \
        return;                                                     \
      }                                                             \
    }                                                               \
                                                                    \
    d3d9_log.Log (L"[!] %s (%s) - "                                 \
             L"[Calling Thread: 0x%04x]",                           \
      L#_Name, L#_Proto, GetCurrentThreadId ());                    \
                                                                    \
    _default_impl _Args;                                            \
}

#define D3D9_STUB_INT(_Return, _Name, _Proto, _Args)                \
  __declspec (dllexport) _Return STDMETHODCALLTYPE                  \
  _Name _Proto {                                                    \
    WaitForInit ();                                                 \
                                                                    \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;      \
    static passthrough_t _default_impl = nullptr;                   \
                                                                    \
    if (_default_impl == nullptr) {                                 \
      static const char* szName = #_Name;                           \
      _default_impl = (passthrough_t)GetProcAddress (hD3D9, szName);\
                                                                    \
      if (_default_impl == nullptr) {                               \
        d3d9_log.Log (                                              \
          L"Unable to locate symbol  %s in d3d9.dll",               \
          L#_Name);                                                 \
        return 0;                                                   \
      }                                                             \
    }                                                               \
                                                                    \
    d3d9_log.Log (L"[!] %s (%s) - "                                 \
             L"[Calling Thread: 0x%04x]",                           \
      L#_Name, L#_Proto, GetCurrentThreadId ());                    \
                                                                    \
    return _default_impl _Args;                                     \
}


typedef void (WINAPI *DebugSetMuteProc)(void);

extern "C" {
D3D9_STUB_HRESULT (HRESULT, Direct3DCreate9Ex, 
                   (UINT SDKVersion, IDirect3D9Ex** d3d9ex),
                        (SDKVersion,                d3d9ex))

D3D9_STUB_VOIDP   (void*, Direct3DShaderValidatorCreate, (void), 
                                                         (    ))

D3D9_STUB_INT     (int,   D3DPERF_BeginEvent, (D3DCOLOR color, LPCWSTR name),
                                                       (color,         name))
D3D9_STUB_INT     (int,   D3DPERF_EndEvent,   (void),          ( ))

D3D9_STUB_INT     (DWORD, D3DPERF_GetStatus,  (void),          ( ))
D3D9_STUB_VOID    (void,  D3DPERF_SetOptions, (DWORD options), (options))

D3D9_STUB_INT     (BOOL,  D3DPERF_QueryRepeatFrame, (void),    ( ))
D3D9_STUB_VOID    (void,  D3DPERF_SetMarker, (D3DCOLOR color, LPCWSTR name),
                                                      (color,         name))
D3D9_STUB_VOID    (void,  D3DPERF_SetRegion, (D3DCOLOR color, LPCWSTR name),
                                                      (color,         name))

extern const wchar_t*
BMF_DescribeVirtualProtectFlags (DWORD dwProtect);

#ifdef _WIN64
#define D3D9_VIRTUAL_OVERRIDE(_Base,_Index,_Name,_Override,_Original,_Type) { \
  void** vftable = *(void***)*_Base;                                          \
                                                                              \
  if (vftable [_Index] != _Override) {                                        \
    DWORD dwProtect;                                                          \
                                                                              \
    VirtualProtect (&vftable [_Index], 8, PAGE_EXECUTE_READWRITE, &dwProtect);\
                                                                              \
  d3d9_log.Log (L" Original VFTable entry for %s: %08Xh  (Memory Policy: %s)",\
             L##_Name, vftable [_Index],                                      \
             BMF_DescribeVirtualProtectFlags (dwProtect));                    \
                                                                              \
    if (_Original == NULL)                                                    \
      _Original = (##_Type)vftable [_Index];                                  \
                                                                              \
    d3d9_log.Log (L"  + %s: %08Xh", L#_Original, _Original);                  \
                                                                              \
    vftable [_Index] = _Override;                                             \
                                                                              \
    VirtualProtect (&vftable [_Index], 8, dwProtect, &dwProtect);             \
                                                                              \
    d3d9_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n", \
                  L##_Name, vftable [_Index],                                 \
                  BMF_DescribeVirtualProtectFlags (dwProtect));               \
  }                                                                           \
}
#else
#define D3D9_VIRTUAL_OVERRIDE(_Base,_Index,_Name,_Override,_Original,_Type) { \
  void** vftable = *(void***)*_Base;                                          \
                                                                              \
  if (vftable [_Index] != _Override) {                                        \
    DWORD dwProtect;                                                          \
                                                                              \
    VirtualProtect (&vftable [_Index], 4, PAGE_EXECUTE_READWRITE, &dwProtect);\
                                                                              \
  d3d9_log.Log (L" Original VFTable entry for %s: %08Xh  (Memory Policy: %s)",\
             L##_Name, vftable [_Index],                                      \
             BMF_DescribeVirtualProtectFlags (dwProtect));                    \
                                                                              \
    if (_Original == NULL)                                                    \
      _Original = (##_Type)vftable [_Index];                                  \
                                                                              \
    d3d9_log.Log (L"  + %s: %08Xh", L#_Original, _Original);                  \
                                                                              \
    vftable [_Index] = _Override;                                             \
                                                                              \
    VirtualProtect (&vftable [_Index], 4, dwProtect, &dwProtect);             \
                                                                              \
    d3d9_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n", \
                  L##_Name, vftable [_Index],                                 \
                  BMF_DescribeVirtualProtectFlags (dwProtect));               \
  }                                                                           \
}
#endif

//#include <d3d9.h>

HRESULT
STDMETHODCALLTYPE
D3D9CreateDevice_Override (IDirect3D9             *This,
                           UINT                    Adapter,
                           D3DDEVTYPE              DeviceType,
                           HWND                    hFocusWindow,
                           DWORD                   BehaviorFlags,
                           D3DPRESENT_PARAMETERS  *pPresentationParameters,
                           IDirect3DDevice9      **ppReturnedDeviceInterface)
{
  //std::wstring iname = BMF_GetDXGIAdapterInterface (This);

  //DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc2", L"%08Xh, %08Xh", This, pDesc);

  HRESULT ret =
    D3D9CreateDevice_Original (This,          Adapter,
                               DeviceType,    hFocusWindow,
                               BehaviorFlags, pPresentationParameters,
                               ppReturnedDeviceInterface);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 17,
                         "IDirect3DDevice9::Present", D3D9PresentCallback,
                         D3D9Present_Original, D3D9PresentSwapChain_t);

  //DXGI_CALL (ret, GetDesc2_Original (This, pDesc));

  return ret;
}

__declspec (dllexport)
IDirect3D9*
__cdecl
Direct3DCreate9 (UINT SDKVersion)
{
  WaitForInit ();

  d3d9_log.Log (L"[!] %s (%lu) - "
                L"[Calling Thread: 0x%04x]",
                L"Direct3DCreate9", SDKVersion, GetCurrentThreadId ());

  IDirect3D9* dev = nullptr;

  if (Direct3DCreate9_Import)
    dev = Direct3DCreate9_Import (SDKVersion);

  D3D9_VIRTUAL_OVERRIDE (&dev, 16, "pDev->CreateDevice",
                         D3D9CreateDevice_Override, D3D9CreateDevice_Original,
                         D3D9CreateDevice_t);

  return dev;
}
}