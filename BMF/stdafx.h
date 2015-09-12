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

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#undef COM_NO_WINDOWS_H
#include <Windows.h>

// TODO: reference additional headers your program requires here

/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 500
#endif

/* verify that the <rpcsal.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCSAL_H_VERSION__
#define __REQUIRED_RPCSAL_H_VERSION__ 100
#endif

#define DXGI_USAGE_SHADER_INPUT             ( 1L << (0 + 4) )
#define DXGI_USAGE_RENDER_TARGET_OUTPUT     ( 1L << (1 + 4) )
#define DXGI_USAGE_BACK_BUFFER              ( 1L << (2 + 4) )
#define DXGI_USAGE_SHARED                   ( 1L << (3 + 4) )
#define DXGI_USAGE_READ_ONLY                ( 1L << (4 + 4) )
#define DXGI_USAGE_DISCARD_ON_PRESENT       ( 1L << (5 + 4) )
#define DXGI_USAGE_UNORDERED_ACCESS         ( 1L << (6 + 4) )
typedef UINT DXGI_USAGE;

#include "rpc.h"
#include "rpcndr.h"

#include <Unknwnbase.h>

EXTERN_C const IID IID_IDXGIObject;

MIDL_INTERFACE ("aec22fb8-76f3-4639-9be0-28eb43a67a2e")
IDXGIObject : public IUnknown
{
public:
  virtual HRESULT STDMETHODCALLTYPE SetPrivateData (
    /* [annotation][in] */
    _In_  REFGUID Name,
    /* [in] */ UINT DataSize,
    /* [annotation][in] */
    _In_reads_bytes_ (DataSize)  const void *pData) = 0;

  virtual HRESULT STDMETHODCALLTYPE SetPrivateDataInterface (
    /* [annotation][in] */
    _In_  REFGUID Name,
    /* [annotation][in] */
    _In_  const IUnknown *pUnknown) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetPrivateData (
    /* [annotation][in] */
    _In_  REFGUID Name,
    /* [annotation][out][in] */
    _Inout_  UINT *pDataSize,
    /* [annotation][out] */
    _Out_writes_bytes_ (*pDataSize)  void *pData) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetParent (
    /* [annotation][in] */
    _In_  REFIID riid,
    /* [annotation][retval][out] */
    _Out_  void **ppParent) = 0;

};

#include <dxgitype.h>

typedef struct DXGI_FRAME_STATISTICS
{
  UINT PresentCount;
  UINT PresentRefreshCount;
  UINT SyncRefreshCount;
  LARGE_INTEGER SyncQPCTime;
  LARGE_INTEGER SyncGPUTime;
} 	DXGI_FRAME_STATISTICS;

typedef struct DXGI_MAPPED_RECT
{
  INT Pitch;
  BYTE *pBits;
} 	DXGI_MAPPED_RECT;


EXTERN_C const IID IID_IDXGIDeviceSubObject;


MIDL_INTERFACE ("3d3e0379-f9de-4d58-bb6c-18d62992f1a6")
IDXGIDeviceSubObject : public IDXGIObject
{
public:
  virtual HRESULT STDMETHODCALLTYPE GetDevice (
    /* [annotation][in] */
    _In_  REFIID riid,
    /* [annotation][retval][out] */
    _Out_  void **ppDevice) = 0;

};

typedef struct DXGI_SURFACE_DESC
{
  UINT Width;
  UINT Height;
  DXGI_FORMAT Format;
  DXGI_SAMPLE_DESC SampleDesc;
} 	DXGI_SURFACE_DESC;

EXTERN_C const IID IID_IDXGISurface;

MIDL_INTERFACE ("cafcb56c-6ac3-4889-bf47-9e23bbd260ec")
IDXGISurface : public IDXGIDeviceSubObject
{
public:
  virtual HRESULT STDMETHODCALLTYPE GetDesc (
    /* [annotation][out] */
    _Out_  DXGI_SURFACE_DESC *pDesc) = 0;

  virtual HRESULT STDMETHODCALLTYPE Map (
    /* [annotation][out] */
    _Out_  DXGI_MAPPED_RECT *pLockedRect,
    /* [in] */ UINT MapFlags) = 0;

  virtual HRESULT STDMETHODCALLTYPE Unmap (void) = 0;

};


