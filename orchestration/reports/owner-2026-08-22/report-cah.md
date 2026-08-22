# Lane CAH — "my custom hero never shows up in the build options" (queue Q69)

Owner report 2026-08-22. Brief: `orchestration/briefs/owner-2026-08-22/brief-cah.md`.

- Base commit: `47c2aa8d` (worktree branch `worktree-agent-aa0ca7621c4ed8b4c`).
- Every number below came from the **selected** men pack
  `rotwk-men-vslice/b361ec5fc2cc72aa98ab5362538636d6be6cadbe82fedbf3000752bad072d4e7`
  (`workspace/content-packs/selection.json` `activePack`), whose compiled CAH table
  is `data/cah/system.json` `descriptorSha256 = f83dfba2ca5c89b29bd4875b18580de336598ab83b130f75905412dd4d3d7d5a`.
- Logs: `C:\Users\Jonathan\Desktop\open-bfme\workspace\logs\cah-owner\`.
- Nothing was published, no `selection.json` change, no `VERSION` change.

## 1. The cause, proven

**Suspect (a), and only (a).** The setup screen's Hero column re-selected item 0
("-") on every rebuild (`main_menu.gd::_populate_row_hero`), so a player who
never opened that dropdown reported *no hero*. `apply_skirmish_selection()`
still wrote that as `retail_picked_created_hero_documents = []`, and an **empty
pick is authoritative** in `retail_vertical_slice.gd:2058-2062` — so the slice
fielded no created hero, the fortress hero page had none on it, and nothing
printed a reason. The player's own hero existed, validated, and was eligible;
the screen simply never handed it over.

Why no gate saw it: `menu_instant_runner.gd:287` drives the same screen and the
same launch write but calls `picker.select(picked_index)` first — it makes the
pick the player never made. `cah_match_runner` and
`fortress_command_surface_runner` hand a profile to the roster directly and
never go through the setup screen. The one path the owner actually walks had no
coverage, and it was the broken one.

### The other suspects, each ruled out with evidence

**(b) exported `user://` differs from the editor's — NO.** `game/project.godot`
sets `config/name="Open BFME"` and does **not** set
`application/config/use_custom_user_dir`, so editor, headless runner and
exported build all resolve `user://` to
`%APPDATA%\Godot\app_userdata\Open BFME`. The owner's store is there and holds
exactly one hero.

**(d) `admitted_seat_heroes` refusing the profile — NO, for skirmish.** A
read-only probe (log `workspace/logs/cah-owner/owner-profile-probe.txt`) read
`user://cah-heroes` **read-only**, wrote nothing into it, and proved the real
store byte-identical before and after
(`da7b16f65e6fcadbc10137b6.json:1093/7eaa78b0...f9d445`):

```
PROBE pack_descriptor=f83dfba2ca5c89b29bd4875b18580de336598ab83b130f75905412dd4d3d7d5a
PROBE da7b16f65e6fcadbc10137b6.json heroId=da7b16f65e6fcadbc10137b6 name=Gate Hero
      descriptor=8ba5d74613dbd715d010f248a00f65de889946224b4542b64b270a895c455a20 class=0/0
PROBE   validate_profile -> OK
PROBE   subclass_allows_men -> true
PROBE   admitted(skirmish/lenient)=1 refusals=[]
PROBE   admitted(lockstep/strict)=0 refusals=["... the hero was built against different content"]
PROBE   producible_created=["CreateAHero__da7b16f65e6fcadbc10137b6"]
PROBE real_store_untouched=true
```

So the owner's own hero — `Gate Hero`, the same
`create-ahero-da7b16f65e6fcadbc10137b6` the v0.2.8 `run.log` fielded — validates
against the current men pack and reaches the producible roster the moment the
setup hands it over. The probe was a one-off diagnostic and is **not** committed
(a gate that reads the player's real store is exactly what this codebase has
been burned by); its log is the evidence.

**(c) the HUD hiding created heroes — already fixed, now asserted.** The Q45
world-radial truncation is genuinely fixed at `retail_hud.gd:4587` (the ring is
sized to `entries.size()`, buttons past the palantir pool dispatch directly).
Nothing tested it, so this lane added the assertion (section 3).

## 2. The fix

`game/src/ui/main_menu.gd`

- `_populate_row_hero`: an **untouched** human row now selects the most recently
  saved eligible hero instead of leaving "-" selected. "-" is still item 0,
  still offered, still selectable.
- `_most_recently_saved_hero_index()`: newest by profile-file
  `FileAccess.get_modified_time`, ties broken on hero id so the offered default
  is stable across visits.
- `_on_row_hero_changed(row)` replaces the anonymous `hero_changed` lambda and
  records `_row_hero_choice_made[row]`. Once the player has worked that column,
  their choice — including "-" for *bring none* — survives every later rebuild
  and is never overwritten by the default. Programmatic `select()` does not emit
  `item_selected`, so re-populating a row cannot fake a player choice.

### Retail citation, and what I could NOT cite

Honest: **retail's own default for that combo box is not derivable from shipped
data.** The skirmish setup rows are APT/Flash (`apt/mpgamesetup.big`), and its
`.const` carries only the column header (`GUIHero`, `APT:HeaderHero` = "Hero");
the list contents and the initial selection are filled by C++ (`game.dat`
carries the source path `...\GameClient\Gui\GUICallbacks\Apt\AptMyHero.cpp` and
the profile glob `MyHero*.cah`). No `.wnd`, `.ini` or `.str` entry states the
default. So "default to the most recent saved hero" is the **owner's directive
in the brief**, not a retail-derived constant, and I am labelling it as such
rather than dressing it up.

