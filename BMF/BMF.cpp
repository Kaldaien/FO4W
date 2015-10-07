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
#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>

#include "dxgi_backend.h"
#include "d3d9_backend.h"
#include "opengl_backend.h"

enum DLL_ROLE {
  DXGI   = 0x01, // D3D 10-12
  D3D9   = 0x02,
  OpenGL = 0x04,
  Vulkan = 0x08
} dll_role;


bool
BMF_EstablishDllRole (HMODULE hModule)
{
  wchar_t wszDllFullName [MAX_PATH];
  wszDllFullName [MAX_PATH - 1] = L'\0';

  GetModuleFileName (hModule, wszDllFullName, MAX_PATH - 1);

  wchar_t* wszDllName = wcsrchr (wszDllFullName, L'\\') + 1;


  if (! _wcsicmp (wszDllName, L"dxgi.dll"))
    dll_role = DLL_ROLE::DXGI;

  else if (! _wcsicmp (wszDllName, L"d3d9.dll"))
    dll_role = DLL_ROLE::D3D9;

  else if (! _wcsicmp (wszDllName, L"OpenGL32.dll"))
    dll_role = DLL_ROLE::OpenGL;

  return true;
}


bool
BMF_Attach (DLL_ROLE role)
{
  switch (role)
  {
  case DLL_ROLE::DXGI:
    return BMF::DXGI::Startup ();
    break;
  case DLL_ROLE::D3D9:
    return BMF::D3D9::Startup ();
    break;
  case DLL_ROLE::OpenGL:
    return BMF::OpenGL::Startup ();
    break;
  case DLL_ROLE::Vulkan:
    break;
  }

  return false;
}

bool
BMF_Detach (DLL_ROLE role)
{
  switch (role)
  {
  case DLL_ROLE::DXGI:
    return BMF::DXGI::Shutdown ();
    break;
  case DLL_ROLE::D3D9:
    return BMF::D3D9::Shutdown ();
    break;
  case DLL_ROLE::OpenGL:
    return BMF::OpenGL::Shutdown ();
    break;
  case DLL_ROLE::Vulkan:
    break;
  }

  return false;
}


BOOL
APIENTRY DllMain ( HMODULE hModule,
                   DWORD   ul_reason_for_call,
                   LPVOID  lpReserved )
{
  switch (ul_reason_for_call)
  {
    case DLL_PROCESS_ATTACH:
    {
       BMF_EstablishDllRole (hModule);
       BMF_Attach           (dll_role);
    } break;

    case DLL_THREAD_ATTACH:
      //dll_log.Log (L"Custom dxgi.dll Attached (tid=%x)",
      //                GetCurrentThreadId ());
      break;

    case DLL_THREAD_DETACH:
      //dll_log.Log (L"Custom dxgi.dll Detached (tid=%x)",
      //                GetCurrentThreadId ());
      break;

    case DLL_PROCESS_DETACH:
    {
      BMF_Detach (dll_role);

      extern void BMF_ShutdownCOM (void);
      BMF_ShutdownCOM ();
    } break;
  }

  return TRUE;
}