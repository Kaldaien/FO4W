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

#include "stdafx.h"
#include "nvapi.h"
#include "config.h"

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

#include "minhook/MinHook.h"
#include "osd.h"
#include "io_monitor.h"

static HANDLE           dll_heap      = { 0 };

static CRITICAL_SECTION d3dhook_mutex = { 0 };
static CRITICAL_SECTION budget_mutex  = { 0 };
static CRITICAL_SECTION init_mutex    = { 0 };

// Disable SLI memory in Batman Arkham Knight
static bool USE_SLI = true;

NV_GET_CURRENT_SLI_STATE sli_state;
BOOL                     nvapi_init = FALSE;
int                      gpu_prio;

static ID3D11Device* g_pD3D11Dev;

static IDXGIDevice*  g_pDXGIDev;

extern "C" {
  // We have some really sneaky overlays that manage to call some of our
  //   exported functions before the DLL's even attached -- make them wait,
  //     so we don't crash and burn!
  void WaitForInit (void);

#undef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE __stdcall

typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory2_t) \
  (UINT Flags, REFIID riid,  void** ppFactory);
typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory1_t) \
              (REFIID riid,  void** ppFactory);
typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory_t)  \
              (REFIID riid,  void** ppFactory);

typedef HRESULT (WINAPI *D3D11CreateDeviceAndSwapChain_t)(
    _In_opt_                             IDXGIAdapter*, 
                                         D3D_DRIVER_TYPE,
                                         HMODULE,
                                         UINT, 
    _In_reads_opt_ (FeatureLevels) CONST D3D_FEATURE_LEVEL*, 
                                         UINT FeatureLevels,
                                         UINT,
    _In_opt_                       CONST DXGI_SWAP_CHAIN_DESC*,
    _Out_opt_                            IDXGISwapChain**,
    _Out_opt_                            ID3D11Device**, 
    _Out_opt_                            D3D_FEATURE_LEVEL*,
    _Out_opt_                            ID3D11DeviceContext**);


typedef HRESULT (STDMETHODCALLTYPE *PresentSwapChain_t)(
                  IDXGISwapChain *This,
                  UINT            SyncInterval,
                  UINT            Flags);

typedef HRESULT (STDMETHODCALLTYPE *CreateSwapChain_t)(
                  IDXGIFactory          *This,
            _In_  IUnknown              *pDevice,
            _In_  DXGI_SWAP_CHAIN_DESC  *pDesc,
           _Out_  IDXGISwapChain       **ppSwapChain);


typedef HRESULT (STDMETHODCALLTYPE *GetDesc1_t)(IDXGIAdapter1      *This,
                                         _Out_  DXGI_ADAPTER_DESC1 *pDesc);
typedef HRESULT (STDMETHODCALLTYPE *GetDesc2_t)(IDXGIAdapter2      *This,
                                         _Out_  DXGI_ADAPTER_DESC2 *pDesc);
typedef HRESULT (STDMETHODCALLTYPE *GetDesc_t) (IDXGIAdapter       *This,
                                         _Out_  DXGI_ADAPTER_DESC  *pDesc);

typedef HRESULT (STDMETHODCALLTYPE *EnumAdapters_t)(
        IDXGIFactory  *This,
        UINT           Adapter,
  _Out_ IDXGIAdapter **ppAdapter);

typedef HRESULT (STDMETHODCALLTYPE *EnumAdapters1_t)(
        IDXGIFactory1  *This,
        UINT            Adapter,
  _Out_ IDXGIAdapter1 **ppAdapter);

volatile
D3D11CreateDeviceAndSwapChain_t
D3D11CreateDeviceAndSwapChain_Import = nullptr;

PresentSwapChain_t   Present_Original          = nullptr;
CreateSwapChain_t    CreateSwapChain_Original  = nullptr;

GetDesc_t            GetDesc_Original          = nullptr;
GetDesc1_t           GetDesc1_Original         = nullptr;
GetDesc2_t           GetDesc2_Original         = nullptr;

EnumAdapters_t       EnumAdapters_Original     = nullptr;
EnumAdapters1_t      EnumAdapters1_Original    = nullptr;

CreateDXGIFactory_t  CreateDXGIFactory_Import  = nullptr;
CreateDXGIFactory1_t CreateDXGIFactory1_Import = nullptr;
CreateDXGIFactory2_t CreateDXGIFactory2_Import = nullptr;

static HMODULE hDxgi = NULL;

struct IDXGIAdapter3;

struct budget_thread_params_t {
  IDXGIAdapter3   *pAdapter;
  DWORD            tid;
  HANDLE           handle;
  DWORD            cookie;
  HANDLE           event;
  volatile bool    ready;
} *budget_thread = nullptr;

WORD
BMF_Timestamp (wchar_t* const out)
{
  SYSTEMTIME stLogTime;
  GetLocalTime (&stLogTime);

  wchar_t date [64] = { L'\0' };
  wchar_t time [64] = { L'\0' };

  GetDateFormat (LOCALE_INVARIANT,DATE_SHORTDATE,   &stLogTime,NULL,date,64);
  GetTimeFormat (LOCALE_INVARIANT,TIME_NOTIMEMARKER,&stLogTime,NULL,time,64);

  out [0] = L'\0';

  lstrcatW (out, date);
  lstrcatW (out, L" ");
  lstrcatW (out, time);
  lstrcatW (out, L".");

  return stLogTime.wMilliseconds;
}

//
// NOTE: This is a barbaric approach to the problem... we clearly have a
//         multi-threaded execution model but the logging assumes otherwise.
//
//       The log system _is_ thread-safe, but the output can be non-sensical
//         when multiple threads are logging calls or even when a recursive
//           call is logged in a single thread.
//
//        * Consdier using a stack-based approach if these logs become
//            indecipherable in the future.
//            
struct bmf_logger_t {
  bool init (const char* const szFilename,
             const char* const szMode);

  void close (void);

  void LogEx (bool                 _Timestamp,
              _In_z_ _Printf_format_string_
              wchar_t const* const _Format, ...);

  void Log   (_In_z_ _Printf_format_string_
              wchar_t const* const _Format, ...);

  FILE*            fLog        = NULL;
  bool             silent      = false;
  bool             initialized = false;
  CRITICAL_SECTION log_mutex =   { 0 };
} dxgi_log, budget_log;


void
bmf_logger_t::close (void)
{
  if (fLog != NULL) {
    fflush (fLog);
    fclose (fLog);
  }

  initialized = false;
  silent      = true;

  DeleteCriticalSection (&log_mutex);
}

bool
bmf_logger_t::init (const char* const szFileName,
                    const char* const szMode)
{
  if (initialized)
    return true;

  fLog = fopen (szFileName, szMode);

  BOOL bRet = InitializeCriticalSectionAndSpinCount (&log_mutex, 2500);

  if ((! bRet) || (fLog == NULL)) {
    silent = true;
    return false;
  }

  initialized = true;
  return initialized;
}

void
bmf_logger_t::LogEx (bool                 _Timestamp,
                     _In_z_ _Printf_format_string_
                     wchar_t const* const _Format, ...)
{
  va_list _ArgList;

  if (! initialized)
    return;

  EnterCriticalSection (&log_mutex);

  if ((! fLog) || silent) {
    LeaveCriticalSection (&log_mutex);
    return;
  }

  if (_Timestamp) {
    wchar_t wszLogTime [128];

    WORD ms = BMF_Timestamp (wszLogTime);

    fwprintf (fLog, L"%s%03u: ", wszLogTime, ms);
  }

  va_start (_ArgList, _Format);
  {
    vfwprintf (fLog, _Format, _ArgList);
  }
  va_end   (_ArgList);

  fflush (fLog);

  LeaveCriticalSection (&log_mutex);
}

