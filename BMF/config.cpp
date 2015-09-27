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

static bmf::INI::File*  dll_ini = nullptr;

bmf_config_t config;

bmf::ParameterFactory g_ParameterFactory;

struct {
  struct {
    bmf::ParameterBool*  enable;
    bmf::ParameterFloat* interval;
  } io;

  struct {
    bmf::ParameterBool*  enable;
  } fps;

  struct {
    bmf::ParameterBool*  enable;
  } memory;

  struct {
    bmf::ParameterBool*  enable;
  } SLI;

  struct {
    bmf::ParameterBool*  enable;
    bmf::ParameterFloat* interval;
  } cpu;

  struct {
    bmf::ParameterBool*  enable;
    bmf::ParameterFloat* interval;
  } gpu;

  struct {
    bmf::ParameterBool*  enable;
    bmf::ParameterFloat* interval;
  } disk;

  struct {
    bmf::ParameterBool*  enable;
    bmf::ParameterFloat* interval;
  } pagefile;
} monitoring;

bmf::ParameterFloat* mem_reserve;
bmf::ParameterInt*   init_delay;
bmf::ParameterBool*  silent;




bool
BMF_LoadConfig (void) {
  // Load INI File
  dll_ini = new bmf::INI::File (L"dxgi.ini");

  //
  // Create Parameters
  //
  monitoring.io.enable =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (L"Enable IO Monitoring"));
  monitoring.io.enable->register_to_ini (dll_ini, L"Monitor.IO", L"Enable");

  monitoring.io.interval =
    static_cast <bmf::ParameterFloat *>
     (g_ParameterFactory.create_parameter <float> (L"IO Monitoring Interval"));
  monitoring.io.interval->register_to_ini(dll_ini, L"Monitor.IO", L"Interval");

  monitoring.disk.enable =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (L"Enable Disk Monitoring"));
  monitoring.disk.enable->register_to_ini(dll_ini, L"Monitor.Disk", L"Enable");

  monitoring.disk.interval =
    static_cast <bmf::ParameterFloat *>
     (g_ParameterFactory.create_parameter <float> (
       L"Disk Monitoring Interval")
     );
  monitoring.disk.interval->register_to_ini (
    dll_ini,
      L"Monitor.Disk",
        L"Interval" );


  monitoring.cpu.enable =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (L"Enable CPU Monitoring"));
  monitoring.cpu.enable->register_to_ini(dll_ini, L"Monitor.CPU", L"Enable");

  monitoring.cpu.interval =
    static_cast <bmf::ParameterFloat *>
     (g_ParameterFactory.create_parameter <float> (
       L"CPU Monitoring Interval (seconds)")
     );
  monitoring.cpu.interval->register_to_ini (
    dll_ini,
      L"Monitor.CPU",
        L"Interval" );

  monitoring.gpu.enable =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (L"Enable GPU Monitoring"));
  monitoring.gpu.enable->register_to_ini(dll_ini, L"Monitor.GPU", L"Enable");

  monitoring.gpu.interval =
    static_cast <bmf::ParameterFloat *>
     (g_ParameterFactory.create_parameter <float> (
       L"GPU Monitoring Interval (seconds)")
     );
  monitoring.gpu.interval->register_to_ini (
    dll_ini,
      L"Monitor.GPU",
        L"Interval" );


  monitoring.pagefile.enable =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Enable Pagefile Monitoring")
      );
  monitoring.pagefile.enable->register_to_ini (
    dll_ini,
      L"Monitor.Pagefile",
        L"Enable" );

  monitoring.pagefile.interval =
    static_cast <bmf::ParameterFloat *>
     (g_ParameterFactory.create_parameter <float> (
       L"Pagefile Monitoring Interval (seconds)")
     );
  monitoring.pagefile.interval->register_to_ini (
    dll_ini,
      L"Monitor.Pagefile",
        L"Interval" );


  monitoring.memory.enable =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Enable Memory Monitoring")
      );
  monitoring.memory.enable->register_to_ini (
    dll_ini,
      L"Monitor.Memory",
        L"Enable" );


  monitoring.fps.enable =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Enable Framerate Monitoring")
      );
  monitoring.fps.enable->register_to_ini (
    dll_ini,
      L"Monitor.FPS",
        L"Enable" );


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

  monitoring.SLI.enable =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Enable SLI Monitoring")
      );
  monitoring.SLI.enable->register_to_ini (
    dll_ini,
      L"Monitor.SLI",
        L"Enable" );

  //
  // Load Parameters
  //
  if (monitoring.io.enable->load ())
    config.io_stats = monitoring.io.enable->get_value ();
  if (monitoring.io.interval->load ())
    config.io_interval = monitoring.io.interval->get_value ();

  if (monitoring.fps.enable->load ())
    config.mem_stats = monitoring.fps.enable->get_value ();

  if (monitoring.memory.enable->load ())
    config.mem_stats = monitoring.memory.enable->get_value ();
  if (mem_reserve->load ())
    config.mem_reserve = mem_reserve->get_value ();

  if (monitoring.cpu.enable->load ())
    config.cpu_stats = monitoring.cpu.enable->get_value ();
  if (monitoring.cpu.interval->load ())
    config.cpu_interval = monitoring.cpu.interval->get_value ();

  if (monitoring.gpu.enable->load ())
    config.gpu_stats = monitoring.gpu.enable->get_value ();
  if (monitoring.gpu.interval->load ())
    config.gpu_interval = monitoring.gpu.interval->get_value ();

  if (monitoring.disk.enable->load ())
    config.disk_stats = monitoring.disk.enable->get_value ();
  if (monitoring.disk.interval->load ())
    config.disk_interval = monitoring.disk.interval->get_value ();

  if (monitoring.pagefile.enable->load ())
    config.pagefile_stats = monitoring.pagefile.enable->get_value ();
  if (monitoring.pagefile.interval->load ())
    config.pagefile_interval = monitoring.pagefile.interval->get_value ();

  if (monitoring.SLI.enable->load ())
    config.sli_stats = monitoring.SLI.enable->get_value ();

  if (init_delay->load ())
    config.init_delay = init_delay->get_value ();
  if (silent->load ())
    config.silent = silent->get_value ();


  if (dll_ini->get_sections ().size () > 0)
    return true;

  return false;
}

