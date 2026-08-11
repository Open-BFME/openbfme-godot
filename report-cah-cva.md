# CAH CVA lane report

Implementation commit: `ee5df64c2d99736c929cac169bceac2a90f46bd1`

Validated content root: the requested `OPENBFME_CONTENT`, with the active host pack reported by Godot as `rotwk-men-vslice/d9a12509ba2591245c50a7109a90ef4a348f82ac9e08b4290e0aaac070238a2e`.

## Record correction

The earlier claim that `e55c339` regressed `cah_match_runner` is retracted. The wrong review run mounted a pack missing required `*_U_SKN` skins. Against the specified content root the lane remains `72 passed / 0 failed`; `roster_document` was not changed to address that disproved regression.

## Review fixes

- Award accounting is session-mode gated. Solo/skirmish credits `HERO_VICTORY_COUNT_SKIRMISH` or `HERO_DEFEAT_COUNT_SKIRMISH` and never credits `MP_CREATE_A_HEROES_KILLED` or `MP_KEEPS_DESTROYED`. A real lockstep host/join session carries `session_mode=openplay-mp`, credits `HERO_VICTORY_COUNT_OPENPLAY_MP` or `HERO_DEFEAT_COUNT_OPENPLAY_MP`, and enables the two MP-prefixed kill counters. IDs were checked verbatim against pure-retail `cache/effective-assets/data/ini/awardsystem.ini`.
- The compiled descriptor now states the voice limitations: three conditional bow/mounted `SoundUpgrade` variants are uncompiled; the dropped keys are `VoiceAttackCharge`, `VoiceAttackAir`, `VoiceAttackMachine`, `VoiceMoveToCamp`, `VoiceMoveWhileAttacking`, `VoiceRetreatToCastle`, `VoiceGarrison`, `VoiceEnterUnit*`, `VoiceInitiateCaptureBuilding`, `VoicePriority`, and `SoundImpact`; no `registration.audioBindings` are projected, so CAH heroes remain silent pending the later binding lane.
- Missing `data/house-color.json` or a zero-surface recolor now produces a visible preview caption and a `color-unavailable` garment-status suffix. The descriptor also states: hero colors are preview-only; the battlefield still uses team house color.
- Color dragging recolors private shader materials in place and no longer clears `_loaded_model_id` or reloads the GLB on every `color_changed` emission.
- Profile loading restores typed RGB bytes and tracking counters after JSON decoding. Fixture tests now cover compiled defaults through create/reload, picker change through create/reload, tracking stats and owned awards through edit/re-save, per-color versus team cache isolation, and authored read-only award rows.
- Retail authors 72 award references but 71 definitions because `DROGOTHS_KILLED` occurs twice. The compiler intentionally preserves that duplicate source list. The awards tab now deduplicates by award ID for display, so the duplicate is rendered once.

## Dormancy and remaining integration

The active `d9a12509...` pack's `data/cah/system.json` has no subclass `defaultColors`, no subclass `voice`, and no `registration.awardDefinitions`; it also has no `data/house-color.json`. Therefore the new compiled-field consumers remain dormant in mounted-pack end-to-end runs. This change closes the game-side paths with synthetic system/profile/house-color fixtures and does not rebuild or mutate any sealed pack. End-to-end exercise awaits the orchestrator's later batched, attested pack rebuild.

The compiler still does not project audio bindings, so even the rebuilt voice-route fields will not make heroes audible until the binding lane lands. Battlefield profile-color consumption is likewise still absent by design and explicitly reported as a gap.

## Red-first evidence

Unique `%TEMP%` logs from 2026-08-10:

- `cah-cva-red-compiler-20260810-222837-756.log`: 1 failed (`audioBindings` limitation absent).
- `cah-cva-red-awards-20260810-222837-756.log`: 8 passed / 4 failed (both wrong-direction mode gates exposed).
- `cah-cva-red-create-20260810-222837-756.log`: 236 passed / 7 failed (typed persistence, edit preservation, picker round trip, reload-on-drag, silent degradation, and duplicate rendering exposed). The cache-isolation fixture already passed and now pins the existing correct key behavior.

## Final verification

All commands used the requested Godot executable, `OPENBFME_CONTENT`, pinned Python, `PYTHONPATH`, and ProfileSandbox. No real profile store or sealed pack was modified.

- `cah_create_a_hero_runner.gd`: `245 passed / 0 failed`
- `cah_match_runner.gd`: `72 passed / 0 failed`
- `cah_awards_runner.gd`: `12 passed / 0 failed`
- `retail_state_pin_runner.gd`: 3,000 ticks, `a436bb5989a026ee0be6674ac514c1035784dbe6fc92281ddfbb78cc79e0a05a` (unchanged)
- Pinned importer, `pytest importer/tests -q -k cah`: `148 passed, 2687 deselected`

The CAH creation runner still emits pre-existing headless diagnostics for absent optional texture directories and dummy-renderer shutdown RID leaks after its successful result line. They did not change the runner result or exit code, but they remain diagnostic noise rather than being represented as clean engine shutdown.
