# Battalion building exits

Produced soldiers follow a shared exit lane before joining their formation:
the first leg of the exit path is a straight line out of the producer, after
which the soldier paths as usual and the horde's formation pass steers it to
its slot.

Source facts:

- BFME1 `QueueProductionExitUpdateExitObjectViaDoor.cpp`, RVA `0x002D0F00`,
  sends each produced member to the natural rally point. BFME1 now uses this
  single waypoint rather than BFME2's member-slot path.
- BFME2 `QueueProductionExitUpdateExitObjectViaDoor.cpp` uses a shared halfway
  point between creation and natural rally positions, followed by the member's
  formation position twice. That path remains in BFME2/RotWK.
- RotWK `AIFollowPathState::onEnter` (RW 0x74F88C) sets the AI's
  path-through-units flag (AI + 0x3BA) once, for the exit-production state
  (id 7) only (RW 0x74F8EE); the segment advance (RW 0x74FA18) never sets it.
- RotWK `computePath` (RW 0x666036 .. 0x66607C; BFME2
  `AIUpdateInterface_computePath.cpp` as the donor) takes `computeQuickPath`
  (RW 0x665A83) for state 7 with the flag. When it made a path, the flag is
  dropped (RW 0x666079) and the machine's goal becomes the destination
  (RW 0x6654AD with CMD_FROM_AI, then RW 0x8DB810: not while the machine is
  locked). The straight path is therefore one-shot: later segments and repaths
  search around buildings and cliffs. The port applies this to battalion
  members only.
- RotWK's member order (RW 0x877A7A) and moveHub (RW 0x87468B) do not look at
  the exit state: an exiting member gets formation orders like any other. The
  only "keep its own path" exemption is goal type 4 with a path
  (HordeMemberPass.cpp, `AIGOAL_EXPLICIT_WITH_PATH`).

Remaining limits: this does not enable the exit-state straight path and goal
overwrite for heroes and other individual units (S-166), nor claim independent
retail timing or animation parity. The existing producer-footprint inference for
later formation walks remains S-224.

`horde production exit:*` in `test_move_horde.cpp` checks the BFME1/BFME2
waypoint counts, that a member's two-segment exit takes one straight path and
keeps the flag clear through the segment advance, and that every member leaves
the exit state and reaches its slot; `backlog5: the two withdrawn rules ...` in
`test_pathfind_move.cpp` checks that the next path after the straight one goes
around a wall.
