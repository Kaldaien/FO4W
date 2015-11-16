#include <string>

#include "ini.h"
#include "parameter.h"
#include "utility.h"

bmf::ParameterFactory fo4_factory;

bmf::INI::File*       fo4_prefs      = nullptr;
bmf::ParameterBool*   fo4_fullscreen = nullptr;
bmf::ParameterBool*   fo4_borderless = nullptr;

void
BMF_FO4_InitPlugin (void)
{
  if (fo4_prefs == nullptr) {
    std::wstring fo4_prefs_file =
      BMF_GetDocumentsDir () +
      std::wstring (L"\\My Games\\Fallout4\\Fallout4Prefs.ini");

    fo4_prefs = new bmf::INI::File (fo4_prefs_file.c_str ());
    fo4_prefs->parse ();
  }
}

bool
BMF_FO4_IsFullscreen (void)
{
  BMF_FO4_InitPlugin ();

  if (fo4_fullscreen == nullptr) {
    fo4_fullscreen = 
      static_cast <bmf::ParameterBool *>
        (fo4_factory.create_parameter <bool> (L"Fullscreen Mode"));
    fo4_fullscreen->register_to_ini ( fo4_prefs,
                                        L"Display",
                                          L"bFull Screen" );

    fo4_fullscreen->load ();
  }

  return (fo4_fullscreen->get_value ());
}

bool
BMF_FO4_IsBorderlessWindow (void)
{
  BMF_FO4_InitPlugin ();

  if (fo4_borderless == nullptr) {
    fo4_borderless = 
      static_cast <bmf::ParameterBool *>
        (fo4_factory.create_parameter <bool> (L"Borderless Window"));
    fo4_borderless->register_to_ini ( fo4_prefs,
                                        L"Display",
                                          L"bBorderless" );

    fo4_borderless->load ();
  }

  return (fo4_borderless->get_value ());
}