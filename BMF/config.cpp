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

#include "config.h"
#include "parameter.h"
#include "import.h"
#include "ini.h"
#include "log.h"

std::wstring BMF_VER_STR = L"0.15";

static bmf::INI::File*  dll_ini = nullptr;

bmf_config_t config;

bmf::ParameterFactory g_ParameterFactory;

struct {
  struct {
    bmf::ParameterBool*    show;
  } time;

  struct {
    bmf::ParameterBool*    show;
    bmf::ParameterFloat*   interval;
  } io;

  struct {
    bmf::ParameterBool*    show;
  } fps;

  struct {
    bmf::ParameterBool*    show;
  } memory;

  struct {
    bmf::ParameterBool*    show;
  } SLI;

  struct {
    bmf::ParameterBool*    show;
    bmf::ParameterFloat*   interval;
    bmf::ParameterBool*    simple;
  } cpu;

  struct {
    bmf::ParameterBool*    show;
    bmf::ParameterBool*    print_slowdown;
    bmf::ParameterFloat*   interval;
  } gpu;

  struct {
    bmf::ParameterBool*    show;
    bmf::ParameterFloat*   interval;
    bmf::ParameterInt*     type;
  } disk;

  struct {
    bmf::ParameterBool*    show;
    bmf::ParameterFloat*   interval;
  } pagefile;
} monitoring;

struct {
  bmf::ParameterBool*      show;

  struct {
    bmf::ParameterBool*    pump;
    bmf::ParameterFloat*   pump_interval;
  } update_method;

  struct {
    bmf::ParameterInt*     red;
    bmf::ParameterInt*     green;
    bmf::ParameterInt*     blue;
  } text;

  struct {
    bmf::ParameterInt*     scale;
    bmf::ParameterInt*     pos_x;
    bmf::ParameterInt*     pos_y;
  } viewport;
} osd;

struct {
  struct {
    bmf::ParameterStringW* sound_file;
    bmf::ParameterBool*    nosound;
    bmf::ParameterInt*     notify_corner;
    bmf::ParameterInt*     notify_insetX;
    bmf::ParameterInt*     notify_insetY;
  } achievements;

  struct {
    bmf::ParameterBool*    silent;
  } log;
} steam;

bmf::ParameterFloat*     mem_reserve;
bmf::ParameterInt*       init_delay;
bmf::ParameterBool*      silent;
bmf::ParameterStringW*   version;
bmf::ParameterBool*      prefer_fahrenheit;


