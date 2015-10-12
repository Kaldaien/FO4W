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

//#undef STDMETHODCALLTYPE
//#define STDMETHODCALLTYPE __stdcall

typedef DWORD D3DCOLOR;

/* Direct3D9 Device types */
typedef enum _D3DDEVTYPE
{
  D3DDEVTYPE_HAL         = 1,
  D3DDEVTYPE_REF         = 2,
  D3DDEVTYPE_SW          = 3,

  D3DDEVTYPE_NULLREF     = 4,

  D3DDEVTYPE_FORCE_DWORD  = 0x7fffffff
} D3DDEVTYPE;

/* Multi-Sample buffer types */
typedef enum _D3DMULTISAMPLE_TYPE
{
  D3DMULTISAMPLE_NONE            =  0,
  D3DMULTISAMPLE_NONMASKABLE     =  1,
  D3DMULTISAMPLE_2_SAMPLES       =  2,
  D3DMULTISAMPLE_3_SAMPLES       =  3,
  D3DMULTISAMPLE_4_SAMPLES       =  4,
  D3DMULTISAMPLE_5_SAMPLES       =  5,
  D3DMULTISAMPLE_6_SAMPLES       =  6,
  D3DMULTISAMPLE_7_SAMPLES       =  7,
  D3DMULTISAMPLE_8_SAMPLES       =  8,
  D3DMULTISAMPLE_9_SAMPLES       =  9,
  D3DMULTISAMPLE_10_SAMPLES      = 10,
  D3DMULTISAMPLE_11_SAMPLES      = 11,
  D3DMULTISAMPLE_12_SAMPLES      = 12,
  D3DMULTISAMPLE_13_SAMPLES      = 13,
  D3DMULTISAMPLE_14_SAMPLES      = 14,
  D3DMULTISAMPLE_15_SAMPLES      = 15,
  D3DMULTISAMPLE_16_SAMPLES      = 16,

  D3DMULTISAMPLE_FORCE_DWORD     = 0x7fffffff
} D3DMULTISAMPLE_TYPE;

