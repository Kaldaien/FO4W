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

#define _CRT_SECURE_NO_WARNINGS

#include "core.h"
#include "stdafx.h"

#include "log.h"

#include "steam_api.h"

#pragma warning   (push)
#pragma warning   (disable: 4091)
#  include <Psapi.h>
#  include <DbgHelp.h>
#
#  pragma comment (lib, "psapi.lib")
#  pragma comment (lib, "dbghelp.lib")
#pragma warning   (pop)

#include "config.h"
#include "osd.h"
#include "io_monitor.h"
#include "import.h"

memory_stats_t mem_stats [MAX_GPU_NODES];
mem_info_t     mem_info  [NumBuffers];

HANDLE           dll_heap      = { 0 };
CRITICAL_SECTION budget_mutex  = { 0 };
CRITICAL_SECTION init_mutex    = { 0 };
volatile HANDLE  hInitThread   = { 0 };
         HANDLE  hPumpThread   = { 0 };

struct budget_thread_params_t {
  IDXGIAdapter3   *pAdapter;
  DWORD            tid;
  HANDLE           handle;
  DWORD            cookie;
  HANDLE           event;
  volatile bool    ready;
} *budget_thread = nullptr;

std::wstring host_app;

// Disable SLI memory in Batman Arkham Knight
bool USE_SLI = true;

NV_GET_CURRENT_SLI_STATE sli_state;
BOOL                     nvapi_init = FALSE;
int                      gpu_prio;

HMODULE backend_dll;

char*   szOSD;

#include <d3d9.h>

const wchar_t*
BMF_DescribeHRESULT (HRESULT result)
{
  switch (result)
  {
    /* Generic (SUCCEEDED) */

  case S_OK:
    return L"S_OK";

  case S_FALSE:
    return L"S_FALSE";


    /* DXGI */

  case DXGI_ERROR_DEVICE_HUNG:
    return L"DXGI_ERROR_DEVICE_HUNG";

  case DXGI_ERROR_DEVICE_REMOVED:
    return L"DXGI_ERROR_DEVICE_REMOVED";

  case DXGI_ERROR_DEVICE_RESET:
    return L"DXGI_ERROR_DEVICE_RESET";

  case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
    return L"DXGI_ERROR_DRIVER_INTERNAL_ERROR";

  case DXGI_ERROR_FRAME_STATISTICS_DISJOINT:
    return L"DXGI_ERROR_FRAME_STATISTICS_DISJOINT";

  case DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE:
    return L"DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE";

  case DXGI_ERROR_INVALID_CALL:
    return L"DXGI_ERROR_INVALID_CALL";

  case DXGI_ERROR_MORE_DATA:
    return L"DXGI_ERROR_MORE_DATA";

  case DXGI_ERROR_NONEXCLUSIVE:
    return L"DXGI_ERROR_NONEXCLUSIVE";

  case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
    return L"DXGI_ERROR_NOT_CURRENTLY_AVAILABLE";

  case DXGI_ERROR_NOT_FOUND:
    return L"DXGI_ERROR_NOT_FOUND";

  case DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED:
    return L"DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED";

  case DXGI_ERROR_REMOTE_OUTOFMEMORY:
    return L"DXGI_ERROR_REMOTE_OUTOFMEMORY";

  case DXGI_ERROR_WAS_STILL_DRAWING:
    return L"DXGI_ERROR_WAS_STILL_DRAWING";

  case DXGI_ERROR_UNSUPPORTED:
    return L"DXGI_ERROR_UNSUPPORTED";

  case DXGI_ERROR_ACCESS_LOST:
    return L"DXGI_ERROR_ACCESS_LOST";

  case DXGI_ERROR_WAIT_TIMEOUT:
    return L"DXGI_ERROR_WAIT_TIMEOUT";

  case DXGI_ERROR_SESSION_DISCONNECTED:
    return L"DXGI_ERROR_SESSION_DISCONNECTED";

  case DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE:
    return L"DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE";

  case DXGI_ERROR_CANNOT_PROTECT_CONTENT:
    return L"DXGI_ERROR_CANNOT_PROTECT_CONTENT";

  case DXGI_ERROR_ACCESS_DENIED:
    return L"DXGI_ERROR_ACCESS_DENIED";

  case DXGI_ERROR_NAME_ALREADY_EXISTS:
    return L"DXGI_ERROR_NAME_ALREADY_EXISTS";

  case DXGI_ERROR_SDK_COMPONENT_MISSING:
    return L"DXGI_ERROR_SDK_COMPONENT_MISSING";


    /* DXGI (Status) */
  case DXGI_STATUS_OCCLUDED:
    return L"DXGI_STATUS_OCCLUDED";

  case DXGI_STATUS_MODE_CHANGED:
    return L"DXGI_STATUS_MODE_CHANGED";

  case DXGI_STATUS_MODE_CHANGE_IN_PROGRESS:
    return L"DXGI_STATUS_MODE_CHANGE_IN_PROGRESS";


    /* D3D11 */

  case D3D11_ERROR_FILE_NOT_FOUND:
    return L"D3D11_ERROR_FILE_NOT_FOUND";

  case D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS:
    return L"D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS";

  case D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS:
    return L"D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS";

  case D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD:
    return L"D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD";


    /* D3D9 */

  case D3DERR_WRONGTEXTUREFORMAT:
    return L"D3DERR_WRONGTEXTUREFORMAT";

  case D3DERR_UNSUPPORTEDCOLOROPERATION:
    return L"D3DERR_UNSUPPORTEDCOLOROPERATION";

  case D3DERR_UNSUPPORTEDCOLORARG:
    return L"D3DERR_UNSUPPORTEDCOLORARG";

  case D3DERR_UNSUPPORTEDALPHAOPERATION:
    return L"D3DERR_UNSUPPORTEDALPHAOPERATION";

  case D3DERR_UNSUPPORTEDALPHAARG:
    return L"D3DERR_UNSUPPORTEDALPHAARG";

  case D3DERR_TOOMANYOPERATIONS:
    return L"D3DERR_TOOMANYOPERATIONS";

  case D3DERR_CONFLICTINGTEXTUREFILTER:
    return L"D3DERR_CONFLICTINGTEXTUREFILTER";

  case D3DERR_UNSUPPORTEDFACTORVALUE:
    return L"D3DERR_UNSUPPORTEDFACTORVALUE";

  case D3DERR_CONFLICTINGRENDERSTATE:
    return L"D3DERR_CONFLICTINGRENDERSTATE";

  case D3DERR_UNSUPPORTEDTEXTUREFILTER:
    return L"D3DERR_UNSUPPORTEDTEXTUREFILTER";

  case D3DERR_CONFLICTINGTEXTUREPALETTE:
    return L"D3DERR_CONFLICTINGTEXTUREPALETTE";

  case D3DERR_DRIVERINTERNALERROR:
    return L"D3DERR_DRIVERINTERNALERROR";


  case D3DERR_NOTFOUND:
    return L"D3DERR_NOTFOUND";

  case D3DERR_MOREDATA:
    return L"D3DERR_MOREDATA";

  case D3DERR_DEVICELOST:
    return L"D3DERR_DEVICELOST";

  case D3DERR_DEVICENOTRESET:
    return L"D3DERR_DEVICENOTRESET";

  case D3DERR_NOTAVAILABLE:
    return L"D3DERR_NOTAVAILABLE";

  case D3DERR_OUTOFVIDEOMEMORY:
    return L"D3DERR_OUTOFVIDEOMEMORY";

  case D3DERR_INVALIDDEVICE:
    return L"D3DERR_INVALIDDEVICE";

  case D3DERR_INVALIDCALL:
    return L"D3DERR_INVALIDCALL";

  case D3DERR_DRIVERINVALIDCALL:
    return L"D3DERR_DRIVERINVALIDCALL";

  case D3DERR_WASSTILLDRAWING:
    return L"D3DERR_WASSTILLDRAWING";


  case D3DOK_NOAUTOGEN:
    return L"D3DOK_NOAUTOGEN";


    /* D3D12 */

    //case D3D12_ERROR_FILE_NOT_FOUND:
    //return L"D3D12_ERROR_FILE_NOT_FOUND";

    //case D3D12_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS:
    //return L"D3D12_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS";

    //case D3D12_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS:
    //return L"D3D12_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS";


    /* Generic (FAILED) */

  case E_FAIL:
    return L"E_FAIL";

  case E_INVALIDARG:
    return L"E_INVALIDARG";

  case E_OUTOFMEMORY:
    return L"E_OUTOFMEMORY";

  case E_NOTIMPL:
    return L"E_NOTIMPL";


  default:
    dll_log.Log (L" *** Encountered unknown HRESULT: (0x%08X)",
      (unsigned long)result);
    return L"UNKNOWN";
  }
}