typedef struct DXGI_OUTPUT_DESC
{
  WCHAR DeviceName [32];
  RECT DesktopCoordinates;
  BOOL AttachedToDesktop;
  DXGI_MODE_ROTATION Rotation;
  HMONITOR Monitor;
} 	DXGI_OUTPUT_DESC;

EXTERN_C const IID IID_IDXGIOutput;

MIDL_INTERFACE ("ae02eedb-c735-4690-8d52-5a8dc20213aa")
IDXGIOutput : public IDXGIObject
{
public:
  virtual HRESULT STDMETHODCALLTYPE GetDesc (
    /* [annotation][out] */
    _Out_  DXGI_OUTPUT_DESC *pDesc) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetDisplayModeList (
    /* [in] */ DXGI_FORMAT EnumFormat,
    /* [in] */ UINT Flags,
    /* [annotation][out][in] */
    _Inout_  UINT *pNumModes,
    /* [annotation][out] */
    _Out_writes_to_opt_ (*pNumModes,*pNumModes)  DXGI_MODE_DESC *pDesc) = 0;

  virtual HRESULT STDMETHODCALLTYPE FindClosestMatchingMode (
    /* [annotation][in] */
    _In_  const DXGI_MODE_DESC *pModeToMatch,
    /* [annotation][out] */
    _Out_  DXGI_MODE_DESC *pClosestMatch,
    /* [annotation][in] */
    _In_opt_  IUnknown *pConcernedDevice) = 0;

  virtual HRESULT STDMETHODCALLTYPE WaitForVBlank (void) = 0;

  virtual HRESULT STDMETHODCALLTYPE TakeOwnership (
    /* [annotation][in] */
    _In_  IUnknown *pDevice,
    BOOL Exclusive) = 0;

  virtual void STDMETHODCALLTYPE ReleaseOwnership (void) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetGammaControlCapabilities (
    /* [annotation][out] */
    _Out_  DXGI_GAMMA_CONTROL_CAPABILITIES *pGammaCaps) = 0;

  virtual HRESULT STDMETHODCALLTYPE SetGammaControl (
    /* [annotation][in] */
    _In_  const DXGI_GAMMA_CONTROL *pArray) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetGammaControl (
    /* [annotation][out] */
    _Out_  DXGI_GAMMA_CONTROL *pArray) = 0;

  virtual HRESULT STDMETHODCALLTYPE SetDisplaySurface (
    /* [annotation][in] */
    _In_  IDXGISurface *pScanoutSurface) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetDisplaySurfaceData (
    /* [annotation][in] */
    _In_  IDXGISurface *pDestination) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics (
    /* [annotation][out] */
    _Out_  DXGI_FRAME_STATISTICS *pStats) = 0;

};



typedef struct DXGI_ADAPTER_DESC
{
  WCHAR Description [128];
  UINT VendorId;
  UINT DeviceId;
  UINT SubSysId;
  UINT Revision;
  SIZE_T DedicatedVideoMemory;
  SIZE_T DedicatedSystemMemory;
  SIZE_T SharedSystemMemory;
  LUID AdapterLuid;
} 	DXGI_ADAPTER_DESC;

EXTERN_C const IID IID_IDXGIAdapter;

MIDL_INTERFACE ("2411e7e1-12ac-4ccf-bd14-9798e8534dc0")
IDXGIAdapter : public IDXGIObject
{
public:
  virtual HRESULT STDMETHODCALLTYPE EnumOutputs (
    /* [in] */ UINT Output,
    /* [annotation][out][in] */
    _Out_  IDXGIOutput **ppOutput) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetDesc (
    /* [annotation][out] */
    _Out_  DXGI_ADAPTER_DESC *pDesc) = 0;

  virtual HRESULT STDMETHODCALLTYPE CheckInterfaceSupport (
    /* [annotation][in] */
    _In_  REFGUID InterfaceName,
    /* [annotation][out] */
    _Out_  LARGE_INTEGER *pUMDVersion) = 0;

};

typedef
enum DXGI_SWAP_EFFECT
{
  DXGI_SWAP_EFFECT_DISCARD = 0,
  DXGI_SWAP_EFFECT_SEQUENTIAL = 1,
  DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL = 3
} 	DXGI_SWAP_EFFECT;