bool
BMF_LoadConfig (std::wstring name) {
  // Load INI File
  std::wstring full_name = name + L".ini";
  dll_ini = new bmf::INI::File ((wchar_t *)full_name.c_str ());

  bool empty = dll_ini->get_sections ().empty ();

  //
  // Create Parameters
  //
  monitoring.io.show =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (L"Show IO Monitoring"));
  monitoring.io.show->register_to_ini (dll_ini, L"Monitor.IO", L"Show");

  monitoring.io.interval =
    static_cast <bmf::ParameterFloat *>
     (g_ParameterFactory.create_parameter <float> (L"IO Monitoring Interval"));
  monitoring.io.interval->register_to_ini(dll_ini, L"Monitor.IO", L"Interval");

  monitoring.disk.show =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (L"Show Disk Monitoring"));
  monitoring.disk.show->register_to_ini(dll_ini, L"Monitor.Disk", L"Show");

  monitoring.disk.interval =
    static_cast <bmf::ParameterFloat *>
     (g_ParameterFactory.create_parameter <float> (
       L"Disk Monitoring Interval")
     );
  monitoring.disk.interval->register_to_ini (
    dll_ini,
      L"Monitor.Disk",
        L"Interval" );

  monitoring.disk.type =
    static_cast <bmf::ParameterInt *>
     (g_ParameterFactory.create_parameter <int> (
       L"Disk Monitoring Type (0 = Physical, 1 = Logical)")
     );
  monitoring.disk.type->register_to_ini (
    dll_ini,
      L"Monitor.Disk",
        L"Type" );


  monitoring.cpu.show =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (L"Show CPU Monitoring"));
  monitoring.cpu.show->register_to_ini (dll_ini, L"Monitor.CPU", L"Show");

  monitoring.cpu.interval =
    static_cast <bmf::ParameterFloat *>
     (g_ParameterFactory.create_parameter <float> (
       L"CPU Monitoring Interval (seconds)")
     );
  monitoring.cpu.interval->register_to_ini (
    dll_ini,
      L"Monitor.CPU",
        L"Interval" );

  monitoring.cpu.simple =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (L"Minimal CPU info"));
  monitoring.cpu.simple->register_to_ini (dll_ini, L"Monitor.CPU", L"Simple");

  monitoring.gpu.show =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (L"Show GPU Monitoring"));
  monitoring.gpu.show->register_to_ini (dll_ini, L"Monitor.GPU", L"Show");

  monitoring.gpu.print_slowdown =
    static_cast <bmf::ParameterBool *>
    (g_ParameterFactory.create_parameter <bool>(L"Print GPU Slowdown Reason"));
  monitoring.gpu.print_slowdown->register_to_ini (
    dll_ini,
      L"Monitor.GPU",
        L"PrintSlowdown" );

  monitoring.gpu.interval =
    static_cast <bmf::ParameterFloat *>
     (g_ParameterFactory.create_parameter <float> (
       L"GPU Monitoring Interval (seconds)")
     );
  monitoring.gpu.interval->register_to_ini (
    dll_ini,
      L"Monitor.GPU",
        L"Interval" );


  monitoring.pagefile.show =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Show Pagefile Monitoring")
      );
  monitoring.pagefile.show->register_to_ini (
    dll_ini,
      L"Monitor.Pagefile",
        L"Show" );

  monitoring.pagefile.interval =
    static_cast <bmf::ParameterFloat *>
     (g_ParameterFactory.create_parameter <float> (
       L"Pagefile Monitoring Interval (seconds)")
     );
  monitoring.pagefile.interval->register_to_ini (
    dll_ini,
      L"Monitor.Pagefile",
        L"Interval" );


  monitoring.memory.show =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Show Memory Monitoring")
      );
  monitoring.memory.show->register_to_ini (
    dll_ini,
      L"Monitor.Memory",
        L"Show" );


  monitoring.fps.show =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Show Framerate Monitoring")
      );
  monitoring.fps.show->register_to_ini (
    dll_ini,
      L"Monitor.FPS",
        L"Show" );


  monitoring.time.show =
    static_cast <bmf::ParameterBool *>
    (g_ParameterFactory.create_parameter <bool> (
      L"Show Time")
    );
  monitoring.time.show->register_to_ini (
    dll_ini,
      L"Monitor.Time",
        L"Show" );


  mem_reserve =
    static_cast <bmf::ParameterFloat *>
      (g_ParameterFactory.create_parameter <float> (
        L"Memory Reserve Percentage")
      );
  mem_reserve->register_to_ini (
    dll_ini,
      L"Manage.Memory",
        L"ReservePercent" );


  init_delay =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"Initialization Delay (msecs)")
      );
  init_delay->register_to_ini (
    dll_ini,
      L"RSFN.System",
        L"InitDelay" );

  silent =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Log Silence")
      );
  silent->register_to_ini (
    dll_ini,
      L"RSFN.System",
        L"Silent" );

  prefer_fahrenheit =
    static_cast <bmf::ParameterBool *>
    (g_ParameterFactory.create_parameter <bool> (
      L"Prefer Fahrenheit Units")
      );
  prefer_fahrenheit->register_to_ini (
    dll_ini,
      L"RSFN.System",
        L"PreferFahrenheit" );

  version =
    static_cast <bmf::ParameterStringW *>
      (g_ParameterFactory.create_parameter <std::wstring> (
        L"Software Version")
      );
  version->register_to_ini (
    dll_ini,
      L"RSFN.System",
        L"Version" );




  osd.show =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"OSD Visibility")
      );
  osd.show->register_to_ini (
    dll_ini,
      L"RSFN.OSD",
        L"Show" );

  osd.update_method.pump =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Refresh the OSD irrespective of frame completion")
      );
  osd.update_method.pump->register_to_ini (
    dll_ini,
      L"RSFN.OSD",
        L"AutoPump" );

  osd.update_method.pump_interval =
    static_cast <bmf::ParameterFloat *>
    (g_ParameterFactory.create_parameter <float> (
      L"Time in seconds between OSD updates")
    );
  osd.update_method.pump_interval->register_to_ini (
    dll_ini,
      L"RSFN.OSD",
        L"PumpInterval" );

  osd.text.red =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Color (Red)")
      );
  osd.text.red->register_to_ini (
    dll_ini,
      L"RSFN.OSD",
        L"TextColorRed" );

  osd.text.green =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Color (Green)")
      );
  osd.text.green->register_to_ini (
    dll_ini,
      L"RSFN.OSD",
        L"TextColorGreen" );

  osd.text.blue =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Color (Blue)")
      );
  osd.text.blue->register_to_ini (
    dll_ini,
      L"RSFN.OSD",
        L"TextColorBlue" );

  osd.viewport.pos_x =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Position (X)")
      );
  osd.viewport.pos_x->register_to_ini (
    dll_ini,
      L"RSFN.OSD",
        L"PositionX" );

  osd.viewport.pos_y =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Position (Y)")
      );
  osd.viewport.pos_y->register_to_ini (
    dll_ini,
      L"RSFN.OSD",
        L"PositionY" );

  osd.viewport.scale =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Scale")
      );
  osd.viewport.scale->register_to_ini (
    dll_ini,
      L"RSFN.OSD",
        L"Scale" );


  monitoring.SLI.show =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Show SLI Monitoring")
      );
  monitoring.SLI.show->register_to_ini (
    dll_ini,
      L"Monitor.SLI",
        L"Show" );


  steam.achievements.sound_file =
    static_cast <bmf::ParameterStringW *>
      (g_ParameterFactory.create_parameter <std::wstring> (
        L"Achievement Sound File")
      );
  steam.achievements.sound_file->register_to_ini(
    dll_ini,
      L"Steam.Achievements",
        L"SoundFile" );

  steam.achievements.nosound =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Silence is Bliss?")
      );
  steam.achievements.nosound->register_to_ini(
    dll_ini,
      L"Steam.Achievements",
        L"NoSound" );

  steam.achievements.notify_corner =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"Achievement Notification Position")
      );
  steam.achievements.notify_corner->register_to_ini (
    dll_ini,
      L"Steam.Achievements",
        L"NotifyCorner" );

  steam.achievements.notify_insetX =
    static_cast <bmf::ParameterInt *>
    (g_ParameterFactory.create_parameter <int> (
      L"Achievement Notification Inset X")
    );
  steam.achievements.notify_insetX->register_to_ini (
    dll_ini,
      L"Steam.Achievements",
        L"NotifyInsetX" );

  steam.achievements.notify_insetY =
    static_cast <bmf::ParameterInt *>
    (g_ParameterFactory.create_parameter <int> (
      L"Achievement Notification Inset Y")
    );
  steam.achievements.notify_insetY->register_to_ini (
    dll_ini,
      L"Steam.Achievements",
        L"NotifyInsetY" );

  steam.log.silent =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Makes steam_api.log go away")
      );
  steam.log.silent->register_to_ini(
    dll_ini,
      L"Steam.Log",
        L"Silent" );

  const std::map <std::wstring, bmf::INI::File::Section>& sections =
    dll_ini->get_sections ();

  std::map <std::wstring, bmf::INI::File::Section>::const_iterator sec =
    sections.begin ();

  int import = 0;

  while (sec != sections.end ()) {
    if (wcsstr ((*sec).first.c_str (), L"Import.")) {
      imports [import].filename = 
         static_cast <bmf::ParameterStringW *>
             (g_ParameterFactory.create_parameter <std::wstring> (
                L"Import Filename")
             );
      imports [import].filename->register_to_ini (
        dll_ini,
          (*sec).first.c_str (),
            L"Filename" );

      imports [import].when = 
         static_cast <bmf::ParameterStringW *>
             (g_ParameterFactory.create_parameter <std::wstring> (
                L"Import Timeframe")
             );
      imports [import].when->register_to_ini (
        dll_ini,
          (*sec).first.c_str (),
            L"When" );

      imports [import].role = 
         static_cast <bmf::ParameterStringW *>
             (g_ParameterFactory.create_parameter <std::wstring> (
                L"Import Role")
             );
      imports [import].role->register_to_ini (
        dll_ini,
          (*sec).first.c_str (),
            L"Role" );

      imports [import].architecture = 
         static_cast <bmf::ParameterStringW *>
             (g_ParameterFactory.create_parameter <std::wstring> (
                L"Import Architecture")
             );
      imports [import].architecture->register_to_ini (
        dll_ini,
          (*sec).first.c_str (),
            L"Architecture" );

      imports [import].filename->load     ();
      imports [import].when->load         ();
      imports [import].role->load         ();
      imports [import].architecture->load ();

      ++import;

      if (import >= BMF_MAX_IMPORTS)
        break;
    }

    ++sec;
  }

  //
  // Load Parameters
  //
  if (monitoring.io.show->load ())
    config.io.show = monitoring.io.show->get_value ();
  if (monitoring.io.interval->load ())
    config.io.interval = monitoring.io.interval->get_value ();

  if (monitoring.fps.show->load ())
    config.fps.show = monitoring.fps.show->get_value ();

  if (monitoring.memory.show->load ())
    config.mem.show = monitoring.memory.show->get_value ();
  if (mem_reserve->load ())
    config.mem.reserve = mem_reserve->get_value ();

  if (monitoring.cpu.show->load ())
    config.cpu.show = monitoring.cpu.show->get_value ();
  if (monitoring.cpu.interval->load ())
    config.cpu.interval = monitoring.cpu.interval->get_value ();
  if (monitoring.cpu.simple->load ())
    config.cpu.simple = monitoring.cpu.simple->get_value ();

  if (monitoring.gpu.show->load ())
    config.gpu.show = monitoring.gpu.show->get_value ();
  if (monitoring.gpu.print_slowdown->load ())
    config.gpu.print_slowdown = monitoring.gpu.print_slowdown->get_value ();
  if (monitoring.gpu.interval->load ())
    config.gpu.interval = monitoring.gpu.interval->get_value ();

  if (monitoring.disk.show->load ())
    config.disk.show = monitoring.disk.show->get_value ();
  if (monitoring.disk.interval->load ())
    config.disk.interval = monitoring.disk.interval->get_value ();
  if (monitoring.disk.type->load ())
    config.disk.type = monitoring.disk.type->get_value ();

  if (monitoring.pagefile.show->load ())
    config.pagefile.show = monitoring.pagefile.show->get_value ();
  if (monitoring.pagefile.interval->load ())
    config.pagefile.interval = monitoring.pagefile.interval->get_value ();

  if (monitoring.time.show->load ())
    config.time.show = monitoring.time.show->get_value ();

  if (osd.show->load ())
    config.osd.show = osd.show->get_value ();

  if (osd.update_method.pump->load ())
    config.osd.pump = osd.update_method.pump->get_value ();

  if (osd.update_method.pump_interval->load ())
    config.osd.pump_interval = osd.update_method.pump_interval->get_value ();

  if (osd.text.red->load ())
    config.osd.red = osd.text.red->get_value ();
  if (osd.text.green->load ())
    config.osd.green = osd.text.green->get_value ();
  if (osd.text.blue->load ())
    config.osd.blue = osd.text.blue->get_value ();

  if (osd.viewport.pos_x->load ())
    config.osd.pos_x = osd.viewport.pos_x->get_value ();
  if (osd.viewport.pos_y->load ())
    config.osd.pos_y = osd.viewport.pos_y->get_value ();
  if (osd.viewport.scale->load ())
    config.osd.scale = osd.viewport.scale->get_value ();

  if (monitoring.SLI.show->load ())
    config.sli.show = monitoring.SLI.show->get_value ();

  if (steam.achievements.nosound->load ())
    config.steam.nosound = steam.achievements.nosound->get_value ();
  if (steam.achievements.sound_file->load ())
    config.steam.achievement_sound =
      steam.achievements.sound_file->get_value ();
  if (steam.achievements.notify_corner->load ())
    config.steam.notify_corner =
      steam.achievements.notify_corner->get_value ();
  if (steam.achievements.notify_insetX->get_value ())
    config.steam.inset_x = steam.achievements.notify_insetX->get_value ();
  if (steam.achievements.notify_insetY->get_value ())
    config.steam.inset_y = steam.achievements.notify_insetY->get_value ();
  if (steam.log.silent->load ())
    config.steam.silent = steam.log.silent->get_value ();

  if (init_delay->load ())
    config.system.init_delay = init_delay->get_value ();
  if (silent->load ())
    config.system.silent = silent->get_value ();
  if (prefer_fahrenheit->load ())
    config.system.prefer_fahrenheit = prefer_fahrenheit->get_value ();
  if (version->load ())
    config.system.version = version->get_value ();

  if (empty)
    return false;

  return true;
}

