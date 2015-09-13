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

#include "stdafx.h"
#include "nvapi.h"

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

extern "C" {
  // We have some really sneaky overlays that manage to call some of our
  //   exported functions before the DLL's even attached -- make them wait,
  //     so we don't crash and burn!
  void WaitForInit (void);

static HANDLE hBudgetThread = NULL;

static BOOL    bSilent = FALSE;
static FILE*   fLog    = nullptr;
static HMODULE hDxgi   = NULL;

struct memory_stats_t {
  uint64_t min_reserve = UINT64_MAX;
  uint64_t max_reserve = 0;

  uint64_t min_avail_reserve = UINT64_MAX;
  uint64_t max_avail_reserve = 0;

  uint64_t min_budget = UINT64_MAX;
  uint64_t max_budget = 0;

  uint64_t min_usage = UINT64_MAX;
  uint64_t max_usage = 0;

  uint64_t min_over_budget = UINT64_MAX;
  uint64_t max_over_budget = 0;

  uint64_t budget_changes = 0;
} static mem_stats [4];

struct IDXGIAdapter3;

struct budget_thread_params_t {
  IDXGIAdapter3*   pAdapter;
  FILE*            fLog;
  CRITICAL_SECTION log_mutex;
  bool             silent;
  DWORD            tid;
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

static CRITICAL_SECTION log_mutex = { 0 };

void
BMF_LogEx (                              bool                 _Timestamp,
           _In_z_ _Printf_format_string_ wchar_t const* const _Format, ...)
{
  va_list _ArgList;

  EnterCriticalSection (&log_mutex);

  if ((! fLog) || bSilent) {
    LeaveCriticalSection (&log_mutex);
    return;
  }

  if (_Timestamp) {
    wchar_t wszLogTime [128];

    WORD ms = BMF_Timestamp (wszLogTime);

    fwprintf (fLog, L"%s%03u: ", wszLogTime, ms);
  }

  va_start (_ArgList, _Format);
  vfwprintf (fLog, _Format, _ArgList);
  va_end (_ArgList);

  fflush (fLog);

  LeaveCriticalSection (&log_mutex);
}

void
BMF_Log (_In_z_ _Printf_format_string_ wchar_t const* const _Format, ...)
{
  va_list _ArgList;

  EnterCriticalSection (&log_mutex);

  if ((! fLog) || bSilent) {
    LeaveCriticalSection (&log_mutex);
    return;
  }

  wchar_t wszLogTime [128];

  WORD ms = BMF_Timestamp (wszLogTime);

  fwprintf (fLog, L"%s%03u: ", wszLogTime, ms);

  va_start (_ArgList, _Format);
  vfwprintf (fLog, _Format, _ArgList);
  va_end (_ArgList);

  fwprintf  (fLog, L"\n");
  fflush    (fLog);

  LeaveCriticalSection (&log_mutex);
}

// Is Thread Local Storage acceptable in a DLL? It'd be nice to have here...
void
BMF_BLogEx (                              budget_thread_params_t* _Params,
                                          bool                    _Timestamp,
            _In_z_ _Printf_format_string_ wchar_t const* const    _Format, ...)
{
  va_list _ArgList;

  EnterCriticalSection (&_Params->log_mutex);

  if ((! _Params->fLog) || _Params->silent) {
    LeaveCriticalSection (&_Params->log_mutex);
    return;
  }

  if (_Timestamp) {
    wchar_t wszLogTime [128];

    WORD ms = BMF_Timestamp (wszLogTime);

    fwprintf (_Params->fLog, L"%s%03u: ", wszLogTime, ms);
  }

  va_start (_ArgList, _Format);
  vfwprintf (_Params->fLog, _Format, _ArgList);
  va_end (_ArgList);

  fflush (_Params->fLog);

  LeaveCriticalSection (&_Params->log_mutex);
}

void
BMF_BLog (                              budget_thread_params_t* _Params,
          _In_z_ _Printf_format_string_ wchar_t const* const    _Format, ...)
{
  va_list _ArgList;

  EnterCriticalSection (&_Params->log_mutex);

  if ((! _Params->fLog) || _Params->silent) {
    LeaveCriticalSection (&_Params->log_mutex);
    return;
  }

  wchar_t wszLogTime [128];

  WORD ms = BMF_Timestamp (wszLogTime);

  fwprintf (_Params->fLog, L"%s%03u: ", wszLogTime, ms);

  va_start (_ArgList, _Format);
  vfwprintf (_Params->fLog, _Format, _ArgList);
  va_end (_ArgList);

  fwprintf  (_Params->fLog, L"\n");
  fflush    (_Params->fLog);

  LeaveCriticalSection (&_Params->log_mutex);
}

const wchar_t*
BMF_DescribeHRESULT (HRESULT result)
{
  switch (result) {
    case S_OK:
      return L"S_OK";
    case DXGI_ERROR_NOT_FOUND:
      return L"DXGI_ERROR_NOT_FOUND";
    case E_NOTIMPL:
      return L"E_NOTIMPL";
    default:
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


static BOOL nvapi_init = FALSE;

typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory2_t) \
  (UINT Flags, REFIID riid,  void** ppFactory);
typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory1_t) \
              (REFIID riid,  void** ppFactory);
typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory_t)  \
              (REFIID riid,  void** ppFactory);

static CreateDXGIFactory_t  CreateDXGIFactoryEX  = nullptr;
static CreateDXGIFactory1_t CreateDXGIFactory1EX = nullptr;
static CreateDXGIFactory2_t CreateDXGIFactory2EX = nullptr;

void
BMF_Init (void)
{
  if (hDxgi != NULL) {
    return;
  }

  InitializeCriticalSection (&log_mutex);

  HANDLE hProc = GetCurrentProcess ();

  FILE*   silent = NULL;
  errno_t err    = fopen_s (&silent, "dxgi.silent", "r");

  if (err == 0 && silent != NULL) {
    bSilent = true;
    fclose (silent);
  }
  else {
    bSilent = false;
    fLog = fopen ("dxgi.log", "w");
    //err = fopen_s (&fLog, "dxgi.log", "w");
  }

  BMF_Log (L"dxgi.log created");

  BMF_LogEx (false,
  L"------------------------------------------------------------------------"
  L"-----------\n");

  DWORD   dwProcessSize = MAX_PATH;
  wchar_t wszProcessName [MAX_PATH];

  QueryFullProcessImageName (hProc, 0, wszProcessName, &dwProcessSize);

  wchar_t* pwszShortName = wszProcessName + lstrlenW (wszProcessName);

  while (pwszShortName > wszProcessName && *(pwszShortName - 1) != L'\\')
    --pwszShortName;

  BMF_Log (L">> (%s) <<", pwszShortName);

  BMF_LogEx (false,
  L"------------------------------------------------------------------------"
  L"-----------\n");

  wchar_t wszDxgiDLL [MAX_PATH] = { L'\0' };
  GetSystemDirectory (wszDxgiDLL, MAX_PATH);

  BMF_Log (L" System Directory:           %s", wszDxgiDLL);

  lstrcatW (wszDxgiDLL, L"\\dxgi.dll");

  BMF_LogEx (true, L" Loading default dxgi.dll: ");

  hDxgi = LoadLibrary (wszDxgiDLL);

  if (hDxgi != NULL)
    BMF_LogEx (false, L" (%s)\n", wszDxgiDLL);
  else
    BMF_LogEx (false, L" FAILED (%s)!\n", wszDxgiDLL);

  BMF_LogEx (false,
    L"----------------------------------------------------------------------"
    L"-------------\n");

  BMF_LogEx (true, L"Initializing NvAPI: ");

  nvapi_init = bmf::NVAPI::InitializeLibrary ();

  BMF_LogEx (false, L" %s\n\n", nvapi_init ? L"Success" : L"Failed");

  if (nvapi_init) {
    int num_sli_gpus = bmf::NVAPI::CountSLIGPUs ();

    BMF_Log (L" >> NVIDIA Driver Version: %s",
      bmf::NVAPI::GetDriverVersion ().c_str ());

    BMF_Log (L"  * Number of Installed NVIDIA GPUs: %u (%u are in SLI mode)",
       bmf::NVAPI::CountPhysicalGPUs (), num_sli_gpus);

    if (num_sli_gpus > 0) {
      BMF_LogEx (false, L"\n");

      DXGI_ADAPTER_DESC* sli_adapters =
        bmf::NVAPI::EnumSLIGPUs ();

      int sli_gpu_idx = 0;

      while (*sli_adapters->Description != L'\0') {
        BMF_Log ( L"   + SLI GPU %d: %s",
                    sli_gpu_idx++,
                      (sli_adapters++)->Description );
      }
    }

    BMF_LogEx (false, L"\n");
  }

  BMF_Log (L"Importing CreateDXGIFactory{1|2}");
  BMF_Log (L"================================");

  BMF_Log (L"  CreateDXGIFactory:  %08Xh", 
    (CreateDXGIFactoryEX =  \
      (CreateDXGIFactory_t)GetProcAddress (hDxgi, "CreateDXGIFactory")));
  BMF_Log (L"  CreateDXGIFactory1: %08Xh",
    (CreateDXGIFactory1EX = \
      (CreateDXGIFactory1_t)GetProcAddress (hDxgi, "CreateDXGIFactory1")));
  BMF_Log (L"  CreateDXGIFactory2: %08Xh",
    (CreateDXGIFactory2EX = \
      (CreateDXGIFactory2_t)GetProcAddress (hDxgi, "CreateDXGIFactory2")));

  // Put to sleep 1 scheduler quantum before recursively loading debug symbols
  Sleep (15);

  BMF_LogEx (true, L" @ Loading Debug Symbols: ");
  SymInitializeW       (GetCurrentProcess (), NULL, TRUE);
  SymRefreshModuleList (GetCurrentProcess ());

  BMF_LogEx (false, L"done!\n");

  BMF_Log (L"=== Initialization Finished! ===\n");
}


#define DXGI_CALL(_Ret, _Call) {                                  \
  BMF_LogEx (true, L"  Calling original function: ");             \
  (_Ret) = (_Call);                                               \
  BMF_LogEx (false, L"(ret=%s)\n\n", BMF_DescribeHRESULT (_Ret)); \
}

