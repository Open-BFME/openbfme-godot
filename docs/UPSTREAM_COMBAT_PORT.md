# Upstream combat/navigation port

This branch ports the ground-structure obstacle-ring melee reach test and OCL
structure footprint registration from the fork combat/navigation PR. Upstream's
siege-tower portal handling is preserved. Source anchors remain beside the code.

The fork's peace-time exemption is intentionally absent: upstream has no peace-time
feature to amend. This PR adds no separate lair/rubble runtime; current main owns
RebuildHoleExposeDie and RebuildHoleBehavior. The earlier PR base still reported
that runtime as unported (S-1427), which is why the fork-only correction was not
included here.

The Harlindon regression here covers the lair and its defenders, without asserting
peace-time or fork-specific rubble behavior. Native tests cover obstacle-ring
reach and OCL placement, secondary rotation and deferred footprint cleanup.

## Integration with current main

Merged main `0669a3a013` and reran the reported seven-faction Medium-AI Evendim
check using installed BFME2/RotWK data, default RotWK rules and seed 1234.
All seven attacks arrived within 2400 frames. Isengard launched at 1306 and arrived
at 1467; Mordor launched at 1306 and arrived at 1497. These are observations from
this merged engine, not retail timing goldens. The earlier batch's crossbow-horde
stall did not reproduce on this tree; its original cause is not established.

The same run passed the Mordor two-run frame-hash check, Aragorn-versus-creep
checks, structure obstacle-ring reach, and OCL placement/secondary-facing checks:
14 cases, 341 assertions. OrientInSecondaryDirection still only changes the
orientation: the initial footprint is intentionally retained, as RW 0x5F293A does.