void
bmf_logger_t::Log   (_In_z_ _Printf_format_string_
                     wchar_t const* const _Format, ...)
{
  va_list _ArgList;

  if (! initialized)
    return;

  EnterCriticalSection (&log_mutex);

  if ((! fLog) || silent) {
    LeaveCriticalSection (&log_mutex);
    return;
  }

  wchar_t wszLogTime [128];

  WORD ms = BMF_Timestamp (wszLogTime);

  fwprintf (fLog, L"%s%03u: ", wszLogTime, ms);

  va_start (_ArgList, _Format);
  {
    vfwprintf (fLog, _Format, _ArgList);
  }
  va_end   (_ArgList);

  fwprintf  (fLog, L"\n");
  fflush    (fLog);

  LeaveCriticalSection (&log_mutex);
}

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

    //case D3DERR_INVALIDCALL:
      //return L"D3DERR_INVALIDCALL";

    //case D3DERR_WASSTILLDRAWING:
      //return L"D3DERR_WASSTILLDRAWING";


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
      dxgi_log.Log (L" *** Encountered unknown HRESULT: (0x%08X)",
                    (unsigned long)result);
      return L"UNKNOWN";
  }
}

const wchar_t*
BMF_DescribeVirtualProtectFlags (DWORD dwProtect)
{
  switch (dwProtect)
  {
    case 0x10:
      return L"Execute";
    case 0x20:
      return L"Execute + Read-Only";
    case 0x40:
      return L"Execute + Read/Write";
    case 0x80:
      return L"Execute + Read-Only or Copy-on-Write)";
    case 0x01:
      return L"No Access";
    case 0x02:
      return L"Read-Only";
    case 0x04:
      return L"Read/Write";
    case 0x08:
      return L" Read-Only or Copy-on-Write";
    default:
      return L"UNKNOWN";
  }
}

void
BMF_Init (void)
{
  EnterCriticalSection (&init_mutex);
  if (hDxgi != NULL) {
    LeaveCriticalSection (&init_mutex);
    return;
  }

  dxgi_log.init ("dxgi.log", "w");
  dxgi_log.Log  (L"dxgi.log created");

  dxgi_log.LogEx (false,
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

  dxgi_log.Log (L">> (%s) <<", pwszShortName);

  if (! lstrcmpW (pwszShortName, L"BatmanAK.exe"))
    USE_SLI = false;

  dxgi_log.LogEx (false,
    L"----------------------------------------------------------------------"
    L"-------------\n");

  wchar_t wszDxgiDLL [MAX_PATH] = { L'\0' };
  GetSystemDirectory (wszDxgiDLL, MAX_PATH);

  dxgi_log.Log (L" System Directory:           %s", wszDxgiDLL);

  lstrcatW (wszDxgiDLL, L"\\dxgi.dll");

  dxgi_log.LogEx (true, L" Loading default dxgi.dll: ");

  hDxgi = LoadLibrary (wszDxgiDLL);

  //CreateThread (NULL, 0, LoadD3D11, NULL, 0, NULL);

  if (hDxgi != NULL)
    dxgi_log.LogEx (false, L" (%s)\n", wszDxgiDLL);
  else
    dxgi_log.LogEx (false, L" FAILED (%s)!\n", wszDxgiDLL);

  dxgi_log.LogEx (false,
    L"----------------------------------------------------------------------"
    L"-------------\n");


  dxgi_log.LogEx (true, L"Loading user preferences from dxgi.ini... ");
  BMF_LoadConfig ();
  dxgi_log.LogEx (false, L"done!\n\n");

  if (config.silent) {
    dxgi_log.silent = true;
    dxgi_log.close ();
    DeleteFile     (L"dxgi.log");
  } else {
    dxgi_log.silent = false;
  }

  dxgi_log.LogEx (true, L"Initializing NvAPI: ");

  nvapi_init = bmf::NVAPI::InitializeLibrary ();

  dxgi_log.LogEx (false, L" %s\n\n", nvapi_init ? L"Success" : L"Failed");

  if (nvapi_init) {
    int num_sli_gpus = bmf::NVAPI::CountSLIGPUs ();

    dxgi_log.Log (L" >> NVIDIA Driver Version: %s",
      bmf::NVAPI::GetDriverVersion ().c_str ());

    dxgi_log.Log (L"  * Number of Installed NVIDIA GPUs: %u "
                  L"(%u are in SLI mode)",
       bmf::NVAPI::CountPhysicalGPUs (), num_sli_gpus);

    if (num_sli_gpus > 0) {
      dxgi_log.LogEx (false, L"\n");

      DXGI_ADAPTER_DESC* sli_adapters =
        bmf::NVAPI::EnumSLIGPUs ();

      int sli_gpu_idx = 0;

      while (*sli_adapters->Description != L'\0') {
        dxgi_log.Log ( L"   + SLI GPU %d: %s",
                    sli_gpu_idx++,
                      (sli_adapters++)->Description );
      }
    }

    dxgi_log.LogEx (false, L"\n");
  }

    HMODULE hMod = GetModuleHandle (pwszShortName);

  if (hMod != NULL) {
    DWORD* dwOptimus = (DWORD *)GetProcAddress (hMod, "NvOptimusEnablement");

    if (dwOptimus != NULL) {
      dxgi_log.Log (L"  NvOptimusEnablement..................: 0x%02X (%s)",
                     *dwOptimus,
                    (*dwOptimus & 0x1 ? L"Max Perf." :
                                        L"Don't Care"));
    } else {
      dxgi_log.Log (L"  NvOptimusEnablement..................: UNDEFINED");
    }

    DWORD* dwPowerXpress =
      (DWORD *)GetProcAddress (hMod, "AmdPowerXpressRequestHighPerformance");

    if (dwPowerXpress != NULL) {
      dxgi_log.Log (L"  AmdPowerXpressRequestHighPerformance.: 0x%02X (%s)",
                    *dwPowerXpress,
                   (*dwPowerXpress & 0x1 ? L"High Perf." :
                                           L"Don't Care"));
    }
    else
      dxgi_log.Log (L"  AmdPowerXpressRequestHighPerformance.: UNDEFINED");

    dxgi_log.LogEx (false, L"\n");
  }

  dxgi_log.Log (L"Importing CreateDXGIFactory{1|2}");
  dxgi_log.Log (L"================================");

  dxgi_log.Log (L"  CreateDXGIFactory:  %08Xh", 
    (CreateDXGIFactory_Import =  \
      (CreateDXGIFactory_t)GetProcAddress (hDxgi, "CreateDXGIFactory")));
  dxgi_log.Log (L"  CreateDXGIFactory1: %08Xh",
    (CreateDXGIFactory1_Import = \
      (CreateDXGIFactory1_t)GetProcAddress (hDxgi, "CreateDXGIFactory1")));
  dxgi_log.Log (L"  CreateDXGIFactory2: %08Xh",
    (CreateDXGIFactory2_Import = \
      (CreateDXGIFactory2_t)GetProcAddress (hDxgi, "CreateDXGIFactory2")));

  dxgi_log.LogEx (true, L" @ Loading Debug Symbols: ");

  SymInitializeW       (GetCurrentProcess (), NULL, TRUE);
  SymRefreshModuleList (GetCurrentProcess ());

  dxgi_log.LogEx (false, L"done!\n");

  dxgi_log.Log (L"=== Initialization Finished! ===\n");

  //
  // Spawn CPU Refresh Thread
  //
  if (cpu_stats.hThread == 0) {
    dxgi_log.LogEx (true, L" [WMI] Spawning CPU Monitor...      ");
    cpu_stats.hThread = CreateThread (NULL, 0, BMF_MonitorCPU, NULL, 0, NULL);
	if (cpu_stats.hThread != 0)
      dxgi_log.LogEx (false, L"tid=0x%04x\n", GetThreadId (cpu_stats.hThread));
	else
	  dxgi_log.LogEx (false, L"Failed!\n");
  }

  Sleep (250);

  if (disk_stats.hThread == 0) {
    dxgi_log.LogEx (true, L" [WMI] Spawning Disk Monitor...     ");
    disk_stats.hThread =
      CreateThread (NULL, 0, BMF_MonitorDisk, NULL, 0, NULL);
	if (disk_stats.hThread != 0)
      dxgi_log.LogEx (false, L"tid=0x%04x\n", GetThreadId (disk_stats.hThread));
	else
	  dxgi_log.LogEx (false, L"failed!\n");
  }

  Sleep (250);

  if (pagefile_stats.hThread == 0) {
    dxgi_log.LogEx (true, L" [WMI] Spawning Pagefile Monitor... ");
    pagefile_stats.hThread =
      CreateThread (NULL, 0, BMF_MonitorPagefile, NULL, 0, NULL);
	if (pagefile_stats.hThread != 0)
      dxgi_log.LogEx (false, L"tid=0x%04x\n",
                        GetThreadId (pagefile_stats.hThread));
	else
	  dxgi_log.LogEx (false, L"failed!\n");
  }

  dxgi_log.LogEx (false, L"\n");

  LeaveCriticalSection (&init_mutex);
}


#define DXGI_CALL(_Ret, _Call) {                                      \
  dxgi_log.LogEx (true, L"  Calling original function: ");            \
  (_Ret) = (_Call);                                                   \
  dxgi_log.LogEx (false, L"(ret=%s)\n\n", BMF_DescribeHRESULT (_Ret));\
}