// Interface-based DXGI call
#define DXGI_LOG_CALL_I(_Interface,_Name,_Format)                            \
  BMF_LogEx (true, L"[!] %s::%s (", _Interface, _Name);                      \
  BMF_LogEx (false, _Format
// Global DXGI call
#define DXGI_LOG_CALL(_Name,_Format)                                         \
  BMF_LogEx (true, L"[!] %s (", _Name);                                      \
  BMF_LogEx (false, _Format
#define DXGI_LOG_CALL_END                                                    \
  BMF_LogEx (false, L") -- [Calling Thread: 0x%04x]\n",GetCurrentThreadId ());

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
  _declspec (dllexport) _Return STDMETHODCALLTYPE                     \
  _Name _Proto {                                                      \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;        \
    static passthrough_t _default_impl = nullptr;                     \
                                                                      \
    WaitForInit ();                                                   \
                                                                      \
    if (_default_impl == nullptr) {                                   \
      static const char* szName = #_Name;                             \
      _default_impl = (passthrough_t)GetProcAddress (hDxgi, szName);  \
                                                                      \
      if (_default_impl == nullptr) {                                 \
        BMF_Log (                                                     \
          L"Unable to locate symbol  %s in dxgi.dll",                 \
          L#_Name);                                                   \
        return E_NOTIMPL;                                             \
      }                                                               \
    }                                                                 \
                                                                      \
    BMF_Log (L"[!] %s (%s) - "                                        \
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
    BMF_GetDXGIFactoryInterfaceEx (const IID& riid) {
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
      wchar_t* pwszIID;
      StringFromIID (riid, (LPOLESTR *)&pwszIID);

      interface_name = pwszIID;

      CoTaskMemFree (pwszIID);
    }

    return interface_name;
  }

  std::wstring
    BMF_GetDXGIFactoryInterface (IUnknown* pFactory) {
    IUnknown* pTemp = nullptr;

    if (pFactory->QueryInterface (__uuidof (IDXGIFactory4), (void **)&pTemp)
        == S_OK) {
      pTemp->Release ();
      return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory4));
    }
    if (pFactory->QueryInterface (__uuidof (IDXGIFactory3), (void **)&pTemp)
        == S_OK) {
      pTemp->Release ();
      return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory3));
    }

    if (pFactory->QueryInterface (__uuidof (IDXGIFactory2), (void **)&pTemp)
        == S_OK) {
      pTemp->Release ();
      return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory2));
    }

    if (pFactory->QueryInterface (__uuidof (IDXGIFactory1), (void **)&pTemp)
        == S_OK) {
      pTemp->Release ();
      return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory1));
    }

    if (pFactory->QueryInterface (__uuidof (IDXGIFactory), (void **)&pTemp)
        == S_OK) {
      pTemp->Release ();
      return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory));
    }

    return L"{Invalid-Factory-UUID}";
  }

  std::wstring
    BMF_GetDXGIAdapterInterfaceEx (const IID& riid) {
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
      StringFromIID (riid, (LPOLESTR *)&pwszIID);

      interface_name = pwszIID;

      CoTaskMemFree (pwszIID);
    }

    return interface_name;
  }

  std::wstring
    BMF_GetDXGIAdapterInterface (IUnknown* pAdapter) {
    IUnknown* pTemp = nullptr;

    if (pAdapter->QueryInterface (__uuidof (IDXGIAdapter3), (void **)&pTemp)
                == S_OK) {
      pTemp->Release ();
      return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter3));
    }

    if (pAdapter->QueryInterface (__uuidof (IDXGIAdapter2), (void **)&pTemp)
                == S_OK) {
      pTemp->Release ();
      return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter2));
    }

    if (pAdapter->QueryInterface (__uuidof (IDXGIAdapter1), (void **)&pTemp)
        == S_OK) {
      pTemp->Release ();
      return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter1));
    }

    if (pAdapter->QueryInterface (__uuidof (IDXGIAdapter), (void **)&pTemp)
        == S_OK) {
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
    BMF_Log (L" Original VFTable entry for %s: %08Xh  (Memory Policy: %s)",   \
             L##_Name, vftable [_Index],                                      \
             BMF_DescribeVirtualProtectFlags (dwProtect));                    \
                                                                              \
    if (_Original == NULL)                                                    \
      _Original = (##_Type)vftable [_Index];                                  \
                                                                              \
    BMF_Log (L"  + %s: %08Xh", L#_Original, _Original);                       \
                                                                              \
    vftable [_Index] = _Override;                                             \
                                                                              \
    VirtualProtect (&vftable [_Index], 8, dwProtect, &dwProtect);             \
                                                                              \
    BMF_Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n",      \
             L##_Name, vftable [_Index],                                      \
             BMF_DescribeVirtualProtectFlags (dwProtect));                    \
  }                                                                           \
}

typedef HRESULT (STDMETHODCALLTYPE *GetDesc1_t)(
         IDXGIAdapter1      *This,
  _Out_  DXGI_ADAPTER_DESC1 *pDesc);

GetDesc1_t GetDesc1_Original = NULL;

HRESULT STDMETHODCALLTYPE GetDesc1_Override (IDXGIAdapter1*      This,
                                      _Out_  DXGI_ADAPTER_DESC1* pDesc)
{
  std::wstring iname = BMF_GetDXGIAdapterInterface (This);

  DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc1", L"%08Xh, %08Xh", This, pDesc);

  HRESULT ret;
  DXGI_CALL (ret, GetDesc1_Original (This, pDesc));

  //// OVERRIDE VRAM NUMBER
  if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
    BMF_LogEx ( true,
      L" <> GetDesc1_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );
      
    DXGI_ADAPTER_DESC* match =
      bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);
    if (match != NULL) {
      BMF_LogEx (false, L"Success! (%s)\n", match->Description);
      pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
    }
    else
      BMF_LogEx (false, L"Failure! (No Match Found)\n");
  }

  BMF_LogEx (false, L"\n");

  return ret;
}

typedef HRESULT (STDMETHODCALLTYPE *GetDesc_t)(
         IDXGIAdapter      *This,
  _Out_  DXGI_ADAPTER_DESC *pDesc);

GetDesc_t GetDesc_Original = NULL;

HRESULT STDMETHODCALLTYPE GetDesc_Override (IDXGIAdapter      *This,
                                     _Out_  DXGI_ADAPTER_DESC *pDesc)
{
  std::wstring iname = BMF_GetDXGIAdapterInterface (This);

  DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc",L"%08Xh, %08Xh", This, pDesc);

  HRESULT ret;
  DXGI_CALL (ret, GetDesc_Original (This, pDesc));

  //// OVERRIDE VRAM NUMBER
  if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
    BMF_LogEx ( true,
      L" <> GetDesc_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );

    DXGI_ADAPTER_DESC* match =
      bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

    if (match != NULL) {
      BMF_LogEx (false, L"Success! (%s)\n", match->Description);
      pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
    }
    else
      BMF_LogEx (false, L"Failure! (No Match Found)\n");
  }

  BMF_LogEx (false, L"\n");

  return ret;
}

typedef HRESULT (STDMETHODCALLTYPE *EnumAdapters_t)(
        IDXGIFactory  *This,
        UINT           Adapter,
  _Out_ IDXGIAdapter **ppAdapter);

typedef HRESULT (STDMETHODCALLTYPE *EnumAdapters1_t)(
        IDXGIFactory1  *This,
        UINT            Adapter,
  _Out_ IDXGIAdapter1 **ppAdapter);

typedef enum bmfUndesirableVendors {
  Microsoft = 0x1414,
  Intel     = 0x8086
} Vendors;

EnumAdapters1_t EnumAdapters1_Original = NULL;

HRESULT STDMETHODCALLTYPE EnumAdapters1_Override (IDXGIFactory1  *This,
                                                  UINT            Adapter,
                                           _Out_  IDXGIAdapter1 **ppAdapter)
{
  std::wstring iname = BMF_GetDXGIFactoryInterface (This);

  DXGI_LOG_CALL_I3 (iname.c_str (), L"EnumAdapters1", L"%08Xh, %u, %08Xh",
                    This, Adapter, ppAdapter);

  HRESULT ret;
  DXGI_CALL (ret, EnumAdapters1_Original (This,Adapter,ppAdapter));

  if (ret == S_OK) {
    // Only do this for NVIDIA GPUs
    if (nvapi_init) {
      // GetDesc1 calls GetDesc..

      //DXGI_VIRTUAL_OVERRIDE (ppAdapter,  8, "(*ppAdapter)->GetDesc",
      //                       GetDesc_Override,  GetDesc_Original, GetDesc_t);
      DXGI_VIRTUAL_OVERRIDE (ppAdapter, 10, "(*ppAdapter)->GetDesc1",
                             GetDesc1_Override, GetDesc1_Original, GetDesc1_t);
    } else {
      GetDesc1_Original = (GetDesc1_t)(*(void ***)*ppAdapter) [10];
    }

    DXGI_ADAPTER_DESC1 desc;
    GetDesc1_Original ((*ppAdapter), &desc);

    // Logic to skip Intel and Microsoft adapters and return only AMD / NV
    if (lstrlenW (desc.Description)) {
      if (desc.VendorId == Microsoft || desc.VendorId == Intel) {
        BMF_LogEx (false,
          L" >> (Host Application Tried To Enum Intel or Microsoft Adapter) "
          L"-- Skipping Adapter %d <<\n\n", Adapter);

        return (EnumAdapters1_Override (This, Adapter + 1, ppAdapter));
      }
      else {
        if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
          DXGI_ADAPTER_DESC* match =
            bmf::NVAPI::FindGPUByDXGIName (desc.Description);

          if (match != NULL &&
              desc.DedicatedVideoMemory > match->DedicatedVideoMemory) {
            BMF_Log (L"   # SLI Detected (Corrected Memory Total: %u MiB -- "
                     L"Original: %u MiB)",
                            match->DedicatedVideoMemory / 1024ULL / 1024ULL,
                              desc.DedicatedVideoMemory / 1024ULL / 1024ULL);
          }
        }

        BMF_Log (L"   @ Returning Adapter w/ Name: %s\n", desc.Description);
      }
    }
  }

  return ret;
}

static EnumAdapters_t EnumAdapters_Original = NULL;

HRESULT STDMETHODCALLTYPE EnumAdapters_Override (IDXGIFactory  *This,
                                                 UINT           Adapter,
                                          _Out_  IDXGIAdapter **ppAdapter)
{
  std::wstring iname = BMF_GetDXGIFactoryInterface (This);

  DXGI_LOG_CALL_I3 (iname.c_str (), L"EnumAdapters", L"%08Xh, %u, %08Xh",
                    This, Adapter, ppAdapter);

  HRESULT ret;
  DXGI_CALL (ret, EnumAdapters_Original (This, Adapter, ppAdapter));

  if (ret == S_OK) {
    // Only do this for NVIDIA GPUs
    if (nvapi_init) {
      DXGI_VIRTUAL_OVERRIDE (ppAdapter, 8, "(*ppAdapter)->GetDesc",
                             GetDesc_Override, GetDesc_Original, GetDesc_t);
    } else {
      GetDesc_Original = (GetDesc_t)(*(void ***)*ppAdapter) [8];
    }

    DXGI_ADAPTER_DESC desc;
    GetDesc_Original ((*ppAdapter), &desc);

    // Logic to skip Intel and Microsoft adapters and return only AMD / NV
    if (lstrlenW (desc.Description)) {
      if (desc.VendorId == Microsoft || desc.VendorId == Intel) {
        BMF_LogEx (false,
           L" >> (Host Application Tried To Enum Intel or Microsoft Adapter)"
           L" -- Skipping Adapter %d <<\n\n", Adapter);

        return (EnumAdapters_Override (This, Adapter + 1, ppAdapter));
      }
      else {
        if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
          DXGI_ADAPTER_DESC* match =
            bmf::NVAPI::FindGPUByDXGIName (desc.Description);

          if (match != NULL &&
              desc.DedicatedVideoMemory > match->DedicatedVideoMemory) {
            BMF_Log (
              L"   # SLI Detected (Corrected Memory Total: %u MiB -- "
              L"Original: %u MiB)",
                            match->DedicatedVideoMemory / 1024ULL / 1024ULL,
                              desc.DedicatedVideoMemory / 1024ULL / 1024ULL);
          }

          IDXGIAdapter3* pAdapter3;
          if (S_OK ==
              (*ppAdapter)->QueryInterface (
                __uuidof (IDXGIAdapter3), (void **)&pAdapter3)) {
            if (hBudgetThread == NULL) {
              // We're going to Release this interface after this loop, but
              //   the running thread still needs a reference counted.
              pAdapter3->AddRef ();
#if 0
              if (hBudgetThread) {
                budget_thread->ready = false;
                SignalObjectAndWait (budget_thread->event, hBudgetThread,
                                     INFINITE, TRUE);
                budget_thread->pAdapter->
                  UnregisterVideoMemoryBudgetChangeNotification (
                    budget_thread->cookie
                  );
                budget_thread->pAdapter->Release ();
              }
#endif

              DWORD WINAPI BudgetThread (LPVOID user_data);

              if (budget_thread == nullptr) {
                budget_thread =
                  new budget_thread_params_t ();
              }

              BMF_LogEx (true,
                   L"   $ Spawning DXGI 1.3 Memory Budget Change Thread.: ");

              budget_thread->pAdapter = pAdapter3;
              budget_thread->fLog     = NULL;
              budget_thread->tid      = 0;
              budget_thread->event    = 0;
              budget_thread->silent   = true;// false;
              budget_thread->ready    = false;

              hBudgetThread =
                CreateThread (NULL, 0, BudgetThread, (LPVOID)budget_thread,
                              0, NULL);

              while (! budget_thread->ready)
                ;

              if (budget_thread->tid != 0) {
                BMF_LogEx (false, L"tid=0x%04x\n", budget_thread->tid);

                BMF_LogEx (true,
                  L"   %% Setting up Budget Change Notification.........: ");

                HRESULT result =
                pAdapter3->RegisterVideoMemoryBudgetChangeNotificationEvent (
                  budget_thread->event, &budget_thread->cookie
                );

                if (result == S_OK) {
                  BMF_LogEx (false, L"eid=0x%x, cookie=%u\n",
                             budget_thread->event, budget_thread->cookie);

                  // Immediately run the event loop one time
                  //SignalObjectAndWait (budget_thread->event, hBudgetThread,
                                   //0, TRUE);
                } else {
                  BMF_LogEx (false, L"Failed! (%s)\n",
                             BMF_DescribeHRESULT (result));
                }
              } else {
                BMF_LogEx (false, L"failed!\n");
              }
            }
            int i = 0;

            BMF_LogEx (true,
                       L"   [DXGI 1.3]: Local Memory.....:");

            DXGI_QUERY_VIDEO_MEMORY_INFO mem_info;
            while (S_OK ==
                   pAdapter3->QueryVideoMemoryInfo (
                     i,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&mem_info)) {
              if (i > 0) {
                BMF_LogEx (false, L"\n");
               BMF_LogEx (true,  L"                                 ");
              }
              BMF_LogEx (false,
                         L" Node%u (Reserve: %#5u / %#5u MiB - "
                         L"Budget: %#5u / %#5u MiB)",
                       i++,
                         mem_info.CurrentReservation / 1024ULL / 1024ULL,
                         mem_info.AvailableForReservation / 1024ULL / 1024ULL,
                         mem_info.CurrentUsage / 1024ULL / 1024ULL,
                         mem_info.Budget / 1024ULL / 1024ULL);
              pAdapter3->SetVideoMemoryReservation (
                i-1, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                mem_info.AvailableForReservation);
            }
            BMF_LogEx (false, L"\n");

            i = 0;

            BMF_LogEx (true,
                       L"   [DXGI 1.3]: Non-Local Memory.:");

            while (S_OK ==
                   pAdapter3->QueryVideoMemoryInfo (
                     i,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&mem_info)) {
              if (i > 0) {
                BMF_LogEx (false, L"\n");
                BMF_LogEx (true,  L"                                 ");
              }
              BMF_LogEx (false,
                         L" Node%u (Reserve: %#5u / %#5u MiB - "
                         L"Budget: %#5u / %#5u MiB)",
                       i++,
                         mem_info.CurrentReservation / 1024ULL / 1024ULL,
                         mem_info.AvailableForReservation / 1024ULL / 1024ULL,
                         mem_info.CurrentUsage / 1024ULL / 1024ULL,
                         mem_info.Budget / 1024ULL / 1024ULL);
              pAdapter3->SetVideoMemoryReservation (
                i-1, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                mem_info.AvailableForReservation);
            }
          }

          BMF_LogEx (false, L"\n");

          // This sounds good in theory, but we have a tendancy to underflow
          //   the reference counter and I don't know why you'd even really
          //     care about references to a DXGI adapter. Keep it alvie as
          //       long as possible to prevent nasty things from happening.
          //pAdapter3->Release ();
        }

        BMF_Log (L"   @ Returning Adapter w/ Name: %s\n", desc.Description);
      }
    }
  }

  return ret;
}

  _declspec (dllexport)
  HRESULT
  WINAPI
  CreateDXGIFactory (REFIID         riid,
                       _Out_ void** ppFactory) {
    WaitForInit ();

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx (riid);

    DXGI_LOG_CALL_2 (L"CreateDXGIFactory", L"%s, %08Xh",
                     iname.c_str (), ppFactory);

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactoryEX (riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7, "IDXGIFactory::EnumAdapters",
                           EnumAdapters_Override, EnumAdapters_Original,
                           EnumAdapters_t);

    return ret;
  }

  _declspec (dllexport)
  HRESULT
  WINAPI
  CreateDXGIFactory1 (REFIID          riid,
                         _Out_ void** ppFactory) {
    WaitForInit ();

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx (riid);

    DXGI_LOG_CALL_2 (L"CreateDXGIFactory1", L"%s, %08Xh",
                     iname.c_str (), ppFactory);

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory1EX (riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7,  "IDXGIFactory1::EnumAdapters",
                           EnumAdapters_Override,  EnumAdapters_Original,
                           EnumAdapters_t);
    //DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory1::EnumAdapters1",
    //                       EnumAdapters1_Override, EnumAdapters1_Original,
    //                       EnumAdapters1_t);

    return ret;
  }

  _declspec (dllexport)
  HRESULT
  WINAPI
  CreateDXGIFactory2 (UINT            Flags,
                      REFIID          riid,
                         _Out_ void **ppFactory) {
    WaitForInit ();

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx (riid);

    DXGI_LOG_CALL_3 (L"CreateDXGIFactory2", L"0x%04X, %s, %08Xh",
                    Flags, iname.c_str (), ppFactory);

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory2EX (Flags, riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7, "IDXGIFactory2::EnumAdapters",
                           EnumAdapters_Override, EnumAdapters_Original,
                           EnumAdapters_t);
    //DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory2::EnumAdapters1",
    //                       EnumAdapters1_Override, EnumAdapters1_Original,
    //                       EnumAdapters1_t);

    return ret;
  }

