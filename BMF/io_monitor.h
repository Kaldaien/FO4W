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
* along with Batman "Fix". If not, see <http://www.gnu.org/licenses/>.
**/

#ifndef __BMF__IO_MONITOR_H__
#define __BMF__IO_MONITOR_H__

#include <Windows.h>

struct io_perf_t {
  bool           init = false;
  ULARGE_INTEGER last_update;
  IO_COUNTERS    accum;
  ULONGLONG      dt;
  IO_COUNTERS    last_counter;

  double read_mb_sec  = 0.0;
  double write_mb_sec = 0.0;
  double other_mb_sec = 0.0;

  double read_iop_sec  = 0.0;
  double write_iop_sec = 0.0;
  double other_iop_sec = 0.0;
};

void BMF_CountIO (io_perf_t& ioc, const double update = 0.25 / 1.0e-7);

#endif /* __ BMF__IO_MONITOR_H__ */