# Retail production doorway routing

Production exits use the RotWK quick-path branch in both Faithful and Uber.
There is no producer-footprint exemption or special line-passability rule.
Docking, harvesting and ordinary movement retain their normal path checks.

Observed directly in the inspected community-modified game.dat with LLVM objdump
(SHA-256 `5481de75b63e22483660ea8097478faef594f90b9ade7de893e092256ba68a26`):

Source caveat (PLAN.md rule 9 / S-001): the originally inspected image is
community-modified, with added `stxt*`, `.mackt` and `.danetta` sections and
active calls from 0x5D8A64 and 0x5D8AF1 into `.danetta`. The fingerprint
identifies the inspected image; it does not establish a clean RotWK 2.01
oracle or an independently verified inventory of patches and every function
that reaches added code. All address findings below carry this caveat until
one of those requirements is met. Work touching patched areas must stop.
BFME2 donor agreement and the passing OpenBFME tests do not remove this
source qualification or independently establish retail parity.

- RW 0x666036..0x66605C tests state 7 and AI byte +0x3BA.
- RW 0x666065 calls computeQuickPath (0x665A83) without a line test.
- On success, RW 0x666079 clears the flag and 0x66607C calls 0x6654AD
  with CMD_FROM_AI. That forwards through 0x6636D9 to the machine goal
  setter at 0x8DB810. It does not issue a new move order.
- AIFollowPathState entry (RW 0x74F88C) tests state 7 at 0x74F8E6 and
  sets +0x3BA at 0x74F8EE. Update at 0x74FA18 publishes its segment goal
  at 0x74FA28 before calling the internal move update. The port arms the
  flag only at entry, rather than rearming each waypoint.
- The earlier review address 0x74BB56 also writes this byte, but belongs to
  AIFollowWaypointPathExactState update, not production entry: vtable
  0xC29068, name getter 0x744698 -> string 0xC290B0. Production uses the
  AIFollowPathState table 0xC29228, name getter 0x744777 -> 0xC29270.

The matching BFME2 `AIUpdateInterface_computePath.cpp` consumes the same
flag and updates the goal after success. `AIFollowPathStateMethods.cpp`
arms it in onEnter, and update publishes the active segment goal each tick.
The port mirrors that goal bookkeeping for every follow path, with no final
facing (FLT_MAX), so an old waypoint or angle cannot remain the machine goal.
Every follow-path exit clears the path-through flag and ignored obstacle, as
RW 0x74B79E / 0x74B7A5 do; the source caveat above applies.

The previous Uber-only approach and broad allied-goal line exemption have
been removed. A blocked first leg is intentionally not rejected by the
ordinary pathfinder: retail constructs a two-node straight path. Later
searches use normal rules after the flag is consumed.

Tests cover unconditional first paths through a blocked line, one-shot
consumption, rotated factories and rallies in both profiles, cancellation,
and the original production-clearing behavior. S-166 retains its unrelated
movement gaps; it no longer reports the quick-path branch as unported.

## Regression-scene changes

The older S-166 withdrawal notes mixed retail code evidence with measurements
of OpenBFME scenes. Those measurements are not retail capture oracles.
With the quick path enabled, the map's slaved goblins leave their lair earlier,
so the flying hunt scene can select one before its previously assumed lair.
The hunt fixture now supplies two controlled enemy structures and stops the
lair spawning; it still requires the nearer lair and exactly 837.5 damage.
Drogoth's incidental q28 auto-attack in the hero scene changes from one shot,
975 health and 13 kills to zero; this engine-scene expectation is updated
explicitly. Ordered hero combat, stance guard bounds and the AI-power timing
checks retain their existing expectations.

## Integration regression fixes (2026-10-29)

A stationary transport can have no requested destination and its rider no
machine goal. The resulting exit path consists entirely of zero-initialized
waypoints. Following the maintainer's #133 recommendation, OpenContain keeps
the ordinary search for this absent lane: after entering state 7, it clears
the straight-leg flag only if the ship request, rider goal and computed exit
waypoint are all zero in all three coordinates. Production exits and nonzero
ship/rider destinations retain the unconditional first leg. This is an explicit
OpenBFME boundary policy, reported and registered under S-4160; it does not
claim an additional RotWK binary finding or add save fields. The stationary
transport regression retains its < 5 member-slot bound and now reports zero.

The Minas Tirith siege failure was a fixture dependency on map creeps. The
extra contact was a scattered GondorTrebuchetRockProjectile hitting the side
gatehouse MinisGate1 at logic frame 165, not the Gate target. Faster creep
exits change guard RNG draws and subsequent weapon scatter. Two idle human
slots alone did not isolate the weapon/structure pair: hostile lairs still
spawned units and consumed logic RNG.

The gate fixture now removes objects hostile to both players before its first
logic frame, including their spawners. The contact bound remains zero; the
original Minas Tirith death frames remain 123 (catapult) and 158 (trebuchet).
Only the first-hit engine pins change to 21 for both weapons in this isolated
fixture. The gate suite still checks each run twice for determinism and keeps
its positive side-tower/arch contact cases. No collision rule, weapon data,
scatter rule or gameplay pin outside this fixture is changed.
