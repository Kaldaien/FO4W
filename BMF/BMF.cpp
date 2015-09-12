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


static BOOL    bSilent = FALSE;
static FILE*   fLog    = nullptr;
static HMODULE hDxgi   = NULL;

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
    err = fopen_s (&fLog, "dxgi.log", "w");
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
  SymInitialize        (GetCurrentProcess (), NULL, TRUE);
  SymRefreshModuleList (GetCurrentProcess ());

  BMF_LogEx (false, L"done!\n");

  BMF_Log (L"=== Initialization Finished! ===\n");
}


#define DXGI_CALL(_Ret, _Call) {                                  \
  BMF_LogEx (true, L"  Calling original function: ");             \
  (_Ret) = (_Call);                                               \
  BMF_LogEx (false, L"(ret=%s)\n\n", BMF_DescribeHRESULT (_Ret)); \
}

#define DXGI_LOG_CALL(_Name,_Format)                                         \
  BMF_LogEx (true, L"[!] %s (", _Name);                                      \
  BMF_LogEx (false, _Format
#define DXGI_LOG_CALL_END                                                    \
  BMF_LogEx (false, L") -- [Calling Thread: 0x%04x]\n",GetCurrentThreadId ());

#define DXGI_LOG_CALL0(_Name) {                                              \
  DXGI_LOG_CALL     (_Name, L"void"));                                       \
  DXGI_LOG_CALL_END                                                          \
}

#define DXGI_LOG_CALL1(_Name,_Format,_Args) {                                \
  DXGI_LOG_CALL     (_Name, _Format), _Args);                                \
  DXGI_LOG_CALL_END                                                          \
}

#define DXGI_LOG_CALL2(_Name,_Format,_Args0,_Args1) {                        \
  DXGI_LOG_CALL     (_Name, _Format), _Args0, _Args1);                       \
  DXGI_LOG_CALL_END                                                          \
}

#define DXGI_LOG_CALL3(_Name,_Format,_Args0,_Args1,_Args2) {                 \
  DXGI_LOG_CALL     (_Name, _Format), _Args0, _Args1, _Args2);               \
  DXGI_LOG_CALL_END                                                          \
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
  DXGI_LOG_CALL2 (L"IDXGIAdapter1::GetDesc1", L"%08Xh, %08Xh", This, pDesc);

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
  DXGI_LOG_CALL2 (L"IDXGIAdapter::GetDesc", L"%08Xh, %08Xh", This, pDesc);

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
  DXGI_LOG_CALL3 (L"IDXGIFactory1::EnumAdapters1", L"%08Xh, %u, %08Xh",
                  This, Adapter, ppAdapter);

  HRESULT ret;
  DXGI_CALL (ret, EnumAdapters1_Original (This,Adapter,ppAdapter));

  if (ret == S_OK) {
    // GetDesc1 calls GetDesc...
    //DXGI_VIRTUAL_OVERRIDE (ppAdapter,  8, "(*ppAdapter)->GetDesc",
    //                       GetDesc_Override,  GetDesc_Original, GetDesc_t);
    DXGI_VIRTUAL_OVERRIDE (ppAdapter, 10, "(*ppAdapter)->GetDesc1",
                           GetDesc1_Override, GetDesc1_Original, GetDesc1_t);

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
  DXGI_LOG_CALL3 (L"IDXGIFactory::EnumAdapters", L"%08Xh, %u, %08Xh",
                  This, Adapter, ppAdapter);

  HRESULT ret;
  DXGI_CALL (ret, EnumAdapters_Original (This, Adapter, ppAdapter));

  if (ret == S_OK) {
    DXGI_VIRTUAL_OVERRIDE (ppAdapter, 8, "(*ppAdapter)->GetDesc",
                           GetDesc_Override, GetDesc_Original, GetDesc_t);

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

    wchar_t* pwszIID;
    StringFromIID (riid, (LPOLESTR *)&pwszIID);

    DXGI_LOG_CALL2 (L"CreateDXGIFactory", L"%s, %08Xh", pwszIID, ppFactory);

    CoTaskMemFree (pwszIID);

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

    wchar_t* pwszIID;
    StringFromIID (riid, (LPOLESTR *)&pwszIID);

    DXGI_LOG_CALL2 (L"CreateDXGIFactory1", L"%s, %08Xh", pwszIID, ppFactory);

    CoTaskMemFree (pwszIID);

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory1EX (riid, ppFactory));

    //DXGI_VIRTUAL_OVERRIDE (ppFactory, 7,  "IDXGIFactory1::EnumAdapters",
    //                       EnumAdapters_Override,  EnumAdapters_Original,
    //                       EnumAdapters_t);
    DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory1::EnumAdapters1",
                           EnumAdapters1_Override, EnumAdapters1_Original,
                           EnumAdapters1_t);

    return ret;
  }

  _declspec (dllexport)
  HRESULT
  WINAPI
  CreateDXGIFactory2 (UINT            Flags,
                      REFIID          riid,
                         _Out_ void **ppFactory) {
    WaitForInit ();

    wchar_t* pwszIID;
    StringFromIID (riid, (LPOLESTR *)&pwszIID);

    DXGI_LOG_CALL3 (L"CreateDXGIFactory2", L"0x%04X, %s, %08Xh",
                    Flags, pwszIID, ppFactory);

    CoTaskMemFree (pwszIID);

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory2EX (Flags, riid, ppFactory));

    //DXGI_VIRTUAL_OVERRIDE (ppFactory, 7, "IDXGIFactory2::EnumAdapters",
    //                       EnumAdapters_Override, EnumAdapters_Original,
    //                       EnumAdapters_t);
    DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory2::EnumAdapters1",
                           EnumAdapters1_Override, EnumAdapters1_Original,
                           EnumAdapters1_t);

    return ret;
  }

#if 0
  DXGI_STUB (HRESULT, CreateDXGIFactory0,
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
    DXGI_LOG_CALL0 (L"DXGIDumpJournal");

    return E_NOTIMPL;
  }

  _declspec (dllexport)
  HRESULT
  STDMETHODCALLTYPE
  DXGIReportAdapterConfiguration (void)
  {
    DXGI_LOG_CALL0 (L"DXGIReportAdapterConfiguration");

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
      BMF_Log (L"Custom dxgi.dll Detached (pid=0x%04x)",
               GetCurrentProcessId ());

      if (! bSilent)
        fclose (fLog);

      SymCleanup (GetCurrentProcess ());

      DeleteCriticalSection (&init_mutex);
      DeleteCriticalSection (&log_mutex);
     break;
  }

  return TRUE;
}