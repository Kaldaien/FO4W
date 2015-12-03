#include <string>

#include "ini.h"
#include "parameter.h"
#include "utility.h"

#include "log.h"

bmf::ParameterFactory fo4_factory;

bmf::INI::File*       fo4_prefs        = nullptr;
bmf::ParameterBool*   fo4_fullscreen   = nullptr;
bmf::ParameterBool*   fo4_borderless   = nullptr;
bmf::ParameterInt*    fo4_spoof_memory = nullptr;

#define __NvAPI_GPU_GetMemoryInfo 0x07F9B368

HMODULE nvapi64_dll;
#include "core.h"
#include "nvapi.h"

typedef NvAPI_Status (__cdecl *NvAPI_GPU_GetMemoryInfo_t)(NvPhysicalGpuHandle hPhysicalGpu, NV_DISPLAY_DRIVER_MEMORY_INFO *pMemoryInfo);
NvAPI_GPU_GetMemoryInfo_t NvAPI_GPU_GetMemoryInfo_Original = nullptr;

NvAPI_Status
__cdecl
NvAPI_GPU_GetMemoryInfo_Detour ( NvPhysicalGpuHandle            hPhysicalGpu,
                                 NV_DISPLAY_DRIVER_MEMORY_INFO *pMemoryInfo )
{
  if (fo4_spoof_memory == nullptr) {
    fo4_spoof_memory = 
      static_cast <bmf::ParameterInt *>
        (fo4_factory.create_parameter <int> (L"Memory Multiplier"));
    fo4_spoof_memory->register_to_ini ( fo4_prefs,
                                        L"Display",
                                          L"iMemoryMultiplier" );

    fo4_spoof_memory->load ();
  }

  const int shift = fo4_spoof_memory->get_value ();

  NvAPI_Status status =
    NvAPI_GPU_GetMemoryInfo_Original (hPhysicalGpu, pMemoryInfo);

  if (status == NVAPI_OK) {
    pMemoryInfo->dedicatedVideoMemory             <<= shift;
    pMemoryInfo->availableDedicatedVideoMemory    <<= shift;
    pMemoryInfo->curAvailableDedicatedVideoMemory <<= shift;
    pMemoryInfo->sharedSystemMemory               <<= shift;
    pMemoryInfo->systemVideoMemory                <<= shift;
  }

  return status;
}

LPVOID NVAPI_GPU_GETMEMORYINFO_PROC;

void
BMF_FO4_InitPlugin (void)
{
  if (fo4_prefs == nullptr) {
    std::wstring fo4_prefs_file =
      BMF_GetDocumentsDir () +
      std::wstring (L"\\My Games\\Fallout4\\Fallout4Prefs.ini");

    fo4_prefs = new bmf::INI::File (fo4_prefs_file.c_str ());
    fo4_prefs->parse ();
  }

  //nvapi64_dll = LoadLibrary (L"nvapi64.dll");

  //typedef void* (*NvAPI_QueryInterface_t)(unsigned int ordinal);

  //static NvAPI_QueryInterface_t NvAPI_QueryInterface =
    //(NvAPI_QueryInterface_t)GetProcAddress (nvapi64_dll, "nvapi_QueryInterface");

    //(NvAPI_GPU_GetMemoryInfo_t)NvAPI_QueryInterface (__NvAPI_GPU_GetMemoryInfo);

  BMF_CreateFuncHook ( L"NvAPI_GPU_GetMemoryInfo", NvAPI_GPU_GetMemoryInfo,
                         NvAPI_GPU_GetMemoryInfo_Detour, (LPVOID *)&NvAPI_GPU_GetMemoryInfo_Original );
  BMF_EnableHook     (NvAPI_GPU_GetMemoryInfo);
}

bool
BMF_FO4_IsFullscreen (void)
{
  BMF_FO4_InitPlugin ();

  if (fo4_fullscreen == nullptr) {
    fo4_fullscreen = 
      static_cast <bmf::ParameterBool *>
        (fo4_factory.create_parameter <bool> (L"Fullscreen Mode"));
    fo4_fullscreen->register_to_ini ( fo4_prefs,
                                        L"Display",
                                          L"bFull Screen" );

    fo4_fullscreen->load ();
  }

  return (fo4_fullscreen->get_value ());
}

bool
BMF_FO4_IsBorderlessWindow (void)
{
  BMF_FO4_InitPlugin ();

  if (fo4_borderless == nullptr) {
    fo4_borderless = 
      static_cast <bmf::ParameterBool *>
        (fo4_factory.create_parameter <bool> (L"Borderless Window"));
    fo4_borderless->register_to_ini ( fo4_prefs,
                                        L"Display",
                                          L"bBorderless" );

    fo4_borderless->load ();
  }

  return (fo4_borderless->get_value ());
}