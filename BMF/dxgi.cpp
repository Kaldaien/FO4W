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
#define _CRT_SECURE_NO_WARNINGS

#include "dxgi_backend.h"

#include "dxgi_interfaces.h"
#include <d3d11.h>

#include "nvapi.h"
#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "log.h"

#include "core.h"

#include <unordered_map>
std::unordered_map <DWORD, DWORD>         thread_sleep;
std::unordered_map <DWORD, LARGE_INTEGER> thread_perf;

extern std::wstring host_app;

extern BOOL __stdcall BMF_NvAPI_SetFramerateLimit ( const wchar_t* wszAppName,
                                                    uint32_t       limit );
extern void __stdcall BMF_NvAPI_SetAppFriendlyName (const wchar_t* wszFriendlyName);

extern int                      gpu_prio;

ID3D11Device*    g_pDevice     = nullptr;

bool bFlipMode = false;
bool bWait     = false;

struct dxgi_caps_t {
  struct {
    bool latency_control = false;
    bool enqueue_event   = false;
  } device;

  struct {
    bool flip_sequential = false;
    bool flip_discard    = false;
    bool waitable        = false;
  } present;
} dxgi_caps;

typedef BOOL (WINAPI *QueryPerformanceCounter_t)(_Out_ LARGE_INTEGER *lpPerformanceCount);
QueryPerformanceCounter_t QueryPerformanceCounter_Original = nullptr;

extern "C" {
  typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory2_t) \
    (UINT Flags, REFIID riid,  void** ppFactory);
  typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory1_t) \
    (REFIID riid,  void** ppFactory);
  typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory_t)  \
    (REFIID riid,  void** ppFactory);

  typedef HRESULT (WINAPI *D3D11CreateDeviceAndSwapChain_t)(
    _In_opt_                             IDXGIAdapter*, 
                                         D3D_DRIVER_TYPE,
                                         HMODULE,
                                         UINT, 
    _In_reads_opt_ (FeatureLevels) CONST D3D_FEATURE_LEVEL*, 
                                         UINT FeatureLevels,
                                         UINT,
    _In_opt_                       CONST DXGI_SWAP_CHAIN_DESC*,
    _Out_opt_                            IDXGISwapChain**,
    _Out_opt_                            ID3D11Device**, 
    _Out_opt_                            D3D_FEATURE_LEVEL*,
    _Out_opt_                            ID3D11DeviceContext**);


  typedef HRESULT (STDMETHODCALLTYPE *PresentSwapChain_t)(
                                         IDXGISwapChain *This,
                                         UINT            SyncInterval,
                                         UINT            Flags);

  typedef HRESULT (STDMETHODCALLTYPE *CreateSwapChain_t)(
                                         IDXGIFactory          *This,
                                   _In_  IUnknown              *pDevice,
                                   _In_  DXGI_SWAP_CHAIN_DESC  *pDesc,
                                  _Out_  IDXGISwapChain       **ppSwapChain);

  typedef HRESULT (STDMETHODCALLTYPE *SetFullscreenState_t)(
                                         IDXGISwapChain *This,
                                         BOOL            Fullscreen,
                                         IDXGIOutput    *pTarget);

  typedef HRESULT (STDMETHODCALLTYPE *ResizeBuffers_t)(
                                         IDXGISwapChain *This,
                              /* [in] */ UINT            BufferCount,
                              /* [in] */ UINT            Width,
                              /* [in] */ UINT            Height,
                              /* [in] */ DXGI_FORMAT     NewFormat,
                              /* [in] */ UINT            SwapChainFlags);


  typedef HRESULT (STDMETHODCALLTYPE *GetDesc1_t)(IDXGIAdapter1      *This,
                                           _Out_  DXGI_ADAPTER_DESC1 *pDesc);
  typedef HRESULT (STDMETHODCALLTYPE *GetDesc2_t)(IDXGIAdapter2      *This,
                                           _Out_  DXGI_ADAPTER_DESC2 *pDesc);
  typedef HRESULT (STDMETHODCALLTYPE *GetDesc_t) (IDXGIAdapter       *This,
                                           _Out_  DXGI_ADAPTER_DESC  *pDesc);

  typedef HRESULT (STDMETHODCALLTYPE *EnumAdapters_t)(
                                          IDXGIFactory  *This,
                                          UINT           Adapter,
                                    _Out_ IDXGIAdapter **ppAdapter);

  typedef HRESULT (STDMETHODCALLTYPE *EnumAdapters1_t)(
                                          IDXGIFactory1  *This,
                                          UINT            Adapter,
                                    _Out_ IDXGIAdapter1 **ppAdapter);

  volatile
    D3D11CreateDeviceAndSwapChain_t
    D3D11CreateDeviceAndSwapChain_Import = nullptr;

  PresentSwapChain_t   Present_Original            = nullptr;
  CreateSwapChain_t    CreateSwapChain_Original    = nullptr;
  SetFullscreenState_t SetFullscreenState_Original = nullptr;
  ResizeBuffers_t      ResizeBuffers_Original      = nullptr;

  GetDesc_t            GetDesc_Original            = nullptr;
  GetDesc1_t           GetDesc1_Original           = nullptr;
  GetDesc2_t           GetDesc2_Original           = nullptr;

  EnumAdapters_t       EnumAdapters_Original       = nullptr;
  EnumAdapters1_t      EnumAdapters1_Original      = nullptr;

  CreateDXGIFactory_t  CreateDXGIFactory_Import    = nullptr;
  CreateDXGIFactory1_t CreateDXGIFactory1_Import   = nullptr;
  CreateDXGIFactory2_t CreateDXGIFactory2_Import   = nullptr;

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

#define DXGI_CALL(_Ret, _Call) {                                      \
  dll_log.LogEx (true, L"  Calling original function: ");             \
  (_Ret) = (_Call);                                                   \
  dll_log.LogEx (false, L"(ret=%s)\n\n", BMF_DescribeHRESULT (_Ret)); \
}

  // Interface-based DXGI call
#define DXGI_LOG_CALL_I(_Interface,_Name,_Format)                           \
  dll_log.LogEx (true, L"[!] %s::%s (", _Interface, _Name);                 \
  dll_log.LogEx (false, _Format
  // Global DXGI call
#define DXGI_LOG_CALL(_Name,_Format)                                        \
  dll_log.LogEx (true, L"[!] %s (", _Name);                                 \
  dll_log.LogEx (false, _Format
#define DXGI_LOG_CALL_END                                                   \
  dll_log.LogEx (false, L") -- [Calling Thread: 0x%04x]\n",                 \
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

#define DXGI_STUB(_Return, _Name, _Proto, _Args)                          \
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
          L"Unable to locate symbol  %s in dxgi.dll",                     \
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

  extern "C++" {
    int
    BMF_GetDXGIFactoryInterfaceVer (const IID& riid)
    {
      if (riid == __uuidof (IDXGIFactory))
        return 0;
      if (riid == __uuidof (IDXGIFactory1))
        return 1;
      if (riid == __uuidof (IDXGIFactory2))
        return 2;
      if (riid == __uuidof (IDXGIFactory3))
        return 3;
      if (riid == __uuidof (IDXGIFactory4))
        return 4;

      //assert (false);
      return -1;
    }

    std::wstring
    BMF_GetDXGIFactoryInterfaceEx (const IID& riid)
    {
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
        wchar_t *pwszIID;

        if (SUCCEEDED (StringFromIID (riid, (LPOLESTR *)&pwszIID)))
        {
          interface_name = pwszIID;
          CoTaskMemFree (pwszIID);
        }
      }

      return interface_name;
    }

    int
    BMF_GetDXGIFactoryInterfaceVer (IUnknown *pFactory)
    {
      IUnknown *pTemp = nullptr;

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory4), (void **)&pTemp)))
      {
        dxgi_caps.device.enqueue_event    = true;
        dxgi_caps.device.latency_control  = true;
        dxgi_caps.present.flip_sequential = true;
        dxgi_caps.present.waitable        = true;
        dxgi_caps.present.flip_discard    = true;
        pTemp->Release ();
        return 4;
      }
      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory3), (void **)&pTemp)))
      {
        dxgi_caps.device.enqueue_event    = true;
        dxgi_caps.device.latency_control  = true;
        dxgi_caps.present.flip_sequential = true;
        dxgi_caps.present.waitable        = true;
        pTemp->Release ();
        return 3;
      }

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory2), (void **)&pTemp)))
      {
        dxgi_caps.device.enqueue_event    = true;
        dxgi_caps.device.latency_control  = true;
        dxgi_caps.present.flip_sequential = true;
        pTemp->Release ();
        return 2;
      }

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory1), (void **)&pTemp)))
      {
        dxgi_caps.device.latency_control  = true;
        pTemp->Release ();
        return 1;
      }

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 0;
      }

      //assert (false);
      return -1;
    }

    std::wstring
    BMF_GetDXGIFactoryInterface (IUnknown *pFactory)
    {
      int iver = BMF_GetDXGIFactoryInterfaceVer (pFactory);

      if (iver == 4)
        return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory4));

      if (iver == 3)
        return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory3));

      if (iver == 2)
        return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory2));

      if (iver == 1)
        return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory1));

      if (iver == 0)
        return BMF_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory));

      return L"{Invalid-Factory-UUID}";
    }

    int
    BMF_GetDXGIAdapterInterfaceVer (const IID& riid)
    {
      if (riid == __uuidof (IDXGIAdapter))
        return 0;
      if (riid == __uuidof (IDXGIAdapter1))
        return 1;
      if (riid == __uuidof (IDXGIAdapter2))
        return 2;
      if (riid == __uuidof (IDXGIAdapter3))
        return 3;

      //assert (false);
      return -1;
    }

    std::wstring
    BMF_GetDXGIAdapterInterfaceEx (const IID& riid)
    {
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

        if (SUCCEEDED (StringFromIID (riid, (LPOLESTR *)&pwszIID)))
        {
          interface_name = pwszIID;
          CoTaskMemFree (pwszIID);
        }
      }

      return interface_name;
    }

    int
    BMF_GetDXGIAdapterInterfaceVer (IUnknown *pAdapter)
    {
      IUnknown *pTemp = nullptr;

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter3), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 3;
      }

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter2), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 2;
      }

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter1), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 1;
      }

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter), (void **)&pTemp)))
      {
        pTemp->Release ();
        return 0;
      }

      //assert (false);
      return -1;
    }

    std::wstring
    BMF_GetDXGIAdapterInterface (IUnknown *pAdapter)
    {
      int iver = BMF_GetDXGIAdapterInterfaceVer (pAdapter);

      if (iver == 3)
        return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter3));

      if (iver == 2)
        return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter2));

      if (iver == 1)
        return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter1));

      if (iver == 0)
        return BMF_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter));

      return L"{Invalid-Adapter-UUID}";
    }
  }

