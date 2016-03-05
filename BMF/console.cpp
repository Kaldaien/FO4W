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

#include "console.h"
#include "core.h"

#include <stdint.h>

#include <string>
#include "steam_api.h"

#include "log.h"
#include "config.h"
#include "command.h"

#include <mmsystem.h>
#pragma comment (lib, "winmm.lib")

#include <comdef.h>

struct window_t {
  DWORD proc_id;
  HWND  root;
};

BOOL
CALLBACK
BMF_EnumWindows (HWND hWnd, LPARAM lParam)
{
  window_t& win = *(window_t*)lParam;

  DWORD proc_id = 0;

  GetWindowThreadProcessId (hWnd, &proc_id);

  if (win.proc_id != proc_id) {
    if (GetWindow (hWnd, GW_OWNER) != (HWND)nullptr ||
        GetWindowTextLength (hWnd) < 30           ||
          // I forget what the purpose of this is :P
        (! IsWindowVisible  (hWnd)))
      return TRUE;
  }

  win.root = hWnd;
  return FALSE;
}

HWND
BMF_FindRootWindow (DWORD proc_id)
{
  window_t win;

  win.proc_id  = proc_id;
  win.root     = 0;

  EnumWindows (BMF_EnumWindows, (LPARAM)&win);

  return win.root;
}

BMF_Console::BMF_Console (void) { }

BMF_Console*
BMF_Console::getInstance (void)
{
  if (pConsole == NULL)
    pConsole = new BMF_Console ();

  return pConsole;
}

#include "framerate.h"
bool bNoConsole = false;

void
BMF_Console::Draw (void)
{
  // Some plugins have their own...
  if (bNoConsole)
    return;

  static bool          carret    = false;
  static LARGE_INTEGER last_time = { 0 };

  std::string output;

  if (visible) {
    output += text;

    LARGE_INTEGER now;
    LARGE_INTEGER freq;

    QueryPerformanceFrequency        (&freq);

    extern LARGE_INTEGER BMF_QueryPerf (void);
    now = BMF_QueryPerf ();

    // Blink the Carret
    if ((now.QuadPart - last_time.QuadPart) > (freq.QuadPart / 3)) {
      carret = ! carret;

      last_time.QuadPart = now.QuadPart;
    }

    if (carret)
      output += "-";

    // Show Command Results
      if (command_issued) {
      output += "\n";
      output += result_str;
    }
  }

  extern BOOL
  __stdcall
  BMF_DrawExternalOSD (std::string app_name, std::string text);
  BMF_DrawExternalOSD ("BMF Console", output);
}

void
BMF_Console::Start (void)
{
  // STUPID HACK UNTIL WE PROPERLY UNIFY BMF AND TZFIX'S CONSOLE.
  if (GetModuleHandle (L"tzfix.dll") || GetModuleHandle (L"AgDrag.dll") || GetModuleHandle (L"tsfix.dll")) {
    bNoConsole = true;
    return;
  }

  hMsgPump =
    CreateThread ( NULL,
                      NULL,
                        BMF_Console::MessagePump,
                          &hooks,
                            NULL,
                              NULL );
}

void
BMF_Console::End (void)
{
  // STUPID HACK UNTIL WE PROPERLY UNIFY BMF AND TZFIX'S CONSOLE.
  if (GetModuleHandle (L"tzfix.dll") || GetModuleHandle (L"AgDrag.dll") || GetModuleHandle (L"tsfix.dll")) {
    bNoConsole = true;
    return;
  }

  TerminateThread     (hMsgPump, 0);
  UnhookWindowsHookEx (hooks.keyboard);
  UnhookWindowsHookEx (hooks.mouse);
}

HANDLE
BMF_Console::GetThread (void)
{
  return hMsgPump;
}

DWORD
WINAPI
BMF_Console::MessagePump (LPVOID hook_ptr)
{
  hooks_t* pHooks = (hooks_t *)hook_ptr;

  ZeroMemory (text, 4096);

  text [0] = '>';

  extern    HMODULE hModSelf;

  HWND  hWndForeground;
  DWORD dwThreadId;

  int hits = 0;

  DWORD dwTime = timeGetTime ();

  while (true) {
    // Spin until the game has drawn a frame
    if (! frames_drawn) {
      Sleep (83);
      continue;
    }

    hWndForeground = GetForegroundWindow ();

    if ((! hWndForeground) ||
           hWndForeground != BMF_FindRootWindow (GetCurrentProcessId ())) {
      Sleep (83);
      continue;
    }

    DWORD dwProc;

    dwThreadId =
      GetWindowThreadProcessId (hWndForeground, &dwProc);

    // Ugly hack, but a different window might be in the foreground...
    if (dwProc != GetCurrentProcessId ()) {
      //dll_log.Log (L" *** Tried to hook the wrong process!!!");
      Sleep (83);
      continue;
    }

    break;
  }

  dll_log.Log ( L"  # Found window in %03.01f seconds, "
                L"installing keyboard hook...",
                  (float)(timeGetTime () - dwTime) / 1000.0f );

  dwTime = timeGetTime ();
  hits   = 1;

  while (! (pHooks->keyboard = SetWindowsHookEx ( WH_KEYBOARD,
              KeyboardProc,
                hModSelf,
                  dwThreadId ))) {
    _com_error err (HRESULT_FROM_WIN32 (GetLastError ()));

    dll_log.Log ( L"  @ SetWindowsHookEx failed: 0x%04X (%s)",
      err.WCode (), err.ErrorMessage () );

    ++hits;

    if (hits >= 5) {
      dll_log.Log ( L"  * Failed to install keyboard hook after %lu tries... "
        L"bailing out!",
        hits );
      return 0;
    }

    Sleep (1);
  }

#if 0
  while (! (pHooks->mouse = SetWindowsHookEx ( WH_MOUSE,
              MouseProc,
                hModSelf,
                  dwThreadId ))) {
    _com_error err (HRESULT_FROM_WIN32 (GetLastError ()));

    dll_log.Log ( L"  @ SetWindowsHookEx failed: 0x%04X (%s)",
      err.WCode (), err.ErrorMessage () );

    ++hits;

    if (hits >= 5) {
      dll_log.Log ( L"  * Failed to install mouse hook after %lu tries... "
        L"bailing out!",
        hits );
      return 0;
    }

    Sleep (1);
  }
#endif

  dll_log.Log ( L"  * Installed keyboard hook for command console... "
                L"%lu %s (%lu ms!)",
                  hits,
                    hits > 1 ? L"tries" : L"try",
                      timeGetTime () - dwTime );

  //193 - 199

  // Pump the sucker
  while (true) Sleep (10);

  return 0;
}