typedef enum _D3DFORMAT
{
  D3DFMT_UNKNOWN              =  0,

  D3DFMT_R8G8B8               = 20,
  D3DFMT_A8R8G8B8             = 21,
  D3DFMT_X8R8G8B8             = 22,
  D3DFMT_R5G6B5               = 23,
  D3DFMT_X1R5G5B5             = 24,
  D3DFMT_A1R5G5B5             = 25,
  D3DFMT_A4R4G4B4             = 26,
  D3DFMT_R3G3B2               = 27,
  D3DFMT_A8                   = 28,
  D3DFMT_A8R3G3B2             = 29,
  D3DFMT_X4R4G4B4             = 30,
  D3DFMT_A2B10G10R10          = 31,
  D3DFMT_A8B8G8R8             = 32,
  D3DFMT_X8B8G8R8             = 33,
  D3DFMT_G16R16               = 34,
  D3DFMT_A2R10G10B10          = 35,
  D3DFMT_A16B16G16R16         = 36,

  D3DFMT_A8P8                 = 40,
  D3DFMT_P8                   = 41,

  D3DFMT_L8                   = 50,
  D3DFMT_A8L8                 = 51,
  D3DFMT_A4L4                 = 52,

  D3DFMT_V8U8                 = 60,
  D3DFMT_L6V5U5               = 61,
  D3DFMT_X8L8V8U8             = 62,
  D3DFMT_Q8W8V8U8             = 63,
  D3DFMT_V16U16               = 64,
  D3DFMT_A2W10V10U10          = 67,

  D3DFMT_UYVY                 = MAKEFOURCC('U', 'Y', 'V', 'Y'),
  D3DFMT_R8G8_B8G8            = MAKEFOURCC('R', 'G', 'B', 'G'),
  D3DFMT_YUY2                 = MAKEFOURCC('Y', 'U', 'Y', '2'),
  D3DFMT_G8R8_G8B8            = MAKEFOURCC('G', 'R', 'G', 'B'),
  D3DFMT_DXT1                 = MAKEFOURCC('D', 'X', 'T', '1'),
  D3DFMT_DXT2                 = MAKEFOURCC('D', 'X', 'T', '2'),
  D3DFMT_DXT3                 = MAKEFOURCC('D', 'X', 'T', '3'),
  D3DFMT_DXT4                 = MAKEFOURCC('D', 'X', 'T', '4'),
  D3DFMT_DXT5                 = MAKEFOURCC('D', 'X', 'T', '5'),

  D3DFMT_D16_LOCKABLE         = 70,
  D3DFMT_D32                  = 71,
  D3DFMT_D15S1                = 73,
  D3DFMT_D24S8                = 75,
  D3DFMT_D24X8                = 77,
  D3DFMT_D24X4S4              = 79,
  D3DFMT_D16                  = 80,

  D3DFMT_D32F_LOCKABLE        = 82,
  D3DFMT_D24FS8               = 83,

  /* D3D9Ex only -- */
#if !defined(D3D_DISABLE_9EX)

  /* Z-Stencil formats valid for CPU access */
  D3DFMT_D32_LOCKABLE         = 84,
  D3DFMT_S8_LOCKABLE          = 85,

#endif // !D3D_DISABLE_9EX
  /* -- D3D9Ex only */


  D3DFMT_L16                  = 81,

  D3DFMT_VERTEXDATA           =100,
  D3DFMT_INDEX16              =101,
  D3DFMT_INDEX32              =102,

  D3DFMT_Q16W16V16U16         =110,

  D3DFMT_MULTI2_ARGB8         = MAKEFOURCC('M','E','T','1'),

  // Floating point surface formats

  // s10e5 formats (16-bits per channel)
  D3DFMT_R16F                 = 111,
  D3DFMT_G16R16F              = 112,
  D3DFMT_A16B16G16R16F        = 113,

  // IEEE s23e8 formats (32-bits per channel)
  D3DFMT_R32F                 = 114,
  D3DFMT_G32R32F              = 115,
  D3DFMT_A32B32G32R32F        = 116,

  D3DFMT_CxV8U8               = 117,

  /* D3D9Ex only -- */
#if !defined(D3D_DISABLE_9EX)

  // Monochrome 1 bit per pixel format
  D3DFMT_A1                   = 118,

  // 2.8 biased fixed point
  D3DFMT_A2B10G10R10_XR_BIAS  = 119,


  // Binary format indicating that the data has no inherent type
  D3DFMT_BINARYBUFFER         = 199,

#endif // !D3D_DISABLE_9EX
  /* -- D3D9Ex only */


  D3DFMT_FORCE_DWORD          =0x7fffffff
} D3DFORMAT;

/* SwapEffects */
typedef enum _D3DSWAPEFFECT
{
  D3DSWAPEFFECT_DISCARD           = 1,
  D3DSWAPEFFECT_FLIP              = 2,
  D3DSWAPEFFECT_COPY              = 3,

  /* D3D9Ex only -- */
#if !defined(D3D_DISABLE_9EX)
  D3DSWAPEFFECT_OVERLAY           = 4,
  D3DSWAPEFFECT_FLIPEX            = 5,
#endif // !D3D_DISABLE_9EX
  /* -- D3D9Ex only */

  D3DSWAPEFFECT_FORCE_DWORD       = 0x7fffffff
} D3DSWAPEFFECT;

/* Resize Optional Parameters */
typedef struct _D3DPRESENT_PARAMETERS_
{
  UINT                BackBufferWidth;
  UINT                BackBufferHeight;
  D3DFORMAT           BackBufferFormat;
  UINT                BackBufferCount;

  D3DMULTISAMPLE_TYPE MultiSampleType;
  DWORD               MultiSampleQuality;

  D3DSWAPEFFECT       SwapEffect;
  HWND                hDeviceWindow;
  BOOL                Windowed;
  BOOL                EnableAutoDepthStencil;
  D3DFORMAT           AutoDepthStencilFormat;
  DWORD               Flags;

  /* FullScreen_RefreshRateInHz must be zero for Windowed mode */
  UINT                FullScreen_RefreshRateInHz;
  UINT                PresentationInterval;
} D3DPRESENT_PARAMETERS;