#define __PTR_SIZE   sizeof LPCVOID
#define __PAGE_PRIVS PAGE_EXECUTE_READWRITE

#define DXGI_VIRTUAL_OVERRIDE(_Base,_Index,_Name,_Override,_Original,_Type) { \
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
    /*dll_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n",*/\
                  /*L##_Name, vftable [_Index],                               */\
                  /*BMF_DescribeVirtualProtectFlags (dwProtect));             */\
  }                                                                           \
}

  HRESULT
    STDMETHODCALLTYPE PresentCallback (IDXGISwapChain *This,
                                       UINT            SyncInterval,
                                       UINT            Flags)
  {
    BMF_BeginBufferSwap ();

    HRESULT hr = E_FAIL;

    if (This != NULL) {
#if 0
      static bool first_frame = true;

      if (first_frame) {
        if (bFlipMode) {
          DEVMODE devmode = { 0 };
          devmode.dmSize = sizeof DEVMODE;
          EnumDisplaySettings (nullptr, ENUM_CURRENT_SETTINGS, &devmode);

          DXGI_SWAP_CHAIN_DESC desc;
          This->GetDesc (&desc);

          devmode.dmPelsWidth  = desc.BufferDesc.Width;
          devmode.dmPelsHeight = desc.BufferDesc.Height;

          ChangeDisplaySettings (&devmode, CDS_FULLSCREEN);

          //This->ResizeTarget  (&desc);
          This->ResizeBuffers (0, devmode.dmPelsWidth, devmode.dmPelsHeight, desc.BufferDesc.Format, Flags);
        }
        first_frame = false;
      }
#endif

      IUnknown* pDev = nullptr;

      int interval = config.render.framerate.present_interval;
      int flags    = Flags;

      if (bFlipMode) {
        flags = Flags | DXGI_PRESENT_RESTART;

        if (bWait)
          flags |= DXGI_PRESENT_DO_NOT_WAIT;
      }

      if (! bFlipMode) {
        hr =
          ((HRESULT (*)(IDXGISwapChain *, UINT, UINT))Present_Original)
                       (This, interval, flags);
      } else {
        // No overlays will work if we don't do this...
        /////if (config.osd.show) {
          hr =
            ((HRESULT (*)(IDXGISwapChain *, UINT, UINT))Present_Original)
            (This, 0, flags | DXGI_PRESENT_DO_NOT_SEQUENCE | DXGI_PRESENT_DO_NOT_WAIT);
        /////}

        DXGI_PRESENT_PARAMETERS pparams;
        pparams.DirtyRectsCount = 0;
        pparams.pDirtyRects     = nullptr;
        pparams.pScrollOffset   = nullptr;
        pparams.pScrollRect     = nullptr;

        IDXGISwapChain1* pSwapChain1 = nullptr;
        This->QueryInterface (__uuidof(IDXGISwapChain1),(void **)&pSwapChain1);

        hr = pSwapChain1->Present1 (interval, flags, &pparams);

        pSwapChain1->Release ();

        if (bWait) {
          IDXGISwapChain2* pSwapChain2;
          if (SUCCEEDED (This->QueryInterface (__uuidof(IDXGISwapChain2),(void **)&pSwapChain2)))
          {
            HANDLE hWait = pSwapChain2->GetFrameLatencyWaitableObject ();

            pSwapChain2->Release ();

            WaitForSingleObjectEx ( hWait,
                                      config.render.framerate.max_delta_time,
                                        TRUE );
          }
        }
      }

      if (SUCCEEDED (This->GetDevice (__uuidof (ID3D11Device), (void **)&pDev)))
      {
        HRESULT ret = BMF_EndBufferSwap (hr, pDev);
        pDev->Release ();
        return ret;
      }
    }

    // Not a D3D11 device -- weird...
    HRESULT ret = BMF_EndBufferSwap (hr);

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE SetFullscreenState_Override (IDXGISwapChain *This,
                                                 BOOL            Fullscreen,
                                                 IDXGIOutput    *pTarget)
  {
    DXGI_LOG_CALL_I2 (L"IDXGISwapChain", L"SetFullscreenState", L"%lu, %08Xh",
                      Fullscreen, pTarget);

    HRESULT ret;
    DXGI_CALL (ret, SetFullscreenState_Original (This, Fullscreen, pTarget));

    This->ResizeBuffers ( 0,
                            0,
                              0,
                                DXGI_FORMAT_UNKNOWN,
                                  DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH );

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE ResizeBuffers_Override (IDXGISwapChain *This,
                                 /* [in] */ UINT            BufferCount,
                                 /* [in] */ UINT            Width,
                                 /* [in] */ UINT            Height,
                                 /* [in] */ DXGI_FORMAT     NewFormat,
                                 /* [in] */ UINT            SwapChainFlags)
  {
#if 0
    DXGI_LOG_CALL_I3 (L"IDXGISwapChain", L"ResizeBuffers", L"%lu,...,...,0x%08X,0x%08X",
                        BufferCount, NewFormat, SwapChainFlags);

    HRESULT ret;
    DXGI_CALL (ret, ResizeBuffers_Original (This, BufferCount, Width, Height, NewFormat, SwapChainFlags));

    return ret;
#else
    return ResizeBuffers_Original (This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
#endif
  }

  extern "C++" bool BMF_FO4_IsFullscreen (void);

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

    if (pDesc != nullptr) {
      dll_log.LogEx ( true,
        L"  SwapChain: (%lux%lu @ %4.1f Hz - Scaling: %s) - {%s}"
        L" [%lu Buffers] :: Flags=0x%04X, SwapEffect: %s\n",
        pDesc->BufferDesc.Width,
        pDesc->BufferDesc.Height,
        pDesc->BufferDesc.RefreshRate.Denominator > 0 ? 
        (float)pDesc->BufferDesc.RefreshRate.Numerator /
        (float)pDesc->BufferDesc.RefreshRate.Denominator :
        (float)pDesc->BufferDesc.RefreshRate.Numerator,
        pDesc->BufferDesc.Scaling == 0 ?
        L"Unspecified" :
        pDesc->BufferDesc.Scaling == 1 ?
        L"Centered" : L"Stretched",
        pDesc->Windowed ? L"Windowed" : L"Fullscreen",
        pDesc->BufferCount,
        pDesc->Flags,
        pDesc->SwapEffect == 0 ?
        L"Discard" :
        pDesc->SwapEffect == 1 ?
        L"Sequential" :
        pDesc->SwapEffect == 2 ?
        L"<Unknown>" :
        pDesc->SwapEffect == 3 ?
        L"Flip Sequential" :
        pDesc->SwapEffect == 4 ?
        L"Flip Discard" : L"<Unknown>");

      bFlipMode =
        (dxgi_caps.present.flip_sequential && host_app == L"Fallout4.exe");

      if (bFlipMode) {
        bFlipMode = (! BMF_FO4_IsFullscreen ());
      }

#if 0
      DEVMODE devmode = { 0 };
      devmode.dmSize  = sizeof DEVMODE;

      EnumDisplaySettings (nullptr, ENUM_CURRENT_SETTINGS, &devmode);

      if (pDesc->BufferDesc.Width  > devmode.dmPelsWidth ||
          pDesc->BufferDesc.Height > devmode.dmPelsHeight)
        bFlipMode = false;
#endif

      bFlipMode = bFlipMode && pDesc->BufferDesc.Scaling == 0;
      bWait     = bFlipMode && dxgi_caps.present.waitable;

      if (bFlipMode) {
        if (bWait)
          pDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

        // Flip Presentation Model requires 3 Buffers
        config.render.framerate.buffer_count =
          max (3, config.render.framerate.buffer_count);

        if (config.render.framerate.flip_discard &&
            dxgi_caps.present.flip_discard)
          pDesc->SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        else
          pDesc->SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
      }

      else
        pDesc->SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

      pDesc->BufferCount = config.render.framerate.buffer_count;

      // We cannot switch modes on a waitable swapchain
      if (bFlipMode && bWait) {
        pDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        //pDesc->Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
      }

      dll_log.Log ( L" >> Using %s Presentation Model  [Waitable: %s]",
                     bFlipMode ? L"Flip" : L"Traditional",
                       bWait ? L"Yes" : L"No" );
    }

    IDXGIFactory2* pFactory = nullptr;

    if (bFlipMode && SUCCEEDED (This->QueryInterface (__uuidof (IDXGIFactory2), (void **)&pFactory)))
    {
      DXGI_SWAP_CHAIN_DESC1 desc1;
      desc1.Width              = pDesc->BufferDesc.Width;
      desc1.Height             = pDesc->BufferDesc.Height;
      desc1.Format             = pDesc->BufferDesc.Format;
      desc1.Stereo             = FALSE;
      desc1.SampleDesc.Count   = pDesc->SampleDesc.Count;
      desc1.SampleDesc.Quality = pDesc->SampleDesc.Quality;
      desc1.BufferUsage        = pDesc->BufferUsage;
      desc1.BufferCount        = pDesc->BufferCount;
      desc1.Scaling            = DXGI_SCALING_STRETCH;
      desc1.SwapEffect         = pDesc->SwapEffect;
      desc1.AlphaMode          = DXGI_ALPHA_MODE_UNSPECIFIED;
      desc1.Flags              = pDesc->Flags;

      IDXGISwapChain1* pSwapChain1 = nullptr;

      DXGI_CALL (ret, pFactory->CreateSwapChainForHwnd (pDevice, pDesc->OutputWindow, &desc1, nullptr, nullptr, &pSwapChain1));

      if (SUCCEEDED (ret)) {
        pSwapChain1->QueryInterface (__uuidof (IDXGISwapChain), (void **)ppSwapChain);
        pSwapChain1->Release ();
      }

      pFactory->Release ();
    } else {
      DXGI_CALL(ret, CreateSwapChain_Original (This, pDevice, pDesc, ppSwapChain));
    }

    if ( SUCCEEDED (ret)      &&
         ppSwapChain  != NULL &&
       (*ppSwapChain) != NULL )
    {
      DXGI_VIRTUAL_OVERRIDE (ppSwapChain, 10, "IDXGISwapChain::SetFullscreenState",
                             SetFullscreenState_Override, SetFullscreenState_Original,
                             SetFullscreenState_t);

      DXGI_VIRTUAL_OVERRIDE (ppSwapChain, 13, "IDXGISwapChain::ResizeBuffers",
                             ResizeBuffers_Override, ResizeBuffers_Original,
                             ResizeBuffers_t);

      const uint32_t max_latency = config.render.framerate.pre_render_limit;

      IDXGISwapChain2* pSwapChain2 = nullptr;
      if (bFlipMode && bWait &&
          SUCCEEDED ( (*ppSwapChain)->QueryInterface (
                         __uuidof (IDXGISwapChain2),
                           (void **)&pSwapChain2 )
                    )
         )
      {
        dll_log.Log (L"Setting Swapchain Frame Latency: %lu", max_latency);
        pSwapChain2->SetMaximumFrameLatency (max_latency);

        HANDLE hWait = pSwapChain2->GetFrameLatencyWaitableObject ();

        pSwapChain2->Release ();

        WaitForSingleObjectEx ( hWait,
                                  config.render.framerate.max_delta_time,
                                    TRUE );
      }
      else
      {
        IDXGIDevice1* pDevice1 = nullptr;
        if (SUCCEEDED ( (*ppSwapChain)->GetDevice (
                           __uuidof (IDXGIDevice1),
                             (void **)&pDevice1 )
                      )
           ) {
          dll_log.Log (L"Setting Device Frame Latency: %lu", max_latency);
          pDevice1->SetMaximumFrameLatency (config.render.framerate.pre_render_limit);
          pDevice1->Release ();
        }
      }

      ID3D11Device *pDev;

      if (SUCCEEDED ( pDevice->QueryInterface ( __uuidof (ID3D11Device),
                                                  (void **)&pDev )
                    )
         )
      {
        g_pDevice = pDev;

        DXGI_VIRTUAL_OVERRIDE (ppSwapChain, 8, "IDXGISwapChain::Present",
                               PresentCallback, Present_Original,
                               PresentSwapChain_t);

        pDev->Release ();
      }
    }

    return ret;
  }

  HRESULT
  WINAPI
  D3D11CreateDeviceAndSwapChain_Detour (IDXGIAdapter          *pAdapter,
                                        D3D_DRIVER_TYPE        DriverType,
                                        HMODULE                Software,
                                        UINT                   Flags,
   _In_reads_opt_ (FeatureLevels) CONST D3D_FEATURE_LEVEL     *pFeatureLevels,
                                        UINT                   FeatureLevels,
                                        UINT                   SDKVersion,
   _In_opt_                       CONST DXGI_SWAP_CHAIN_DESC  *pSwapChainDesc,
   _Out_opt_                            IDXGISwapChain       **ppSwapChain,
   _Out_opt_                            ID3D11Device         **ppDevice,
   _Out_opt_                            D3D_FEATURE_LEVEL     *pFeatureLevel,
   _Out_opt_                            ID3D11DeviceContext  **ppImmediateContext)
  {
    DXGI_LOG_CALL_0 (L"D3D11CreateDeviceAndSwapChain");

    dll_log.LogEx (true, L" Preferred Feature Level(s): <%u> - ", FeatureLevels);

    for (UINT i = 0; i < FeatureLevels; i++) {
      switch (pFeatureLevels [i])
      {
      case D3D_FEATURE_LEVEL_9_1:
        dll_log.LogEx (false, L" 9_1");
        break;
      case D3D_FEATURE_LEVEL_9_2:
        dll_log.LogEx (false, L" 9_2");
        break;
      case D3D_FEATURE_LEVEL_9_3:
        dll_log.LogEx (false, L" 9_3");
        break;
      case D3D_FEATURE_LEVEL_10_0:
        dll_log.LogEx (false, L" 10_0");
        break;
      case D3D_FEATURE_LEVEL_10_1:
        dll_log.LogEx (false, L" 10_1");
        break;
      case D3D_FEATURE_LEVEL_11_0:
        dll_log.LogEx (false, L" 11_0");
        break;
      case D3D_FEATURE_LEVEL_11_1:
        dll_log.LogEx (false, L" 11_1");
        break;
        //case D3D_FEATURE_LEVEL_12_0:
        //dll_log.LogEx (false, L" 12_0");
        //break;
        //case D3D_FEATURE_LEVEL_12_1:
        //dll_log.LogEx (false, L" 12_1");
        //break;
      }
    }

    dll_log.LogEx (false, L"\n");

    if (pSwapChainDesc != nullptr) {
      dll_log.LogEx ( true,
                        L"  SwapChain: (%lux%lu@%lu Hz - Scaling: %s) - "
                        L"[%lu Buffers] :: Flags=0x%04X, SwapEffect: %s\n",
                          pSwapChainDesc->BufferDesc.Width,
                          pSwapChainDesc->BufferDesc.Height,
                          pSwapChainDesc->BufferDesc.RefreshRate.Numerator / 
                          pSwapChainDesc->BufferDesc.RefreshRate.Denominator,
                          pSwapChainDesc->BufferDesc.Scaling == 0 ?
                            L"Unspecified" :
                          pSwapChainDesc->BufferDesc.Scaling == 1 ?
                            L"Centered" : L"Stretched",
                          pSwapChainDesc->BufferCount,
                          pSwapChainDesc->Flags,
                          pSwapChainDesc->SwapEffect == 0 ?
                            L"Discard" :
                          pSwapChainDesc->SwapEffect == 1 ?
                            L"Sequential" :
                          pSwapChainDesc->SwapEffect == 2 ?
                            L"<Unknown>" :
                          pSwapChainDesc->SwapEffect == 3 ?
                            L"Flip Sequential" :
                          pSwapChainDesc->SwapEffect == 4 ?
                            L"Flip Discard" : L"<Unknown>");
    }

    HRESULT res =
      D3D11CreateDeviceAndSwapChain_Import (pAdapter,
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
    {
      dll_log.Log (L" >> Device = 0x%08Xh", *ppDevice);
    }

    return res;
  }

  HRESULT
  STDMETHODCALLTYPE GetDesc2_Override (IDXGIAdapter2      *This,
                                _Out_  DXGI_ADAPTER_DESC2 *pDesc)
  {
    std::wstring iname = BMF_GetDXGIAdapterInterface (This);

    DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc2", L"%08Xh, %08Xh", This, pDesc);

    HRESULT ret;
    DXGI_CALL (ret, GetDesc2_Original (This, pDesc));

    //// OVERRIDE VRAM NUMBER
    if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
      dll_log.LogEx ( true,
        L" <> GetDesc2_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );

      DXGI_ADAPTER_DESC* match =
        bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

      if (match != NULL) {
        dll_log.LogEx (false, L"Success! (%s)\n", match->Description);
        pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
      }
      else
        dll_log.LogEx (false, L"Failure! (No Match Found)\n");
    }

    dll_log.LogEx (false, L"\n");

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE GetDesc1_Override (IDXGIAdapter1      *This,
                                _Out_  DXGI_ADAPTER_DESC1 *pDesc)
  {
    std::wstring iname = BMF_GetDXGIAdapterInterface (This);

    DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc1", L"%08Xh, %08Xh", This, pDesc);

    HRESULT ret;
    DXGI_CALL (ret, GetDesc1_Original (This, pDesc));

    //// OVERRIDE VRAM NUMBER
    if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
      dll_log.LogEx ( true,
        L" <> GetDesc1_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );

      DXGI_ADAPTER_DESC* match =
        bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

      if (match != NULL) {
        dll_log.LogEx (false, L"Success! (%s)\n", match->Description);
        pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
      }
      else
        dll_log.LogEx (false, L"Failure! (No Match Found)\n");
    }

    dll_log.LogEx (false, L"\n");

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE GetDesc_Override (IDXGIAdapter      *This,
                               _Out_  DXGI_ADAPTER_DESC *pDesc)
  {
    std::wstring iname = BMF_GetDXGIAdapterInterface (This);

    DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc",L"%08Xh, %08Xh", This, pDesc);

    HRESULT ret;
    DXGI_CALL (ret, GetDesc_Original (This, pDesc));

    //// OVERRIDE VRAM NUMBER
    if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0) {
      dll_log.LogEx ( true,
        L" <> GetDesc_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );

      DXGI_ADAPTER_DESC* match =
        bmf::NVAPI::FindGPUByDXGIName (pDesc->Description);

      if (match != NULL) {
        dll_log.LogEx (false, L"Success! (%s)\n", match->Description);
        pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
      }
      else
        dll_log.LogEx (false, L"Failure! (No Match Found)\n");
    }

    dll_log.LogEx (false, L"\n");

    return ret;
  }

  typedef enum bmfUndesirableVendors {
    Microsoft = 0x1414,
    Intel     = 0x8086
  } Vendors;

  HRESULT
  STDMETHODCALLTYPE EnumAdapters_Common (IDXGIFactory       *This,
                                         UINT                Adapter,
                                _Inout_  IDXGIAdapter      **ppAdapter,
                                         EnumAdapters_t      pFunc)
  {
    DXGI_ADAPTER_DESC desc;

    bool silent = dll_log.silent;
    dll_log.silent = true;
    {
      // Don't log this call
      (*ppAdapter)->GetDesc (&desc);
    }
    dll_log.silent = false;

    int iver = BMF_GetDXGIAdapterInterfaceVer (*ppAdapter);

    // Only do this for NVIDIA SLI GPUs on Windows 10 (DXGI 1.4)
    if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0 && iver >= 3) {
      if (! GetDesc_Original) {
        DXGI_VIRTUAL_OVERRIDE (ppAdapter, 8, "(*ppAdapter)->GetDesc",
          GetDesc_Override, GetDesc_Original, GetDesc_t);
      }

      if (! GetDesc1_Original) {
        IDXGIAdapter1* pAdapter1;
        (*ppAdapter)->QueryInterface (__uuidof (IDXGIAdapter1), (void **)&pAdapter1);

        DXGI_VIRTUAL_OVERRIDE (&pAdapter1, 10, "(pAdapter1)->GetDesc1",
          GetDesc1_Override, GetDesc1_Original, GetDesc1_t);

        (pAdapter1)->Release ();
      }

      if (! GetDesc2_Original) {
        IDXGIAdapter2* pAdapter2;
        (*ppAdapter)->QueryInterface (__uuidof (IDXGIAdapter2), (void **)&pAdapter2);

        DXGI_VIRTUAL_OVERRIDE (ppAdapter, 11, "(*pAdapter2)->GetDesc2",
          GetDesc2_Override, GetDesc2_Original, GetDesc2_t);

        (pAdapter2)->Release ();
      }
    }

    // Logic to skip Intel and Microsoft adapters and return only AMD / NV
    //if (lstrlenW (pDesc->Description)) {
    if (true) {
      if (! lstrlenW (desc.Description))
        dll_log.LogEx (false, L" >> Assertion filed: Zero-length adapter name!\n");

#ifdef SKIP_INTEL
      if ((desc.VendorId == Microsoft || desc.VendorId == Intel) && Adapter == 0) {
#else
      if (false) {
#endif
        // We need to release the reference we were just handed before
        //   skipping it.
        (*ppAdapter)->Release ();

        dll_log.LogEx (false,
          L" >> (Host Application Tried To Enum Intel or Microsoft Adapter "
          L"as Adapter 0) -- Skipping Adapter '%s' <<\n\n", desc.Description);

        return (pFunc (This, Adapter + 1, ppAdapter));
      }
      else {
        // Only do this for NVIDIA SLI GPUs on Windows 10 (DXGI 1.4)
        if (nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0 && iver >= 3) {
          DXGI_ADAPTER_DESC* match =
            bmf::NVAPI::FindGPUByDXGIName (desc.Description);

          if (match != NULL &&
            desc.DedicatedVideoMemory > match->DedicatedVideoMemory) {
// This creates problems in 32-bit environments...
#ifdef _WIN64
            dll_log.Log (
              L"   # SLI Detected (Corrected Memory Total: %llu MiB -- "
              L"Original: %llu MiB)",
              match->DedicatedVideoMemory >> 20ULL,
              desc.DedicatedVideoMemory   >> 20ULL);
#endif
          }
        }

        // IDXGIAdapter3 = DXGI 1.4 (Windows 10+)
        if (iver >= 3)
          BMF_StartDXGI_1_4_BudgetThread (ppAdapter);

        dll_log.LogEx (false, L"\n");
      }

      dll_log.LogEx(true,L"   @ Returning Adapter %lu: '%s' (LUID: %08X:%08X)",
        Adapter,
          desc.Description,
            desc.AdapterLuid.HighPart,
              desc.AdapterLuid.LowPart );

      //
      // Windows 8 has a software implementation, which we can detect.
      //
      IDXGIAdapter1* pAdapter1;
      HRESULT hr =
        (*ppAdapter)->QueryInterface (
          __uuidof (IDXGIAdapter1), (void **)&pAdapter1
        );

      if (SUCCEEDED (hr)) {
        bool silence = dll_log.silent;
        dll_log.silent = true; // Temporarily disable logging

        DXGI_ADAPTER_DESC1 desc1;
        if (SUCCEEDED (pAdapter1->GetDesc1 (&desc1))) {
          dll_log.silent = silence; // Restore logging
#define DXGI_ADAPTER_FLAG_REMOTE   0x1
#define DXGI_ADAPTER_FLAG_SOFTWARE 0x2
          if (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            dll_log.LogEx (false, L" <Software>");
          else
            dll_log.LogEx (false, L" <Hardware>");
          if (desc1.Flags & DXGI_ADAPTER_FLAG_REMOTE)
            dll_log.LogEx (false, L" [Remote]");
        }
        dll_log.silent = silence; // Restore logging
        pAdapter1->Release ();
      }

      dll_log.LogEx (false, L"\n\n");
    }

    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE EnumAdapters1_Override (IDXGIFactory1  *This,
                                            UINT            Adapter,
                                     _Out_  IDXGIAdapter1 **ppAdapter)
  {
    std::wstring iname = BMF_GetDXGIFactoryInterface    (This);

    DXGI_LOG_CALL_I3 (iname.c_str (), L"EnumAdapters1", L"%08Xh, %u, %08Xh",
      This, Adapter, ppAdapter);

    HRESULT ret;
    DXGI_CALL (ret, EnumAdapters1_Original (This,Adapter,ppAdapter));

    if (SUCCEEDED (ret) && ppAdapter != nullptr && (*ppAdapter) != nullptr) {
      return EnumAdapters_Common (This, Adapter, (IDXGIAdapter **)ppAdapter,
                                  (EnumAdapters_t)EnumAdapters1_Override);
    }

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE EnumAdapters_Override (IDXGIFactory  *This,
                                           UINT           Adapter,
                                    _Out_  IDXGIAdapter **ppAdapter)
  {
    std::wstring iname = BMF_GetDXGIFactoryInterface    (This);

    DXGI_LOG_CALL_I3 (iname.c_str (), L"EnumAdapters", L"%08Xh, %u, %08Xh",
      This, Adapter, ppAdapter);

    HRESULT ret;
    DXGI_CALL (ret, EnumAdapters_Original (This, Adapter, ppAdapter));

    if (SUCCEEDED (ret) && ppAdapter != nullptr && (*ppAdapter) != nullptr) {
      return EnumAdapters_Common (This, Adapter, ppAdapter,
                                  (EnumAdapters_t)EnumAdapters_Override);
    }

    return ret;
  }

  __declspec (nothrow)
    HRESULT
    STDMETHODCALLTYPE CreateDXGIFactory (REFIID   riid,
                                   _Out_ void   **ppFactory)
  {
    WaitForInit ();

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx  (riid);
    int          iver  = BMF_GetDXGIFactoryInterfaceVer (riid);

    DXGI_LOG_CALL_2 (L"CreateDXGIFactory", L"%s, %08Xh",
      iname.c_str (), ppFactory);

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory_Import (riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7, "IDXGIFactory::EnumAdapters",
                           EnumAdapters_Override, EnumAdapters_Original,
                           EnumAdapters_t);

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory::CreateSwapChain",
                           CreateSwapChain_Override, CreateSwapChain_Original,
                           CreateSwapChain_t);

    // DXGI 1.1+
    if (iver > 0) {
      DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory::EnumAdapters1",
                             EnumAdapters1_Override, EnumAdapters1_Original,
                             EnumAdapters1_t);
    }

    return ret;
  }

  __declspec (nothrow)
    HRESULT
    STDMETHODCALLTYPE CreateDXGIFactory1 (REFIID   riid,
                                    _Out_ void   **ppFactory)
  {
    WaitForInit ();

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx  (riid);
    int          iver  = BMF_GetDXGIFactoryInterfaceVer (riid);

    DXGI_LOG_CALL_2 (L"CreateDXGIFactory1", L"%s, %08Xh",
      iname.c_str (), ppFactory);

    // Windows Vista does not have this function -- wrap it with CreateDXGIFactory
    if (CreateDXGIFactory1_Import == nullptr) {
      dll_log.Log (L"  >> Falling back to CreateDXGIFactory on Vista...\n");
      return CreateDXGIFactory (riid, ppFactory);
    }

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory1_Import (riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7,  "IDXGIFactory1::EnumAdapters",
                           EnumAdapters_Override,  EnumAdapters_Original,
                           EnumAdapters_t);
    DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory1::CreateSwapChain",
                           CreateSwapChain_Override, CreateSwapChain_Original,
                           CreateSwapChain_t);

    // DXGI 1.1+
    if (iver > 0) {
      DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory1::EnumAdapters1",
                             EnumAdapters1_Override, EnumAdapters1_Original,
                             EnumAdapters1_t);
    }

    return ret;
  }

  __declspec (nothrow)
    HRESULT
    STDMETHODCALLTYPE CreateDXGIFactory2 (UINT     Flags,
                                          REFIID   riid,
                                    _Out_ void   **ppFactory)
  {
    WaitForInit ();

    std::wstring iname = BMF_GetDXGIFactoryInterfaceEx  (riid);
    int          iver  = BMF_GetDXGIFactoryInterfaceVer (riid);

    DXGI_LOG_CALL_3 (L"CreateDXGIFactory2", L"0x%04X, %s, %08Xh",
      Flags, iname.c_str (), ppFactory);

    // Windows 7 does not have this function -- wrap it with CreateDXGIFactory1
    if (CreateDXGIFactory2_Import == nullptr) {
      dll_log.Log (L"  >> Falling back to CreateDXGIFactory1 on Vista/7...\n");
      return CreateDXGIFactory1 (riid, ppFactory);
    }

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory2_Import (Flags, riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE (ppFactory, 7, "IDXGIFactory2::EnumAdapters",
                           EnumAdapters_Override, EnumAdapters_Original,
                           EnumAdapters_t);
    DXGI_VIRTUAL_OVERRIDE (ppFactory, 10, "IDXGIFactory2::CreateSwapChain",
                           CreateSwapChain_Override, CreateSwapChain_Original,
                           CreateSwapChain_t);

    // DXGI 1.1+
    if (iver > 0) {
      DXGI_VIRTUAL_OVERRIDE (ppFactory, 12, "IDXGIFactory2::EnumAdapters1",
                             EnumAdapters1_Override, EnumAdapters1_Original,
                             EnumAdapters1_t);
    }

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

  HRESULT
  STDMETHODCALLTYPE DXGIDumpJournal (void)
  {
    DXGI_LOG_CALL_0 (L"DXGIDumpJournal");

    return E_NOTIMPL;
  }

  HRESULT
  STDMETHODCALLTYPE DXGIReportAdapterConfiguration (void)
  {
    DXGI_LOG_CALL_0 (L"DXGIReportAdapterConfiguration");

    return E_NOTIMPL;
  }
}


LPVOID pfnD3D11CreateDeviceAndSwapChain = nullptr;
LPVOID pfnQueryPerformanceCounter       = nullptr;
LPVOID pfnSleep                         = nullptr;

static __declspec (thread) int           last_sleep     =   1;
static __declspec (thread) LARGE_INTEGER last_perfCount = { 0 };


typedef MMRESULT (WINAPI *timeBeginPeriod_t)(UINT uPeriod);
timeBeginPeriod_t timeBeginPeriod_Original = nullptr;

MMRESULT
WINAPI
timeBeginPeriod_Detour (UINT uPeriod)
{
  dll_log.Log ( L"[!] timeBeginPeriod (%d) - "
    L"[Calling Thread: 0x%04x]",
    uPeriod,
    GetCurrentThreadId () );


  return timeBeginPeriod_Original (uPeriod);
}

// We need to use TLS for this, I would imagine...


typedef void (WINAPI *Sleep_t)(DWORD dwMilliseconds);
Sleep_t Sleep_Original = nullptr;

void
WINAPI
Sleep_Detour (DWORD dwMilliseconds)
{
  thread_sleep [GetCurrentThreadId ()] = dwMilliseconds;
  //last_sleep = dwMilliseconds;

  //if (config.framerate.yield_processor && dwMilliseconds == 0)
  //if (dwMilliseconds == 0)
    //YieldProcessor ();

  if (dwMilliseconds != 0) {// || config.framerate.allow_fake_sleep) {
    Sleep_Original (dwMilliseconds);
  }
}

BOOL
WINAPI
QueryPerformanceCounter_Detour (_Out_ LARGE_INTEGER *lpPerformanceCount)
{
  static bool period = false;
  if (! period) {
    timeBeginPeriod (1);
    period = true;
  }

  BOOL ret = QueryPerformanceCounter_Original (lpPerformanceCount);

  if (thread_sleep [GetCurrentThreadId ()] > 0 /*|| (! (tzf::FrameRateFix::fullscreen ||
    tzf::FrameRateFix::driver_limit_setup ||
    config.framerate.allow_windowed_mode))*/) {
    memcpy (&last_perfCount, lpPerformanceCount, sizeof (LARGE_INTEGER) );
    thread_perf [GetCurrentThreadId ()] = last_perfCount;
    return ret;
  } else {
    static LARGE_INTEGER freq = { 0 };
    if (freq.QuadPart == 0ULL)
      QueryPerformanceFrequency (&freq);

    const float fudge_factor = config.render.framerate.fudge_factor * freq.QuadPart;

    thread_sleep [GetCurrentThreadId ()] = 1;

    last_perfCount = thread_perf [GetCurrentThreadId ()];

    // Mess with the numbers slightly to prevent hiccups
    lpPerformanceCount->QuadPart += (lpPerformanceCount->QuadPart - last_perfCount.QuadPart) * 
      fudge_factor/* * freq.QuadPart*/;
    memcpy (&last_perfCount, lpPerformanceCount, sizeof (LARGE_INTEGER) );
    thread_perf [GetCurrentThreadId ()] = last_perfCount;

    return ret;
  }
}




#ifdef _MSC_VER
# include <unordered_map>
//# include <hash_map>
//# define hash_map stdext::hash_map
#else
# include <hash_map.h>
#endif

#include <locale> // tolower (...)

template <typename T>
class eTB_VarStub;// : public eTB_Variable

class eTB_Variable;
class eTB_Command;

class eTB_CommandResult
{
public:
  eTB_CommandResult (       std::string   word,
    std::string   arguments = "",
    std::string   result    = "",
    int           status = false,
    const eTB_Variable* var    = NULL,
    const eTB_Command*  cmd    = NULL ) : word_   (word),
    args_   (arguments),
    result_ (result) {
    var_    = var;
    cmd_    = cmd;
    status_ = status;
  }

  std::string         getWord     (void) const { return word_;   }
  std::string         getArgs     (void) const { return args_;   }
  std::string         getResult   (void) const { return result_; }

  const eTB_Variable* getVariable (void) const { return var_;    }
  const eTB_Command*  getCommand  (void) const { return cmd_;    }

  int                 getStatus   (void) const { return status_; }

protected:

private:
  const eTB_Variable* var_;
  const eTB_Command*  cmd_;
  std::string   word_;
  std::string   args_;
  std::string   result_;
  int           status_;
};

class eTB_Command {
public:
  virtual eTB_CommandResult execute (const char* szArgs) = 0;

  virtual const char* getHelp            (void) { return "No Help Available"; }

  virtual int         getNumArgs         (void) { return 0; }
  virtual int         getNumOptionalArgs (void) { return 0; }
  virtual int         getNumRequiredArgs (void) {
    return getNumArgs () - getNumOptionalArgs ();
  }

protected:
private:
};

class eTB_Variable
{
  friend class eTB_iVariableListener;
public:
  enum VariableType {
    Float,
    Double,
    Boolean,
    Byte,
    Short,
    Int,
    LongInt,
    String,

    NUM_VAR_TYPES_,

    Unknown
  } VariableTypes;

  virtual VariableType  getType        (void) const = 0;
  virtual std::string   getValueString (void) const = 0;

protected:
  VariableType type_;
};

class eTB_iVariableListener
{
public:
  virtual bool OnVarChange (eTB_Variable* var, void* val = NULL) = 0;
protected:
};

template <typename T>
class eTB_VarStub : public eTB_Variable
{
  friend class eTB_iVariableListener;
public:
  eTB_VarStub (void) : type_     (Unknown),
    var_      (NULL),
    listener_ (NULL)     { };

  eTB_VarStub ( T*                     var,
    eTB_iVariableListener* pListener = NULL );

  eTB_Variable::VariableType getType (void) const
  {
    return type_;
  }

  virtual std::string getValueString (void) const { return "(null)"; }

  const T& getValue (void) const { return *var_; }
  void     setValue (T& val)     {
    if (listener_ != NULL)
      listener_->OnVarChange (this, &val);
    else
      *var_ = val;
  }

  /// NOTE: Avoid doing this, as much as possible...
  T* getValuePtr (void) { return var_; }

  typedef  T _Tp;

protected:
  typename eTB_VarStub::_Tp* var_;

private:
  eTB_iVariableListener*     listener_;
};

#define eTB_CaseAdjust(ch,lower) ((lower) ? ::tolower ((int)(ch)) : (ch))

// Hash function for UTF8 strings
template < class _Kty, class _Pr = std::less <_Kty> >
class str_hash_compare
{
public:
  typedef typename _Kty::value_type value_type;
  typedef typename _Kty::size_type  size_type;  /* Was originally size_t ... */

  enum
  {
    bucket_size = 4,
    min_buckets = 8
  };

  str_hash_compare (void)      : comp ()      { };
  str_hash_compare (_Pr _Pred) : comp (_Pred) { };

  size_type operator() (const _Kty& _Keyval) const;
  bool      operator() (const _Kty& _Keyval1, const _Kty& _Keyval2) const;

  size_type hash_string (const _Kty& _Keyval) const;

private:
  _Pr comp;
};

typedef std::pair <std::string, eTB_Command*>  eTB_CommandRecord;
typedef std::pair <std::string, eTB_Variable*> eTB_VariableRecord;


class eTB_CommandProcessor
{
public:
  eTB_CommandProcessor (void);

  virtual ~eTB_CommandProcessor (void)
  {
  }

  eTB_Command* FindCommand   (const char* szCommand) const;

  const eTB_Command* AddCommand    ( const char*  szCommand,
    eTB_Command* pCommand );
  bool               RemoveCommand ( const char* szCommand );


  const eTB_Variable* FindVariable  (const char* szVariable) const;

  const eTB_Variable* AddVariable    ( const char*   szVariable,
    eTB_Variable* pVariable  );
  bool                RemoveVariable ( const char*   szVariable );


  eTB_CommandResult ProcessCommandLine (const char* szCommandLine);


protected:
private:
  std::unordered_map < std::string, eTB_Command*,
    str_hash_compare <std::string> > commands_;
  std::unordered_map < std::string, eTB_Variable*,
    str_hash_compare <std::string> > variables_;
};


eTB_CommandProcessor command;



#include <string>
#include "steam_api.h"

//#include "log.h"
//#include "config.h"

#include <mmsystem.h>
#pragma comment (lib, "winmm.lib")

#include <comdef.h>

struct window_t {
  DWORD proc_id;
  HWND  root;
};

BOOL
CALLBACK
BMF_EnumWindows (HWND hWnd, LPARAM lParam)
{
  window_t& win = *(window_t*)lParam;

  DWORD proc_id = 0;

  GetWindowThreadProcessId (hWnd, &proc_id);

  if (win.proc_id != proc_id) {
    if (GetWindow (hWnd, GW_OWNER) != (HWND)nullptr ||
      GetWindowTextLength (hWnd) < 30             ||
      (! IsWindowVisible     (hWnd)))
      return TRUE;
  }

  win.root = hWnd;
  return FALSE;
}

HWND
BMF_FindRootWindow (DWORD proc_id)
{
  window_t win;

  win.proc_id  = proc_id;
  win.root     = 0;

  EnumWindows (BMF_EnumWindows, (LPARAM)&win);

  return win.root;
}

class BMF_InputHooker
{
private:
  HANDLE                  hMsgPump;
  struct hooks_t {
    HHOOK                 keyboard;
    HHOOK                 mouse;
  } hooks;

  static BMF_InputHooker* pInputHook;

  static char                text [16384];

  static BYTE keys_ [256];
  static bool visible;

  static bool command_issued;
  static std::string result_str;

  struct command_history_t {
    std::vector <std::string> history;
    int_fast32_t              idx     = -1;
  } static commands;

protected:
  BMF_InputHooker (void) { }

public:
  static BMF_InputHooker* getInstance (void)
  {
    if (pInputHook == NULL)
      pInputHook = new BMF_InputHooker ();

    return pInputHook;
  }

  void Start (void)
  {
    hMsgPump =
      CreateThread ( NULL,
        NULL,
        BMF_InputHooker::MessagePump,
        &hooks,
        NULL,
        NULL );
  }

  void End (void)
  {
    TerminateThread     (hMsgPump, 0);
    UnhookWindowsHookEx (hooks.keyboard);
    UnhookWindowsHookEx (hooks.mouse);
  }

  HANDLE GetThread (void)
  {
    return hMsgPump;
  }

  static DWORD
    WINAPI
    MessagePump (LPVOID hook_ptr)
  {
    hooks_t* pHooks = (hooks_t *)hook_ptr;

    ZeroMemory (text, 16384);

    text [0] = '>';

    extern    HMODULE hModSelf;

    HWND  hWndForeground;
    DWORD dwThreadId;

    int hits = 0;

    DWORD dwTime = timeGetTime ();

    while (true) {
      // Spin until the game has a render window setup
      if (! g_pDevice) {
        Sleep (83);
        continue;
      }

      hWndForeground = GetForegroundWindow ();

      if ((! hWndForeground) ||
        hWndForeground != BMF_FindRootWindow (GetCurrentProcessId ())) {
        Sleep (83);
        continue;
      }

      dwThreadId = GetWindowThreadProcessId (hWndForeground, nullptr);

      break;
    }

    dll_log.Log ( L"  # Found window in %03.01f seconds, "
      L"installing keyboard hook...",
      (float)(timeGetTime () - dwTime) / 1000.0f );

    dwTime = timeGetTime ();
    hits   = 1;

    while (! (pHooks->keyboard = SetWindowsHookEx ( WH_KEYBOARD,
      KeyboardProc,
      hModSelf,
      dwThreadId ))) {
      _com_error err (HRESULT_FROM_WIN32 (GetLastError ()));

      dll_log.Log ( L"  @ SetWindowsHookEx failed: 0x%04X (%s)",
        err.WCode (), err.ErrorMessage () );

      ++hits;

      if (hits >= 5) {
        dll_log.Log ( L"  * Failed to install keyboard hook after %lu tries... "
          L"bailing out!",
          hits );
        return 0;
      }

      Sleep (1);
    }

    while (! (pHooks->mouse = SetWindowsHookEx ( WH_MOUSE,
      MouseProc,
      hModSelf,
      dwThreadId ))) {
      _com_error err (HRESULT_FROM_WIN32 (GetLastError ()));

      dll_log.Log ( L"  @ SetWindowsHookEx failed: 0x%04X (%s)",
        err.WCode (), err.ErrorMessage () );

      ++hits;

      if (hits >= 5) {
        dll_log.Log ( L"  * Failed to install mouse hook after %lu tries... "
          L"bailing out!",
          hits );
        return 0;
      }

      Sleep (1);
    }

    dll_log.Log ( L"  * Installed keyboard hook for command console... "
      L"%lu %s (%lu ms!)",
      hits,
      hits > 1 ? L"tries" : L"try",
      timeGetTime () - dwTime );

    DWORD last_time = timeGetTime ();
    bool  carret    = true;

    //193 - 199

    while (true)
    {
      std::string output;

      if (visible) {
        output += text;

        // Blink the Carret
        if (timeGetTime () - last_time > 333) {
          carret = ! carret;

          last_time = timeGetTime ();
        }

        if (carret)
          output += "-";

        // Show Command Results
        if (command_issued) {
          output += "\n";
          output += result_str;
        }
      }

      extern BOOL BMF_UpdateOSD (LPCSTR lpText, LPVOID pMapAddr, LPCSTR lpAppName);
      BMF_UpdateOSD (output.c_str (), nullptr, "BMF Console");

      Sleep (16);
    }

    return 0;
  }

  static LRESULT
    CALLBACK
    MouseProc (int nCode, WPARAM wParam, LPARAM lParam)
  {
    MOUSEHOOKSTRUCT* pmh = (MOUSEHOOKSTRUCT *)lParam;

#if 0
    static bool fudging = false;

    if (tzf::RenderFix::pDevice != nullptr && (! fudging)) {
      //GetCursorPos (&pmh->pt);

      extern POINT CalcCursorPos (LPPOINT pPos);
      extern POINT real_cursor;

      POINT adjusted_cursor;
      adjusted_cursor.x = pmh->pt.x;
      adjusted_cursor.y = pmh->pt.y;

      real_cursor = CalcCursorPos (&adjusted_cursor);

      pmh->pt.x = adjusted_cursor.x;
      pmh->pt.y = adjusted_cursor.y;

      //SetCursorPos (adjusted_cursor.x, adjusted_cursor.y);

      //tzf::RenderFix::pDevice->SetCursorPosition (adjusted_cursor.x,
      //adjusted_cursor.y,
      //0);//D3DCURSOR_IMMEDIATE_UPDATE);
      fudging = true;
      SendMessage (pmh->hwnd, WM_MOUSEMOVE, 0, LOWORD (pmh->pt.x) | HIWORD (pmh->pt.y));
      return 1;
    }
    else {
      fudging = false;
    }
#endif

    return CallNextHookEx (BMF_InputHooker::getInstance ()->hooks.mouse, nCode, wParam, lParam);
  }

  static LRESULT
    CALLBACK
    KeyboardProc (int nCode, WPARAM wParam, LPARAM lParam)
  {
    if (nCode >= 0) {
      if (true) {
        DWORD   vkCode   = LOWORD (wParam);
        DWORD   scanCode = HIWORD (lParam) & 0x7F;
        bool    repeated = LOWORD (lParam);
        bool    keyDown  = ! (lParam & 0x80000000);

        if (visible && vkCode == VK_BACK) {
          if (keyDown) {
            int len = strlen (text);
            len--;
            if (len < 1)
              len = 1;
            text [len] = '\0';
          }
        } else if ((vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT)) {
          if (keyDown)
            keys_ [VK_SHIFT] = 0x81;
          else
            keys_ [VK_SHIFT] = 0x00;
        }
        else if ((!repeated) && vkCode == VK_CAPITAL) {
          if (keyDown) {
            if (keys_ [VK_CAPITAL] == 0x00)
              keys_ [VK_CAPITAL] = 0x81;
            else
              keys_ [VK_CAPITAL] = 0x00;
          }
        }
        else if ((vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL)) {
          if (keyDown)
            keys_ [VK_CONTROL] = 0x81;
          else
            keys_ [VK_CONTROL] = 0x00;
        }
        else if ((vkCode == VK_UP) || (vkCode == VK_DOWN)) {
          if (keyDown && visible) {
            if (vkCode == VK_UP)
              commands.idx--;
            else
              commands.idx++;

            // Clamp the index
            if (commands.idx < 0)
              commands.idx = 0;
            else if (commands.idx >= commands.history.size ())
              commands.idx = commands.history.size () - 1;

            if (commands.history.size ())
              strcpy (&text [1], commands.history [commands.idx].c_str ());
            command_issued = false;
          }
        }
        else if (visible && vkCode == VK_RETURN) {
          if (keyDown && LOWORD (lParam) < 2) {
            int len = strlen (text+1);
            // Don't process empty or pure whitespace command lines
            if (len > 0 && strspn (text+1, " ") != len) {
              eTB_CommandResult result = command.ProcessCommandLine (text+1);

              if (result.getStatus ()) {
                // Don't repeat the same command over and over
                if (commands.history.size () == 0 ||
                  commands.history.back () != &text [1]) {
                  commands.history.push_back (&text [1]);
                }

                commands.idx = commands.history.size ();

                text [1] = '\0';

                command_issued = true;
              }
              else {
                command_issued = false;
              }

              result_str     = result.getWord () + std::string (" ")   +
                result.getArgs () + std::string (":  ") +
                result.getResult ();
            }
          }
        }
        else if (keyDown) {
          bool new_press = keys_ [vkCode] != 0x81;

          keys_ [vkCode] = 0x81;

          if (keys_ [VK_CONTROL] && keys_ [VK_SHIFT] && keys_ [VK_TAB] && new_press)
            visible = ! visible;

            // This will pause/unpause the game
            BMF::SteamAPI::SetOverlayState (visible);

          if (visible) {
            char key_str [2];
            key_str [1] = '\0';

            if (1 == ToAsciiEx ( vkCode,
              scanCode,
              keys_,
              (LPWORD)key_str,
              0,
              GetKeyboardLayout (0) )) {
              strncat (text, key_str, 1);
              command_issued = false;
            }
          }
        } else if ((! keyDown)) {
          keys_ [vkCode] = 0x00;
        }

        if (visible)
          return 1;
      }
    }

    return CallNextHookEx (BMF_InputHooker::getInstance ()->hooks.keyboard, nCode, wParam, lParam);
  };
};

BMF_InputHooker* BMF_InputHooker::pInputHook;
char             BMF_InputHooker::text [16384];

BYTE BMF_InputHooker::keys_ [256] = { 0 };
bool BMF_InputHooker::visible     = false;

bool BMF_InputHooker::command_issued = false;
std::string BMF_InputHooker::result_str;

BMF_InputHooker::command_history_t BMF_InputHooker::commands;




void
WINAPI
dxgi_init_callback (void)
{
  HMODULE hBackend = backend_dll;

  dll_log.Log (L"Importing CreateDXGIFactory{1|2}");
  dll_log.Log (L"================================");

  dll_log.Log (L"  CreateDXGIFactory:  %08Xh", 
    (CreateDXGIFactory_Import =  \
      (CreateDXGIFactory_t)GetProcAddress (hBackend, "CreateDXGIFactory")));
  dll_log.Log (L"  CreateDXGIFactory1: %08Xh",
    (CreateDXGIFactory1_Import = \
      (CreateDXGIFactory1_t)GetProcAddress (hBackend, "CreateDXGIFactory1")));
  dll_log.Log (L"  CreateDXGIFactory2: %08Xh",
    (CreateDXGIFactory2_Import = \
      (CreateDXGIFactory2_t)GetProcAddress (hBackend, "CreateDXGIFactory2")));

  BMF_CreateDLLHook ( L"d3d11.dll", "D3D11CreateDeviceAndSwapChain",
    D3D11CreateDeviceAndSwapChain_Detour,
    (LPVOID *)&D3D11CreateDeviceAndSwapChain_Import,
    &pfnD3D11CreateDeviceAndSwapChain );

  BMF_EnableHook (pfnD3D11CreateDeviceAndSwapChain);

  BMF_CreateDLLHook ( L"kernel32.dll", "QueryPerformanceCounter",
                      QueryPerformanceCounter_Detour, 
           (LPVOID *)&QueryPerformanceCounter_Original,
           (LPVOID *)&pfnQueryPerformanceCounter );

  BMF_CreateDLLHook ( L"kernel32.dll", "Sleep",
                      Sleep_Detour, 
           (LPVOID *)&Sleep_Original,
           (LPVOID *)&pfnSleep );

  BMF_EnableHook (pfnQueryPerformanceCounter);
  BMF_EnableHook (pfnSleep);

  BMF_InputHooker* pHooker = BMF_InputHooker::getInstance ();
  pHooker->Start ();

  command.AddVariable ( "MaxDeltaTime",
          new eTB_VarStub <int> (&config.render.framerate.max_delta_time));
  command.AddVariable ( "PresentationInterval",
          new eTB_VarStub <int> (&config.render.framerate.present_interval));
  command.AddVariable ( "PreRenderLimit",
          new eTB_VarStub <int> (&config.render.framerate.pre_render_limit));
  command.AddVariable ( "BufferCount",
          new eTB_VarStub <int> (&config.render.framerate.buffer_count));
  command.AddVariable ( "UseFlipDiscard",
          new eTB_VarStub <bool> (&config.render.framerate.flip_discard));
  command.AddVariable ( "FudgeFactor",
          new eTB_VarStub <float> (&config.render.framerate.fudge_factor));
}


bool
BMF::DXGI::Startup (void)
{
  return BMF_StartupCore (L"dxgi", dxgi_init_callback);
}

bool
BMF::DXGI::Shutdown (void)
{
  BMF_InputHooker* pHooker = BMF_InputHooker::getInstance ();
  pHooker->End ();

  BMF_RemoveHook (pfnSleep);
  BMF_RemoveHook (pfnQueryPerformanceCounter);
  BMF_RemoveHook (pfnD3D11CreateDeviceAndSwapChain);

  return BMF_ShutdownCore (L"dxgi");
}











































template <>
str_hash_compare <std::string, std::less <std::string> >::size_type
str_hash_compare <std::string, std::less <std::string> >::hash_string (const std::string& _Keyval) const
{
  const bool case_insensitive = true;

  size_type   __h    = 0;
  const size_type   __len  = _Keyval.size ();
  const value_type* __data = _Keyval.data ();

  for (size_type __i = 0; __i < __len; ++__i) {
    /* Hash Collision Discovered: "r_window_res_x" vs. "r_window_pos_x" */
    //__h = 5 * __h + eTB_CaseAdjust (__data [__i], case_insensitive);

    /* New Hash: sdbm   -  Collision Free (08/04/12) */
    __h = eTB_CaseAdjust (__data [__i], case_insensitive) +
      (__h << 06)  +  (__h << 16)      -
      __h;
  }

  return __h;
}


template <>
str_hash_compare <std::string, std::less <std::string> >::size_type
str_hash_compare <std::string, std::less <std::string> >::operator() (const std::string& _Keyval) const
{
  return hash_string (_Keyval);
}

template <>
bool
str_hash_compare <std::string, std::less <std::string> >::operator() (const std::string& _lhs, const std::string& _rhs) const
{
  return hash_string (_lhs) < hash_string (_rhs);
}

class eTB_SourceCmd : public eTB_Command
{
public:
  eTB_SourceCmd (eTB_CommandProcessor* cmd_proc) {
    processor_ = cmd_proc;
  }

  eTB_CommandResult execute (const char* szArgs) {
    /* TODO: Replace with a special tokenizer / parser... */
    FILE* src = fopen (szArgs, "r");

    if (! src) {
      return
        eTB_CommandResult ( "source", szArgs,
          "Could not open file!",
          false,
          NULL,
          this );
    }

    char line [1024];

    static int num_lines = 0;

    while (fgets (line, 1024, src) != NULL) {
      num_lines++;

      /* Remove the newline character... */
      line [strlen (line) - 1] = '\0';

      processor_->ProcessCommandLine (line);

      //printf (" Source Line %d - '%s'\n", num_lines++, line);
    }

    fclose (src);

    return eTB_CommandResult ( "source", szArgs,
      "Success",
      num_lines,
      NULL,
      this );
  }

  int getNumArgs (void) {
    return 1;
  }

  int getNumOptionalArgs (void) {
    return 0;
  }

  const char* getHelp (void) {
    return "Load and execute a file containing multiple commands "
      "(such as a config file).";
  }

private:
  eTB_CommandProcessor* processor_;

};

eTB_CommandProcessor::eTB_CommandProcessor (void)
{
  eTB_Command* src = new eTB_SourceCmd (this);

  AddCommand ("source", src);
}

const eTB_Command*
eTB_CommandProcessor::AddCommand (const char* szCommand, eTB_Command* pCommand)
{
  if (szCommand == NULL || strlen (szCommand) < 1)
    return NULL;

  if (pCommand == NULL)
    return NULL;

  /* Command already exists, what should we do?! */
  if (FindCommand (szCommand) != NULL)
    return NULL;

  commands_.insert (eTB_CommandRecord (szCommand, pCommand));

  return pCommand;
}

bool
eTB_CommandProcessor::RemoveCommand (const char* szCommand)
{
  if (FindCommand (szCommand) != NULL) {
    std::unordered_map <std::string, eTB_Command*, str_hash_compare <std::string> >::iterator
      command = commands_.find (szCommand);

    commands_.erase (command);
    return true;
  }

  return false;
}

eTB_Command*
eTB_CommandProcessor::FindCommand (const char* szCommand) const
{
  std::unordered_map <std::string, eTB_Command*, str_hash_compare <std::string> >::const_iterator
    command = commands_.find (szCommand);

  if (command != commands_.end ())
    return (command)->second;

  return NULL;
}



const eTB_Variable*
eTB_CommandProcessor::AddVariable (const char* szVariable, eTB_Variable* pVariable)
{
  if (szVariable == NULL || strlen (szVariable) < 1)
    return NULL;

  if (pVariable == NULL)
    return NULL;

  /* Variable already exists, what should we do?! */
  if (FindVariable (szVariable) != NULL)
    return NULL;

  variables_.insert (eTB_VariableRecord (szVariable, pVariable));

  return pVariable;
}

bool
eTB_CommandProcessor::RemoveVariable (const char* szVariable)
{
  if (FindVariable (szVariable) != NULL) {
    std::unordered_map <std::string, eTB_Variable*, str_hash_compare <std::string> >::iterator
      variable = variables_.find (szVariable);

    variables_.erase (variable);
    return true;
  }

  return false;
}

const eTB_Variable*
eTB_CommandProcessor::FindVariable (const char* szVariable) const
{
  std::unordered_map <std::string, eTB_Variable*, str_hash_compare <std::string> >::const_iterator
    variable = variables_.find (szVariable);

  if (variable != variables_.end ())
    return (variable)->second;

  return NULL;
}



eTB_CommandResult
eTB_CommandProcessor::ProcessCommandLine (const char* szCommandLine)
{
  if (szCommandLine != NULL && strlen (szCommandLine))
  {
    char*  command_word     = strdup (szCommandLine);
    size_t command_word_len = strlen (command_word);

    char*  command_args     = command_word;
    size_t command_args_len = 0;

    /* Terminate the command word on the first space... */
    for (size_t i = 0; i < command_word_len; i++) {
      if (command_word [i] == ' ') {
        command_word [i] = '\0';

        if (i < (command_word_len - 1)) {
          command_args     = &command_word [i + 1];
          command_args_len = strlen (command_args);

          /* Eliminate trailing spaces */
          for (unsigned int j = 0; j < command_args_len; j++) {
            if (command_word [i + j + 1] != ' ') {
              command_args = &command_word [i + j + 1];
              break;
            }
          }

          command_args_len = strlen (command_args);
        }

        break;
      }
    }

    std::string cmd_word (command_word);
    std::string cmd_args (command_args_len > 0 ? command_args : "");
    /* ^^^ cmd_args is what is passed back to the object that issued
    this command... If no arguments were passed, it MUST be
    an empty string. */

    eTB_Command* cmd = command.FindCommand (command_word);

    if (cmd != NULL) {
      return cmd->execute (command_args);
    }

    /* No command found, perhaps the word was a variable? */

    const eTB_Variable* var = command.FindVariable (command_word);

    if (var != NULL) {
      if (var->getType () == eTB_Variable::Boolean)
      {
        if (command_args_len > 0) {
          eTB_VarStub <bool>* bool_var = (eTB_VarStub <bool>*) var;
          bool                bool_val = false;

          /* False */
          if (! (stricmp (command_args, "false") && stricmp (command_args, "0") &&
            stricmp (command_args, "off"))) {
            bool_val = false;
            bool_var->setValue (bool_val);
          }

          /* True */
          else if (! (stricmp (command_args, "true") && stricmp (command_args, "1") &&
            stricmp (command_args, "on"))) {
            bool_val = true;
            bool_var->setValue (bool_val);
          }

          /* Toggle */
          else if ( !(stricmp (command_args, "toggle") && stricmp (command_args, "~") &&
            stricmp (command_args, "!"))) {
            bool_val = ! bool_var->getValue ();
            bool_var->setValue (bool_val);

            /* ^^^ TODO: Consider adding a toggle (...) function to
            the bool specialization of eTB_VarStub... */
          } else {
            // Unknown Trailing Characters
          }
        }
      }

      else if (var->getType () == eTB_Variable::Int)
      {
        if (command_args_len > 0) {
          int original_val = ((eTB_VarStub <int>*) var)->getValue ();
          int int_val = 0;

          /* Increment */
          if (! (stricmp (command_args, "++") && stricmp (command_args, "inc") &&
            stricmp (command_args, "next"))) {
            int_val = original_val + 1;
          } else if (! (stricmp (command_args, "--") && stricmp (command_args, "dec") &&
            stricmp (command_args, "prev"))) {
            int_val = original_val - 1;
          } else
            int_val = atoi (command_args);

          ((eTB_VarStub <int>*) var)->setValue (int_val);
        }
      }

      else if (var->getType () == eTB_Variable::Short)
      {
        if (command_args_len > 0) {
          short original_val = ((eTB_VarStub <short>*) var)->getValue ();
          short short_val    = 0;

          /* Increment */
          if (! (stricmp (command_args, "++") && stricmp (command_args, "inc") &&
            stricmp (command_args, "next"))) {
            short_val = original_val + 1;
          } else if (! (stricmp (command_args, "--") && stricmp (command_args, "dec") &&
            stricmp (command_args, "prev"))) {
            short_val = original_val - 1;
          } else
            short_val = (short)atoi (command_args);

          ((eTB_VarStub <short>*) var)->setValue (short_val);
        }
      }

      else if (var->getType () == eTB_Variable::Float)
      {
        if (command_args_len > 0) {
          //          float original_val = ((eTB_VarStub <float>*) var)->getValue ();
          float float_val = (float)atof (command_args);

          ((eTB_VarStub <float>*) var)->setValue (float_val);
        }
      }

      free (command_word);

      return eTB_CommandResult (cmd_word, cmd_args, var->getValueString (), true, var, NULL);
    } else {
      free (command_word);

      /* Default args --> failure... */
      return eTB_CommandResult (cmd_word, cmd_args); 
    }
  } else {
    /* Invalid Command Line (not long enough). */
    return eTB_CommandResult (szCommandLine); /* Default args --> failure... */
  }
}

/** Variable Type Support **/


template <>
eTB_VarStub <bool>::eTB_VarStub ( bool*                  var,
  eTB_iVariableListener* pListener ) :
  var_ (var)
{
  listener_ = pListener;
  type_     = Boolean;
}

template <>
std::string
eTB_VarStub <bool>::getValueString (void) const
{
  if (getValue ())
    return std::string ("true");
  else
    return std::string ("false");
}

template <>
eTB_VarStub <const char*>::eTB_VarStub ( const char**           var,
  eTB_iVariableListener* pListener ) :
  var_ (var)
{
  listener_ = pListener;
  type_     = String;
}

template <>
eTB_VarStub <int>::eTB_VarStub ( int*                  var,
  eTB_iVariableListener* pListener ) :
  var_ (var)
{
  listener_ = pListener;
  type_     = Int;
}

template <>
std::string
eTB_VarStub <int>::getValueString (void) const
{
  char szIntString [32];
  snprintf (szIntString, 32, "%d", getValue ());

  return std::string (szIntString);
}


template <>
eTB_VarStub <short>::eTB_VarStub ( short*                 var,
  eTB_iVariableListener* pListener ) :
  var_ (var)
{
  listener_ = pListener;
  type_     = Short;
}

template <>
std::string
eTB_VarStub <short>::getValueString (void) const
{
  char szShortString [32];
  snprintf (szShortString, 32, "%d", getValue ());

  return std::string (szShortString);
}


template <>
eTB_VarStub <float>::eTB_VarStub ( float*                 var,
  eTB_iVariableListener* pListener ) :
  var_ (var)
{
  listener_ = pListener;
  type_     = Float;
}

template <>
std::string
eTB_VarStub <float>::getValueString (void) const
{
  char szFloatString [32];
  snprintf (szFloatString, 32, "%f", getValue ());

  return std::string (szFloatString);
}