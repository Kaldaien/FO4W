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
#include "ini.h"
#include "log.h"

#define BMF_VER_STR L"0.09"

static bmf::INI::File*  dll_ini = nullptr;

bmf_config_t config;

bmf::ParameterFactory g_ParameterFactory;

struct {
  struct {
    bmf::ParameterBool*  show;
    bmf::ParameterFloat* interval;
  } io;

  struct {
    bmf::ParameterBool*  show;
  } fps;

  struct {
    bmf::ParameterBool*  show;
  } memory;

  struct {
    bmf::ParameterBool*  show;
  } SLI;

  struct {
    bmf::ParameterBool*  show;
    bmf::ParameterFloat* interval;
  } cpu;

  struct {
    bmf::ParameterBool*  show;
    bmf::ParameterFloat* interval;
  } gpu;

  struct {
    bmf::ParameterBool*  show;
    bmf::ParameterFloat* interval;
    bmf::ParameterInt*   type;
  } disk;

  struct {
    bmf::ParameterBool*  show;
    bmf::ParameterFloat* interval;
  } pagefile;
} monitoring;

struct {
  bmf::ParameterBool*    show;

  struct {
    bmf::ParameterInt*   red;
    bmf::ParameterInt*   green;
    bmf::ParameterInt*   blue;
  } text;

  struct {
    bmf::ParameterInt*   scale;
    bmf::ParameterInt*   pos_x;
    bmf::ParameterInt*   pos_y;
  } viewport;
} osd;

bmf::ParameterFloat*     mem_reserve;
bmf::ParameterInt*       init_delay;
bmf::ParameterBool*      silent;
bmf::ParameterStringW*   version;



bool
BMF_LoadConfig (void) {
  // Load INI File
  dll_ini = new bmf::INI::File (L"dxgi.ini");

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

  monitoring.gpu.show =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (L"Show GPU Monitoring"));
  monitoring.gpu.show->register_to_ini (dll_ini, L"Monitor.GPU", L"Show");

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
      L"DXGI.System",
        L"InitDelay" );

  silent =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Log Silence")
      );
  silent->register_to_ini (
    dll_ini,
      L"DXGI.System",
        L"Silent" );

  version =
    static_cast <bmf::ParameterStringW *>
      (g_ParameterFactory.create_parameter <std::wstring> (
        L"Software Version")
      );
  version->register_to_ini (
    dll_ini,
      L"DXGI.System",
        L"Version" );




  osd.show =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"OSD Visibility")
      );
  osd.show->register_to_ini(
    dll_ini,
      L"DXGI.OSD",
        L"Show" );

  osd.text.red =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Color (Red)")
      );
  osd.text.red->register_to_ini (
    dll_ini,
      L"DXGI.OSD",
        L"TextColorRed" );

  osd.text.green =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Color (Green)")
      );
  osd.text.green->register_to_ini (
    dll_ini,
      L"DXGI.OSD",
        L"TextColorGreen" );

  osd.text.blue =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Color (Blue)")
      );
  osd.text.blue->register_to_ini (
    dll_ini,
      L"DXGI.OSD",
        L"TextColorBlue" );

  osd.viewport.pos_x =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Position (X)")
      );
  osd.viewport.pos_x->register_to_ini (
    dll_ini,
      L"DXGI.OSD",
        L"PositionX" );

  osd.viewport.pos_y =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Position (Y)")
      );
  osd.viewport.pos_y->register_to_ini (
    dll_ini,
      L"DXGI.OSD",
        L"PositionY" );

  osd.viewport.scale =
    static_cast <bmf::ParameterInt *>
      (g_ParameterFactory.create_parameter <int> (
        L"OSD Scale")
      );
  osd.viewport.scale->register_to_ini (
    dll_ini,
      L"DXGI.OSD",
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

  if (monitoring.gpu.show->load ())
    config.gpu.show = monitoring.gpu.show->get_value ();
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

  if (osd.show->load ())
    config.osd.show = osd.show->get_value ();

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

  if (init_delay->load ())
    config.system.init_delay = init_delay->get_value ();
  if (silent->load ())
    config.system.silent = silent->get_value ();
  if (version->load ())
    config.system.version = version->get_value ();

  if (dll_ini->get_sections ().size () > 0)
    return true;

  return false;
}

void
BMF_SaveConfig (bool close_config) {
  monitoring.memory.show->set_value       (config.mem.show);
  mem_reserve->set_value                  (config.mem.reserve);

  monitoring.fps.show->set_value          (config.fps.show);

  monitoring.io.show->set_value           (config.io.show);
  monitoring.io.interval->set_value       (config.io.interval);

  monitoring.cpu.show->set_value          (config.cpu.show);
  monitoring.cpu.interval->set_value      (config.cpu.interval);

  monitoring.gpu.show->set_value          (config.gpu.show);
  monitoring.gpu.interval->set_value      (config.gpu.interval);

  monitoring.disk.show->set_value         (config.disk.show);
  monitoring.disk.interval->set_value     (config.disk.interval);
  monitoring.disk.type->set_value         (config.disk.type);

  monitoring.pagefile.show->set_value     (config.pagefile.show);
  monitoring.pagefile.interval->set_value (config.pagefile.interval);

  osd.show->set_value                     (config.osd.show);
  osd.text.red->set_value                 (config.osd.red);
  osd.text.green->set_value               (config.osd.green);
  osd.text.blue->set_value                (config.osd.blue);
  osd.viewport.pos_x->set_value           (config.osd.pos_x);
  osd.viewport.pos_y->set_value           (config.osd.pos_y);
  osd.viewport.scale->set_value           (config.osd.scale);

  monitoring.SLI.show->set_value          (config.sli.show);

  init_delay->set_value                   (config.system.init_delay);
  silent->set_value                       (config.system.silent);

  monitoring.memory.show->store       ();
  mem_reserve->store                  ();

  monitoring.SLI.show->store          ();

  monitoring.fps.show->store          ();

  monitoring.io.show->store           ();
  monitoring.io.interval->store       ();

  monitoring.cpu.show->store          ();
  monitoring.cpu.interval->store      ();

  monitoring.gpu.show->store          ();
  monitoring.gpu.interval->store      ();

  monitoring.disk.show->store         ();
  monitoring.disk.interval->store     ();
  monitoring.disk.type->store         ();

  monitoring.pagefile.show->store     ();
  monitoring.pagefile.interval->store ();

  osd.show->store                     ();
  osd.text.red->store                 ();
  osd.text.green->store               ();
  osd.text.blue->store                ();
  osd.viewport.pos_x->store           ();
  osd.viewport.pos_y->store           ();
  osd.viewport.scale->store           ();

  init_delay->store                   ();
  silent->store                       ();

  version->set_value                  (BMF_VER_STR);
  version->store                      ();

  dll_ini->write (L"dxgi.ini");

  if (close_config) {
    if (dll_ini != nullptr) {
      delete dll_ini;
      dll_ini = nullptr;
    }
  }
}