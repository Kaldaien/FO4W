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

static CRITICAL_SECTION init_mutex = { 0 };

#include "minhook/MinHook.h"

// Disable SLI memory in Batman Arkham Knight
static bool USE_SLI = true;

HMODULE hParent;

extern "C" {
  // We have some really sneaky overlays that manage to call some of our
  //   exported functions before the DLL's even attached -- make them wait,
  //     so we don't crash and burn!
  void WaitForInit (void);

static HANDLE hBudgetThread = NULL;

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

static CRITICAL_SECTION d3dhook_mutex = { 0 };
static CRITICAL_SECTION budget_mutex  = { 0 };

struct budget_thread_params_t {
  IDXGIAdapter3*   pAdapter;
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
  if (fLog != NULL)
    fclose (fLog);

  silent = true;

  DeleteCriticalSection (&log_mutex);
}

bool
bmf_logger_t::init (const char* const szFileName,
                    const char* const szMode)
{
  if (initialized)
    return true;

  fLog = fopen (szFileName, szMode);

  InitializeCriticalSection (&log_mutex);

  if (fLog == NULL) {
    silent = true;
    return false;
  }

  return (initialized = true);
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
  vfwprintf (fLog, _Format, _ArgList);
  va_end (_ArgList);

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
  EnterCriticalSection (&init_mutex);
  if (hDxgi != NULL) {
    LeaveCriticalSection (&init_mutex);
    return;
  }

  HANDLE hProc = GetCurrentProcess ();

  FILE*   silent = NULL;
  errno_t err    = fopen_s (&silent, "dxgi.silent", "r");

  if (err == 0 && silent != NULL) {
    dxgi_log.silent = true;
    fclose (silent);
  }
  else {
    dxgi_log.init ("dxgi.log", "w");
    dxgi_log.silent = false;
  }

  dxgi_log.Log (L"dxgi.log created");

  dxgi_log.LogEx (false,
  L"------------------------------------------------------------------------"
  L"-----------\n");

  DWORD   dwProcessSize = MAX_PATH;
  wchar_t wszProcessName [MAX_PATH];

  QueryFullProcessImageName (hProc, 0, wszProcessName, &dwProcessSize);

  wchar_t* pwszShortName = wszProcessName + lstrlenW (wszProcessName);

  while (pwszShortName > wszProcessName && *(pwszShortName - 1) != L'\\')
    --pwszShortName;

  dxgi_log.Log (L">> (%s) <<", pwszShortName);

  if (! lstrcmpW (pwszShortName, L"BatmanAK.exe"))
    USE_SLI = false;

  dxgi_log.LogEx (false,
  L"------------------------------------------------------------------------"
  L"-----------\n");

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
    (CreateDXGIFactoryEX =  \
      (CreateDXGIFactory_t)GetProcAddress (hDxgi, "CreateDXGIFactory")));
  dxgi_log.Log (L"  CreateDXGIFactory1: %08Xh",
    (CreateDXGIFactory1EX = \
      (CreateDXGIFactory1_t)GetProcAddress (hDxgi, "CreateDXGIFactory1")));
  dxgi_log.Log (L"  CreateDXGIFactory2: %08Xh",
    (CreateDXGIFactory2EX = \
      (CreateDXGIFactory2_t)GetProcAddress (hDxgi, "CreateDXGIFactory2")));

  dxgi_log.LogEx (true, L" @ Loading Debug Symbols: ");
  SymInitializeW       (GetCurrentProcess (), NULL, TRUE);
  SymRefreshModuleList (GetCurrentProcess ());

  dxgi_log.LogEx (false, L"done!\n");

  dxgi_log.Log (L"=== Initialization Finished! ===\n");

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

#include "RTSSSharedMemory.h"

#include <shlwapi.h>
#include <float.h>
#include <io.h>
#include <tchar.h>

/////////////////////////////////////////////////////////////////////////////
BOOL UpdateOSD(LPCSTR lpText)
{
	BOOL bResult	= FALSE;

	HANDLE hMapFile =
    OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, "RTSSSharedMemoryV2");

	if (hMapFile)
	{
		LPVOID               pMapAddr =
      MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);

		LPRTSS_SHARED_MEMORY pMem	    =
      (LPRTSS_SHARED_MEMORY)pMapAddr;

		if (pMem)
		{
			if ((pMem->dwSignature == 'RTSS') && 
				(pMem->dwVersion >= 0x00020000))
			{
				for (DWORD dwPass=0; dwPass<2; dwPass++)
					//1st pass : find previously captured OSD slot
					//2nd pass : otherwise find the first unused OSD slot and capture it
				{
					for (DWORD dwEntry=1; dwEntry<pMem->dwOSDArrSize; dwEntry++)
						//allow primary OSD clients (i.e. EVGA Precision / MSI Afterburner)
            //  to use the first slot exclusively, so third party applications
            //    start scanning the slots from the second one
					{
						RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry =
              (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)
              ((LPBYTE)pMem + pMem->dwOSDArrOffset +
                              dwEntry * pMem->dwOSDEntrySize);

						if (dwPass)
						{
							if (!strlen(pEntry->szOSDOwner))
								strcpy(pEntry->szOSDOwner, "Batman Fix");
						}

						if (!strcmp(pEntry->szOSDOwner, "Batman Fix"))
						{
							if (pMem->dwVersion >= 0x00020007)
								//use extended text slot for v2.7 and higher shared memory,
                // it allows displaying 4096 symbols instead of 256 for regular
                //   text slot
								strncpy(pEntry->szOSDEx, lpText, sizeof(pEntry->szOSDEx) - 1);	
							else
								strncpy(pEntry->szOSD, lpText, sizeof(pEntry->szOSD) - 1);

							pMem->dwOSDFrame++;

							bResult = TRUE;

							break;
						}
					}

					if (bResult)
						break;
				}
			}

			UnmapViewOfFile(pMapAddr);
		}

		CloseHandle(hMapFile);
	}

	return bResult;
}
/////////////////////////////////////////////////////////////////////////////
void ReleaseOSD (void)
{
	HANDLE hMapFile =
    OpenFileMappingA (FILE_MAP_ALL_ACCESS, FALSE, "RTSSSharedMemoryV2");

	if (hMapFile)
	{
		LPVOID pMapAddr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);

		LPRTSS_SHARED_MEMORY pMem = (LPRTSS_SHARED_MEMORY)pMapAddr;

		if (pMem)
		{
			if ((pMem->dwSignature == 'RTSS') && 
				(pMem->dwVersion >= 0x00020000))
			{
				for (DWORD dwEntry=1; dwEntry<pMem->dwOSDArrSize; dwEntry++)
				{
					RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry =
            (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)
            ((LPBYTE)pMem + pMem->dwOSDArrOffset +
                  dwEntry * pMem->dwOSDEntrySize);

					if (! strcmp (pEntry->szOSDOwner, "Batman Fix"))
					{
						memset (pEntry, 0, pMem->dwOSDEntrySize);
						pMem->dwOSDFrame++;
					}
				}
			}

			UnmapViewOfFile(pMapAddr);
		}

		CloseHandle(hMapFile);
	}
}

