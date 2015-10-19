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

#include "resource.h"

#include "core.h"

#include "config.h"
#include "log.h"

// PlaySound
#pragma comment (lib, "winmm.lib")

bmf_logger_t steam_log;
HANDLE       hSteamHeap = { 0 };

static bool init = false;

// We're not going to use DLL Import - we will load these function pointers
//  by hand.
#define STEAM_API_NODLL
#include "steam_api.h"

S_API typedef bool (S_CALLTYPE *SteamAPI_Init_t    )(void);
S_API typedef bool (S_CALLTYPE *SteamAPI_InitSafe_t)(void);

S_API typedef void (S_CALLTYPE *SteamAPI_Shutdown_t)(void);

S_API typedef void (S_CALLTYPE *SteamAPI_RegisterCallback_t)
          (class CCallbackBase *pCallback, int iCallback);
S_API typedef void (S_CALLTYPE *SteamAPI_UnregisterCallback_t)
          (class CCallbackBase *pCallback);
S_API typedef void (S_CALLTYPE *SteamAPI_RunCallbacks_t)(void);

S_API typedef HSteamUser (S_CALLTYPE *SteamAPI_GetHSteamUser_t)(void);
S_API typedef HSteamPipe (S_CALLTYPE *SteamAPI_GetHSteamPipe_t)(void);

S_API typedef ISteamClient*    (S_CALLTYPE *SteamClient_t   )(void);

S_API SteamAPI_RunCallbacks_t       SteamAPI_RunCallbacks       = nullptr;
S_API SteamAPI_RegisterCallback_t   SteamAPI_RegisterCallback   = nullptr;
S_API SteamAPI_UnregisterCallback_t SteamAPI_UnregisterCallback = nullptr;

S_API SteamAPI_Init_t               SteamAPI_Init               = nullptr;
S_API SteamAPI_InitSafe_t           SteamAPI_InitSafe           = nullptr;
S_API SteamAPI_Shutdown_t           SteamAPI_Shutdown           = nullptr;

S_API SteamAPI_GetHSteamUser_t      SteamAPI_GetHSteamUser      = nullptr;
S_API SteamAPI_GetHSteamPipe_t      SteamAPI_GetHSteamPipe      = nullptr;

S_API SteamClient_t                 SteamClient                 = nullptr;


class BMF_SteamAPIContext {
public:
  bool Init (HMODULE hSteamDLL)
  {
    if (hSteamHeap == nullptr)
      hSteamHeap = HeapCreate (HEAP_CREATE_ENABLE_EXECUTE, 0, 0);

    if (SteamAPI_InitSafe == nullptr) {
      SteamAPI_InitSafe =
        (SteamAPI_InitSafe_t)GetProcAddress (
          hSteamDLL,
            "SteamAPI_InitSafe"
        );
    }

    if (SteamAPI_GetHSteamUser == nullptr) {
      SteamAPI_GetHSteamUser =
        (SteamAPI_GetHSteamUser_t)GetProcAddress (
           hSteamDLL,
             "SteamAPI_GetHSteamUser"
        );
    }

    if (SteamAPI_GetHSteamPipe == nullptr) {
      SteamAPI_GetHSteamPipe =
        (SteamAPI_GetHSteamPipe_t)GetProcAddress (
           hSteamDLL,
             "SteamAPI_GetHSteamPipe"
        );
    }

    if (SteamClient == nullptr) {
      SteamClient =
        (SteamClient_t)GetProcAddress (
           hSteamDLL,
             "SteamClient"
        );
    }

    if (SteamAPI_RegisterCallback == nullptr) {
      SteamAPI_RegisterCallback =
        (SteamAPI_RegisterCallback_t)GetProcAddress (
          hSteamDLL,
            "SteamAPI_RegisterCallback"
        );
    }

    if (SteamAPI_UnregisterCallback == nullptr) {
      SteamAPI_UnregisterCallback =
        (SteamAPI_UnregisterCallback_t)GetProcAddress (
          hSteamDLL,
            "SteamAPI_UnregisterCallback"
        );
    }

    if (SteamAPI_RunCallbacks == nullptr) {
      SteamAPI_RunCallbacks =
        (SteamAPI_RunCallbacks_t)GetProcAddress (
          hSteamDLL,
            "SteamAPI_RunCallbacks"
        );
    }

    if (SteamAPI_Shutdown == nullptr) {
      SteamAPI_Shutdown =
        (SteamAPI_Shutdown_t)GetProcAddress (
          hSteamDLL,
            "SteamAPI_Shutdown"
        );
    }

    if (SteamAPI_InitSafe == nullptr)
      return false;

    if (SteamAPI_GetHSteamUser == nullptr)
      return false;

    if (SteamAPI_GetHSteamPipe == nullptr)
      return false;

    if (SteamClient == nullptr)
      return false;

    if (SteamAPI_RegisterCallback == nullptr)
      return false;

    if (SteamAPI_UnregisterCallback == nullptr)
      return false;

    if (SteamAPI_RunCallbacks == nullptr)
      return false;

    if (! SteamAPI_InitSafe ())
      return false;

    client_ = SteamClient ();

    HSteamUser hSteamUser = SteamAPI_GetHSteamUser ();
    HSteamPipe hSteamPipe = SteamAPI_GetHSteamPipe ();

    if (! client_)
      return false;

    user_stats_ =
      client_->GetISteamUserStats (
        hSteamUser,
          hSteamPipe,
            STEAMUSERSTATS_INTERFACE_VERSION
      );

    if (user_stats_ == nullptr)
      return false;

    utils_ =
      client_->GetISteamUtils ( hSteamPipe,
                                  STEAMUTILS_INTERFACE_VERSION );

   if (utils_ == nullptr)
     return false;

    // 4 == Don't Care
    if (config.steam.notify_corner != 4)
      utils_->SetOverlayNotificationPosition (
        (ENotificationPosition)config.steam.notify_corner
      );

    if (config.steam.inset_x != 0 ||
        config.steam.inset_y != 0) {
      utils_->SetOverlayNotificationInset (config.steam.inset_x,
                                           config.steam.inset_y);
    }

    return true;
  }

