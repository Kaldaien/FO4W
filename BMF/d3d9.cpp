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

#define D3DPRESENTFLAG_LOCKABLE_BACKBUFFER      0x00000001
#define D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL     0x00000002
#define D3DPRESENTFLAG_DEVICECLIP               0x00000004
#define D3DPRESENTFLAG_VIDEO                    0x00000010

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
WINAPI
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

IDirect3DSwapChain9*  g_pSwapChain9    = nullptr;

extern "C" {
__declspec (noinline)
__declspec (dllexport)
D3DPRESENT_PARAMETERS*
__stdcall
BMF_SetPresentParamsD3D9 (IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pparams)
{
  // What a mess, but this helps with TZFIX
  Sleep (0);

  return pparams;
}
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

  typedef HRESULT (STDMETHODCALLTYPE *GetSwapChain_t)
              ( IDirect3DDevice9*     This,
                UINT                  iSwapChain,
                IDirect3DSwapChain9** ppSwapChain );
  void** vtable = *(void ***)This;
  GetSwapChain_t GetSwapChain = (GetSwapChain_t)vtable [14];

  GetSwapChain (This, 0, &g_pSwapChain9);
  ((IUnknown *)g_pSwapChain9)->Release ();

  typedef HRESULT (STDMETHODCALLTYPE *Present_t)
    ( IDirect3DSwapChain9*      This,
      UINT                      iSwapChain,
      CONST RECT*               pSourceRect,
      CONST RECT*               pDestRect,
      HWND                      hDestWindowOverride, 
      CONST RGNDATA*            pDirtyRegion,
      DWORD                     dwFlags );
  vtable = *(void ***)g_pSwapChain9;
  Present_t Present = (Present_t)vtable [3];

#ifndef SPOOF_STEAM_FPS
  HRESULT hr = D3D9Present_Original (This,
                                     pSourceRect,
                                     pDestRect,
                                     hDestWindowOverride,
                                     pDirtyRegion);
#else
  HRESULT hr = Present (g_pSwapChain9, 0, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, 2);
#endif

  if (! config.osd.pump)
    return BMF_EndBufferSwap ( hr,
                               (IUnknown *)This );

  return hr;
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

  typedef HRESULT (STDMETHODCALLTYPE *GetSwapChain_t)
              ( IDirect3DDevice9Ex*   This,
                UINT                  iSwapChain,
                IDirect3DSwapChain9** ppSwapChain );
  void** vtable = *(void ***)This;
  GetSwapChain_t GetSwapChain = (GetSwapChain_t)vtable [14];

  GetSwapChain (This, 0, &g_pSwapChain9);

  ((IUnknown *)g_pSwapChain9)->Release ();

  HRESULT hr = D3D9PresentEx_Original (This,
                                       pSourceRect,
                                       pDestRect,
                                       hDestWindowOverride,
                                       pDirtyRegion,
                                       dwFlags);

  if (! config.osd.pump)
    return BMF_EndBufferSwap ( hr,
                               (IUnknown *)This );

  return hr;
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

#ifdef PERF_ENDEVENT_ENDS_FRAME
  //
  // TODO: For the SLI meter, how the heck do we get a device from this?
  //
  //  if (! config.osd.pump)
  //    BMF_EndBufferSwap (S_OK);
#endif

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

#define D3DPRESENT_DONOTWAIT                   0x00000001L

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

    g_pSwapChain9 = This;

    //if (config.system.target_fps != 0)
      //dwFlags &= ~D3DPRESENT_DONOTWAIT;

    HRESULT hr = D3D9PresentSwap_Original (This,
                                           pSourceRect,
                                           pDestRect,
                                           hDestWindowOverride,
                                           pDirtyRegion,
                                           dwFlags);

    // We are manually pumping OSD updates, do not do them on buffer swaps.
    if (config.osd.pump) {
      return hr;
    }

    //
    // Get the Device Pointer so we can check the SLI state through NvAPI
    //
    IDirect3DDevice9* dev;
    typedef HRESULT (__stdcall *GetDevice_t)
                (IDirect3DSwapChain9*,IDirect3DDevice9**);

    // Silly to do it this way, but... we can't include d3d9.h
    void** vtable = *(void ***)This;
    GetDevice_t GetDevice = (GetDevice_t)vtable [8];

    if (SUCCEEDED (GetDevice (This, &dev))) {
      HRESULT ret = BMF_EndBufferSwap ( hr,
                                        (IUnknown *)dev );
      ((IUnknown *)dev)->Release ();
      return ret;
    }

    return BMF_EndBufferSwap (hr);
  }
}

COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9CreateAdditionalSwapChain_Override (
    IDirect3DDevice9       *This,
    D3DPRESENT_PARAMETERS  *pPresentationParameters,
    IDirect3DSwapChain9   **pSwapChain
  )
{
  dll_log.Log (L"[!] %s (%08Xh, %08Xh, %08Xh) - "
    L"[Calling Thread: 0x%04x]",
    L"IDirect3DDevice9::CreateAdditionalSwapChain", This,
    pPresentationParameters, pSwapChain, GetCurrentThreadId ()
  );

  HRESULT hr;

  D3D9_CALL (hr,D3D9CreateAdditionalSwapChain_Original(This,
                                                       pPresentationParameters,
                                                       pSwapChain));

  if (SUCCEEDED (hr)) {
    D3D9_VIRTUAL_OVERRIDE ( pSwapChain, 3,
                            "IDirect3DSwapChain9::Present",
                            D3D9PresentSwapCallback, D3D9PresentSwap_Original,
                            D3D9PresentSwapChain_t );
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

#ifdef ENDSCENE_ENDS_FRAME
  if (SUCCEEDED (hr) && (! config.osd.pump)) {
    BMF_EndBufferSwap (hr);
  }
#endif

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

    int TargetFPS = config.render.framerate.target_fps;
    int Refresh   = pPresentationParameters != nullptr ? 
                      pPresentationParameters->FullScreen_RefreshRateInHz :
                      0;

    if (TargetFPS != 0 && Refresh != 0) {
      if (Refresh >= TargetFPS) {
        if (! (Refresh % TargetFPS)) {
          dll_log.Log ( L"  >> Targeting %li FPS - using 1:%li VSYNC;"
                        L" (refresh = %li Hz)\n",
                          TargetFPS,
                            Refresh / TargetFPS,
                              Refresh );

          pPresentationParameters->SwapEffect           = D3DSWAPEFFECT_DISCARD;
          pPresentationParameters->PresentationInterval = Refresh / TargetFPS;
          pPresentationParameters->BackBufferCount      = 1; // No Triple Buffering Please!

        } else {
          dll_log.Log ( L"  >> Cannot target %li FPS - no such factor exists;"
                        L" (refresh = %li Hz)\n",
                          TargetFPS,
                            Refresh );
        }
      } else {
        dll_log.Log ( L"  >> Cannot target %li FPS - higher than refresh rate;"
                      L" (refresh = %li Hz)\n",
                        TargetFPS,
                          Refresh );
      }
    }

    pPresentationParameters->Flags |= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

    BMF_SetPresentParamsD3D9 (This, pPresentationParameters);

    HRESULT hr;

    D3D9_CALL (hr, D3D9Reset_Original (This,
                                       pPresentationParameters));

    if (SUCCEEDED (hr)) {
#if 0
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
#endif

#ifdef RESET_ENDS_FRAME
      if (! config.osd.pump)
        BMF_EndBufferSwap (hr);
#endif
    }

    return hr;
  }
}


typedef enum _D3DSAMPLERSTATETYPE
{
  D3DSAMP_ADDRESSU       = 1,  /* D3DTEXTUREADDRESS for U coordinate */
  D3DSAMP_ADDRESSV       = 2,  /* D3DTEXTUREADDRESS for V coordinate */
  D3DSAMP_ADDRESSW       = 3,  /* D3DTEXTUREADDRESS for W coordinate */
  D3DSAMP_BORDERCOLOR    = 4,  /* D3DCOLOR */
  D3DSAMP_MAGFILTER      = 5,  /* D3DTEXTUREFILTER filter to use for magnification */
  D3DSAMP_MINFILTER      = 6,  /* D3DTEXTUREFILTER filter to use for minification */
  D3DSAMP_MIPFILTER      = 7,  /* D3DTEXTUREFILTER filter to use between mipmaps during minification */
  D3DSAMP_MIPMAPLODBIAS  = 8,  /* float Mipmap LOD bias */
  D3DSAMP_MAXMIPLEVEL    = 9,  /* DWORD 0..(n-1) LOD index of largest map to use (0 == largest) */
  D3DSAMP_MAXANISOTROPY  = 10, /* DWORD maximum anisotropy */
  D3DSAMP_SRGBTEXTURE    = 11, /* Default = 0 (which means Gamma 1.0,
                               no correction required.) else correct for
                               Gamma = 2.2 */
  D3DSAMP_ELEMENTINDEX   = 12, /* When multi-element texture is assigned to sampler, this
                               indicates which element index to use.  Default = 0.  */
  D3DSAMP_DMAPOFFSET     = 13, /* Offset in vertices in the pre-sampled displacement map.
                               Only valid for D3DDMAPSAMPLER sampler  */
  D3DSAMP_FORCE_DWORD   = 0x7fffffff, /* force 32-bit size enum */
} D3DSAMPLERSTATETYPE;