#if 0
  DXGI_STUB (HRESULT, CreateDXGIFactory,
             (      REFIID     riid,
              _Out_ void     **ppFactory),
             (riid, ppFactory));

  DXGI_STUB (HRESULT, CreateDXGIFactory1,
             (      REFIID     riid,
              _Out_ void     **ppFactory),
             (riid, ppFactory));
  DXGI_STUB (HRESULT, CreateDXGIFactory2,
             (      UINT       Flags,
                    REFIID     riid,
              _Out_ void     **ppFactory),
             (Flags, riid, ppFactory));
#endif

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
  STDMETHODCALLTYPE
  DXGIDumpJournal (void)
  {
    DXGI_LOG_CALL_0 (L"DXGIDumpJournal");

    return E_NOTIMPL;
  }

  _declspec (dllexport)
  HRESULT
  STDMETHODCALLTYPE
  DXGIReportAdapterConfiguration (void)
  {
    DXGI_LOG_CALL_0 (L"DXGIReportAdapterConfiguration");

    return E_NOTIMPL;
  }

  static CRITICAL_SECTION init_mutex = { 0 };

  DWORD
  WINAPI
  DllThread (LPVOID param) {
    EnterCriticalSection (&init_mutex);
    BMF_Init ();
    LeaveCriticalSection (&init_mutex);

    return 0;
  }

  DWORD
  WINAPI
  BudgetThread (LPVOID user_data)
  {
    budget_thread_params_t* params =
      (budget_thread_params_t *)user_data;

    params->fLog = fopen ("dxgi_budget.log", "w");

    if (params->fLog) {
      params->tid    = GetCurrentThreadId ();
      params->event  = CreateEvent (NULL, FALSE, FALSE, L"DXGIMemoryBudget");
      InitializeCriticalSection (&params->log_mutex);
      params->silent = true;
      params->ready  = true;
    } else {
      params->tid    = 0;
      params->ready  = true; // Not really :P
      return -1;
    }

    enum buffer {
      Front = 0,
      Back  = 1,
      NumBuffers
    } buffer = Front;

    const int MAX_NODES = 4;

    struct mem_info_t {
      DXGI_QUERY_VIDEO_MEMORY_INFO local    [MAX_NODES];
      DXGI_QUERY_VIDEO_MEMORY_INFO nonlocal [MAX_NODES];
      SYSTEMTIME                   time;
    } mem_info [NumBuffers];

    while (params->ready) {
      WaitForSingleObject (params->event, INFINITE);

      if (! params->ready)
          break;

      if (buffer == Front)
        buffer = Back;
      else
        buffer = Front;

      GetLocalTime (&mem_info [buffer].time);

      int node = 0;

      while (node < MAX_NODES &&
             S_OK == params->pAdapter->QueryVideoMemoryInfo (
               node, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
               &mem_info [buffer].local [node++])) ;

      node = 0;

      while (node < MAX_NODES &&
             S_OK == params->pAdapter->QueryVideoMemoryInfo (
               node, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
               &mem_info [buffer].nonlocal [node++])) ;

      int nodes = node - 1;

      if (nodes > 0) {
        int i = 0;

        BMF_BLogEx (params, true, L"   [DXGI 1.3]: Local Memory.....:");

        while (i < nodes) {
#if 0
          static bool init = false;
          if (init == false) {
            params->pAdapter->SetVideoMemoryReservation (
              i, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
              mem_info [buffer].local [i].AvailableForReservation);
            params->pAdapter->SetVideoMemoryReservation (
              i, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
              mem_info [buffer].nonlocal [i].AvailableForReservation);
            if (i == nodes - 1)
              init = true;
          }
#endif

          mem_stats [i].budget_changes++;

          if (i > 0) {
            BMF_BLogEx (params, false, L"\n");
            BMF_BLogEx (params, true,  L"                                 ");
          }

          BMF_BLogEx (params, false,
                           L" Node%u (Reserve: %#5u / %#5u MiB - "
                           L"Budget: %#5u / %#5u MiB)",
                         i,
                 mem_info [buffer].local [i].CurrentReservation      >> 20ULL,
                 mem_info [buffer].local [i].AvailableForReservation >> 20ULL,
                 mem_info [buffer].local [i].CurrentUsage            >> 20ULL,
                 mem_info [buffer].local [i].Budget                  >> 20ULL);

          if (mem_stats [i].max_avail_reserve < 
              mem_info [buffer].local [i].AvailableForReservation)
            mem_stats [i].max_avail_reserve =
              mem_info [buffer].local [i].AvailableForReservation;

          if (mem_stats [i].min_avail_reserve >
              mem_info [buffer].local [i].AvailableForReservation)
            mem_stats [i].min_avail_reserve =
              mem_info [buffer].local [i].AvailableForReservation;

          if (mem_stats [i].max_reserve <
              mem_info [buffer].local [i].CurrentReservation)
            mem_stats [i].max_reserve =
              mem_info [buffer].local [i].CurrentReservation;

          if (mem_stats [i].min_usage >
              mem_info [buffer].local [i].CurrentUsage)
            mem_stats [i].min_usage =
              mem_info [buffer].local [i].CurrentUsage;

          if (mem_stats [i].max_budget < 
              mem_info [buffer].local [i].Budget)
            mem_stats [i].max_budget =
              mem_info [buffer].local [i].Budget;

          if (mem_stats [i].min_budget >
              mem_info [buffer].local [i].Budget)
            mem_stats [i].min_budget =
              mem_info [buffer].local [i].Budget;

          if (mem_stats [i].max_usage <
              mem_info [buffer].local [i].CurrentUsage)
            mem_stats [i].max_usage =
              mem_info [buffer].local [i].CurrentUsage;

          if (mem_stats [i].min_usage >
              mem_info [buffer].local [i].CurrentUsage)
            mem_stats [i].min_usage =
              mem_info [buffer].local [i].CurrentUsage;

          if (mem_info [buffer].local [i].CurrentUsage >
              mem_info [buffer].local [i].Budget) {
            uint64_t over_budget =
              mem_info [buffer].local [i].CurrentUsage -
                mem_info [buffer].local [i].Budget;

            if (mem_stats [i].min_over_budget > over_budget)
              mem_stats [i].min_over_budget = over_budget;
            if (mem_stats [i].max_over_budget < over_budget)
              mem_stats [i].max_over_budget = over_budget;
          }
#if 0
          static ULONGLONG last_tick [4] = { 0ULL, 0ULL, 0ULL, 0ULL };
          if (GetTickCount64 () - last_tick[i] > 1000000000000000ULL) {
          if (over_budget <= 0) {
            params->pAdapter->SetVideoMemoryReservation (
              i, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
              ((uint64_t)
                (float)mem_info [buffer].local [i].CurrentReservation * 1.01f)
                <= mem_info [buffer].local [i].AvailableForReservation ?
              ((uint64_t)
                (float)mem_info [buffer].local [i].CurrentReservation * 1.01f) :
               mem_info [buffer].local [i].AvailableForReservation);
          } else {
            params->pAdapter->SetVideoMemoryReservation (
              i, DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
              (uint64_t)
                (float)mem_info [buffer].local [i].CurrentReservation * 0.66f);
            }
          last_tick [i] = GetTickCount64 ();
          }
#else
#endif
          i++;
        }
        BMF_BLogEx (params, false, L"\n");

        i = 0;

        BMF_BLogEx (params, true,
                         L"   [DXGI 1.3]: Non-Local Memory.:");

        while (i < nodes) {
          if (i > 0) {
            BMF_BLogEx (params, false, L"\n");
            BMF_BLogEx (params, true,  L"                                 ");
          }

          BMF_BLogEx (params, false,
                           L" Node%u (Reserve: %#5u / %#5u MiB - "
                           L"Budget: %#5u / %#5u MiB)",
                         i,
              mem_info [buffer].nonlocal [i].CurrentReservation      >> 20ULL,
              mem_info [buffer].nonlocal [i].AvailableForReservation >> 20ULL,
              mem_info [buffer].nonlocal [i].CurrentUsage            >> 20ULL,
              mem_info [buffer].nonlocal [i].Budget                  >> 20ULL);
          i++;
        }
        BMF_BLogEx (params, false, L"\n");
      }
    }

    return 0;
  }

  static volatile HANDLE hInitThread = { 0 };

  void
  WaitForInit (void) {
    while (! hInitThread) ;

    WaitForSingleObject (hInitThread, INFINITE);
  }
}

