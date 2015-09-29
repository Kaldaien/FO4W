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

  if (dt > config.gpu.interval) {
    gpu_stats.last_update.QuadPart = update_ul.QuadPart;

    NvPhysicalGpuHandle gpus [NVAPI_MAX_PHYSICAL_GPUS];
    NvU32               gpu_count;

    if (NVAPI_OK != NvAPI_EnumPhysicalGPUs (gpus, &gpu_count))
      return;

    NV_GPU_DYNAMIC_PSTATES_INFO_EX psinfoex;
    psinfoex.version = NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER;

    gpu_stats.num_gpus = gpu_count;

    for (int i = 0; i < gpu_stats.num_gpus; i++) {
      NvPhysicalGpuHandle gpu    = gpus [i];
      NvAPI_Status        status =
        NvAPI_GPU_GetDynamicPstatesInfoEx (gpu, &psinfoex);

      if (status == NVAPI_OK)
      {
#ifdef SMOOTH_GPU_UPDATES
      gpu_stats.gpus [i].loads_percent.gpu = (gpu_stats.gpus [i].loads_percent.gpu +
       psinfoex.utilization [NVAPI_GPU_UTILIZATION_DOMAIN_GPU].percentage) / 2;
      gpu_stats.gpus [i].loads_percent.fb  = (gpu_stats.gpus [i].loads_percent.fb +
        psinfoex.utilization [NVAPI_GPU_UTILIZATION_DOMAIN_FB].percentage) / 2;
      gpu_stats.gpus [i].loads_percent.vid = (gpu_stats.gpus [i].loads_percent.vid +
        psinfoex.utilization [NVAPI_GPU_UTILIZATION_DOMAIN_VID].percentage) / 2;
      gpu_stats.gpus [i].loads_percent.bus = (gpu_stats.gpus [i].loads_percent.bus +
        psinfoex.utilization [NVAPI_GPU_UTILIZATION_DOMAIN_BUS].percentage) / 2;
#else
      gpu_stats.gpus [i].loads_percent.gpu =
        psinfoex.utilization [NVAPI_GPU_UTILIZATION_DOMAIN_GPU].percentage;
      gpu_stats.gpus [i].loads_percent.fb =
        psinfoex.utilization [NVAPI_GPU_UTILIZATION_DOMAIN_FB].percentage;
      gpu_stats.gpus [i].loads_percent.vid =
        psinfoex.utilization [NVAPI_GPU_UTILIZATION_DOMAIN_VID].percentage;
      gpu_stats.gpus [i].loads_percent.bus =
        psinfoex.utilization [NVAPI_GPU_UTILIZATION_DOMAIN_BUS].percentage;
#endif
      }

      NV_GPU_THERMAL_SETTINGS thermal;
      thermal.version = NV_GPU_THERMAL_SETTINGS_VER;

      status = NvAPI_GPU_GetThermalSettings (gpu,
                                             NVAPI_THERMAL_TARGET_ALL,
                                             &thermal);

      if (status == NVAPI_OK) {
        for (NvU32 j = 0; j < thermal.count; j++) {
#ifdef SMOOTH_GPU_UPDATES
          if (thermal.sensor [j].target == NVAPI_THERMAL_TARGET_GPU)
            gpu_stats.gpus [i].temps_c.gpu = (gpu_stats.gpus[i].temps_c.gpu + thermal.sensor [j].currentTemp) / 2;
#else
          if (thermal.sensor [j].target == NVAPI_THERMAL_TARGET_GPU)
            gpu_stats.gpus [i].temps_c.gpu = thermal.sensor [j].currentTemp;
#endif
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

      if (NVAPI_OK == NvAPI_GPU_GetMemoryInfo (gpu, &meminfo))
      {
        int64_t local = 
          (meminfo.availableDedicatedVideoMemory) - 
          (meminfo.curAvailableDedicatedVideoMemory);

        gpu_stats.gpus [i].memory_B.local = local * 1024LL;

        gpu_stats.gpus [i].memory_B.total = 
          ((meminfo.dedicatedVideoMemory) -
           (meminfo.curAvailableDedicatedVideoMemory)) * 1024LL;

        // Compute Non-Local
        gpu_stats.gpus [i].memory_B.nonlocal = 
          gpu_stats.gpus [i].memory_B.total - gpu_stats.gpus [i].memory_B.local;
      }

      NV_GPU_CLOCK_FREQUENCIES freq;
      freq.version = NV_GPU_CLOCK_FREQUENCIES_VER;

      freq.ClockType = NV_GPU_CLOCK_FREQUENCIES_CURRENT_FREQ;
      if (NVAPI_OK == NvAPI_GPU_GetAllClockFrequencies (gpu, &freq))
      {
        gpu_stats.gpus [i].clocks_kHz.gpu    =
          freq.domain [NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency;
        gpu_stats.gpus [i].clocks_kHz.ram    =
          freq.domain [NVAPI_GPU_PUBLIC_CLOCK_MEMORY].frequency;
        gpu_stats.gpus [i].clocks_kHz.shader =
          freq.domain [NVAPI_GPU_PUBLIC_CLOCK_PROCESSOR].frequency;
      }

      NvU32 tach;

      gpu_stats.gpus [i].fans_rpm.supported = false;

      if (NVAPI_OK == NvAPI_GPU_GetTachReading (gpu, &tach)) {
        gpu_stats.gpus [i].fans_rpm.gpu       = tach;
        gpu_stats.gpus [i].fans_rpm.supported = true;
      }

      NvU32 perf_decrease_info;
      NvAPI_GPU_GetPerfDecreaseInfo (gpu, &perf_decrease_info);

      gpu_stats.gpus [i].nv_perf_state = perf_decrease_info;


      NV_GPU_PERF_PSTATES20_INFO ps20info;
      ps20info.version = NV_GPU_PERF_PSTATES20_INFO_VER;

      gpu_stats.gpus [i].volts_mV.supported = false;

      if (NVAPI_OK == NvAPI_GPU_GetPstates20 (gpu, &ps20info))
      {
        NV_GPU_PERF_PSTATE_ID current_pstate;

        if (NVAPI_OK == NvAPI_GPU_GetCurrentPstate (gpu, &current_pstate))
        {
          for (NvU32 pstate = 0; pstate < ps20info.numPstates; pstate++) {
            if (ps20info.pstates [pstate].pstateId == current_pstate) {
#if 1
              // First, check for over-voltage...
              if (gpu_stats.gpus [i].volts_mV.supported == false)
              {
                for (NvU32 volt = 0; volt < ps20info.ov.numVoltages; volt++) {
                  if (ps20info.ov.voltages [volt].domainId ==
                    NVAPI_GPU_PERF_VOLTAGE_INFO_DOMAIN_CORE) {
                    gpu_stats.gpus [i].volts_mV.supported = true;
                    gpu_stats.gpus [i].volts_mV.over      = true;

                    NV_GPU_PSTATE20_BASE_VOLTAGE_ENTRY_V1* voltage = 
                      &ps20info.ov.voltages [volt];

                    gpu_stats.gpus [i].volts_mV.core = voltage->volt_uV/1000.0f;

                    int over  =
                      voltage->voltDelta_uV.value -
                      voltage->voltDelta_uV.valueRange.max;

                    int under =
                      voltage->voltDelta_uV.valueRange.min -
                      voltage->voltDelta_uV.value;

                    if (over > 0)
                      gpu_stats.gpus [i].volts_mV.ov =   over  / 1000.0f;
                    else if (under > 0)
                      gpu_stats.gpus [i].volts_mV.ov = -(under / 1000.0f);
                    break;
                  }
                }
              }

              // If that fails, look through the normal voltages.
#endif
              for (NvU32 volt = 0; volt < ps20info.numBaseVoltages; volt++) {
                if (ps20info.pstates [pstate].baseVoltages [volt].domainId ==
                    NVAPI_GPU_PERF_VOLTAGE_INFO_DOMAIN_CORE) {
                  gpu_stats.gpus [i].volts_mV.supported = true;
                  gpu_stats.gpus [i].volts_mV.over      = false;

                  NV_GPU_PSTATE20_BASE_VOLTAGE_ENTRY_V1* voltage = 
                    &ps20info.pstates [pstate].baseVoltages [volt];

                  gpu_stats.gpus [i].volts_mV.core = voltage->volt_uV/1000.0f;

                  int over  =
                    voltage->voltDelta_uV.value -
                    voltage->voltDelta_uV.valueRange.max;

                  int under =
                    voltage->voltDelta_uV.valueRange.min -
                    voltage->voltDelta_uV.value;

                  if (over > 0) {
                    gpu_stats.gpus [i].volts_mV.ov   =   over  / 1000.0f;
                    gpu_stats.gpus [i].volts_mV.over = true;
                  } else if (under > 0) {
                    gpu_stats.gpus [i].volts_mV.ov   = -(under / 1000.0f);
                    gpu_stats.gpus [i].volts_mV.over = true;
                  }
                  break;
                }
              }
              break;
            }
          }
        }
      }
    }
  }
}