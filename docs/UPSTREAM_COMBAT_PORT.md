# Upstream combat/navigation port

This branch ports the ground-structure obstacle-ring melee reach test and OCL
structure footprint registration from the fork combat/navigation PR. Upstream's
siege-tower portal handling is preserved. Source anchors remain beside the code.

The fork's peace-time exemption is intentionally absent: upstream has no peace-time
feature to amend. Its lair-to-rubble footprint correction depends on the fork's
partial RebuildHoleExposeDie runtime; upstream still marks that runtime unported
(S-1427). Bringing in that separate gameplay feature implicitly would broaden this
fix. The original fork PR retains it for a later dedicated runtime port.

The Harlindon regression here covers the lair and its defenders, without asserting
peace-time or rubble behavior absent from upstream. Native tests cover obstacle-ring
reach and OCL placement, secondary rotation and deferred footprint cleanup.
