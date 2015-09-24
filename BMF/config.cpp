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
  bmf::ParameterBool*  enable_io;
  bmf::ParameterFloat* io_interval;
  bmf::ParameterBool*  enable_memory;
  bmf::ParameterBool*  enable_SLI;
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
  monitoring.enable_io =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (L"Enable IO Monitoring"));
  monitoring.enable_io->register_to_ini (dll_ini, L"Monitor.IO", L"Enable");

  monitoring.io_interval =
    static_cast <bmf::ParameterFloat *>
     (g_ParameterFactory.create_parameter <float> (L"IO Monitoring Interval"));
  monitoring.io_interval->register_to_ini(dll_ini, L"Monitor.IO", L"Interval");

  monitoring.enable_memory =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Enable Memory Monitoring")
      );
  monitoring.enable_memory->register_to_ini (
    dll_ini,
      L"Monitor.Memory",
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

  monitoring.enable_SLI =
    static_cast <bmf::ParameterBool *>
      (g_ParameterFactory.create_parameter <bool> (
        L"Enable SLI Monitoring")
      );
  monitoring.enable_SLI->register_to_ini (
    dll_ini,
      L"Monitor.SLI",
        L"Enable" );

  //
  // Load Parameters
  //
  if (monitoring.enable_io->load ())
    config.io_stats = monitoring.enable_io->get_value ();
  if (monitoring.io_interval->load ())
    config.io_interval = monitoring.io_interval->get_value ();

  if (monitoring.enable_memory->load ())
    config.mem_stats = monitoring.enable_memory->get_value ();
  if (mem_reserve->load ())
    config.mem_reserve = mem_reserve->get_value ();

  if (monitoring.enable_SLI->load ())
    config.sli_stats = monitoring.enable_SLI->get_value ();

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
  monitoring.enable_memory->set_value (config.mem_stats);
  mem_reserve->set_value (config.mem_reserve);

  monitoring.enable_io->set_value     (config.io_stats);
  monitoring.io_interval->set_value   (config.io_interval);
  monitoring.enable_SLI->set_value    (config.sli_stats);

  init_delay->set_value               (config.init_delay);
  silent->set_value                   (config.silent);

  monitoring.enable_memory->store ();
  mem_reserve->store              ();

  monitoring.enable_SLI->store    ();
  monitoring.enable_io->store     ();
  monitoring.io_interval->store   ();

  init_delay->store               ();
  silent->store                   ();

  dll_ini->write (L"dxgi.ini");

  if (dll_ini != nullptr) {
    delete dll_ini;
    dll_ini = nullptr;
  }
}