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

#include "opengl_backend.h"
#include "log.h"

bmf_logger_t opengl_log;
HMODULE      hOpenGL = nullptr;

bool
BMF::OpenGL::Startup (void)
{
  opengl_log.init ("opengl.log", "w+");
  hOpenGL = LoadLibrary (L"OpenGL32.dll");

  return false;
}

bool
BMF::OpenGL::Shutdown (void)
{
  return false;
}


#define OPENGL_STUB(_Return, _Name, _Proto, _Args)                    \
  __declspec (dllexport) _Return STDMETHODCALLTYPE                    \
  _Name _Proto {                                                      \
    WaitForInit ();                                                   \
                                                                      \
    typedef _Return (STDMETHODCALLTYPE *passthrough_t) _Proto;        \
    static passthrough_t _default_impl = nullptr;                     \
                                                                      \
    if (_default_impl == nullptr) {                                   \
      static const char* szName = #_Name;                             \
      _default_impl = (passthrough_t)GetProcAddress (hOpenGL, szName);\
                                                                      \
      if (_default_impl == nullptr) {                                 \
        opengl_log.Log (                                              \
          L"Unable to locate symbol  %s in OpenGL32.dll",             \
          L#_Name);                                                   \
        return E_NOTIMPL;                                             \
      }                                                               \
    }                                                                 \
                                                                      \
    opengl_log.Log (L"[!] %s (%s) - "                                 \
             L"[Calling Thread: 0x%04x]",                             \
      L#_Name, L#_Proto, GetCurrentThreadId ());                      \
                                                                      \
    return _default_impl _Args;                                       \
}