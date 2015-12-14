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
    /*dll_log.Log (L" Old VFTable entry for %s: %08Xh  (Memory Policy: %s)",*/\
                 /*L##_Name, vftable [_Index],                              */\
                 /*BMF_DescribeVirtualProtectFlags (dwProtect));            */\
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
    /*dll_log.Log(L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n"*/\
                  /*,L##_Name, vftable [_Index],                            */\
                  /*BMF_DescribeVirtualProtectFlags (dwProtect));           */\
  }                                                                           \
}

#define D3D9_CALL(_Ret, _Call) {                                     \
  dll_log.LogEx (true, L"  Calling original function: ");            \
  (_Ret) = (_Call);                                                  \
  dll_log.LogEx (false, L"(ret=%s)\n\n", BMF_DescribeHRESULT (_Ret));\
}

COM_DECLSPEC_NOTHROW
__declspec (noinline)
D3DPRESENT_PARAMETERS*
WINAPI
BMF_SetPresentParamsD3D9 (IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pparams)
{
  // If we did not have this here, the function would be too short to hook  :-/
  pDevice->AddRef  ();
  pDevice->Release ();

  return pparams;
}

COM_DECLSPEC_NOTHROW
__declspec (noinline)
HRESULT
WINAPI D3D9PresentCallback (IDirect3DDevice9 *This,
                 _In_ const RECT             *pSourceRect,
                 _In_ const RECT             *pDestRect,
                 _In_       HWND              hDestWindowOverride,
                 _In_ const RGNDATA          *pDirtyRegion)
{
  BMF_BeginBufferSwap ();

  HRESULT hr = D3D9Present_Original (This,
                                     pSourceRect,
                                     pDestRect,
                                     hDestWindowOverride,
                                     pDirtyRegion);

  if (! config.osd.pump)
    return BMF_EndBufferSwap ( hr,
                               (IUnknown *)This );

  return hr;
}

COM_DECLSPEC_NOTHROW
__declspec (noinline)
HRESULT
WINAPI D3D9PresentCallbackEx (IDirect3DDevice9Ex *This,
                   _In_ const RECT               *pSourceRect,
                   _In_ const RECT               *pDestRect,
                   _In_       HWND                hDestWindowOverride,
                   _In_ const RGNDATA            *pDirtyRegion,
                   _In_       DWORD               dwFlags)
{
  BMF_BeginBufferSwap ();

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


#define D3D9_STUB_HRESULT(_Return, _Name, _Proto, _Args)                  \
  COM_DECLSPEC_NOTHROW __declspec (noinline) _Return STDMETHODCALLTYPE    \
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
  COM_DECLSPEC_NOTHROW __declspec (noinline) _Return STDMETHODCALLTYPE    \
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
  COM_DECLSPEC_NOTHROW __declspec (noinline) _Return STDMETHODCALLTYPE    \
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
  COM_DECLSPEC_NOTHROW __declspec (noinline) _Return STDMETHODCALLTYPE    \
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

COM_DECLSPEC_NOTHROW
__declspec (noinline)
int
STDMETHODCALLTYPE
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

typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory_t)(REFIID,IDXGIFactory**);
typedef HRESULT (STDMETHODCALLTYPE *GetSwapChain_t)
  (IDirect3DDevice9* This, UINT iSwapChain, IDirect3DSwapChain9** pSwapChain);

typedef HRESULT (STDMETHODCALLTYPE *CreateAdditionalSwapChain_t)
  (IDirect3DDevice9* This, D3DPRESENT_PARAMETERS* pPresentationParameters,
   IDirect3DSwapChain9** pSwapChain);

CreateAdditionalSwapChain_t D3D9CreateAdditionalSwapChain_Original = nullptr;

  COM_DECLSPEC_NOTHROW
  __declspec (noinline)
  HRESULT
  WINAPI D3D9PresentSwapCallback (IDirect3DSwapChain9 *This,
                          _In_ const RECT             *pSourceRect,
                          _In_ const RECT             *pDestRect,
                          _In_       HWND              hDestWindowOverride,
                          _In_ const RGNDATA          *pDirtyRegion,
                          _In_       DWORD             dwFlags)
  {
    BMF_BeginBufferSwap ();

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
    IDirect3DDevice9* dev = nullptr;

    if (SUCCEEDED (This->GetDevice (&dev)) && dev != nullptr) {
      HRESULT ret = BMF_EndBufferSwap ( hr,
                                        (IUnknown *)dev );
      ((IUnknown *)dev)->Release ();
      return ret;
    }

    return BMF_EndBufferSwap (hr);
  }

