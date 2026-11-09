YYToolkit / Aurie headers go here.

Run ..\fetch_deps.bat from the project root, or manually copy the
"ChroniconDPS/include" folder from https://github.com/atty303/chronicon-dps
into this directory.

Expected layout (the vcxproj adds this folder to the include path):

  extern\YYToolkit\Shared.hpp
  extern\YYToolkit\Shared.cpp
  extern\Aurie\shared.hpp
  extern\FunctionWrapper\FunctionWrapper.hpp

These are third-party headers. A local copy has ALREADY been fetched into this
folder, so build.bat works offline once Visual Studio is installed. If you
re-clone the project or this folder is empty, run ..\fetch_deps.bat (it has a
curl.exe + GitHub API fallback). They are not part of this project's own source.

Version note: this header set comes from atty303/chronicon-dps and matches
the Aurie v1.x / YYToolkit API (the generation that ships AurieManager.exe
with the "Add Game" button). Aurie v2.x changed module entrypoints, hook
defaults and callback verification, so installing against v2.0.2 may need
Gml.cpp / ModuleMain.cpp adaptation. See README.md section 3.
