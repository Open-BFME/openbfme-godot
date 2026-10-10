# XMAP-1: how much of the BFME2 decomp carries over to RotWK

Question (owner, 2026-10-09): how much of the Open-BFME-2 decompilation (BFME2 1.06 `game.dat`,
byte-matching C++) can be used for RotWK 2.01? Would a separate BFME2 -> RotWK project pay off?

Answer: most of RotWK *is* BFME2. 95% of RotWK's code bytes have a BFME2 counterpart (91% the same
function, identical or changed), 64% are byte-identical (after masking addresses), and **49% of RotWK's code is identical to a BFME2
function that already has byte-matched decomp C++** (43.5% human-authored, the rest generated EH
funclets and vendored libraries). Counting the "same function, changed" tier, 67% of RotWK's code
bytes have byte-matched C++ for their counterpart. The expansion is not a separate block: RotWK-only
code is 5.4% of the bytes, in small islands (the largest is 21 KiB) between BFME2 functions. A
separate project is not needed: the map below (tools/re/, regenerated in about 3 minutes) is the
bridge, and it improves on its own as Open-BFME-2 matches more functions.

Inputs: RotWK `game.dat` (68,404 functions, Ghidra), BFME2 1.06 `game.dat` (66,336 functions,
Open-BFME-2's `reverse/ghidra_functions.csv`), the Open-BFME-2 clone at 3fc3508b69 (73,322 matched
ledger rows, 1,625 attempts). Method and tiers: `tools/re/README.md`. The RotWK `game.dat` used is
the community-modified one (S-001); only 3 functions branch into the added sections
(RW 0x5D893C tier B, 0x63252F tier A, 0xA3DA7E RotWK-only), so the caveat barely touches the map.

## Overall (share of RotWK `.text` bytes, function count in brackets)

| Tier | Bytes | Functions |
|---|---|---|
| A identical after masking | 63.5% | 58,024 (84.8%) |
| B same-shape (operand values differ: struct offsets, constants) | 14.6% | 4,745 (6.9%) |
| B edited (similarity >= 0.9) | 12.6% | 1,755 (2.6%) |
| C string-anchored | 3.1% | 207 |
| D diverged (0.6-0.9) | 0.8% | 329 |
| RotWK-only | 5.4% | 3,344 (4.9%) |

Decomp status of the counterpart (all tiers): byte-matched C++ 67.2% of RotWK bytes (A 49.0%,
B 17.0%), recovered-but-not-matching attempts 7.9%, file assignment only 3.5%, partially ledgered
3.5%, prebuilt library / re-encoded bytes 2.5%, nothing yet 10.0%. Game code only (libraries and
compiler EH glue excluded, 7.0 MiB): A 62.5%, B 29.4%, RotWK-only 6.0%; A with byte-matched C++
48.0% (authored 45.4%), any tier with byte-matched C++ 67.4%.

## Per subsystem (share of the subsystem's RotWK bytes)

Subsystem = the decomp source path of the counterpart (or its TU assignment), else RotWK's own
`__FILE__` strings, else link-order neighbours; "unclassified" has no evidence either way.

| Subsystem | Functions | KiB | A | B | C+D | RotWK-only | A with byte-matched C++ | any tier with byte-matched C++ |
|---|---|---|---|---|---|---|---|---|
| engine core / system | 13777 | 1478 | 71.1% | 23.2% | 2.1% | 3.6% | 54.3% | 70.7% |
| GameClient / draw | 5701 | 1243 | 77.2% | 18.9% | 1.4% | 2.5% | 56.6% | 69.8% |
| APT / UI / shell | 3525 | 799 | 65.2% | 29.0% | 3.4% | 2.4% | 51.0% | 70.6% |
| library | 4091 | 670 | 61.5% | 11.8% | 25.3% | 1.4% | 53.9% | 64.8% |
| network / online | 2826 | 624 | 66.4% | 28.1% | 1.2% | 4.2% | 64.3% | 88.8% |
| objects / modules | 3725 | 495 | 41.9% | 48.3% | 1.0% | 8.7% | 35.9% | 67.3% |
| AI / pathfinding | 2000 | 449 | 32.4% | 56.6% | 2.5% | 8.5% | 23.8% | 57.6% |
| unclassified | 2734 | 438 | 44.0% | 20.4% | 1.0% | 34.7% | 0.0% | 0.0% |
| math / support libs | 2714 | 362 | 75.9% | 20.5% | 1.0% | 2.6% | 73.1% | 91.0% |
| EH/compiler glue | 21258 | 299 | 90.8% | 8.9% | 0.0% | 0.2% | 61.6% | 68.9% |
| script engine | 1254 | 256 | 38.6% | 52.0% | 8.2% | 1.2% | 32.8% | 73.9% |
| combat / weapons | 1182 | 166 | 50.2% | 41.9% | 1.5% | 6.4% | 42.1% | 67.6% |
| INI / templates | 975 | 129 | 75.7% | 19.1% | 4.4% | 0.9% | 67.4% | 85.8% |
| horde / locomotion | 620 | 123 | 41.3% | 52.4% | 1.1% | 5.2% | 29.7% | 45.4% |
| castles / structures | 459 | 104 | 45.5% | 46.6% | 0.4% | 7.5% | 39.3% | 59.0% |
| audio | 675 | 101 | 87.4% | 9.2% | 0.7% | 2.6% | 64.8% | 72.3% |
| War of the Ring / LivingWorld | 460 | 95 | 40.5% | 43.1% | 5.5% | 10.9% | 32.3% | 60.5% |
| other | 276 | 72 | 79.3% | 19.3% | 0.4% | 0.9% | 68.9% | 82.9% |
| map / terrain logic | 135 | 23 | 92.9% | 6.8% | 0.3% | 0.0% | 78.3% | 83.6% |
| Create-a-Hero | 17 | 8 | 55.8% | 24.2% | 13.9% | 6.1% | 0.0% | 0.0% |

