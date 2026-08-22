# Lane CAH — "my custom hero never shows up in the build options"

See _common.md. The player creates a hero in Create-a-Hero, starts a skirmish, and the created hero is not offered in the fortress's hero page. Code: game\src\content\cah_heroes.gd (profiles in user://cah-heroes), game\src\retail_slice\retail_vertical_slice.gd `_add_created_heroes` (~line 2025): in SINGLE PLAYER the hero is fielded ONLY if the skirmish setup handed over `retail_picked_created_hero_documents` containing it — "when it is present (even empty = '-' picked) it is authoritative".

Suspects: (a) the setup screen's Hero column defaults to "-"/empty so an untouched setup fields nothing (verify what retail's skirmish setup does for the human player when a created hero exists — Skirmish APT / GameSpy setup behaviour — and what game\src\ui\main_menu.gd and multiplayer_lobby.gd do; cite); (b) the exported dist build's user:// differs from the editor's (project.godot config/name, any CAH dir env override in cah_heroes.gd); (c) the HUD fortress hero page hides created heroes (Q45 world-radial truncation is already fixed at retail_hud.gd:4587 — confirm); (d) `admitted_seat_heroes` refusing the profile against the selected men pack's cah_system_runtime (stale profile schema / class table). The owner's v0.2.8 run.log shows a created hero `create-ahero-da7b16f65e6fcadbc10137b6` WAS fielded in that match, so at least one path works — find which and why the owner's normal path doesn't.

Tasks:
1. Reproduce end-to-end headless: save a profile into a scratch CAH dir, drive the setup screen the way the player does (no manual Hero pick), start a skirmish, assert the fortress hero page contains the created hero. Make this a failing-first runner check.
2. Fix the real cause(s). If (a): default the human seat's Hero pick to the most recent saved hero when the player has not chosen (cite retail), keep "-" selectable.
3. Assert the created hero appears in BOTH the palantir page and the world radial and is trainable (queue accepts it) — extend the existing CAH runner(s) in game\tests\ (*cah*).
4. Report with before/after numbers and the pack digest.

Gates: CAH runners, HUD runner (124/0), lobby runner (11/0), state pin unchanged.