void
BMF_SaveConfig (void) {
  monitoring.memory.enable->set_value     (config.mem_stats);
  mem_reserve->set_value                  (config.mem_reserve);

  monitoring.fps.enable->set_value        (config.fps_stats);

  monitoring.io.enable->set_value         (config.io_stats);
  monitoring.io.interval->set_value       (config.io_interval);

  monitoring.cpu.enable->set_value        (config.cpu_stats);
  monitoring.cpu.interval->set_value      (config.cpu_interval);

  monitoring.gpu.enable->set_value        (config.gpu_stats);
  monitoring.gpu.interval->set_value      (config.gpu_interval);

  monitoring.disk.enable->set_value       (config.disk_stats);
  monitoring.disk.interval->set_value     (config.disk_interval);

  monitoring.pagefile.enable->set_value   (config.pagefile_stats);
  monitoring.pagefile.interval->set_value (config.pagefile_interval);

  monitoring.SLI.enable->set_value        (config.sli_stats);

  init_delay->set_value                   (config.init_delay);
  silent->set_value                       (config.silent);

  monitoring.memory.enable->store     ();
  mem_reserve->store                  ();

  monitoring.SLI.enable->store        ();

  monitoring.fps.enable->store        ();

  monitoring.io.enable->store         ();
  monitoring.io.interval->store       ();

  monitoring.cpu.enable->store        ();
  monitoring.cpu.interval->store      ();

  monitoring.gpu.enable->store        ();
  monitoring.gpu.interval->store      ();

  monitoring.disk.enable->store       ();
  monitoring.disk.interval->store     ();

  monitoring.pagefile.enable->store   ();
  monitoring.pagefile.interval->store ();

  init_delay->store                   ();
  silent->store                       ();

  dll_ini->write (L"dxgi.ini");

  if (dll_ini != nullptr) {
    delete dll_ini;
    dll_ini = nullptr;
  }
}