typedef enum D3DSCANLINEORDERING
{
  D3DSCANLINEORDERING_UNKNOWN                    = 0, 
  D3DSCANLINEORDERING_PROGRESSIVE                = 1,
  D3DSCANLINEORDERING_INTERLACED                 = 2,
} D3DSCANLINEORDERING;

typedef struct D3DDISPLAYMODEEX
{
  UINT                    Size;
  UINT                    Width;
  UINT                    Height;
  UINT                    RefreshRate;
  D3DFORMAT               Format;
  D3DSCANLINEORDERING     ScanLineOrdering;
} D3DDISPLAYMODEEX;

typedef interface IDirect3D9                     IDirect3D9;
typedef interface IDirect3DDevice9               IDirect3DDevice9;
typedef interface IDirect3DSwapChain9            IDirect3DSwapChain9;
typedef interface IDirect3D9Ex                   IDirect3D9Ex;
typedef interface IDirect3DDevice9Ex             IDirect3DDevice9Ex;
typedef interface IDirect3DSwapChain9Ex          IDirect3DSwapChain9Ex;

typedef IDirect3D9*
  (STDMETHODCALLTYPE *Direct3DCreate9PROC)(  UINT           SDKVersion);
typedef HRESULT
  (STDMETHODCALLTYPE *Direct3DCreate9ExPROC)(UINT           SDKVersion,
                                             IDirect3D9Ex** d3d9ex);

typedef HRESULT (STDMETHODCALLTYPE *D3D9PresentSwapChain_t)(
           IDirect3DDevice9    *This,
_In_ const RECT                *pSourceRect,
_In_ const RECT                *pDestRect,
_In_       HWND                 hDestWindowOverride,
_In_ const RGNDATA             *pDirtyRegion/*,
_In_       DWORD                dwFlags*/);

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

D3D9PresentSwapChain_t   D3D9Present_Original        = nullptr;
D3D9PresentSwapChainEx_t D3D9PresentEx_Original      = nullptr;
D3D9PresentSwapChain_t   D3D9PresentSwap_Original    = nullptr;
D3D9CreateDevice_t       D3D9CreateDevice_Original   = nullptr;
D3D9CreateDeviceEx_t     D3D9CreateDeviceEx_Original = nullptr;

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

#ifdef _WIN64
# define D3D9_PROLOG
#else
# define D3D9_PROLOG  __asm PUSHAD;
#endif

#ifdef _WIN64
# define D3D9_EPILOG
#else
# define D3D9_EPILOG __asm POPAD;
#endif

extern "C" {
#ifndef _WIN64
__declspec (naked)
void
#else
HRESULT
#endif
__stdcall D3D9PresentCallback (IDirect3DDevice9 *This,
                            _In_ const RECT             *pSourceRect,
                            _In_ const RECT             *pDestRect,
                            _In_       HWND              hDestWindowOverride,
                            _In_ const RGNDATA          *pDirtyRegion/*,
                            _In_       DWORD             dwFlags*/)
{
  D3D9_PROLOG

  BMF_BeginBufferSwap ();

  //dll_log.Log (L"Flip");

#ifndef _WIN64
  BMF_EndBufferSwap (S_OK);

  D3D9_EPILOG

  __asm {
    jmp D3D9Present_Original;
  };
#else
  return BMF_EndBufferSwap (D3D9Present_Original (This,
                                                  pSourceRect,
                                                  pDestRect,
                                                  hDestWindowOverride,
                                                  pDirtyRegion));
#endif
}

#ifndef _WIN64
__declspec (naked)
void
#else
HRESULT
#endif
__stdcall D3D9PresentCallbackEx (IDirect3DDevice9Ex *This,
                      _In_ const RECT               *pSourceRect,
                      _In_ const RECT               *pDestRect,
                      _In_       HWND                hDestWindowOverride,
                      _In_ const RGNDATA            *pDirtyRegion,
                      _In_       DWORD               dwFlags)
{
  D3D9_PROLOG

    BMF_BeginBufferSwap ();

  //dll_log.Log (L"Flip");

#ifndef _WIN64
  BMF_EndBufferSwap (S_OK);

  D3D9_EPILOG

    __asm {
    jmp D3D9PresentEx_Original;
  };
#else
  return BMF_EndBufferSwap (D3D9PresentEx_Original (This,
                                                    pSourceRect,
                                                    pDestRect,
                                                    hDestWindowOverride,
                                                    pDirtyRegion,
                                                    dwFlags));
#endif
}
}

