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

#ifndef __BMF__D3D9_BACKEND_H__
#define __BMF__D3D9_BACKEND_H__

#include <Windows.h>

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

interface DECLSPEC_UUID("02177241-69FC-400C-8FF1-93A44DF6861D") IDirect3D9Ex;
interface DECLSPEC_UUID("81BDCBCA-64D4-426d-AE8D-AD0147F4275C") IDirect3D9;
interface DECLSPEC_UUID("B18B10CE-2649-405a-870F-95F777D4313A") IDirect3DDevice9Ex;
interface DECLSPEC_UUID("D0223B96-BF7A-43fd-92BD-A43B0D82B9EB") IDirect3DDevice9;

namespace BMF
{
  namespace D3D9
  {

    bool Startup  (void);
    bool Shutdown (void);

  }
}

#endif /* __BMF__D3D9_BACKEND_H__ */