typedef HRESULT (WINAPI* PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)( _In_opt_ IDXGIAdapter*, 
    D3D_DRIVER_TYPE, HMODULE, UINT, 
    _In_reads_opt_( FeatureLevels ) CONST D3D_FEATURE_LEVEL*, 
    UINT FeatureLevels, UINT, _In_opt_ CONST DXGI_SWAP_CHAIN_DESC*, 
    _Out_opt_ IDXGISwapChain**, _Out_opt_ ID3D11Device**, 
    _Out_opt_ D3D_FEATURE_LEVEL*, _Out_opt_ ID3D11DeviceContext** );

volatile PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN
  D3D11CreateDeviceAndSwapChainEX = NULL;

typedef HRESULT (STDMETHODCALLTYPE* PFN_PRESENT_SWAP_CHAIN)(
                                                 IDXGISwapChain* This,
                                                 UINT            SyncInterval,
                                                 UINT            Flags);
PFN_PRESENT_SWAP_CHAIN Present_Original;

HRESULT
WINAPI
PresentCallback (IDXGISwapChain* This,
                 UINT            SyncInterval,
                 UINT            Flags)
{
  return Present_Original (This, SyncInterval, Flags);
}

typedef HRESULT (STDMETHODCALLTYPE *CreateSwapChain_t)(
                  IDXGIFactory          *This,
            _In_  IUnknown              *pDevice,
            _In_  DXGI_SWAP_CHAIN_DESC  *pDesc,
            _Out_  IDXGISwapChain      **ppSwapChain);

