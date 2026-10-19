# Build from source

Build on Windows x64 with Python 3 for repository checks and **Zig 0.17.0** for
the C++ components. Zig is a build dependency only, not a player requirement.
SDK/API headers are vendored with their licenses for reproducibility:

- SCS Telemetry SDK: https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry
- ReShade 6.8.0 API: https://github.com/crosire/reshade/tree/v6.8.0/include

From a fresh checkout:

```powershell
.\tools\Build.ps1 -ZigPath 'D:\Tools\zig\zig.exe'
python .\tools\test_installer.py --game-exe 'D:\SteamLibrary\steamapps\common\American Truck Simulator\bin\win_x64\amtrucks.exe' --loader 'D:\SteamLibrary\steamapps\common\American Truck Simulator\bin\win_x64\dxgi.dll'
python .\tools\audit_package.py
```

Build.ps1 compiles the clock DLL, graphics add-on and native setup; runs clock,
timezone layout and mock SDK/IPC tests; then creates the extracted distribution
under `dist/`. It generates a manifest header with exact payload hashes for the
installer. Source binaries, symbols, caches and local fixture inputs are ignored.

The installer test copies a legitimately installed supported game executable and
ReShade loader to disposable directories **under build/**. It never installs into
the provided game directory. Neither input is included in the release or repo.
The release ZIP is created only after the audit passes.

`src/theme_graphics/generated_shaders.h` contains the original replacement HLSL
source for all eight day/night shaders. `native_fingerprints.h` contains only
hash/length identifiers, not native bytecode. The SCS is an ordinary ZIP of the
original Weaysy material/UI files and licensed icon assets. Its runtime files are
unchanged from the tested TEST004 map asset package; only metadata was renamed.

Changes to game layout constants, shader identifiers, palettes or native module
versions require new compatibility testing. Do not turn mock-test results into
a claim that another game build or ETS2 works in game.
