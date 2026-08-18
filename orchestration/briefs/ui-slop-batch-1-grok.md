# UI slop batch 1 — Q35 setup picker, Q36 placement visuals, Q42 footsteps, Q38 hero roster (Grok)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Read AGENTS.md. Claim Q35, Q36, Q42,
Q38 (owner=grok-ui-1) in orchestration/queue.md. Standard rules: explicit-path
git, no sweeps, sequential Godot, logs under workspace/logs/ui1-*.txt. NO sim
changes, NO pack builds, NO selection change, NO pins other than runner counts
you re-pin with dated comments in repo-root tools/gate-m2-focused.ps1. Every
item is presentation/UI/audio; the sim state pin must not move (prove it:
retail_state_pin_runner 0e4bcdbf… before/after).

Owner intent: "a lot of AI slop" — invented constants where retail authors
data. Rule for every item: find the retail authoring (anchors below), bind to
it, delete the guess. Anchors are from orchestration/reports (scoping 2026-08-18)
and the queue rows; verify each anchor is still current before editing.

## Q35 — skirmish hero picker blank until faction switch (S)
`game/src/ui/skirmish_setup.gd:293-301` seeds rows with only "-";
`game/src/ui/main_menu.gd:2669 _refresh_hero_rows()` is wired to army/controller
change (:2663-2665, :2821) and the custom-heroes toggle (:4197) but never
called on initial build (:4185-4205). Fix: call it once after rows are built
(next to the `_refresh_skirmish_launch_state()` seeding). Test: a menu runner
check that the hero OptionButton for the default faction is populated on first
show (find the existing menu/skirmish-setup runner and extend it).

## Q36 — placement visuals (S)
Retail draws NO placement outline. The farm's white claim circle is
`TerrainResourceClientBehavior` (farm.ini:100-101) over
`TerrainResourceBehavior.Radius = GONDOR_FARM_MONEY_RANGE` (gamedata.ini:2141
→ 300). Ours: `game/src/retail_slice/retail_vertical_slice.gd:5424` green
FootprintCircle, :5435 green EffectivenessRing, :5417 green fallback quad, and
a comment at :5403 claiming green/red validity tint "as retail" — unsupported.
Fix: delete the FootprintCircle + fallback quad; EffectivenessRing becomes a
FILLED translucent white disc (alpha ~0.25-0.35; state your choice) whose
radius is the authored TerrainResourceBehavior radius already read by
`_structure_effectiveness_radius_local`; structures without that behavior draw
NO ring; keep the ~50% ghost (:5404-5406). Remove the false comment. Test:
placement runner (find/extend) asserting no green material remains and the
disc radius equals the authored value for GondorFarm.

## Q42 — hero footsteps (M)
Retail: `AnimationSoundClientBehavior` frame-keyed rows (eomer.ini:744-747:
`Sound:FootstepDirtA Animation:RUEomer_SKL.RUEomer_RUNA Frames:4 15`, RUNB
frames 5 15 26 36) + `ModelConditionSoundSelectorClientBehavior SoundState =
MOUNTED` (:726-730, VoiceMove EomerVoiceMoveMounted) + `SoundImpact =
ImpactHorse` (:712). Ours: rows imported into
`retail_slice_audio.gd:198-202 playable_unit_animation_sounds`;
`select_animation_sound()` called only for category "bodyfall"
(:1610-1633 `_route_bodyfall`); zero footstep code. Fix: add a "footstep"
routing path mirroring `_route_bodyfall`, driven by `clip_frame_clock.gd`
(exists) — fire the authored sample at the authored frames of the authored
clip; honour the MOUNTED sound state selector so mounted heroes never play
foot samples. If a hero's pack lacks the rows, name the gap; never pick a
generic sample. Test: audio runner (extend retail_slice_audio's runner)
asserting Eomer RUNA emits FootstepDirtA at frames 4 and 15 and nothing else,
and that mounted state selects the mounted voice/impact.

## Q38 — hero roster bar (M)
Retail: `InGameHeroSelect.swf` movie at authored translation [375,700] on the
1024x768 stage (`retail_hud_apt_runtime.gd:66-79`, labels _show:19 _fadein:9,
frameCount 29; art in effective-assets/InGameHeroSelect_geometry/); each hero
portrait: level badge bottom-left, CURVED health arc under the portrait,
circle highlight when selected; `NonCommand_SelectAllHeroes`
(commandbutton.ini:3494-3496) with the faction icon to its left; palantir big
portrait shows "Level: N" with a curved bar. Ours: `retail_hud.gd:4082-4145`
HBox + 58x72 Buttons + stock ProgressBar (:4141) = the green rectangle the
owner circled; dish level bar :4055-4059 same defect. Fix: (a) anchor the bar
via the authored [375,700] transform scaled with the stage (coordinate with
Q37's stage mapping — if Q37 has not landed, place via the same 1024x768
stage→viewport transform helper you introduce, and Q37 reuses it); (b) draw
health as an arc ring hugging the circular portrait mask (retail geometry
files are the oracle for arc angles/thickness — read InGameHeroSelect_geometry
shapes; if you cannot derive exact geometry, state the approximation and cite
the shape file); (c) level badge bottom-left; (d) selection = circle highlight;
(e) add the select-all-heroes button + faction icon (icon from the faction
manifest / commandbutton art already imported); (f) same arc treatment for the
palantir big-portrait level bar. Test: extend retail_four_unit_hud_runner /
the hero-bar runner: badge, arc, highlight, select-all present; screenshot
capture PNGs before/after under workspace/logs/ui1-frames/ (use the
cah_capture_runner viewport→png pattern) — the orchestrator will look at them.

## Definition of Done
1. Each item's runner check(s) green; re-pinned counts in gate-m2-focused.ps1
   with dated comments; retail_state_pin_runner UNCHANGED 0e4bcdbf…;
   retail_spellbook_runner 218/0; slice_start_roster_presentation_runner 22/0;
   boot_startup_runner 44/0; zero `SCRIPT ERROR`/`Invalid access` in every
   runner's stderr (paste counts).
2. Screenshots: workspace/logs/ui1-frames/{placement-before,placement-after,
   herobar-before,herobar-after}.png.
3. hygiene PASS; git status clean; commits `fix(ui):`/`feat(ui):`/`fix(audio):`
   /`test(...)`, explicit paths. Report orchestration/reports/ui-slop-batch-1.md
   with per-item retail anchor → change → evidence. Close Q35/Q36/Q42/Q38 rows.
