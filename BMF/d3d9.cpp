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

typedef IDirect3D9*
  (STDMETHODCALLTYPE *Direct3DCreate9PROC)(  UINT           SDKVersion);
typedef HRESULT
  (STDMETHODCALLTYPE *Direct3DCreate9ExPROC)(UINT           SDKVersion,
                                             IDirect3D9Ex** d3d9ex);

typedef HRESULT (STDMETHODCALLTYPE *D3D9PresentDevice_t)(
           IDirect3DDevice9    *This,
_In_ const RECT                *pSourceRect,
_In_ const RECT                *pDestRect,
_In_       HWND                 hDestWindowOverride,
_In_ const RGNDATA             *pDirtyRegion);

typedef HRESULT (STDMETHODCALLTYPE *D3D9PresentDeviceEx_t)(
           IDirect3DDevice9Ex  *This,
_In_ const RECT                *pSourceRect,
_In_ const RECT                *pDestRect,
_In_       HWND                 hDestWindowOverride,
_In_ const RGNDATA             *pDirtyRegion,
_In_       DWORD                dwFlags);

typedef HRESULT (STDMETHODCALLTYPE *D3D9PresentSwapChain_t)(
             IDirect3DSwapChain9 *This,
  _In_ const RECT                *pSourceRect,
  _In_ const RECT                *pDestRect,
  _In_       HWND                 hDestWindowOverride,
  _In_ const RGNDATA             *pDirtyRegion,
  _In_       DWORD                dwFlags);

typedef HRESULT (STDMETHODCALLTYPE *D3D9PresentSwapChainEx_t)(
  IDirect3DDevice9Ex  *This,
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

typedef HRESULT (STDMETHODCALLTYPE *D3D9CreateDeviceEx_t)(
           IDirect3D9Ex           *This,
           UINT                    Adapter,
           D3DDEVTYPE              DeviceType,
           HWND                    hFocusWindow,
           DWORD                   BehaviorFlags,
           D3DPRESENT_PARAMETERS  *pPresentationParameters,
           D3DDISPLAYMODEEX       *pFullscreenDisplayMode,
           IDirect3DDevice9Ex    **ppReturnedDeviceInterface);

typedef HRESULT (STDMETHODCALLTYPE *D3D9Reset_t)(
           IDirect3DDevice9      *This,
           D3DPRESENT_PARAMETERS *pPresentationParameters);

D3D9PresentDevice_t    D3D9Present_Original        = nullptr;
D3D9PresentDeviceEx_t  D3D9PresentEx_Original      = nullptr;
D3D9PresentSwapChain_t D3D9PresentSwap_Original    = nullptr;
D3D9CreateDevice_t     D3D9CreateDevice_Original   = nullptr;
D3D9CreateDeviceEx_t   D3D9CreateDeviceEx_Original = nullptr;
D3D9Reset_t            D3D9Reset_Original          = nullptr;

Direct3DCreate9PROC   Direct3DCreate9_Import   = nullptr;
Direct3DCreate9ExPROC Direct3DCreate9Ex_Import = nullptr;

void
d3d9_init_callback (void)
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

extern "C" const wchar_t* BMF_DescribeVirtualProtectFlags (DWORD dwProtect);

#define __PTR_SIZE   sizeof LPCVOID
#define __PAGE_PRIVS PAGE_EXECUTE_READWRITE

#define D3D9_VIRTUAL_OVERRIDE(_Base,_Index,_Name,_Override,_Original,_Type) { \
  void** vftable = *(void***)*_Base;                                          \
                                                                              \
  if (vftable [_Index] != _Override) {                                        \
    DWORD dwProtect;                                                          \
                                                                              \
    VirtualProtect (&vftable [_Index], __PTR_SIZE, __PAGE_PRIVS, &dwProtect); \
                                                                              \
    dll_log.Log (L" Old VFTable entry for %s: %08Xh  (Memory Policy: %s)",    \
                 L##_Name, vftable [_Index],                                  \
                 BMF_DescribeVirtualProtectFlags (dwProtect));                \
                                                                              \
    if (_Original == NULL)                                                    \
      _Original = (##_Type)vftable [_Index];                                  \
                                                                              \
    /*dll_log.Log (L"  + %s: %08Xh", L#_Original, _Original);*/               \
                                                                              \
    vftable [_Index] = _Override;                                             \
                                                                              \
    VirtualProtect (&vftable [_Index], __PTR_SIZE, dwProtect, &dwProtect);    \
                                                                              \
    dll_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n",  \
                  L##_Name, vftable [_Index],                                 \
                  BMF_DescribeVirtualProtectFlags (dwProtect));               \
  }                                                                           \
}

#define D3D9_CALL(_Ret, _Call) {                                     \
  dll_log.LogEx (true, L"  Calling original function: ");            \
  (_Ret) = (_Call);                                                  \
  dll_log.LogEx (false, L"(ret=%s)\n\n", BMF_DescribeHRESULT (_Ret));\
}

extern "C" {
COM_DECLSPEC_NOTHROW
HRESULT
__stdcall D3D9PresentCallback (IDirect3DDevice9 *This,
                    _In_ const RECT             *pSourceRect,
                    _In_ const RECT             *pDestRect,
                    _In_       HWND              hDestWindowOverride,
                    _In_ const RGNDATA          *pDirtyRegion)
{
  BMF_BeginBufferSwap ();

  return BMF_EndBufferSwap (D3D9Present_Original (This,
                                                  pSourceRect,
                                                  pDestRect,
                                                  hDestWindowOverride,
                                                  pDirtyRegion));
}

COM_DECLSPEC_NOTHROW
HRESULT
__stdcall D3D9PresentCallbackEx (IDirect3DDevice9Ex *This,
                      _In_ const RECT               *pSourceRect,
                      _In_ const RECT               *pDestRect,
                      _In_       HWND                hDestWindowOverride,
                      _In_ const RGNDATA            *pDirtyRegion,
                      _In_       DWORD               dwFlags)
{
  BMF_BeginBufferSwap ();

  return BMF_EndBufferSwap (D3D9PresentEx_Original (This,
                                                    pSourceRect,
                                                    pDestRect,
                                                    hDestWindowOverride,
                                                    pDirtyRegion,
                                                    dwFlags));
}
}


#define D3D9_STUB_HRESULT(_Return, _Name, _Proto, _Args)                  \
  COM_DECLSPEC_NOTHROW _Return STDMETHODCALLTYPE                          \
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
    /*dll_log.Log (L"[!] %s %s - "                                      */\
             /*L"[Calling Thread: 0x%04x]",                             */\
      /*L#_Name, L#_Proto, GetCurrentThreadId ());                      */\
                                                                          \
    return _default_impl _Args;                                           \
}

#define D3D9_STUB_VOIDP(_Return, _Name, _Proto, _Args)                    \
  COM_DECLSPEC_NOTHROW _Return STDMETHODCALLTYPE                          \
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
    /*dll_log.Log (L"[!] %s %s - "                                      */\
           /*L"[Calling Thread: 0x%04x]",                               */\
      /*L#_Name, L#_Proto, GetCurrentThreadId ());                      */\
                                                                          \
    return _default_impl _Args;                                           \
}

