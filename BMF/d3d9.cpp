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

#include <cstdio>
#include <cstdlib>
#include <string>

#include "log.h"

#undef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE __cdecl

#include <d3d9types.h>

typedef interface IDirect3D9                     IDirect3D9;
typedef interface IDirect3DDevice9               IDirect3DDevice9;
typedef interface IDirect3DSwapChain9            IDirect3DSwapChain9;
typedef interface IDirect3D9Ex                   IDirect3D9Ex;
typedef interface IDirect3DDevice9Ex             IDirect3DDevice9Ex;
typedef interface IDirect3DSwapChain9Ex          IDirect3DSwapChain9Ex;

extern "C" {
typedef IDirect3D9*
  (STDMETHODCALLTYPE *Direct3DCreate9PROC)(  UINT           SDKVersion);
typedef HRESULT
  (STDMETHODCALLTYPE *Direct3DCreate9ExPROC)(UINT           SDKVersion,
                                             IDirect3D9Ex** d3d9ex);

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

void d3d9_init_callback (void)
{
  dll_log.Log (L"Importing Direct3DCreate9{Ex}...");
  dll_log.Log (L"================================");

  dll_log.Log (L"  Direct3DCreate9:   %08Xh", 
    (Direct3DCreate9_Import) =  \
      (Direct3DCreate9PROC)GetProcAddress (backend_dll, "Direct3DCreate9"));
  dll_log.Log (L"  Direct3DCreate9Ex: %08Xh",
    (Direct3DCreate9Ex_Import) =  \
      (Direct3DCreate9ExPROC)GetProcAddress (backend_dll, "Direct3DCreate9Ex"));
}
}

bool
BMF::D3D9::Startup (void)
{
  return BMF_StartupCore (L"d3d9", d3d9_init_callback);
}

bool
BMF::D3D9::Shutdown (void)
{
  return BMF_ShutdownCore (L"d3d9");
}

extern "C" {
  HRESULT
  __cdecl D3D9PresentCallback (IDirect3DSwapChain9 *This,
                    _In_ const RECT                *pSourceRect,
                    _In_ const RECT                *pDestRect,
                    _In_       HWND                 hDestWindowOverride,
                    _In_ const RGNDATA             *pDirtyRegion,
                    _In_       DWORD                dwFlags)
  {
    BMF_BeginBufferSwap ();

    return BMF_EndBufferSwap (D3D9Present_Original (This,
                                                    pSourceRect,
                                                    pDestRect,
                                                    hDestWindowOverride,
                                                    pDirtyRegion,
                                                    dwFlags));
  }
}