typedef HRESULT (STDMETHODCALLTYPE *SetSamplerState_t)
  (IDirect3DDevice9*   This,
   DWORD               Sampler,
   D3DSAMPLERSTATETYPE Type,
   DWORD               Value);

SetSamplerState_t D3D9SetSamplerState_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9SetSamplerState_Override (IDirect3DDevice9*   This,
                              DWORD               Sampler,
                              D3DSAMPLERSTATETYPE Type,
                              DWORD               Value)
{
  return D3D9SetSamplerState_Original (This, Sampler, Type, Value);
}


struct D3DVIEWPORT9;

typedef HRESULT (STDMETHODCALLTYPE *SetViewport_t)
  (      IDirect3DDevice9* This,
   CONST D3DVIEWPORT9*     pViewport);

SetViewport_t D3D9SetViewport_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9SetViewport_Override (IDirect3DDevice9* This,
                    CONST D3DVIEWPORT9*     pViewport)
{
  return D3D9SetViewport_Original (This, pViewport);
}


typedef HRESULT (STDMETHODCALLTYPE *SetVertexShaderConstantF_t)
  (IDirect3DDevice9* This,
    UINT             StartRegister,
    CONST float*     pConstantData,
    UINT             Vector4fCount);

SetVertexShaderConstantF_t D3D9SetVertexShaderConstantF_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9SetVertexShaderConstantF_Override (IDirect3DDevice9* This,
                                       UINT             StartRegister,
                                       CONST float*     pConstantData,
                                       UINT             Vector4fCount)
{
  return D3D9SetVertexShaderConstantF_Original (This, StartRegister, pConstantData, Vector4fCount);
}


typedef HRESULT (STDMETHODCALLTYPE *SetPixelShaderConstantF_t)
  (IDirect3DDevice9* This,
    UINT             StartRegister,
    CONST float*     pConstantData,
    UINT             Vector4fCount);

SetPixelShaderConstantF_t D3D9SetPixelShaderConstantF_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9SetPixelShaderConstantF_Override (IDirect3DDevice9* This,
                                      UINT             StartRegister,
                                      CONST float*     pConstantData,
                                      UINT             Vector4fCount)
{
  return D3D9SetPixelShaderConstantF_Original (This, StartRegister, pConstantData, Vector4fCount);
}



struct IDirect3DPixelShader9;

typedef HRESULT (STDMETHODCALLTYPE *SetPixelShader_t)
  (IDirect3DDevice9*      This,
   IDirect3DPixelShader9* pShader);

SetPixelShader_t D3D9SetPixelShader_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9SetPixelShader_Override (IDirect3DDevice9* This,
                        IDirect3DPixelShader9* pShader)
{
  return D3D9SetPixelShader_Original (This, pShader);
}


struct IDirect3DVertexShader9;

typedef HRESULT (STDMETHODCALLTYPE *SetVertexShader_t)
  (IDirect3DDevice9*       This,
   IDirect3DVertexShader9* pShader);

SetVertexShader_t D3D9SetVertexShader_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9SetVertexShader_Override (IDirect3DDevice9* This,
                        IDirect3DVertexShader9* pShader)
{
  return D3D9SetVertexShader_Original (This, pShader);
}


typedef HRESULT (STDMETHODCALLTYPE *SetScissorRect_t)
  (IDirect3DDevice9* This,
   CONST RECT*       pRect);

