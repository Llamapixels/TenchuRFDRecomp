# Original Xbox cheat-code scan

USA/Asia retail XBE SHA-256: `0d8203f74d9476dd93637bfbf3d75855b29daf576b752e77600d4ff8460a9d34`.

Table: `004CB8E8` (31 pointers). Matcher: `00251FD0`. Dispatcher: `002515E0`.
Each step has four DWORDs: context/terminal bit, pressed button, held buttons, controller slot.
The dispatcher masks the result with `F1B0FFF3`. A prefix requiring `FFFFFFFF` held buttons cannot be entered on the real pad.
This is static code evidence. The new patch collection has automated coverage; its visual effects still need in-game checks.

| Entry | Effect | Reachability | Sequence from game data |
|---|---|---|---|
| 0 | Restore P1 health | Enabled; paused gameplay | release holds: Up; release holds: Down; release holds: Up; release holds: Down; release holds: X; release holds: X; release holds: X |
| 1 | Restore P2 health | Enabled; paused multiplayer | release holds: Up; release holds: Down; release holds: Up; release holds: Down; release holds: X; release holds: X; release holds: X |
| 2 | Inactive prefix: Kuji variant | Inactive / impossible prefix | hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; release holds: Left; release holds: Left; release holds: Left; release holds: Right; release holds: X |
| 3 | Inactive prefix: Kuji variant | Inactive / impossible prefix | hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; release holds: Left; release holds: Left; release holds: Left; release holds: Right; release holds: X |
| 4 | Add one kanji to P1 | Enabled; paused single-player | release holds: Left; release holds: Left; release holds: Left; release holds: Right; release holds: X |
| 5 | Fill P1 Kuji meter | Enabled; paused single-player | hold LT+RT: Left; hold LT+RT: Left; hold LT+RT: Left; hold LT+RT: Right; hold LT+RT: X |
| 6 | P1 ability editor | Enabled; paused gameplay | hold Black+RT: Up; hold Black+RT: Up; hold Black+RT: Down; hold Black+RT: Down; release holds: X; release holds: X; release holds: LT; release holds: RT |
| 7 | P2 ability editor | Enabled; paused gameplay | hold Black+RT: Up; hold Black+RT: Up; hold Black+RT: Down; hold Black+RT: Down; release holds: X; release holds: X; release holds: LT; release holds: RT |
| 8 | Toggle score display | Enabled; paused single-player | release holds: Right; release holds: Right; release holds: Right; release holds: Left |
| 9 | Add 100 bonus points | Enabled; paused single-player | hold Black+White: Right; hold Black+White: Right; hold Black+White: Right; hold Black+White: Left |
| 10 | Unlock ninja items | Enabled; item selection | hold LT+RT: Up; hold LT+RT: Down; hold LT+RT: Up; hold LT+RT: Down; hold LT+RT: X; hold LT+RT: X; hold LT+RT: X; hold LT+RT: Left; hold LT+RT: Right; hold LT+RT: Left; hold LT+RT: Right; hold LT+RT: X; hold LT+RT: X; hold LT+RT: X |
| 11 | Add 10 unlocked items and 100000 money | Enabled; item selection | hold LT+RT: Up; hold LT+RT: Left; hold LT+RT: Down; hold LT+RT: Right; hold LT+RT: X; hold LT+RT: X; hold LT+RT: X |
| 12 | Unlimited item capacity | Enabled; item selection | hold White+LT+RT: Up; hold White+LT+RT: Up; hold White+LT+RT: Down; hold White+LT+RT: Down; hold White+LT+RT: Left; hold White+LT+RT: Right; hold White+LT+RT: Left; hold White+LT+RT: Right; hold LT+RT: X; hold LT+RT: X; hold LT+RT: X |
| 13 | Unlock all characters | Enabled; title/start | hold Black+White: Up; hold Black+White: Right; hold Black+White: Left; hold Black+White: Down; release holds: LT; release holds: RT |
| 14 | Unlock current character missions | Enabled; mission selection | release holds: White; release holds: White; release holds: LT; release holds: RT; release holds: Right; release holds: X; release holds: L-click; release holds: R-click |
| 15 | Unlock current mission layouts | Enabled; mission selection | release holds: R-click; release holds: L-click; release holds: LT; release holds: RT; release holds: White; release holds: Black |
| 16 | Inactive prefix: enemy debug mode | Inactive / impossible prefix | hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; release holds: White; release holds: White; release holds: LT; release holds: RT; release holds: Black; release holds: L-click; release holds: R-click |
| 17 | Enemy debug mode (masked out) | Inactive / impossible prefix | release holds: White; release holds: White; release holds: LT; release holds: RT; release holds: Black; release holds: L-click; release holds: R-click |
| 18 | Inactive prefix: attack power | Inactive / impossible prefix | hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; hold White+RT: Up; hold White+RT: Down; hold White+RT: Up; hold White+RT: Down; release holds: X; release holds: X; release holds: X |
| 19 | Inactive prefix: attack power | Inactive / impossible prefix | hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; hold White+RT: Up; hold White+RT: Down; hold White+RT: Up; hold White+RT: Down; release holds: X; release holds: X; release holds: X |
| 20 | Toggle P1 attack power x5 | Enabled; paused gameplay | hold White+RT: Up; hold White+RT: Down; hold White+RT: Up; hold White+RT: Down; release holds: X; release holds: X; release holds: X |
| 21 | Toggle P2 attack power x5 | Enabled; paused gameplay | hold White+RT: Up; hold White+RT: Down; hold White+RT: Up; hold White+RT: Down; release holds: X; release holds: X; release holds: X |
| 22 | Inactive prefix: alternate mode | Inactive / impossible prefix | hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; hold Black+White: Left; hold Black+White: Right; hold Black+White: Left; hold Black+White: Right; hold Black+White: Up; hold Black+White: Down; hold Black+White: Up; hold Black+White: Down |
| 23 | Toggle alternate/B-Side runtime mode | Enabled; title/start | hold Black+White: Left; hold Black+White: Right; hold Black+White: Left; hold Black+White: Right; hold Black+White: Up; hold Black+White: Down; hold Black+White: Up; hold Black+White: Down |
| 24 | Full unlock and stock refill | Enabled; title/start | release holds: White; release holds: Up; release holds: Left; release holds: Down; release holds: Right; release holds: L-click; release holds: Black; release holds: X; release holds: A; release holds: B; release holds: Y; release holds: R-click; release holds: LT; release holds: RT |
| 25 | Inactive prefix: combat modifier | Inactive / impossible prefix | hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; release holds: Up; release holds: Down; release holds: Right; release holds: Left; release holds: A; release holds: A; release holds: A |
| 26 | Inactive prefix: all P1 abilities | Inactive / impossible prefix | hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; release holds: Up; release holds: Down; release holds: Right; release holds: Left; release holds: A; release holds: A; release holds: A |
| 27 | Inactive prefix: all P2 abilities | Inactive / impossible prefix | hold Up+Down+Left+Right+Start+Back+L-click+R-click+A+B+X+Y+Black+White+LT+RT: R-click; release holds: Up; release holds: Down; release holds: Right; release holds: Left; release holds: A; release holds: A; release holds: A |
| 28 | Success sound and flag 10000000; no effect consumer located | Enabled; title/start | release holds: Up; release holds: Down; release holds: Right; release holds: Left; release holds: A; release holds: A; release holds: A |
| 29 | Unlock Through the Portal | Enabled; title/start | release holds: White; release holds: Up; release holds: Black; release holds: Down; release holds: LT; release holds: Right; release holds: RT; release holds: Left |
| 30 | Unlock B-Side option | Enabled; title/start | hold LT+RT: Down; hold LT+RT: X; hold LT+RT: X; hold LT+RT: Up; hold LT+RT: X; hold LT+RT: X; hold LT+RT: Left; hold LT+RT: X; hold LT+RT: X; hold LT+RT: Right; hold LT+RT: X; hold LT+RT: X |