  void Shutdown (void) {
    if (hSteamHeap != nullptr) {
      HeapDestroy (hSteamHeap);
      hSteamHeap = nullptr;
    }

    client_     = nullptr;
    user_stats_ = nullptr;
    utils_      = nullptr;

    if (SteamAPI_Shutdown != nullptr) {
      // We probably should not shutdown Steam API; the underlying
      //  game will do this at a more opportune time for sure.
      SteamAPI_Shutdown ();
    }
  }

  ISteamUserStats* UserStats (void) { return user_stats_; }
  ISteamUtils*     Utils     (void) { return utils_; }

protected:
private:
  // TODO: We have an obvious lack of thread-safety here...

  ISteamClient*    client_     = nullptr;
  ISteamUserStats* user_stats_ = nullptr;
  ISteamUtils*     utils_      = nullptr;
} static steam_ctx;

#if 0
struct BaseStats_t
{
  uint64  m_nGameID;
  EResult status;
  uint64  test;
};

S_API typedef void (S_CALLTYPE *steam_callback_run_t)
                       (CCallbackBase *pThis, void *pvParam);

S_API typedef void (S_CALLTYPE *steam_callback_run_ex_t)
                       (CCallbackBase *pThis, void *pvParam, bool, SteamAPICall_t);

steam_callback_run_t    Steam_Callback_RunStat_Orig   = nullptr;
steam_callback_run_ex_t Steam_Callback_RunStatEx_Orig = nullptr;

S_API
void
S_CALLTYPE
Steam_Callback_RunStat (CCallbackBase *pThis, void *pvParam)
{
  steam_log.Log ( L"CCallback::Run (%04Xh, %04Xh)  <Stat %s>;",
                    pThis, pvParam, pThis->GetICallback () == 1002L ? L"Store" : L"Receive" );

  BaseStats_t* stats = (BaseStats_t *)pvParam;

  steam_log.Log ( L" >> Size: %04i, Event: %04i - %04lu, %04llu, %04llu\n", pThis->GetCallbackSizeBytes (), pThis->GetICallback (), stats->status, stats->m_nGameID, stats->test);

  Steam_Callback_RunStat_Orig (pThis, pvParam);
}

