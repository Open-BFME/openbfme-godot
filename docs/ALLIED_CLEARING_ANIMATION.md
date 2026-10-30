# Allied clearing animation

Units asked to clear an exit use `AI_MOVE_OUT_OF_THE_WAY` (26), the same
state used when allies clear an ordinary movement path. Its installed path
already moved the unit, but the port omitted the internal move state's
animation-condition update and cleanup. The state now shares that condition
update with normal movement and clears MOVING, BACKING_UP, CLIMBING and
RAPPELLING on exit, including interruption.

Evidence: RotWK state 26's update at 0x749497 delegates to the internal move
update at 0x748E46 (target caveat S-001); matching BFME2
`AIStatesBfmeMoveOnEnters.cpp` also delegates to the internal move state.
This restores the missing animation bookkeeping without changing clearing
paths, speeds or destinations. A synthetic movement regression checks animated
displacement, arrival and interruption without requiring game assets.