extern "C" {
#ifndef _WIN64
  __declspec (naked)
    void
#else
  HRESULT
#endif
    __stdcall D3D9PresentSwapCallback (IDirect3DDevice9 *This,
      _In_ const RECT             *pSourceRect,
      _In_ const RECT             *pDestRect,
      _In_       HWND              hDestWindowOverride,
      _In_ const RGNDATA          *pDirtyRegion/*,
                                               _In_       DWORD             dwFlags*/)
  {
    D3D9_PROLOG

    BMF_BeginBufferSwap ();

    //dll_log.Log (L"Flip");

#ifndef _WIN64
    BMF_EndBufferSwap (S_OK);

    D3D9_EPILOG

    __asm {
      jmp D3D9PresentSwap_Original;
    };
#else
    return BMF_EndBufferSwap (D3D9Present_Original (This,
      pSourceRect,
      pDestRect,
      hDestWindowOverride,
      pDirtyRegion));
#endif
  }
}


#define D3D9_STUB_HRESULT(_Return, _Name, _Proto, _Args)                  \
  __declspec (nothrow) _Return STDMETHODCALLTYPE                          \
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
  __declspec (nothrow) _Return STDMETHODCALLTYPE                          \
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
  __declspec (nothrow) _Return STDMETHODCALLTYPE                          \
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
  __declspec (nothrow) _Return STDMETHODCALLTYPE                          \
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
}

#ifdef _WIN64
#define D3D9_VIRTUAL_OVERRIDE(_Base,_Index,_Name,_Override,_Original,_Type) { \
  void** vftable = *(void***)*_Base;                                          \
                                                                              \
  if (vftable [_Index] != _Override) {                                        \
    DWORD dwProtect;                                                          \
                                                                              \
    VirtualProtect (&vftable [_Index], 8, PAGE_EXECUTE_READWRITE, &dwProtect);\
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
    VirtualProtect (&vftable [_Index], 8, dwProtect, &dwProtect);             \
                                                                              \
    dll_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n",  \
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
    VirtualProtect (&vftable [_Index], 4, dwProtect, &dwProtect);             \
                                                                              \
    dll_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n",  \
                  L##_Name, vftable [_Index],                                 \
                  BMF_DescribeVirtualProtectFlags (dwProtect));               \
  }                                                                           \
}
#endif

#define D3D9_CALL(_Ret, _Call) {                                     \
  dll_log.LogEx (true, L"  Calling original function: ");            \
  (_Ret) = (_Call);                                                  \
  dll_log.LogEx (false, L"(ret=%s)\n\n", BMF_DescribeHRESULT (_Ret));\
}

typedef HRESULT (WINAPI *CreateDXGIFactory_t)(REFIID,IDXGIFactory**);
typedef HRESULT (STDMETHODCALLTYPE *GetSwapChain_t)
  (IDirect3DDevice9* This, UINT iSwapChain, IDirect3DSwapChain9** pSwapChain);

typedef HRESULT (STDMETHODCALLTYPE *CreateAdditionalSwapChain_t)
  (IDirect3DDevice9* This, D3DPRESENT_PARAMETERS* pPresentationParameters,
   IDirect3DSwapChain9** pSwapChain);

CreateAdditionalSwapChain_t D3D9CreateAdditionalSwapChain_Original = nullptr;

__declspec (nothrow)
HRESULT
STDMETHODCALLTYPE
D3D9CreateAdditionalSwapChain_Override (IDirect3DDevice9*      This,
                                        D3DPRESENT_PARAMETERS* pPresentationParameters,
                                        IDirect3DSwapChain9**  pSwapChain)
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

