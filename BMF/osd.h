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

#ifndef __BMF__OSD_H__
#define __BMF__OSD_H__

#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <stdint.h>

const int MAX_GPU_NODES = 4;

struct memory_stats_t {
  uint64_t min_reserve       = UINT64_MAX;
  uint64_t max_reserve       = 0;

  uint64_t min_avail_reserve = UINT64_MAX;
  uint64_t max_avail_reserve = 0;

  uint64_t min_budget        = UINT64_MAX;
  uint64_t max_budget        = 0;

  uint64_t min_usage         = UINT64_MAX;
  uint64_t max_usage         = 0;

  uint64_t min_over_budget   = UINT64_MAX;
  uint64_t max_over_budget   = 0;

  uint64_t budget_changes    = 0;
};

extern memory_stats_t mem_stats [MAX_GPU_NODES];

enum buffer_t {
  Front = 0,
  Back  = 1,
  NumBuffers
};

#include "stdafx.h"
//#include "dxgi_interfaces.h"

struct mem_info_t {
  DXGI_QUERY_VIDEO_MEMORY_INFO local    [MAX_GPU_NODES];
  DXGI_QUERY_VIDEO_MEMORY_INFO nonlocal [MAX_GPU_NODES];
  SYSTEMTIME                   time;
  buffer_t                     buffer = Front;
  int                          nodes  = MAX_GPU_NODES;
};

extern mem_info_t mem_info [NumBuffers];

LPVOID BMF_GetSharedMemory     (void);
BOOL   BMF_ReleaseSharedMemory (LPVOID pMemory);

BOOL BMF_DrawOSD          (void);
BOOL BMF_UpdateOSD        (LPCSTR lpText, LPVOID pMapAddr = nullptr);
void BMF_ReleaseOSD       (void);

void BMF_SetOSDPos        (int x,   int y);

// Any value out of range: [0,255] means IGNORE that color
void BMF_SetOSDColor      (int red, int green, int blue);
//void BMF_SetOSDShadow     (int red, int green, int blue);

void BMF_SetOSDScale      (DWORD dwScale, bool relative = false);
void BMF_ResizeOSD        (int scale_incr);

#endif /* __BMF__OSD_H__ */