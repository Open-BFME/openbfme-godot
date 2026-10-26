# Coverage results and regressions

## Coverage

The clean baseline and final full no-game runs use the same GCC/gcovr branch filters; exploratory focused and temporary baseline-regression runs are not accumulated into the final counters. Generated summaries and detailed tables are not retained in this tree, so the historical percentages below cannot be recalculated from the retained files alone. Follow [the reproduction instructions](README.md#reproduction) to generate local summaries and tables with numerators and denominators, all eight files, every measured source/header, direct directories, candidate rankings, simulation findings and unmeasured files.

| Scope | Line coverage before → after | Branch coverage before → after |
|---|---:|---:|
| Overall, excluding verified retail-only peer | 58.95% → 64.05% | 39.39% → 43.77% |
| Raw, including that peer | 58.45% → 63.51% | 38.57% → 42.84% |
| Online shell | 0.00% → 52.68% | 0.00% → 31.31% |
| Simple screens | 11.13% → 49.14% | 6.21% → 29.35% |
| Skirmish | 7.02% → 70.04% | 4.74% → 45.02% |
| Create-a-Hero | 1.07% → 72.46% | 0.76% → 48.25% |
| VP6 | 10.82% → 69.39% | 12.00% → 58.33% |
| In-game HUD | 0.00% → 52.86% | 0.00% → 25.92% |
| MP3 | 5.07% → 97.87% | 4.18% → 82.20% |
| Online hub | 0.00% → 90.86% | 0.00% → 71.08% |

The selection is exactly the eight highest baseline-uncovered eligible non-simulation translation units (1,637, 1,357, 1,285, 1,016, 964, 797, 693 and 654 uncovered lines, respectively). All eight are reachable using synthetic data, despite being normally driven by installed movies/maps/UI.

## Fixed defects and regression cases

Every test below has the `coverage1:` prefix. Tests construct their data in memory, not from retail assets. Production changes are confined to the eight selected non-simulation sources.

| Source | Defect fixed | Regression case / check |
|---|---|---|
| AptOnlineShell | An unavailable page was cached as null, so a repeated attempt could alter current/resume-page state. | `page lifecycle refuses an unavailable page repeatedly without changing resume state` |
| AptOnlineHub | A tournament refresh could leave a removed selection's row index and bracket attached to another tournament. | `tournament refresh clears details when the selected tournament disappears` |
| AptCreateAHero | A deferred power click survived leaving the power page and could later mutate the hero. | `leaving power page discards a queued choice before rebuilding its cells` |
| AptSimpleScreens | Unloaded tribute rows left dynamic command/provider registrations and stale gadget references alive. | Tribute hide/unload case verifies rows and their callbacks/providers retire, then can reopen. |
| AptSkirmish | An out-of-range stale profile selection could discard the current active profile/lobby. | Stale-profile case checks both active profile and lobby are preserved. |
| AptSkirmish | Switching to a map with fewer positions could retain visible buttons from the previous map. | `a sparse map hides previously visible start spots` |
| InGameHud | Missing local player or command store rejected the spell store without a reported error. | Missing-player/store spell-store cases require an explicit error and no open store. |
| InGameHud | Keys pressed under a modal could become held world-scrolling keys after closing it. | Modal-key case verifies no latent world movement after dismissal. |
| Mp3Decoder | Long-block intensity stereo used a right-channel extent already inflated by the left channel (and potentially zero-valued count1 groups), skipping required intensity bands. | Generated nonzero intensity-stereo frame checks the channel relationship. |
| VP6Decoder | An invalid coefficient offset formed an out-of-object pointer before checking its bounds. | Synthetic coefficient-partition rejection case checks the error before decoding the partition. |
| VP6Decoder | A codec beginning with `v`/`V` was accepted even if not VP6. | Container codec case checks the three tags documented in VP6Decoder.h and rejects unrelated tags for video and alpha. |
| VP6Decoder | One to seven trailing bytes after the last complete chunk were silently ignored. | `VP6 container rejects non-chunk trailing bytes`, for all seven tail lengths. |

The online-shell, tournament-refresh and pending-power regressions were additionally run independently against the exact original production sources: each failed one case/two assertions, then passed with its fix restored. The other regression cases pass against the final implementation; their original implementations were inspected, but no separate before-fix runtime reproduction is claimed.

## Test counts

| Run | Pass | Fail | Skip | Notes |
|---|---:|---:|---:|---|
| Baseline complete no-game C++ suite | 2,444 | 1 | 1,418 | 997,601 assertions; 2 failed assertions |
| Final complete no-game C++ suite | 2,535 | 1 | 1,418 | 1,000,863 assertions; 2 failed assertions |
| Eight new sets, focused run | 91 | 0 | 0 | 3,249 assertions; 3,863 unrelated cases excluded by the filter |
| Coverage/counting Python unit tests | 15 | 0 | 0 | Re-run after final reporting changes |
| Repository PR Python subset | 1,097 | 0 | 254 | 94 additional subtests passed; 57 files, Python 3.12.13 |

The final no-game suite's raw doctest summary is **3,928 successful / 1 failed / 25 skipped**. Of those nominal successes, **1,393 are runtime skips**, including the five audited silent early returns. Thus `2535 + 1393 = 3928`, and `1393 + 25 = 1418` reconciled skips. The baseline has the same skip inventory. Its nominal successful count is 3,837. Machine-readable reconciliations are [before-counts.json](before-counts.json) and [after-counts.json](after-counts.json). The only failure is not counted as a skip or hidden with a test filter.

The five explicit MP3 golden-fixture exclusions are included in the 25 doctest skips. Other runtime skips cover absent retail installations/private save inputs, optional native comparison helpers, and disabled survey/probe diagnostics. They are not ordinary passed cases. The focused new cases contain no runtime skips.

## Unchanged simulation findings

- **Existing full-suite failure:** `dep2 libm: the same bits as the host glibc's FMA variants over a sweep of every function (what Linux computed before DEP-2)`, `test_deterministic_libm.cpp:290`. Baseline and final runs both have two failed checks, each reporting 53 NaN-payload mismatches against host glibc 2.35. The pinned deterministic hashes and special-value tests pass. No simulation/libm source, baseline pin or assertion was weakened or changed.
- **Simulation coverage gaps:** the reporting tool includes the measured manifest-source ranking in locally generated tables. The original generated ranking is not retained. These are findings only, not candidates for this change.
- **Simulation audit passes:** 1,077 compile commands, 10 link commands, 346 simulation sources and 240 classified exclusions; full libclang AST audit accepts the existing 333 pinned violations in 15 files (17 pinned files). Existing S-230/S-231/S-234 audit findings remain visible. None of the selected files is in the generated simulation source set.

## Validation and limitations

- Instrumented CMake/Ninja build succeeds; the final binary contains all 91 new cases.
- Coverage is an opt-in **OFF-by-default** CMake option in this same change. A fresh default-off configure has no coverage compile/link flags.
- Reporting tools' tests, simulation audit, staged commit gate, and whitespace checks pass.
- The **public-tree publication audit fails on both the untouched base and this branch** because this private development tree includes the pre-existing `archive/` and operational files. Its findings were compared against the base, not bypassed or fixed by deleting private repository content. This coverage PR is private-only, not a publication/sync change.
- No retail files or private assets were loaded for the new tests. Existing retail-dependent rendering/movie/backend/save comparisons remain untested here, not waived. The only verified whole-file retail-only exclusion is `engine/src/Main/openbfme_peer.cpp`; Godot-only/uncompiled code is listed as unmeasured instead of falsely assigning 0% coverage.
- The public Python subset initially failed under the VM's Python 3.10 because the repository release tools require newer regular-expression internals. It passes under Python 3.12.13; repository CI provisions Python 3.13. No unrelated compatibility shim was added.
