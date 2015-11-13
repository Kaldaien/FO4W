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
#include <string>

extern std::wstring BMF_VER_STR;

struct bmf_config_t
{
  struct {
    bool   show           = true;
    LONG   format         = LOCALE_CUSTOM_UI_DEFAULT;

    struct {
      BYTE toggle [4]     = { VK_CONTROL, VK_SHIFT, 'T', 0 };
    } keys;
  } time;

  struct {
    bool   show           = true;
    float  reserve        = 75.0f; // 75%
    float  interval       = 0.25f;

    struct {
      BYTE toggle [4]     = { VK_CONTROL, VK_SHIFT, 'M', 0 };
    } keys;
  } mem;


  struct {
    bool   show           = true;
    float  interval       = 0.25f; // 250 msecs (4 Hz)

    struct {
      BYTE toggle [4]     = { VK_CONTROL, VK_SHIFT, 'I', 0 };
    } keys;
  } io;


  struct {
    bool   show           = false;

    struct {
      BYTE toggle [4]     = { VK_CONTROL, VK_SHIFT, 'S', 0 };
    } keys;
  } sli;


  struct {
    bool   use            = false;

    struct {
      BYTE toggle [4]     = { VK_CONTROL, VK_SHIFT, 'B', 0 };
    } keys;
  } load_balance;


  struct {
    bool   show           = true;

    bool   pump           = false;
    float  pump_interval  = 0.0166666666f;

    DWORD  red            = -1;
    DWORD  green          = -1;
    DWORD  blue           = -1;
    DWORD  scale          =  1;
    DWORD  pos_x          =  0;
    DWORD  pos_y          =  0;

    struct {
      BYTE toggle [4]     = { VK_CONTROL, VK_SHIFT, 'O',          0 };
      BYTE shrink [4]     = { VK_CONTROL, VK_SHIFT, VK_OEM_MINUS, 0 };
      BYTE expand [4]     = { VK_CONTROL, VK_SHIFT, VK_OEM_PLUS,  0 };
    } keys;
  } osd;


  struct {
    bool   show           = false;
    float  interval       = 0.33333333f;
    bool   simple         = true;

    struct {
      BYTE toggle  [4]    = { VK_CONTROL, VK_SHIFT, 'C', 0 };
    } keys;
  } cpu;


  struct {
    bool   show           = true;

    struct {
      BYTE toggle [4]     = { VK_CONTROL, VK_SHIFT, 'F', 0 };
    } keys;
    //float fps_interval  = 1.0f;
  } fps;


  struct {
    bool   show           = true;
    bool   print_slowdown = false;
    float  interval       = 0.333333f;

    struct {
      BYTE toggle [4]     = { VK_CONTROL, VK_SHIFT, 'G', 0 };
    } keys;
  } gpu;


  struct {
    bool   show            = false;

    float  interval        = 0.333333f;
    int    type            = 0; // Physical = 0,
                                // Logical  = 1

    struct {
      BYTE toggle [4]      = { VK_CONTROL, VK_SHIFT, 'D', 0 };
    } keys;
  } disk;


  struct {
    bool   show            = false;
    float  interval        = 2.5f;

    struct {
      BYTE toggle [4]      = { VK_CONTROL, VK_SHIFT, 'P', 0 };
    } keys;
  } pagefile;


  struct {
    std::wstring
            achievement_sound = L"";
    int     notify_corner     = 4; // 0=Top-Left,
                                   // 1=Top-Right,
                                   // 2=Bottom-Left,
                                   // 3=Bottom-Right,
                                   // 4=Don't Care
    int     inset_x           = 0;
    int     inset_y           = 0;

    bool    nosound           = false;
    bool    silent            = false;

    int     appid             = 0;
  } steam;


  struct {
    struct {
      int     target_fps       = 0;
      int     pre_render_limit = 3;
      int     present_interval = 1;
      int     backbuffer_count = 1;
      int     max_delta_time   = 33;
      bool    flip_discard     = false;
      float   fudge_factor     = 3.333333f;
    } framerate;
  } render;

  struct {
    int     init_delay        = 250;
    bool    silent            = false;
    bool    allow_dev_trans   = false;
    bool    prefer_fahrenheit = true;
    std::wstring
            version           = BMF_VER_STR;
  } system;
};

extern bmf_config_t config;

bool BMF_LoadConfig (std::wstring name         = L"dxgi");
void BMF_SaveConfig (std::wstring name         = L"dxgi",
                     bool         close_config = false);

#endif __BMF__CONFIG_H__