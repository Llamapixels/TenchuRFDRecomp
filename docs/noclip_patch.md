# RFD noclip flight

Install the matching `TenchuRFD.exe` and `TenchuRFD.dll` runtime update, then
put `RFD - noclip.txt` in `memory_patches` beside the game. Restart once for
the runtime update. F6 > Patches lists **No clip flight - P1**. The switch
can then be changed live. Older loaders ignore the controller-only entry.

The player uses the same controls and movement speed as PhotoCam:

| Action | Controller | Keyboard/mouse |
| --- | --- | --- |
| Move | Left stick | WASD |
| Look | Right stick | Arrows, or hold right mouse and move |
| Up / down | RT / LT | E / Q |
| Fast / slow | RB / LB | Shift / Ctrl |

Release movement to hold position, including height. Jump and other gameplay
actions are withheld while flying. F6, the game's pause, and PhotoCam suspend
flight movement; PhotoCam can still move independently. Uncheck in open space
to resume normal physics. This does not add invulnerability or disable
mission, water, damage, and scripted triggers.

## Implementation

The old collision-only patch let jumping/gravity move the player through the
floor. It has been replaced with `Controller=NoClip`, with no periodic data
writes. The patch writer publishes only a request. Player simulation owns
position and collision changes on the guest thread.

Two existing named-function override points wrap the original routines:

- `sub_0029D830`: read the current guarded player, integrate PhotoCam-style
  flight, run the original player update, then pin its position caches again.
- `sub_002059C0`: runs after animation/gravity callbacks and before world
  collision. It zeros the motion vector and reasserts collision bypass here,
  every simulation tick, including when an action cleared the bits.

Retail branch `0029E253` tests actor `+31C` bit `800`: set skips world
collision `00271D50`. Routine `002059C0` tests actor `+4CC` bit `40`: set
skips character pushing. The flight session owns only those two flag bits.
Unchecking restores them and clears residual motion, preserving unrelated
status flags. It never restores bytes into a former player allocation.

The player pointer remains `01BAB110`; guards require gameplay scene 3,
the PLAY label, and player slot zero. Position caches `20`, `110`, `130`,
`1F0`, `258`, `268`, and `2A8` are kept together. The camera follows the
controlled player using the same look integration as PhotoCam.

The source XBE, generated game instructions, config, and guest allocation
logic are unchanged. The DLL is relinked so its two original function calls
reach the new host wrappers; the original function bodies remain present.
Share the matching EXE, DLL, patch, and compiler import library together.

Automated tests cover gravity/jump displacement and action resets, actual
trigger and stick movement, hover after release, input withholding, F6 and
PhotoCam suspension, disable, scene guards, player two, pointer changes,
and the exact shipped text file. Visual gameplay confirmation is pending.