// Interface-based DXGI call
#define DXGI_LOG_CALL_I(_Interface,_Name,_Format)                            \
  dxgi_log.LogEx (true, L"[!] %s::%s (", _Interface, _Name);                 \
  dxgi_log.LogEx (false, _Format
// Global DXGI call
#define DXGI_LOG_CALL(_Name,_Format)                                         \
  dxgi_log.LogEx (true, L"[!] %s (", _Name);                                 \
  dxgi_log.LogEx (false, _Format
#define DXGI_LOG_CALL_END                                                    \
  dxgi_log.LogEx (false, L") -- [Calling Thread: 0x%04x]\n",                 \
    GetCurrentThreadId ());

#define DXGI_LOG_CALL_I0(_Interface,_Name) {                                 \
  DXGI_LOG_CALL_I   (_Interface,_Name, L"void"));                            \
  DXGI_LOG_CALL_END                                                          \
}

#define DXGI_LOG_CALL_I1(_Interface,_Name,_Format,_Args) {                   \
  DXGI_LOG_CALL_I   (_Interface,_Name, _Format), _Args);                     \
  DXGI_LOG_CALL_END                                                          \
}

#define DXGI_LOG_CALL_I2(_Interface,_Name,_Format,_Args0,_Args1) {           \
  DXGI_LOG_CALL_I   (_Interface,_Name, _Format), _Args0, _Args1);            \
  DXGI_LOG_CALL_END                                                          \
}

#define DXGI_LOG_CALL_I3(_Interface,_Name,_Format,_Args0,_Args1,_Args2) {    \
  DXGI_LOG_CALL_I   (_Interface,_Name, _Format), _Args0, _Args1, _Args2);    \
  DXGI_LOG_CALL_END                                                          \
}

#define DXGI_LOG_CALL_0(_Name) {                               \
  DXGI_LOG_CALL   (_Name, L"void"));                           \
  DXGI_LOG_CALL_END                                            \
}

#define DXGI_LOG_CALL_1(_Name,_Format,_Args0) {                \
  DXGI_LOG_CALL   (_Name, _Format), _Args0);                   \
  DXGI_LOG_CALL_END                                            \
}

#define DXGI_LOG_CALL_2(_Name,_Format,_Args0,_Args1) {         \
  DXGI_LOG_CALL     (_Name, _Format), _Args0, _Args1);         \
  DXGI_LOG_CALL_END                                            \
}

#define DXGI_LOG_CALL_3(_Name,_Format,_Args0,_Args1,_Args2) {  \
  DXGI_LOG_CALL     (_Name, _Format), _Args0, _Args1, _Args2); \
  DXGI_LOG_CALL_END                                            \
}

#define DXGI_STUB(_Return, _Name, _Proto, _Args)                      \
  __declspec (dllexport) _Return STDMETHODCALLTYPE                    \
  _Name _Proto {                                                      \
    WaitForInit ();                                                   \
                                                                      \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;        \
    static passthrough_t _default_impl = nullptr;                     \
                                                                      \
    if (_default_impl == nullptr) {                                   \
      static const char* szName = #_Name;                             \
      _default_impl = (passthrough_t)GetProcAddress (hDxgi, szName);  \
                                                                      \
      if (_default_impl == nullptr) {                                 \
        dxgi_log.Log (                                                \
          L"Unable to locate symbol  %s in dxgi.dll",                 \
          L#_Name);                                                   \
        return E_NOTIMPL;                                             \
      }                                                               \
    }                                                                 \
                                                                      \
    dxgi_log.Log (L"[!] %s (%s) - "                                   \
             L"[Calling Thread: 0x%04x]",                             \
      L#_Name, L#_Proto, GetCurrentThreadId ());                      \
                                                                      \
    return _default_impl _Args;                                       \
}

#if 0
typedef struct dxgi_override_t {
  void** object;
  uint32_t idx;
  DWORD    protection;
  void*    override_func;
  void*    original_func;
};
#endif

extern "C++" {
  std::wstring
  BMF_GetDXGIFactoryInterfaceEx (const IID& riid)
  {
    std::wstring interface_name;

    if (riid == __uuidof (IDXGIFactory))
      interface_name = L"IDXGIFactory";
    else if (riid == __uuidof (IDXGIFactory1))
      interface_name = L"IDXGIFactory1";
    else if (riid == __uuidof (IDXGIFactory2))
      interface_name = L"IDXGIFactory2";
    else if (riid == __uuidof (IDXGIFactory3))
      interface_name = L"IDXGIFactory3";
    else if (riid == __uuidof (IDXGIFactory4))
      interface_name = L"IDXGIFactory4";
    else {
      wchar_t *pwszIID;

      if (SUCCEEDED (StringFromIID (riid, (LPOLESTR *)&pwszIID)))
	  {
        interface_name = pwszIID;
        CoTaskMemFree (pwszIID);
      }
    }

    return interface_name;
  }

  std::wstring
  BMF_GetDXGIFactoryInterface (IUnknown *pFactory)
  {
    IUnknown *pTemp = nullptr;

    if (SUCCEEDED (
         pFactory->QueryInterface (__uuidof (IDXGIFactory4), (void **)&pTemp)))
    {
      pTemp->Release ();
      return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory4));
    }
    if (SUCCEEDED (
         pFactory->QueryInterface (__uuidof (IDXGIFactory3), (void **)&pTemp)))
    {
      pTemp->Release ();
      return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory3));
    }

    if (SUCCEEDED (
         pFactory->QueryInterface (__uuidof (IDXGIFactory2), (void **)&pTemp)))
    {
      pTemp->Release ();
      return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory2));
    }

    if (SUCCEEDED (
         pFactory->QueryInterface (__uuidof (IDXGIFactory1), (void **)&pTemp)))
    {
      pTemp->Release ();
      return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory1));
    }

    if (SUCCEEDED (
         pFactory->QueryInterface (__uuidof (IDXGIFactory), (void **)&pTemp)))
    {
      pTemp->Release ();
      return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory));
    }

    return L"{Invalid-Factory-UUID}";
  }

  std::wstring
  BMF_GetDXGIAdapterInterfaceEx (const IID& riid)
  {
    std::wstring interface_name;

    if (riid == __uuidof (IDXGIAdapter))
      interface_name = L"IDXGIAdapter";
    else if (riid == __uuidof (IDXGIAdapter1))
      interface_name = L"IDXGIAdapter1";
    else if (riid == __uuidof (IDXGIAdapter2))
      interface_name = L"IDXGIAdapter2";
    else if (riid == __uuidof (IDXGIAdapter3))
      interface_name = L"IDXGIAdapter3";
    else {
      wchar_t* pwszIID;

	  if (SUCCEEDED (StringFromIID (riid, (LPOLESTR *)&pwszIID)))
	  {
		  interface_name = pwszIID;
          CoTaskMemFree (pwszIID);
	  }
    }

    return interface_name;
  }

  std::wstring
  BMF_GetDXGIAdapterInterface (IUnknown *pAdapter)
  {
    IUnknown *pTemp = nullptr;

    if (SUCCEEDED (
         pAdapter->QueryInterface (__uuidof (IDXGIAdapter3), (void **)&pTemp)))
    {
      pTemp->Release ();
      return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter3));
    }

    if (SUCCEEDED (
         pAdapter->QueryInterface (__uuidof (IDXGIAdapter2), (void **)&pTemp)))
    {
      pTemp->Release ();
      return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter2));
    }

    if (SUCCEEDED (
         pAdapter->QueryInterface (__uuidof (IDXGIAdapter1), (void **)&pTemp)))
    {
      pTemp->Release ();
      return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter1));
    }

    if (SUCCEEDED 
        (pAdapter->QueryInterface (__uuidof (IDXGIAdapter), (void **)&pTemp)))
    {
      pTemp->Release ();
      return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter));
    }

    return L"{Invalid-Adapter-UUID}";
  }
}