#define D3D9_STUB_HRESULT(_Return, _Name, _Proto, _Args)                  \
  __declspec (dllexport) _Return STDMETHODCALLTYPE                        \
  _Name _Proto {                                                          \
    WaitForInit ();                                                       \
                                                                          \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;            \
    static passthrough_t _default_impl = nullptr;                         \
                                                                          \
    if (_default_impl == nullptr) {                                       \
      static const char* szName = #_Name;                                 \
      _default_impl = (passthrough_t)GetProcAddress (backend_dll, szName);\
                                                                          \
      if (_default_impl == nullptr) {                                     \
        dll_log.Log (                                                     \
          L"Unable to locate symbol  %s in d3d9.dll",                     \
          L#_Name);                                                       \
        return E_NOTIMPL;                                                 \
      }                                                                   \
    }                                                                     \
                                                                          \
    dll_log.Log (L"[!] %s %s - "                                          \
             L"[Calling Thread: 0x%04x]",                                 \
      L#_Name, L#_Proto, GetCurrentThreadId ());                          \
                                                                          \
    return _default_impl _Args;                                           \
}

#define D3D9_STUB_VOIDP(_Return, _Name, _Proto, _Args)                    \
  __declspec (dllexport) _Return STDMETHODCALLTYPE                        \
  _Name _Proto {                                                          \
    WaitForInit ();                                                       \
                                                                          \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;            \
    static passthrough_t _default_impl = nullptr;                         \
                                                                          \
    if (_default_impl == nullptr) {                                       \
      static const char* szName = #_Name;                                 \
      _default_impl = (passthrough_t)GetProcAddress (backend_dll, szName);\
                                                                          \
      if (_default_impl == nullptr) {                                     \
        dll_log.Log (                                                     \
          L"Unable to locate symbol  %s in d3d9.dll",                     \
          L#_Name);                                                       \
        return nullptr;                                                   \
      }                                                                   \
    }                                                                     \
                                                                          \
    dll_log.Log (L"[!] %s %s - "                                          \
             L"[Calling Thread: 0x%04x]",                                 \
      L#_Name, L#_Proto, GetCurrentThreadId ());                          \
                                                                          \
    return _default_impl _Args;                                           \
}

#define D3D9_STUB_VOID(_Return, _Name, _Proto, _Args)                     \
  __declspec (dllexport) _Return STDMETHODCALLTYPE                        \
  _Name _Proto {                                                          \
    WaitForInit ();                                                       \
                                                                          \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;            \
    static passthrough_t _default_impl = nullptr;                         \
                                                                          \
    if (_default_impl == nullptr) {                                       \
      static const char* szName = #_Name;                                 \
      _default_impl = (passthrough_t)GetProcAddress (backend_dll, szName);\
                                                                          \
      if (_default_impl == nullptr) {                                     \
        dll_log.Log (                                                     \
          L"Unable to locate symbol  %s in d3d9.dll",                     \
          L#_Name);                                                       \
        return;                                                           \
      }                                                                   \
    }                                                                     \
                                                                          \
    dll_log.Log (L"[!] %s %s - "                                          \
             L"[Calling Thread: 0x%04x]",                                 \
      L#_Name, L#_Proto, GetCurrentThreadId ());                          \
                                                                          \
    _default_impl _Args;                                                  \
}

#define D3D9_STUB_INT(_Return, _Name, _Proto, _Args)                      \
  __declspec (dllexport) _Return STDMETHODCALLTYPE                        \
  _Name _Proto {                                                          \
    WaitForInit ();                                                       \
                                                                          \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;            \
    static passthrough_t _default_impl = nullptr;                         \
                                                                          \
    if (_default_impl == nullptr) {                                       \
      static const char* szName = #_Name;                                 \
      _default_impl = (passthrough_t)GetProcAddress (backend_dll, szName);\
                                                                          \
      if (_default_impl == nullptr) {                                     \
        dll_log.Log (                                                     \
          L"Unable to locate symbol  %s in d3d9.dll",                     \
          L#_Name);                                                       \
        return 0;                                                         \
      }                                                                   \
    }                                                                     \
                                                                          \
    dll_log.Log (L"[!] %s %s - "                                          \
             L"[Calling Thread: 0x%04x]",                                 \
      L#_Name, L#_Proto, GetCurrentThreadId ());                          \
                                                                          \
    return _default_impl _Args;                                           \
}

extern "C" {
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
  dll_log.Log (L" Original VFTable entry for %s: %08Xh  (Memory Policy: %s)",\
             L##_Name, vftable [_Index],                                      \
             BMF_DescribeVirtualProtectFlags (dwProtect));                    \
                                                                              \
    if (_Original == NULL)                                                    \
      _Original = (##_Type)vftable [_Index];                                  \
                                                                              \
    dll_log.Log (L"  + %s: %08Xh", L#_Original, _Original);                  \
                                                                              \
    vftable [_Index] = _Override;                                             \
                                                                              \
    VirtualProtect (&vftable [_Index], 8, dwProtect, &dwProtect);             \
                                                                              \
    dll_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n", \
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
  dll_log.Log (L" Original VFTable entry for %s: %08Xh  (Memory Policy: %s)",\
             L##_Name, vftable [_Index],                                      \
             BMF_DescribeVirtualProtectFlags (dwProtect));                    \
                                                                              \
    if (_Original == NULL)                                                    \
      _Original = (##_Type)vftable [_Index];                                  \
                                                                              \
    dll_log.Log (L"  + %s: %08Xh", L#_Original, _Original);                  \
                                                                              \
    vftable [_Index] = _Override;                                             \
                                                                              \
    VirtualProtect (&vftable [_Index], 4, dwProtect, &dwProtect);             \
                                                                              \
    dll_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n", \
                  L##_Name, vftable [_Index],                                 \
                  BMF_DescribeVirtualProtectFlags (dwProtect));               \
  }                                                                           \
}
#endif

//#include <dxgi.h>

#define D3D9_CALL(_Ret, _Call) {                                     \
  dll_log.LogEx (true, L"  Calling original function: ");            \
  (_Ret) = (_Call);                                                  \
  dll_log.LogEx (false, L"(ret=%s)\n\n", BMF_DescribeHRESULT (_Ret));\
}

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
  WaitForInit ();

  //std::wstring iname = BMF_GetDXGIAdapterInterface (This);

  //dll_log_CALL_I2 (iname.c_str (), L"GetDesc2", L"%08Xh, %08Xh", This, pDesc);

  dll_log.Log (L"[!] %s (%08Xh, %lu, %lu, %08Xh, %lu, %08Xh, %08Xh) - "
    L"[Calling Thread: 0x%04x]",
    L"IDirect3D9::D3D9CreateDevice", This, Adapter, DeviceType, hFocusWindow,
    BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface,
    GetCurrentThreadId ());

  HRESULT ret;

  D3D9_CALL (ret, D3D9CreateDevice_Original (This,
                                             Adapter,
                                             DeviceType,
                                             hFocusWindow,
                                             BehaviorFlags,
                                             pPresentationParameters,
                                             ppReturnedDeviceInterface));

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 17,
                         "IDirect3DDevice9::Present", D3D9PresentCallback,
                         D3D9Present_Original, D3D9PresentSwapChain_t);

  typedef HRESULT (__cdecl *CreateDXGIFactory_t)(REFIID,IDXGIFactory**);
  static HMODULE hDXGI = LoadLibrary (L"dxgi.dll");
  static CreateDXGIFactory_t CreateDXGIFactory = (CreateDXGIFactory_t)GetProcAddress (hDXGI, "CreateDXGIFactory");
  IDXGIFactory* factory;
  if (SUCCEEDED (CreateDXGIFactory (__uuidof (IDXGIFactory), &factory))) {
    IDXGIAdapter* adapter;
    factory->EnumAdapters (0, &adapter);
    BMF_StartDXGI_1_4_BudgetThread (&adapter);
  }

  return ret;
}
}

extern "C" __declspec(dllexport) IDirect3D9* __cdecl Direct3DCreate9(UINT SDKVersion)
{
  WaitForInit ();

  dll_log.Log (L"[!] %s (%lu) - "
                L"[Calling Thread: 0x%04x]",
                L"Direct3DCreate9", SDKVersion, GetCurrentThreadId ());

  IDirect3D9* dev = nullptr;

  if (Direct3DCreate9_Import)
    dev = Direct3DCreate9_Import (SDKVersion);

  if (dev != nullptr)
    D3D9_VIRTUAL_OVERRIDE (&dev, 16, "pDev->CreateDevice",
                           D3D9CreateDevice_Override, D3D9CreateDevice_Original,
                           D3D9CreateDevice_t);

  return dev;
}

//__declspec (dllexport)
extern "C" __declspec(dllexport) HRESULT __cdecl Direct3DCreate9Ex(__in   UINT SDKVersion, __out  IDirect3D9Ex **ppD3D)
{
  WaitForInit ();

  dll_log.Log (L"[!] %s (%lu, %08Xh) - "
    L"[Calling Thread: 0x%04x]",
    L"Direct3DCreate9Ex", SDKVersion, ppD3D, GetCurrentThreadId ());

  HRESULT hr;

  if (Direct3DCreate9Ex_Import)
    hr = Direct3DCreate9Ex_Import (SDKVersion, ppD3D);

  if (SUCCEEDED (hr))
    D3D9_VIRTUAL_OVERRIDE (ppD3D, 15, "(*d3d9ex)->CreateDevice",
      D3D9CreateDevice_Override, D3D9CreateDevice_Original,
      D3D9CreateDevice_t);

  return hr;
}