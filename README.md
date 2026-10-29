# Tenchu: Return from Darkness - HoloCatt Recomp Project

**Tenchu: Return from Darkness - HoloCatt Recomp Project** is a work-in-progress static recompilation of *Tenchu:
Return from Darkness* (original Xbox) into a native Windows game. The game's own code runs natively on your PC, with
higher resolutions, widescreen, Direct3D 12 or Vulkan, texture packs, mods, cheats, a photo mode and a level editor.

> [!TIP]
> **Press F6 in game for the port menu.** Everything this port adds is there: display, shader, camera, cheats,
> audio, texture packs, mods, memory patches and custom levels. The menu is used with the mouse; Esc closes it.

> [!WARNING]
> **This is an alpha, a preview for testers.** The menus and the first mission have been played; the rest of the
> game hasn't been played through yet on this port, so expect bugs or crashes further on. It has only been tested on
> one PC (Windows 11, NVIDIA). If something breaks, please [report it](../../issues/new/choose).

> [!NOTE]
> **No game files are included.** You need your own copy of *Tenchu: Return from Darkness* for the original Xbox,
> **USA release**, as a disc image (`.iso`) made from your own disc. This project does not include, and will not
> provide, game files, disc images or links to them. Your disc image is only read, never changed.

## Getting started (Alpha 0.1.4, Windows)

1. Download `TenchuRFD-0.1.4-alpha-windows.zip` from [Releases](../../releases) and extract it to a folder you can
   write to, such as Documents (not Program Files). It needs about 6 GB free for the first start.
2. Run **`TenchuRFD.exe`** and pick your `.iso` when asked (or put the `.iso` in the `assets` folder first). The
   location is remembered.
3. The first start unpacks every file of the disc into the `extracted` folder (under a minute), then prepares the
   game's code on your PC from your own disc (a few minutes, first time only). When it says ready, press Start.
4. In game, press **F6** for the port menu.

Windows may warn that the files come from an unknown publisher (they aren't signed): choose **More info > Run
anyway**.

Needs Windows 10 or 11 (64-bit) and a graphics card with Direct3D 12 support.

## Status

| | Part | What it means |
|---|---|---|
| ✅ | **Recompiler** | The game's code is recompiled to native x64 on your PC from your own disc |
| ✅ | **Setup from your disc image** | Pick the `.iso` on the first start; files are unpacked so everything can be modded |
| ✅ | **Graphics (Direct3D 12)** | Menus, cutscenes and the first mission render correctly on NVIDIA |
| 🔧 | **Graphics (Vulkan)** | Through vkd3d-proton; choose it in F6 > Display |
| ✅ | **Resolution and widescreen** | 0.5x to 4x internal resolution; experimental 16:9 with the HUD kept at 4:3 |
| ✅ | **Sound and music** | Some original DirectSound effects are not reproduced yet |
| 🔧 | **Controllers** | Xbox, PlayStation and Switch controllers; keyboard and mouse only drive the free camera so far |
| ✅ | **Texture packs** | PS2 *Tenchu: Wrath of Heaven* packs work, matched by appearance |
| ✅ | **Mods and memory patches** | A `mods` folder for replacement files; Cheat Engine style patches without Cheat Engine |
| 🔧 | **Custom levels** | Make your own maps with RFD Switchblade (OBJ models, lighting, enemies, items, lights) |
| 🔧 | **Multiplayer alone** | Two players in a story mission with one controller (experimental) |
| 🔧 | **Tested in game** | The menus and the first mission; the rest is untested: [report what breaks](../../issues/new/choose) |
| 🗺️ | **Xbox Live** | Shut down in 2010; its menu option is disabled |

✅ done · 🔧 in progress · 🗺️ not planned

## RFD Switchblade

`tools\RFD Switchblade.exe` is a separate program for modding. It finds the game by itself.

- **Game files**: browse every unpacked file, preview and edit textures in your own image editor, replace, export or
  revert files, and turn your changes into a mod.
- **Texture packs**: match a PS2 pack to this game and review the close calls a page at a time.
- **Levels**: view every stage in 3D and build your own levels from a ground, boxes and OBJ models from Blender, with
  lighting and the mission's enemies, items and lights. Save it, then pick it in F6 > Levels.

How mods, texture packs, memory patches and levels work: [docs/modding.md](docs/modding.md).

## Hotkeys

| Hotkey | | Hotkey | |
|---|---|---|---|
| F6 | Port menu (mouse) | F12 | Screenshot (`screenshots` folder) |
| F10 | Photo mode / free camera | F3 | Render capture for graphics bugs (turn it on in F6 > Tools) |
| Ctrl+F3 | Thread dump, if the game seems stuck | | |

## Reporting bugs

[Open a bug report](../../issues/new/choose) and attach the `logs` folder from the game folder (zip it). Say where in
the game it happened and what your PC is (Windows version, graphics card). For a crash, add the newest file in
`CrashDumps`; for a freeze, press Ctrl+F3 while it is stuck and send the logs after.

## Building from source

See [docs/building.md](docs/building.md) and [SDK-VERSION.md](SDK-VERSION.md). The repository holds only this port's
sources: no game files, no code generated from the game, and no binaries.

## License

The port's own code is MIT licensed ([LICENSE](LICENSE)). It builds on
[XboxXBERecomp](https://github.com/TGP482/XboxXBERecomp) (MIT); the release includes the licenses of everything it
ships. *Tenchu: Return from Darkness* belongs to its rights holders; this project is not affiliated with them.
