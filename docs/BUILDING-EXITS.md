# Battalion building exits

Produced soldiers follow a shared exit lane before joining their formation.
The horde formation pass preserves an active production-exit order instead of
replacing it with a busy/formation order while the soldier is still leaving.
After the path finishes, normal formation steering resumes. A new player order
can still interrupt production-exit movement through the usual horde command path.

Source facts:

- BFME1 `QueueProductionExitUpdateExitObjectViaDoor.cpp`, RVA `0x002D0F00`,
  sends each produced member to the natural rally point. BFME1 now uses this
  single waypoint rather than BFME2's member-slot path.
- BFME2 `QueueProductionExitUpdateExitObjectViaDoor.cpp` uses a shared halfway
  point between creation and natural rally positions, followed by the member's
  formation position twice. That path remains in BFME2/RotWK.
- BFME2 `AIUpdateInterface_computePath.cpp` uses `computeQuickPath` for state 7
  with the path-through-units flag, allowing the initial exit through the producer
  rather than searching around its footprint. The port enables this two-node
  path for battalion members only.
- BFME1 `HordeMemberOrder00245420.cpp` preserves an explicit path before steering
  a member toward its slot. Port adaptation: our production path has goal type
  `AIGOAL_ON_PATH`, so the guard explicitly preserves production-exit state 7,
  including the interval before its pathfinding request runs.

Remaining limits: this does not enable the general exit-state flag/goal overwrite
for heroes and other individual units (S-166), nor claim independent retail timing
or animation parity. The existing producer-footprint inference for later formation
walks remains S-224.

`horde production exit:*` in `test_move_horde.cpp` checks the shared lane for all
15 soldiers in both data modes, BFME1/BFME2 waypoint counts, and re-formation after
leaving. The handoff regression fails before the fix by switching to busy state 42
and moving the soldier sideways.
