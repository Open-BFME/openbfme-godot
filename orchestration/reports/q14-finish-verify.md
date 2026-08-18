# Q14 finish — v0.2.4 alpha final verification

Fresh-context verifier of the shipped `dist\v0.2.4\` build (Grok lane grok-q14-finish).
Verified the DELIVERABLE against the artifact, not the report. Read-only lane.
Release commits: `fdbb648` (re-pin), `6a537f2` (release), `b51be4d` (patch notes).

## VERDICT: ACCEPT

v0.2.4 alpha is correctly built and boots clean on the new content. Two cosmetic
notes below (in-game build label, one non-pin file in the re-pin commit); neither
blocks a playtester hand-off.

## Criterion-by-criterion

### 1. Headless boot of the shipped exe — PASS
Booted `dist\v0.2.4\OpenBFME.exe --headless --quit-after 600` with
`OPENBFME_CONTENT` = the bundle's `content-packs\` (the same path `run-with-log.bat`
sets). Log: `workspace\logs\q14fin-verify-boot.txt`. Exit code 0.
- Mounted `rotwk-men-vslice/a0fde4ac89596cab4d34beae3ebce0e33aa30323f99a73727706a29c85d315c0` (the NEW men digest).
- NO durable/user-profile fallback: 0 hits on `DURABLE`, `4f92c8a4`, `app_userdata`.
- `[ContentDB] playable: factions=7 (angmar,dwarves,elves,isengard,men,mordor,wild) units=137 structures=147 skipped_units=0`.
- 0 "no compiled armor contract" lines. 0 `ERROR`/`Parse Error`/`FATAL` lines.
- My independent boot log is byte-for-byte identical to Grok's `q14fin-artifact-run.txt` (7 lines).
- Caveat: this build prints no explicit "menu ready" marker; usable-menu is
  inferred from a clean exit-0 boot reaching the terminal ContentDB line plus
  Grok's `boot_startup_runner` (44/0). Headless cannot click a literal menu.

### 2. selection.json — PASS
- sha256 = `4e3e702486154bff0af025f56254e7d1a2c6b5a5d1a7b87a873c5e9f460cb7af` (matches expected).
- activePack = `rotwk-men-vslice/a0fde4ac...`.
- All 7 new faction digests present: men a0fde4ac, elves 99f31f9d, dwarves ba3e0d12,
  isengard fab985f0, mordor 36238eaa, wild e2ce0dd2, angmar 15720e1f.

### 3. Version stamping — PASS
- `VERSION` = `0.2.4`; `game\project.godot` config/version = `0.2.4`.
- `dist\v0.2.4\BUILD-INFO.txt`: bundle v0.2.4, commit `b51be4dc45739...`, godot 4.7.stable.
- `docs\patch-notes\v0.2.4.md` exists, player-facing (projectiles, 7-faction recook,
  Noldor Warriors restored) with a `## Known gaps` section.
- COSMETIC NOTE (already disclosed by Grok): the in-game menu still prints build 363
  / `328f2fe` (Write-BuildInfo predates the 3 release commits). BUILD-INFO.txt and
  the folder are correct; only the on-screen label lags. Not a publish refusal.

### 4. Re-pin correctness — PASS (with one benign extra file)
`git show fdbb648 --stat`: touched `tools/gate-retail.ps1` (selection sha, activePack,
and the 6 non-men faction supplement pins only), `importer/tests/test_retail_gate_script.py`
(the expected-sha string only), and `orchestration/queue.md` (the queue-claim row —
NOT a hash or pin). No other hash/pin file was modified; the brief's "no other hash/pin"
constraint holds. queue.md being in the same commit is a bookkeeping edit, not a defect.
- `pytest importer/tests/test_retail_gate_script.py` (pinned interpreter): 12 passed in 0.12s.
- Pinned `$expectedSelectionSha256` = `4e3e7024...` == live selection sha. Match.

### 5. Elves fix in shipped content — PASS
`dist\v0.2.4\content-packs\rotwk-elves-vslice\99f31f9d.../data/playable-units/noldorwarriorhorde.json`
present (932 KB, read-only/sealed).

### 6. Note-only — bundle size / batch packs
- Zip: `dist\v0.2.4.zip` = 13,860,833,623 bytes (12.91 GiB / 13.86 GB). Confirmed large.
- 79 `*-missing-physical-20260816-batch-*` packs ARE in `dist\v0.2.4\content-packs`
  (39 bfme2 batches 041–079, 40 rotwk batches 001–040).
- Their combined size = **6.24 GB**, ~39% of the 15.93 GB staged content
  (~45% of the compressed zip). Prime target for the Q1 consolidation follow-up.

## Things I could not / did not verify
- Literal on-screen main menu interaction (headless boot only; no GUI click test).
- Full `gate-retail.ps1` Section A (Grok also left this undone; not required for accept).
- The `du` on the full 16 GB tree timed out; batch-pack size measured via PowerShell
  Get-ChildItem sum instead.

## Pre-existing / disclosed (not regressions)
- 6 importer reds (Q6), Q11 armor-contract gate-step regex red on the state-pin gate
  (artifact boot emitted none of those lines), EVA/maps deferred to v0.2.5. All in the
  patch-notes Known gaps and Grok's report.