#define D3D9_STUB_VOID(_Return, _Name, _Proto, _Args)                     \
  COM_DECLSPEC_NOTHROW _Return STDMETHODCALLTYPE                          \
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
    /*dll_log.Log (L"[!] %s %s - "                                      */\
             /*L"[Calling Thread: 0x%04x]",                             */\
      /*L#_Name, L#_Proto, GetCurrentThreadId ());                      */\
                                                                          \
    _default_impl _Args;                                                  \
}

#define D3D9_STUB_INT(_Return, _Name, _Proto, _Args)                      \
  COM_DECLSPEC_NOTHROW _Return STDMETHODCALLTYPE                          \
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
    /*dll_log.Log (L"[!] %s %s - "                                      */\
             /*L"[Calling Thread: 0x%04x]",                             */\
      /*L#_Name, L#_Proto, GetCurrentThreadId ());                      */\
                                                                          \
    return _default_impl _Args;                                           \
}

extern "C" {
D3D9_STUB_VOIDP   (void*, Direct3DShaderValidatorCreate, (void), 
                                                         (    ))

D3D9_STUB_INT     (int,   D3DPERF_BeginEvent, (D3DCOLOR color, LPCWSTR name),
                                                       (color,         name))
//D3D9_STUB_INT     (int,   D3DPERF_EndEvent,   (void),          ( ))

D3D9_STUB_INT     (DWORD, D3DPERF_GetStatus,  (void),          ( ))
D3D9_STUB_VOID    (void,  D3DPERF_SetOptions, (DWORD options), (options))

D3D9_STUB_INT     (BOOL,  D3DPERF_QueryRepeatFrame, (void),    ( ))
D3D9_STUB_VOID    (void,  D3DPERF_SetMarker, (D3DCOLOR color, LPCWSTR name),
                                                      (color,         name))
D3D9_STUB_VOID    (void,  D3DPERF_SetRegion, (D3DCOLOR color, LPCWSTR name),
                                                      (color,         name))

int
__stdcall
D3DPERF_EndEvent (void)
{
  WaitForInit ();

  typedef int (STDMETHODCALLTYPE *passthrough_t) (void);

  static passthrough_t _default_impl = nullptr;
  
  if (_default_impl == nullptr) {
    static const char* szName = "D3DPERF_EndEvent";
    _default_impl = (passthrough_t)GetProcAddress (backend_dll, szName);

    if (_default_impl == nullptr) {
      dll_log.Log (
          L"Unable to locate symbol  %s in d3d9.dll",
          L"D3DPERF_EndEvent");
      return 0;
    }
  }

  BMF_EndBufferSwap (S_OK);

  return _default_impl ();
}
}

typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory_t)(REFIID,IDXGIFactory**);
typedef HRESULT (STDMETHODCALLTYPE *GetSwapChain_t)
  (IDirect3DDevice9* This, UINT iSwapChain, IDirect3DSwapChain9** pSwapChain);

typedef HRESULT (STDMETHODCALLTYPE *CreateAdditionalSwapChain_t)
  (IDirect3DDevice9* This, D3DPRESENT_PARAMETERS* pPresentationParameters,
   IDirect3DSwapChain9** pSwapChain);

CreateAdditionalSwapChain_t D3D9CreateAdditionalSwapChain_Original = nullptr;

extern "C" {
  COM_DECLSPEC_NOTHROW
  HRESULT
  __stdcall D3D9PresentSwapCallback (IDirect3DSwapChain9 *This,
                             _In_ const RECT             *pSourceRect,
                             _In_ const RECT             *pDestRect,
                             _In_       HWND              hDestWindowOverride,
                             _In_ const RGNDATA          *pDirtyRegion,
                             _In_       DWORD             dwFlags)
  {
    BMF_BeginBufferSwap ();

    return BMF_EndBufferSwap (D3D9PresentSwap_Original (This,
                                                        pSourceRect,
                                                        pDestRect,
                                                        hDestWindowOverride,
                                                        pDirtyRegion,
                                                        dwFlags));
  }
}

COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9CreateAdditionalSwapChain_Override (IDirect3DDevice9       *This,
                                        D3DPRESENT_PARAMETERS  *pPresentationParameters,
                                        IDirect3DSwapChain9   **pSwapChain)
{
  dll_log.Log (L"[!] %s (%08Xh, %08Xh, %08Xh) - "
    L"[Calling Thread: 0x%04x]",
    L"IDirect3DDevice9::CreateAdditionalSwapChain", This, pPresentationParameters,
    pSwapChain, GetCurrentThreadId ()
  );

  HRESULT hr;

  D3D9_CALL (hr, D3D9CreateAdditionalSwapChain_Original (This,
                                                         pPresentationParameters,
                                                         pSwapChain));

  if (SUCCEEDED (hr)) {
    D3D9_VIRTUAL_OVERRIDE (pSwapChain, 3,
                           "IDirect3DSwapChain9::Present", D3D9PresentSwapCallback,
                           D3D9PresentSwap_Original, D3D9PresentSwapChain_t);
  }

  return hr;
}

typedef HRESULT (STDMETHODCALLTYPE *EndScene_t)
  (IDirect3DDevice9* This);

EndScene_t D3D9EndScene_Original = nullptr;

COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9EndScene_Override (IDirect3DDevice9* This)
{
  //dll_log.Log (L"[!] %s (%08Xh) - "
    //L"[Calling Thread: 0x%04x]",
    //L"IDirect3DDevice9::EndScene", This,
    //GetCurrentThreadId ()
  //);

  HRESULT hr;

  hr = D3D9EndScene_Original (This);

  //D3D9_CALL (hr, D3D9EndScene_Original (This));

  if (SUCCEEDED (hr)) {
    BMF_EndBufferSwap (hr);
  }

  return hr;
}

