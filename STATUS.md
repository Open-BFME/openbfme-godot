Owner: Integration owner
Owns: Volatile repository state, selected private-pack identity, active blockers, and latest verified gate results.
Does not own: Product scope, architecture, milestone definitions, or historical evidence.
Last audited base commit: `ad370cc9b02bdec600564cf1c606e70833faa97a`
Audit date: 2026-07-22
Update trigger: Selection, worktree state, blocker, capability, or gate result changes.
Validation: Compare this file with the current commit, private selection and oracle metadata, and the named focused runners.

# OpenBFME status

## Evidence boundary

This is an audit of a large, dirty development working tree based on
`ad370cc9`. Kimi's UI rewrite is actively changing covered files, so there is no
stable current runtime identity. Results below are explicitly diagnostic and
must not approve a milestone, release, or parity claim.

The product target is the full game. BFME2 1.06 skirmish compatibility is the
active acceptance milestone, not the scope; campaigns and War of the Ring are
ladder steps 6 and 7 in DIRECTION.md.

## Audited identity

- Base commit: `ad370cc9b02bdec600564cf1c606e70833faa97a`.
- Active private pack recorded on 2026-07-22:
  `bfme2-men-vslice/91f1b104bdc70c72c026b2577e98bb75411f5747cf4cbc8c64d91fc933bcd6cf`.
- Selection SHA-256:
  `6BA7B1CB8073DE9A0448228CEA720C4C0F78139B9AEE80C65DA82C8957D00FF2`.
- Godot: `4.7.stable.official.5b4e0cb0f`, executable SHA-256
  `D8055FB8C7E7F5010D7439EC69BE051554055DAE55A265F8647BD7301C34161C`.
- A retained diagnostic run began with code snapshot
  `500313D6EA50A1D159B8D761259F30ADF37AB6DD25BEDBFECD4D6E766748BA20`
  and ended with
  `632B6972ECA2732D26FAE0F64BCB205705856A5A593721411C5171FB2764DA82`.
  Because those digests differ, the run is invalid as current verification.

The raw logs and machine-readable receipt remain under the ignored private job
workspace and are not public-release inputs.

## Surfaces observed in recent code

- A skirmish shell modeled on BFME2, six faction entries, five map entries,
  player colors, start positions, resources, command-point factor, and persistent
  display/audio/gameplay options.
- Document-driven faction manifests, rosters, structures, production, upgrades,
  heroes, powers, and spellbooks.
- Construction, rally points, production cancellation, combat, stances,
  formations, cavalry trample, experience, hero abilities/death/revival,
  selection, commands, control groups, AI production/attacks, and outcomes.
- Source map plumbing for Fords of Isen II, Rivendell, Mount Doom, Dagorlad, and
  Mordor, including terrain, water, navigation, minimaps, start positions, and
  fortress placement.
- Importer commands for faction census/import/conversion, individual units,
  spellbooks, five-map profiles, and publication into the private slice.

Code presence is not runtime success or parity. The active rewrite must settle
before these surfaces are reverified together.

## Unbound diagnostic results

Before the active UI rewrite moved the covered source identity, the same
2026-07-22 working session observed the following totals. Exact code and pack
digests were not retained for each row, so these are historical diagnostics,
not current verified results:

| Runner | Result | Interpretation |
|---|---:|---|
| Menu/skirmish shell with selected private packs | 74 passed, 0 failed | The tested shell represented six factions and five maps at that unbound identity |
| Men retail slice | 337 passed, 3 failed | Broad exercised coverage; armor-counter matrix, ambient idle loop, and early production timing failed |
| Elves retail slice | 289 passed, 7 failed | Substantial runtime coverage; suite is not green |
| Dwarves retail slice | 262 passed, 4 failed | Substantial runtime coverage; suite is not green |
| Isengard retail slice | 263 passed, 5 failed | Substantial runtime coverage; suite is not green |
| Mordor retail slice | 282 passed, 4 failed | Substantial runtime coverage; suite is not green |
| Goblins/Wild retail slice | 262 passed, 4 failed | Substantial runtime coverage; suite is not green |
| Retail spellbook runner | 180 passed, 3 failed | Men tree and casts were broadly exercised; War Chant setup failed for Isengard, Mordor, and Wild |
| Ranger playable surface | 3 passed, 2 failed | Scene and clips load; typed registration and level-one HUD locking fail |
| Options/pause surface | 15 passed, 0 failed | Focused assertions pass, but teardown diagnostics remain |

Every non-Men faction reproduced its own deterministic replay inside those runs,
but every pinned battle signature differed from the expected constant. These are
test failures, not values to repin without review.

During the identity-bound audit attempt, covered code changed between the start
and end digests. The Men runner reported `5 passed, 7 failed` after slice
initialization stopped at faction-roster presentation validation; the menu runner
reported `74 passed, 0 failed`. Both are retained as race diagnostics only.

## Map state

At an unbound pre-rewrite identity, all five selected maps booted through their focused source-map runner with zero
map assertions failing. Fords loaded terrain, roads, water, navigation, 1,249
bound props, and the expected starting structures. Rivendell, Mount Doom,
Dagorlad, and Mordor loaded source terrain/navigation/start structures but bound
zero props while reporting large unresolved prop counts. They are bootable
development maps, not five equally polished battlefields.

## Open blockers

- The Men suite is not green and the five other faction suites each fail.
- The ongoing UI rewrite currently prevents a stable whole-surface verification
  identity; no public “works today” claim should be promoted until it settles.
- Pinned battle signatures for all five non-Men factions have drifted.
- The spellbook and Ranger runners have real failures.
- Godot reports RID/ObjectDB/instance teardown leaks across the gameplay and map
  runners, plus animation diagnostics in some runs.
- Fords is materially ahead of the other four maps in presentation binding.
- Cross-faction skirmish launch is currently rejected; the shell launches
  same-faction matchups only.
- The statistics screen is a visible placeholder rather than tracked gameplay
  statistics.
- Final original-game visual approval and the identity-bound long-running
  reliability gate are not complete.
- The generated BFME2-wide feature graph, evidence catalog, and coverage matrix
  are not implemented.
- Deterministic production networking/dedicated servers and the planned pure C#
  simulation cutover are not complete.

Under repository policy, any required warning, error, leak, or failed assertion
makes the applicable gate a failure. A runner exiting zero does not override its
own reported failures.

## Next bounded work

1. Let the active UI rewrite reach a stable owner-approved identity, then rerun
   the menu, Men, faction, map, spellbook, Ranger, options, and teardown gates
   with retained logs and exact code/pack/tool digests.
2. Reconcile the Men armor matrix, ambient idle loop, and production-timing
   failures against source/original evidence.
3. Explain rather than blindly repin each faction signature drift, then close
   the remaining faction-specific failures.
4. Repair the spellbook and Ranger contract failures and make every runner return
   a failing process code when it reports failures.
5. Eliminate teardown leaks and animation errors on required paths.
6. Bind and validate presentation props for the four non-Fords maps.
7. Complete identity-bound human visual review and reliability evidence before
   declaring the first milestone frozen.

## Status discipline

Changing counts, hashes, timings, benchmark values, and gate outcomes belong
here or in generated private reports. Architecture and product documents define
how to measure them but do not copy volatile results.
