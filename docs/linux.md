# Running the mod on Linux / Proton

A community-tested way to run the mod under Proton. OpenXR head tracking, Alternate Frame Warp and FSR 3.1 have all been
run this way. No port is needed: the Windows build runs as-is and is installed by the Windows launcher, with one Wine
loader setting added on top.

This is a community recipe, not official support. What has actually been run, on one machine (Fedora 44, native Steam,
GE-Proton10-34, SteamVR for Linux 2.18.4, AMD Radeon 8060S iGPU):

| Mod | What was run |
| --- | --- |
| v1.03, 2026-11-18 | launcher install, then a run with the headset on: into the world, AFW + FSR 3.1 |
| v1.02, 2026-11-16 | install, re-install, `action=flat`, `action=remove`, refusing while the game runs |
| Not tested | Flatpak Steam, runtimes other than SteamVR, DLSS (needs RTX), FSR 4 (needs RX 9000) |

## The one thing Linux needs

The mod injects itself through a `xinput1_3.dll` in the game folder: the game loads that instead of XInput, and it starts
the mod. Wine loads its own **builtin** `xinput1_3` first, so the mod's file never runs and the game comes up as plain
flatscreen. In the report that started this, the game instead aborted with `Failed to initialize Oculus PC SDK`.

The fix is a per-exe DLL override that tells Wine to prefer the game folder's file:

```
HKCU\Software\Wine\AppDefaults\DyingLightGame.exe\DllOverrides
    xinput1_3 = native,builtin
```

It is stored in the prefix, in `$COMPAT/pfx/user.reg`. Everything else the mod does - the OpenXR session, the launcher's
own install steps - works unchanged.

## Requirements

- **Native Steam**, not Flatpak: this needs to reach the game folder and the prefix directly.
- **Dying Light Enhanced Edition on Steam** (appid **239140**) with a **Proton 10.x** tool set as its compatibility tool.
- **The game started once through that Proton tool**, so the prefix exists - steps 2 and 3 both write into it.
- **SteamVR for Linux running with the headset connected before the game is launched** - the mod talks to the OpenXR
  runtime that is already up.

## Install

### Paths

```bash
STEAM=$HOME/.local/share/Steam            # or ~/.steam/steam
```

The game folder and the prefix live in whichever library folder the game is installed in, which is often not the one
above. This resolves them from the appid:

```bash
APPID=239140                                     # Dying Light (Enhanced Edition) on Steam
LIB=$(grep -o '"path"[[:space:]]*"[^"]*"' "$STEAM/steamapps/libraryfolders.vdf" \
      | cut -d'"' -f4 \
      | while read -r L; do [ -f "$L/steamapps/appmanifest_$APPID.acf" ] && echo "$L"; done | head -n1)
[ -n "$LIB" ] || echo "STOP: appid $APPID is not installed in any library folder"
INST=$(sed -n 's/.*"installdir"[[:space:]]*"\([^"]*\)".*/\1/p' "$LIB/steamapps/appmanifest_$APPID.acf")
GAME="$LIB/steamapps/common/$INST"
COMPAT="$LIB/steamapps/compatdata/$APPID"       # the prefix itself is $COMPAT/pfx
PROTON="$STEAM/compatibilitytools.d/GE-Proton10-34/proton"      # any Proton 10.x tool
# PROTON="$STEAM/steamapps/common/Proton 10.0/proton"          # the Steam default instead

CACHE=$HOME/.cache/dlvr-linux                  # step 1 puts the release here
wpath() { printf 'Z:%s' "${1//\//\\}"; }       # Linux path -> the path Wine sees

echo "GAME   = $GAME"; echo "COMPAT = $COMPAT"; test -x "$PROTON" && echo "proton ok"
```

`GAME`, `COMPAT/pfx` and `PROTON` must all exist before going on. A library folder on a second drive is fine - paths with
spaces are fine, because every path below is quoted.

### 1. Download the release files

