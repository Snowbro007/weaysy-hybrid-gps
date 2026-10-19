# Installation, configuration and recovery

## What is installed

`WeaysySetup.exe` copies exactly these files relative to the ATS `bin/win_x64` directory:

| File | Purpose |
| --- | --- |
| `WeaysyRenderProbe.addon64` | ReShade add-on for the GPS colours and labels |
| `plugins/WeaysyTimeReader.dll` | Telemetry plugin publishing the game clock locally |
| `plugins/WeaysyTimeReader.ini` | Day/night options; existing options are preserved |

The legacy technical filenames are retained for compatibility with the author's tested prototype. No incident detector is installed. If old incident research plugins are present, installation stops and asks you to disable those first.

The `.scs` is copied manually to the active user `mod` folder. Do not copy the companion DLL/add-on into `mod`, and do not copy the SCS into the game installation.

## ReShade dependency

Install the **official 6.8.0 full add-on** build from https://reshade.me/ for ATS `bin/win_x64/amtrucks.exe`. Select DirectX 10/11/12; run ATS in **DX11**. No post-processing shader pack/preset is needed for this mod. The game folder should then contain the expected `dxgi.dll` loader. The installer accepts the tested full-add-on loader SHA-256:

`0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7`

The native game adapter accepts ATS `amtrucks.exe` SHA-256:

`c60e8ee02cc97b3cc6ef8c6298bb5c3c23c4ffd330b8185b646fb1b608a3c7b4`

Unknown versions are unsupported, even if their displayed version starts with 1.61. No downloads or loader replacements are performed by our installer.

## Choose the game folder

The GUI detects Steam library paths, including other drives. If needed, from a terminal in the extracted download:

```powershell
.\WeaysySetup.exe --check 'D:\SteamLibrary\steamapps\common\American Truck Simulator'
.\WeaysySetup.exe --install 'D:\SteamLibrary\steamapps\common\American Truck Simulator'
.\WeaysySetup.exe --disable 'D:\SteamLibrary\steamapps\common\American Truck Simulator'
```

`--check` is read-only. `--install` and `--disable` require ATS to be closed. A successful install is not a promise of compatibility with unrelated UI/native modifications.

## Configuration

Close ATS and edit `bin/win_x64/plugins/WeaysyTimeReader.ini`:

```ini
[Clock]
DayStart=06:00
NightStart=20:00
UseLocalTimeZone=1

[Test]
SwitchEveryGameMinutes=0
```

The boundaries use local game time on the supported build. They are fixed, configurable times; no latitude/seasonal sunrise model is included. For a short experiment, set `SwitchEveryGameMinutes=2`: two game minutes light, two game minutes dark. Reset to `0` afterwards. It is not a real-time timer. Restart ATS after editing.

The last valid theme is retained during missing clock samples/loading; the plugin does not invent elapsed time. If labels or colours stop matching after a game update, disable the hybrid companion and use a static theme until a compatible release exists.

## Disable or remove

1. Close ATS and run `DISABLE.cmd`, or the explicit `--disable` command above.
2. Deactivate the hybrid mod in Mod Manager. You may remove `weaysy_ats_hybrid_gps.scs` from the active `mod` folder.
3. The companion files are now renamed with `.weaysy-disabled`. Known previous files have backups under `bin/win_x64/Weaysy-backups/hybrid-*`. They are not automatically deleted.
4. Run the installer again to re-enable on the supported game version. Existing INI changes survive disable/re-enable.

The disable action remains available if ATS was updated. ReShade, save files, profiles and other plugins are untouched. If automatic removal refuses unfamiliar files, close ATS and manually move only `WeaysyRenderProbe.addon64`, `plugins/WeaysyTimeReader.dll` and `plugins/WeaysyTimeReader.ini` out of the game folder. Do not restore old research DLLs.

## Troubleshooting

- **Red/green GPS map:** the tagged SCS is active but the matching add-on is not processing it. Disable the hybrid SCS while checking the full-add-on loader, DX11 and native companion installation. Do not combine it with static colour mods.
- **No automatic switching:** check `plugins/WeaysyTimeReader.log` and `WeaysyRenderProbe.log` in the game folder for valid clock samples and matched shaders. Those are local diagnostic logs, not files uploaded by this project.
- **Wrong clock:** ensure `UseLocalTimeZone=1`; note the HUD time/timezone and any teleport. Unsupported game builds should be handled by a new release, not by bypassing the installer.
- **Game stops starting:** close ATS, disable the companion and deactivate the hybrid SCS. Report the game build, GPU, DX11 mode and relevant error text. Redact personal paths before posting logs publicly.
- **Another map/mod:** ProMods and other map/background combinations are not validated by this preview. Use a clean standard-map profile for diagnosis.

Issues: https://github.com/Snowbro007/weaysy-hybrid-gps/issues
