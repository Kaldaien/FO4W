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
#include "command.h"
#include "framerate.h"

#include <unordered_map>
std::unordered_map <DWORD, DWORD>         thread_sleep;
std::unordered_map <DWORD, LARGE_INTEGER> thread_perf;

extern std::wstring host_app;

extern BOOL __stdcall BMF_NvAPI_SetFramerateLimit (uint32_t limit);
extern void __stdcall BMF_NvAPI_SetAppFriendlyName (const wchar_t* wszFriendlyName);

extern "C++" bool BMF_FO4_IsFullscreen       (void);
extern "C++" bool BMF_FO4_IsBorderlessWindow (void);

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
      static bool first_frame = true;

      if (first_frame && bmf::NVAPI::app_name == L"Fallout4.exe") {
        // Fix the broken borderless window system that doesn't scale the swapchain
        //   properly.
        if (BMF_FO4_IsBorderlessWindow ()) {
          DEVMODE devmode = { 0 };
          devmode.dmSize = sizeof DEVMODE;
          EnumDisplaySettings (nullptr, ENUM_CURRENT_SETTINGS, &devmode);

          DXGI_SWAP_CHAIN_DESC desc;
          This->GetDesc (&desc);

          if (devmode.dmPelsHeight != desc.BufferDesc.Height ||
              devmode.dmPelsWidth  != desc.BufferDesc.Width) {
            devmode.dmPelsWidth  = desc.BufferDesc.Width;
            devmode.dmPelsHeight = desc.BufferDesc.Height;
            ChangeDisplaySettings (&devmode, CDS_FULLSCREEN);
          }
        }
      }

      first_frame = false;

      IUnknown* pDev = nullptr;

      int interval = config.render.framerate.present_interval;
      int flags    = Flags;

      // Application preference
      if (interval == -1)
        interval = SyncInterval;

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
        if (SUCCEEDED (This->QueryInterface (__uuidof(IDXGISwapChain1),(void **)&pSwapChain1))) {
          hr = pSwapChain1->Present1 (interval, flags, &pparams);
          pSwapChain1->Release ();
        }
      }

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

      if (host_app == L"Fallout4.exe") {
        if (bFlipMode) {
          bFlipMode = (! BMF_FO4_IsFullscreen ());
          if (bFlipMode) {
            bFlipMode = (! config.nvidia.sli.override);
          }
        }
      }

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

      if (config.render.framerate.buffer_count != -1)
        pDesc->BufferCount = config.render.framerate.buffer_count;

      // We cannot switch modes on a waitable swapchain
      if (bFlipMode && bWait) {
        pDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        pDesc->Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
      }

      dll_log.Log ( L" >> Using %s Presentation Model  [Waitable: %s]",
                     bFlipMode ? L"Flip" : L"Traditional",
                       bWait ? L"Yes" : L"No" );
    }

#if 0
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
#endif
      DXGI_CALL(ret, CreateSwapChain_Original (This, pDevice, pDesc, ppSwapChain));
//    }

    if ( SUCCEEDED (ret)      &&
         ppSwapChain  != NULL &&
       (*ppSwapChain) != NULL )
    {
#if 0
      DXGI_VIRTUAL_OVERRIDE (ppSwapChain, 10, "IDXGISwapChain::SetFullscreenState",
                             SetFullscreenState_Override, SetFullscreenState_Original,
                             SetFullscreenState_t);

      DXGI_VIRTUAL_OVERRIDE (ppSwapChain, 13, "IDXGISwapChain::ResizeBuffers",
                             ResizeBuffers_Override, ResizeBuffers_Original,
                             ResizeBuffers_t);
#endif

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
        if (max_latency != -1) {
          IDXGIDevice1* pDevice1 = nullptr;
          if (SUCCEEDED ( (*ppSwapChain)->GetDevice (
                             __uuidof (IDXGIDevice1),
                               (void **)&pDevice1 )
                        )
             ) {
            dll_log.Log (L"Setting Device Frame Latency: %lu", max_latency);
            pDevice1->SetMaximumFrameLatency (max_latency);
            pDevice1->Release ();
          }
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

    HRESULT res;
    DXGI_CALL(res, 
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
                                            ppImmediateContext));

    if (res == S_OK && (ppDevice != NULL))
    {
      dll_log.Log (L" >> Device = 0x%08Xh", *ppDevice);
    }
    return S_OK;

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
    if (false) {//nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0 && iver >= 3) {
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
        if (false) { //nvapi_init && bmf::NVAPI::CountSLIGPUs () > 0 && iver >= 3) {
          DXGI_ADAPTER_DESC* match =
            bmf::NVAPI::FindGPUByDXGIName (desc.Description);

          if (match != NULL &&
            desc.DedicatedVideoMemory > match->DedicatedVideoMemory) {
// This creates problems in 32-bit environments...
#ifdef _WIN64
            if (bmf::NVAPI::app_name != L"Fallout4.exe") {
              dll_log.Log (
                L"   # SLI Detected (Corrected Memory Total: %llu MiB -- "
                L"Original: %llu MiB)",
                match->DedicatedVideoMemory >> 20ULL,
                desc.DedicatedVideoMemory   >> 20ULL);
            } else {
              match->DedicatedVideoMemory = desc.DedicatedVideoMemory;
            }
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

#if 0
  BMF_CreateDLLHook ( L"d3d11.dll", "D3D11CreateDeviceAndSwapChain",
    D3D11CreateDeviceAndSwapChain_Detour,
    (LPVOID *)&D3D11CreateDeviceAndSwapChain_Import,
    &pfnD3D11CreateDeviceAndSwapChain );

  BMF_EnableHook (pfnD3D11CreateDeviceAndSwapChain);
#endif

  BMF_CommandProcessor* pCommandProc =
    BMF_GetCommandProcessor ();

  pCommandProc->AddVariable ( "MaxDeltaTime",
          new BMF_VarStub <int> (&config.render.framerate.max_delta_time));
  pCommandProc->AddVariable ( "PresentationInterval",
          new BMF_VarStub <int> (&config.render.framerate.present_interval));
  pCommandProc->AddVariable ( "PreRenderLimit",
          new BMF_VarStub <int> (&config.render.framerate.pre_render_limit));
  pCommandProc->AddVariable ( "BufferCount",
          new BMF_VarStub <int> (&config.render.framerate.buffer_count));
  pCommandProc->AddVariable ( "UseFlipDiscard",
          new BMF_VarStub <bool> (&config.render.framerate.flip_discard));
  pCommandProc->AddVariable ( "FudgeFactor",
          new BMF_VarStub <float> (&config.render.framerate.fudge_factor));
}


bool
BMF::DXGI::Startup (void)
{
  return BMF_StartupCore (L"dxgi", dxgi_init_callback);
}

bool
BMF::DXGI::Shutdown (void)
{
  ////BMF_RemoveHook (pfnD3D11CreateDeviceAndSwapChain);

  return BMF_ShutdownCore (L"dxgi");
}
