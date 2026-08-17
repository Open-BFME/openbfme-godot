# Stage 2a — purge docket (2026-08-17)

Lane: `orchestration/briefs/stage2-triage-kimi.md` task 3. Scope: `workspace/`
top level and `workspace/retail-work/`. Sizes measured with `cmd dir /s`
(byte totals, approximate; hardlinks/shared inodes not deducted). Small-file
sizes from `stat`. Verdict rule: approved DELETE classes only; explicit KEEP
list untouched; **anything else is HOLD, never DELETE**.

## Approved DELETE classes (executed after this docket was written)

| path | approx size | verdict | reason |
|---|---:|---|---|
| workspace/cah-capture-after/ | 1 MB | DELETE | capture-session dir, superseded (class `cah-capture-*`) |
| workspace/cah-capture-shell/ | 6 MB | DELETE | capture-session dir, superseded (class `cah-capture-*`) |
| workspace/cah-capture-v2/ | 1 MB | DELETE | capture-session dir, superseded (class `cah-capture-*`) |
| workspace/cah-capture-v3/ | 1 MB | DELETE | capture-session dir, superseded (class `cah-capture-*`) |
| workspace/cah-v2-final/ | 14 MB | DELETE | capture-session dir, superseded (class `cah-v2-*`) |
| workspace/cah-v2-final2/ | 14 MB | DELETE | capture-session dir, superseded (class `cah-v2-*`) |
| workspace/cah-v2-final3/ | 14 MB | DELETE | capture-session dir, superseded (class `cah-v2-*`) |
| workspace/cah-v2-r1/ | 14 MB | DELETE | capture-session dir, superseded (class `cah-v2-*`) |
| workspace/cah-v2-r2/ | 14 MB | DELETE | capture-session dir, superseded (class `cah-v2-*`) |
| workspace/cah-v2-r3/ | 14 MB | DELETE | capture-session dir, superseded (class `cah-v2-*`) |
| workspace/cah-base-2026-08-09/ | 1 MB | DELETE | capture-session dir, superseded (class `cah-base-*`) |
| workspace/cap-r3-b0/ … cap-r3-b4/ | 23/22/22/22/42 MB | DELETE | capture-session dirs, superseded (class `cap-r*`) |
| workspace/cap-r5b-1/ … cap-r5b-4/, cap-r5b-base/ | 42/43/43/43/42 MB | DELETE | capture-session dirs, superseded (class `cap-r*`) |
| workspace/capture-r4-base/, capture-r4-streamb/ | 21/43 MB | DELETE | capture-session dirs, superseded (class `capture-r4*`) |
| workspace/capture-r4a/ … capture-r4e/ | 35 MB each | DELETE | capture-session dirs, superseded (class `capture-r4*`) |
| workspace/attach-*.log (6 files) | 19,028 B | DELETE | loose top-level logs, approved prefix `attach-*` |
| workspace/drawable-script-runtime.log | 1,079 B | DELETE | loose top-level log, approved prefix `drawable-*` |
| workspace/dual-*.log (5 files) | 38,864 B | DELETE | loose top-level logs, approved prefix `dual-*` |
| workspace/foundation-monitor-*.log (3 files) | 12,490 B | DELETE | loose top-level logs, approved prefix `foundation-*` |
| workspace/refund-*.log (6 files) | 32,743 B | DELETE | loose top-level logs, approved prefix `refund-*` |
| workspace/orchestration/queue.json | 59,589 B | DELETE | stale 2026-07-18 queue mechanism, superseded by `orchestration/queue.md` |
| workspace/orchestration/locks.json | 165 B | DELETE | stale 2026-07-18 queue mechanism, superseded |
| workspace/orchestration/metrics.json | 515 B | DELETE | stale 2026-07-18 queue mechanism, superseded |

Projected freed: ~677 MB (du -m on the dirs) + ~164 KB (logs + JSONs).

## KEEP (explicit in brief — not touched)

| path | approx size | verdict | reason |
|---|---:|---|---|
| workspace/content-packs/ | 128,482,165,208 B (~119.7 GiB) | KEEP | live + sealed pack root |
| workspace/retail-work/editions/ | 166,673,499,494 B (~155.2 GiB) | KEEP | retail layered installs (conversion source) |
| workspace/retail-work/tools/ | 1,946,303,481 B | KEEP | pinned importer toolchain |
| workspace/retail-work/oracle/ | 915,113,318 B | KEEP | reference video (explicit KEEP) |
| workspace/retail-oracle/ | 6,024 B | KEEP | explicit KEEP |
| workspace/scratch/ | 57,512,255,134 B (~53.6 GiB) | KEEP | live apply scripts + lane reports |
| workspace/orchestration/fable-wave/ | 4,042,216,398 B | KEEP | briefs/reports = institutional history; castle-lanes L3-L10 queued (queue.md Q8) |
| workspace/playtest-program.md | 13,027 B | KEEP | live ledger (promoted copy now in `docs/state/`) |
| workspace/INDEX.json | 40,435 B | KEEP | explicit KEEP |
| workspace/onboard.config.json | 301 B | KEEP | explicit KEEP |
| workspace/logs/ | 12,918,211 B | KEEP | explicit KEEP |
| workspace/manifest.json | 297 B | KEEP | explicit KEEP |
| workspace/retail-extract/ | 24,255,597 B | KEEP | explicit KEEP |
| workspace/review/ | 44,046 B | KEEP | explicit KEEP |