## Findings beyond the supplied list

- Player 2 has separate restore-health, ability-editor and x5-power sequences on controller 2.
- Title/start: hold White+Black and enter Left, Right, Left, Right, Up, Down, Up, Down. The handler toggles the alternate runtime mode and save-option bit 0 at `01BA2B79`. Its B-Side identification is inferred from its consumers; no visual check yet.
- Title/start full unlock: White, Up, Left, Down, Right, L-click, Black, X, A, B, Y, R-click, LT, RT. This refills all three stock lists, enables unlimited capacity, opens mission/layout progress, all characters, B-Side and Portal. It preserves already-earned rank values in its unlock loop.
- The title/start Up, Down, Right, Left, A, A, A entry is accepted, but its only traced effects are a sound and flag `10000000`. It is not advertised as a working gameplay cheat.
- “New ability” is an editor: hold RT+Black, Up, Up, Down, Down; release them, then X, X, LT, RT. The supplied list omitted the final RT. Use D-pad to select, A to toggle an ability, B to leave.
- No separate invisibility button combo appears in this 31-entry table. Invisibility is item 17; its runtime timer and status flag were traced separately.

## Effect addresses and evidence

| Effect | Data | Original code evidence |
|---|---|---|
| Kuji | P1 `008E40AA`, P2 `008E40C6`, uint16, full=36 | `001957C0`, `00251820` |
| Bonus score | P1 `008E40AE`, uint16, each unit adds 100 | `002519D8`, `002B1792` |
| Score display | `01BAADAC`, DWORD bit 0 | `002518CE` |
| Cheat requests/flags | `01BAA8C0`: items=400, add stock=800, capacity=1000, missions=4000, layouts=8000, P1 power=100000, P2 power=200000 | `002515E0`, `002CADE0`, `002CD690`, `0020C830` |
| Characters | `01BA2B80`, bits 0..19; Semimaru bit 16 | `00251A61`, `0023E146`, `002AB199`; portrait name table `00455034` |
| B-Side option / Portal | `01BA2790` bit 0 / `01BA2D11` signed byte >=0 | `00251E19`, `00251DE4` |
| Health | player +48 and +54, copy max at +50 | `00296340` |
| Invisibility | player +38C timer, +392 bit 08, +EC bit 20000 | `000B5863`..`000B58B0`, `000A6D9D` |
| Chameleon | player +38A timer and +392 bit 20; must initialize using actual spell first | `000B5220`, `000A6CC9` |
| Stock | `01BD02A0 + item*4`: uint16 flags mask 0002; uint16 count at +2 | `002C31A0`, `002CADE0` |
| Mission GM reward IDs | `004ECE58 + mission*6 + character*2` | `002ABD63`..`002ABDD7` |

The master-reward list differs by character. The individual item patches grant an item directly; they do not falsify mission ranks.
Semimaru’s traced natural unlock tests rank 5 on all 12 Rikimaru missions, rather than the “Expert Ninja” wording in the supplied list.