CreateSwapChain_t CreateSwapChain_Original = nullptr;

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
      Present_Original == nullptr) {
    ID3D11Device* pDev;
    budget_log.silent = false;
    if (SUCCEEDED (pDevice->QueryInterface (__uuidof (ID3D11Device),
                                            (void **)&pDev))) {

      budget_log.LogEx (true, L"Hooking IDXGISwapChain::Present... ");
      DXGI_VIRTUAL_OVERRIDE (ppSwapChain, 8, "IDXGISwapChain::Present",
                             PresentCallback, Present_Original,
                             PFN_PRESENT_SWAP_CHAIN);
      //(*ppSwapChain)->AddRef ();
      budget_log.LogEx (false, L"Done\n");
      budget_log.silent = true;
    }
  }

  return ret;
}

HRESULT WINAPI D3D11CreateDeviceAndSwapChain (
  _In_opt_ IDXGIAdapter* pAdapter,
  D3D_DRIVER_TYPE DriverType,
  HMODULE Software,
  UINT Flags,
  _In_reads_opt_ (FeatureLevels) CONST D3D_FEATURE_LEVEL* pFeatureLevels,
  UINT FeatureLevels,
  UINT SDKVersion,
  _In_opt_ CONST DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
  _Out_opt_ IDXGISwapChain** ppSwapChain,
  _Out_opt_ ID3D11Device** ppDevice,
  _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
  _Out_opt_ ID3D11DeviceContext** ppImmediateContext) {
  DXGI_LOG_CALL_0 (L"D3D11CreateDeviceAndSwapChain");
  HRESULT res = D3D11CreateDeviceAndSwapChainEX (pAdapter,
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

  if (res == S_OK && (ppDevice != NULL))
    dxgi_log.Log (L" >> Device = 0x%08Xh", *ppDevice);

  return res;
}
typedef HRESULT (STDMETHODCALLTYPE  *GetDesc2_t)(
                 IDXGIAdapter2      *This,
          _Out_  DXGI_ADAPTER_DESC2 *pDesc);

GetDesc2_t GetDesc2_Original = NULL;

HRESULT STDMETHODCALLTYPE GetDesc2_Override (IDXGIAdapter2*      This,
                                      _Out_  DXGI_ADAPTER_DESC2* pDesc)
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

typedef HRESULT (STDMETHODCALLTYPE  *GetDesc1_t)(
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

volatile bool init = false;

#if 1
DWORD
WINAPI
HookThread (LPVOID user)
{
  if (init) {
    //LeaveCriticalSection (&d3dhook_mutex);
    return 1;
  }

  init = true;

  LoadLibrary (L"d3d11.dll");

  dxgi_log.LogEx (true, L"Hooking D3D11CreateDeviceAndSwapChain... ");

  MH_STATUS stat = MH_CreateHookApi (L"d3d11.dll",
                                     "D3D11CreateDeviceAndSwapChain",
                                     D3D11CreateDeviceAndSwapChain,
                                     (void **)&D3D11CreateDeviceAndSwapChainEX);

  if (stat != MH_ERROR_ALREADY_CREATED &&
      stat != MH_OK) {
    dxgi_log.LogEx (false, L" failed\n");
  }
  else {
    dxgi_log.LogEx (false, L" %p\n", D3D11CreateDeviceAndSwapChainEX);
  }

  dxgi_log.LogEx (false, L"\n");

  MH_ApplyQueued ();
  MH_EnableHook  (MH_ALL_HOOKS);

  return 0;
}
#endif

void
BMF_InstallD3D11DeviceHooks (void)
{
  WaitForInit ();

  EnterCriticalSection (&d3dhook_mutex);
  HANDLE hThread = CreateThread (NULL, 0, HookThread, NULL, 0, NULL);
  WaitForSingleObject  (hThread, INFINITE);
  LeaveCriticalSection (&d3dhook_mutex);
}


HRESULT STDMETHODCALLTYPE EnumAdapters_Common (IDXGIFactory   *This,
                                               UINT            Adapter,
                                        _Out_  IDXGIAdapter  **ppAdapter,
                                            DXGI_ADAPTER_DESC *pDesc,
                                               EnumAdapters_t  pFunc)
{
  // Logic to skip Intel and Microsoft adapters and return only AMD / NV
  if (lstrlenW (pDesc->Description)) {
    if (pDesc->VendorId == Microsoft || pDesc->VendorId == Intel) {
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
            L"   # SLI Detected (Corrected Memory Total: %u MiB -- "
            L"Original: %u MiB)",
                          match->DedicatedVideoMemory >> 20ULL,
                          pDesc->DedicatedVideoMemory >> 20ULL);
        }
      }

      IDXGIAdapter3* pAdapter3;
      if (S_OK ==
          (*ppAdapter)->QueryInterface (
            __uuidof (IDXGIAdapter3), (void **)&pAdapter3)) {
        EnterCriticalSection (&budget_mutex);
        if (hBudgetThread == NULL) {
          // We're going to Release this interface after this loop, but
          //   the running thread still needs a reference counted.
          pAdapter3->AddRef ();

          DWORD WINAPI BudgetThread (LPVOID user_data);

          if (budget_thread == nullptr) {
            budget_thread =
              new budget_thread_params_t ();
          }

          dxgi_log.LogEx (true,
                L"   $ Spawning DXGI 1.3 Memory Budget Change Thread.: ");

          budget_thread->pAdapter = pAdapter3;
          budget_thread->tid      = 0;
          budget_thread->event    = 0;
          budget_thread->ready    = false;
          budget_log.silent = true;

          hBudgetThread =
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

            if (result == S_OK) {
              dxgi_log.LogEx (false, L"eid=0x%x, cookie=%u\n",
                          budget_thread->event, budget_thread->cookie);

              // Immediately run the event loop one time
              //SignalObjectAndWait (budget_thread->event, hBudgetThread,
                                //0, TRUE);
            } else {
              dxgi_log.LogEx (false, L"Failed! (%s)\n",
                          BMF_DescribeHRESULT (result));
            }
          } else {
            dxgi_log.LogEx (false, L"failed!\n");
          }
        }
        LeaveCriticalSection (&budget_mutex);
        int i = 0;

        dxgi_log.LogEx (true,
                    L"   [DXGI 1.3]: Local Memory.....:");

        DXGI_QUERY_VIDEO_MEMORY_INFO mem_info;
        while (S_OK ==
                pAdapter3->QueryVideoMemoryInfo (
                  i,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&mem_info)) {

          if (i > 0) {
            dxgi_log.LogEx (false, L"\n");
            dxgi_log.LogEx (true,  L"                                 ");
          }
          dxgi_log.LogEx (false,
                      L" Node%u (Reserve: %#5u / %#5u MiB - "
                      L"Budget: %#5u / %#5u MiB)",
                      i++,
                      mem_info.CurrentReservation      >> 20ULL,
                      mem_info.AvailableForReservation >> 20ULL,
                      mem_info.CurrentUsage            >> 20ULL,
                      mem_info.Budget                  >> 20ULL);

          pAdapter3->SetVideoMemoryReservation (
                    i-1,
                      DXGI_MEMORY_SEGMENT_GROUP_LOCAL,
                        (i == 1 || USE_SLI) ? 
                          mem_info.AvailableForReservation :
                          0
          );
        }
        dxgi_log.LogEx (false, L"\n");

        i = 0;

        dxgi_log.LogEx (true,
                    L"   [DXGI 1.3]: Non-Local Memory.:");

        while ( S_OK ==
                pAdapter3->QueryVideoMemoryInfo (
                  i,
                    DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                      &mem_info
                )
                )
        {
          if (i > 0) {
            dxgi_log.LogEx (false, L"\n");
            dxgi_log.LogEx (true,  L"                                 ");
          }
          dxgi_log.LogEx (false,
                      L" Node%u (Reserve: %#5u / %#5u MiB - "
                      L"Budget: %#5u / %#5u MiB)",
                    i++,
                      mem_info.CurrentReservation      >> 20ULL,
                      mem_info.AvailableForReservation >> 20ULL,
                      mem_info.CurrentUsage            >> 20ULL,
                      mem_info.Budget                  >> 20ULL );

            pAdapter3->SetVideoMemoryReservation (
                    i-1,
                      DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                        (i == 1 || USE_SLI) ?
                          mem_info.AvailableForReservation :
                          0
          );
        }
      }

      dxgi_log.LogEx (false, L"\n");

      // This sounds good in theory, but we have a tendancy to underflow
      //   the reference counter and I don't know why you'd even really
      //     care about references to a DXGI adapter. Keep it alvie as
      //       long as possible to prevent nasty things from happening.
      //pAdapter3->Release ();
    }

    dxgi_log.Log(L"   @ Returning Adapter w/ Name: %s\n",pDesc->Description);
  }

  return S_OK;
}

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

  _declspec (dllexport)
  HRESULT
  WINAPI
  CreateDXGIFactory (REFIID         riid,
                       _Out_ void** ppFactory) {
    BMF_InstallD3D11DeviceHooks ();

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx (riid);

    DXGI_LOG_CALL_2 (L"CreateDXGIFactory", L"%s, %08Xh",
                     iname.c_str (), ppFactory);

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactoryEX (riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7, "IDXGIFactory::EnumAdapters",
                           EnumAdapters_Override, EnumAdapters_Original,
                           EnumAdapters_t);

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory::CreateSwapChain",
                           CreateSwapChain_Override, CreateSwapChain_Original,
                           CreateSwapChain_t);

    return ret;
  }

  _declspec (dllexport)
  HRESULT
  WINAPI
  CreateDXGIFactory1 (REFIID          riid,
                         _Out_ void** ppFactory) {
    BMF_InstallD3D11DeviceHooks ();

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx (riid);

    DXGI_LOG_CALL_2 (L"CreateDXGIFactory1", L"%s, %08Xh",
                     iname.c_str (), ppFactory);

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory1EX (riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7,  "IDXGIFactory1::EnumAdapters",
                           EnumAdapters_Override,  EnumAdapters_Original,
                           EnumAdapters_t);
    DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory1::EnumAdapters1",
                           EnumAdapters1_Override, EnumAdapters1_Original,
                           EnumAdapters1_t);

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory::CreateSwapChain",
                           CreateSwapChain_Override, CreateSwapChain_Original,
                           CreateSwapChain_t);

    return ret;
  }

  _declspec (dllexport)
  HRESULT
  WINAPI
  CreateDXGIFactory2 (UINT            Flags,
                      REFIID          riid,
                         _Out_ void **ppFactory) {
    BMF_InstallD3D11DeviceHooks ();

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx (riid);

    DXGI_LOG_CALL_3 (L"CreateDXGIFactory2", L"0x%04X, %s, %08Xh",
                    Flags, iname.c_str (), ppFactory);

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory2EX (Flags, riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7, "IDXGIFactory2::EnumAdapters",
                           EnumAdapters_Override, EnumAdapters_Original,
                           EnumAdapters_t);
    DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory2::EnumAdapters1",
                           EnumAdapters1_Override, EnumAdapters1_Original,
                           EnumAdapters1_t);

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory::CreateSwapChain",
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

  DWORD
  WINAPI
  DllThread (LPVOID param) {
    EnterCriticalSection (&init_mutex);
    BMF_Init ();
    LeaveCriticalSection (&init_mutex);

    return 0;
  }

