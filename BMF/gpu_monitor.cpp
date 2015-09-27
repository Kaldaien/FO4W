#include "gpu_monitor.h"
#include "config.h"

gpu_sensors_t gpu_stats;

#include "nvapi.h"
extern BOOL nvapi_init;

#define NVAPI_GPU_UTILIZATION_DOMAIN_GPU 0
#define NVAPI_GPU_UTILIZATION_DOMAIN_FB  1
#define NVAPI_GPU_UTILIZATION_DOMAIN_VID 2
#define NVAPI_GPU_UTILIZATION_DOMAIN_BUS 3

void
BMF_PollGPU (void)
{
  // Only support GPU stats on NVIDIA hardware for now
  if (! nvapi_init) {
    gpu_stats.num_gpus = 0;
    return;
  }

  SYSTEMTIME     update_time;
  FILETIME       update_ftime;
  ULARGE_INTEGER update_ul;

  GetSystemTime        (&update_time);
  SystemTimeToFileTime (&update_time, &update_ftime);

  update_ul.HighPart = update_ftime.dwHighDateTime;
  update_ul.LowPart  = update_ftime.dwLowDateTime;

  double dt = (update_ul.QuadPart - gpu_stats.last_update.QuadPart) * 1.0e-7;

  if (dt > config.gpu_interval) {
    gpu_stats.last_update.QuadPart = update_ul.QuadPart;

    NvPhysicalGpuHandle gpus [NVAPI_MAX_PHYSICAL_GPUS];
    NvU32               gpu_count;

    NvAPI_EnumPhysicalGPUs (gpus, &gpu_count);

    NV_GPU_DYNAMIC_PSTATES_INFO_EX psinfoex;
    psinfoex.version = NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER;

    gpu_stats.num_gpus = gpu_count;

    for (int i = 0; i < gpu_stats.num_gpus; i++) {
      NvPhysicalGpuHandle gpu    = gpus [i];
      NvAPI_Status        status =
        NvAPI_GPU_GetDynamicPstatesInfoEx (gpu, &psinfoex);

      gpu_stats.gpus [i].loads_percent.gpu = psinfoex.utilization
        [NVAPI_GPU_UTILIZATION_DOMAIN_GPU].percentage;
      gpu_stats.gpus [i].loads_percent.fb  = psinfoex.utilization
        [NVAPI_GPU_UTILIZATION_DOMAIN_FB].percentage;
      gpu_stats.gpus [i].loads_percent.vid = psinfoex.utilization
        [NVAPI_GPU_UTILIZATION_DOMAIN_VID].percentage;
      gpu_stats.gpus [i].loads_percent.bus = psinfoex.utilization
        [NVAPI_GPU_UTILIZATION_DOMAIN_BUS].percentage;

      NV_GPU_THERMAL_SETTINGS thermal;
      thermal.version = NV_GPU_THERMAL_SETTINGS_VER;

      status = NvAPI_GPU_GetThermalSettings (gpu,
                                             NVAPI_THERMAL_TARGET_ALL,
                                             &thermal);

      if (status == NVAPI_OK) {
        for (int j = 0; j < thermal.count; j++) {
          if (thermal.sensor [j].target == NVAPI_THERMAL_TARGET_GPU)
            gpu_stats.gpus [i].temps_c.gpu = thermal.sensor [j].currentTemp;
          if (thermal.sensor [j].target == NVAPI_THERMAL_TARGET_MEMORY)
            gpu_stats.gpus [i].temps_c.ram = thermal.sensor [j].currentTemp;
          if (thermal.sensor [j].target == NVAPI_THERMAL_TARGET_POWER_SUPPLY)
            gpu_stats.gpus [i].temps_c.psu = thermal.sensor [j].currentTemp;
          if (thermal.sensor [j].target == NVAPI_THERMAL_TARGET_BOARD)
            gpu_stats.gpus [i].temps_c.pcb = thermal.sensor [j].currentTemp;
        }
      }

      NV_DISPLAY_DRIVER_MEMORY_INFO meminfo;
      meminfo.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER;

      NvAPI_GPU_GetMemoryInfo (gpu, &meminfo);

      int64_t local = 
        (meminfo.availableDedicatedVideoMemory) - 
        (meminfo.curAvailableDedicatedVideoMemory);

      gpu_stats.gpus[i].memory_B.local = local * 1024LL;

      gpu_stats.gpus [i].memory_B.total = 
        ((meminfo.dedicatedVideoMemory) -
         (meminfo.curAvailableDedicatedVideoMemory)) * 1024LL;

      // Compute Non-Local
      gpu_stats.gpus [i].memory_B.nonlocal = 
        gpu_stats.gpus [i].memory_B.total - gpu_stats.gpus [i].memory_B.local;

      NV_GPU_CLOCK_FREQUENCIES freq;
      freq.version = NV_GPU_CLOCK_FREQUENCIES_VER;

      freq.ClockType = NV_GPU_CLOCK_FREQUENCIES_CURRENT_FREQ;
      NvAPI_GPU_GetAllClockFrequencies (gpu, &freq);

      gpu_stats.gpus [i].clocks_kHz.gpu    = freq.domain [NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency;
      gpu_stats.gpus [i].clocks_kHz.ram    = freq.domain [NVAPI_GPU_PUBLIC_CLOCK_MEMORY].frequency;
      gpu_stats.gpus [i].clocks_kHz.shader = freq.domain [NVAPI_GPU_PUBLIC_CLOCK_PROCESSOR].frequency;
    }
  }
}