LRESULT
CALLBACK
BMF_Console::MouseProc (int nCode, WPARAM wParam, LPARAM lParam)
{
  MOUSEHOOKSTRUCT* pmh = (MOUSEHOOKSTRUCT *)lParam;

  return CallNextHookEx (BMF_Console::getInstance ()->hooks.mouse, nCode, wParam, lParam);
}

LRESULT
CALLBACK
BMF_Console::KeyboardProc (int nCode, WPARAM wParam, LPARAM lParam)
{
  if (nCode >= 0) {
    BYTE    vkCode   = LOWORD (wParam) & 0xFF;
    BYTE    scanCode = HIWORD (lParam) & 0x7F;
    bool    repeated = LOWORD (lParam);
    bool    keyDown  = ! (lParam & 0x80000000);

    if (visible && vkCode == VK_BACK) {
      if (keyDown) {
        size_t len = strlen (text);
                len--;

        if (len < 1)
          len = 1;

        text [len] = '\0';
      }
    }
      
    else if ((vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT)) {
      if (keyDown) keys_ [VK_SHIFT] = 0x81; else keys_ [VK_SHIFT] = 0x00;
    }

    else if ((!repeated) && vkCode == VK_CAPITAL) {
      if (keyDown) if (keys_ [VK_CAPITAL] == 0x00) keys_ [VK_CAPITAL] = 0x81; else keys_ [VK_CAPITAL] = 0x00;
    }

    else if ((vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL)) {
      if (keyDown) keys_ [VK_CONTROL] = 0x81; else keys_ [VK_CONTROL] = 0x00;
    }

    else if ((vkCode == VK_UP) || (vkCode == VK_DOWN)) {
      if (keyDown && visible) {
        if (vkCode == VK_UP)
          commands.idx--;
        else
          commands.idx++;

        // Clamp the index
        if (commands.idx < 0)
          commands.idx = 0;
        else if (commands.idx >= commands.history.size ())
          commands.idx = commands.history.size () - 1;

        if (commands.history.size ()) {
          strcpy (&text [1], commands.history [commands.idx].c_str ());
          command_issued = false;
        }
      }
    }

    else if (visible && vkCode == VK_RETURN) {
      if (keyDown && LOWORD (lParam) < 2) {
        size_t len = strlen (text+1);
        // Don't process empty or pure whitespace command lines
        if (len > 0 && strspn (text+1, " ") != len) {
          BMF_CommandResult result =
            BMF_GetCommandProcessor ()->ProcessCommandLine (text+1);

          if (result.getStatus ()) {
            // Don't repeat the same command over and over
            if (commands.history.size () == 0 ||
                commands.history.back () != &text [1]) {
              commands.history.push_back (&text [1]);
            }

            commands.idx = commands.history.size ();

            text [1] = '\0';

            command_issued = true;
          }
          else {
            command_issued = false;
          }

          result_str = result.getWord () + std::string (" ")   +
                       result.getArgs () + std::string (":  ") +
                       result.getResult ();
        }
      }
    }

    else if (keyDown) {
      bool new_press = keys_ [vkCode] != 0x81;

      keys_ [vkCode] = 0x81;

      if (keys_ [VK_CONTROL] && keys_ [VK_SHIFT] && keys_ [VK_TAB] && new_press) {
        visible = ! visible;
        // This will pause/unpause the game
        BMF::SteamAPI::SetOverlayState (visible);
      }

      if (visible) {
        char key_str [2];
        key_str [1] = '\0';

        if (1 == ToAsciiEx ( vkCode,
                              scanCode,
                              keys_,
                            (LPWORD)key_str,
                              0,
                              GetKeyboardLayout (0) )) {
          strncat (text, key_str, 1);
          command_issued = false;
        }
      }
    }

    else if ((! keyDown))
      keys_ [vkCode] = 0x00;

    if (visible) return 1;
  }

  return CallNextHookEx (BMF_Console::getInstance ()->hooks.keyboard, nCode, wParam, lParam);
};

void
BMF_DrawConsole (void)
{
  // Drop the first frame so that the console shows up below
  //   the main OSD.
  if (frames_drawn > 1) {
    BMF_Console* pConsole = BMF_Console::getInstance ();
    pConsole->Draw ();
  }
}

BMF_Console* BMF_Console::pConsole;
char         BMF_Console::text [4096];

BYTE         BMF_Console::keys_ [256]    = { 0 };
bool         BMF_Console::visible        = false;

bool         BMF_Console::command_issued = false;
std::string  BMF_Console::result_str;

BMF_Console::command_history_t
             BMF_Console::commands;