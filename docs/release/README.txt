TENCHU: RETURN FROM DARKNESS - NATIVE PC PORT (ALPHA {VERSION})
===========================================================

Thanks for testing. This is an early alpha of the original Xbox game,
recompiled to run natively on Windows. Expect bugs and crashes, and please
report them (see "Reporting a problem" below).

What you need
-------------
  - Windows 10 or 11, 64-bit, and a GPU with Direct3D 12 support.
  - A controller (Xbox, PlayStation or Switch). Keyboard and mouse only
    drive the free camera so far.
  - Your own copy of the game as an Xbox disc image (.iso):
    Tenchu: Return from Darkness (USA). Other releases will not work.
  - About 6 GB of free disk space for the first start (the game files are
    unpacked beside the game so everything can be modded).

First start
-----------
  1. Extract this folder somewhere you can write to, such as Documents.
     (Not Program Files.)
  2. Run TenchuRFD.exe and pick your .iso when asked. The location is
     remembered; if you move the .iso later, the game asks again.
     (Or put the .iso in the "assets" folder before starting.)
  3. Every file of the disc is unpacked into the "extracted" folder, and
     each archive (Media\*.con) into a <name>.con.files folder beside it.
     This takes under a minute. Later starts run from these loose files;
     delete "extracted" to unpack again.
  4. The game's code is then prepared on your PC from your own disc. This
     takes a few minutes the first time only. When it says ready, press
     Start (A). You can tick "delete the compiler" there to save space.

Hotkeys
-------
  F6        Port menu (mouse): display, camera, cheats, audio, shader,
            texture packs, mods and more. Esc also closes it.
  F10       Free camera / photo mode
  F12       Screenshot (screenshots folder)
  F3        Render capture for graphics bugs (switch it on first in
            F6 > Tools)
  Ctrl+F3   Thread dump, if the game seems stuck

What to test
------------
  Anything you like. Most useful right now:
  - Levels beyond mission 1, and both characters.
  - Saving and loading your progress.
  - Cutscenes (with and without a custom FOV in F6 > Camera).
  - Multiplayer modes on one PC.
  - Both renderers (F6 > Display: Direct3D 12 and Vulkan).
  - Enhanced cloth (F6 > Physics): compare movement, collisions, bending
    and saved presets. See PHYSICS.txt for the controls and limitations.

Updating from 0.1.x
------------------
  Extract this release into a new folder for a clean tester installation,
  or copy it over your existing game folder with the game closed. Saves
  are in "hdd"; port settings are in tenchu_startup.toml. This ZIP does
  not include either, so copying it over does not replace them.
  Keep the bundled compiler files together: an update may need to prepare
  the game code again. First-start compilation is expected on a fresh
  installation; no TenchuRFD.dll or game files are shipped.

Reporting a problem
-------------------
  Please send:
  - What you were doing, and which level or menu.
  - The "logs" folder from the game folder (zip it). Close the game first
    if you can.
  - For a crash: the newest file in "CrashDumps" if there is one.
  - For a freeze: press Ctrl+F3 while it is stuck, wait a few seconds,
    then send the logs.
  - For a graphics bug: switch on F6 > Tools > F3 render capture, press F3
    while it is on screen, and send the newest folder in "render_captures".
  Logs mention your PC's hardware and the folder you put the game in.

Texture packs
-------------
  Packs made for the PS2 version (Tenchu: Wrath of Heaven) work. Put the
  pack's folder in textures\packs and open F6 > Textures. Textures are
  matched by how they look, so the pack's file names do not matter, and
  more get replaced as you play. Close calls are listed under "Needs
  review" in F6 > Textures.

  Your own edits: tick F6 > Textures > Dump textures, play past the
  texture, and find it in textures\dumps (PNG by default, the easiest to
  edit; DDS can be picked under Dump format). Edit it, keep the file name (the
  16-character key in it is what matters), and put it in
  textures\replacement or in any pack folder. It replaces exactly that
  texture and counts under "replaced". Dumps hold only the full size
  image; the smaller versions are made from your edit.

Mods
----
  Each folder in "mods" mirrors the disc: a file at mods\<mod>\<disc path>
  replaces that disc file. Files inside archives are replaced one by one:
  mods\<mod>\Media\m.con.files\<file> replaces only that file in m.con,
  so several mods can change the same archive. Copy just the files you
  changed from "extracted" into your mod. Editing a file directly in
  "extracted" works too. Archives are rebuilt when the game starts.
  F6 > Mods turns mods on and off, sets their order and opens the
  extracted folder.

