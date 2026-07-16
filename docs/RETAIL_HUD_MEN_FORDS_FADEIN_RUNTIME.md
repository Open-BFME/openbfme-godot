# Retail Men/Fords side-command FadeIn runtime

The private HUD now binds the exact BFME2 1.06 `InGameSideCommandBar.FadeIn`
route to the live Men/Fords selection state. This is a typed adapter, not a
general ActionScript virtual machine or generic timeline player.

## Converted contract

The converter emits `sideCommandFadeRuntime` only after validating:

- `InGameSideCommandBar.apt` SHA-256
  `84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef`;
- FadeIn body `[8836,9009)`, completion program `[9404,10086)`, and settled
  stop `[10088,10090)` by exact byte hash;
- the retail `commandset.ini`, `commandbutton.ini`, and six Men object INIs;
- all four battalion and five structure object-to-command-set declarations;
- a nonempty authored `InPalantir=Yes` command closure for every selection;
- shared `OK_FOR_MULTI_SELECT` Toggle Stance, Attack Move, and Stop commands
  for mixed battalion selection.

The nine selectors are Soldier, Tower Guard, Archer, and Knight battalions,
plus Fortress, Farm, Barracks, Archery Range, and Stable structures.

## Live typed input

`RetailVerticalSlice._refresh_hud()` passes the existing state without a second
selection model:

- sorted `simulation.selected_ids`;
- mutually exclusive `selected_structure_id`;
- `simulation.entities` and `simulation.structures` rows;
- `simulation.winner` and fixed local team 0.

`RetailHud` forwards that context to `RetailHudAptRuntime`. The evaluator
requires positive unique sorted IDs, exclusive selection kinds, a living local
row, `winner == -1`, a declared roster selector, and at least one authored
eligible command. Empty, enemy, dead, post-match, and outside-roster selections
do not dispatch FadeIn. Malformed input fails closed and hides the private APT
surface.

## Exact state and frame behavior

The loaded side-command movie starts in native state 1 at one-based frame 1.
On the first eligible selection outside native states 2 and 3:

1. Current frames outside `[32,42)` target frame 12.
2. Current frames inside `[32,42)` target `12 + 42 - currentframe`, reversing
   an in-progress FadeOut. The sealed examples are `32 -> 22`, `37 -> 17`, and
   `41 -> 13`; boundary frames 31 and 42 target 12.
3. Dispatch writes native state 2 and advances at the authored 33 ms interval.
4. Frame 22 invokes `OnAptInGameSideCommandBarFadeInComplete`, changing state
   2 to settled-visible state 3.
5. Playback continues through the unchanged settled frames and stops on frame
   31.

No FadeOut behavior is guessed. The generic `timeline-playback-not-bound`
blocker and the `palantir-nondefault-frame-selection-not-bound` blocker remain.
The broad `side-command-bar-fade-runtime-not-bound` blocker is removed. The
single `side-command-native-row-alias-trace` gate remains metadata-only: it
blocks a claim about exact native field aliases, not this typed implementation.

## Acceptance

- Focused converter tests and Ruff pass.
- The legal-safe fixture HUD runner passes 67 checks.
- Each fresh private A/B bundle passes 129 runtime checks sequentially.
- A/B scene contracts have the same aggregate hash.
- Godot headless editor import/compile passes after the live HUD binding.

The production contract has 19 blockers, 66 supported ActionScript programs,
8 unsupported programs, and one typed Men/Fords side FadeIn runtime.