SetScissorRect_t D3D9SetScissorRect_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9SetScissorRect_Override (IDirect3DDevice9* This,
                                   const RECT* pRect)
{
  return D3D9SetScissorRect_Original (This, pRect);
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
    L"IDirect3D9Ex::CreateDeviceEx", This, Adapter, (DWORD)DeviceType,
    hFocusWindow, BehaviorFlags, pPresentationParameters,
    pFullscreenDisplayMode, ppReturnedDeviceInterface, GetCurrentThreadId ());

  int TargetFPS = config.render.framerate.target_fps;
  int Refresh   = pFullscreenDisplayMode != nullptr ? 
                    pFullscreenDisplayMode->RefreshRate :
                    0;

  if (Refresh == 0) {
    Refresh = pPresentationParameters != nullptr ? 
                pPresentationParameters->FullScreen_RefreshRateInHz :
                0;
  }

  if (TargetFPS != 0 && Refresh != 0) {
    if (Refresh >= TargetFPS) {
      if (! (Refresh % TargetFPS)) {
        dll_log.Log ( L"  >> Targeting %li FPS - using 1:%lu VSYNC;"
                      L" (refresh = %li Hz)\n",
                        TargetFPS,
                          Refresh / TargetFPS,
                            Refresh );

        pPresentationParameters->SwapEffect           = D3DSWAPEFFECT_DISCARD;
        pPresentationParameters->PresentationInterval = Refresh / TargetFPS;
        pPresentationParameters->BackBufferCount      = 1; // No Triple Buffering Please!
      } else {
        dll_log.Log ( L"  >> Cannot target %li FPS - no such factor exists;"
                      L" (refresh = %li Hz)\n",
                        TargetFPS,
                          Refresh );
      }
    } else {
      dll_log.Log ( L"  >> Cannot target %li FPS - higher than refresh rate;"
                    L" (refresh = %li Hz)\n",
                      TargetFPS,
                        Refresh );
    }
  }

  pPresentationParameters->Flags |= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

  HRESULT ret;

  D3D9_CALL (ret, D3D9CreateDeviceEx_Original (This,
                                               Adapter,
                                               DeviceType,
                                               hFocusWindow,
                                               BehaviorFlags,
                                               pPresentationParameters,
                                               pFullscreenDisplayMode,
                                               ppReturnedDeviceInterface));

  if (! SUCCEEDED (ret))
    return ret;

  BMF_SetPresentParamsD3D9 ( (IDirect3DDevice9 *)*ppReturnedDeviceInterface,
                               pPresentationParameters );

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 16,
                         "IDirect3DDevice9Ex::Reset", D3D9Reset,
                         D3D9Reset_Original, D3D9Reset_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 17,
                         "IDirect3DDevice9Ex::Present", D3D9PresentCallback,
                         D3D9Present_Original, D3D9PresentDevice_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 121,
                         "IDirect3DDevice9Ex::PresentEx",D3D9PresentCallbackEx,
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

  void** vtable                     = *(void***)*ppReturnedDeviceInterface;
  GetSwapChain_t       GetSwapChain = (GetSwapChain_t)vtable [14];
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

typedef HRESULT (STDMETHODCALLTYPE *DXGIMakeWindowAssociation_t)
  (IDXGIFactory *This,
   HWND          WindowHandle,
   UINT          Flags);
DXGIMakeWindowAssociation_t DXGIMakeWindowAssociation_Original = nullptr;

COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
DXGIMakeWindowAssociation_Override (IDXGIFactory *This,
                                    HWND          WindowHandle,
                                    UINT          Flags)
{
  dll_log.Log (L" [!] IDXGIFactory::MakeWindowAssociation (%d, 0x%04X)",
    WindowHandle, Flags);
  return DXGIMakeWindowAssociation_Original (This, WindowHandle, Flags);
}


/* Pool types */
typedef enum _D3DPOOL {
  D3DPOOL_DEFAULT                 = 0,
  D3DPOOL_MANAGED                 = 1,
  D3DPOOL_SYSTEMMEM               = 2,
  D3DPOOL_SCRATCH                 = 3,

  D3DPOOL_FORCE_DWORD             = 0x7fffffff
} D3DPOOL;

struct IDirect3DTexture9;

typedef HRESULT (STDMETHODCALLTYPE *CreateTexture_t)
  (IDirect3DDevice9   *This,
   UINT                Width,
   UINT                Height,
   UINT                Levels,
   DWORD               Usage,
   D3DFORMAT           Format,
   D3DPOOL             Pool,
   IDirect3DTexture9 **ppTexture,
   HANDLE             *pSharedHandle);

CreateTexture_t D3D9CreateTexture_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9CreateTexture_Override (IDirect3DDevice9   *This,
                            UINT                Width,
                            UINT                Height,
                            UINT                Levels,
                            DWORD               Usage,
                            D3DFORMAT           Format,
                            D3DPOOL             Pool,
                            IDirect3DTexture9 **ppTexture,
                            HANDLE             *pSharedHandle)
{
  return D3D9CreateTexture_Original (This, Width, Height, Levels, Usage,
                                     Format, Pool, ppTexture, pSharedHandle);
}

