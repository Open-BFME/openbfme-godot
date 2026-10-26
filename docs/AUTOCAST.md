# Ability autocast (lane AUTOCAST-1)

How RotWK 2.01 lets a hero use an ability by itself, read from the RotWK binary
(`game.dat`, caveat S-001) with the RotWK -> BFME2 -> decomp map (`tools/re/rw2decomp.py`).
"RW" addresses are RotWK virtual addresses. The simulation half was ported by lane
HERO-2 (S-1227); AUTOCAST-1 added the toggle paths, the AI's script actions and the glow.

## Which abilities can autocast

- A **CommandButton** with `AutoAbility = Yes` (+ 0x10C, field table of ControlBarCommands.cpp).
  Retail 2.01 has 126 such buttons: 125 `SPECIAL_POWER`, one `FIRE_WEAPON`
  (`Command_GimliAxeThrow`).
- The object needs an **AutoAbilityBehavior** module (create RW 0x64AC9A, data RW 0x85D355,
  field table RW 0xC57EC0). `SpecialAbility` ties a module to one power; a module without it
  takes any button. The Witch-king has `ModuleTag_MorgulBladeAutoAbility`
  (`SpecialAbilityWitchkingMorgulBlade`, `Query = 1 MORGULBLADE_OBJECT_FILTER`,
  `AllowSelf = No`, `ForbiddenStatus = INSIDE_GARRISON`) and a generic one after it.
- Further button fields read by the update: `AutoDelay` (+ 0x134), `PresetRange` (+ 0x130),
  `TriggerWhenReady` (+ 0x12C), `NeedDamagedTarget` (+ 0x138),
  `AutoAbilityDisallowedOnModelCondition` (+ 0x13C). The `Set/UnsetAutoAbilityUnitSound` voices
  (+ 0xD4 / + 0xE0) belong to the client's right-press path, not the update; they are parsed
  but their playback remains unported (S-3460).

## Per-object state

`AutoAbilityBehavior` + 0x20: the name of the auto button ("" = off); + 0x24 / + 0x30 the start
position for `BaseMaxRangeFromStartPos`. Ported in `HeroAbilityModules.h`; hashed by
`AutoAbilityBehavior::crc` (the name, the start position), so the toggle is in the lockstep CRC.

- toggle RW 0x85D76C: a button with AutoAbility other than the current one becomes the auto
  button and wakes the update (RW 0x85D48F); anything else clears it and sleeps forever.
- set RW 0x85D7B6 (scripts): the current button with `on = 0` clears; anything else sets.
- is-auto RW 0x85D741: the name is set and equals the button's.
- the module for a power RW 0x85D6AD (`SpecialAbility` empty or that power); the first module by
  name key RW 0x68BDA5 ("AutoAbilityBehavior").

## The toggle message (lockstep)

| Message | Handler | Arguments | Effect |
|---|---|---|---|
| `MSG_DO_AUTO_ABILITY` (1112) | RW 0x77B9BA | int power id, object id | the object's first SPECIAL_POWER button of that power (33 slots) toggles `forPower(power)` |
| `MSG_DO_AUTO_ABILITY_WEAPON` (1113) | RW 0x77B8EE | int weapon slot, object id | the first FIRE_WEAPON button with that `WeaponSlot` (+ 0x80) toggles the first module |