#define DXGI_VIRTUAL_OVERRIDE(_Base,_Index,_Name,_Override,_Original,_Type) { \
  void** vftable = *(void***)*_Base;                                          \
                                                                              \
  if (vftable [_Index] != _Override) {                                        \
    DWORD dwProtect;                                                          \
                                                                              \
    VirtualProtect (&vftable [_Index], 8, PAGE_EXECUTE_READWRITE, &dwProtect);\
                                                                              \
  dxgi_log.Log (L" Original VFTable entry for %s: %08Xh  (Memory Policy: %s)",\
             L##_Name, vftable [_Index],                                      \
             BMF_DescribeVirtualProtectFlags (dwProtect));                    \
                                                                              \
    if (_Original == NULL)                                                    \
      _Original = (##_Type)vftable [_Index];                                  \
                                                                              \
    dxgi_log.Log (L"  + %s: %08Xh", L#_Original, _Original);                  \
                                                                              \
    vftable [_Index] = _Override;                                             \
                                                                              \
    VirtualProtect (&vftable [_Index], 8, dwProtect, &dwProtect);             \
                                                                              \
    dxgi_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n", \
                  L##_Name, vftable [_Index],                                 \
                  BMF_DescribeVirtualProtectFlags (dwProtect));               \
  }                                                                           \
}

#define min_max(ref,min,max) if ((ref) > (max)) (max) = (ref); \
                             if ((ref) < (min)) (min) = (ref);

HRESULT
__cdecl PresentCallback (IDXGISwapChain *This,
                         UINT            SyncInterval,
                         UINT            Flags)
{
  BMF_DrawOSD ();

  HRESULT hr = Present_Original (This, SyncInterval, Flags);

  static bool toggle_mem = false;
  if (HIWORD (GetAsyncKeyState (config.mem_keys [0])) &&
      HIWORD (GetAsyncKeyState (config.mem_keys [1])) &&
      HIWORD (GetAsyncKeyState (config.mem_keys [2])))
  {
    if (! toggle_mem)
      config.mem_stats = (! config.mem_stats);
    toggle_mem = true;
  } else {
    toggle_mem = false;
  }

  static bool toggle_balance = false;
  if (HIWORD (GetAsyncKeyState (config.load_keys [0])) &&
      HIWORD (GetAsyncKeyState (config.load_keys [1])) &&
      HIWORD (GetAsyncKeyState (config.load_keys [2])))
  {
    if (! toggle_balance)
      config.load_balance = (! config.load_balance); 
    toggle_balance = true;
  } else {
    toggle_balance = false;
  }

  static bool toggle_sli = false;
  if (HIWORD (GetAsyncKeyState (config.sli_keys [0])) &&
      HIWORD (GetAsyncKeyState (config.sli_keys [1])) &&
      HIWORD (GetAsyncKeyState (config.sli_keys [2])))
  {
    if (! toggle_sli)
      config.sli_stats = (! config.sli_stats);
    toggle_sli = true;
  } else {
    toggle_sli = false;
  }

  static bool toggle_io = false;
  if (HIWORD (GetAsyncKeyState (config.io_keys [0])) &&
      HIWORD (GetAsyncKeyState (config.io_keys [1])) &&
      HIWORD (GetAsyncKeyState (config.io_keys [2])))
  {
    if (! toggle_io)
      config.io_stats = (! config.io_stats);
    toggle_io = true;
  } else {
    toggle_io = false;
  }

  static bool toggle_cpu = false;
  if (HIWORD (GetAsyncKeyState (config.cpu_keys [0])) &&
      HIWORD (GetAsyncKeyState (config.cpu_keys [1])) &&
      HIWORD (GetAsyncKeyState (config.cpu_keys [2])))
  {
    if (! toggle_cpu)
      config.cpu_stats = (! config.cpu_stats);
    toggle_cpu = true;
  } else {
    toggle_cpu = false;
  }

  static bool toggle_disk = false;
  if (HIWORD (GetAsyncKeyState (config.disk_keys [0])) &&
      HIWORD (GetAsyncKeyState (config.disk_keys [1])) &&
      HIWORD (GetAsyncKeyState (config.disk_keys [2])))
  {
    if (! toggle_disk)
      config.disk_stats = (! config.disk_stats);
    toggle_disk = true;
  } else {
    toggle_disk = false;
  }

  static bool toggle_pagefile = false;
  if (HIWORD (GetAsyncKeyState (config.pagefile_keys [0])) &&
      HIWORD (GetAsyncKeyState (config.pagefile_keys [1])) &&
      HIWORD (GetAsyncKeyState (config.pagefile_keys [2])))
  {
    if (! toggle_pagefile)
      config.pagefile_stats = (! config.pagefile_stats);
    toggle_pagefile = true;
  } else {
    toggle_pagefile = false;
  }

  if (config.sli_stats)
  {
    // Get SLI status for the frame we just displayed... this will show up
    //   one frame late, but this is the safest approach.
    if (This != NULL && nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
      IUnknown* pDev = nullptr;

      This->GetDevice (__uuidof (ID3D11Device), (void **)&pDev);

      if (pDev != nullptr) {
        sli_state = bmf::NVAPI::GetSLIState (pDev);
        pDev->Release ();
      }
    }
  }

  return hr;
}

HRESULT
STDMETHODCALLTYPE CreateSwapChain_Override (IDXGIFactory          *This,
                                      _In_  IUnknown              *pDevice,
                                      _In_  DXGI_SWAP_CHAIN_DESC  *pDesc,
                                     _Out_  IDXGISwapChain       **ppSwapChain)
{
  std::wstring iname = BMF_GetDXGIFactoryInterface (This);

  DXGI_LOG_CALL_I3 (iname.c_str (), L"CreateSwapChain", L"%08Xh, %08Xh, %08Xh",
                    pDevice, pDesc, ppSwapChain);

  HRESULT ret;
  DXGI_CALL(ret, CreateSwapChain_Original (This, pDevice, pDesc, ppSwapChain));

  if (ppSwapChain != NULL && (*ppSwapChain) != NULL &&
      Present_Original == nullptr)
  {
    ID3D11Device *pDev;

    if (SUCCEEDED (pDevice->QueryInterface (__uuidof (ID3D11Device),
                                            (void **)&pDev)))
    {
      budget_log.silent = false;

      budget_log.LogEx (true, L"Hooking IDXGISwapChain::Present... ");

#if 0
      void** vftable = *(void***)*ppSwapChain;

      MH_STATUS stat =
        MH_CreateHook (vftable [8], PresentCallback, (void **)&Present_Original);

      if (stat != MH_ERROR_ALREADY_CREATED &&
          stat != MH_OK) {
        budget_log.LogEx (false, L" failed\n");
      }
      else {
        budget_log.LogEx (false, L" %p\n", Present_Original);
      }

      MH_ApplyQueued ();
      MH_EnableHook  (MH_ALL_HOOKS);
#else
      DXGI_VIRTUAL_OVERRIDE (ppSwapChain, 8, "IDXGISwapChain::Present",
                             PresentCallback, Present_Original,
                             PresentSwapChain_t);

      budget_log.LogEx (false, L"Done\n");
#endif

      budget_log.silent = true;

      pDev->Release ();
    }
  }

  return ret;
}

HRESULT
WINAPI D3D11CreateDeviceAndSwapChain(
_In_opt_                             IDXGIAdapter          *pAdapter,
                                     D3D_DRIVER_TYPE        DriverType,
                                     HMODULE                Software,
                                     UINT                   Flags,
_In_reads_opt_ (FeatureLevels) CONST D3D_FEATURE_LEVEL     *pFeatureLevels,
                                     UINT                   FeatureLevels,
                                     UINT                   SDKVersion,
_In_opt_                       CONST DXGI_SWAP_CHAIN_DESC  *pSwapChainDesc,
_Out_opt_                            IDXGISwapChain       **ppSwapChain,
_Out_opt_                            ID3D11Device         **ppDevice,
_Out_opt_                            D3D_FEATURE_LEVEL     *pFeatureLevel,
_Out_opt_                            ID3D11DeviceContext  **ppImmediateContext)
{
  DXGI_LOG_CALL_0 (L"D3D11CreateDeviceAndSwapChain");

  dxgi_log.LogEx (true, L" Preferred Feature Level(s): <%u> - ", FeatureLevels);

  for (UINT i = 0; i < FeatureLevels; i++) {
    switch (pFeatureLevels [i])
    {
      case D3D_FEATURE_LEVEL_9_1:
        dxgi_log.LogEx (false, L" 9_1");
        break;
      case D3D_FEATURE_LEVEL_9_2:
        dxgi_log.LogEx (false, L" 9_2");
        break;
      case D3D_FEATURE_LEVEL_9_3:
        dxgi_log.LogEx (false, L" 9_3");
        break;
      case D3D_FEATURE_LEVEL_10_0:
        dxgi_log.LogEx (false, L" 10_0");
        break;
      case D3D_FEATURE_LEVEL_10_1:
        dxgi_log.LogEx (false, L" 10_1");
        break;
      case D3D_FEATURE_LEVEL_11_0:
        dxgi_log.LogEx (false, L" 11_0");
        break;
      case D3D_FEATURE_LEVEL_11_1:
        dxgi_log.LogEx (false, L" 11_1");
        break;
      case D3D_FEATURE_LEVEL_12_0:
        dxgi_log.LogEx (false, L" 12_0");
        break;
      case D3D_FEATURE_LEVEL_12_1:
        dxgi_log.LogEx (false, L" 12_1");
        break;
    }
  }

  dxgi_log.LogEx (false, L"\n");

  HRESULT res =
    D3D11CreateDeviceAndSwapChain_Import (pAdapter,
                                          DriverType,
                                          Software,
                                          Flags,
                                          pFeatureLevels,
                                          FeatureLevels,
                                          SDKVersion,
                                          pSwapChainDesc,
                                          ppSwapChain,
                                          ppDevice,
                                          pFeatureLevel,
                                          ppImmediateContext);

  if (res == S_OK && (ppDevice != NULL)) {
    dxgi_log.Log (L" >> Device = 0x%08Xh", *ppDevice);
    g_pD3D11Dev = (*ppDevice);

    if (g_pDXGIDev == nullptr && g_pD3D11Dev != nullptr) {
        g_pD3D11Dev->QueryInterface (__uuidof (IDXGIDevice),
                                     (void **)&g_pDXGIDev);
    }
  }

  return res;
}

HRESULT
STDMETHODCALLTYPE GetDesc2_Override (IDXGIAdapter2      *This,
                              _Out_  DXGI_ADAPTER_DESC2 *pDesc)
{
  std::wstring iname = BMF_GetDXGIAdapterInterface (This);

  DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc2", L"%08Xh, %08Xh", This, pDesc);

  HRESULT ret;
  DXGI_CALL (ret, GetDesc2_Original (This, pDesc));

  //// OVERRIDE VRAM NUMBER
  if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
    dxgi_log.LogEx ( true,
      L" <> GetDesc2_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );
      
    DXGI_ADAPTER_DESC* match =
      bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

    if (match != NULL) {
      dxgi_log.LogEx (false, L"Success! (%s)\n", match->Description);
      pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
    }
    else
      dxgi_log.LogEx (false, L"Failure! (No Match Found)\n");
  }

  dxgi_log.LogEx (false, L"\n");

  return ret;
}

HRESULT
STDMETHODCALLTYPE GetDesc1_Override (IDXGIAdapter1      *This,
                              _Out_  DXGI_ADAPTER_DESC1 *pDesc)
{
  std::wstring iname = BMF_GetDXGIAdapterInterface (This);

  DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc1", L"%08Xh, %08Xh", This, pDesc);

  HRESULT ret;
  DXGI_CALL (ret, GetDesc1_Original (This, pDesc));

  //// OVERRIDE VRAM NUMBER
  if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
    dxgi_log.LogEx ( true,
      L" <> GetDesc1_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );
      
    DXGI_ADAPTER_DESC* match =
      bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

    if (match != NULL) {
      dxgi_log.LogEx (false, L"Success! (%s)\n", match->Description);
      pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
    }
    else
      dxgi_log.LogEx (false, L"Failure! (No Match Found)\n");
  }

  dxgi_log.LogEx (false, L"\n");

  return ret;
}

HRESULT
STDMETHODCALLTYPE GetDesc_Override (IDXGIAdapter      *This,
                             _Out_  DXGI_ADAPTER_DESC *pDesc)
{
  std::wstring iname = BMF_GetDXGIAdapterInterface (This);

  DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc",L"%08Xh, %08Xh", This, pDesc);

  HRESULT ret;
  DXGI_CALL (ret, GetDesc_Original (This, pDesc));

  //// OVERRIDE VRAM NUMBER
  if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
    dxgi_log.LogEx ( true,
      L" <> GetDesc_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );

    DXGI_ADAPTER_DESC* match =
      bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

    if (match != NULL) {
      dxgi_log.LogEx (false, L"Success! (%s)\n", match->Description);
      pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
    }
    else
      dxgi_log.LogEx (false, L"Failure! (No Match Found)\n");
  }

  dxgi_log.LogEx (false, L"\n");

  return ret;
}

typedef enum bmfUndesirableVendors {
  Microsoft = 0x1414,
  Intel     = 0x8086
} Vendors;

volatile bool init = false;

DWORD
WINAPI HookThread (LPVOID user)
{
  if (init) {
    //LeaveCriticalSection (&d3dhook_mutex);
    return 1;
  }

  init = true;

  LoadLibrary (L"d3d11.dll");

  dxgi_log.LogEx (true, L"Hooking D3D11CreateDeviceAndSwapChain... ");

  MH_STATUS stat =
    MH_CreateHookApi (L"d3d11.dll",
                      "D3D11CreateDeviceAndSwapChain",
                      D3D11CreateDeviceAndSwapChain,
                      (void **)&D3D11CreateDeviceAndSwapChain_Import);

  if (stat != MH_ERROR_ALREADY_CREATED &&
      stat != MH_OK) {
    dxgi_log.LogEx (false, L" failed\n");
  }
  else {
    dxgi_log.LogEx (false, L" %p\n", D3D11CreateDeviceAndSwapChain_Import);
  }

  dxgi_log.LogEx (false, L"\n");

  MH_ApplyQueued ();
  MH_EnableHook  (MH_ALL_HOOKS);

  return 0;
}

void
BMF_InstallD3D11DeviceHooks (void)
{
  EnterCriticalSection (&d3dhook_mutex);
  {
    WaitForInit ();

	if (init) {
      LeaveCriticalSection (&d3dhook_mutex);
	  return;
    }

    HANDLE hThread =
      CreateThread (NULL, 0, HookThread, NULL, 0, NULL);

    if (hThread != 0)
      WaitForSingleObject (hThread, INFINITE);
	else
	  Sleep (20);

	init = true;
  }
  LeaveCriticalSection (&d3dhook_mutex);
}


HRESULT
STDMETHODCALLTYPE EnumAdapters_Common (IDXGIFactory       *This,
                                       UINT                Adapter,
                                _Out_  IDXGIAdapter      **ppAdapter,
                                       DXGI_ADAPTER_DESC  *pDesc,
                                       EnumAdapters_t      pFunc)
{
  // Logic to skip Intel and Microsoft adapters and return only AMD / NV
  if (lstrlenW (pDesc->Description)) {
    if (pDesc->VendorId == Microsoft || pDesc->VendorId == Intel) {
      // We need to release the reference we were just handed before
      //   skipping it.
      (*ppAdapter)->Release ();

      dxgi_log.LogEx (false,
          L" >> (Host Application Tried To Enum Intel or Microsoft Adapter)"
          L" -- Skipping Adapter %d <<\n\n", Adapter);

      return (pFunc (This, Adapter + 1, ppAdapter));
    }
    else {
      if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
        DXGI_ADAPTER_DESC* match =
          bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

        if (match != NULL &&
            pDesc->DedicatedVideoMemory > match->DedicatedVideoMemory) {
          dxgi_log.Log (
            L"   # SLI Detected (Corrected Memory Total: %llu MiB -- "
            L"Original: %llu MiB)",
                          match->DedicatedVideoMemory >> 20ULL,
                          pDesc->DedicatedVideoMemory >> 20ULL);
        }
      }

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

          dxgi_log.LogEx (true,
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
            dxgi_log.LogEx (false, L"tid=0x%04x\n", budget_thread->tid);

            dxgi_log.LogEx (true,
              L"   %% Setting up Budget Change Notification.........: ");

            HRESULT result =
              pAdapter3->RegisterVideoMemoryBudgetChangeNotificationEvent (
                budget_thread->event, &budget_thread->cookie
              );

            if (SUCCEEDED (result)) {
              dxgi_log.LogEx (false, L"eid=0x%x, cookie=%u\n",
                          budget_thread->event, budget_thread->cookie);
            } else {
              dxgi_log.LogEx (false, L"Failed! (%s)\n",
                          BMF_DescribeHRESULT (result));
            }
          } else {
            dxgi_log.LogEx (false, L"failed!\n");
          }

          dxgi_log.LogEx (false, L"\n");
        }

        LeaveCriticalSection (&budget_mutex);

        dxgi_log.LogEx (true,  L"   [DXGI 1.2]: GPU Scheduling...:"
                               L" Pre-Emptive");

        DXGI_ADAPTER_DESC2 desc2;
        pAdapter3->GetDesc2 (&desc2);

        switch (desc2.GraphicsPreemptionGranularity)
        {
          case DXGI_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY:
            dxgi_log.LogEx (false, L" (DMA Buffer)\n\n");
            break;
          case DXGI_GRAPHICS_PREEMPTION_PRIMITIVE_BOUNDARY:
            dxgi_log.LogEx (false, L" (Graphics Primitive)\n\n");
            break;
          case DXGI_GRAPHICS_PREEMPTION_TRIANGLE_BOUNDARY:
            dxgi_log.LogEx (false, L" (Triangle)\n\n");
            break;
          case DXGI_GRAPHICS_PREEMPTION_PIXEL_BOUNDARY:
            dxgi_log.LogEx (false, L" (Fragment)\n\n");
            break;
          case DXGI_GRAPHICS_PREEMPTION_INSTRUCTION_BOUNDARY:
            dxgi_log.LogEx (false, L" (Instruction)\n\n");
            break;
          default:
            dxgi_log.LogEx (false, L"UNDEFINED\n\n");
            break;
        }

        int i = 0;

        dxgi_log.LogEx (true,
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
            dxgi_log.LogEx (false, L"\n");
            dxgi_log.LogEx (true,  L"                                 ");
          }

          dxgi_log.LogEx (false,
                      L" Node%u (Reserve: %#5llu / %#5llu MiB - "
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
                                    config.mem_reserve * 0.01f) :
                          0
          );
        }
        dxgi_log.LogEx (false, L"\n");

        i = 0;

        dxgi_log.LogEx (true,
                    L"   [DXGI 1.4]: Non-Local Memory.:");

        while (SUCCEEDED (pAdapter3->QueryVideoMemoryInfo (
                            i,
                              DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                                &mem_info)
                          )
              )
        {
          if (i > 0) {
            dxgi_log.LogEx (false, L"\n");
            dxgi_log.LogEx (true,  L"                                 ");
          }
          dxgi_log.LogEx (false,
                      L" Node%u (Reserve: %#5llu / %#5llu MiB - "
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
                                    config.mem_reserve * 0.01f) :
                          0
          );
        }
      
        dxgi_log.LogEx (false, L"\n");

        pAdapter3->Release ();
      }

      dxgi_log.LogEx (false, L"\n");
    }

    dxgi_log.Log(L"   @ Returning Adapter w/ Name: %s\n",pDesc->Description);
  }

  return S_OK;
}

