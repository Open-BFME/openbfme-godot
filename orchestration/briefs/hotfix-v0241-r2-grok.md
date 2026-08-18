# HOTFIX v0.2.4.1 — round 2: apply the code review, then ship (Grok)

Repo: C:\Users\Jonathan\Desktop\open-bfme. Continue Q31. The round-1 lane was
STOPPED before publishing; its edits are UNCOMMITTED in the working tree
(game/src/retail_slice/retail_structure.gd, game/src/content/content_db.gd,
game/tests/playable_structure_runtime_consumer_runner.gd, NEW
game/tests/slice_start_roster_presentation_runner.gd, tools/gate-m2-focused.ps1,
orchestration/queue.md, orchestration/briefs/play-smoke-harness.md). Keep what
is right, fix what the review found. READ FIRST:
orchestration/reports/code-review-34e6cfa-hotfix.md (the independent review —
binding), then orchestration/briefs/hotfix-v0241-lifecycle-construction-grok.md
(round-1 brief; the review supersedes it where they conflict). Standard rules
(explicit-path git, no sweeps, sequential Godot, detached publish, logs under
workspace/logs/hotfix-0241-r2-*.txt). No pack builds, no selection change.

## Corrections the review requires (all seven; each is a DoD item)
1. **Separate flag, not a widened `construction_omitted`.** In
   retail_structure.gd `validate_lifecycle_contract`: restore the ORIGINAL
   `construction_omitted` semantics (composed-only, status-marker) so the
   phase-list computation at ~:702 (`composed and construction_omitted`) is
   byte-identical to before. Introduce a NEW local, e.g.
   `has_construction_facts := typeof(facts.get("construction")) == TYPE_DICTIONARY
   and not (facts["construction"] as Dictionary).has("status")`, and run the
   `_valid_construction_facts` + phase-0 cross-check ONLY when
   `has_construction_facts`. Never index `facts["construction"]` unguarded.
2. **Revert content_db.gd to HEAD** (`git checkout` is banned — re-edit it back
   by hand to the exact HEAD text; `git diff -- game/src/content/content_db.gd`
   must be empty). The review replayed its strict branch over all 235
   registration docs: 0 rejected. It is not implicated; do not loosen it.
3. **Failing-first evidence is the stderr count.** In debug Godot the bug
   fails OPEN (function aborts → empty error) — return-value assertions cannot
   fail pre-fix. The new runner must capture stderr and assert ZERO
   `Invalid access`/`SCRIPT ERROR` lines, AND assert the slice is actually
   ready (`ready_ok`/equivalent) AND that the live selection mounted (men
   `a0fde4ac` in the log). Record the pre-fix count (round 1 measured 11 in
   workspace/logs/q14fin-retail_slice_runner.err at v0.2.4; your own before-run
   printed passed=6 failed=4 + the SCRIPT ERROR) → post-fix 0.
4. **Two genuinely different surfaces.** `retail_slice_ids.gd:36` default
   MAP_ID is already bfme2.map.fords-of-isen-ii, so round-1's (a) and (b) were
   the same run. Surface (a) must be a ROTWK Men skirmish start (find how the
   menu launches a rotwk skirmish — map + host faction env/args) and (b) the
   BFME2 Fords scenario. Both PASS post-fix; both show 0 forbidden lines.
5. **Fixture shape**: `_legacy_objects_json_lifecycle` must `erase("construction")`
   (the shipped key is ABSENT, not null — see the review); better still, add a
   check that validates the REAL doc:
   bfme2-men-vslice/7de517bf…/data/objects.json `bfme2.object.men-fortress`.
6. **Add the mirror strict case**: MANUAL facts vs phase-0 `mode:"none"` must
   still FAIL ("disagree") — proves 34e6cfa's intent survives.
7. **Print the real men-fortress contract result post-fix** in the runner
   output (bib/routes/rebuild-hole checks after :730 run on those docs for the
   first time since 34e6cfa) — if anything after :730 now fails on the legacy
   doc, report it; do NOT paper over it.

## Then ship (unchanged from round 1)
Checkpoint (sequential; new runner both surfaces; spellbook 218/0; member
combat 115/0; projectile table 4/0; fortress men+angmar same known fails;
retail_slice_runner failure NAMES vs workspace/logs/q14fin-* no new AND its
.err must show 0 `Invalid access` (was 11); lockstep 5/0; state pin
0e4bcdbf unchanged; boot 44/0). VERSION 0.2.4.1; Write-BuildInfo; project.godot
+ boot.tscn label; patch notes docs\patch-notes\v0.2.4.1.md (alpha hotfix
wording from round-1 brief; add: "why our gates missed it: debug builds fail
open on this class of error; the release build fails closed — the new
slice-start runner asserts zero script errors so this cannot recur");
Test-DistPipeline PASS; detached Publish-DistBuild -Zip → dist\v0.2.4.1\; prove
the SHIPPED export starts a rotwk skirmish AND the Fords scenario headless via
the new runner pointed at the export's content-packs; 0 Invalid access; 0
RETAIL SLICE UNAVAILABLE. Close Q29 (subsumed) + Q31; file Q32 (legacy
objects.json men docs authored without construction facts / the :2647
shadowing of the good composed menfortress.json — content-side fix on next
BFME2 men recook). Commits `fix(game):` / `test(game):` / `chore(release):
v0.2.4.1` / `docs(patch-notes):`. Report orchestration/reports/hotfix-v0241.md
with the seven review items each marked DONE with evidence.