Both go through the command list, so they are recorded in replays like every other player command
(the recorder writes the frame's command list; `tools/replay/rotwk_replay.py` names both types).

## When the unit fires it (AutoAbilityBehavior::update, RW 0x85D9D1)

See the header comment of `AutoAbilityBehavior` in `HeroAbilityModules.h` (HERO-2): the button's
model condition gates, MOUNTED_ONLY / UNMOUNTED_ONLY, `IdleTimeSeconds` against the AI's idle
state, readiness RW 0x85D5EA (ForbiddenStatus, the power fully recharged, the button in the
command set, not stunned or stealthed, not IGNORE_AI_COMMAND), then the scan: range = vision,
`MaxScanRange`, else `PresetRange`; each `Query` slot near to far through ThePartitionManager with
`canSee` (RW 0x68FA3D) and the ObjectFilter; the target pick RW 0x85D4B5 (`AllowSelf`,
`NeedDamagedTarget` at 0.8, `MinScanRange`); the cast with `AUTO_ABILITY_TRIGGERED`
(0x20000000) through RW 0x696FD2 / 0x697890 / 0x6979D9. The next check is 50 frames later, or
`5 * AutoDelay`; a failed check sleeps 6 frames.

## How the computer player uses it

- Its heroes' abilities are mostly driven by **AISpecialPowerUpdate** (lane MOD-4, S-1421), a
  separate system: it presses the button through RW 0x696FD2 itself.
- **Autocast** is asked for by the skirmish AI's script library
  `libraries\lib_hero_behaviors\lib_hero_behaviors.map` in two subroutines (*b_Faramir - Use
  Captain of Gondor when Ready*, *b_Theoden - Use Kings Favor on Hero when Ready*:
  `TEAM_SET_AUTO_ABILITY(<This Team>, <button>, 1)`). **Both buttons
  (`Command_SpecialAbilityCaptainOfGondor`, `Command_SpecialAbilityKingsFavor`) have
  `AutoAbility = No` in 2.01**, and the action ignores such a button (RW 0x7C3DD8), so retail's
  skirmish AI never switches autocast on; its heroes' abilities come from AISpecialPowerUpdate.
- The other uses are campaign maps (the Evil campaign's Erebor and Rivendell, Shire, Blue
  Mountain, Celduin, Ettenmoors, the Good Rivendell): e.g. Erebor's *Set Gloin smash to
  autoability* `NAME_SET_AUTO_ABILITY(Gloin, Command_SpecialAbilityDwarvenGloinSlam, 1)`.
  Retail maps hold 10 `TEAM_SET_AUTO_ABILITY` and 19 `NAME_SET_AUTO_ABILITY` uses
  (`engine/tests/data/script-census.json`).
- The actions: `NAME_SET_AUTO_ABILITY` RW 0x7C3C88 and `TEAM_SET_AUTO_ABILITY` RW 0x7C3D61
  (every member): the first module (RW 0x68BDA5), the button by name with AutoAbility, for each of
  the 33 command set slots holding it RW 0x85D7B6(button, on). Ported in `ScriptActionsUnits.cpp`.

## The button: right press and glow (client)

- **Input**: the Apt input translator RW 0x83EE14 writes the mouse button into
  `TheAptWindowManager + 0x318` (0 left, 2 right) and posts the press into the Apt (RW 0xAE0B10).
- **The clip** (RotWK-only class, vtable RW 0xC8C3D4, built by the Palantir arc RW 0x92FF5C and
  the side bar RW 0x92F082 through RW 0x9D2AED): `<content>_OnPress` (RW 0x9D6E54) dispatches on
  + 0x318: 0 = left, 2 = right, other = nothing. A right press (RW 0x9D288A / 0x9D28D4) sends
  `GWM_RIGHT_UP` (0x4009) to the control bar unless the clip state is `_disabled` /
  `_cantAfford` without CANCELABLE (then the sound `Gui_PalantirCommandButtonDisabledClick`);
  `_notReady` toggles too (cooling-down abilities can be switched).
  The clip state names (RW 0xC8CAF8): `_unused _disabled _extraDisabled _cantAfford _static
  _notReady _up _visuallyEnabled`.
- **processCommandUI** RW 0x940435 with the right press: "autoable" = AutoAbility or CANCELABLE;
  `SPECIAL_POWER` (RW 0x941511) and `SPECIAL_POWER_TOGGLE` (RW 0x9418F7) send
  `MSG_DO_AUTO_ABILITY`, `FIRE_WEAPON` (RW 0x941391) `MSG_DO_AUTO_ABILITY_WEAPON`; the voice
  0x7E4 / 0x7E5 (set / unset) is played through RW 0x8DEDBB (not ported, S-3460).
- **The glow**: the clip's update RW 0x9D2B49 calls
  `InGameCommandButtonMovieClip::Impl::SetAutoAbilityOverlayVisibility` (RW 0x9D6F9C, BFME2 decomp
  `Rva005C7A29Overlay.cpp`, tier A) with: state not `_unused`, the module for the button (its
  power's, else the first) and is-auto. It calls the content's ActionScript
  `SetAutoAbilityOverlayState("_show" / "_hide")` on a change only; the content's destructor hides
  it (RW 0x9D6EC7). The art is the retail movie's own (`libInGameUI.apt`, `Palantir.apt`
  `AutoAbilityOverlays`), loaded from the player's files at runtime.

Ported in `ControlBar::rightPressButton`, `ControlBarButton::autoAbility`,
`AptPalantir::setOverlay`, `WindowManager::pressButton` and `InGameHud::mouseButton`.

## Stops

S-1227 (HERO-2: the update's open items), S-3460 (AUTOCAST-1: the voice, click sounds,
multi-selection source, the right press reaching only the command button clips).