void
BMF_StartDXGI_1_4_BudgetThread (IDXGIAdapter** ppAdapter)
{
  //
  // If the adapter implements DXGI 1.4, then create a budget monitoring
  //  thread...
  //
  IDXGIAdapter3* pAdapter3;
  if (SUCCEEDED ((*ppAdapter)->QueryInterface (
    __uuidof (IDXGIAdapter3), (void **)&pAdapter3)))
  {
    // We darn sure better not spawn multiple threads!
    EnterCriticalSection (&budget_mutex);

    if (budget_thread == nullptr) {
      // We're going to Release this interface after thread spawnning, but
      //   the running thread still needs a reference counted.
      pAdapter3->AddRef ();

      DWORD WINAPI BudgetThread (LPVOID user_data);

      budget_thread =
        (budget_thread_params_t *)
        HeapAlloc ( dll_heap,
          HEAP_ZERO_MEMORY,
          sizeof (budget_thread_params_t) );

      dll_log.LogEx (true,
        L"   $ Spawning DXGI 1.4 Memory Budget Change Thread.: ");

      budget_thread->pAdapter = pAdapter3;
      budget_thread->tid      = 0;
      budget_thread->event    = 0;
      budget_thread->ready    = false;
      budget_log.silent       = true;

      budget_thread->handle =
        CreateThread (NULL, 0, BudgetThread, (LPVOID)budget_thread,
          0, NULL);

      while (! budget_thread->ready)
        ;

      if (budget_thread->tid != 0) {
        dll_log.LogEx (false, L"tid=0x%04x\n", budget_thread->tid);

        dll_log.LogEx (true,
          L"   %% Setting up Budget Change Notification.........: ");

        HRESULT result =
          pAdapter3->RegisterVideoMemoryBudgetChangeNotificationEvent (
            budget_thread->event, &budget_thread->cookie
            );

        if (SUCCEEDED (result)) {
          dll_log.LogEx (false, L"eid=0x%x, cookie=%u\n",
            budget_thread->event, budget_thread->cookie);
        } else {
          dll_log.LogEx (false, L"Failed! (%s)\n",
            BMF_DescribeHRESULT (result));
        }
      } else {
        dll_log.LogEx (false, L"failed!\n");
      }

      dll_log.LogEx (false, L"\n");
    }

    LeaveCriticalSection (&budget_mutex);

    dll_log.LogEx (true,  L"   [DXGI 1.2]: GPU Scheduling...:"
      L" Pre-Emptive");

    DXGI_ADAPTER_DESC2 desc2;

    bool silent    = dll_log.silent;
    dll_log.silent = true;
    {
      // Don't log this call, because that would be silly...
      pAdapter3->GetDesc2 (&desc2);
    }
    dll_log.silent = silent;

    switch (desc2.GraphicsPreemptionGranularity)
    {
    case DXGI_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY:
      dll_log.LogEx (false, L" (DMA Buffer)\n\n");
      break;
    case DXGI_GRAPHICS_PREEMPTION_PRIMITIVE_BOUNDARY:
      dll_log.LogEx (false, L" (Graphics Primitive)\n\n");
      break;
    case DXGI_GRAPHICS_PREEMPTION_TRIANGLE_BOUNDARY:
      dll_log.LogEx (false, L" (Triangle)\n\n");
      break;
    case DXGI_GRAPHICS_PREEMPTION_PIXEL_BOUNDARY:
      dll_log.LogEx (false, L" (Fragment)\n\n");
      break;
    case DXGI_GRAPHICS_PREEMPTION_INSTRUCTION_BOUNDARY:
      dll_log.LogEx (false, L" (Instruction)\n\n");
      break;
    default:
      dll_log.LogEx (false, L"UNDEFINED\n\n");
      break;
    }

    int i = 0;

    dll_log.LogEx (true,
      L"   [DXGI 1.4]: Local Memory.....:");

    DXGI_QUERY_VIDEO_MEMORY_INFO mem_info;
    while (SUCCEEDED (pAdapter3->QueryVideoMemoryInfo (
      i,
      DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
      &mem_info)
      )
      )
    {
      if (i > 0) {
        dll_log.LogEx (false, L"\n");
        dll_log.LogEx (true,  L"                                 ");
      }

      dll_log.LogEx (false,
        L" Node%i (Reserve: %#5llu / %#5llu MiB - "
        L"Budget: %#5llu / %#5llu MiB)",
        i++,
        mem_info.CurrentReservation      >> 20ULL,
        mem_info.AvailableForReservation >> 20ULL,
        mem_info.CurrentUsage            >> 20ULL,
        mem_info.Budget                  >> 20ULL);

      pAdapter3->SetVideoMemoryReservation (
        (i - 1),
        DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
        (i == 1 || USE_SLI) ? 
        uint64_t (mem_info.AvailableForReservation *
          config.mem.reserve * 0.01f) :
        0
        );
    }
    dll_log.LogEx (false, L"\n");

    i = 0;

    dll_log.LogEx (true,
      L"   [DXGI 1.4]: Non-Local Memory.:");

    while (SUCCEEDED (pAdapter3->QueryVideoMemoryInfo (
      i,
      DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
      &mem_info )
      )
      )
    {
      if (i > 0) {
        dll_log.LogEx (false, L"\n");
        dll_log.LogEx (true,  L"                                 ");
      }
      dll_log.LogEx (false,
        L" Node%i (Reserve: %#5llu / %#5llu MiB - "
        L"Budget: %#5llu / %#5llu MiB)",
        i++,
        mem_info.CurrentReservation      >> 20ULL,
        mem_info.AvailableForReservation >> 20ULL,
        mem_info.CurrentUsage            >> 20ULL,
        mem_info.Budget                  >> 20ULL );

      pAdapter3->SetVideoMemoryReservation (
        (i - 1),
        DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
        (i == 1 || USE_SLI) ?
        uint64_t (mem_info.AvailableForReservation * 
          config.mem.reserve * 0.01f) :
        0
        );
    }

    ::mem_info [0].nodes = i-1;
    ::mem_info [1].nodes = i-1;

    dll_log.LogEx (false, L"\n");

    pAdapter3->Release ();
  }
}

