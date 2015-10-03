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

#include "osd.h"

#include "RTSSSharedMemory.h"

#include <shlwapi.h>
#include <float.h>
#include <io.h>
#include <tchar.h>

#include "config.h"
#include "io_monitor.h"
#include "gpu_monitor.h"
#include "memory_monitor.h"

#define OSD_PRINTF   if (config.osd.show)     { pszOSD += sprintf (pszOSD,
#define OSD_M_PRINTF if (config.osd.show &&\
                         config.mem.show)     { pszOSD += sprintf (pszOSD,
#define OSD_B_PRINTF if (config.osd.show &&\
                         config.load_balance\
                         .use)                { pszOSD += sprintf (pszOSD,
#define OSD_S_PRINTF if (config.osd.show &&\
                         config.mem.show &&\
                         config.sli.show)     { pszOSD += sprintf (pszOSD,
#define OSD_C_PRINTF if (config.osd.show &&\
                         config.cpu.show)     { pszOSD += sprintf (pszOSD,
#define OSD_G_PRINTF if (config.osd.show &&\
                         config.gpu.show)     { pszOSD += sprintf (pszOSD,
#define OSD_D_PRINTF if (config.osd.show &&\
                         config.disk.show)    { pszOSD += sprintf (pszOSD,
#define OSD_P_PRINTF if (config.osd.show &&\
                         config.pagefile.show)\
                                              { pszOSD += sprintf (pszOSD,
#define OSD_I_PRINTF if (config.osd.show &&\
                         config.io.show)      { pszOSD += sprintf (pszOSD,
#define OSD_END    ); }

static char szOSD [4096];

#include "nvapi.h"
extern NV_GET_CURRENT_SLI_STATE sli_state;
extern BOOL nvapi_init;

// Probably need to use a critical section to make this foolproof, we will
//   cross that bridge later though. The OSD is performance critical
static bool osd_shutting_down = false;

// Initialize some things (like color, position and scale) on first use
static bool osd_init          = false;

BOOL
BMF_ReleaseSharedMemory (LPVOID lpMemory)
{
  if (lpMemory != nullptr) {
    return UnmapViewOfFile (lpMemory);
  }

  return FALSE;
}

LPVOID
BMF_GetSharedMemory (DWORD dwProcID)
{
  if (osd_shutting_down)
    return nullptr;

  HANDLE hMapFile =
    OpenFileMappingA (FILE_MAP_ALL_ACCESS, FALSE, "RTSSSharedMemoryV2");

  if (hMapFile) {
    LPVOID               pMapAddr =
      MapViewOfFile (hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0);

    // We got our pointer, now close the file... we'll clean this pointer up later
    CloseHandle (hMapFile);

    LPRTSS_SHARED_MEMORY pMem     =
      (LPRTSS_SHARED_MEMORY)pMapAddr;

    if (pMem)
    {
      if ((pMem->dwSignature == 'RTSS') && 
          (pMem->dwVersion >= 0x00020000))
      {
        // ProcID is a wild-card, just return memory without checking to see if RTSS
        //   knows about a particular process.
        if (dwProcID == 0)
          return pMapAddr;

        for (DWORD dwApp = 0; dwApp < pMem->dwAppArrSize; dwApp++)
        {
          RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_APP_ENTRY pApp =
            (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_APP_ENTRY)
              ((LPBYTE)pMem + pMem->dwAppArrOffset +
                      dwApp * pMem->dwAppEntrySize);

          if (pApp->dwProcessID == dwProcID)
          {
#if 0
            wchar_t wszFlags [1024];
            wsprintf (wszFlags, L"Flags=%X, StatFlags=%X, ScreenCaptureFlags=%X",
               pApp->dwFlags, pApp->dwStatFlags, pApp->dwScreenCaptureFlags);
            if (pApp->dwFlags != 0x1000000 &&
              pApp->dwFlags != 0x0 || pApp->dwStatFlags != 0x0)
            MessageBox(NULL, wszFlags, L"OSD Flags", MB_OK);
            //pApp->dwFlags &= ~0x1000000;
            //pApp->
            //sprintf (pEntry->szOSDEx, ");
            //*((float *)&pApp->dwOSDPixel) = 200.0f;
            //pApp->dwOSDPixel++;
            //pApp->dwOSDX++;
            //pApp->dwOSDY++;
            //pApp->dwOSDColor     = 0xffffffff;
            //pApp->dwOSDBgndColor = 0xffffffff;
            //pApp->dwFlags |= OSDFLAG_UPDATED;
#endif
            // Everything is good and RTSS knows about dwProcID!
            return pMapAddr;
          }
        }
      }

      // We got a pointer, but... it was not to useable RivaTuner memory
      UnmapViewOfFile (pMapAddr);
    }
  }

  return nullptr;
}

LPVOID
BMF_GetSharedMemory (void)
{
  return BMF_GetSharedMemory (GetCurrentProcessId ());
}

#include "log.h"

std::wstring
BMF_GetAPINameFromOSDFlags (DWORD dwFlags)
{
  // Both are DXGI-based and probable
  if (dwFlags & APPFLAG_D3D11)
    return L"D3D11";
  if (dwFlags & APPFLAG_D3D10)
    return L"D3D10";

  // Never going to happen
#ifdef HELL_FROZEN_OVER
  if (dwFlags & APPFLAG_OGL)
    return L"OpenGL";
#endif

  // Plan to expand this to D3D9 eventually
#ifdef OLDER_D3D_SUPPORT
  if (dwFlags & APPFLAG_D3D9EX)
    return L"D3D9EX";
  if (dwFlags & APPFLAG_D3D9)
    return L"D3D9";
  if (dwFlags & APPFLAG_D3D8)
    return L"D3D8";
  if (dwFlags & APPFLAG_DD)
    return L"DDRAW";
#endif

  return L"UNKNOWN";
}

enum BMF_UNITS {
  Celsius    = 0,
  Fahrenheit = 1,
  B          = 2,
  KiB        = 3,
  MiB        = 4,
  GiB        = 5,
  Auto       = 32
};

std::wstring
BMF_SizeToString (uint64_t size, BMF_UNITS unit = Auto)
{
  wchar_t str [64];

  if (unit == Auto) {
    if      (size > (1ULL << 32ULL)) unit = GiB;
    else if (size > (1ULL << 22ULL)) unit = MiB;
    else if (size > (1ULL << 12ULL)) unit = KiB;
    else                             unit = B;
  }

  switch (unit)
  {
    case GiB:
      swprintf (str, L"%#5llu GiB", size >> 30);
      break;
    case MiB:
      swprintf (str, L"%#5llu MiB", size >> 20);
      break;
    case KiB:
      swprintf (str, L"%#5llu KiB", size >> 10);
      break;
    case B:
    default:
      swprintf (str, L"%#3llu Bytes", size);
      break;
  }

  return str;
}

std::wstring
BMF_SizeToStringF (uint64_t size, int width, int precision, BMF_UNITS unit = Auto)
{
  wchar_t str [64];

  if (unit == Auto) {
    if      (size > (1ULL << 32ULL)) unit = GiB;
    else if (size > (1ULL << 22ULL)) unit = MiB;
    else if (size > (1ULL << 12ULL)) unit = KiB;
    else                             unit = B;
  }

  switch (unit)
  {
  case GiB:
    swprintf (str, L"%#*.*f GiB", width, precision,
             (float)size / (1024.0f * 1024.0f * 1024.0f));
    break;
  case MiB:
    swprintf (str, L"%#*.*f MiB", width, precision,
             (float)size / (1024.0f * 1024.0f));
    break;
  case KiB:
    swprintf (str, L"%#*.*f KiB", width, precision, (float)size / 1024.0f);
    break;
  case B:
  default:
    swprintf (str, L"%#*llu Bytes", width-1-precision, size);
    break;
  }

  return str;
}

std::wstring
BMF_FormatTemperature (int32_t in_temp, BMF_UNITS in_unit, BMF_UNITS out_unit)
{
  int32_t converted;
  wchar_t wszOut [8];

  if (in_unit == Celsius && out_unit == Fahrenheit) {
    //converted = in_temp * 2 + 30;
    converted = (int32_t)((float)(in_temp) * 9.0f/5.0f + 32.0f);
    swprintf (wszOut, L"%#3lu°F", converted);
  } else if (in_unit == Fahrenheit && out_unit == Celsius) {
    converted = (int32_t)(((float)in_temp - 32.0f) * (5.0f/9.0f));
    swprintf (wszOut, L"%#2lu°C", converted);
  } else {
    swprintf (wszOut, L"%#2lu°C", in_temp);
  }

  return wszOut;
}

BOOL
BMF_DrawOSD (void)
{
  static int connect_attempts = 1;

  // Bail-out early when shutting down, or RTSS does not know about our process
  LPVOID pMemory = BMF_GetSharedMemory ();

  if (! pMemory) {
    ++connect_attempts;
    return false;
  }

  if (! osd_init) {
    osd_init = true;

    extern bmf_logger_t dxgi_log;

    dxgi_log.LogEx ( true,
      L" [RTSS] Opening Connection to RivaTuner Statistics Server... " );

    dxgi_log.LogEx ( false,
      L"successful after %u attempt(s)!\n", connect_attempts );

    BMF_SetOSDScale (config.osd.scale);
    BMF_SetOSDPos   (config.osd.pos_x, config.osd.pos_y);
    BMF_SetOSDColor (config.osd.red, config.osd.green, config.osd.blue);
  }

  char* pszOSD = szOSD;
  *pszOSD = '\0';

  static io_perf_t
    io_counter;

  buffer_t buffer = mem_info [0].buffer;
  int      nodes  = mem_info [buffer].nodes;

  extern std::wstring BMF_VER_STR;

  if (config.time.show)
  {
    SYSTEMTIME st;
    GetLocalTime (&st);

    wchar_t time [64];
    GetTimeFormat (config.time.format,0L,&st,NULL,time,64);

    OSD_PRINTF "Batman \"Fix\" v %ws   %ws\n\n",
      BMF_VER_STR.c_str (), time
    OSD_END
  }

  if (config.fps.show)
  {
    LPRTSS_SHARED_MEMORY pMem =
      (LPRTSS_SHARED_MEMORY)pMemory;

    if (pMem)
    {
      if ((pMem->dwSignature == 'RTSS') && 
          (pMem->dwVersion >= 0x00020000))
      {
        for (DWORD dwApp = 0; dwApp < pMem->dwAppArrSize; dwApp++)
        {
          RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_APP_ENTRY pApp =
            (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_APP_ENTRY)
              ((LPBYTE)pMem + pMem->dwAppArrOffset +
                      dwApp * pMem->dwAppEntrySize);

          //
          // Print the API Statistics and Framerate
          //
          if (pApp->dwProcessID == GetCurrentProcessId ())
          {
            OSD_PRINTF "  %-6ws :  %#4.01f FPS, %#13.01f ms\n",
              BMF_GetAPINameFromOSDFlags (pApp->dwFlags).c_str (),
                // Cast to FP to avoid integer division by zero.
                1000.0f * (float)pApp->dwFrames / (float)(pApp->dwTime1 - pApp->dwTime0),
                  pApp->dwFrameTime / 1000.0f
                //1000000.0f / pApp->dwFrameTime,
                  //pApp->dwFrameTime / 1000.0f
            OSD_END
            break;
          }
        }
      }
    }
  }

  // Poll GPU stats...
  BMF_PollGPU ();

  for (int i = 0; i < gpu_stats.num_gpus; i++) {
    OSD_G_PRINTF "  GPU%u   :            %#3lu%%",
      i, gpu_stats.gpus [i].loads_percent.gpu
    OSD_END

    if (gpu_stats.gpus [i].loads_percent.vid > 0) {
      OSD_G_PRINTF ",  VID%u %#3lu%%  ,",
        i, gpu_stats.gpus [i].loads_percent.vid
      OSD_END
    } else {
      OSD_G_PRINTF ",              " OSD_END
    }

    OSD_G_PRINTF " %#4lu MHz",
          gpu_stats.gpus [i].clocks_kHz.gpu / 1000UL
    OSD_END

    if (gpu_stats.gpus [i].volts_mV.supported)
    {
      // Over (or under) voltage limit!
      if (false)//gpu_stats.gpus [i].volts_mV.over)
      {
        OSD_G_PRINTF ", %#6.1fmV (%+#6.1fmV)",
          gpu_stats.gpus [i].volts_mV.core, gpu_stats.gpus [i].volts_mV.ov
        OSD_END
      } else {
        OSD_G_PRINTF ", %#6.1fmV",
          gpu_stats.gpus [i].volts_mV.core
        OSD_END
      }
    }

    if (gpu_stats.gpus [i].fans_rpm.supported)
    {
      OSD_G_PRINTF ", %#4lu RPM",
        gpu_stats.gpus [i].fans_rpm.gpu
      OSD_END
    }


    OSD_G_PRINTF ", (%ws)",
      BMF_FormatTemperature (
        gpu_stats.gpus [i].temps_c.gpu,
          Celsius,
            config.system.prefer_fahrenheit ? Fahrenheit :
                                              Celsius ).c_str ()
    OSD_END

    if (config.gpu.print_slowdown &&
        gpu_stats.gpus [i].nv_perf_state != NV_GPU_PERF_DECREASE_NONE) {
      OSD_G_PRINTF "   SLOWDOWN:" OSD_END

      if (gpu_stats.gpus [i].nv_perf_state & NV_GPU_PERF_DECREASE_REASON_AC_BATT)
        OSD_G_PRINTF " (Battery)" OSD_END
      if (gpu_stats.gpus [i].nv_perf_state & NV_GPU_PERF_DECREASE_REASON_API_TRIGGERED)
        OSD_G_PRINTF " (Driver)" OSD_END
      if (gpu_stats.gpus [i].nv_perf_state & NV_GPU_PERF_DECREASE_REASON_INSUFFICIENT_POWER)
        OSD_G_PRINTF " (Power Supply)" OSD_END
      if (gpu_stats.gpus [i].nv_perf_state & NV_GPU_PERF_DECREASE_REASON_POWER_CONTROL)
        OSD_G_PRINTF " (Power Limit)" OSD_END
      if (gpu_stats.gpus [i].nv_perf_state & NV_GPU_PERF_DECREASE_REASON_THERMAL_PROTECTION)
        OSD_G_PRINTF " (Thermal Limit)" OSD_END
    }

    OSD_G_PRINTF "\n" OSD_END
  }

  //
  // DXGI 1.4 Memory Info (VERY accurate)
  ///
  if (nodes > 0) {
    // We need to be careful here, it's not guaranteed that NvAPI adapter indices
    //   match up with DXGI 1.4 node indices... Adapter LUID may shed some light
    //     on that in the future.
    for (int i = 0; i < nodes; i++) {
#if 1
      OSD_G_PRINTF "  VRAM%u  : %#5llu MiB (%#3lu%%: %#5.01lf GiB/s)",
        i,
        mem_info [buffer].local    [i].CurrentUsage >> 20ULL,
                    gpu_stats.gpus [i].loads_percent.fb,
        (double)((uint64_t)gpu_stats.gpus [i].clocks_kHz.ram * 2ULL * 1000ULL *
                 (uint64_t)gpu_stats.gpus [i].hwinfo.mem_bus_width) / 8.0 /
                   (1024.0 * 1024.0 * 1024.0) *
                  ((double)gpu_stats.gpus [i].loads_percent.fb / 100.0)
      OSD_END

      OSD_G_PRINTF ", %#4lu MHz",
        gpu_stats.gpus [i].clocks_kHz.ram / 1000UL
      OSD_END

      // Add memory temperature if it exists
      if (i <= gpu_stats.num_gpus &&
          gpu_stats.gpus [i].temps_c.ram != 0) {
        OSD_G_PRINTF " (%#3luC)",
          gpu_stats.gpus [i].temps_c.ram
        OSD_END
      }

      OSD_G_PRINTF "\n" OSD_END
#else
      OSD_G_PRINTF "  MEM%u %#5.01lf GiB/s",
        i,
        (double)((uint64_t)gpu_stats.gpus [i].clocks_kHz.ram * 2ULL * 1000ULL *
          (uint64_t)gpu_stats.gpus [i].hwinfo.mem_bus_width) / 8.0 /
        (1024.0 * 1024.0 * 1024.0) *
        ((double)gpu_stats.gpus [i].loads_percent.fb / 100.0)
        OSD_END
      OSD_G_PRINTF ", VRAM%u %#4llu MiB, SHARED%u %#3llu MiB",
        i, mem_info [buffer].local    [i].CurrentUsage >> 20ULL,
        i, mem_info [buffer].nonlocal [i].CurrentUsage >> 20ULL
      OSD_END

      OSD_G_PRINTF ", %#4lu MHz",
       gpu_stats.gpus [i].clocks_kHz.ram / 1000UL
      OSD_END

      // Add memory temperature if it exists
      if (i <= gpu_stats.num_gpus &&
        gpu_stats.gpus [i].temps_c.ram != 0) {
        OSD_G_PRINTF " (%#3luC)",
          gpu_stats.gpus [i].temps_c.ram
          OSD_END
      }
#endif
    }

    for (int i = 0; i < nodes; i++) {
      OSD_G_PRINTF "  SHARE%u : %#5llu MiB (%#3lu%%: %#5.02lf GiB/s), PCIe %lu.0@x%lu\n",
        i,
         mem_info [buffer].nonlocal [i].CurrentUsage >> 20ULL,
                       gpu_stats.gpus [i].loads_percent.bus,
                       gpu_stats.gpus [i].hwinfo.pcie_bandwidth_mb () / 1024.0 *
              ((double)gpu_stats.gpus [i].loads_percent.bus / 100.0),
                       gpu_stats.gpus [i].hwinfo.pcie_gen >= 1 ?
                       gpu_stats.gpus [i].hwinfo.pcie_gen : 1,
                       gpu_stats.gpus [i].hwinfo.pcie_lanes
      OSD_END
    }
  }

  //
  // NvAPI Memory Info (Reasonably Accurate on Windows 8.1 and older)
  //
  else {
    OSD_G_PRINTF "\n" OSD_END

    // We need to be careful here, it's not guaranteed that NvAPI adapter indices
    //   match up with DXGI 1.4 node indices... Adapter LUID may shed some light
    //     on that in the future.
    for (int i = 0; i < gpu_stats.num_gpus; i++) {
      OSD_G_PRINTF "  MEM%d %#4lu MHz, VRAM%d %#4llu MiB, SHARED%d %#3llu MiB",
        i, gpu_stats.gpus[i].clocks_kHz.ram / 1000UL,
        i, gpu_stats.gpus [i].memory_B.local    >> 20ULL,
        i, gpu_stats.gpus [i].memory_B.nonlocal >> 20ULL
      OSD_END

      // Add memory temperature if it exists
      if (gpu_stats.gpus [i].temps_c.ram != 0) {
        OSD_G_PRINTF " (%#3luC)",
          gpu_stats.gpus [i].temps_c.ram
        OSD_END
      }

      OSD_G_PRINTF "\n" OSD_END
    }
  }

  //OSD_G_PRINTF "\n" OSD_END

  if (nodes > 0) {
    int i = 0;

    int afr_idx  = sli_state.currentAFRIndex,
        afr_last = sli_state.previousFrameAFRIndex,
        afr_next = sli_state.nextFrameAFRIndex;

    OSD_M_PRINTF "\n"
                   "----- (DXGI 1.4): Local Memory -------"
                   "--------------------------------------\n"
    OSD_END

    while (i < nodes) {
      OSD_M_PRINTF "  %8s %u  (Reserve:  %#5llu / %#5llu MiB  - "
                   " Budget:  %#5llu / %#5llu MiB)",
                  nodes > 1 ? (nvapi_init ? "SLI Node" : "CFX Node") : "GPU",
                  i,
                  mem_info [buffer].local [i].CurrentReservation      >> 20ULL,
                  mem_info [buffer].local [i].AvailableForReservation >> 20ULL,
                  mem_info [buffer].local [i].CurrentUsage            >> 20ULL,
                  mem_info [buffer].local [i].Budget                  >> 20ULL
      OSD_END

      //
      // SLI Status Indicator
      //
      if (afr_last == i)
        OSD_S_PRINTF "@" OSD_END

      if (afr_idx == i)
        OSD_S_PRINTF "!" OSD_END

      if (afr_next == i)
        OSD_S_PRINTF "#" OSD_END

      OSD_M_PRINTF "\n" OSD_END

      i++;
    }

    i = 0;

    OSD_M_PRINTF "----- (DXGI 1.4): Non-Local Memory ---"
                 "--------------------------------------\n"
    OSD_END

    while (i < nodes) {
      if ((mem_info [buffer].nonlocal [i].CurrentUsage >> 20ULL) > 0) {
        OSD_M_PRINTF "  %8s %u  (Reserve:  %#5llu / %#5llu MiB  -  "
                     "Budget:  %#5llu / %#5llu MiB)\n",
                         nodes > 1 ? "SLI Node" : "GPU",
                         i,
                mem_info [buffer].nonlocal [i].CurrentReservation      >> 20ULL,
                mem_info [buffer].nonlocal [i].AvailableForReservation >> 20ULL,
                mem_info [buffer].nonlocal [i].CurrentUsage            >> 20ULL,
                mem_info [buffer].nonlocal [i].Budget                  >> 20ULL
        OSD_END
      }

      i++;
    }

    OSD_M_PRINTF "----- (DXGI 1.4): Miscellaneous ------"
                 "--------------------------------------\n"
    OSD_END

    int64_t headroom = mem_info [buffer].local [0].Budget -
                       mem_info [buffer].local [0].CurrentUsage;

    OSD_M_PRINTF "  Max. Resident Set:  %#5llu MiB  -"
                 "  Max. Over Budget:  %#5llu MiB\n"
                 "     Budget Changes:  %#5llu      - "
                  "      Budget Left:  %#5lli MiB\n",
                                    mem_stats [0].max_usage       >> 20ULL,
                                    mem_stats [0].max_over_budget >> 20ULL,
                                    mem_stats [0].budget_changes,
                                    headroom / 1024 / 1024
    OSD_END
  }

  OSD_M_PRINTF "\n" OSD_END

  OSD_M_PRINTF "  Working Set: %ws,  Committed: %ws,  Address Space: %ws\n",
    BMF_SizeToString (process_stats.memory.working_set,   MiB).c_str (),
    BMF_SizeToString (process_stats.memory.private_bytes, MiB).c_str (),
    BMF_SizeToString (process_stats.memory.virtual_bytes, MiB).c_str ()
  OSD_END
  OSD_M_PRINTF "        *Peak: %ws,      *Peak: %ws,          *Peak: %ws\n",
    BMF_SizeToString (process_stats.memory.working_set_peak,     MiB).c_str (),
    BMF_SizeToString (process_stats.memory.page_file_bytes_peak, MiB).c_str (),
    BMF_SizeToString (process_stats.memory.virtual_bytes_peak,   MiB).c_str ()
  OSD_END

  extern int gpu_prio;

  OSD_B_PRINTF "\n  GPU Priority: %+1i\n",
    gpu_prio
  OSD_END

  // Only do this if the IO data view is active
  if (config.io.show)
    BMF_CountIO (io_counter, config.io.interval / 1.0e-7);

  OSD_I_PRINTF "\n  Read   :%#6.02f MiB/s - (%#6.01f IOP/s)"
               "\n  Write  :%#6.02f MiB/s - (%#6.01f IOP/s)"
               "\n  Other  :%#6.02f MiB/s - (%#6.01f IOP/s)\n",
               io_counter.read_mb_sec,  io_counter.read_iop_sec,
               io_counter.write_mb_sec, io_counter.write_iop_sec,
               io_counter.other_mb_sec, io_counter.other_iop_sec
  OSD_END

#if 0
  bool use_mib_sec = disk_stats.num_disks > 0 ?
                       (disk_stats.disks [0].bytes_sec > (1024 * 1024 * 2)) : false;

  if (use_mib_sec) {
#endif
    for (DWORD i = 0; i < disk_stats.num_disks; i++) {
      if (i == 0) {
        OSD_D_PRINTF "\n  Disk %16s %#3llu%%  -  (Read %#3llu%%: %ws/s, "
                                                 "Write %#3llu%%: %ws/s)\n",
          disk_stats.disks [i].name,
            disk_stats.disks [i].percent_load,
              disk_stats.disks [i].percent_read,
                BMF_SizeToStringF (
                  disk_stats.disks [i].read_bytes_sec, 6, 1).c_str (),
                    disk_stats.disks [i].percent_write,
                      BMF_SizeToStringF (
                        disk_stats.disks [i].write_bytes_sec, 6, 1).c_str ()
        OSD_END
      }
      else {
        OSD_D_PRINTF "  Disk %-16s %#3llu%%  -  (Read %#3llu%%: %ws/s, "
                                                "Write %#3llu%%: %ws/s)\n",
          disk_stats.disks [i].name,
            disk_stats.disks [i].percent_load,
              disk_stats.disks [i].percent_read,
                BMF_SizeToStringF (
                  disk_stats.disks [i].read_bytes_sec, 6, 1).c_str (),
                    disk_stats.disks [i].percent_write,
                      BMF_SizeToStringF (
                        disk_stats.disks [i].write_bytes_sec, 6, 1).c_str ()
        OSD_END
      }
    }
#if 0
  }
  else
  {
    for (int i = 0; i < disk_stats.num_disks; i++) {
      OSD_D_PRINTF "\n  Disk %16s %#3llu%%  -  (Read: %#3llu%%   Write: %#3llu%%) - "
                                        "(Read: %#5.01f KiB   Write: %#5.01f KiB)",
        disk_stats.disks[i].name,
          disk_stats.disks[i].percent_load,
            disk_stats.disks[i].percent_read,
              disk_stats.disks[i].percent_write,
                (float)disk_stats.disks[i].read_bytes_sec / (1024.0f),
                (float)disk_stats.disks[i].write_bytes_sec / (1024.0f)
      OSD_END

      if (i == 0)
        OSD_D_PRINTF "\n" OSD_END
    }
  }
#endif

  OSD_C_PRINTF "\n  Total %#3llu%%  -  (Kernel: %#3llu%%   User: %#3llu%%   "
                 "Interrupt: %#3llu%%)\n",
        cpu_stats.cpus [0].percent_load, 
          cpu_stats.cpus [0].percent_kernel, 
            cpu_stats.cpus [0].percent_user, 
              cpu_stats.cpus [0].percent_interrupt
  OSD_END

  for (DWORD i = 1; i < cpu_stats.num_cpus; i++) {
    OSD_C_PRINTF "  CPU%d: %#3llu%%  -  (Kernel: %#3llu%%   User: %#3llu%%   "
                 "Interrupt: %#3llu%%)\n",
      i-1,
        cpu_stats.cpus [i].percent_load, 
          cpu_stats.cpus [i].percent_kernel, 
            cpu_stats.cpus [i].percent_user, 
              cpu_stats.cpus [i].percent_interrupt
    OSD_END
  }

  for (DWORD i = 0; i < pagefile_stats.num_pagefiles; i++) {
      OSD_P_PRINTF "\n  Pagefile %20s  %ws / %ws  (Peak: %ws)",
        pagefile_stats.pagefiles [i].name,
          BMF_SizeToStringF (pagefile_stats.pagefiles [i].usage, 5,2).c_str (),
            BMF_SizeToStringF (
              pagefile_stats.pagefiles [i].size, 5,2).c_str (),
                BMF_SizeToStringF (
                  pagefile_stats.pagefiles [i].usage_peak, 5,2).c_str ()
      OSD_END
  }

  OSD_P_PRINTF "\n" OSD_END

  BOOL ret = BMF_UpdateOSD (szOSD, pMemory);

  BMF_ReleaseSharedMemory (pMemory);

  return ret;
}

BOOL
BMF_UpdateOSD (LPCSTR lpText, LPVOID pMapAddr)
{
  static DWORD dwProcID =
    GetCurrentProcessId ();

  BOOL bResult = FALSE;

  if (osd_shutting_down)
    return bResult;

  // If pMapAddr == nullptr, manage memory ourselves
  bool own_memory = false;
  if (pMapAddr == nullptr) {
    pMapAddr = BMF_GetSharedMemory (dwProcID);
    own_memory = true;
  }

  LPRTSS_SHARED_MEMORY pMem     =
    (LPRTSS_SHARED_MEMORY)pMapAddr;

  if (pMem)
  {
    for (DWORD dwPass = 0; dwPass < 2; dwPass++)
    {
      //1st Pass: Find previously captured OSD slot
      //2nd Pass: Otherwise find the first unused OSD slot and capture it

      for (DWORD dwEntry = 1; dwEntry < pMem->dwOSDArrSize; dwEntry++)
      {
        // Allow primary OSD clients (i.e. EVGA Precision / MSI Afterburner)
        //   to use the first slot exclusively, so third party applications
        //     start scanning the slots from the second one

        RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry =
          (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)
            ((LPBYTE)pMem + pMem->dwOSDArrOffset +
                  dwEntry * pMem->dwOSDEntrySize);

        if (dwPass)
        {
          if (! strlen (pEntry->szOSDOwner))
            strcpy (pEntry->szOSDOwner, "Batman Fix");
        }

        if (! strcmp (pEntry->szOSDOwner, "Batman Fix"))
        {
          if (pMem->dwVersion >= 0x00020007)
            //Use extended text slot for v2.7 and higher shared memory,
            // it allows displaying 4096 symbols instead of 256 for regular
            //  text slot
            strncpy (pEntry->szOSDEx, lpText, sizeof pEntry->szOSDEx - 1);
            //snprintf (pEntry->szOSDEx, sizeof pEntry->szOSDEx - 1, "Frame: %d\n%s", pMem->dwOSDFrame, lpText);
          else
            strncpy (pEntry->szOSD,   lpText, sizeof pEntry->szOSD   - 1);

          pMem->dwOSDFrame++;

          bResult = TRUE;
          break;
        }
      }

      if (bResult)
        break;
    }
  }

  if (own_memory)
    BMF_ReleaseSharedMemory (pMapAddr);

  return bResult;
}

void
BMF_ReleaseOSD (void)
{
  LPVOID pMapAddr =
    BMF_GetSharedMemory ();

  osd_shutting_down = true;

  LPRTSS_SHARED_MEMORY pMem     =
    (LPRTSS_SHARED_MEMORY)pMapAddr;

  if (pMem)
  {
    for (DWORD dwEntry = 1; dwEntry < pMem->dwOSDArrSize; dwEntry++)
    {
      RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry =
          (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)
            ((LPBYTE)pMem + pMem->dwOSDArrOffset +
                  dwEntry * pMem->dwOSDEntrySize);

      if (! strcmp (pEntry->szOSDOwner, "Batman Fix"))
      {
        memset (pEntry, 0, pMem->dwOSDEntrySize);
        pMem->dwOSDFrame++;
      }
    }

    BMF_ReleaseSharedMemory (pMapAddr);
  }
}


void
BMF_SetOSDPos (int x, int y)
{
  LPVOID pMapAddr =
    BMF_GetSharedMemory ();

  LPRTSS_SHARED_MEMORY pMem     =
    (LPRTSS_SHARED_MEMORY)pMapAddr;

  if (pMem)
  {
    for (DWORD dwEntry = 1; dwEntry < pMem->dwOSDArrSize; dwEntry++)
    {
      RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry =
        (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)
        ((LPBYTE)pMem + pMem->dwOSDArrOffset +
          dwEntry * pMem->dwOSDEntrySize);

      if (! strcmp (pEntry->szOSDOwner, "Batman Fix"))
      {
        for (DWORD dwApp = 0; dwApp < pMem->dwAppArrSize; dwApp++)
        {
          RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_APP_ENTRY pApp =
            (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_APP_ENTRY)
            ((LPBYTE)pMem + pMem->dwAppArrOffset +
              dwApp * pMem->dwAppEntrySize);

          if (pApp->dwProcessID == GetCurrentProcessId ())
          {
            config.osd.pos_x = x;
            config.osd.pos_y = y;

            pApp->dwOSDX = x;
            pApp->dwOSDX = y;

            pApp->dwFlags |= OSDFLAG_UPDATED;
            break;
          }
        }
        break;
      }
    }
  }
  BMF_ReleaseSharedMemory (pMapAddr);
}

void
BMF_SetOSDColor (int red, int green, int blue)
{
  LPVOID pMapAddr =
    BMF_GetSharedMemory ();

  LPRTSS_SHARED_MEMORY pMem     =
    (LPRTSS_SHARED_MEMORY)pMapAddr;

  if (pMem)
  {
    for (DWORD dwEntry = 1; dwEntry < pMem->dwOSDArrSize; dwEntry++)
    {
      RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry =
        (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)
        ((LPBYTE)pMem + pMem->dwOSDArrOffset +
          dwEntry * pMem->dwOSDEntrySize);

      if (! strcmp (pEntry->szOSDOwner, "Batman Fix"))
      {
        for (DWORD dwApp = 0; dwApp < pMem->dwAppArrSize; dwApp++)
        {
          RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_APP_ENTRY pApp =
            (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_APP_ENTRY)
            ((LPBYTE)pMem + pMem->dwAppArrOffset +
              dwApp * pMem->dwAppEntrySize);

          if (pApp->dwProcessID == GetCurrentProcessId ())
          {
            int red_   = (pApp->dwOSDColor >> 16) & 0xFF;
            int green_ = (pApp->dwOSDColor >>  8) & 0xFF;
            int blue_  = (pApp->dwOSDColor      ) & 0xFF;

            if (red >= 0 && red <= 255) {
              config.osd.red = red;
              red_ = red;
            }

            if (green >= 0 && green <= 255) {
              config.osd.green = green;
              green_ = green;
            }

            if (blue >= 0 && blue <= 255) {
              config.osd.blue = blue;
              blue_ = blue;
            }

            pApp->dwOSDColor = ((red_ << 16) & 0xff0000) | ((green_ << 8) & 0xff00) | (blue_ & 0xff);

            pApp->dwFlags |= OSDFLAG_UPDATED;
            break;
          }
        }
        break;
      }
    }
  }
  BMF_ReleaseSharedMemory (pMapAddr);
}

void
BMF_SetOSDScale (DWORD dwScale, bool relative)
{
  LPVOID pMapAddr =
    BMF_GetSharedMemory ();

  LPRTSS_SHARED_MEMORY pMem     =
    (LPRTSS_SHARED_MEMORY)pMapAddr;

  if (pMem)
  {
    for (DWORD dwEntry = 1; dwEntry < pMem->dwOSDArrSize; dwEntry++)
    {
      RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY pEntry =
        (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_OSD_ENTRY)
        ((LPBYTE)pMem + pMem->dwOSDArrOffset +
          dwEntry * pMem->dwOSDEntrySize);

      if (! strcmp (pEntry->szOSDOwner, "Batman Fix"))
      {
        for (DWORD dwApp = 0; dwApp < pMem->dwAppArrSize; dwApp++)
        {
          RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_APP_ENTRY pApp =
            (RTSS_SHARED_MEMORY::LPRTSS_SHARED_MEMORY_APP_ENTRY)
            ((LPBYTE)pMem + pMem->dwAppArrOffset +
              dwApp * pMem->dwAppEntrySize);

          if (pApp->dwProcessID == GetCurrentProcessId ())
          {
            if (! relative)
              pApp->dwOSDPixel = dwScale;

             else
               pApp->dwOSDPixel += dwScale;

            // Clamp to a sane range :)
            if (pApp->dwOSDPixel < 1)
              pApp->dwOSDPixel = 1;

            config.osd.scale = pApp->dwOSDPixel;

            pApp->dwFlags |= OSDFLAG_UPDATED;
            break;
          }
        }
        break;
      }
    }
  }
  BMF_ReleaseSharedMemory (pMapAddr);
}

void
BMF_ResizeOSD (int scale_incr)
{
  BMF_SetOSDScale (scale_incr, true);
}