What retail data *does* say, and what the code comment cites: every faction's
`playertemplate.ini` `BuildableHeroesMP` **begins with `CreateAHero`** —
`playertemplate.ini:100` (Men), `:150` (Rohan), `:198` (Elves), `:243`
(Dwarves), `:288` (Isengard), `:334` (Mordor), `:381` (Wild) — so retail always
reserves the created-hero slot on the fortress. An empty hero page is ours, not
retail's. Related strings: `lotr.str:26101 RULE:AllowCustomHeroes`,
`lotr.str:26010 APT:HeaderHero`. No numeric constant was invented.

## 3. Tests

**New, failing-first:** `game/tests/cah_untouched_setup_runner.gd`. Saves two
heroes a filesystem tick apart in a **scratch** store
(`tests/cah_profile_sandbox.gd`), instantiates the real `boot.tscn` menu, waits
for the skirmish options, **never touches the Hero column**, calls the real
`apply_skirmish_selection()`, then classifies the men slice from that recorded
GameState and buys the hero through `submit_command` + `advance`. It also pins
the reverse: choosing "-" is recorded, fields nothing, and survives a row
rebuild.

**Extended:** `game/tests/fortress_command_surface_runner.gd` section 6 now
asserts the world ring around the fortress carries the **whole** hero page
(`world_radial_buttons().size() == hero_entries.size()`) and that the created
hero is on it (`men_created_hero_is_on_the_world_radial`) — the palantir half
(`men_created_hero_is_purchasable_on_the_fortress`, cost, command points) was
already covered there.

## 4. Numbers — before -> after

| Runner | Before | After | Note |
|---|---|---|---|
| `cah_untouched_setup_runner` (new) | **12 passed / 5 failed** | **19 passed / 0 failed** | failing-first; red log `red-untouched-setup.txt`, green `green-untouched-setup.txt` |
| `fortress_command_surface_runner` (men) | 113 / 3 | **115 / 3** | +2 new world-radial checks; the 3 failure **names** are identical before and after |
| `cah_match_runner` | 72 / 3 | 72 / 3 | same 3 names |
| `cah_create_a_hero_runner` | 267 / 0 | 267 / 0 | |
| `cah_awards_runner` | 12 / 0 | 12 / 0 | |
| `retail_four_unit_hud_runner` (HUD gate) | 124 / 0 (brief baseline) | **124 / 0** | |
| `retail_mp_lobby_runner` | 26 / 1 | 26 / 1 | pre-existing: re-run on the **unmodified main checkout** gives 26/1 too |
| `castle_lobby_admission_runner` (lobby 11/0) | 7 / 4 | 7 / 4 | pre-existing content state: selection carries a 10-map pack with no castle maps; unmodified main gives 7/4 too |
| `retail_state_pin_runner` | `b025d162...` | `b025d16237ff644d66211a9cc26872f18b61520b9a377f11e9e99c6eceb43f58` | **unchanged**, matches the pinned value |

Pre-existing failure names (unchanged by this lane):
`men_precompiled_page_selector_fallback_is_named`,
`men_left_click_selection_routes_through_the_castle_resolver` (x2);
`cah_match`: presentation sweep on `bfme2.object.gondor-archer-range`, "a hero
whose skin the pack cannot ship stays off the roster", "the exclusion names the
skin the pack does not ship"; `retail_mp_lobby`:
`lobby_panel_builds_for_both_sides`; `castle_lobby`: the four castle-map rows.

## 5. Honest residue — what I did NOT do

1. **`cah_capture_runner` was not run to completion.** It hangs under
   `--headless` (it opens a rendering context and screenshots; its own header
   says "IT ASSERTS NOTHING. It is a camera, not a test."). Killed at 300 s; the
   same hang was seen before any of my edits. Not a gate, not a regression, but
   I cannot claim it green.
2. **The multiplayer lobby has the identical default.**
   `multiplayer_lobby.gd:244-246` builds `hero_opt` with "-" selected and
   `_populate_hero_picker()` never re-defaults. This lane deliberately left it
   alone (the owner's bug is single-player, and an MP default changes what each
   seat announces). It is a real follow-up.
3. **The owner's existing hero would be refused in multiplayer today.** It was
   built against CAH descriptor `8ba5d746...`; the selected v0.2.9 men pack ships
   `f83dfba2...`. Skirmish is lenient by design and admits it; **lockstep is
   strict and refuses it** ("the hero was built against different content").
   Every recook re-mints that descriptor, so every recook silently invalidates
   every saved hero for MP. Nobody has decided what should happen there.
4. **No retail oracle for the combo default** — see section 2. Labelled as an
   owner directive, not retail.
5. **Not verified in a real (non-headless) game window.** All proof is headless:
   the roster, the fortress command surface, the world ring and the purchase are
   read off the live surfaces, not off pixels.
6. `game/.godot/global_script_class_cache.cfg` was copied from the main checkout
   and `--import` run once, because a fresh worktree cannot resolve `class_name`
   types and every runner fails to compile without it. That directory is
   git-ignored; nothing under it is committed.

## 6. Files touched

- `game/src/ui/main_menu.gd` (the fix)
- `game/tests/cah_untouched_setup_runner.gd` (new, failing-first)
- `game/tests/fortress_command_surface_runner.gd` (world-radial assertions)
- `orchestration/reports/owner-2026-08-22/report-cah.md` (this file)