__declspec (nothrow)
HRESULT
STDMETHODCALLTYPE
D3D9EndScene_Override (IDirect3DDevice9* This)
{
D3D9_PROLOG

#if 0
  dll_log.Log (L"[!] %s (%08Xh) - "
    L"[Calling Thread: 0x%04x]",
    L"IDirect3DDevice9::EndScene", This,
    GetCurrentThreadId ()
  );
#endif

  HRESULT hr;

  hr = D3D9EndScene_Original (This);
  //D3D9_CALL (hr, D3D9EndScene_Original (This));

  if (SUCCEEDED (hr)) {
    //BMF_EndBufferSwap (S_OK);
    //D3D9_VIRTUAL_OVERRIDE (pSwapChain, 3,
      //"IDirect3DSwapChain9::Present", D3D9PresentSwapCallback,
      //D3D9PresentSwap_Original, D3D9PresentSwapChain_t);
  }

D3D9_EPILOG

  return hr;
}

__declspec (nothrow)
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
  D3D9_PROLOG

  //std::wstring iname = BMF_GetDXGIAdapterInterface (This);

  //dll_log_CALL_I2 (iname.c_str (), L"GetDesc2", L"%08Xh, %08Xh", This, pDesc);

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

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 17,
                         "IDirect3DDevice9Ex::Present", D3D9PresentCallback,
                         D3D9Present_Original, D3D9PresentSwapChain_t);

  D3D9_VIRTUAL_OVERRIDE (ppReturnedDeviceInterface, 121,
                         "IDirect3DDevice9Ex::PresentEx", D3D9PresentCallbackEx,
                         D3D9PresentEx_Original, D3D9PresentSwapChainEx_t);

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
    factory->EnumAdapters (0, &adapter);

    BMF_StartDXGI_1_4_BudgetThread (&adapter);

    adapter->Release ();
  }

  void** vftable                    = *(void***)*ppReturnedDeviceInterface;
  GetSwapChain_t       GetSwapChain = (GetSwapChain_t)vftable [14];
  IDirect3DSwapChain9* SwapChain    = nullptr;

  for (int i = 0; i < 4; i++) {
    if (SUCCEEDED (GetSwapChain ((IDirect3DDevice9 *)*ppReturnedDeviceInterface, i, &SwapChain))) {
      D3D9_VIRTUAL_OVERRIDE (&SwapChain, 3,
                             "IDirect3DSwapChain9::Present", D3D9PresentSwapCallback,
                             D3D9PresentSwap_Original, D3D9PresentSwapChain_t);

      ((IUnknown *)SwapChain)->Release ();
    }
  }

  D3D9_EPILOG

  return ret;
}

__declspec (nothrow)
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
  D3D9_PROLOG

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
    factory->EnumAdapters (0, &adapter);

    BMF_StartDXGI_1_4_BudgetThread (&adapter);

    adapter->Release ();
  }

  void** vftable                    = *(void***)*ppReturnedDeviceInterface;
  GetSwapChain_t       GetSwapChain = (GetSwapChain_t)vftable [14];
  IDirect3DSwapChain9* SwapChain    = nullptr;

  for (int i = 0; i < 4; i++) {
    if (SUCCEEDED (GetSwapChain (*ppReturnedDeviceInterface, i, &SwapChain))) {
      D3D9_VIRTUAL_OVERRIDE (&SwapChain, 3,
                             "IDirect3DSwapChain9::Present", D3D9PresentSwapCallback,
                             D3D9PresentSwap_Original, D3D9PresentSwapChain_t);

      ((IUnknown *)SwapChain)->Release ();
    }
  }

  D3D9_EPILOG

  return ret;
}

IDirect3D9*
STDMETHODCALLTYPE
Direct3DCreate9 (UINT SDKVersion)
{
  D3D9_PROLOG

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

  D3D9_EPILOG

  return d3d9;
}

HRESULT
STDMETHODCALLTYPE
Direct3DCreate9Ex (__in UINT SDKVersion, __out IDirect3D9Ex **ppD3D)
{
  D3D9_PROLOG

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

  D3D9_EPILOG

  return hr;
}