HRESULT
STDMETHODCALLTYPE EnumAdapters1_Override (IDXGIFactory1  *This,
                                          UINT            Adapter,
                                   _Out_  IDXGIAdapter1 **ppAdapter)
{
  std::wstring iname = BMF_GetDXGIFactoryInterface (This);

  DXGI_LOG_CALL_I3 (iname.c_str (), L"EnumAdapters1", L"%08Xh, %u, %08Xh",
                    This, Adapter, ppAdapter);

  HRESULT ret;
  DXGI_CALL (ret, EnumAdapters1_Original (This,Adapter,ppAdapter));

  if (SUCCEEDED (ret)) {
    if (! GetDesc1_Original) {
      // Only do this for NVIDIA GPUs
      if (nvapi_init) {
        DXGI_VIRTUAL_OVERRIDE (ppAdapter, 10, "(*ppAdapter)->GetDesc1",
                               GetDesc1_Override, GetDesc1_Original, GetDesc1_t);
      } else {
        GetDesc1_Original = (GetDesc1_t)(*(void ***)*ppAdapter) [10];
      }
    }

    DXGI_ADAPTER_DESC1 desc;
    GetDesc1_Original ((*ppAdapter), &desc);

    return EnumAdapters_Common (This, Adapter, (IDXGIAdapter **)ppAdapter,
                                (DXGI_ADAPTER_DESC *)&desc,
                                (EnumAdapters_t)EnumAdapters1_Override);
  }

  return ret;
}