#include <ctime>
const uint32_t BUDGET_POLL_INTERVAL = 66UL; // How often to sample the budget
                                            //  in msecs

#define min_max(ref,min,max) if ((ref) > (max)) (max) = (ref); \
                             if ((ref) < (min)) (min) = (ref);

#define OSD_PRINTF if (print_mem_stats) { pszOSD += sprintf (pszOSD,
#define OSD_END    ); }

  DWORD
  WINAPI
  BudgetThread (LPVOID user_data)
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

    HANDLE hThreadHeap =  HeapCreate (0, 0, 0);
    char* szOSD  = (char *)HeapAlloc (hThreadHeap, HEAP_ZERO_MEMORY, 4096);
    char* pszOSD = szOSD;

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
      DWORD dwWaitStatus = WaitForSingleObject (params->event,
                                                BUDGET_POLL_INTERVAL);

      static bool print_mem_stats = true;
#if 0
      BYTE keys [256];
      GetKeyboardState (keys);

      if (keys [VK_CONTROL] && keys [VK_SHIFT] && keys ['M'])
#else
      static bool toggle = false;
      if (HIWORD (GetAsyncKeyState (VK_CONTROL)) &&
          HIWORD (GetAsyncKeyState (VK_SHIFT))   &&
          HIWORD (GetAsyncKeyState ('M'))) {
        if (! toggle)
          print_mem_stats = (! print_mem_stats);
        toggle = true;
      } else {
        toggle = false;
      }