extern "C" {
  COM_DECLSPEC_NOTHROW
  HRESULT
  __stdcall D3D9Reset (IDirect3DDevice9      *This,
                       D3DPRESENT_PARAMETERS *pPresentationParameters)
  {
    dll_log.Log (L"[!] %s (%08Xh, %08Xh) - "
      L"[Calling Thread: 0x%04x]",
      L"IDirect3DDevice9::Reset", This, pPresentationParameters,
                                  GetCurrentThreadId ()
    );

    HRESULT hr;

    D3D9_CALL (hr, D3D9Reset_Original (This,
                                       pPresentationParameters));

    if (SUCCEEDED (hr)) {
      D3D9_VIRTUAL_OVERRIDE (&This, 16,
                             "IDirect3DDevice9::Reset", D3D9Reset,
                             D3D9Reset_Original, D3D9Reset_t);

      D3D9_VIRTUAL_OVERRIDE (&This, 17,
                             "IDirect3DDevice9::Present", D3D9PresentCallback,
                             D3D9Present_Original, D3D9PresentDevice_t);

      D3D9_VIRTUAL_OVERRIDE (&This, 13,
                             "IDirect3DDevice9::CreateAdditionalSwapChain",
                             D3D9CreateAdditionalSwapChain_Override,
                             D3D9CreateAdditionalSwapChain_Original,
                             CreateAdditionalSwapChain_t);

      D3D9_VIRTUAL_OVERRIDE (&This, 42,
                             "IDirect3DDevice9::EndScene",
                             D3D9EndScene_Override,
                             D3D9EndScene_Original,
                             EndScene_t);

      BMF_EndBufferSwap (hr);
    }

    return hr;
  }
}

COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9CreateDeviceEx_Override (IDirect3D9Ex           *This,
                             UINT                    Adapter,
                             D3DDEVTYPE              DeviceType,
                             HWND                    hFocusWindow,
                             DWORD                   BehaviorFlags,
                             D3DPRESENT_PARAMETERS  *pPresentationParameters,
                             D3DDISPLAYMODEEX       *pFullscreenDisplayMode,
                             IDirect3DDevice9Ex    **ppReturnedDeviceInterface)
{
  dll_log.Log (L"[!] %s (%08Xh, %lu, %lu, %08Xh, %lu, %08Xh, %08Xh, %08Xh) - "
    L"[Calling Thread: 0x%04x]",
    L"IDirect3D9Ex::D3D9CreateDeviceEx", This, Adapter, DeviceType, hFocusWindow,
    BehaviorFlags, pPresentationParameters, pFullscreenDisplayMode,
    ppReturnedDeviceInterface,
    GetCurrentThreadId ());

  HRESULT ret;

  D3D9_CALL (ret, D3D9CreateDeviceEx_Original (This,
                                               Adapter,
                                               DeviceType,
                                               hFocusWindow,
                                               BehaviorFlags,
                                               pPresentationParameters,
                                               pFullscreenDisplayMode,
                                               ppReturnedDeviceInterface));

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 16,
                         "IDirect3DDevice9Ex::Reset", D3D9Reset,
                         D3D9Reset_Original, D3D9Reset_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 17,
                         "IDirect3DDevice9Ex::Present", D3D9PresentCallback,
                         D3D9Present_Original, D3D9PresentDevice_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 121,
                         "IDirect3DDevice9Ex::PresentEx", D3D9PresentCallbackEx,
                         D3D9PresentEx_Original, D3D9PresentDeviceEx_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 13,
                         "IDirect3DDevice9Ex::CreateAdditionalSwapChain",
                         D3D9CreateAdditionalSwapChain_Override,
                         D3D9CreateAdditionalSwapChain_Original,
                         CreateAdditionalSwapChain_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 42,
                         "IDirect3DDevice9Ex::EndScene",
                         D3D9EndScene_Override,
                         D3D9EndScene_Original,
                         EndScene_t);


  static HMODULE hDXGI = LoadLibrary (L"dxgi.dll");
  static CreateDXGIFactory_t CreateDXGIFactory =
    (CreateDXGIFactory_t)GetProcAddress (hDXGI, "CreateDXGIFactory");

  IDXGIFactory* factory;

  // Only spawn the DXGI 1.4 budget thread if ... DXGI 1.4 is implemented.
  if (SUCCEEDED (CreateDXGIFactory (__uuidof (IDXGIFactory4), &factory))) {
    IDXGIAdapter* adapter;
    factory->EnumAdapters (Adapter, &adapter);

    BMF_StartDXGI_1_4_BudgetThread (&adapter);

    adapter->Release ();
    factory->Release ();
  }

  void** vftable                    = *(void***)*ppReturnedDeviceInterface;
  GetSwapChain_t       GetSwapChain = (GetSwapChain_t)vftable [14];
  IDirect3DSwapChain9* SwapChain    = nullptr;

  for (int i = 0; i < 4; i++) {
    if (SUCCEEDED (
          GetSwapChain ( (IDirect3DDevice9 *)*ppReturnedDeviceInterface,
                           i,
                             &SwapChain
                       )
                   )
       ) {
      D3D9_VIRTUAL_OVERRIDE (&SwapChain, 3,
                             "IDirect3DSwapChain9::Present",
                             D3D9PresentSwapCallback,
                             D3D9PresentSwap_Original,
                             D3D9PresentSwapChain_t);

      ((IUnknown *)SwapChain)->Release ();
    }
  }

  return ret;
}