HRESULT
STDMETHODCALLTYPE EnumAdapters_Override (IDXGIFactory  *This,
                                         UINT           Adapter,
                                  _Out_  IDXGIAdapter **ppAdapter)
{
  std::wstring iname = BMF_GetDXGIFactoryInterface (This);

  DXGI_LOG_CALL_I3 (iname.c_str (), L"EnumAdapters", L"%08Xh, %u, %08Xh",
                    This, Adapter, ppAdapter);

  HRESULT ret;
  DXGI_CALL (ret, EnumAdapters_Original (This, Adapter, ppAdapter));

  if (SUCCEEDED (ret)) {
    if (! GetDesc_Original) {
      // Only do this for NVIDIA GPUs
      if (nvapi_init) {
        DXGI_VIRTUAL_OVERRIDE (ppAdapter, 8, "(*ppAdapter)->GetDesc",
                               GetDesc_Override, GetDesc_Original, GetDesc_t);
      } else {
        GetDesc_Original = (GetDesc_t)(*(void ***)*ppAdapter) [8];
      }
    }

    DXGI_ADAPTER_DESC desc;
    GetDesc_Original ((*ppAdapter), &desc);

    return EnumAdapters_Common (This, Adapter, ppAdapter,
                                &desc,
                                (EnumAdapters_t)EnumAdapters_Override);
  }

  return ret;
}

