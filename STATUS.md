Owner: Integration owner
Owns: Volatile repository state, selected private-pack identity, active blockers, and latest verified gate results.
Does not own: Product scope, architecture, milestone definitions, or historical evidence.
Last verified commit: `27f738021c36a3e3d015bb6ef87da46204184103`
Update trigger: Selection, worktree state, blocker, capability, or gate result changes.
Validation: Compare this file with the current commit, private selection and oracle metadata, and the named focused reports.

# OpenBFME status

## Current identity

- Active target: BFME2 1.06 Men-versus-Men on Fords of Isen II.
- Last clean product/oracle baseline before this status-only update: `27f738021c36a3e3d015bb6ef87da46204184103`; clean dirty-state digest: `3d0d02f93695a6b4b6e73bb892cf153c90e5c48ef25b25ac64aa22b7144ebecb`. The still-empty oracle workspace must be rebound after this edit is committed before capture begins.
- Selected private pack: `bfme2-men-vslice/69fd5efe0dfd77a9475250a102c52691044dff0b7d8216b873d725dd22de4cc1`.
- Strict completion profile SHA-256: `0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d`.
- The last clean baseline's pending oracle workspace contains exactly 47 ordered rows, with 0 paired and 0 approved. It will be rebound after this status-only commit. Frozen thresholds remain 60 average FPS, 15 one-percent-low FPS, 1 GiB peak memory, 128 MiB total growth, and 16 MiB final-five-minute growth.

## Verified now

- The selected private retail slice loads fail-closed and the current `run_retail_slice.bat --test` result is `245 passed, 0 failed`, signature `AC8D8C4D`.
- Men/Fords terrain, roads, water, navigation, four unit types, five structures per side, construction, production, combat, victory, defeat, HUD, and audio contracts exercised by that focused runner are green.
- Commit `ba251d71479abb952ac87fa9f5d5fc428f03126a` added a separate three-match lifecycle proof. It reproduces the full enemy construction -> production -> Fortress hit -> destruction -> defeat chain three times in one process, destroys all three scenes, clears the mesh cache, and reproduces lifecycle signature `932C2486`.
- The lifecycle packet's exact five-stage focused reliability artifact has SHA-256 `7f7cea814ba353af0ef9f37bc3d44ff8401acf3b930981cbfdeca64dd0ac45d5`; two final adversarial reviews reported no P0/P1 finding.
- Commit `7e7efffaed0bc6c0d7ed15436ae65c6960dcbe59` preallocates exact frame and memory evidence buffers before the live-soak baseline. The implementation packet's 300.004861-second dirty-worktree diagnostic retained 31,304 recomputable frames, averaged 104.345 FPS, recorded a 36.412 one-percent low, and had zero multi-second stalls or forbidden diagnostics. Its changes were subsequently committed as `7e7efff`; because the artifact itself records the preceding `ba251d7` revision plus a dirty-state digest, it is diagnostic implementation evidence rather than clean current-identity reliability evidence.
- Private retail and converted payloads remain below `.private`; the public export boundary remains mandatory.

## Not complete

- M2 is not complete. No retail/Godot capture pair is populated or human-approved; all 47 audiovisual oracle rows remain open and will be rebound before capture.
- Reliability is not complete. The current-identity 300-second diagnostic retained 10,119,014 bytes across its late window with a +29,439.71-byte/second linear trend. It remains diagnostic rather than accepted reliability evidence: two adversarial reviewers found that it did not demonstrate an improvement over the prior curve, exercised zero match completions, and did not retain wrapper stdout/stderr.
- The rejected 1,800-second `3dd886a` artifact remains archived as failure evidence. Its wrapper-level pass must not be finalized or reused.
- The last clean product identity has no accepted 1,800-second reliability artifact. The authoritative run is deferred until oracle/content identity freezes, then judged against the already-frozen thresholds. A correction packet is warranted only if that run violates them.
- The generated BFME2-wide feature graph, evidence catalog, and coverage matrix are not implemented; no whole-game completeness claim is valid.
- C# simulation, 30 Hz production ticks, eight-player lockstep, replays/checkpoints, self-hosted networking, campaigns, War of the Ring, Create-a-Hero, naval play, Ring mechanics, and later content milestones remain roadmap work after the current vertical slice is frozen.

## Next bounded work

1. Populate and human-review all 47 retail/Godot oracle pairs for the current identity, beginning with a bounded deterministic capture-scenario tranche backed by retained BFME2 1.06 evidence.
2. Correct only reproducible severity-0 or severity-1 audiovisual differences exposed by those comparisons; every implementation packet receives adversarial review.
3. Freeze the approved content identity.
4. Run the 1,800-second authoritative reliability packet once against that frozen identity and retain complete process output.
5. Run `run_m2_acceptance.bat -IntegrationOwnerPublish` and tag the M2 baseline before Phase C cleanup or any M3 expansion.

## Status discipline

Changing counts, hashes, timings, benchmark values, and gate outcomes belong here or in generated private reports. Architecture and product documents define how to measure them but do not copy volatile results.