typedef
enum DXGI_SWAP_CHAIN_FLAG
{
  DXGI_SWAP_CHAIN_FLAG_NONPREROTATED = 1,
  DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH = 2,
  DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE = 4,
  DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT = 8,
  DXGI_SWAP_CHAIN_FLAG_RESTRICT_SHARED_RESOURCE_DRIVER = 16,
  DXGI_SWAP_CHAIN_FLAG_DISPLAY_ONLY = 32,
  DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT = 64,
  DXGI_SWAP_CHAIN_FLAG_FOREGROUND_LAYER = 128,
  DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO = 256,
  DXGI_SWAP_CHAIN_FLAG_YUV_VIDEO = 512
} 	DXGI_SWAP_CHAIN_FLAG;

typedef struct DXGI_SWAP_CHAIN_DESC
{
  DXGI_MODE_DESC BufferDesc;
  DXGI_SAMPLE_DESC SampleDesc;
  DXGI_USAGE BufferUsage;
  UINT BufferCount;
  HWND OutputWindow;
  BOOL Windowed;
  DXGI_SWAP_EFFECT SwapEffect;
  UINT Flags;
} 	DXGI_SWAP_CHAIN_DESC;

EXTERN_C const IID IID_IDXGISwapChain;


MIDL_INTERFACE ("310d36a0-d2e7-4c0a-aa04-6a9d23b8886a")
IDXGISwapChain : public IDXGIDeviceSubObject
{
public:
  virtual HRESULT STDMETHODCALLTYPE Present (
    /* [in] */ UINT SyncInterval,
    /* [in] */ UINT Flags) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetBuffer (
    /* [in] */ UINT Buffer,
    /* [annotation][in] */
    _In_  REFIID riid,
    /* [annotation][out][in] */
    _Out_  void **ppSurface) = 0;

  virtual HRESULT STDMETHODCALLTYPE SetFullscreenState (
    /* [in] */ BOOL Fullscreen,
    /* [annotation][in] */
    _In_opt_  IDXGIOutput *pTarget) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetFullscreenState (
    /* [annotation][out] */
    _Out_opt_  BOOL *pFullscreen,
    /* [annotation][out] */
    _Out_opt_  IDXGIOutput **ppTarget) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetDesc (
    /* [annotation][out] */
    _Out_  DXGI_SWAP_CHAIN_DESC *pDesc) = 0;

  virtual HRESULT STDMETHODCALLTYPE ResizeBuffers (
    /* [in] */ UINT BufferCount,
    /* [in] */ UINT Width,
    /* [in] */ UINT Height,
    /* [in] */ DXGI_FORMAT NewFormat,
    /* [in] */ UINT SwapChainFlags) = 0;

  virtual HRESULT STDMETHODCALLTYPE ResizeTarget (
    /* [annotation][in] */
    _In_  const DXGI_MODE_DESC *pNewTargetParameters) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetContainingOutput (
    /* [annotation][out] */
    _Out_  IDXGIOutput **ppOutput) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics (
    /* [annotation][out] */
    _Out_  DXGI_FRAME_STATISTICS *pStats) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetLastPresentCount (
    /* [annotation][out] */
    _Out_  UINT *pLastPresentCount) = 0;

};

EXTERN_C const IID IID_IDXGIFactory;