S_API
void
S_CALLTYPE
Steam_Callback_RunStatEx (CCallbackBase *pThis, void           *pvParam,
                          bool           tf,    SteamAPICall_t  call)
{
  steam_log.Log ( L"CCallback::Run (%04Xh, %04Xh, %01i, %04llu)  "
                  L"<Stat %s>;",
                    pThis, pvParam, tf, call, pThis->GetICallback () == 1002L ? L"Store" : L"Receive" );

  BaseStats_t* stats = (BaseStats_t *)pvParam;

  steam_log.Log ( L" >> Size: %04i, Event: %04i - %04lu, %04llu, %04llu\n", pThis->GetCallbackSizeBytes (), pThis->GetICallback (), stats->status, stats->m_nGameID, stats->test);

  Steam_Callback_RunStatEx_Orig (pThis, pvParam, tf, call);
}
#endif

class BMF_Steam_AchievementManager {
public:
  BMF_Steam_AchievementManager (const wchar_t* wszUnlockSound) :
       unlock_listener ( this, &BMF_Steam_AchievementManager::OnUnlock      ),
       stat_listener   ( this, &BMF_Steam_AchievementManager::OnRecvStats   ),
       stat_receipt    ( this, &BMF_Steam_AchievementManager::AckStoreStats )
  {
    FILE* fWAV = _wfopen (wszUnlockSound, L"rb");

    if (fWAV != nullptr) {
      steam_log.LogEx (true, L"  >> Loading Achievement Unlock Sound: '%s'...",
                       wszUnlockSound);

                  fseek  (fWAV, 0, SEEK_END);
      long size = ftell  (fWAV);
                  rewind (fWAV);

      unlock_sound = (uint8_t *)HeapAlloc (hSteamHeap, HEAP_ZERO_MEMORY, size);

      fread  (unlock_sound, size, 1, fWAV);
      fclose (fWAV);

      steam_log.LogEx (false, L" %d bytes\n", size);

      default_loaded = false;
    } else {
      steam_log.Log (L"  * Failed to Load Unlock Sound: '%s', using DEFAULT",
                       wszUnlockSound);

      extern HMODULE hModSelf;
      HRSRC   default_sound =
        FindResource (hModSelf, MAKEINTRESOURCE (IDR_WAVE1), L"WAVE");
      HGLOBAL sound_ref     =
        LoadResource (hModSelf, default_sound);
      unlock_sound          = (uint8_t *)LockResource (sound_ref);

      default_loaded = true;
    }
  }

  ~BMF_Steam_AchievementManager (void) {
    if ((! default_loaded) && (unlock_sound != nullptr)) {
      HeapFree (hSteamHeap, 0, unlock_sound);
      unlock_sound = nullptr;
    }
  }

  class BMF_SteamAchievement {
  public:
    BMF_SteamAchievement (const char* szName, ISteamUserStats* stats) {
      name_ =
        stats->GetAchievementDisplayAttribute (szName, "name");
      desc_ =
        stats->GetAchievementDisplayAttribute (szName, "desc");

      stats->GetAchievementAndUnlockTime ( szName,
                                             &unlocked_,
                                               (uint32_t *)&time_ );
    }

    const char* name_;
    const char* desc_;

    bool        unlocked_;
    __time32_t  time_;
  };

  void log_all_achievements (void)
  {
    ISteamUserStats* stats = steam_ctx.UserStats ();

    for (int i = 0; i < stats->GetNumAchievements (); i++)
    {
      BMF_SteamAchievement achievement (
        stats->GetAchievementName (i),
        stats
      );

      steam_log.LogEx (false, L"\n [%c] Achievement %03lu......: '%hs'\n",
                         achievement.unlocked_ ? L'X' : L' ',
                           i, stats->GetAchievementName (i)
                      );
      steam_log.LogEx (false,
                              L"  + Human Readable Name...: %hs\n",
                         achievement.name_);
      if (strlen (achievement.desc_))
        steam_log.LogEx (false,
                                L"  *- Detailed Description.: %hs\n",
                          achievement.desc_);

      if (achievement.unlocked_) {
        steam_log.LogEx (false,
                                L"  @-- Player Unlocked At..: %s",
                                  _wctime32 (&achievement.time_));
      }
    }

    steam_log.LogEx (false, L"\n");
  }

