# Changelog

## 0.1.6-alpha

### Multiplayer (early testing)
- **Online co-op and versus.** The game's Xbox Live menus now work with local
  player profiles and connect straight to a friend's PC. No Microsoft service,
  matchmaking or relay is involved.
- **F6 > Online**: join codes (Get/Copy, Paste/Join), five recent hosts, LAN and
  virtual-LAN codes, and optional automatic router setup (UPnP) with a status
  report when forwarding fails. See ONLINE.txt.
- Joining again straight after a match works.
- Player 2's inputs are sent every frame instead of every fourth, when both
  players have this version.
- Both sides now keep their replay queues short: before, every late packet
  added delay that never went away. Button presses are never skipped.
- Known problems: player 2's own moves still respond late (the trip to the
  host and back remains), and player 2 should turn off the modern
  third-person camera. Both players need the same version.

### New
- **F6 > Controls**: keyboard and mouse play, with remapping.
- **F6 > Camera > Draw distance**: separate world and character sliders from
  0.25x to 4x, lower for weaker PCs.
- **F6 > Display > HUD**: show or hide the HUD and set its opacity.
- **RFD Switchblade > Levels**: browse the game's own models and textures and
  place them in a custom level; pick enemy types from any mission, and name
  items.
- F6 > Mods notices a new mod folder while it is open.
- The Vulkan renderer leaves out OBS's and Wallpaper Engine's capture layers,
  which could freeze the game while resizing the window
  (`TENCHU_OBS_CAPTURE=1` keeps them for OBS Game Capture).

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

- Custom levels, RFD Switchblade level building, lighting and mission objects.
- Multiplayer alone with two players and one controller (experimental).
- Mod Workbench renamed to RFD Switchblade.
- Scrolling sky/water textures repeat, and mods take priority over loose files.