Game logic (objects, AI, combat, horde, castles, scripts) changed the most: there tier B is
42-57% of the bytes, mostly struct layouts that grew (B same-shape: the same instructions with
other field offsets). The engine core, renderer, UI, network, audio and math are 65-87% identical.

## What RotWK added (RotWK-only 415 KiB of game code, plus 983 KiB of edited functions)

New / changed code by subsystem (RotWK-only KiB / tier D + B-edited KiB): engine core 53 / 184,
objects and modules 43 / 69, AI 38 / 56, renderer 31 / 143, network 26 / 139, UI 19 / 148, combat
11 / 15, War of the Ring 10 / 10, castles 8 / 22, horde 6 / 13, script engine 3 / 71, Create-a-Hero
0.5 / 2, plus 152 KiB RotWK-only that has no subsystem evidence. Strings only RotWK-only code
references name the new pieces: the faction fortress citadels (`AngmarFortressCitadel`, ...),
`AngmarThrallMaster`, `FactionAngmar`, the Angmar campaigns, skirmish-AI tactics (`WallTactic`,
`FeintAttack`, `PincerAttack`, `FormationAttack`, `FlagCaptureSquad`, `AIWoTRForfeitTactic`),
`SummonReplacementSpecialAbilityUpdate`, War of the Ring treasury / veterancy / disband screens,
Create-a-Hero power costs, and the expansion's base-game install checks. Create-a-Hero and War of the
Ring were already in BFME2 1.06: 94% and 89% of their RotWK bytes have a BFME2 counterpart.

## Precision of the map

- Tier A, 30 random pairs (functions >= 24 bytes), instruction diff: 30/30 identical
  instruction-for-instruction. Identity (the right BFME2 instance): unique tier-A pairs are 98.7%
  link-order consistent with their neighbours (the rest are relocated functions); identical
  bodies with several BFME2 copies are resolved by link order and 99.8% consistent, and the 1,191
  that stay far (45 KiB) are flagged `amb-far`.
- Tier B, 2 x 30 random pairs, instruction diff and (for edited ones) Ghidra decompile vs decomp
  source: 60/60 the same function (one 15-instruction accessor counts as plausible rather than
  certain). B pairs are 98.7% link-order consistent. Typical differences: field offsets (grown
  structs), `__FILE__` build paths, stack frame sizes, inserted returns / branches, split tails.

## Open stops that the decomp source can close (from tools/re/stops_xmap.py)

Of 521 open stops, 412 cite code that resolves to a RotWK function; 301 cite at least one site
in a tier-A function with byte-matched decomp source, 71 more only tier-B sites with byte-matched
source. Ranked by skirmish impact (stop area), then by coverage (cited sites the source
describes: A counts 1, B 1/2). Top 30 (the full list with the files and names per site: run the
tool):

| Rank | Stop | Covered sites (A + B / cited) | Area |
|---|---|---|---|
| 1 | S-201 | 4A+0B/4 | Production AI hand-off |
| 2 | S-857 | 3A+0B/3 | Special power weather (HERO-1) |
| 3 | S-1791 | 2A+0B/2 | Shared model flags' dependents (COMBAT-4) |
| 4 | S-365 | 2A+0B/2 | Projectile drawing |
| 5 | S-420 | 2A+0B/2 | Skirmish AI vs AI victory |
| 6 | S-701 | 2A+0B/2 | Unit voice: draw module overrides |
| 7 | S-162 | 3A+1B/4 | Pathfinder zones and hierarchical search |
| 8 | S-1220 | 6A+0B/7 | Engine script events (HERO-2) |
| 9 | S-1581 | 3A+2B/5 | Melee engage exit (ANIM-1) |
| 10 | S-920 | 11A+4B/17 | Special power trigger |
| 11 | S-149 | 3A+0B/4 | ActiveBody and HordeContain |
| 12 | S-1520 | 3A+0B/4 | Wall span look (BUILD-4) |
| 13 | S-586 | 3A+3B/6 | Melee contact |
| 14 | S-1280 | 2A+2B/4 | FindPositionAround (BUILD-3) |
| 15 | S-414 | 2A+2B/4 | Skirmish AI base builder inference |
| 16 | S-891 | 2A+2B/4 | Skirmish AI tactic idle check inference |
| 17 | S-075 | 1A+1B/2 | Module factory |
| 18 | S-483 | 1A+1B/2 | Upgrade queries |
| 19 | S-681 | 1A+1B/2 | Damage FX |
| 20 | S-862 | 1A+1B/2 | ModelConditionSpecialAbilityUpdate emotions (HERO-1) |
| 21 | S-1029 | 9A+3B/15 | Crush warning (MODULES-3) |
| 22 | S-980 | 2A+3B/5 | Die / delete modules (MODULES-1) |
| 23 | S-1188 | 6A+4B/12 | AI hunt state (SCRIPT-3) |
| 24 | S-1024 | 2A+4B/6 | HitReactionBehavior (MODULES-2) |
| 25 | S-1792 | 2A+0B/3 | W3DTruckDraw tires (COMBAT-4) |
| 26 | S-111 | 1A+2B/3 | W3DTreeDraw behaviour |
| 27 | S-1223 | 1A+2B/3 | FellBeastSwoopPower (HERO-2) |
| 28 | S-270 | 1A+2B/3 | Skirmish random resolution |
| 29 | S-983 | 1A+2B/3 | Burning (MODULES-1) |
| 30 | S-461 | 6A+1B/10 | Construction look |