#endif

      if (! params->ready) {
        ResetEvent (params->event);
        break;
      }

      //if (dwWaitStatus != WAIT_OBJECT_0)
        //continue;

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

#if 0
      static time_t last_flush = time (NULL);
      static time_t last_empty = time (NULL);


      static uint64_t last_budget =
        mem_info [buffer].local [0].Budget;

      static bool queued_flush = false;

      if (dwWaitStatus == WAIT_OBJECT_0 && last_budget > mem_info [buffer].local [0].Budget)
        queued_flush = true;

      if (FlushAllCaches != nullptr
          && (last_budget > mem_info [buffer].local [0].Budget && time (NULL) - last_flush > 2 ||
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
        } heap_opt;
        
        heap_opt.version = 1;
        heap_opt.length  = sizeof (heap_opt_t);

        HeapSetInformation (NULL, (_HEAP_INFORMATION_CLASS)3, &heap_opt, heap_opt.length);

        budget_log.silent = silence;
        queued_flush = false;
      }
#endif
      //if (dwWaitStatus == WAIT_OBJECT_0)
      //last_budget = mem_info [buffer].local [0].Budget;

      pszOSD = szOSD;

      if (nodes > 0) {
        int i = 0;

        budget_log.LogEx (true, L"   [DXGI 1.3]: Local Memory.....:");

        OSD_PRINTF "\n"
                                   "----- [DXGI 1.3]: Local Memory -----------"
                                   "------------------------------------------"
                                   "-\n"
          OSD_END

        while (i < nodes) {
          if (dwWaitStatus == WAIT_OBJECT_0)
            mem_stats [i].budget_changes++;

          if (i > 0) {
            budget_log.LogEx (false, L"\n");
            budget_log.LogEx (true,  L"                                 ");
          }
          OSD_PRINTF "  %8s %u  (Reserve:  %05u / %05u MiB  - "
                     " Budget:  %05u / %05u MiB)\n",
                   nodes > 1 ? (nvapi_init ? "SLI Node" : "CFX Node") : "GPU",
                   i,
                   mem_info [buffer].local [i].CurrentReservation >> 20ULL,
                   mem_info [buffer].local [i].AvailableForReservation >> 20ULL,
                   mem_info [buffer].local [i].CurrentUsage >> 20ULL,
                   mem_info [buffer].local [i].Budget >> 20ULL
            OSD_END

          budget_log.LogEx (false,
                           L" Node%u (Reserve: %#5u / %#5u MiB - "
                           L"Budget: %#5u / %#5u MiB)",
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

        OSD_PRINTF "----- [DXGI 1.3]: Non-Local Memory -------"
                   "------------------------------------------"
                   "\n"
        OSD_END

        budget_log.LogEx (true,
                          L"   [DXGI 1.3]: Non-Local Memory.:");

        while (i < nodes) {
          if (i > 0) {
            budget_log.LogEx (false, L"\n");
            budget_log.LogEx (true,  L"                                 ");
          }

          OSD_PRINTF "  %8s %u  (Reserve:  %05u / %05u MiB  -  "
                     "Budget:  %05u / %05u MiB)\n",
                         nodes > 1 ? "SLI Node" : "GPU",
                         i,
              mem_info [buffer].nonlocal [i].CurrentReservation      >> 20ULL,
              mem_info [buffer].nonlocal [i].AvailableForReservation >> 20ULL,
              mem_info [buffer].nonlocal [i].CurrentUsage            >> 20ULL,
              mem_info [buffer].nonlocal [i].Budget                  >> 20ULL
          OSD_END

          budget_log.LogEx (false,
                           L" Node%u (Reserve: %#5u / %#5u MiB - "
                           L"Budget: %#5u / %#5u MiB)",
                         i,
              mem_info [buffer].nonlocal [i].CurrentReservation      >> 20ULL,
              mem_info [buffer].nonlocal [i].AvailableForReservation >> 20ULL,
              mem_info [buffer].nonlocal [i].CurrentUsage            >> 20ULL,
              mem_info [buffer].nonlocal [i].Budget                  >> 20ULL);
          i++;
        }

        OSD_PRINTF "----- [DXGI 1.3]: Miscellaneous ----------"
                   "------------------------------------------"
                   "---\n"
        OSD_END

        int64_t headroom = mem_info [buffer].local [0].Budget -
                           mem_info [buffer].local [0].CurrentUsage;

        OSD_PRINTF "  Max. Resident Set:  %05u MiB  -"
                   "  Max. Over Budget:  %05u MiB\n"
                   "    Budget Changes:  %06u       -    "
                   "       Budget Left:  %05i MiB",
                                        mem_stats [0].max_usage       >> 20ULL,
                                        mem_stats [0].max_over_budget >> 20ULL,
                                        mem_stats [0].budget_changes,
                           headroom / 1024 / 1024
        OSD_END

        budget_log.LogEx (false, L"\n");
      }

      if (print_mem_stats)
        UpdateOSD (szOSD);
      else
        UpdateOSD (" ");

      ResetEvent (params->event);
    }

    HeapFree    (hThreadHeap, 0, szOSD);
    HeapDestroy (hThreadHeap);

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
    {
      MH_Initialize ();

      //LoadLibrary (L"d3d11.dll");

      InitializeCriticalSection (&init_mutex);
      InitializeCriticalSection (&budget_mutex);
      InitializeCriticalSection (&d3dhook_mutex);

      hInitThread = CreateThread (NULL, 0, DllThread, NULL, 0, NULL);
      WaitForSingleObject (hInitThread, 100U);
    } break;

    case DLL_THREAD_ATTACH:
      //dxgi_log.Log (L"Custom dxgi.dll Attached (tid=%x)", GetCurrentThreadId ());
      break;

    case DLL_THREAD_DETACH:
      //dxgi_log.Log (L"Custom dxgi.dll Detached (tid=%x)", GetCurrentThreadId ());
      break;

    case DLL_PROCESS_DETACH:
    {
      if (hBudgetThread != NULL) {
        dxgi_log.LogEx (
              true,
                L"Shutting down DXGI 1.3 Memory Budget Change Thread... "
        );

        budget_thread->ready  = false;

        SignalObjectAndWait (budget_thread->event, hBudgetThread, INFINITE,
                             TRUE);

        dxgi_log.LogEx (false, L"done!\n");

        budget_thread_params_t* params = budget_thread;

        // Record the final statistics always
        budget_log.silent = false;

        budget_log.LogEx (false, L"\n");
        budget_log.Log   (L"--------------------");
        budget_log.Log   (L"Shutdown Statistics:");
        budget_log.Log   (L"--------------------\n");

                                         // in %10u seconds\n",
        budget_log.Log (L" Memory Budget Changed %d times\n",
              mem_stats [0].budget_changes);

        for (int i = 0; i < 4; i++) {
          if (mem_stats [i].budget_changes > 0) {
            if (mem_stats [i].min_reserve == UINT64_MAX)
              mem_stats [i].min_reserve = 0ULL;

            if (mem_stats [i].min_over_budget == UINT64_MAX)
              mem_stats [i].min_over_budget = 0ULL;

        budget_log.LogEx (true, L" GPU%u: Min Budget:        %05u MiB\n", i,
                    mem_stats [i].min_budget >> 20ULL);
        budget_log.LogEx (true, L"       Max Budget:        %05u MiB\n",
                    mem_stats [i].max_budget >> 20ULL);

        budget_log.LogEx (true, L"       Min Usage:         %05u MiB\n",
                    mem_stats [i].min_usage >> 20ULL);
        budget_log.LogEx (true, L"       Max Usage:         %05u MiB\n",
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
        budget_log.LogEx (true, L" Minimum Over Budget:     %05u MiB\n",
                    mem_stats [i].min_over_budget >> 20ULL);
        budget_log.LogEx (true, L" Maximum Over Budget:     %05u MiB\n",
                    mem_stats [i].max_over_budget >> 20ULL);
        budget_log.LogEx (true, L"------------------------------------\n");

        budget_log.LogEx (false, L"\n");
          }
        }

        //params->pAdapter->UnregisterVideoMemoryBudgetChangeNotification
          //(params->cookie);

        //params->pAdapter->Release ();

        hBudgetThread = NULL;
      }

      ReleaseOSD ();

      SymCleanup (GetCurrentProcess ());

      DeleteCriticalSection (&init_mutex);
      DeleteCriticalSection (&d3dhook_mutex);

      MH_Uninitialize ();

      dxgi_log.Log (L"Custom dxgi.dll Detached (pid=0x%04x)",
               GetCurrentProcessId ());

      budget_log.close ();
      dxgi_log.close   ();
    }
    break;
  }

  return TRUE;
}