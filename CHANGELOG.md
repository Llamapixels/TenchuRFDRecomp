# Changelog


## 0.1.5-alpha

### New
- **F6 > Physics**: optional enhanced cloth simulation using existing models,
  with smaller simulation steps, repeated stretch constraints and bend
  resistance based on the original loaded cloth shape.
- Animated body collision capsules and additional collision checks along cloth
  edges. Capsule size and edge correction strength are adjustable.
- Optional self-collision between nonlocal points and edges within each panel,
  with adjustable strength and thickness. Self-collision starts off.
- Global response speed, motion retention, gravity, stiffness and collision
  controls, plus named physics presets with startup persistence.
- Pointer-based memory patches that follow the current player allocation,
  with guards and examples for player effects and items.
- Original cheat action examples, including one-shot actions that run when F6
  closes and automatically uncheck, and sustained checkbox variants.
- No clip flight through the supplied patch example, using PhotoCam-style
  controls and holding position when movement is released.

### Packaging and testing
- Keeps the existing Windows release layout, bundled first-start compiler,
  Vulkan runtime, Visual C++ runtime, RFD Switchblade and patch examples.
- No game files, generated game DLL, saves, personal settings or installed
  mods/texture packs are included. Testers supply their own supported disc.
- Physics is opt-in. Body capsules are approximate; self-collision is limited
  to individual panels. Visual quality and model coverage need tester feedback.
- Includes PHYSICS.txt, updated setup instructions and checksums. Matching
  debug symbols are retained separately for crash analysis.

## 0.1.4-alpha

### New
- **Custom levels** (F6 > Levels): a level replaces the map of a story mission, its look, collision and lighting,
  while the mission itself stays. Pick one in F6 > Levels, then start the mission it is built on.
- **RFD Switchblade > Levels**: view every stage in 3D and build levels from a ground, boxes and OBJ models exported
  from Blender, with the pictures from their `.mtl` files packed in automatically on save.
  - Lighting: a light colour and a sun, baked into the level.
  - Per model "Ledges": hang from its edges, or switch it off for buildings you walk into.
  - The mission's enemies, items and candles, lanterns and torches: move, add or remove them.
  - No size limit on a level's map or collision, and the ground can be any size (or none).
- **Multiplayer alone** (F6 > Cheats, experimental): two players in a story mission with one controller.

### Changed
- The Mod Workbench is now **RFD Switchblade** (`tools\RFD Switchblade.exe`). Its settings carry over.
- Scrolling sky and water textures keep repeating instead of stretching into streaks.
- Files in `mods` come before edits made directly in `extracted`.

## 0.1.3-alpha

- Fixed whole-archive mods being undone by the unpacked original files.

## 0.1.2-alpha

- **F6 > Patches**: memory patches in the format people share for Cheat Engine, from `memory_patches\*.txt`, each
  with an on/off switch remembered between starts.

## 0.1.1-alpha

- Texture replacements: edited dumps count as replaced, a dump put in a pack folder replaces exactly its own
  texture, and dumps hold only the full-size image.

## 0.1.0-alpha

- First tester release: pick your `.iso` on the first start; the game's code is prepared on your PC from your own
  disc.
- Every disc file unpacked into `extracted`, and archives into folders, so everything can be modded; per-file mods
  in `mods`.
- The Mod Workbench: game files browser and texture pack matching.
- PS2 *Tenchu: Wrath of Heaven* texture packs, matched in game (F6 > Textures).
- Direct3D 12 and Vulkan, internal resolution, widescreen, shader presets, graphics profiles, camera options, photo
  mode, cheats and volume.
- Fixed far walls flickering black, cutscene camera freezes and the Xbox Live menu option trapping the player.
