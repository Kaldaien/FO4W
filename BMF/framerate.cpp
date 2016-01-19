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

#include <Windows.h>

#include "framerate.h"

#include "log.h"
#include "config.h"
#include "command.h"
#include "core.h" // Hooking

LARGE_INTEGER BMF::Framerate::Stats::freq;

LPVOID pfnQueryPerformanceCounter       = nullptr;
LPVOID pfnSleep                         = nullptr;

static __declspec (thread) int           last_sleep     =   1;
static __declspec (thread) LARGE_INTEGER last_perfCount = { 0 };

typedef MMRESULT (WINAPI *timeBeginPeriod_t)(UINT uPeriod);
timeBeginPeriod_t timeBeginPeriod_Original = nullptr;

MMRESULT
WINAPI
timeBeginPeriod_Detour (UINT uPeriod)
{
  dll_log.Log ( L"[!] timeBeginPeriod (%d) - "
    L"[Calling Thread: 0x%04x]",
    uPeriod,
    GetCurrentThreadId () );


  return timeBeginPeriod_Original (uPeriod);
}

typedef void (WINAPI *Sleep_t)(DWORD dwMilliseconds);
Sleep_t Sleep_Original = nullptr;

void
WINAPI
Sleep_Detour (DWORD dwMilliseconds)
{
  static bool period = false;
  if (! period) {
    timeBeginPeriod (1);
    period = true;
  }

  last_sleep = dwMilliseconds;

  //if (config.framerate.yield_processor && dwMilliseconds == 0)
  //if (dwMilliseconds == 0)
    //YieldProcessor ();

  // TODO: Stop this nonsense and make an actual parameter for this...
  //         (min sleep?)
  if (dwMilliseconds >= config.render.framerate.max_delta_time) {
    Sleep_Original (dwMilliseconds);
  }
}

typedef BOOL (WINAPI *QueryPerformanceCounter_t)(_Out_ LARGE_INTEGER *lpPerformanceCount);
QueryPerformanceCounter_t QueryPerformanceCounter_Original = nullptr;

BOOL
WINAPI
QueryPerformanceCounter_Detour (_Out_ LARGE_INTEGER *lpPerformanceCount)
{
  static bool period = false;
  if (! period) {
    timeBeginPeriod (1);
    period = true;
  }

  BOOL ret = QueryPerformanceCounter_Original (lpPerformanceCount);

  if (last_sleep > 0 /*thread_sleep [GetCurrentThreadId ()] > 0 *//*|| (! (tzf::FrameRateFix::fullscreen ||
    tzf::FrameRateFix::driver_limit_setup ||
    config.framerate.allow_windowed_mode))*/) {
    memcpy (&last_perfCount, lpPerformanceCount, sizeof (LARGE_INTEGER) );
    //thread_perf [GetCurrentThreadId ()] = last_perfCount;
    return ret;
  } else {
    static LARGE_INTEGER freq = { 0 };
    if (freq.QuadPart == 0ULL)
      QueryPerformanceFrequency (&freq);

    const float fudge_factor = config.render.framerate.fudge_factor;// * freq.QuadPart;

    last_sleep = 1;
    //thread_sleep [GetCurrentThreadId ()] = 1;

    //last_perfCount = thread_perf [GetCurrentThreadId ()];

    // Mess with the numbers slightly to prevent hiccups
    lpPerformanceCount->QuadPart += (lpPerformanceCount->QuadPart - last_perfCount.QuadPart) * 
      fudge_factor/* * freq.QuadPart*/;
    memcpy (&last_perfCount, lpPerformanceCount, sizeof (LARGE_INTEGER) );
    //thread_perf [GetCurrentThreadId ()] = last_perfCount;

    return ret;
  }
}

float limiter_tolerance = 0.4f;
float target_fps        = 60.0;

void
BMF::Framerate::Init (void)
{
  BMF_CommandProcessor* pCommandProc =
    BMF_GetCommandProcessor ();

  pCommandProc->AddVariable ( "LimiterTolerance",
          new BMF_VarStub <float> (&limiter_tolerance));
  pCommandProc->AddVariable ( "TargetFPS",
          new BMF_VarStub <float> (&target_fps));

#if 0
  BMF_CreateDLLHook ( L"kernel32.dll", "QueryPerformanceCounter",
                      QueryPerformanceCounter_Detour, 
           (LPVOID *)&QueryPerformanceCounter_Original,
           (LPVOID *)&pfnQueryPerformanceCounter );
  BMF_EnableHook (pfnQueryPerformanceCounter);
#endif

#if 1
  BMF_CreateDLLHook ( L"kernel32.dll", "Sleep",
                      Sleep_Detour, 
           (LPVOID *)&Sleep_Original,
           (LPVOID *)&pfnSleep );
  BMF_EnableHook (pfnSleep);
#endif
}