struct IDirect3DSurface9;

typedef HRESULT (STDMETHODCALLTYPE *CreateRenderTarget_t)
  (IDirect3DDevice9     *This,
   UINT                  Width,
   UINT                  Height,
   D3DFORMAT             Format,
   D3DMULTISAMPLE_TYPE   MultiSample,
   DWORD                 MultisampleQuality,
   BOOL                  Lockable,
   IDirect3DSurface9   **ppSurface,
   HANDLE               *pSharedHandle);

CreateRenderTarget_t D3D9CreateRenderTarget_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9CreateRenderTarget_Override (IDirect3DDevice9     *This,
                                 UINT                  Width,
                                 UINT                  Height,
                                 D3DFORMAT             Format,
                                 D3DMULTISAMPLE_TYPE   MultiSample,
                                 DWORD                 MultisampleQuality,
                                 BOOL                  Lockable,
                                 IDirect3DSurface9   **ppSurface,
                                 HANDLE               *pSharedHandle)
{
  return D3D9CreateRenderTarget_Original (This, Width, Height, Format,
                                          MultiSample, MultisampleQuality,
                                          Lockable, ppSurface, pSharedHandle);
}

typedef HRESULT (STDMETHODCALLTYPE *CreateDepthStencilSurface_t)
  (IDirect3DDevice9     *This,
   UINT                  Width,
   UINT                  Height,
   D3DFORMAT             Format,
   D3DMULTISAMPLE_TYPE   MultiSample,
   DWORD                 MultisampleQuality,
   BOOL                  Discard,
   IDirect3DSurface9   **ppSurface,
   HANDLE               *pSharedHandle);

CreateDepthStencilSurface_t D3D9CreateDepthStencilSurface_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9CreateDepthStencilSurface_Override (IDirect3DDevice9     *This,
                                        UINT                  Width,
                                        UINT                  Height,
                                        D3DFORMAT             Format,
                                        D3DMULTISAMPLE_TYPE   MultiSample,
                                        DWORD                 MultisampleQuality,
                                        BOOL                  Discard,
                                        IDirect3DSurface9   **ppSurface,
                                        HANDLE               *pSharedHandle)
{
  return D3D9CreateDepthStencilSurface_Original (This, Width, Height, Format,
                                                 MultiSample, MultisampleQuality,
                                                 Discard, ppSurface, pSharedHandle);
}

typedef HRESULT (STDMETHODCALLTYPE *SetRenderTarget_t)
  (IDirect3DDevice9  *This,
   DWORD              RenderTargetIndex,
   IDirect3DSurface9 *pRenderTarget);

SetRenderTarget_t D3D9SetRenderTarget_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9SetRenderTarget_Override (IDirect3DDevice9  *This,
                              DWORD              RenderTargetIndex,
                              IDirect3DSurface9 *pRenderTarget)
{
  return D3D9SetRenderTarget_Original (This, RenderTargetIndex, pRenderTarget);
}

typedef HRESULT (STDMETHODCALLTYPE *SetDepthStencilSurface_t)
  (IDirect3DDevice9  *This,
   IDirect3DSurface9 *pNewZStencil);

SetDepthStencilSurface_t D3D9SetDepthStencilSurface_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9SetDepthStencilSurface_Override (IDirect3DDevice9  *This,
                                     IDirect3DSurface9 *pNewZStencil)
{
  return D3D9SetDepthStencilSurface_Original (This, pNewZStencil);
}

struct IDirect3DBaseTexture9;

typedef HRESULT (STDMETHODCALLTYPE *UpdateTexture_t)
  (IDirect3DDevice9      *This,
   IDirect3DBaseTexture9 *pSourceTexture,
   IDirect3DBaseTexture9 *pDestinationTexture);

UpdateTexture_t D3D9UpdateTexture_Original = nullptr;

