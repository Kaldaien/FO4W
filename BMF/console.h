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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Batman "Fix". If not, see <http://www.gnu.org/licenses/>.
**/

#ifndef __BMF__CONSOLE_H__
#define __BMF__CONSOLE_H__

#include <Windows.h>

#include <string>
#include <vector>

#include <cstdint>

class BMF_Console
{
private:
  HANDLE               hMsgPump;
  struct hooks_t {
    HHOOK              keyboard;
    HHOOK              mouse;
  } hooks;

  static BMF_Console*  pConsole;

  static char          text [4096];

  static BYTE          keys_ [256];
  static bool          visible;

  static bool          command_issued;
  static std::string   result_str;

  struct command_history_t {
    std::vector <std::string> history;
    size_t                    idx     = -1;
  } static commands;

protected:
  BMF_Console (void);

public:
  static BMF_Console* getInstance (void);

  void Draw        (void);

  void Start       (void);
  void End         (void);

  HANDLE GetThread (void);

  static DWORD
    WINAPI
    MessagePump (LPVOID hook_ptr);

  static LRESULT
    CALLBACK
    MouseProc (int nCode, WPARAM wParam, LPARAM lParam);

  static LRESULT
    CALLBACK
    KeyboardProc (int nCode, WPARAM wParam, LPARAM lParam);
};

#endif /* __BMF__CONSOLE_H__ */