void
BMF::Framerate::Shutdown (void)
{
  // Removing hooks is dangerous, likely because something else
  //   has hooked many of these functions...

////////  BMF_DisableHook (pfnSleep);

  // Disabling the hook is no safer, apparently

#if 0
  BMF_RemoveHook (pfnSleep);
  BMF_RemoveHook (pfnQueryPerformanceCounter);
#endif
}

BMF::Framerate::Limiter::Limiter (double target)
{
  init (target);
}

void
BMF::Framerate::Limiter::init (double target)
{
  ms  = 1000.0 / target;
  fps = target;

  frames = 0;

#if 0
  IDirect3DDevice9Ex* d3d9ex = nullptr;
  if (tzf::RenderFix::pDevice != nullptr) {
    tzf::RenderFix::pDevice->QueryInterface ( 
                       __uuidof (IDirect3DDevice9Ex),
                         (void **)&d3d9ex );
  }
#endif

  QueryPerformanceFrequency (&freq);

#if 0
  // Align the start to VBlank for minimum input latency
  if (d3d9ex != nullptr) {
    d3d9ex->SetMaximumFrameLatency (1);
    d3d9ex->WaitForVBlank          (0);
    d3d9ex->SetMaximumFrameLatency (max_latency);
    d3d9ex->Release                ();
  }
#endif

  QueryPerformanceCounter (&start);

  next.QuadPart = 0ULL;
  time.QuadPart = 0ULL;
  last.QuadPart = 0ULL;

  last.QuadPart = start.QuadPart - (ms / 1000.0) * freq.QuadPart;
  next.QuadPart = start.QuadPart + (ms / 1000.0) * freq.QuadPart;
}

void
BMF::Framerate::Limiter::wait (void)
{
  static bool restart = false;

  if (fps != target_fps)
    init (target_fps);

  frames++;

  QueryPerformanceCounter (&time);

  // Actual frametime before we forced a delay
  effective_ms = 1000.0 * ((double)(time.QuadPart - last.QuadPart) / (double)freq.QuadPart);

  if ((double)(time.QuadPart - next.QuadPart) / (double)freq.QuadPart / (ms / 1000.0) > (limiter_tolerance * fps)) {
    //dll_log.Log ( L" * Frame ran long (%3.01fx expected) - restarting"
                  //L" limiter...",
            //(double)(time.QuadPart - next.QuadPart) / (double)freq.QuadPart / (ms / 1000.0) / fps );
    restart = true;
  }

  if (restart) {
    frames         = 0;
    start.QuadPart = time.QuadPart + (ms / 1000.0) * (double)freq.QuadPart;
    restart        = false;
  }

  next.QuadPart = (start.QuadPart + (double)frames * (ms / 1000.0) * (double)freq.QuadPart);

  if (next.QuadPart > 0ULL) {
#if 0
    // If available (Windows 7+), wait on the swapchain
    IDirect3DDevice9Ex* d3d9ex = nullptr;
    if (tzf::RenderFix::pDevice != nullptr) {
      tzf::RenderFix::pDevice->QueryInterface ( 
                          __uuidof (IDirect3DDevice9Ex),
                            (void **)&d3d9ex );
    }
#endif

    while (time.QuadPart < next.QuadPart) {
#if 0
      if (wait_for_vblank && (double)(next.QuadPart - time.QuadPart) > (0.0166667 * (double)freq.QuadPart)) {
        if (d3d9ex != nullptr) {
          d3d9ex->WaitForVBlank (0);
        }
      }
#endif

#if 0
      if (GetForegroundWindow () != tzf::RenderFix::hWndDevice &&
          tzf::FrameRateFix::fullscreen) {
        //dll_log.Log (L" # Restarting framerate limiter; fullscreen Alt+Tab...");
        restart = true;
        break;
      }
#endif

      QueryPerformanceCounter (&time);
    }

#if 0
    if (d3d9ex != nullptr)
      d3d9ex->Release ();
#endif
  }

  else {
    dll_log.Log (L"Lost time");
    start.QuadPart += -next.QuadPart;
  }

  last.QuadPart = time.QuadPart;
}

void
BMF::Framerate::Limiter::change_limit (double target) {
  init (target);
}

double
BMF::Framerate::Limiter::effective_frametime (void) {
  return effective_ms;
}


BMF::Framerate::Limiter*
BMF::Framerate::GetLimiter (void)
{
  static Limiter* limiter = nullptr;

  if (limiter == nullptr) {
    limiter = new Limiter (config.render.framerate.target_fps);
  }

  return limiter;
}

void
BMF::Framerate::Tick (double& dt, LARGE_INTEGER& now)
{
  static LARGE_INTEGER last_frame = { 0 };

  now = BMF_CurrentPerf ();

  dt = (double)(now.QuadPart - last_frame.QuadPart) /
        (double)BMF::Framerate::Stats::freq.QuadPart;

  last_frame = now;
};