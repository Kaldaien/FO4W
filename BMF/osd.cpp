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

#define OSD_M_PRINTF if (config.mem_stats)    { pszOSD += sprintf (pszOSD,
#define OSD_B_PRINTF if (config.load_balance) { pszOSD += sprintf (pszOSD,
#define OSD_S_PRINTF if (config.mem_stats &&\
                         config.sli_stats)    { pszOSD += sprintf (pszOSD,
#define OSD_C_PRINTF if (config.cpu_stats)    { pszOSD += sprintf (pszOSD,
#define OSD_D_PRINTF if (config.disk_stats)   { pszOSD += sprintf (pszOSD,
#define OSD_P_PRINTF if (config.pagefile_stats)\
                                              { pszOSD += sprintf (pszOSD,
#define OSD_I_PRINTF if (config.io_stats)     { pszOSD += sprintf (pszOSD,
#define OSD_END    ); }

static char szOSD [4096];

#include "nvapi.h"
extern NV_GET_CURRENT_SLI_STATE sli_state;
extern BOOL nvapi_init;

// Probably need to use a critical section to make this foolproof, we will
//   cross that bridge later though. The OSD is performance critical
static bool osd_shutting_down = false;

BOOL
BMF_ReleaseSharedMemory (LPVOID lpMemory)
{
  if (lpMemory != nullptr) {
    UnmapViewOfFile (lpMemory);
    return TRUE;
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
    }

    // We got a pointer, but... it was not to useable RivaTuner memory
    UnmapViewOfFile (pMapAddr);
  }

  return nullptr;
}

LPVOID
BMF_GetSharedMemory (void)
{
  return BMF_GetSharedMemory (GetCurrentProcessId ());
}

BOOL
BMF_DrawOSD (void)
{
  // Bail-out early when shutting down, or RTSS does not know about our process
  LPVOID pMemory = BMF_GetSharedMemory ();

  if (! pMemory)
    return false;

  char* pszOSD = szOSD;
  *pszOSD = '\0';

  static io_perf_t
    io_counter;

  buffer_t buffer = mem_info [0].buffer;

  int nodes = mem_info [buffer].nodes;

  if (nodes > 0) {
    int i = 0;

    int afr_idx  = sli_state.currentAFRIndex,
        afr_last = sli_state.previousFrameAFRIndex,
        afr_next = sli_state.nextFrameAFRIndex;

    OSD_M_PRINTF "\n"
                   "----- [DXGI 1.4]: Local Memory -----------"
                   "------------------------------------------"
                   "-\n"
    OSD_END

    while (i < nodes) {
      OSD_M_PRINTF "  %8s %u  (Reserve:  %05llu / %05llu MiB  - "
                   " Budget:  %05llu / %05llu MiB)",
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
      if (afr_last == i) {
        OSD_S_PRINTF "@"
        OSD_END
      }

      if (afr_idx == i) {
        OSD_S_PRINTF "!"
        OSD_END
      }

      if (afr_next == i) {
        OSD_S_PRINTF "#"
        OSD_END
      }
        
      OSD_M_PRINTF "\n"
      OSD_END

      i++;
    }

    i = 0;

    OSD_M_PRINTF "----- [DXGI 1.4]: Non-Local Memory -------"
                 "------------------------------------------"
                 "\n"
    OSD_END

    while (i < nodes) {
      OSD_M_PRINTF "  %8s %u  (Reserve:  %05llu / %05llu MiB  -  "
                   "Budget:  %05llu / %05llu MiB)\n",
                       nodes > 1 ? "SLI Node" : "GPU",
                       i,
              mem_info [buffer].nonlocal [i].CurrentReservation      >> 20ULL,
              mem_info [buffer].nonlocal [i].AvailableForReservation >> 20ULL,
              mem_info [buffer].nonlocal [i].CurrentUsage            >> 20ULL,
              mem_info [buffer].nonlocal [i].Budget                  >> 20ULL
      OSD_END

      i++;
    }

    OSD_M_PRINTF "----- [DXGI 1.4]: Miscellaneous ----------"
                 "------------------------------------------"
                 "---\n"
    OSD_END

    int64_t headroom = mem_info [buffer].local [0].Budget -
                       mem_info [buffer].local [0].CurrentUsage;

    OSD_M_PRINTF "  Max. Resident Set:  %05llu MiB  -"
                 "  Max. Over Budget:  %05llu MiB\n"
                 "    Budget Changes:  %06llu       -    "
                 "       Budget Left:  %05lli MiB\n",
                                    mem_stats [0].max_usage       >> 20ULL,
                                    mem_stats [0].max_over_budget >> 20ULL,
                                    mem_stats [0].budget_changes,
                                    headroom / 1024 / 1024
    OSD_END
  }
  
  extern int gpu_prio;

  OSD_B_PRINTF "\n  GPU Priority: %+1i\n",
    gpu_prio
  OSD_END

  BMF_CountIO (io_counter, config.io_interval / 1.0e-7);

  OSD_I_PRINTF "\n  Read..: %#6.02f MiB - (%#6.01f IOPs)"
               "\n  Write.: %#6.02f MiB - (%#6.01f IOPs)"
               "\n  Other.: %#6.02f MiB - (%#6.01f IOPs)\n",
               io_counter.read_mb_sec,  io_counter.read_iop_sec,
               io_counter.write_mb_sec, io_counter.write_iop_sec,
               io_counter.other_mb_sec, io_counter.other_iop_sec
  OSD_END

#if 0
  bool use_mib_sec = disk_stats.num_disks > 0 ?
                       (disk_stats.disks [0].bytes_sec > (1024 * 1024 * 2)) : false;

  if (use_mib_sec) {
#endif
    for (int i = 0; i < disk_stats.num_disks; i++) {
      OSD_D_PRINTF "\n  Disk %16s %#3llu%%  -  (Read: %#3llu%%   Write: %#3llu%%) - "
                                     "(Read: %#5.01f MiB   Write: %#5.01f MiB)",
        disk_stats.disks [i].name,
          disk_stats.disks [i].percent_load, 
            disk_stats.disks [i].percent_read,
              disk_stats.disks [i].percent_write,
                (float)disk_stats.disks [i].read_bytes_sec / (1024.0f * 1024.0f),
                (float)disk_stats.disks [i].write_bytes_sec / (1024.0f * 1024.0f)
      OSD_END

      if (i == 0) {
        OSD_D_PRINTF "\n"
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

      if (i == 0) {
        OSD_D_PRINTF "\n"
        OSD_END
      }
    }
  }
#endif

  OSD_D_PRINTF "\n"
  OSD_END

  OSD_C_PRINTF "\n  Total %#3llu%%  -  (Kernel: %#3llu%%   User: %#3llu%%   "
                 "Interrupt: %#3llu%%)\n",
        cpu_stats.cpus [0].percent_load, 
          cpu_stats.cpus [0].percent_kernel, 
            cpu_stats.cpus [0].percent_user, 
              cpu_stats.cpus [0].percent_interrupt
  OSD_END

  for (int i = 1; i < cpu_stats.num_cpus; i++) {
    OSD_C_PRINTF "\n  CPU%d: %#3llu%%  -  (Kernel: %#3llu%%   User: %#3llu%%   "
                 "Interrupt: %#3llu%%)",
      i-1,
        cpu_stats.cpus [i].percent_load, 
          cpu_stats.cpus [i].percent_kernel, 
            cpu_stats.cpus [i].percent_user, 
              cpu_stats.cpus [i].percent_interrupt
    OSD_END
  }

  OSD_C_PRINTF "\n"
  OSD_END

  for (int i = 0; i < pagefile_stats.num_pagefiles; i++) {
    OSD_P_PRINTF "\n  Pagefile %16s %05.02f KiB / %05.02f KiB  (Peak: %05.02f KiB)",
      pagefile_stats.pagefiles [i].name,
        (float)pagefile_stats.pagefiles [i].usage / 1024.0f,
          (float)pagefile_stats.pagefiles [i].size / 1024.0f,
            (float)pagefile_stats.pagefiles [i].usage_peak / 1024.0f
    OSD_END

    if (i == 0) {
      OSD_P_PRINTF "\n"
      OSD_END
    }
  }

  OSD_P_PRINTF "\n"
  OSD_END

  bool ret = BMF_UpdateOSD (szOSD, pMemory);

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