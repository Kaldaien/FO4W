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

#include "parameter.h"

#define BMF_MAX_IMPORTS 4

const std::wstring BMF_IMPORT_EARLY = L"Early";
const std::wstring BMF_IMPORT_LATE  = L"Late";
const std::wstring BMF_IMPORT_LAZY  = L"Lazy";

const std::wstring BMF_IMPORT_ROLE_DXGI  = L"dxgi";
const std::wstring BMF_IMPORT_ROLE_D3D11 = L"d3d11";

const std::wstring BMF_IMPORT_ARCH_X64   = L"x64";
const std::wstring BMF_IMPORT_ARCH_WIN32 = L"Win32";

struct import_t {
  HMODULE                hLibrary     = 0;

  bmf::ParameterStringW* filename     = nullptr;
  bmf::ParameterStringW* when         = nullptr; // 0 = Early,  1 = Late,  2 = Lazy
  bmf::ParameterStringW* role         = nullptr; // 0 = dxgi,   1 = d3d11
  bmf::ParameterStringW* architecture = nullptr; // 0 = 64-bit, 1 = 32-bit
};

extern import_t imports [BMF_MAX_IMPORTS];

void BMF_LoadEarlyImports64 (void);
void BMF_LoadLateImports64  (void);
void BMF_LoadLazyImports64  (void);