MIDL_INTERFACE ("7b7166ec-21c7-44ae-b21a-c9ae321ae369")
IDXGIFactory : public IDXGIObject
{
public:
  virtual HRESULT STDMETHODCALLTYPE EnumAdapters (
    /* [in] */ UINT Adapter,
    /* [annotation][out] */
    _Out_  IDXGIAdapter **ppAdapter) = 0;

  virtual HRESULT STDMETHODCALLTYPE MakeWindowAssociation (
    HWND WindowHandle,
    UINT Flags) = 0;

  virtual HRESULT STDMETHODCALLTYPE GetWindowAssociation (
    /* [annotation][out] */
    _Out_  HWND *pWindowHandle) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateSwapChain (
    /* [annotation][in] */
    _In_  IUnknown *pDevice,
    /* [annotation][in] */
    _In_  DXGI_SWAP_CHAIN_DESC *pDesc,
    /* [annotation][out] */
    _Out_  IDXGISwapChain **ppSwapChain) = 0;

  virtual HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter (
    /* [in] */ HMODULE Module,
    /* [annotation][out] */
    _Out_  IDXGIAdapter **ppAdapter) = 0;

};

  typedef struct DXGI_ADAPTER_DESC1
  {
    WCHAR Description [128];
    UINT VendorId;
    UINT DeviceId;
    UINT SubSysId;
    UINT Revision;
    SIZE_T DedicatedVideoMemory;
    SIZE_T DedicatedSystemMemory;
    SIZE_T SharedSystemMemory;
    LUID AdapterLuid;
    UINT Flags;
  } 	DXGI_ADAPTER_DESC1;

  EXTERN_C const IID IID_IDXGIAdapter1;

  MIDL_INTERFACE ("29038f61-3839-4626-91fd-086879011a05")
    IDXGIAdapter1 : public IDXGIAdapter
  {
  public:
    virtual HRESULT STDMETHODCALLTYPE GetDesc1 (
      /* [annotation][out] */
      _Out_  DXGI_ADAPTER_DESC1 *pDesc) = 0;

  };

  EXTERN_C const IID IID_IDXGIFactory1;

  MIDL_INTERFACE ("770aae78-f26f-4dba-a829-253c83d1b387")
    IDXGIFactory1 : public IDXGIFactory
  {
  public:
    virtual HRESULT STDMETHODCALLTYPE EnumAdapters1 (
      /* [in] */ UINT Adapter,
      /* [annotation][out] */
      _Out_  IDXGIAdapter1 **ppAdapter) = 0;

    virtual BOOL STDMETHODCALLTYPE IsCurrent (void) = 0;

  };

    typedef
  enum DXGI_ALPHA_MODE
  {
    DXGI_ALPHA_MODE_UNSPECIFIED = 0,
    DXGI_ALPHA_MODE_PREMULTIPLIED = 1,
    DXGI_ALPHA_MODE_STRAIGHT = 2,
    DXGI_ALPHA_MODE_IGNORE = 3,
    DXGI_ALPHA_MODE_FORCE_DWORD = 0xffffffff
  } 	DXGI_ALPHA_MODE;

  typedef struct DXGI_MODE_DESC1
  {
    UINT Width;
    UINT Height;
    DXGI_RATIONAL RefreshRate;
    DXGI_FORMAT Format;
    DXGI_MODE_SCANLINE_ORDER ScanlineOrdering;
    DXGI_MODE_SCALING Scaling;
    BOOL Stereo;
  } 	DXGI_MODE_DESC1;

  typedef
  enum DXGI_SCALING
  {
    DXGI_SCALING_STRETCH = 0,
    DXGI_SCALING_NONE = 1,
    DXGI_SCALING_ASPECT_RATIO_STRETCH = 2
  } 	DXGI_SCALING;

  typedef struct DXGI_SWAP_CHAIN_DESC1
  {
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    BOOL Stereo;
    DXGI_SAMPLE_DESC SampleDesc;
    DXGI_USAGE BufferUsage;
    UINT BufferCount;
    DXGI_SCALING Scaling;
    DXGI_SWAP_EFFECT SwapEffect;
    DXGI_ALPHA_MODE AlphaMode;
    UINT Flags;
  } 	DXGI_SWAP_CHAIN_DESC1;

  typedef struct DXGI_SWAP_CHAIN_FULLSCREEN_DESC
  {
    DXGI_RATIONAL RefreshRate;
    DXGI_MODE_SCANLINE_ORDER ScanlineOrdering;
    DXGI_MODE_SCALING Scaling;
    BOOL Windowed;
  } 	DXGI_SWAP_CHAIN_FULLSCREEN_DESC;

  typedef struct DXGI_PRESENT_PARAMETERS
  {
    UINT DirtyRectsCount;
    RECT *pDirtyRects;
    RECT *pScrollRect;
    POINT *pScrollOffset;
  } 	DXGI_PRESENT_PARAMETERS;

  EXTERN_C const IID IID_IDXGISwapChain1;

  MIDL_INTERFACE ("790a45f7-0d42-4876-983a-0a55cfe6f4aa")
    IDXGISwapChain1 : public IDXGISwapChain
  {
  public:
    virtual HRESULT STDMETHODCALLTYPE GetDesc1 (
      /* [annotation][out] */
      _Out_  DXGI_SWAP_CHAIN_DESC1 *pDesc) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetFullscreenDesc (
      /* [annotation][out] */
      _Out_  DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetHwnd (
      /* [annotation][out] */
      _Out_  HWND *pHwnd) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetCoreWindow (
      /* [annotation][in] */
      _In_  REFIID refiid,
      /* [annotation][out] */
      _Out_  void **ppUnk) = 0;

    virtual HRESULT STDMETHODCALLTYPE Present1 (
      /* [in] */ UINT SyncInterval,
      /* [in] */ UINT PresentFlags,
      /* [annotation][in] */
      _In_  const DXGI_PRESENT_PARAMETERS *pPresentParameters) = 0;

    virtual BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported (void) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetRestrictToOutput (
      /* [annotation][out] */
      _Out_  IDXGIOutput **ppRestrictToOutput) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetBackgroundColor (
      /* [annotation][in] */
      _In_  const DXGI_RGBA *pColor) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetBackgroundColor (
      /* [annotation][out] */
      _Out_  DXGI_RGBA *pColor) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetRotation (
      /* [annotation][in] */
      _In_  DXGI_MODE_ROTATION Rotation) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetRotation (
      /* [annotation][out] */
      _Out_  DXGI_MODE_ROTATION *pRotation) = 0;

  };

  EXTERN_C const IID IID_IDXGIFactory2;

  MIDL_INTERFACE ("50c83a1c-e072-4c48-87b0-3630fa36a6d0")
    IDXGIFactory2 : public IDXGIFactory1
  {
  public:
    virtual BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled (void) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd (
      /* [annotation][in] */
      _In_  IUnknown *pDevice,
      /* [annotation][in] */
      _In_  HWND hWnd,
      /* [annotation][in] */
      _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      /* [annotation][in] */
      _In_opt_  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
      /* [annotation][in] */
      _In_opt_  IDXGIOutput *pRestrictToOutput,
      /* [annotation][out] */
      _Out_  IDXGISwapChain1 **ppSwapChain) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow (
      /* [annotation][in] */
      _In_  IUnknown *pDevice,
      /* [annotation][in] */
      _In_  IUnknown *pWindow,
      /* [annotation][in] */
      _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      /* [annotation][in] */
      _In_opt_  IDXGIOutput *pRestrictToOutput,
      /* [annotation][out] */
      _Out_  IDXGISwapChain1 **ppSwapChain) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid (
      /* [annotation] */
      _In_  HANDLE hResource,
      /* [annotation] */
      _Out_  LUID *pLuid) = 0;

    virtual HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow (
      /* [annotation][in] */
      _In_  HWND WindowHandle,
      /* [annotation][in] */
      _In_  UINT wMsg,
      /* [annotation][out] */
      _Out_  DWORD *pdwCookie) = 0;

    virtual HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent (
      /* [annotation][in] */
      _In_  HANDLE hEvent,
      /* [annotation][out] */
      _Out_  DWORD *pdwCookie) = 0;

    virtual void STDMETHODCALLTYPE UnregisterStereoStatus (
      /* [annotation][in] */
      _In_  DWORD dwCookie) = 0;

    virtual HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow (
      /* [annotation][in] */
      _In_  HWND WindowHandle,
      /* [annotation][in] */
      _In_  UINT wMsg,
      /* [annotation][out] */
      _Out_  DWORD *pdwCookie) = 0;

    virtual HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent (
      /* [annotation][in] */
      _In_  HANDLE hEvent,
      /* [annotation][out] */
      _Out_  DWORD *pdwCookie) = 0;

    virtual void STDMETHODCALLTYPE UnregisterOcclusionStatus (
      /* [annotation][in] */
      _In_  DWORD dwCookie) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition (
      /* [annotation][in] */
      _In_  IUnknown *pDevice,
      /* [annotation][in] */
      _In_  const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      /* [annotation][in] */
      _In_opt_  IDXGIOutput *pRestrictToOutput,
      /* [annotation][out] */
      _Outptr_  IDXGISwapChain1 **ppSwapChain) = 0;

  };