# Modding

The first start unpacks every file of your disc into `extracted`, and each archive (`Media\*.con`) into a
`<name>.con.files` folder beside it, so every file can be changed. Archives are rebuilt when the game starts.

## Mods (F6 > Mods)

Each folder in `mods` mirrors the disc: a file at `mods\<mod>\<disc path>` replaces that disc file. Files inside
archives are replaced one by one: `mods\<mod>\Media\m.con.files\<file>` replaces only that file in `m.con`, so several
mods can change the same archive. Copy just the files you changed from `extracted` into your mod. F6 > Mods turns
mods on and off and sets their order. Editing a file directly in `extracted` works too; mods come first.

## Texture packs (F6 > Textures)

Packs made for the PS2 version (*Tenchu: Wrath of Heaven*) work. Put the pack's folder in `textures\packs` and open
F6 > Textures. Textures are matched by how they look, so the pack's file names do not matter, and more get replaced as
you play. Close calls are listed under "Needs review".

Your own edits: tick F6 > Textures > Dump textures, play past the texture and find it in `textures\dumps`. Edit it,
keep the file name (its 16-character key is what matters) and put it in `textures\replacement` or in any pack folder.

RFD Switchblade's Texture packs page matches a whole pack ahead of time and saves `rfd_matches.toml` in the pack
folder, so a pack only needs reviewing once and the matches travel with it.

## Memory patches (F6 > Patches)

Patches people share for Cheat Engine work without Cheat Engine. Put a `.txt` file in `memory_patches` with one or
more patches:

```
[Always gets grandmaster rank]
Author=me
Address,1008E40A0,value,00000063
```

Addresses can be Cheat Engine's (game memory starts at `100000000`) or the game's own. The value's digits set its size
(2 = byte, 4 = 2 bytes, 8 = 4 bytes); a value with a point is a float. F6 > Patches lists each patch with an on/off
switch. Patches that change the game's code have no effect, since the game runs recompiled.

## Custom levels (RFD Switchblade > Levels, F6 > Levels)

A custom level replaces the map of a story mission: its look, collision and lighting. The mission itself (story,
enemies, items) stays unless you change it.

1. In RFD Switchblade, open Levels, pick the stage to build on and start a new level.
2. Add a ground, boxes for walls and platforms, and OBJ models exported from Blender (Y up; scale 100 turns metres
   into game units). Each model's pictures come from its `.mtl` file.
3. Set the light colour and the sun. Per model, **Ledges** lets you hang from its edges; switch it off for buildings
   you walk into (then only the mesh collides).
4. Tick **Own enemies**, **Own items** or **Own lights** to move, add or remove the mission's enemies, move or remove
   its items, and move or remove its candles, lanterns and torches.
5. Save. The level is written to `levels\<name>` beside the game. In the game, pick it in F6 > Levels, then start the
   mission it is built on.

The player always starts at the mission's own start, so build the level around it. The pictures must fit in the
stage's free texture slots (RFD Switchblade says when they do not); combining small textures into atlases in Blender
lowers the count. To share a level, zip its folder; the other player puts it in their own `levels` folder.
