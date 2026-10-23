# dk2ml/

The plugin API of the **Door Kickers 2 Modloader** (dk2ml), copied from the loader's `include/` (the loader's template
zip ships the same files in its `dk2ml\` folder):
- `dk2ml.h`: the C API. era's Lights and Lasers uses safe hooks, the FRAME and MAP_LOADED events, the GUI kit,
  `AddMenuButton` (the Esc button, API 2), `AddReplayData` (the per-trooper switches in replay files, API 3), options
  on the Modloader screen and the settings folder. Both buttons and replay data are optional: on an older loader the
  plugin runs without them (the manifest asks for API 1).
- `dk2ml.hpp`: the header-only C++ layer. `src/game/Game.h`/`Game.cpp` declare every game name as a `dk2ml::Fn`/
  `Field`/`Global`/`Enum` binding, and `dk2ml::ResolveAll` resolves them.

To update, copy both files from the loader's `include/` over these, rebuild, and dry-run the plugin with the loader's
`symtest.exe` (`.\tools\install.ps1` does it). The manifest's first argument is the `DK2ML_API_VERSION` the plugin
needs.