void
BMF_SaveConfig (std::wstring name, bool close_config) {
  monitoring.memory.show->set_value          (config.mem.show);
  mem_reserve->set_value                     (config.mem.reserve);

  monitoring.fps.show->set_value             (config.fps.show);

  monitoring.io.show->set_value              (config.io.show);
  monitoring.io.interval->set_value          (config.io.interval);

  monitoring.cpu.show->set_value             (config.cpu.show);
  monitoring.cpu.interval->set_value         (config.cpu.interval);
  monitoring.cpu.simple->set_value           (config.cpu.simple);

  monitoring.gpu.show->set_value             (config.gpu.show);
  monitoring.gpu.print_slowdown->set_value   (config.gpu.print_slowdown);
  monitoring.gpu.interval->set_value         (config.gpu.interval);

  monitoring.disk.show->set_value            (config.disk.show);
  monitoring.disk.interval->set_value        (config.disk.interval);
  monitoring.disk.type->set_value            (config.disk.type);

  monitoring.pagefile.show->set_value        (config.pagefile.show);
  monitoring.pagefile.interval->set_value    (config.pagefile.interval);

  osd.show->set_value                        (config.osd.show);
  osd.update_method.pump->set_value          (config.osd.pump);
  osd.update_method.pump_interval->set_value (config.osd.pump_interval);
  osd.text.red->set_value                    (config.osd.red);
  osd.text.green->set_value                  (config.osd.green);
  osd.text.blue->set_value                   (config.osd.blue);
  osd.viewport.pos_x->set_value              (config.osd.pos_x);
  osd.viewport.pos_y->set_value              (config.osd.pos_y);
  osd.viewport.scale->set_value              (config.osd.scale);

  monitoring.SLI.show->set_value             (config.sli.show);
  monitoring.time.show->set_value            (config.time.show);

  steam.achievements.sound_file->set_value    (config.steam.achievement_sound);
  steam.achievements.nosound->set_value       (config.steam.nosound);
  steam.achievements.notify_corner->set_value (config.steam.notify_corner);
  steam.achievements.notify_insetX->set_value (config.steam.inset_x);
  steam.achievements.notify_insetY->set_value (config.steam.inset_y);
  steam.log.silent->set_value                 (config.steam.silent);

  init_delay->set_value                      (config.system.init_delay);
  silent->set_value                          (config.system.silent);
  prefer_fahrenheit->set_value               (config.system.prefer_fahrenheit);

  monitoring.memory.show->store          ();
  mem_reserve->store                     ();

  monitoring.SLI.show->store             ();
  monitoring.time.show->store            ();

  monitoring.fps.show->store             ();

  monitoring.io.show->store              ();
  monitoring.io.interval->store          ();

  monitoring.cpu.show->store             ();
  monitoring.cpu.interval->store         ();
  monitoring.cpu.simple->store           ();

  monitoring.gpu.show->store             ();
  monitoring.gpu.print_slowdown->store   ();
  monitoring.gpu.interval->store         ();

  monitoring.disk.show->store            ();
  monitoring.disk.interval->store        ();
  monitoring.disk.type->store            ();

  monitoring.pagefile.show->store        ();
  monitoring.pagefile.interval->store    ();

  osd.show->store                        ();
  osd.update_method.pump->store          ();
  osd.update_method.pump_interval->store ();
  osd.text.red->store                    ();
  osd.text.green->store                  ();
  osd.text.blue->store                   ();
  osd.viewport.pos_x->store              ();
  osd.viewport.pos_y->store              ();
  osd.viewport.scale->store              ();

  steam.achievements.sound_file->store    ();
  steam.achievements.nosound->store       ();
  steam.achievements.notify_corner->store ();
  steam.achievements.notify_insetX->store ();
  steam.achievements.notify_insetY->store ();
  steam.log.silent->store                 ();

  init_delay->store                      ();
  silent->store                          ();
  prefer_fahrenheit->store               ();

  version->set_value                     (BMF_VER_STR);
  version->store                         ();

  dll_ini->write (name + L".ini");

  if (close_config) {
    if (dll_ini != nullptr) {
      delete dll_ini;
      dll_ini = nullptr;
    }
  }
}