BOOL
APIENTRY
DllMain ( HMODULE hModule,
          DWORD   ul_reason_for_call,
          LPVOID  lpReserved )
{
  switch (ul_reason_for_call)
  {
    case DLL_PROCESS_ATTACH:
      InitializeCriticalSection (&init_mutex);

      hInitThread = CreateThread (NULL, 0, DllThread, NULL, 0, NULL);
      break;

    case DLL_THREAD_ATTACH:
      //BMF_Log (L"Custom dxgi.dll Attached (tid=%x)", GetCurrentThreadId ());
      break;

    case DLL_THREAD_DETACH:
      //BMF_Log (L"Custom dxgi.dll Detached (tid=%x)", GetCurrentThreadId ());
      break;

    case DLL_PROCESS_DETACH:
    {
      if (hBudgetThread != NULL) {
        BMF_LogEx (true,
                   L"Shutting down DXGI 1.3 Memory Budget Change Thread... ");

        budget_thread->ready  = false;

        SignalObjectAndWait (budget_thread->event, hBudgetThread, INFINITE,
                             TRUE);

        BMF_LogEx (false, L"done!\n");

        budget_thread_params_t* params = budget_thread;

        // Record the final statistics always
        params->silent = false;

        BMF_BLogEx (params,
                    true, L"Shutdown Statistics:\n"
                          L"--------------------\n");

        BMF_BLog (params, L" Memory Budget Changed %d times\n",// in %10u seconds\n",
              mem_stats [0].budget_changes);

        for (int i = 0; i < 4; i++) {
          if (mem_stats [i].budget_changes > 0) {
            if (mem_stats [i].min_reserve == UINT64_MAX)
              mem_stats [i].min_reserve = 0ULL;

            if (mem_stats [i].min_over_budget == UINT64_MAX)
              mem_stats [i].min_over_budget = 0ULL;

        BMF_BLogEx (params, true, L" GPU%u: Min Budget:        %05u MiB\n", i,
                    mem_stats [i].min_budget >> 20ULL);
        BMF_BLogEx (params, true, L"       Max Budget:        %05u MiB\n",
                    mem_stats [i].max_budget >> 20ULL);

        BMF_BLogEx (params, true, L"       Min Usage:         %05u MiB\n",
                    mem_stats [i].min_usage >> 20ULL);
        BMF_BLogEx (params, true, L"       Max Usage:         %05u MiB\n",
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

        BMF_BLogEx (params, true, L"------------------------------------\n");
        BMF_BLogEx (params, true, L" Minimum Over Budget:     %05u MiB\n",
                    mem_stats [i].min_over_budget >> 20ULL);
        BMF_BLogEx (params, true, L" Maximum Over Budget:     %05u MiB\n",
                    mem_stats [i].max_over_budget >> 20ULL);
        BMF_BLogEx (params, true, L"------------------------------------\n");

        BMF_BLogEx (params, false, L"\n");
          }
        }

        //params->pAdapter->UnregisterVideoMemoryBudgetChangeNotification
          //(params->cookie);

        DeleteCriticalSection (&params->log_mutex);
        fclose                (params->fLog);

        //params->pAdapter->Release ();

        hBudgetThread = NULL;
      }

      BMF_Log (L"Custom dxgi.dll Detached (pid=0x%04x)",
               GetCurrentProcessId ());

      if (! bSilent)
        fclose (fLog);

      SymCleanup (GetCurrentProcess ());

      DeleteCriticalSection (&init_mutex);
      DeleteCriticalSection (&log_mutex);
    }
    break;
  }

  return TRUE;
}