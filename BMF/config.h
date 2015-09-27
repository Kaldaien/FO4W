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
#ifndef __BMF__CONFIG_H__
#define __BMF__CONFIG_H__

#include <Windows.h>

struct bmf_config_t
{
  bool   show_overlay     = true;

  bool   mem_stats        = true;
  BYTE   mem_keys [4]     = { VK_CONTROL, VK_SHIFT, 'M', 0 };
  float  mem_reserve      = 75.0f;

  bool  io_stats          = true;
  BYTE  io_keys [4]       = { VK_CONTROL, VK_SHIFT, 'I', 0 };
  float io_interval       = 0.25f;

  bool  sli_stats         = false;
  BYTE  sli_keys [4]      = { VK_CONTROL, VK_SHIFT, 'S', 0 };

  bool  load_balance      = false;
  BYTE  load_keys [4]     = { VK_CONTROL, VK_SHIFT, 'B', 0 };

  bool  cpu_stats         = false;
  BYTE  cpu_keys  [4]     = { VK_CONTROL, VK_SHIFT, 'C', 0 };
  float cpu_interval      = 0.166666f;

  bool  fps_stats         = true;
  BYTE  fps_keys[4]       = { VK_CONTROL, VK_SHIFT, 'F', 0 };
  //float fps_interval      = 1.0f;

  bool  gpu_stats         = true;
  BYTE  gpu_keys [4]      = { VK_CONTROL, VK_SHIFT, 'G', 0 };
  float gpu_interval      = 0.333333f;

  bool  disk_stats        = false;
  BYTE  disk_keys [4]     = { VK_CONTROL, VK_SHIFT, 'D', 0 };
  float disk_interval     = 0.333333f;

  bool  pagefile_stats    = false;
  BYTE  pagefile_keys [4] = { VK_CONTROL, VK_SHIFT, 'P', 0 };
  float pagefile_interval = 2.5f;

  int   init_delay      = 250;
  bool  silent          = false;
  bool  allow_dev_trans = false;
} extern config;

bool BMF_LoadConfig (void);
void BMF_SaveConfig (void);

#endif __BMF__CONFIG_H__