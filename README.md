![Dying Light VR](assets/branding/banner.png)
# Dying Light VR

A native 6DoF OpenXR VR mod for Dying Light (Enhanced Edition): true stereo, motion controls, no helper program.

## Download

**[Download the launcher (DyingLightVR_Launcher.exe)](https://github.com/bymodify-ux/Dying-Light-VR/releases/latest/download/DyingLightVR_Launcher.exe)**
- always the newest version. Run it, press **Check for updates**, then **Install**.

The launcher installs, updates and removes the mod, switches between VR and flatscreen, and lets you change the settings before
you launch. Every release is also listed under [Releases](https://github.com/bymodify-ux/Dying-Light-VR/releases).

## Instructions

**THIS MOD IS CURRENTLY ONLY SUPPORTED FOR STEAM AND EPIC GAMES - GOG ENGINE ADDRESSES HAVE NOT BEEN TRANSLATED AT THIS POINT IN TIME - PIRATED VERSIONS ARE NOT SUPPORTED**
1. Download the latest Launcher from the releases
2. Run the Launcher & hit install
3. Put on your headset, then hit Launch in VR.

## Linux / Proton

Linux is not officially supported. Community testers have the mod running under Proton - same launcher, same files - with
one extra Wine setting (a `xinput1_3` DLL override); the recipe they verified, with what has and has not been tested, is
in [docs/linux.md](docs/linux.md).

## Credits

I'd like to thank the following people for their assistance & contributions to the mod, you're all loved:
- **Timguin**
- **Optimumbox**
- **Mamfro**
- **Pizza Parker**

- **"VR Hud and UI correct"** by **IMIG** (Nexus Mods, Dying Light mod 694) - the VR HUD and menu layout.
- **CheekyFoveatedDLSS** by **ClarkCheekyKent** - https://github.com/ClarkCheekyKent/CheekyFoveatedDLSS (GPL-3.0). The foveated DLSS / FSR shaders and controls are ported from it.
- **Dear ImGui** by Omar Cornut (MIT).
- **puff** by Mark Adler (zlib licence).
- **NVIDIA DLSS** and **NVIDIA Image Scaling** (MIT). This software contains source code provided by NVIDIA Corporation.
- **AMD FidelityFX SDK** - FSR 3.1 / FSR 4 (MIT; its licence ships with the mod).
- **OpenXR SDK** by the Khronos Group (Apache 2.0).
- **All of my beta testers** (Sorry, there's so many of you <3)

## Support me

If you enjoy the mod and want to support further updates, here's my Ko-fi


<a href="https://ko-fi.com/sadbxi_/tip">
  <img src="assets/branding/kofi-support.png" alt="Support me on Ko-fi" width="320">
</a>

## Features

- Native VR for Dying Light: true stereo, full 6DoF head tracking, motion controllers. No helper program.
- Your real arms drive the character's arms, fitted to you by a short arm calibration (standing or seated). Finger tracking on empty hands.
- Physical melee: swing your weapon to hit. Throw melee weapons and throwables with your hand.
- Manual reload for every gun - magazines, shells, revolvers, the bow and crossbow, even the chainsaw.
- Two-handed grip for rifles, shotguns, the chainsaw and big melee weapons.
- Body holsters: weapons, items, ammo, healing and the flashlight at your shoulders, hips, chest and head. Weapon, throwable, arrow and bolt wheels.
- Hands on the world: shove and grab zombies, loot and drag bodies by hand, camouflage by hand, start and steer the buggy with your hands.
- Roomscale walking and physical crouching.
- HUD and menus on panels in 3D space, or on your wrists (optional Wrist HUD).
- Alternate Frame Warp for high frame rates; DLSS, FSR 3.1, FSR 4 and TAA.
- A launcher: install, update, switch between VR and flatscreen, change settings, collect logs.

## Game options

All in the launcher's VR Comfort tab, or in game in the mod menu (hold both thumbstick clicks for 2 seconds).

- **Turning:** snap (30 degrees, default) or smooth. The snap angle can be changed.
- **Body follows head:** your character turns with your head and body, and walks where you look (on by default).
- **Roomscale:** your real steps move your character (on).
- **Physical crouch:** crouch for real to crouch in game (on). Optional: slide when crouching while sprinting (off).
- **Flatscreen Cutscene Animations:** cutscenes keep the game's camera tilt; turn it off if that makes you uncomfortable (on).
- **Hand steadiness:** smooths small hand shake when aiming (on), with strength and response sliders, and a separate melee steadiness.
- **Hands on the buggy:** start the buggy with the key and steer it with your hands (on).
- **Finger tracking (empty hands):** your fingers follow yours on the controller (on).
- **Desktop mirror:** shows the game on your monitor (on).
- **Arm calibration:** standing or seated.
- **HUD:** the HUD and menus sit on panels in front of you; distance, height, size and whether the HUD follows you are all adjustable. Hide the HUD entirely if you like.
- **Wrist HUD (optional, off by default):** health, minimap and weapon info on your wrists instead of in front of you.
- **Controls:** every button can be re-bound in the mod menu's Controls tab, and every holster zone can be moved and resized by hand.

## Graphics options

In the launcher's Config tab, or in game in the mod menu's Anti-Aliasing tab (restart the game after changing, except sharpening).

- **Upscaler / anti-aliasing:**
  - **DLSS** (NVIDIA RTX): DLAA, Quality, Balanced, Performance, Ultra Performance.
  - **FSR 4** (AMD RX 9070 / 9060): Native AA, Quality, Balanced, Performance.
  - **FSR 3.1** (any modern GPU): Native AA, Quality, Balanced, Performance.
  - **TAA** (any GPU): native resolution, no upscaling - lighter, softer image.
  - The launcher recommends a mode for your graphics card on first install.
- **Alternate Frame Warp (AFW):** renders one eye per frame and rebuilds the other, for much higher frame rates. On by default; turn it off in VR Comfort to render both eyes every frame.
- **Sharpening:** optional NVIDIA Image Scaling sharpening (off by default).
- **Texture sharpness:** anisotropic filtering, 2x to 16x (16x by default).
- **Lens crop** and **Foveated DLSS / FSR** (both off by default): performance options - skip the parts of the image your headset never shows, and render the edges of your view at lower quality.
- **Recommended:** lock your headset's refresh rate to one your PC can always hold; head turns judder if the game drops below it.
- The mod switches the game's Motion Blur, V-Sync, frame cap and NVIDIA effects off in VR automatically.

## Controls

Quest / Touch layout, default buttons. Valve Index: X = left A, Y = left B, Menu = press down on the left trackpad.

### Left controller

| Input | Action |
| --- | --- |
| Stick | Move |
| Stick click | Sprint (toggle) |
| Trigger | Use item (grappling hook, UV light) |
| Grip | Tackle while sprinting / holsters / two-handed grip |
| X | Interact / pick up. Hold: Survivor Sense |
| Y | Hold: repair weapon |
| Menu | Inventory / map. Hold: pause |

### Right controller

| Input | Action |
| --- | --- |
| Stick left / right | Turn |
| Stick click | Kick |
| Trigger | Shoot / use / grab throwable |
| Grip | Holsters |
| A | Jump / climb |
| B | Crouch / slide. Hold: re-fit your hands |
| Meta / Oculus button (hold) | Re-centre |

**Both stick clicks, hold 2 seconds:** open the mod menu.

### Holsters and gestures

| Where | Action |
| --- | --- |
| Right shoulder + right grip | Switch weapon. Hold: weapon wheel |
| Right hip + right grip | Swap item. Hold: throwables wheel |
| Right hip + right trigger | Use Throwable. Hold & Release to throw |
| Left hip + left grip | Ammo for manual reload |
| Left chest + left grip | Heal |
| Left side of your head + left grip | Flashlight |
| Left shoulder + left grip (crossbow) | Take a bolt. Hold: bolt wheel |
| Right shoulder + right trigger (bow) | Take an arrow. Hold: arrow wheel |

- **Melee:** swing the weapon - the trigger does not attack.
- **Reload:** every gun is reloaded by hand with the left hand.
- **Zombies:** grab them with the grips, and shove them with an open hand. Shove hard with both hands to break free from a grab.

## Building from source

Requirements: Windows, Visual Studio 2022 Build Tools (C++), CMake, and vcpkg (for OpenXR and Dear ImGui, see `vcpkg.json`).

1. Put the Oculus PC SDK headers in place: see `third_party/OVR/README.md`.
2. Download the NVIDIA DLSS SDK (it holds `include/nvsdk_ngx.h` and `lib/Windows_x86_64/x64/nvsdk_ngx_d.lib`) and the AMD
   FidelityFX SDK 2.3.0 (`Kits/FidelityFX`). By default the build looks for them in `../DLSS_SDK` and
   `../FSR_SDK/v2.3.0/Kits/FidelityFX` next to this folder; or pass their paths as below.
3. Configure and build:

   ```
   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake ^
         -DDLVR_NGX_SDK=<DLSS SDK folder> -DDLVR_FFX_SDK=<FidelityFX SDK>/Kits/FidelityFX
   cmake --build build --config Release
   cmake --build build --config Release --target DyingLightVR_Launcher
   ```

The two mod DLLs (`LibOVRRT64_1.dll`, `xinput1_3.dll`) and the launcher land in `build/Release/`.

## Legal

Dying Light VR is an unofficial, free fan modification. It is not made, endorsed or supported by Techland. Dying Light is a
trademark of Techland. You need a legally owned copy of Dying Light (Steam, Epic or GOG). The mod's game data files are provided
under Techland's terms for modding the game.

## Licence

GNU General Public License v3.0 (GPL-3.0). See LICENSE.
