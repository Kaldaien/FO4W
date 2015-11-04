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

//#include "stdafx.h"
//#include "dxgi_interfaces.h"

LPVOID BMF_GetSharedMemory     (void);
BOOL   BMF_ReleaseSharedMemory (LPVOID pMemory);

BOOL BMF_DrawOSD          (void);
BOOL BMF_UpdateOSD        (LPCSTR lpText, LPVOID pMapAddr = nullptr, LPCSTR lpAppName = nullptr);
void BMF_ReleaseOSD       (void);

void BMF_SetOSDPos        (int x,   int y);

// Any value out of range: [0,255] means IGNORE that color
void BMF_SetOSDColor      (int red, int green, int blue);
//void BMF_SetOSDShadow     (int red, int green, int blue);

void BMF_SetOSDScale      (DWORD dwScale, bool relative = false);
void BMF_ResizeOSD        (int scale_incr);

#endif /* __BMF__OSD_H__ */