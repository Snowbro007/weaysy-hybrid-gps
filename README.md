# Weaysy Hybrid GPS — Day & Night

Automatic light/dark GPS for **American Truck Simulator**, switching the HUD GPS and supported cabin GPS displays together using the game clock. This is an **experimental Windows preview**, shared separately from the static Steam Workshop Light/Dark mods.

**[Download the Windows preview](https://github.com/Snowbro007/weaysy-hybrid-gps/releases)** · [Install / uninstall](docs/INSTALL.md) · [Nederlands](docs/INSTALL_NL.md) · [Report a problem](https://github.com/Snowbro007/weaysy-hybrid-gps/issues)

## What it does

- Light map with dark labels during the day; dark map with light labels at night.
- The HUD and supported cabin GPS views use the same clock snapshot.
- Blue navigation route and the Weaysy icon pack.
- Local game timezone is checked again each frame, including after teleports.
- Defaults: **day starts at 06:00; night starts at 20:00**, local game time. These are configurable boundaries, not astronomical sunrise/sunset calculations.

## Requirements and current limits

| Requirement | This preview |
| --- | --- |
| Game | **ATS 1.61.3.18, Windows x64**, exact executable fingerprint |
| Graphics | **DirectX 11** |
| Companion dependency | **ReShade 6.8.0 with full add-on support**, installed separately from [reshade.me](https://reshade.me/) |
| Map setup | Standard ATS map; additional map/background combinations are not validated |
| ETS2, Linux, macOS, DX12/OpenGL | Not supported by this download |
| Multiplayer / TruckersMP / Convoy | Not validated |
| Live crash, breakdown or roadworks reports | Not included; separate research |

The installer refuses unsupported game builds and unknown loaders instead of guessing. A game update may therefore require a new companion release. **Do not remove the version checks.** A Workshop subscription cannot install these native components.

## Install

1. Download the ZIP from **Releases**, not GitHub's “Source code” ZIP. Extract it completely.
2. Close ATS. Install ReShade **6.8.0 with full add-on support** for `amtrucks.exe`, selecting DirectX 10/11/12. No effect preset is required. The regular ReShade build does not enable this API.
3. Run `WeaysySetup.exe`. It detects Steam libraries, checks the game/loader and package hashes, and backs up known companion files. It installs three files, preserving an existing clock configuration. It does not edit profiles, saves, game settings or the ReShade loader.
4. Copy `weaysy_ats_hybrid_gps.scs` into your **active ATS user folder's `mod` directory**. Normally this is `Documents/American Truck Simulator/mod`; a custom `-homedir` uses its own folder.
5. In Mod Manager, activate **Weaysy Hybrid GPS — Day and Night (ATS Preview)** above other GPS/UI mods. Disable static Weaysy Light/Dark, old GPS tests and other GPS colour overrides. Use one GPS theme.
6. Launch ATS normally with DirectX 11. The game's normal telemetry SDK acknowledgement may appear; accept it if you want the plugin to run. **No SDK download, developer mode, console setting or Python installation is needed.**

The native companion loads automatically when installed. If you return to a static Workshop theme, first run `DISABLE.cmd` and deactivate the hybrid SCS. [Full instructions](docs/INSTALL.md).

## Test status

The original author has observed both HUD and cabin GPS switching in ATS, and subsequently tested the corrected label behaviour in the local TEST004 lineage. The distributed native sources come from that lineage; the distributable binaries are rebuilt with debug symbols removed. This release has automated clock, timezone, IPC, installer and shader checks, **not a fresh driving test on every truck or another PC**. See [validation](docs/VALIDATION.md).

This is an opt-in preview for people who want the automatic mode and accept the extra installation. For a simple Workshop-only installation, use the separate static Lightmode or Darkmode item.

## Configuration and removal

Clock options are in `bin/win_x64/plugins/WeaysyTimeReader.ini`. Keep `UseLocalTimeZone=1` and `SwitchEveryGameMinutes=0` for normal driving. Close the game before editing. [Configuration and troubleshooting](docs/INSTALL.md#configuration).

Run `DISABLE.cmd` with ATS closed to rename only the known Weaysy companion files to `.weaysy-disabled`; deactivate/remove the hybrid SCS in Mod Manager. This also works after a game update. ReShade itself and unrelated mods remain installed. If you installed ReShade only for this mod, its own setup can remove it separately.

## Source and credits

Original Weaysy code and artwork: MIT. Material Icons glyphs: Apache-2.0, composed/recoloured by Weaysy. Bundled SDK/API headers retain their separate notices. See [NOTICE](NOTICE.md), `licenses/` and `third_party/`.

No game executable, game shader bytecode, game models/textures, ProMods files or ReShade runtime is redistributed. The small shader hashes and read-only clock layout constants identify the supported game interfaces; replacement shader source is included. No affiliation with Google, SCS Software, ProMods or ReShade is claimed.

Developers: [build instructions](docs/BUILD.md). This repository and release do not upload telemetry: clock samples are shared locally between the two plugins, with diagnostic files written locally.
