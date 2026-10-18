# dk2ml/

The plugin API of the **Door Kickers 2 Modloader** (dk2ml), copied from the loader's `include/` (the loader's template
zip ships the same files in its `dk2ml\` folder):
- `dk2ml.h`: the C API. era's Lasers Enhanced uses safe hooks, the FRAME and MAP_LOADED events, the GUI kit,
  `AddMenuButton` (the Esc button, API 2), options on the Modloader screen and the settings folder.
- `dk2ml.hpp`: the header-only C++ layer. `src/game/Game.h`/`Game.cpp` declare every game name as a `dk2ml::Fn`/
  `Field`/`Global`/`Enum` binding, and `dk2ml::ResolveAll` resolves them.

To update, copy both files from the loader's `include/` over these, rebuild, and dry-run the plugin with the loader's
`symtest.exe` (`.\tools\install.ps1` does it). The manifest's first argument is the `DK2ML_API_VERSION` the plugin
needs.
