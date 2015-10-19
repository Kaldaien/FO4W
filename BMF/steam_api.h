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
#ifndef __BMF__STEAM_API_H__
#define __BMF__STEAM_API_H__

#define _CRT_SECURE_NO_WARNINGS
#include "steamapi/steam_api.h"

#include <stdint.h>

namespace BMF
{
  namespace SteamAPI
  {
    void Init     (bool preload);
    void Shutdown (void);
    void Pump     (void);

    uint32_t AppID (void);
  }
}

#endif /* __BMF__STEAM_API_H__ */