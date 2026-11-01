# Shareable patches for allocated objects

Ordinary `Address,...` patches retain their existing behavior and fixed Xbox
addresses. Move tables, game code, and globals have not been relocated.

The player is allocated by the original game's task allocator
(`002A1010 -> 002D4140 -> 002D4080 -> 00022560`). This allocator takes a free
run of blocks from the pool at `018FE6D4`. An address observed for one player
allocation is not an identity for that player across arbitrary loads.

For player-object patches, a file can declare:

```text
Pointer=01BAB110,00EC7D10,000007D8
[Example bow ammo]
Check,100EC845C,value,0021
PointerAddress,100EC845E,value,03
```

The fields are a fixed game pointer address, the object address used when
authoring the patch, and the object's size. The writer resolves the pointer
on every update and writes to `current_object + (published_address - original_base)`.
The numeric addresses in a shared file therefore do not need editing for a
new player allocation. Player 1's pointer is at `01BAB110`; player 2's is at
`01BAB114` in this USA/Asia RFD build.

Every write and check in a pointer patch must stay within its declared object.
An unavailable/null pointer, unreadable field, failed check, malformed line,
or out-of-object write blocks the entire update. When the object changes,
old saved bytes are discarded; they are never restored into freed/reused
memory or into a replacement object. Disabling restores the current object's
original bytes only if the pointer and checks still match.

`Check,<address>,value,<hex>` requires equality. `min` and `max` impose an
unsigned bound. `oneof` accepts alternatives separated by `|`, all with the
same size. A failed check waits without writing. For example, the bow patch
checks the PLAYER task label, slot count, and item ID before changing ammo.

Use `PointerAddress` for shared pointer patches. Older port builds ignore
that directive instead of applying the historical address without its
pointer metadata. Recipients need the updated patch loader once; afterward
the `.txt` file reloads and toggles live in F6 > Patches.

The bow example still has deliberate inventory limits: slot 0 must be the
grappling hook, slot 1 must be empty or the bow, and Equip refuses to hide a
larger inventory. Infinite ammo requires the bow in slot 1. This prevents
overwriting other items; it does not claim every possible inventory layout.

## Actions and sustained cheats

The bundled `RFD - ...txt` collection uses `DataAddress` directives. Older
loaders ignore these directives, so an action cannot accidentally become a
freeze on an older build. Recipients need the updated EXE once. No new game
DLL, instruction hook, allocator change or source project is required for
this patch-loader update.

`Mode=Once` displays a checkbox marked **one-shot**. Tick it while F6 is
open: it stays checked and does not write until you close F6. It then makes
one guarded attempt and automatically unchecks. If its required screen or
player does not match, it shows **Not applied**, without leaving a delayed
action waiting for later. Untick before closing F6 to cancel; tick again
and close F6 to repeat. Pending actions are not saved to settings. One-shot
actions never restore their writes; an unlock or resource grant can be
retained by the game's normal saving.

Without `Mode=Once`, the checkbox sustains the values. `Restore=No` stops
refilling a resource without putting old damage or old stock back. The
default restore policy remains unchanged for existing numeric patches.

```text
[Add 100 bonus points]
Mode=Once
GlobalCheck,008E38BC,value,0003
DataAddress,008E40AE,add,0001,FFFF,2 bytes

[Attack power x5]
GlobalCheck,008E38BC,value,0003
DataAddress,01BAA8C0,mask,00100000,00100000,4 bytes
```

Supported data operations:

| Directive | Effect |
|---|---|
| `DataAddress,address,value,value[,type]` | Set a value; same size/float rules as Address. |
| `DataAddress,address,mask,value,mask,type` | Set only the selected bits. On disable, restore only those bits. |
| `DataAddress,address,add,amount,limit,type` | Unsigned saturating addition, capped at limit; requires Mode=Once. |
| `DataAddress,address,xor,bits,type` | Toggle selected bits once; requires Mode=Once. |
| `DataAddress,address,copy,source,type` | Copy the specified number of bytes from source. |

For mask, add, xor and copy, type is explicitly `byte`, `2 bytes` or
`4 bytes`. Both addresses of a copy are relative to Pointer metadata when
present and must fit the object range. All destinations and copy sources
are validated before the update starts. Malformed data operations block the
complete patch. Legacy Address-only patches retain their existing parsing.

`GlobalAddress` supports the same operations at a fixed destination, even
inside a pointer patch. A copy source still follows Pointer metadata. This
lets the health patches update their fixed per-player HUD mirrors from the
current player's actual maximum without guessing its heap address.

`Check,address,mask,bits` requires all specified bits. `GlobalCheck` uses
the same comparisons at a fixed address, even inside a pointer patch.
Global screen checks on a static patch restrict activation; disabling that
static patch can restore its owned bits after leaving the activation screen.
Pointer-patch restoration still requires the same object and its guards.

The collection includes original one-shot/toggle counterparts, full Kuji and
health refill, sustained invisibility, and individual reward-item unlocks.
Chameleon hold requires casting the real spell first, since its disguise
model needs initialization. Stock-full switches affect stored supplies on
item selection; they do not replace carried inventory. Periodic health
refill is not the built-in God mode's immediate lethal-damage protection.

The original-code research and complete 31-entry button table are in
`builtin_cheat_scan.md`. Visual behavior of the new collection remains
unconfirmed until tested in game.