COM_DECLSPEC_NOTHROW
__declspec (noinline)
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
__declspec (noinline)
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

#if 0
  // Some games call this multiple times, if the last time this was called was over
  //   1 ms ago, let's assume this means the frame is done.
  if (SUCCEEDED (hr) && (! config.osd.pump)) {
    static DWORD dwTime = timeGetTime ();
    if (timeGetTime () - dwTime > 1) {
      BMF_BeginBufferSwap ();
      BMF_EndBufferSwap   (hr);
    }
    dwTime = timeGetTime ();
  }
#endif

  return hr;
}

  COM_DECLSPEC_NOTHROW
  __declspec (noinline)
  HRESULT
  WINAPI D3D9Reset ( IDirect3DDevice9      *This,
                     D3DPRESENT_PARAMETERS *pPresentationParameters )
  {
    dll_log.Log ( L"[!] %s (%08Xh, %08Xh) - "
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

    if (pPresentationParameters != nullptr) {
      // So we can wait on this if need be
      pPresentationParameters->Flags |= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

      if (config.render.framerate.buffer_count != -1) {
        dll_log.Log ( L"  >> Backbuffer Override: (Requested=%lu, Override=%lu)",
                        pPresentationParameters->BackBufferCount,
                          config.render.framerate.buffer_count );
      }

      if (config.render.framerate.present_interval != -1) {
        dll_log.Log ( L"  >> VSYNC Override: (Requested=1:%lu, Override=1:%lu)",
                        pPresentationParameters->PresentationInterval,
                          config.render.framerate.present_interval );
      }
    }

    BMF_SetPresentParamsD3D9 (This, pPresentationParameters);

    HRESULT hr;

    D3D9_CALL (hr, D3D9Reset_Original (This,
                                       pPresentationParameters));

    if (SUCCEEDED (hr)) {
#ifdef RESET_ENDS_FRAME
      if (! config.osd.pump)
        BMF_EndBufferSwap (hr);
#endif
    }

    return hr;
  }

typedef HRESULT (STDMETHODCALLTYPE *DrawIndexedPrimitive_t)
  ( IDirect3DDevice9* This,
    D3DPRIMITIVETYPE  Type,
    INT               BaseVertexIndex,
    UINT              MinVertexIndex,
    UINT              NumVertices,
    UINT              startIndex,
    UINT              primCount );

DrawIndexedPrimitive_t D3D9DrawIndexedPrimitive_Original = nullptr;

COM_DECLSPEC_NOTHROW
__declspec (noinline)
HRESULT
STDMETHODCALLTYPE
D3D9DrawIndexedPrimitive_Override ( IDirect3DDevice9* This,
                                    D3DPRIMITIVETYPE  Type,
                                    INT               BaseVertexIndex,
                                    UINT              MinVertexIndex,
                                    UINT              NumVertices,
                                    UINT              startIndex,
                                    UINT              primCount )
{
  return
    D3D9DrawIndexedPrimitive_Original ( This,
                                          Type,
                                            BaseVertexIndex,
                                              MinVertexIndex,
                                                NumVertices,
                                                  startIndex,
                                                    primCount );
}


typedef HRESULT (STDMETHODCALLTYPE *SetSamplerState_t)
  (IDirect3DDevice9*   This,
   DWORD               Sampler,
   D3DSAMPLERSTATETYPE Type,
   DWORD               Value);

SetSamplerState_t D3D9SetSamplerState_Original = nullptr;

COM_DECLSPEC_NOTHROW
__declspec (noinline)
HRESULT
STDMETHODCALLTYPE
D3D9SetSamplerState_Override (IDirect3DDevice9*   This,
                              DWORD               Sampler,
                              D3DSAMPLERSTATETYPE Type,
                              DWORD               Value)
{
  return D3D9SetSamplerState_Original (This, Sampler, Type, Value);
}


typedef HRESULT (STDMETHODCALLTYPE *SetViewport_t)
  (      IDirect3DDevice9* This,
   CONST D3DVIEWPORT9*     pViewport);

SetViewport_t D3D9SetViewport_Original = nullptr;

COM_DECLSPEC_NOTHROW
__declspec (noinline)
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

COM_DECLSPEC_NOTHROW
__declspec (noinline)
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

COM_DECLSPEC_NOTHROW
__declspec (noinline)
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

COM_DECLSPEC_NOTHROW
__declspec (noinline)
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

COM_DECLSPEC_NOTHROW
__declspec (noinline)
HRESULT
STDMETHODCALLTYPE
D3D9SetScissorRect_Override (IDirect3DDevice9* This,
                                   const RECT* pRect)
{
  return D3D9SetScissorRect_Original (This, pRect);
}


COM_DECLSPEC_NOTHROW
__declspec (noinline)
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

  if (pPresentationParameters != nullptr)
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

#if 0
  if (ppReturnedDeviceInterface != nullptr) {
    void** vtable                     = *(void***)*ppReturnedDeviceInterface;
    GetSwapChain_t       GetSwapChain = (GetSwapChain_t)vtable [14];
    IDirect3DSwapChain9* SwapChain    = nullptr;

    if (SUCCEEDED (
          GetSwapChain ( (IDirect3DDevice9 *)*ppReturnedDeviceInterface,
                           0,
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
#endif

  return ret;
}

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

COM_DECLSPEC_NOTHROW
__declspec (noinline)
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

COM_DECLSPEC_NOTHROW
__declspec (noinline)
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

COM_DECLSPEC_NOTHROW
__declspec (noinline)
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

COM_DECLSPEC_NOTHROW
__declspec (noinline)
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


HRESULT
__declspec (noinline)
WINAPI
BMF_IDirect3D9::CreateDevice (UINT                   Adapter,
                              D3DDEVTYPE             DeviceType,
                              HWND                   hFocusWindow,
                              DWORD                  BehaviorFlags,
                              D3DPRESENT_PARAMETERS* pPresentationParameters,
                              IDirect3DDevice9**     ppReturnedDeviceInterface)
{
  dll_log.Log ( L"[!] %s (%08Xh, %lu, %lu, %08Xh, 0x%08X, %08Xh, %08Xh) - "
                L"[Calling Thread: 0x%04x]",
                  L"IDirect3D9::CreateDevice", this, Adapter, (DWORD)DeviceType,
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

  if (pPresentationParameters != nullptr) {
    // So we can wait on this if need be
    pPresentationParameters->Flags |= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

    if (config.render.framerate.buffer_count != -1) {
      dll_log.Log ( L"  >> Backbuffer Override: (Requested=%lu, Override=%lu)",
                      pPresentationParameters->BackBufferCount,
                        config.render.framerate.buffer_count );
    }

    if (config.render.framerate.present_interval != -1) {
      dll_log.Log ( L"  >> VSYNC Override: (Requested=1:%lu, Override=1:%lu)",
                      pPresentationParameters->PresentationInterval,
                        config.render.framerate.present_interval );
    }
  }

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

  D3D9_CALL (ret, d3d9_->CreateDevice ( Adapter,
                                          DeviceType,
                                            hFocusWindow,
                                              BehaviorFlags,
                                                pPresentationParameters,
                                                  ppReturnedDeviceInterface ));

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

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 82,
                         "IDirect3DDevice9::DrawIndexedPrimitive",
                         D3D9DrawIndexedPrimitive_Override,
                         D3D9DrawIndexedPrimitive_Original,
                         DrawIndexedPrimitive_t);

#if 0
  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 87,
                         "IDirect3DDevice9::SetVertexDeclaration",
                         D3D9SetVertexDeclaration_Override,
                         D3D9SetVertexDeclaration_Original,
                         SetVertexDeclaration_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 89,
                         "IDirect3DDevice9::DrawIndexedPrimitive",
                         D3D9SetFVF_Override,
                         D3D9SetFVF_Original,
                         SetFVF_t);
#endif

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

  IDXGIFactory* factory = nullptr;

  // Only spawn the DXGI 1.4 budget thread if ... DXGI 1.4 is implemented.
  if (SUCCEEDED (CreateDXGIFactory (__uuidof (IDXGIFactory4), &factory))) {
    IDXGIAdapter* adapter;
    factory->EnumAdapters (Adapter, &adapter);

    BMF_StartDXGI_1_4_BudgetThread (&adapter);

    adapter->Release ();
    factory->Release ();
  }

  return ret;
}

HRESULT
__declspec (noinline)
WINAPI
D3D9CreateDevice_Override (IDirect3D9*            This,
                  UINT                   Adapter,
                  D3DDEVTYPE             DeviceType,
                  HWND                   hFocusWindow,
                  DWORD                  BehaviorFlags,
                  D3DPRESENT_PARAMETERS* pPresentationParameters,
                  IDirect3DDevice9**     ppReturnedDeviceInterface)
{
  dll_log.Log ( L"[!] %s (%08Xh, %lu, %lu, %08Xh, 0x%08X, %08Xh, %08Xh) - "
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

  if (pPresentationParameters != nullptr) {
    // So we can wait on this if need be
    pPresentationParameters->Flags |= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

    if (config.render.framerate.buffer_count != -1) {
      dll_log.Log ( L"  >> Backbuffer Override: (Requested=%lu, Override=%lu)",
                      pPresentationParameters->BackBufferCount,
                        config.render.framerate.buffer_count );
    }

    if (config.render.framerate.present_interval != -1) {
      dll_log.Log ( L"  >> VSYNC Override: (Requested=1:%lu, Override=1:%lu)",
                      pPresentationParameters->PresentationInterval,
                        config.render.framerate.present_interval );
    }
  }

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

  D3D9_CALL (ret, D3D9CreateDevice_Original ( This, Adapter,
                                          DeviceType,
                                            hFocusWindow,
                                              BehaviorFlags,
                                                pPresentationParameters,
                                                  ppReturnedDeviceInterface ));

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

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 82,
                         "IDirect3DDevice9::DrawIndexedPrimitive",
                         D3D9DrawIndexedPrimitive_Override,
                         D3D9DrawIndexedPrimitive_Original,
                         DrawIndexedPrimitive_t);

#if 0
  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 87,
                         "IDirect3DDevice9::SetVertexDeclaration",
                         D3D9SetVertexDeclaration_Override,
                         D3D9SetVertexDeclaration_Original,
                         SetVertexDeclaration_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 89,
                         "IDirect3DDevice9::DrawIndexedPrimitive",
                         D3D9SetFVF_Override,
                         D3D9SetFVF_Original,
                         SetFVF_t);
#endif

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

  IDXGIFactory* factory = nullptr;

  // Only spawn the DXGI 1.4 budget thread if ... DXGI 1.4 is implemented.
  if (SUCCEEDED (CreateDXGIFactory (__uuidof (IDXGIFactory4), &factory))) {
    IDXGIAdapter* adapter;
    factory->EnumAdapters (Adapter, &adapter);

    BMF_StartDXGI_1_4_BudgetThread (&adapter);

    adapter->Release ();
    factory->Release ();
  }

  return ret;
}


__declspec (noinline)
IDirect3D9*
STDMETHODCALLTYPE
Direct3DCreate9 (UINT SDKVersion)
{
  WaitForInit ();

  IDirect3D9* d3d9 = nullptr;

  IDirect3D9Ex* pD3D9 = nullptr;
  if (true){//FAILED (Direct3DCreate9Ex (SDKVersion, &pD3D9))) {
    if (Direct3DCreate9_Import) {
      dll_log.Log ( L"[!] %s (%lu) - "
        L"[Calling Thread: 0x%04x]",
        L"Direct3DCreate9", SDKVersion, GetCurrentThreadId ());

      d3d9 = Direct3DCreate9_Import (SDKVersion);
    }
  } else {
    return pD3D9;
  }

  if (d3d9 != nullptr) {
    void** vftable = *(void***)&*d3d9;

    BMF_CreateFuncHook ( L"IDirect3D9::CreateDevice",
                         vftable [16],
                         D3D9CreateDevice_Override,
              (LPVOID *)&D3D9CreateDevice_Original );
    BMF_EnableHook     (vftable [16]);
  }

  return d3d9;
}

HRESULT
__declspec (noinline)
STDMETHODCALLTYPE
Direct3DCreate9Ex (__in UINT SDKVersion, __out IDirect3D9Ex **ppD3D)
{
  WaitForInit ();

  dll_log.Log ( L"[!] %s (%lu, %08Xh) - "
                L"[Calling Thread: 0x%04x]",
                  L"Direct3DCreate9Ex",
                    SDKVersion,
                      ppD3D,
                        GetCurrentThreadId () );

  HRESULT hr = E_FAIL;

  IDirect3D9Ex* pD3D = nullptr;

  if (Direct3DCreate9Ex_Import) {
    D3D9_CALL (hr, Direct3DCreate9Ex_Import (SDKVersion, &pD3D));

    if (SUCCEEDED (hr) && pD3D != nullptr) {
      void** vftable = *(void***)&*pD3D;

      BMF_CreateFuncHook ( L"IDirect3D9::CreateDevice",
                           vftable [16],
                           D3D9CreateDevice_Override,
                (LPVOID *)&D3D9CreateDevice_Original );
      BMF_EnableHook     (vftable [16]);

      BMF_CreateFuncHook ( L"IDirect3D9Ex::CreateDeviceEx",
                           vftable [20],
                           D3D9CreateDeviceEx_Override,
                (LPVOID *)&D3D9CreateDeviceEx_Original );
      BMF_EnableHook     (vftable [20]);
      *ppD3D = pD3D;
    }
  }

  return hr;
}