Put these nine files from [the latest release](https://github.com/bymodify-ux/Dying-Light-VR/releases/latest) into one
folder - this is the release directory the launcher expects:

```
DyingLightVR_Launcher.exe  files.zip  release.txt  notes.txt  resets.tsv
retired.txt  settings_schema.json  LICENSE.txt  THIRD_PARTY_NOTICES.txt
```

Leave `vc_redist.x64.exe` out of that folder: the launcher runs it with `/install /quiet` when it thinks the Visual
C++ runtime is missing, and that path has not been tried under Wine.

### 2. Run the launcher, window-less, through Proton

Close the game first (the launcher refuses to touch a running game). `--auto` is the launcher's window-less mode, the
same one its own end-to-end tests drive, so no X display is needed:

```bash
export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM"
export STEAM_COMPAT_DATA_PATH="$COMPAT"

"$PROTON" run "$CACHE/DyingLightVR_Launcher.exe" --auto \
  "game=$(wpath "$GAME");release=$(wpath "$CACHE");action=install"
echo "exit code $?"
grep -a -- '--auto result:' "$CACHE/launcher.log" | tail -1     # --auto result: OK - Install: done.
```

`game=` and `release=` must be Windows-style paths: inside Wine `Z:` is the **Unix root**, so `$GAME` becomes
`Z:` + the same path with `\` instead of `/` - that is what `wpath` above does. A path containing `;` will be cut short,
because `;` separates the fields of the `--auto` string.

Exit codes: `0` success, `1` the action failed, `2` refused (no `game=`, or an unknown action). `action=` also takes
`update`, `repair`, `flat`, `vr`, `remove`, `logs` and `rollback`; leave `selfupdate` alone here, it swaps in a new
launcher and expects to open a window. `--auto` is the launcher's test interface, so it can change - if a new version
stops accepting it, re-check this page.

### 3. The Wine override

```bash
"$PROTON" run reg.exe add \
  'HKCU\Software\Wine\AppDefaults\DyingLightGame.exe\DllOverrides' \
  /v xinput1_3 /t REG_SZ /d 'native,builtin' /f
```

### 4. Check

`xinput1_3.dll` and `LibOVRRT64_1.dll` should now be in the game folder. Start SteamVR, put the headset on, and launch
Dying Light from Steam.

## Flatscreen, uninstall, re-install

Re-run step 2 with `action=flat` or `action=remove`; after a game patch or a file verification, `action=install` (or
`repair`) again and then step 3. All of these are safe to re-run.

Keep the override: once the mod's `xinput1_3.dll` is gone, Wine falls back to its own XInput and the game plays as
normal. It lives in the prefix, so if you delete the prefix or switch compatibility tools, do step 3 again.

## Logs

In the game folder:

| File | What it holds |
| --- | --- |
| `dlvr_bootstrap.log` | the mod loading, and any patch site it refused |
| `dlvr_ovr2xr.log` | the OpenXR session, the runtime and headset it found, AFW |
| `dlvr_engine.log` | in-game behaviour |

`launcher.log` is written next to `DyingLightVR_Launcher.exe`, and carries the `--auto result:` line.

- **Game comes up in flatscreen, and `dlvr_bootstrap.log` has nothing new** - the mod is not loading: step 3 is missing,
  or the prefix was wiped or changed. Re-run step 3.
- **`dlvr_bootstrap.log` is new, but `dlvr_ovr2xr.log` cannot reach a runtime** - SteamVR was not running, or the headset
  is not connected to it. Start SteamVR first.
- **SteamVR itself will not start on some hosts** - a Qt/OpenSSL issue in SteamVR for Linux, unrelated to this mod. Fix
  that first: with no runtime, nothing here can work.
- If the mod refuses a patch site, the log names the site. Include all three logs when reporting a Linux issue.

## Out of scope

Flatpak Steam, runtimes other than SteamVR, and non-Steam copies of the game have not been tried. If you get another
combination working, or hit something that this page gets wrong on your machine, say so on
[the Linux thread](https://github.com/bymodify-ux/Dying-Light-VR/issues/9).