__declspec (dllexport)
COM_DECLSPEC_NOTHROW
HRESULT
STDMETHODCALLTYPE
D3D9UpdateTexture_Override (IDirect3DDevice9      *This,
                            IDirect3DBaseTexture9 *pSourceTexture,
                            IDirect3DBaseTexture9 *pDestinationTexture)
{
  return D3D9UpdateTexture_Original ( This,
                                        pSourceTexture,
                                          pDestinationTexture );
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
  dll_log.Log (L"[!] %s (%08Xh, %lu, %lu, %08Xh, 0x%08X, %08Xh, %08Xh) - "
    L"[Calling Thread: 0x%04x]",
    L"IDirect3D9::CreateDevice", This, Adapter, (DWORD)DeviceType,
    hFocusWindow, BehaviorFlags, pPresentationParameters,
    ppReturnedDeviceInterface, GetCurrentThreadId ());

  int TargetFPS = config.render.framerate.target_fps;
  int Refresh   = pPresentationParameters != nullptr ? 
                    pPresentationParameters->FullScreen_RefreshRateInHz :
                    0;

  if (TargetFPS != 0 && Refresh != 0) {
    if (Refresh >= TargetFPS) {
      if (! (Refresh % TargetFPS)) {
        dll_log.Log ( L"  >> Targeting %li FPS - using 1:%li VSYNC;"
                      L" (refresh = %li Hz)\n",
                        TargetFPS,
                          Refresh / TargetFPS,
                            Refresh );

        pPresentationParameters->SwapEffect           = D3DSWAPEFFECT_DISCARD;
        pPresentationParameters->PresentationInterval = Refresh / TargetFPS;
        pPresentationParameters->BackBufferCount      = 1; // No Triple Buffering Please!

      } else {
        dll_log.Log ( L"  >> Cannot target %li FPS - no such factor exists;"
                      L" (refresh = %li Hz)\n",
                        TargetFPS,
                          Refresh );
      }
    } else {
      dll_log.Log ( L"  >> Cannot target %li FPS - higher than refresh rate;"
                    L" (refresh = %li Hz)\n",
                      TargetFPS,
                        Refresh );
    }
  }

  pPresentationParameters->Flags |= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

  HRESULT ret;

  //vtbl (This)
  //-----------
  // 3   TestCooperativeLevel
  // 4   GetAvailableTextureMem
  // 5   EvictManagedResources
  // 6   GetDirect3D
  // 7   GetDeviceCaps
  // 8   GetDisplayMode
  // 9   GetCreationParameters
  // 10  SetCursorProperties
  // 11  SetCursorPosition
  // 12  ShowCursor
  // 13  CreateAdditionalSwapChain
  // 14  GetSwapChain
  // 15  GetNumberOfSwapChains
  // 16  Reset
  // 17  Present
  // 18  GetBackBuffer
  // 19  GetRasterStatus
  // 20  SetDialogBoxMode
  // 21  SetGammaRamp
  // 22  GetGammaRamp
  // 23  CreateTexture
  // 24  CreateVolumeTexture
  // 25  CreateCubeTexture
  // 26  CreateVertexBuffer
  // 27  CreateIndexBuffer
  // 28  CreateRenderTarget
  // 29  CreateDepthStencilSurface
  // 30  UpdateSurface
  // 31  UpdateTexture
  // 32  GetRenderTargetData
  // 33  GetFrontBufferData
  // 34  StretchRect
  // 35  ColorFill
  // 36  CreateOffscreenPlainSurface
  // 37  SetRenderTarget
  // 38  GetRenderTarget
  // 39  SetDepthStencilSurface
  // 40  GetDepthStencilSurface
  // 41  BeginScene
  // 42  EndScene
  // 43  Clear
  // 44  SetTransform
  // 45  GetTransform
  // 46  MultiplyTransform
  // 47  SetViewport
  // 48  GetViewport
  // 49  SetMaterial
  // 50  GetMaterial
  // 51  SetLight
  // 52  GetLight
  // 53  LightEnable
  // 54  GetLightEnable
  // 55  SetClipPlane
  // 56  GetClipPlane
  // 57  SetRenderState
  // 58  GetRenderState
  // 59  CreateStateBlock
  // 60  BeginStateBlock
  // 61  EndStateBlock
  // 62  SetClipStatus
  // 63  GetClipStatus
  // 64  GetTexture
  // 65  SetTexture
  // 66  GetTextureStageState
  // 67  SetTextureStageState
  // 68  GetSamplerState
  // 69  SetSamplerState
  // 70  ValidateDevice
  // 71  SetPaletteEntries
  // 72  GetPaletteEntries
  // 73  SetCurrentTexturePalette
  // 74  GetCurrentTexturePalette
  // 75  SetScissorRect
  // 76  GetScissorRect
  // 77  SetSoftwareVertexProcessing
  // 78  GetSoftwareVertexProcessing
  // 79  SetNPatchMode
  // 80  GetNPatchMode
  // 81  DrawPrimitive
  // 82  DrawIndexedPrimitive
  // 83  DrawPrimitiveUP
  // 84  DrawIndexedPrimitiveUP
  // 85  ProcessVertices
  // 86  CreateVertexDeclaration
  // 87  SetVertexDeclaration
  // 88  GetVertexDeclaration
  // 89  SetFVF
  // 90  GetFVF
  // 91  CreateVertexShader
  // 92  SetVertexShader
  // 93  GetVertexShader
  // 94  SetVertexShaderConstantF
  // 95  GetVertexShaderConstantF
  // 96  SetVertexShaderConstantI
  // 97  GetVertexShaderConstantI
  // 98  SetVertexShaderConstantB
  // 99  GetVertexShaderConstantB
  // 100 SetStreamSource
  // 101 GetStreamSource
  // 102 SetStreamSourceFreq
  // 103 GetStreamSourceFreq
  // 104 SetIndices
  // 105 GetIndices
  // 106 CreatePixelShader
  // 107 SetPixelShader
  // 108 GetPixelShader
  // 109 SetPixelShaderConstantF
  // 110 GetPixelShaderConstantF
  // 111 SetPixelShaderConstantI
  // 112 GetPixelShaderConstantI
  // 113 SetPixelShaderConstantB
  // 114 GetPixelShaderConstantB
  // 115 DrawRectPatch
  // 116 DrawTriPatch
  // 117 DeletePatch
  // 118 CreateQuery

  D3D9_CALL (ret, D3D9CreateDevice_Original (This,
                                             Adapter,
                                             DeviceType,
                                             hFocusWindow,
                                             BehaviorFlags,
                                             pPresentationParameters,
                                             ppReturnedDeviceInterface));

  // Do not attempt to do vtable override stuff if this failed,
  //   that will cause an immediate crash! Instead log some information that
  //     might help diagnose the problem.
  if (! SUCCEEDED (ret)) {
    if (pPresentationParameters != nullptr) {
      dll_log.LogEx (true,
                L"  SwapChain Settings:   Res=(%lux%lu), Format=0x%04X, "
                                        L"Count=%lu - "
                                        L"SwapEffect: 0x%02X, Flags: 0x%04X,"
                                        L"AutoDepthStencil: %s "
                                        L"PresentationInterval: %lu\n",
                   pPresentationParameters->BackBufferWidth,
                   pPresentationParameters->BackBufferHeight,
                   pPresentationParameters->BackBufferFormat,
                   pPresentationParameters->BackBufferCount,
                   pPresentationParameters->SwapEffect,
                   pPresentationParameters->Flags,
                   pPresentationParameters->EnableAutoDepthStencil ? L"true" :
                                                                     L"false",
                   pPresentationParameters->PresentationInterval);

      if (! pPresentationParameters->Windowed) {
        dll_log.LogEx (true,
                L"  Fullscreen Settings:  Refresh Rate: %lu\n",
                   pPresentationParameters->FullScreen_RefreshRateInHz);
        dll_log.LogEx (true,
                L"  Multisample Settings: Type: %X, Quality: %lu\n",
                   pPresentationParameters->MultiSampleType,
                   pPresentationParameters->MultiSampleQuality);
      }
    }

    return ret;
  }

  BMF_SetPresentParamsD3D9 ( *ppReturnedDeviceInterface,
                               pPresentationParameters );

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

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 23,
                         "IDirect3DDevice9::CreateTexture",
                         D3D9CreateTexture_Override,
                         D3D9CreateTexture_Original,
                         CreateTexture_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 28,
                         "IDirect3DDevice9::CreateRenderTarget",
                         D3D9CreateRenderTarget_Override,
                         D3D9CreateRenderTarget_Original,
                         CreateRenderTarget_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 29,
                         "IDirect3DDevice9::CreateDepthStencilSurface",
                         D3D9CreateDepthStencilSurface_Override,
                         D3D9CreateDepthStencilSurface_Original,
                         CreateDepthStencilSurface_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 31,
                         "IDirect3DDevice9::UpdateTexture",
                         D3D9UpdateTexture_Override,
                         D3D9UpdateTexture_Original,
                         UpdateTexture_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 37,
                         "IDirect3DDevice9::SetRenderTarget",
                         D3D9SetRenderTarget_Override,
                         D3D9SetRenderTarget_Original,
                         SetRenderTarget_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 39,
                         "IDirect3DDevice9::SetDepthStencilSurface",
                         D3D9SetDepthStencilSurface_Override,
                         D3D9SetDepthStencilSurface_Original,
                         SetDepthStencilSurface_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 42,
                         "IDirect3DDevice9::EndScene",
                         D3D9EndScene_Override,
                         D3D9EndScene_Original,
                         EndScene_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 47,
                         "IDirect3DDevice9::SetViewport",
                         D3D9SetViewport_Override,
                         D3D9SetViewport_Original,
                         SetViewport_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 69,
                         "IDirect3DDevice9::SetSamplerState",
                          D3D9SetSamplerState_Override,
                          D3D9SetSamplerState_Original,
                          SetSamplerState_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 75,
                         "IDirect3DDevice9::SetScissorRect",
                          D3D9SetScissorRect_Override,
                          D3D9SetScissorRect_Original,
                          SetScissorRect_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 92,
                         "IDirect3DDevice9::SetVertexShader",
                          D3D9SetVertexShader_Override,
                          D3D9SetVertexShader_Original,
                          SetVertexShader_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 94,
                         "IDirect3DDevice9::SetSetVertexShaderConstantF",
                          D3D9SetVertexShaderConstantF_Override,
                          D3D9SetVertexShaderConstantF_Original,
                          SetVertexShaderConstantF_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 107,
                        "IDirect3DDevice9::SetPixelShader",
                         D3D9SetPixelShader_Override,
                         D3D9SetPixelShader_Original,
                         SetPixelShader_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 109,
                        "IDirect3DDevice9::SetPixelShaderConstantF",
                         D3D9SetPixelShaderConstantF_Override,
                         D3D9SetPixelShaderConstantF_Original,
                         SetPixelShaderConstantF_t);

  static HMODULE hDXGI = LoadLibrary (L"dxgi.dll");
  static CreateDXGIFactory_t CreateDXGIFactory =
    (CreateDXGIFactory_t)GetProcAddress (hDXGI, "CreateDXGIFactory");

  IDXGIFactory* factory;