Memory patches (F6 > Patches)
-----------------------------
  Patches people share for Cheat Engine work without Cheat Engine. Put a
  .txt file in the memory_patches folder with one or more patches:

    [Always gets grandmaster rank]
    Author=me
    Address,1008E40A0,value,00000063

  memory_patches\examples has a file with one patch and a file with
  several, explaining each line; copy one up into memory_patches to use it.
  Other lines are ignored, so you can paste a whole post. F6 > Patches lists
  each patch by name and author with an on/off switch, remembered between
  starts. While on, the value is written many times a second; turning it off
  puts the original back. Patches that change values and tables work.
  Patches that change the game's code have no effect in this port (the game
  runs recompiled to PC code) and are marked so in the list.

  The RFD cheat examples include original one-shot actions and sustained
  checkboxes. One-shots stay checked until you close F6, then run once and
  automatically uncheck. Tick again and close F6 to repeat. Hover a patch
  name for the required screen and instructions. Unlocks and grants
  can be retained by normal game saving. Full-stock patches affect stored
  supplies on item selection. See pointer_patches.md for the text format
  and builtin_cheat_scan.md for the original button-code research.

  No clip flight: copy "RFD - noclip.txt" from memory_patches\examples
  into memory_patches, then enable "No clip flight - P1" in F6 > Patches.
  Left stick moves, right stick looks, RT/LT move up/down, RB/LB change
  speed. Release the controls to hold position. Uncheck in open space.

Cloth physics (F6 > Physics)
---------------------------
  The enhanced solver is optional and off on a clean installation. Enable
  it to use animated body capsules, edge collision and bend resistance.
  Self-collision is a separate experimental switch, initially off.
  Response speed, drag, gravity, stiffness and collision controls apply
  globally. Save named presets and load them from this tab; they are
  stored in physics_presets.toml beside the game. The current settings
  are remembered between starts. See PHYSICS.txt for details.
  These options improve models that already contain cloth simulation
  data; they do not automatically add physics to arbitrary hair meshes.

RFD Switchblade (tools\RFD Switchblade.exe)
-------------------------------------------
  A separate program for modding. It finds the game by itself.
  - Game files: browse every unpacked file, preview textures (zoom,
    alpha), edit a texture in your own image editor (each save there is
    converted back for the game), replace, export or revert any file.
    Turn your changes into a mod, pack an archive into a game-ready
    .con, or turn someone's whole modded .con into a mod of just the
    files they changed.
  - Texture packs: matches a PS2 Wrath of Heaven pack to this game.
    Sure matches are taken; Review shows the close calls a page at a
    time (click the wrong ones, press Enter); Browse shows one texture
    beside every pack image, most alike first, and can import any
    image. Save matches writes rfd_matches.toml into the pack folder:
    the game reads it, and it travels with the pack, so a pack only
    needs reviewing once. Build mod makes an Xbox-size mod instead.
  - Levels: view every stage in 3D and make your own levels (see
    "Custom levels" below).
  Help and keys are in the program's sidebar.

Custom levels (RFD Switchblade > Levels, F6 > Levels)
-----------------------------------------------------
  A custom level replaces the map of a story mission: its look, collision
  and lighting. The mission itself (story, enemies, items) stays unless you
  change it.
  - In RFD Switchblade, pick the stage to build on and start a new level.
    Add a ground, boxes for walls and platforms, and OBJ models exported
    from Blender (Y up), each with the pictures from its .mtl file.
  - Set the light colour and the sun. Per model, "Ledges" lets you hang
    from its edges; switch it off for buildings you walk into.
  - Move, add or remove the mission's enemies, move or remove its items,
    and move or remove its candles, lanterns and torches.
  - The player always starts at the mission's own start, so build the
    level around it.
  - Save writes the level to levels\<name> beside the game. In the game,
    pick it in F6 > Levels, then start the mission it is built on.
  - To share a level, zip its folder. The other player puts the folder in
    their own levels folder.
  The pictures must fit in the stage's free texture slots (RFD Switchblade
  says when they do not).

Known issues
------------
  - Only the first mission has been played through so far; later levels
    may crash.
  - The custom right-stick camera option does not work yet (off by
    default).
  - Xbox Live is gone and its menu option is disabled.

Starting over
-------------
  Delete tenchu_startup.toml to reset the port's settings. Delete
  game_location.txt to choose the .iso again. Your game saves are in the
  "hdd" folder.