__declspec (dllexport)
HRESULT
WINAPI CreateDXGIFactory (REFIID   riid,
                    _Out_ void   **ppFactory)
{
  BMF_InstallD3D11DeviceHooks ();

  std::wstring iname = BMF_GetDXGIFactoryInterfaceEx (riid);

  DXGI_LOG_CALL_2 (L"CreateDXGIFactory", L"%s, %08Xh",
                   iname.c_str (), ppFactory);

  HRESULT ret;
  DXGI_CALL (ret, CreateDXGIFactory_Import (riid, ppFactory));

  DXGI_VIRTUAL_OVERRIDE (ppFactory, 7, "IDXGIFactory::EnumAdapters",
                         EnumAdapters_Override, EnumAdapters_Original,
                         EnumAdapters_t);

  DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory::CreateSwapChain",
                         CreateSwapChain_Override, CreateSwapChain_Original,
                         CreateSwapChain_t);

  return ret;
}

__declspec (dllexport)
HRESULT
WINAPI CreateDXGIFactory1 (REFIID   riid,
                     _Out_ void   **ppFactory)
{
  BMF_InstallD3D11DeviceHooks ();

  std::wstring iname = BMF_GetDXGIFactoryInterfaceEx (riid);

  DXGI_LOG_CALL_2 (L"CreateDXGIFactory1", L"%s, %08Xh",
                   iname.c_str (), ppFactory);

  HRESULT ret;
  DXGI_CALL (ret, CreateDXGIFactory1_Import (riid, ppFactory));

  DXGI_VIRTUAL_OVERRIDE (ppFactory, 7,  "IDXGIFactory1::EnumAdapters",
                         EnumAdapters_Override,  EnumAdapters_Original,
                         EnumAdapters_t);
  DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory1::EnumAdapters1",
                         EnumAdapters1_Override, EnumAdapters1_Original,
                         EnumAdapters1_t);

  DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory1::CreateSwapChain",
                         CreateSwapChain_Override, CreateSwapChain_Original,
                         CreateSwapChain_t);

  return ret;
}

__declspec (dllexport)
HRESULT
WINAPI CreateDXGIFactory2 (UINT     Flags,
                           REFIID   riid,
                     _Out_ void   **ppFactory)
{
  BMF_InstallD3D11DeviceHooks ();

  std::wstring iname = BMF_GetDXGIFactoryInterfaceEx (riid);

  DXGI_LOG_CALL_3 (L"CreateDXGIFactory2", L"0x%04X, %s, %08Xh",
                  Flags, iname.c_str (), ppFactory);

  HRESULT ret;
  DXGI_CALL (ret, CreateDXGIFactory2_Import (Flags, riid, ppFactory));

  DXGI_VIRTUAL_OVERRIDE (ppFactory, 7, "IDXGIFactory2::EnumAdapters",
                         EnumAdapters_Override, EnumAdapters_Original,
                         EnumAdapters_t);
  DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory2::EnumAdapters1",
                         EnumAdapters1_Override, EnumAdapters1_Original,
                         EnumAdapters1_t);

  DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory2::CreateSwapChain",
                         CreateSwapChain_Override, CreateSwapChain_Original,
                         CreateSwapChain_t);

  return ret;
}

DXGI_STUB (HRESULT, DXGID3D10CreateDevice,
    (HMODULE hModule, IDXGIFactory *pFactory, IDXGIAdapter *pAdapter,
     UINT    Flags,   void         *unknown,  void         *ppDevice),
    (hModule, pFactory, pAdapter, Flags, unknown, ppDevice));

struct UNKNOWN5 {
  DWORD unknown [5];
};

DXGI_STUB (HRESULT, DXGID3D10CreateLayeredDevice,
           (UNKNOWN5 Unknown),
           (Unknown))

DXGI_STUB (SIZE_T, DXGID3D10GetLayeredDeviceSize,
           (const void *pLayers, UINT NumLayers),
           (pLayers, NumLayers))

DXGI_STUB (HRESULT, DXGID3D10RegisterLayers,
           (const void *pLayers, UINT NumLayers),
           (pLayers, NumLayers))

__declspec (dllexport)
HRESULT
STDMETHODCALLTYPE DXGIDumpJournal (void)
{
  DXGI_LOG_CALL_0 (L"DXGIDumpJournal");

  return E_NOTIMPL;
}

__declspec (dllexport)
HRESULT
STDMETHODCALLTYPE DXGIReportAdapterConfiguration (void)
{
  DXGI_LOG_CALL_0 (L"DXGIReportAdapterConfiguration");

  return E_NOTIMPL;
}