  STEAM_CALLBACK ( BMF_Steam_AchievementManager,
                   AckStoreStats,
                   UserStatsStored_t,
                   stat_receipt )
  {
    // Sometimes we receive event callbacks for games that aren't this one...
    //   ignore those!
    if (pParam->m_nGameID != BMF::SteamAPI::AppID ())
      return;

    steam_log.Log ( L" >> Stats Stored for AppID: %llu",
                      pParam->m_nGameID );
  }

  STEAM_CALLBACK ( BMF_Steam_AchievementManager,
                   OnRecvStats,
                   UserStatsReceived_t,
                   stat_listener )
  {
    // Sometimes we receive event callbacks for games that aren't this one...
    //   ignore those!
    if (pParam->m_nGameID != BMF::SteamAPI::AppID ())
      return;

    log_all_achievements ();
  }

  STEAM_CALLBACK ( BMF_Steam_AchievementManager,
                   OnUnlock,
                   UserAchievementStored_t,
                   unlock_listener )
  {
    // Sometimes we receive event callbacks for games that aren't this one...
    //   ignore those!
    if (pParam->m_nGameID != BMF::SteamAPI::AppID ())
      return;

    BMF_SteamAchievement achievement (
      pParam->m_rgchAchievementName,
        steam_ctx.UserStats ()
    );

    if (pParam->m_nMaxProgress == 0 &&
        pParam->m_nCurProgress == 0) {
      if (! config.steam.nosound)
        PlaySound ( (LPCWSTR)unlock_sound, NULL, SND_ASYNC | SND_MEMORY );

      steam_log.Log (L" Achievement: '%hs' (%hs) - Unlocked!",
        achievement.name_, achievement.desc_);
    }

    else {
      steam_log.Log (L" Achievement: '%hs' (%hs) - "
                     L"Progress %lu / %lu (%04.01f%%)",
                achievement.name_,
                achievement.desc_,
                      pParam->m_nCurProgress,
                      pParam->m_nMaxProgress,
     100.0f * ((float)pParam->m_nCurProgress / (float)pParam->m_nMaxProgress));
    }
  }

  void* operator new (size_t size) {
    return HeapAlloc (hSteamHeap, HEAP_ZERO_MEMORY, size);
  }

  void operator delete (void* pMemory) {
    HeapFree (hSteamHeap, 0, pMemory);
  }

protected:
private:
  bool     default_loaded;
  uint8_t* unlock_sound;   // A .WAV (PCM) file
} *steam_achievements = nullptr;

#if 0
S_API typedef void (S_CALLTYPE *steam_unregister_callback_t)
                       (class CCallbackBase *pCallback);
S_API typedef void (S_CALLTYPE *steam_register_callback_t)
                       (class CCallbackBase *pCallback, int iCallback);

steam_register_callback_t SteamAPI_RegisterCallbackOrig = nullptr;


S_API bool S_CALLTYPE SteamAPI_Init_Detour (void);
#endif

void
BMF_UnlockSteamAchievement (int idx)
{
  //
  // If we got this far without initialization, something's weird - but
  //   we CAN recover.
  //
  // * Lazy loading steam_api*.dll is supported though usually doesn't work.
  //
  if (! init)
    BMF::SteamAPI::Init (false);

  steam_log.LogEx (true, L" >> Attempting to Unlock Achievement: %i... ",
    idx );

  ISteamUserStats* stats = steam_ctx.UserStats ();

  if (stats) {
    // I am dubious about querying these things by name, so duplicate this
    //   string immediately.
    const char* szName = strdup (stats->GetAchievementName (idx));

    if (szName != nullptr) {
      steam_log.LogEx (false, L" (%hs - Found)\n", szName);

      UserAchievementStored_t store;
      store.m_nCurProgress = 0;
      store.m_nMaxProgress = 0;
      strncpy (store.m_rgchAchievementName, szName, 128);

      steam_achievements->OnUnlock (&store);

//      stats->ClearAchievement            (szName);
//      stats->IndicateAchievementProgress (szName, 0, 1);
//      stats->StoreStats                  ();
#if 0
      bool achieved;
      if (stats->GetAchievement (szName, &achieved)) {
        if (achieved) {
          steam_log.LogEx (true, L"Clearing first\n");
          stats->ClearAchievement            (szName);
          stats->StoreStats                  ();

          SteamAPI_RunCallbacks              ();
        } else {
          steam_log.LogEx (true, L"Truly unlocking\n");
          stats->SetAchievement              (szName);

          stats->StoreStats                  ();

          // Dispatch these ASAP, there's a lot of latency apparently...
          SteamAPI_RunCallbacks ();
        }
      }
      else {
        steam_log.LogEx (true, L" >> GetAchievement (...) Failed\n");
      }
#endif
      free ((void *)szName);
    }
    else {
      steam_log.LogEx (false, L" (None Found)\n");
    }
  } else {
    steam_log.LogEx (false, L" (ISteamUserStats is NULL?!)\n");
  }
}

