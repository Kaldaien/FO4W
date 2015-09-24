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

#include "io_monitor.h"

void
BMF_CountIO (io_perf_t& ioc, const double update)
{
  static HANDLE hProc = GetCurrentProcess ();

  if (ioc.init == false) {
    memset (&ioc, 0, sizeof (io_perf_t));
    ioc.init = true;
  }

  SYSTEMTIME     update_time;
  FILETIME       update_ftime;
  ULARGE_INTEGER update_ul;

  IO_COUNTERS current_io;

  GetProcessIoCounters (hProc, &current_io);
  GetSystemTime        (&update_time);
  SystemTimeToFileTime (&update_time, &update_ftime);

  update_ul.HighPart = update_ftime.dwHighDateTime;
  update_ul.LowPart  = update_ftime.dwLowDateTime;

  ioc.dt += update_ul.QuadPart - ioc.last_update.QuadPart;

  ioc.accum.ReadTransferCount +=
    current_io.ReadTransferCount - ioc.last_counter.ReadTransferCount;
  ioc.accum.WriteTransferCount +=
    current_io.WriteTransferCount - ioc.last_counter.WriteTransferCount;
  ioc.accum.OtherTransferCount +=
    current_io.OtherTransferCount - ioc.last_counter.OtherTransferCount;

  ioc.accum.ReadOperationCount +=
    current_io.ReadOperationCount - ioc.last_counter.ReadOperationCount;
  ioc.accum.WriteOperationCount +=
    current_io.WriteOperationCount - ioc.last_counter.WriteOperationCount;
  ioc.accum.OtherOperationCount +=
    current_io.OtherOperationCount - ioc.last_counter.OtherOperationCount;

  double dRB = (double)ioc.accum.ReadTransferCount;
  double dWB = (double)ioc.accum.WriteTransferCount;
  double dOB = (double)ioc.accum.OtherTransferCount;

  double dRC = (double)ioc.accum.ReadOperationCount;
  double dWC = (double)ioc.accum.WriteOperationCount;
  double dOC = (double)ioc.accum.OtherOperationCount;

  double& read_mb_sec  = ioc.read_mb_sec;
  double& write_mb_sec = ioc.write_mb_sec;
  double& other_mb_sec = ioc.other_mb_sec;

  double& read_iop_sec  = ioc.read_iop_sec;
  double& write_iop_sec = ioc.write_iop_sec;
  double& other_iop_sec = ioc.other_iop_sec;

  if (ioc.dt >= update) {
    read_mb_sec  = (
      read_mb_sec + ((dRB / 1048576.0) / (1.0e-7 * (double)ioc.dt))
                   ) / 2.0;
    write_mb_sec = (
      write_mb_sec + ((dWB / 1048576.0) / (1.0e-7 * (double)ioc.dt))
                   ) / 2.0;
    other_mb_sec = (
      other_mb_sec + ((dOB / 1048576.0) / (1.0e-7 * (double)ioc.dt))
                   ) / 2.0;

    read_iop_sec  = (read_iop_sec  + (dRC / (1.0e-7 * (double)ioc.dt))) / 2.0;
    write_iop_sec = (write_iop_sec + (dWC / (1.0e-7 * (double)ioc.dt))) / 2.0;
    other_iop_sec = (other_iop_sec + (dOC / (1.0e-7 * (double)ioc.dt))) / 2.0;

    ioc.accum.ReadTransferCount  = 0;
    ioc.accum.WriteTransferCount = 0;
    ioc.accum.OtherTransferCount = 0;

    ioc.accum.ReadOperationCount  = 0;
    ioc.accum.WriteOperationCount = 0;
    ioc.accum.OtherOperationCount = 0;
        
    ioc.dt = 0;
  }

  ioc.last_update.QuadPart = update_ul.QuadPart;
  memcpy (&ioc.last_counter, &current_io, sizeof (IO_COUNTERS));
}