COM_DECLSPEC_NOTHROW
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

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 16,
                         "IDirect3DDevice9::Reset", D3D9Reset,
                         D3D9Reset_Original, D3D9Reset_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 17,
                         "IDirect3DDevice9::Present", D3D9PresentCallback,
                         D3D9Present_Original, D3D9PresentDevice_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 13,
                         "IDirect3DDevice9::CreateAdditionalSwapChain",
                         D3D9CreateAdditionalSwapChain_Override,
                         D3D9CreateAdditionalSwapChain_Original,
                         CreateAdditionalSwapChain_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 42,
                         "IDirect3DDevice9::EndScene",
                         D3D9EndScene_Override,
                         D3D9EndScene_Original,
                         EndScene_t);


  static HMODULE hDXGI = LoadLibrary (L"dxgi.dll");
  static CreateDXGIFactory_t CreateDXGIFactory =
    (CreateDXGIFactory_t)GetProcAddress (hDXGI, "CreateDXGIFactory");

  IDXGIFactory* factory;

  // Only spawn the DXGI 1.4 budget thread if ... DXGI 1.4 is implemented.
  if (SUCCEEDED (CreateDXGIFactory (__uuidof (IDXGIFactory4), &factory))) {
    IDXGIAdapter* adapter;
    factory->EnumAdapters (Adapter, &adapter);

    BMF_StartDXGI_1_4_BudgetThread (&adapter);

    adapter->Release ();
    factory->Release ();
  }

  void** vftable                    = *(void***)*ppReturnedDeviceInterface;
  GetSwapChain_t       GetSwapChain = (GetSwapChain_t)vftable [14];
  IDirect3DSwapChain9* SwapChain    = nullptr;

  for (int i = 0; i < 4; i++) {
    if (SUCCEEDED (GetSwapChain (*ppReturnedDeviceInterface, i, &SwapChain))) {
      D3D9_VIRTUAL_OVERRIDE (&SwapChain, 3,
                             "IDirect3DSwapChain9::Present", D3D9PresentSwapCallback,
                             D3D9PresentSwap_Original,       D3D9PresentSwapChain_t);

      ((IUnknown *)SwapChain)->Release ();
    }
  }

  return ret;
}

IDirect3D9*
STDMETHODCALLTYPE
Direct3DCreate9 (UINT SDKVersion)
{
  WaitForInit ();

  dll_log.Log (L"[!] %s (%lu) - "
                L"[Calling Thread: 0x%04x]",
                L"Direct3DCreate9", SDKVersion, GetCurrentThreadId ());

  IDirect3D9* d3d9 = nullptr;

  if (Direct3DCreate9_Import)
    d3d9 = Direct3DCreate9_Import (SDKVersion);

  if (d3d9 != nullptr)
    D3D9_VIRTUAL_OVERRIDE (&d3d9, 16, "d3d9->CreateDevice",
                           D3D9CreateDevice_Override, D3D9CreateDevice_Original,
                           D3D9CreateDevice_t);

  return d3d9;
}

HRESULT
STDMETHODCALLTYPE
Direct3DCreate9Ex (__in UINT SDKVersion, __out IDirect3D9Ex **ppD3D)
{
  WaitForInit ();

  dll_log.Log (L"[!] %s (%lu, %08Xh) - "
    L"[Calling Thread: 0x%04x]",
    L"Direct3DCreate9Ex", SDKVersion, ppD3D, GetCurrentThreadId ());

  HRESULT hr = E_FAIL;

  if (Direct3DCreate9Ex_Import)
    D3D9_CALL (hr, Direct3DCreate9Ex_Import (SDKVersion, ppD3D));

  if (SUCCEEDED (hr)) {
    D3D9_VIRTUAL_OVERRIDE (ppD3D, 16, "(*d3d9ex)->CreateDevice",
      D3D9CreateDevice_Override, D3D9CreateDevice_Original,
      D3D9CreateDevice_t);

    D3D9_VIRTUAL_OVERRIDE (ppD3D, 20, "(*d3d9ex)->CreateDeviceEx",
      D3D9CreateDeviceEx_Override, D3D9CreateDeviceEx_Original,
      D3D9CreateDeviceEx_t);
  }

  return hr;
}