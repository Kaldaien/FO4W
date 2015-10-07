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

#include "import.h"
#include "log.h"

const std::wstring BMF_IMPORT_EARLY = L"Early";
const std::wstring BMF_IMPORT_LATE  = L"Late";
const std::wstring BMF_IMPORT_LAZY  = L"Lazy";

const std::wstring BMF_IMPORT_ROLE_DXGI  = L"dxgi";
const std::wstring BMF_IMPORT_ROLE_D3D11 = L"d3d11";

const std::wstring BMF_IMPORT_ARCH_X64   = L"x64";
const std::wstring BMF_IMPORT_ARCH_WIN32 = L"Win32";

import_t imports [BMF_MAX_IMPORTS];

void
BMF_LoadEarlyImports64 (void)
{
  int success = 0;

  for (int i = 0; i < BMF_MAX_IMPORTS; i++) {

    // Skip libraries that are already loaded
    if (imports [i].hLibrary != NULL)
      continue;

    if (imports [i].filename != nullptr) {
      if (imports [i].when != nullptr) {
        if (imports [i].architecture != nullptr) {
          if (imports [i].architecture->get_value () == BMF_IMPORT_ARCH_X64 &&
              imports [i].when->get_value         () == BMF_IMPORT_EARLY) {

            dll_log.LogEx (true, L"  * Loading Early Custom Import %s... ",
              imports [i].filename->get_value_str ().c_str ());

            imports [i].hLibrary = LoadLibrary (
                imports [i].filename->get_value_str ().c_str ()
            );

            if (imports [i].hLibrary != NULL) {
              dll_log.LogEx (false, L"success!\n");
              ++success;
            } else  {
              imports [i].hLibrary = (HMODULE)-2;
              dll_log.LogEx (false, L"failure!\n");
            }
          }
        }
      }
    }
  }

  if (success > 0)
    dll_log.LogEx (false, L"\n");
}

void
BMF_LoadLateImports64 (void)
{
  int success = 0;

  for (int i = 0; i < BMF_MAX_IMPORTS; i++) {

    // Skip libraries that are already loaded
    if (imports [i].hLibrary != NULL)
      continue;

    if (imports [i].filename != nullptr) {
      if (imports [i].when != nullptr) {
        if (imports [i].architecture != nullptr) {
          if (imports [i].architecture->get_value () == BMF_IMPORT_ARCH_X64 &&
              imports [i].when->get_value         () == BMF_IMPORT_LATE) {

            dll_log.LogEx (true, L"  * Loading Late Custom Import %s... ",
              imports [i].filename->get_value_str ().c_str ());

            imports [i].hLibrary = LoadLibrary (
              imports [i].filename->get_value_str ().c_str ()
              );

            if (imports [i].hLibrary != NULL) {
              dll_log.LogEx (false, L"success!\n");
              ++success;
            } else  {
              imports [i].hLibrary = (HMODULE)-2;
              dll_log.LogEx (false, L"failure!\n");
            }
          }
        }
      }
    }
  }

  if (success > 0)
    dll_log.LogEx (false, L"\n");
}

void
BMF_LoadLazyImports64 (void)
{
  int success = 0;

  for (int i = 0; i < BMF_MAX_IMPORTS; i++) {

    // Skip libraries that are already loaded
    if (imports [i].hLibrary != NULL)
      continue;

    if (imports [i].filename != nullptr) {
      if (imports [i].when != nullptr) {
        if (imports [i].architecture != nullptr) {
          if (imports [i].architecture->get_value () == BMF_IMPORT_ARCH_X64 &&
              imports [i].when->get_value         () == BMF_IMPORT_LAZY) {

            dll_log.LogEx (true, L"  * Loading Lazy Custom Import %s... ",
                imports [i].filename->get_value_str ().c_str ());

            imports [i].hLibrary = LoadLibrary (
              imports [i].filename->get_value_str ().c_str ()
              );

            if (imports [i].hLibrary != NULL) {
              dll_log.LogEx (false, L"success!\n");
              ++success;
            } else  {
              imports [i].hLibrary = (HMODULE)-3;
              dll_log.LogEx (false, L"failure!\n");
            }
          }
        }
      }
    }
  }

  if (success > 0)
    dll_log.LogEx (false, L"\n");
}