#include <ctime>
#define min_max(ref,min,max) if ((ref) > (max)) (max) = (ref); \
                             if ((ref) < (min)) (min) = (ref);

const uint32_t BUDGET_POLL_INTERVAL = 66UL; // How often to sample the budget
                                            //  in msecs

DWORD
WINAPI BudgetThread (LPVOID user_data)
{
  budget_thread_params_t* params =
    (budget_thread_params_t *)user_data;

  if (budget_log.init ("logs/dxgi_budget.log", "w")) {
    params->tid       = GetCurrentThreadId ();
    params->event     = CreateEvent (NULL, FALSE, FALSE, L"DXGIMemoryBudget");
    budget_log.silent = true;
    params->ready     = true;
  } else {
    params->tid    = 0;
    params->ready  = true; // Not really :P
    return -1;
  }

  HANDLE hThreadHeap = HeapCreate (0, 0, 0);

  while (params->ready) {
    if (params->event == 0)
      break;

    DWORD dwWaitStatus = WaitForSingleObject (params->event,
      BUDGET_POLL_INTERVAL);

    if (! params->ready) {
      ResetEvent (params->event);
      break;
    }

    buffer_t buffer = mem_info [0].buffer;

    if (buffer == Front)
      buffer = Back;
    else
      buffer = Front;

    GetLocalTime (&mem_info [buffer].time);

    int node = 0;

    while (node < MAX_GPU_NODES &&
      SUCCEEDED (params->pAdapter->QueryVideoMemoryInfo (
        node,
        DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
        &mem_info [buffer].local [node++] )
        )
      ) ;

    node = 0;

    while (node < MAX_GPU_NODES &&
      SUCCEEDED (params->pAdapter->QueryVideoMemoryInfo (
        node,
        DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
        &mem_info [buffer].nonlocal [node++] )
        )
      ) ;

    int nodes = node - 1;

    // Set the number of SLI/CFX Nodes
    mem_info [buffer].nodes = nodes;

#if 0
    static time_t last_flush = time (NULL);
    static time_t last_empty = time (NULL);
    static uint64_t last_budget =
      mem_info [buffer].local [0].Budget;
    static bool queued_flush = false;
    if (dwWaitStatus == WAIT_OBJECT_0 && last_budget >
      mem_info [buffer].local [0].Budget)
      queued_flush = true;
    if (FlushAllCaches != nullptr
      && (last_budget > mem_info [buffer].local [0].Budget && time (NULL) -
        last_flush > 2 ||
        (queued_flush && time (NULL) - last_flush > 2)
        )
      )
    {
      bool silence = budget_log.silent;
      budget_log.silent = false;
      if (last_budget > mem_info [buffer].local [0].Budget) {
        budget_log.Log (
          L"Flushing caches because budget shrunk... (%05u MiB --> %05u MiB)",
          last_budget >> 20ULL,
          mem_info [buffer].local [0].Budget >> 20ULL);
      } else {
        budget_log.Log (
          L"Flushing caches due to deferred budget shrink... (%u second(s))",
          2);
      }
      SetSystemFileCacheSize (-1, -1, NULL);
      FlushAllCaches (StreamMgr);
      last_flush = time (NULL);
      budget_log.Log (L" >> Compacting Process Heap...");
      HANDLE hHeap = GetProcessHeap ();
      HeapCompact (hHeap, 0);
      struct heap_opt_t {
        DWORD version;
        DWORD length;
      } heap_opt;`

        heap_opt.version = 1;
      heap_opt.length  = sizeof (heap_opt_t);
      HeapSetInformation (NULL, (_HEAP_INFORMATION_CLASS)3, &heap_opt,
        heap_opt.length);
      budget_log.silent = silence;
      queued_flush = false;
    }
#endif

    static uint64_t last_budget =
      mem_info [buffer].local [0].Budget;

    if (dwWaitStatus == WAIT_OBJECT_0 && config.load_balance.use)
    {
      INT prio = 0;

#if 0
      if (g_pDXGIDev != nullptr &&
        SUCCEEDED (g_pDXGIDev->GetGPUThreadPriority (&prio)))
      {
        if (last_budget > mem_info [buffer].local [0].Budget &&
          mem_info [buffer].local [0].CurrentUsage >
          mem_info [buffer].local [0].Budget)
        {
          if (prio > -7)
          {
            g_pDXGIDev->SetGPUThreadPriority (--prio);
          }
        }

        else if (last_budget < mem_info [buffer].local [0].Budget &&
          mem_info [buffer].local [0].CurrentUsage <
          mem_info [buffer].local [0].Budget)
        {
          if (prio < 7)
          {
            g_pDXGIDev->SetGPUThreadPriority (++prio);
          }
        }
      }
#endif

      last_budget = mem_info [buffer].local [0].Budget;
    }

    if (nodes > 0) {
      int i = 0;

      budget_log.LogEx (true, L"   [DXGI 1.4]: Local Memory.....:");

      while (i < nodes) {
        if (dwWaitStatus == WAIT_OBJECT_0)
          mem_stats [i].budget_changes++;

        if (i > 0) {
          budget_log.LogEx (false, L"\n");
          budget_log.LogEx (true,  L"                                 ");
        }

        budget_log.LogEx (false,
          L" Node%i (Reserve: %#5llu / %#5llu MiB - "
          L"Budget: %#5llu / %#5llu MiB)",
          i,
          mem_info [buffer].local [i].CurrentReservation      >> 20ULL,
          mem_info [buffer].local [i].AvailableForReservation >> 20ULL,
          mem_info [buffer].local [i].CurrentUsage            >> 20ULL,
          mem_info [buffer].local [i].Budget                  >> 20ULL);

        min_max (mem_info [buffer].local [i].AvailableForReservation,
          mem_stats [i].min_avail_reserve,
          mem_stats [i].max_avail_reserve);

        min_max (mem_info [buffer].local [i].CurrentReservation,
          mem_stats [i].min_reserve,
          mem_stats [i].max_reserve);

        min_max (mem_info [buffer].local [i].CurrentUsage,
          mem_stats [i].min_usage,
          mem_stats [i].max_usage);

        min_max (mem_info [buffer].local [i].Budget,
          mem_stats [i].min_budget,
          mem_stats [i].max_budget);

        if (mem_info [buffer].local [i].CurrentUsage >
          mem_info [buffer].local [i].Budget) {
          uint64_t over_budget =
            mem_info [buffer].local [i].CurrentUsage -
            mem_info [buffer].local [i].Budget;

          min_max (over_budget, mem_stats [i].min_over_budget,
            mem_stats [i].max_over_budget);
        }

        i++;
      }
      budget_log.LogEx (false, L"\n");

      i = 0;

      budget_log.LogEx (true,
        L"   [DXGI 1.4]: Non-Local Memory.:");

      while (i < nodes) {
        if (i > 0) {
          budget_log.LogEx (false, L"\n");
          budget_log.LogEx (true,  L"                                 ");
        }

        budget_log.LogEx (false,
          L" Node%i (Reserve: %#5llu / %#5llu MiB - "
          L"Budget: %#5llu / %#5llu MiB)",
          i,
          mem_info [buffer].nonlocal [i].CurrentReservation      >> 20ULL,
          mem_info [buffer].nonlocal [i].AvailableForReservation >> 20ULL,
          mem_info [buffer].nonlocal [i].CurrentUsage            >> 20ULL,
          mem_info [buffer].nonlocal [i].Budget                  >> 20ULL);
        i++;
      }

#if 0
      if (g_pDXGIDev != nullptr)
      {
        if (config.load_balance.use)
        {
          if (SUCCEEDED (g_pDXGIDev->GetGPUThreadPriority (&gpu_prio)))
          {
          }
        } else {
          if (gpu_prio != 0) {
            gpu_prio = 0;
            g_pDXGIDev->SetGPUThreadPriority (gpu_prio);
          }
        }
      }
#endif

      budget_log.LogEx (false, L"\n");
    }

    if (params->event != 0)
      ResetEvent (params->event);

    mem_info [0].buffer = buffer;
  }

#if 0
  if (g_pDXGIDev != nullptr) {
    // Releasing this actually causes driver crashes, so ...
    //   let it leak, what do we care?
    //ULONG refs = g_pDXGIDev->AddRef ();
    //if (refs > 2)
    //g_pDXGIDev->Release ();
    //g_pDXGIDev->Release   ();

#ifdef ALLOW_DEVICE_TRANSITION
    g_pDXGIDev->Release ();
#endif
    g_pDXGIDev = nullptr;
  }
#endif

  if (hThreadHeap != 0)
    HeapDestroy (hThreadHeap);

  return 0;
}


// Stupid solution for games that inexplicibly draw to the screen
//   without ever swapping buffers.
DWORD
WINAPI
osd_pump (LPVOID lpThreadParam)
{
  while (true) {
    Sleep ((DWORD)(config.osd.pump_interval * 1000.0f));
    BMF_EndBufferSwap (S_OK, nullptr);
  }

  return 0;
}


void
BMF_InitCore (const wchar_t* backend, void* callback)
{
  EnterCriticalSection (&init_mutex);
  if (backend_dll != NULL) {
    LeaveCriticalSection (&init_mutex);
    return;
  }

  char log_fname [MAX_PATH];
  log_fname [MAX_PATH - 1] = '\0';

  sprintf (log_fname, "logs/%ws.log", backend);

  dll_log.init (log_fname, "w");
  dll_log.Log  (L"%s.log created", backend);

  dll_log.LogEx (false,
    L"------------------------------------------------------------------------"
    L"-----------\n");

  extern bool BMF_InitCOM (void);
  BMF_InitCOM ();

  DWORD   dwProcessSize = MAX_PATH;
  wchar_t wszProcessName [MAX_PATH];

  HANDLE hProc = GetCurrentProcess ();

  QueryFullProcessImageName (hProc, 0, wszProcessName, &dwProcessSize);

  wchar_t* pwszShortName = wszProcessName + lstrlenW (wszProcessName);

  while (  pwszShortName      >  wszProcessName &&
    *(pwszShortName - 1) != L'\\')
    --pwszShortName;

  host_app = pwszShortName;

  dll_log.Log (L">> (%s) <<", pwszShortName);

  if (! lstrcmpW (pwszShortName, L"BatmanAK.exe"))
    USE_SLI = false;

  dll_log.LogEx (false,
    L"----------------------------------------------------------------------"
    L"-------------\n");

  wchar_t wszBackendDLL [MAX_PATH] = { L'\0' };
#ifdef _WIN64
  GetSystemDirectory (wszBackendDLL, MAX_PATH);
#else
  BOOL bWOW64;
  ::IsWow64Process (hProc, &bWOW64);

  if (bWOW64)
    GetSystemWow64Directory (wszBackendDLL, MAX_PATH);
  else
    GetSystemDirectory (wszBackendDLL, MAX_PATH);
#endif

  dll_log.Log (L" System Directory:           %s", wszBackendDLL);

  lstrcatW (wszBackendDLL, L"\\");
  lstrcatW (wszBackendDLL, backend);
  lstrcatW (wszBackendDLL, L".dll");

  dll_log.LogEx (true, L" Loading default %s.dll: ", backend);

  backend_dll = LoadLibrary (wszBackendDLL);

  if (backend_dll != NULL)
    dll_log.LogEx (false, L" (%s)\n", wszBackendDLL);
  else
    dll_log.LogEx (false, L" FAILED (%s)!\n", wszBackendDLL);

  dll_log.LogEx (false,
    L"----------------------------------------------------------------------"
    L"-------------\n");


  dll_log.LogEx (true, L"Loading user preferences from %s.ini... ", backend);
  if (BMF_LoadConfig (backend)) {
    dll_log.LogEx (false, L"done!\n\n");
  } else {
    dll_log.LogEx (false, L"failed!\n");
    // If no INI file exists, write one immediately.
    dll_log.LogEx (true, L"  >> Writing base INI file, because none existed... ");
    BMF_SaveConfig (backend);
    dll_log.LogEx (false, L"done!\n\n");
  }


  // Hard-code the AppID for ToZ
  if (! lstrcmpW (pwszShortName, L"Tales of Zestiria.exe"))
    config.steam.appid = 351970;


  // Load user-defined DLLs (Early)
#ifdef _WIN64
  BMF_LoadEarlyImports64 ();
#else
  BMF_LoadEarlyImports32 ();
#endif

  // Start Steam EARLY, so that it can hook into everything at an opportune
  //   time.
  if (BMF::SteamAPI::AppID () == 0)
  {
    // Module was already loaded... yay!
#ifdef _WIN64
    if (GetModuleHandle (L"steam_api64.dll"))
#else
    if (GetModuleHandle (L"steam_api.dll"))
#endif
      BMF::SteamAPI::Init (true);
    else
      BMF::SteamAPI::Init (false);
  }

  if (config.system.silent) {
    dll_log.silent = true;

    std::wstring log_fnameW (backend);
    log_fnameW += L".log";

    DeleteFile     (log_fnameW.c_str ());
  } else {
    dll_log.silent = false;
  }

  dll_log.LogEx (true, L"Initializing NvAPI: ");

  nvapi_init = bmf::NVAPI::InitializeLibrary ();

  dll_log.LogEx (false, L" %s\n\n", nvapi_init ? L"Success" : L"Failed");

  if (nvapi_init) {
    int num_sli_gpus = bmf::NVAPI::CountSLIGPUs ();

    dll_log.Log (L" >> NVIDIA Driver Version: %s",
      bmf::NVAPI::GetDriverVersion ().c_str ());

    dll_log.Log (L"  * Number of Installed NVIDIA GPUs: %i "
      L"(%i are in SLI mode)",
      bmf::NVAPI::CountPhysicalGPUs (), num_sli_gpus);

    if (num_sli_gpus > 0) {
      dll_log.LogEx (false, L"\n");

      DXGI_ADAPTER_DESC* sli_adapters =
        bmf::NVAPI::EnumSLIGPUs ();

      int sli_gpu_idx = 0;

      while (*sli_adapters->Description != L'\0') {
        dll_log.Log ( L"   + SLI GPU %d: %s",
          sli_gpu_idx++,
          (sli_adapters++)->Description );
      }
    }

    dll_log.LogEx (false, L"\n");
  }

  HMODULE hMod = GetModuleHandle (pwszShortName);

  if (hMod != NULL) {
    DWORD* dwOptimus = (DWORD *)GetProcAddress (hMod, "NvOptimusEnablement");

    if (dwOptimus != NULL) {
      dll_log.Log (L"  NvOptimusEnablement..................: 0x%02X (%s)",
        *dwOptimus,
        (*dwOptimus & 0x1 ? L"Max Perf." :
          L"Don't Care"));
    } else {
      dll_log.Log (L"  NvOptimusEnablement..................: UNDEFINED");
    }

    DWORD* dwPowerXpress =
      (DWORD *)GetProcAddress (hMod, "AmdPowerXpressRequestHighPerformance");

    if (dwPowerXpress != NULL) {
      dll_log.Log (L"  AmdPowerXpressRequestHighPerformance.: 0x%02X (%s)",
        *dwPowerXpress,
        (*dwPowerXpress & 0x1 ? L"High Perf." :
          L"Don't Care"));
    }
    else
      dll_log.Log (L"  AmdPowerXpressRequestHighPerformance.: UNDEFINED");

    dll_log.LogEx (false, L"\n");
  }

  MH_STATUS WINAPI BMF_Init_MinHook (void);
  BMF_Init_MinHook ();

  typedef void (WINAPI *callback_t)(void);
  callback_t callback_fn = (callback_t)callback;
  callback_fn ();

  dll_log.LogEx (true, L" @ Loading Debug Symbols: ");

  SymInitializeW       (GetCurrentProcess (), NULL, TRUE);
  SymRefreshModuleList (GetCurrentProcess ());

  dll_log.LogEx (false, L"done!\n");

  dll_log.Log (L"=== Initialization Finished! ===\n");

  //
  // Spawn CPU Refresh Thread
  //
  if (cpu_stats.hThread == 0) {
    dll_log.LogEx (true, L" [WMI] Spawning CPU Monitor...      ");
    cpu_stats.hThread = CreateThread (NULL, 0, BMF_MonitorCPU, NULL, 0, NULL);
    if (cpu_stats.hThread != 0)
      dll_log.LogEx (false, L"tid=0x%04x\n", GetThreadId (cpu_stats.hThread));
    else
      dll_log.LogEx (false, L"Failed!\n");
  }

  Sleep (90);

  if (disk_stats.hThread == 0) {
    dll_log.LogEx (true, L" [WMI] Spawning Disk Monitor...     ");
    disk_stats.hThread =
      CreateThread (NULL, 0, BMF_MonitorDisk, NULL, 0, NULL);
    if (disk_stats.hThread != 0)
      dll_log.LogEx (false, L"tid=0x%04x\n", GetThreadId (disk_stats.hThread));
    else
      dll_log.LogEx (false, L"failed!\n");
  }

  Sleep (90);

  if (pagefile_stats.hThread == 0) {
    dll_log.LogEx (true, L" [WMI] Spawning Pagefile Monitor... ");
    pagefile_stats.hThread =
      CreateThread (NULL, 0, BMF_MonitorPagefile, NULL, 0, NULL);
    if (pagefile_stats.hThread != 0)
      dll_log.LogEx (false, L"tid=0x%04x\n",
        GetThreadId (pagefile_stats.hThread));
    else
      dll_log.LogEx (false, L"failed!\n");
  }

  Sleep (90);

  //
  // Spawn Process Monitor Thread
  //
  if (process_stats.hThread == 0) {
    dll_log.LogEx (true, L" [WMI] Spawning Process Monitor...  ");
    process_stats.hThread = CreateThread (NULL, 0, BMF_MonitorProcess, NULL, 0, NULL);
    if (process_stats.hThread != 0)
      dll_log.LogEx (false, L"tid=0x%04x\n", GetThreadId (process_stats.hThread));
    else
      dll_log.LogEx (false, L"Failed!\n");
  }

  dll_log.LogEx (false, L"\n");

  // Create a thread that pumps the OSD
  if (config.osd.pump) {
    dll_log.LogEx (true, L" [OSD] Spawning Pump Thread...      ");
    hPumpThread = CreateThread (NULL, 0, osd_pump, NULL, 0, NULL);
    if (hPumpThread != nullptr)
      dll_log.LogEx (false, L"tid=0x%04x, interval=%04.01f ms\n",
                       hPumpThread, config.osd.pump_interval * 1000.0f);
    else
      dll_log.LogEx (false, L"failed!\n");
  }

  dll_log.LogEx (false, L"\n");

  szOSD =
    (char *)
    HeapAlloc ( dll_heap,
      HEAP_ZERO_MEMORY,
      sizeof (char) * 16384 );

  LeaveCriticalSection (&init_mutex);
}



void
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


struct init_params_t {
  const wchar_t* backend;
  void*          callback;
};

DWORD
WINAPI DllThread (LPVOID user)
{
  EnterCriticalSection (&init_mutex);
  {
    init_params_t* params = (init_params_t *)user;

    BMF_InitCore (params->backend, params->callback);

    HeapFree (dll_heap, 0, params);
  }
  LeaveCriticalSection (&init_mutex);

  return 0;
}


typedef DECLSPEC_IMPORT HMODULE (WINAPI *LoadLibraryA_t)(LPCSTR  lpFileName);
typedef DECLSPEC_IMPORT HMODULE (WINAPI *LoadLibraryW_t)(LPCWSTR lpFileName);

LoadLibraryA_t LoadLibraryA_Original = nullptr;
LoadLibraryW_t LoadLibraryW_Original = nullptr;

HMODULE
WINAPI
LoadLibraryA_Detour (LPCSTR lpFileName)
{
  if (lpFileName == nullptr)
    return NULL;

  HMODULE hMod = LoadLibraryA_Original (lpFileName);

  if (strstr (lpFileName, "steam_api") ||
      strstr (lpFileName, "SteamworksNative")) {
    //BMF::SteamAPI::Init (false);
  }

  return hMod;
}

HMODULE
WINAPI
LoadLibraryW_Detour (LPCWSTR lpFileName)
{
  if (lpFileName == nullptr)
    return NULL;

  HMODULE hMod = LoadLibraryW_Original (lpFileName);

  if (wcsstr (lpFileName, L"steam_api") ||
      wcsstr (lpFileName, L"SteamworksNative")) {
    //BMF::SteamAPI::Init (false);
  }

  return hMod;
}


MH_STATUS
WINAPI
BMF_CreateFuncHook ( LPCWSTR pwszFuncName,
                     LPVOID  pTarget,
                     LPVOID  pDetour,
                     LPVOID *ppOriginal )
{
  MH_STATUS status =
    MH_CreateHook ( pTarget,
                      pDetour,
                        ppOriginal );

  if (status != MH_OK) {
    dll_log.Log ( L" [ MinHook ] Failed to Install Hook for '%s' "
                  L"[Address: %04Xh]!  (Status: \"%hs\")",
                    pwszFuncName,
                      pTarget,
                        MH_StatusToString (status) );
  }

  return status;
}

MH_STATUS
WINAPI
BMF_CreateDLLHook ( LPCWSTR pwszModule, LPCSTR  pszProcName,
                    LPVOID  pDetour,    LPVOID *ppOriginal,
                    LPVOID *ppFuncAddr )
{
#if 1
  HMODULE hMod = GetModuleHandle (pwszModule);

  if (hMod == NULL) {
    if (LoadLibraryW_Original != nullptr) {
      hMod = LoadLibraryW_Original (pwszModule);
    } else {
      hMod = LoadLibraryW (pwszModule);
    }
  }

  if (hMod == 0)
    return MH_ERROR_MODULE_NOT_FOUND;

  LPVOID pFuncAddr =
    GetProcAddress (hMod, pszProcName);

  MH_STATUS status =
    MH_CreateHook ( pFuncAddr,
                      pDetour,
                        ppOriginal );
#else
  MH_STATUS status =
    MH_CreateHookApi ( pwszModule,
                         pszProcName,
                           pDetour,
                             ppOriginal );
#endif

  if (status != MH_OK) {
    dll_log.Log ( L" [ MinHook ] Failed to Install Hook for: '%hs' in '%s'! "
                  L"(Status: \"%hs\")",
                    pszProcName,
                      pwszModule,
                        MH_StatusToString (status) );
  }
  else if (ppFuncAddr != nullptr)
    *ppFuncAddr = pFuncAddr;

  return status;
}

MH_STATUS
WINAPI
BMF_EnableHook (LPVOID pTarget)
{
  MH_STATUS status =
    MH_EnableHook (pTarget);

  if (status != MH_OK) {
    if (pTarget != MH_ALL_HOOKS) {
      dll_log.Log(L" [ MinHook ] Failed to Enable Hook with Address: %04Xh!"
                  L" (Status: \"%hs\")",
                    pTarget,
                      MH_StatusToString (status) );
    } else {
      dll_log.Log ( L" [ MinHook ] Failed to Enable All Hooks! "
                    L"(Status: \"%hs\")",
                      MH_StatusToString (status) );
    }
  }

  return status;
}

MH_STATUS
WINAPI
BMF_DisableHook (LPVOID pTarget)
{
  MH_STATUS status =
    MH_DisableHook (pTarget);

  if (status != MH_OK) {
    if (pTarget != MH_ALL_HOOKS) {
      dll_log.Log(L" [ MinHook ] Failed to Disable Hook with Address: %04Xh!"
                  L" (Status: \"%hs\")",
                    pTarget,
                      MH_StatusToString (status));
    } else {
      dll_log.Log ( L" [ MinHook ] Failed to Disable All Hooks! "
                    L"(Status: \"%hs\")",
                      MH_StatusToString (status) );
    }
  }

  return status;
}

MH_STATUS
WINAPI
BMF_RemoveHook (LPVOID pTarget)
{
  MH_STATUS status =
    MH_RemoveHook (pTarget);

  if (status != MH_OK) {
    dll_log.Log ( L" [ MinHook ] Failed to Remove Hook with Address: %04Xh! "
                  L"(Status: \"%hs\")",
                    pTarget,
                      MH_StatusToString (status) );
  }

  return status;
}

MH_STATUS
WINAPI
BMF_Init_MinHook (void)
{
  MH_STATUS status;

  if ((status = MH_Initialize ()) != MH_OK) {
    dll_log.Log ( L" [ MinHook ] Failed to Initialize MinHook Library! "
                  L"(Status: \"%hs\")",
                    MH_StatusToString (status) );
  }

  //
  // Hook LoadLibrary so that we can watch for things like steam_api*.dll
  //
  BMF_CreateDLLHook ( L"kernel32.dll",
                        "LoadLibraryA",
                          LoadLibraryA_Detour,
                            (LPVOID *)&LoadLibraryA_Original );

  BMF_CreateDLLHook ( L"kernel32.dll",
                        "LoadLibraryW",
                          LoadLibraryW_Detour,
                            (LPVOID *)&LoadLibraryW_Original );

  BMF_EnableHook (MH_ALL_HOOKS);

  return status;
}

MH_STATUS
WINAPI
BMF_UnInit_MinHook (void)
{
  MH_STATUS status;

  if ((status = MH_Uninitialize ()) != MH_OK) {
    dll_log.Log ( L" [ MinHook ] Failed to Uninitialize MinHook Library! "
                  L"(Status: \"%hs\")",
                    MH_StatusToString (status) );
  }

  return status;
}



bool
BMF_StartupCore (const wchar_t* backend, void* callback)
{
  dll_heap = HeapCreate (HEAP_CREATE_ENABLE_EXECUTE, 0, 0);

  if (! dll_heap)
    return false;

  InitializeCriticalSectionAndSpinCount (&budget_mutex,  4000);
  InitializeCriticalSectionAndSpinCount (&init_mutex,    50000);

  init_params_t *init =
    (init_params_t *)HeapAlloc ( dll_heap,
                                   HEAP_ZERO_MEMORY,
                                     sizeof (init_params_t) );

  if (! init)
    return false;

  init->backend  = backend;
  init->callback = callback;

  hInitThread = CreateThread (NULL, 0, DllThread, init, 0, NULL);

  // Give other DXGI hookers time to queue up before processing any calls
  //   that they make. But don't wait here infinitely, or we will deadlock!

  /* Default: 0.25 secs seems adequate */
  if (hInitThread != 0)
    WaitForSingleObject (hInitThread, config.system.init_delay);

  return true;
}

bool
BMF_ShutdownCore (const wchar_t* backend)
{
  BMF_AutoClose_Log (budget_log);
  BMF_AutoClose_Log (  dll_log );

  if (hPumpThread != 0) {
    dll_log.LogEx   (true, L" [OSD] Shutting down Pump Thread... ");

    TerminateThread (hPumpThread, 0);
    hPumpThread = 0;

    dll_log.LogEx   (false, L"done!\n");
  }

  dll_log.LogEx  (true,
    L"[RTSS] Closing RivaTuner Statistics Server connection... ");
  // Shutdown the OSD as early as possible to avoid complications
  BMF_ReleaseOSD ();
  dll_log.LogEx  (false, L"done!\n");

  if (budget_thread != nullptr) {
    config.load_balance.use = false; // Turn this off while shutting down

    dll_log.LogEx (
      true,
        L"[DXGI] Shutting down DXGI 1.4 Memory Budget Change Thread... "
    );

    budget_thread->ready = false;

    DWORD dwWaitState =
      SignalObjectAndWait (budget_thread->event, budget_thread->handle,
                           1000UL, TRUE); // Give 1 second, and
                                          // then we're killing
                                          // the thing!

    if (dwWaitState == WAIT_OBJECT_0)
      dll_log.LogEx (false, L"done!\n");
    else {
      dll_log.LogEx (false, L"timed out (killing manually)!\n");
      TerminateThread (budget_thread->handle, 0);
    }

    // Record the final statistics always
    budget_log.silent = false;

    budget_log.Log   (L"--------------------");
    budget_log.Log   (L"Shutdown Statistics:");
    budget_log.Log   (L"--------------------\n");

    // in %10u seconds\n",
    budget_log.Log (L" Memory Budget Changed %llu times\n",
      mem_stats [0].budget_changes);

    for (int i = 0; i < 4; i++) {
      if (mem_stats [i].max_usage > 0) {
        if (mem_stats [i].min_reserve == UINT64_MAX)
          mem_stats [i].min_reserve = 0ULL;

        if (mem_stats [i].min_over_budget == UINT64_MAX)
          mem_stats [i].min_over_budget = 0ULL;

        budget_log.LogEx (true, L" GPU%i: Min Budget:        %05llu MiB\n", i,
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

  if (process_stats.hThread != 0) {
    dll_log.LogEx (true,L" [WMI] Shutting down Process Monitor... ");
    // Signal the thread to shutdown
    process_stats.lID = 0;
    WaitForSingleObject
      (process_stats.hThread, 1000UL); // Give 1 second, and
                                       // then we're killing
                                       // the thing!
    TerminateThread (process_stats.hThread, 0);
    process_stats.hThread  = 0;
    dll_log.LogEx (false, L"done!\n");
  }

  if (cpu_stats.hThread != 0) {
    dll_log.LogEx (true,L" [WMI] Shutting down CPU Monitor... ");
    // Signal the thread to shutdown
    cpu_stats.lID = 0;
    WaitForSingleObject (cpu_stats.hThread, 1000UL); // Give 1 second, and
                                                     // then we're killing
                                                     // the thing!
    TerminateThread (cpu_stats.hThread, 0);
    cpu_stats.hThread  = 0;
    cpu_stats.num_cpus = 0;
    dll_log.LogEx (false, L"done!\n");
  }

  if (disk_stats.hThread != 0) {
    dll_log.LogEx (true, L" [WMI] Shutting down Disk Monitor... ");
    // Signal the thread to shutdown
    disk_stats.lID = 0;
    WaitForSingleObject (disk_stats.hThread, 1000UL); // Give 1 second, and
                                                      // then we're killing
                                                      // the thing!
    TerminateThread (disk_stats.hThread, 0);
    disk_stats.hThread   = 0;
    disk_stats.num_disks = 0;
    dll_log.LogEx (false, L"done!\n");
  }

  if (pagefile_stats.hThread != 0) {
    dll_log.LogEx (true, L" [WMI] Shutting down Pagefile Monitor... ");
    // Signal the thread to shutdown
    pagefile_stats.lID = 0;
    WaitForSingleObject (
      pagefile_stats.hThread, 1000UL); // Give 1 second, and
                                       // then we're killing
                                       // the thing!
    TerminateThread (pagefile_stats.hThread, 0);
    pagefile_stats.hThread       = 0;
    pagefile_stats.num_pagefiles = 0;
    dll_log.LogEx (false, L"done!\n");
  }

  dll_log.LogEx  (true, L"Saving user preferences to %s.ini... ", backend);
  BMF_SaveConfig (backend);
  dll_log.LogEx  (false, L"done!\n");

  dll_log.LogEx  (true, L"Shutting down Steam API... ");
  BMF::SteamAPI::Shutdown ();
  dll_log.LogEx  (false, L"done!\n");

  // Hopefully these things are done by now...
  DeleteCriticalSection (&init_mutex);
  DeleteCriticalSection (&budget_mutex);

/////  BMF_UnInit_MinHook ();

  if (nvapi_init)
    bmf::NVAPI::UnloadLibrary ();

  dll_log.Log (L"Custom %s.dll Detached (pid=0x%04x)",
    backend, GetCurrentProcessId ());

  HeapDestroy (dll_heap);

  SymCleanup (GetCurrentProcess ());

  return true;
}



COM_DECLSPEC_NOTHROW
void
STDMETHODCALLTYPE
BMF_BeginBufferSwap (void)
{
  static int import_tries = 0;

  // Load user-defined DLLs (Late)
#ifdef _WIN64
  BMF_LoadLateImports64 ();
#else
  BMF_LoadLateImports32 ();
#endif

  if (BMF::SteamAPI::AppID () == 0 && import_tries++ < 5)
  {
    // Module was already loaded... yay!
#ifdef _WIN64
    if (GetModuleHandle (L"steam_api64.dll"))
#else
    if (GetModuleHandle (L"steam_api.dll"))
#endif
    {
      BMF::SteamAPI::Init (true);
    } else {
      //
      // YIKES, Steam's still not loaded?!
      //
      //   ** This probably is not a SteamWorks game...
      //
      BMF::SteamAPI::Init (false);
    }
  }
}

extern void BMF_UnlockSteamAchievement (int idx);

COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
BMF_EndBufferSwap (HRESULT hr, IUnknown* device)
{
  // Draw after present, this may make stuff 1 frame late, but... it
  //   helps with VSYNC performance.

  // Treat resize and obscured statuses as failures; DXGI does not, but
  //  we should not draw the OSD when these events happen.
  if (/*FAILED (hr)*/ hr != S_OK)
    return hr;

  // Early-out if the OSD is not functional
  if (! BMF_DrawOSD ())
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
    if (! toggle_time) {
      BMF_UnlockSteamAchievement (0);

      config.time.show = (! config.time.show);
    }
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

  if (config.sli.show && device != nullptr)
  {
    // Get SLI status for the frame we just displayed... this will show up
    //   one frame late, but this is the safest approach.
    if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
      sli_state = bmf::NVAPI::GetSLIState (device);
    }
  }

  //BMF::SteamAPI::Pump ();

  return hr;
}