#if 0
  if (SUCCEEDED (CreateDXGIFactory (__uuidof (IDXGIFactory), &factory))) {
    D3D9_VIRTUAL_OVERRIDE (&factory, 8,
                           "IDXGIFactory::MakeWindowAssociation",
                           DXGIMakeWindowAssociation_Override,
                           DXGIMakeWindowAssociation_Original,
                           DXGIMakeWindowAssociation_t);
    factory->MakeWindowAssociation (pPresentationParameters->hDeviceWindow, 0);
    factory->Release               ();
  }
#endif

  // Only spawn the DXGI 1.4 budget thread if ... DXGI 1.4 is implemented.
  if (SUCCEEDED (CreateDXGIFactory (__uuidof (IDXGIFactory4), &factory))) {
    IDXGIAdapter* adapter;
    factory->EnumAdapters (Adapter, &adapter);

    BMF_StartDXGI_1_4_BudgetThread (&adapter);

    adapter->Release ();
    factory->Release ();
  }

  void** vftbl                      = *(void***)*ppReturnedDeviceInterface;
  GetSwapChain_t       GetSwapChain = (GetSwapChain_t)vftbl [14];
  IDirect3DSwapChain9* SwapChain    = nullptr;

  for (int i = 0; i < 4; i++) {
    if (SUCCEEDED (GetSwapChain (*ppReturnedDeviceInterface, i, &SwapChain))) {
      D3D9_VIRTUAL_OVERRIDE (&SwapChain, 3,
                             "IDirect3DSwapChain9::Present",
                             D3D9PresentSwapCallback, D3D9PresentSwap_Original,
                             D3D9PresentSwapChain_t);

      ((IUnknown *)SwapChain)->Release ();
    }
  }

  return ret;
}

IDirect3D9* g_pD3D9 = nullptr;

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

#if 0
  // This helps with Tales of Zestiria, but should probably be made optional
  if (g_pD3D9 != nullptr) {
#endif
    if (d3d9 != nullptr)
      D3D9_VIRTUAL_OVERRIDE (&d3d9, 16, "d3d9->CreateDevice",
                             D3D9CreateDevice_Override,
                             D3D9CreateDevice_Original, D3D9CreateDevice_t);
#if 0
  }
#endif

  return (g_pD3D9 = d3d9);
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