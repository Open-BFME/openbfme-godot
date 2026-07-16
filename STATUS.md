Owner: Integration owner
Owns: Volatile repository state, selected private-pack identity, active blockers, and latest verified gate results.
Does not own: Product scope, architecture, milestone definitions, or historical evidence.
Last verified commit: `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`
Update trigger: Selection, worktree state, blocker, capability, or gate result changes.
Validation: Compare this file with the current commit, private selection metadata, and the named focused or integration-gate reports.

# OpenBFME status

## Current state

- Active target: BFME2 1.06 Men-versus-Men on Fords of Isen II.
- Selected private content pack: `bfme2-men-vslice/69fd5efe0dfd77a9475250a102c52691044dff0b7d8216b873d725dd22de4cc1`.
- Selected profile: `0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d`.
- Commit `efe6a6c1f7ab76ae84436faed4e9a02298a4a194` contains the recent HUD,
  side-command-bar, vertical-slice, and M3-spec changes that were previously
  protected as dirty work.
- A separate, currently blocked M3 importer packet has uncommitted files under
  its recorded importer lock. Preserve that diff; do not integrate or expand it
  before the pilot and M2 freeze.
- Authoritative baseline for this status snapshot:
  `efe6a6c1f7ab76ae84436faed4e9a02298a4a194`.

## Implemented now

- A Godot retail-slice runtime exists for the active Men/Fords milestone.
- Retail extraction and conversion produce private content packs.
- Private retail and converted payloads are contained under `.private`.
- Focused importer, runtime, oracle, and pipeline checks exist; their latest results must be taken from current reports, not inferred from this summary.
- Phase A contract/gate meta-verification passed 14 focused tests. The product
  policy checker passed with product digest
  `745121aff4ad4d9313b81c00e686414c32185bb49e42f46c4ab08e9210aabb04`
  and modding digest
  `7ef975b4b7facc1df2c4f3225236c19c76b0cfea88c4d41b8bd4ea378cef8cb5`.
- The Phase A correction adversary reported no remaining P0/P1 in its scope.

## Decided, not yet complete

- C# will become the only authoritative deterministic simulation.
- Production simulation will run at 30 Hz and support at most eight players.
- Multiplayer will use server-refereed deterministic lockstep.
- Simulation and presentation packs will have separate compatibility hashes.
- Completeness will come from a generated retail feature graph joined to evidence, not handwritten totals.

## Active blockers and gates

- Men/Fords is not complete until every requirement in `docs/MILESTONE_CURRENT.md` has current evidence.
- The original-game/Godot oracle approvals and the final live soak remain integration-owner gates until their current reports show otherwise.
- The current oracle workspace targets older source/profile/bundle identities,
  has `0/47` approved capture rows and `0/47` paired retail/Godot captures, and
  is not final. It cannot contribute to the current M2 declaration.
- Strict selected-pack-only loading must fail closed before private parity can be accepted.
- `run_retail_pipeline_tests.bat` remains integration-owner-only and must not be treated as proof if it targets or publishes the wrong profile.
- The generated whole-game feature graph, evidence catalog, and coverage matrix
  are not implemented yet; the policy contract therefore blocks completeness
  claims by design.
- A blocked M3 worker violated its forbidden paths by publishing and selecting a
  `men-fords-v1` bundle. The integration owner restored the preserved M2
  selection. The M3 bundle remains immutable and retained for later review, but
  it is not selected or integrated.

## Status discipline

Record changing counts, hashes, timings, benchmark values, and gate outcomes here or in generated private reports only. Architecture and product documents may define the measurement but must not copy volatile results.