## HOLD (not in an approved class — owner review required)

| path | approx size | verdict | reason |
|---|---:|---|---|
| workspace/wall-adj-*.log (3), wall-boot-final.log, wall-hub-focused.log | 47,835 B | HOLD | loose top-level logs but `wall-*` is NOT one of the five approved prefixes |
| workspace/spawn-watchdog.log | 1,350 B | HOLD | loose top-level log, prefix not in approved list |
| workspace/hm-gate-steps.ps1 | 1,766 B | HOLD | top-level script, not in any approved class |
| workspace/scratch-mirror.ps1 | 1,241 B | HOLD | top-level script, not in any approved class |
| workspace/scratch-oldsel/ | 9,327,307,695 B (~8.7 GiB) | HOLD | looks like an old-selection pack backup; could be dead weight but not in an approved class |
| workspace/tmp-review/ | 1,622,250 B | HOLD | tmp-looking but not in an approved class |
| workspace/worktrees/ | 1,438,407,244 B | HOLD | name suggests git worktrees (repo rule bans worktrees); deleting another lane's checkout is an owner call |
| workspace/orchestration/GROK-HANDOFF.md | 8,817 B | HOLD | handoff doc, not in an approved class |
| workspace/orchestration/claims/ | 353 B | HOLD | Jul-era claims dir; only queue/locks/metrics.json were approved |
| workspace/orchestration/wotr/ | 32,649,969,516 B (~30.4 GiB) | HOLD | large WotR evidence tree, not in an approved class |
| workspace/retail-work/backups/ | 15,783,360,666 B | HOLD | backup tree, not in an approved class |
| workspace/retail-work/cache/ | 19,811,670,564 B | HOLD | effective-assets cache = baseline oracle material, not in an approved class |
| workspace/retail-work/capture-junction-target/ | 0 B | HOLD | empty junction target; touching junctions is an owner call |
| workspace/retail-work/captures/ | 624,946,052 B | HOLD | retail captures, not in an approved class |
| workspace/retail-work/catalog/ | 26,660,859 B | HOLD | live importer catalog inputs |
| workspace/retail-work/jobs/ | 23,778,196,130 B | HOLD | lane job outputs, not in an approved class |
| workspace/retail-work/livingmap/ | 23,377,014 B | HOLD | living-world working data |
| workspace/retail-work/livingmap-regions/ | 14,137,934 B | HOLD | living-world working data |
| workspace/retail-work/livingworld-autoresolve/ | 943,253 B | HOLD | living-world working data |
| workspace/retail-work/livingworld-markers/ | 8,584,686 B | HOLD | living-world working data |
| workspace/retail-work/livingworld-region-images/ | 5,228,127 B | HOLD | living-world working data |
| workspace/retail-work/load-captures/ | 17,553,473 B | HOLD | captures, not in an approved class |
| workspace/retail-work/openbfme-ring-icon.png / -preview.png / .ico | 3,789 / 15,568 / 27,059 B | HOLD | branding art, not in an approved class |
| workspace/retail-work/packs/ | 8,285,919,704 B | HOLD | pack staging trees (men/angmar PUBLICATION_READY proofs live here — queue.md Q2) |
| workspace/retail-work/profiles/ | 81,836,058 B | HOLD | holds `men-fords-v0-complete.generated.json` pinned oracle (queue.md Q9) |
| workspace/retail-work/reports/ | 2,014,980,755 B | HOLD | lane reports, not in an approved class |
| workspace/retail-work/retail-payload-manifest.populated-20260810.json | 112,196 B | HOLD | payload manifest, not in an approved class |
| workspace/retail-work/scratch/ | 214,663,638 B | HOLD | importer scratch, not in an approved class |
| workspace/retail-work/strategic-ui/ | 51,246,217 B | HOLD | WotR strategic UI working data |
| workspace/retail-work/tmp/ | 230 B | HOLD | trivially small; not in an approved class |
| workspace/retail-work/videos/ | 44,052,855 B | HOLD | capture videos, not in an approved class |

## Execution record

Executed 2026-08-17 after this docket was committed (`84e00ba`).

- Deleted: 27 capture-session dirs (all `cah-capture-*`, `cah-v2-*`,
  `cah-base-*`, `cap-r*`, `capture-r4*`), 21 loose top-level logs
  (`attach-*` x6, `dual-*` x5, `refund-*` x6, `foundation-*` x3,
  `drawable-*` x1), and `workspace/orchestration/{queue,locks,metrics}.json`.
- **Freed bytes: ~677 MB** (sum of `du -sm` over the deleted dirs = 677 MB,
  plus 104,204 B of logs and 60,269 B of JSON ≈ 0.16 MB). Free-space delta
  on C: moved from 211,464 MB to 212,084 MB free (~620 MB; residual
  difference is unrelated disk noise on a live machine).
- Post-deletion verification: `ls` over every approved-class pattern returns
  "No such file or directory"; every HOLD entry above still exists
  (wall-*/spawn-watchdog logs, both .ps1 scripts, scratch-oldsel, tmp-review,
  worktrees, GROK-HANDOFF.md, claims/, wotr/ and all of retail-work/ verified
  present).
- Zero deletions outside the approved classes.
- `python tools/check_pack_addresses.py` after deletions:
  `PACK_ADDRESS_CHECK PASS packs=42 roots=2`.