void
BMF_LoadEarlyImports32 (void)
{
  int success = 0;

  for (int i = 0; i < BMF_MAX_IMPORTS; i++) {

    // Skip libraries that are already loaded
    if (imports [i].hLibrary != NULL)
      continue;

    if (imports [i].filename != nullptr) {
      if (imports [i].when != nullptr) {
        if (imports [i].architecture != nullptr) {
          if (imports [i].architecture->get_value () == BMF_IMPORT_ARCH_WIN32 &&
              imports [i].when->get_value         () == BMF_IMPORT_EARLY) {

            dll_log.LogEx (true, L"  * Loading Early Custom Import %s... ",
              imports [i].filename->get_value_str ().c_str ());

            imports [i].hLibrary = LoadLibrary (
                imports [i].filename->get_value_str ().c_str ()
            );

            if (imports [i].hLibrary != NULL) {
              dll_log.LogEx (false, L"success!\n");
              ++success;
            } else  {
              imports [i].hLibrary = (HMODULE)-2;
              dll_log.LogEx (false, L"failure!\n");
            }
          }
        }
      }
    }
  }

  if (success > 0)
    dll_log.LogEx (false, L"\n");
}

void
BMF_LoadLateImports32 (void)
{
  int success = 0;

  for (int i = 0; i < BMF_MAX_IMPORTS; i++) {

    // Skip libraries that are already loaded
    if (imports [i].hLibrary != NULL)
      continue;

    if (imports [i].filename != nullptr) {
      if (imports [i].when != nullptr) {
        if (imports [i].architecture != nullptr) {
          if (imports [i].architecture->get_value () == BMF_IMPORT_ARCH_WIN32 &&
              imports [i].when->get_value         () == BMF_IMPORT_LATE) {

            dll_log.LogEx (true, L"  * Loading Late Custom Import %s... ",
              imports [i].filename->get_value_str ().c_str ());

            imports [i].hLibrary = LoadLibrary (
              imports [i].filename->get_value_str ().c_str ()
              );

            if (imports [i].hLibrary != NULL) {
              dll_log.LogEx (false, L"success!\n");
              ++success;
            } else  {
              imports [i].hLibrary = (HMODULE)-2;
              dll_log.LogEx (false, L"failure!\n");
            }
          }
        }
      }
    }
  }

  if (success > 0)
    dll_log.LogEx (false, L"\n");
}

void
BMF_LoadLazyImports32 (void)
{
  int success = 0;

  for (int i = 0; i < BMF_MAX_IMPORTS; i++) {

    // Skip libraries that are already loaded
    if (imports [i].hLibrary != NULL)
      continue;

    if (imports [i].filename != nullptr) {
      if (imports [i].when != nullptr) {
        if (imports [i].architecture != nullptr) {
          if (imports [i].architecture->get_value () == BMF_IMPORT_ARCH_WIN32 &&
              imports [i].when->get_value         () == BMF_IMPORT_LAZY) {

            dll_log.LogEx (true, L"  * Loading Lazy Custom Import %s... ",
                imports [i].filename->get_value_str ().c_str ());

            imports [i].hLibrary = LoadLibrary (
              imports [i].filename->get_value_str ().c_str ()
              );

            if (imports [i].hLibrary != NULL) {
              dll_log.LogEx (false, L"success!\n");
              ++success;
            } else  {
              imports [i].hLibrary = (HMODULE)-3;
              dll_log.LogEx (false, L"failure!\n");
            }
          }
        }
      }
    }
  }

  if (success > 0)
    dll_log.LogEx (false, L"\n");
}