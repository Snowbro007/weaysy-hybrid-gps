# Validation and known limits

## User observations

The author observed automatic switching on both the HUD and cabin GPS in the
original ATS prototype, including the local TEST004 lineage's corrected map
label appearance. Those observations apply to that local configuration. They do
not establish compatibility with ETS2, other game builds, every truck, ProMods,
multiplayer or third-party graphics modifications.

## This distribution

The theme and clock source are copied from the corrected TEST004 snapshot.
The native modules are rebuilt with symbols stripped. The SCS's 82 runtime
material/UI/map files are byte-identical to that snapshot; only descriptive
metadata was renamed. The native installer is adapted to install only the two
theme modules and clock INI. Incident research code is excluded.

Release checks include clock policy boundaries, missing/stale time, restarts,
local timezone layout/teleport changes and SDK IPC using independent mock data;
native installer isolation/backup/rollback/configuration tests; compilation and
DX11 WARP creation of all eight original replacement shaders; archive CRC,
required licenses, native-file allowlist, private-path/credential scan and hashes.

Test sources are included. Build/test reports are generated under `build/`.
The tests do not require a game launch or prove correct final pixels inside
every game view. The source repository does not contain game/research captures.

**A fresh driving test of this exact rebuilt distribution on another PC is still
pending.** The GitHub release is therefore marked as a prerelease/preview.
Report issues with the game version, DX11 mode, GPU, truck and observed game
time/timezone. Do not post unredacted personal paths or entire save profiles.