// Fancy name, for something that barely does anything ...
//   most init is done in the BMF_SteamAPIContext singleton.
bool
BMF_Load_SteamAPI_Imports (HMODULE hDLL)
{
  SteamAPI_InitSafe =
    (SteamAPI_InitSafe_t)GetProcAddress (
       hDLL,
         "SteamAPI_InitSafe"
    );

  if (SteamAPI_InitSafe != nullptr)
    return true;

  return false;
}

void
BMF::SteamAPI::Init (bool pre_load)
{
  // We allow a fixed number of chances to initialize, and then we give up.
  static int  init_tries = 0;

  if (init)
    return;

  // We want to give the init a second-chance because it's not quite
  //  up to snuff yet, but some games would just continue to try and
  //   do this indefinitely.
  if (init_tries > 4)
    return;

  if (init_tries++ == 0) {
    steam_log.init ("steam_api.log", "w");
    steam_log.silent = config.steam.silent;

    steam_log.Log (L"Initializing SteamWorks Backend");
    steam_log.Log (L"-------------------------------\n");
  }

#ifdef _WIN64
  const wchar_t* steam_dll_str = L"steam_api64.dll";
#else
  const wchar_t* steam_dll_str = L"steam_api.dll";
#endif

  HMODULE hSteamAPI;
  bool    bImported;

  if (pre_load) {
    if (init_tries == 1) {
      steam_log.Log (L" @ %s was already loaded => Assuming "
                     L"library needs no initialization...", steam_dll_str);
    }

    hSteamAPI = GetModuleHandle           (steam_dll_str);
    bImported = BMF_Load_SteamAPI_Imports (hSteamAPI);

    steam_ctx.Init (hSteamAPI);
  }
  else {
    hSteamAPI = LoadLibrary               (steam_dll_str);
    bImported = BMF_Load_SteamAPI_Imports (hSteamAPI);

    if (bImported) {
      steam_log.LogEx (true, L" bool SteamAPI_InitSafe (void)... ");
      bool bRet = SteamAPI_InitSafe ();
      steam_log.LogEx (false, L"%s! (Status: %d) [%d-bit]\n",
                bRet ? L"done" : L"failed",
                bRet,
#ifdef _WIN64
        64
#else
        32
#endif
      );

      if (! bRet) {
        init = false;
        return;
      }

      steam_ctx.Init (hSteamAPI);
    }
  }

  if (! bImported) {
    init = false;
    return;
  }

  ISteamUserStats* stats = steam_ctx.UserStats ();

  if (stats)
    stats->RequestCurrentStats ();
  else 
  // Close, but no - we still have not initialized this monster.
  {
    init = false;
    return;
  }

  steam_log.Log (L" Creating Achievement Manager...");

  steam_achievements = new BMF_Steam_AchievementManager (
      config.steam.achievement_sound.c_str ()
    );

  steam_log.LogEx (false, L"\n");

  // Phew, finally!
  steam_log.Log (L"--- Initialization Finished (%d tries) ---", init_tries);

  init = true;
}

void
BMF::SteamAPI::Shutdown (void)
{
  BMF_AutoClose_Log (steam_log);

  steam_ctx.Shutdown ();
}

void BMF::SteamAPI::Pump (void)
{
#if 0
  if (steam_ctx.UserStats ()) {
    if (SteamAPI_RunCallbacks != nullptr)
      SteamAPI_RunCallbacks ();
  } else {
    Init (true);
  }
#endif
}

uint32_t
BMF::SteamAPI::AppID (void)
{
  ISteamUtils* utils = steam_ctx.Utils ();

  if (utils != nullptr)
    return utils->GetAppID ();


  return 0;
}