DWORD
WINAPI DllThread (LPVOID param)
{
  EnterCriticalSection (&init_mutex);
  {
    BMF_Init ();
  }
  LeaveCriticalSection (&init_mutex);

  return 0;
}

#include <ctime>
const uint32_t BUDGET_POLL_INTERVAL = 66UL; // How often to sample the budget
                                            //  in msecs

DWORD
WINAPI BudgetThread (LPVOID user_data)
{
  budget_thread_params_t* params =
    (budget_thread_params_t *)user_data;

  if (budget_log.init ("dxgi_budget.log", "w")) {
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
                            &mem_info [buffer].local [node++])
                     )
          ) ;

    node = 0;

    while (node < MAX_GPU_NODES &&
           SUCCEEDED (params->pAdapter->QueryVideoMemoryInfo (
                        node,
                          DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                            &mem_info [buffer].nonlocal [node++])
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

#if 0
	SetSystemFileCacheSize(-1, -1, NULL);
	HANDLE hHeap = GetProcessHeap();
	HeapCompact(hHeap, 0);
	struct heap_opt_t {
		DWORD version;
		DWORD length;
	} heap_opt;
    heap_opt.version = 1;
	heap_opt.length = sizeof(heap_opt_t);
	HeapSetInformation(NULL, (_HEAP_INFORMATION_CLASS)3, &heap_opt,
		heap_opt.length);
	//SetProcessWorkingSetSize (GetCurrentProcess (), (SIZE_T)-1, (SIZE_T)-1);
#endif

    static uint64_t last_budget =
      mem_info [buffer].local [0].Budget;

    if (dwWaitStatus == WAIT_OBJECT_0 && config.load_balance)
    {
      INT prio = 0;

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
                           L" Node%u (Reserve: %#5llu / %#5llu MiB - "
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
                          L" Node%u (Reserve: %#5llu / %#5llu MiB - "
                          L"Budget: %#5llu / %#5llu MiB)",
                        i,
              mem_info [buffer].nonlocal [i].CurrentReservation      >> 20ULL,
              mem_info [buffer].nonlocal [i].AvailableForReservation >> 20ULL,
              mem_info [buffer].nonlocal [i].CurrentUsage            >> 20ULL,
              mem_info [buffer].nonlocal [i].Budget                  >> 20ULL);
        i++;
      }

      if (g_pDXGIDev != nullptr)
      {
        if (config.load_balance)
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

      budget_log.LogEx (false, L"\n");
    }

	if (params->event != 0)
      ResetEvent (params->event);

    mem_info [0].buffer = buffer;
  }

  if (g_pDXGIDev != nullptr) {
    // Releasing this actually causes driver crashes, so ...
    //   let it leak, what do we care?
    //pDXGIDev->Release ();
    g_pDXGIDev = nullptr;
  }

  if (hThreadHeap != 0)
    HeapDestroy (hThreadHeap);

  return 0;
}

static volatile HANDLE hInitThread = { 0 };

void
WaitForInit (void)
{
  while (! hInitThread) ;

 if (hInitThread)
    WaitForSingleObject (hInitThread, INFINITE);
}
}

BOOL
APIENTRY DllMain ( HMODULE hModule,
                   DWORD   ul_reason_for_call,
                   LPVOID  lpReserved )
{
  switch (ul_reason_for_call)
  {
    case DLL_PROCESS_ATTACH:
    {
      dll_heap = HeapCreate (0, 0, 0);

      MH_Initialize ();

      //LoadLibrary (L"d3d11.dll");

      BOOL bRet = InitializeCriticalSectionAndSpinCount (&d3dhook_mutex, 500);
           bRet = InitializeCriticalSectionAndSpinCount (&budget_mutex,  4000);
           bRet = InitializeCriticalSectionAndSpinCount (&init_mutex,    50000);

      hInitThread = CreateThread (NULL, 0, DllThread, NULL, 0, NULL);

      // Give other DXGI hookers time to queue up before processing any calls
      //   that they make. But don't wait here infinitely, or we will deadlock!

      /* Default: 0.25 secs seems adequate */
	  if (hInitThread != 0)
        WaitForSingleObject (hInitThread, config.init_delay);
    } break;

    case DLL_THREAD_ATTACH:
      //dxgi_log.Log (L"Custom dxgi.dll Attached (tid=%x)",
      //                GetCurrentThreadId ());
      break;

    case DLL_THREAD_DETACH:
      //dxgi_log.Log (L"Custom dxgi.dll Detached (tid=%x)",
      //                GetCurrentThreadId ());
      break;

    case DLL_PROCESS_DETACH:
    {
      if (budget_thread != nullptr) {
        config.load_balance = false; // Turn this off while shutting down

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

        //budget_thread->pAdapter->UnregisterVideoMemoryBudgetChangeNotification
          //(budget_thread->cookie);

        //budget_thread->pAdapter->Release ();

        HeapFree (dll_heap, NULL, budget_thread);
        budget_thread = nullptr;
      }

      if (cpu_stats.hThread != 0) {
        dxgi_log.LogEx (true,L"[WMI] Shutting down CPU Monitor... ");
        // Signal the thread to shutdown
        cpu_stats.lID = 0;
        WaitForSingleObject (cpu_stats.hThread, 1000UL); // Give 1 second, and
                                                         // then we're killing
                                                         // the thing!
        TerminateThread (cpu_stats.hThread, 0);
        cpu_stats.hThread  = 0;
        cpu_stats.num_cpus = 0;
        dxgi_log.LogEx (false, L"done!\n");
      }

      if (disk_stats.hThread != 0) {
        dxgi_log.LogEx(true,L"[WMI] Shutting down Disk Monitor... ");
        // Signal the thread to shutdown
        disk_stats.lID = 0;
        WaitForSingleObject (disk_stats.hThread, 1000UL); // Give 1 second, and
                                                          // then we're killing
                                                          // the thing!
        TerminateThread (disk_stats.hThread, 0);
        disk_stats.hThread   = 0;
        disk_stats.num_disks = 0;
        dxgi_log.LogEx (false, L"done!\n");
      }

      if (pagefile_stats.hThread != 0) {
        dxgi_log.LogEx(true,L"[WMI] Shutting down Pagefile Monitor... ");
        // Signal the thread to shutdown
        pagefile_stats.lID = 0;
        WaitForSingleObject (
          pagefile_stats.hThread, 1000UL); // Give 1 second, and
                                           // then we're killing
                                           // the thing!
        TerminateThread (pagefile_stats.hThread, 0);
        pagefile_stats.hThread       = 0;
        pagefile_stats.num_pagefiles = 0;
        dxgi_log.LogEx (false, L"done!\n");
      }

      dxgi_log.LogEx (true, L"Saving user preferences to dxgi.ini... ");
      BMF_SaveConfig ();
      dxgi_log.LogEx (false, L"done!\n");

      BMF_ReleaseOSD ();

      // Hopefully these things are done by now...
      DeleteCriticalSection (&init_mutex);
      DeleteCriticalSection (&d3dhook_mutex);
      DeleteCriticalSection (&budget_mutex);

      MH_Uninitialize ();

      if (nvapi_init)
        bmf::NVAPI::UnloadLibrary ();

      dxgi_log.Log (L"Custom dxgi.dll Detached (pid=0x%04x)",
               GetCurrentProcessId ());

      budget_log.close ();
      dxgi_log.close   ();

      HeapDestroy (dll_heap);

      SymCleanup (GetCurrentProcess ());
    }
    break;
  }

  return TRUE;
}
