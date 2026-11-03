# Ambient movement

Harlindon's flying Crow and Dove_white_in_game objects use AIUpdateInterface
and CrowLocomotor, not AnimalAIUpdate. Their idle goal reaches
Locomotor::locomotorMaintainCurrentPosition every logic frame. The previous
WINGS branch only reported S-1750 and never moved them.

RotWK 2.01 0x5E74C1 steers airborne wings around the captured hold position:
CirclingRadius chooses the radius and direction, zero uses minimum speed /
maximum turn rate, and the aim is pi - pi/8 beyond the direction toward the
hold point. It invokes 0x5E740D (the wings wrapper of 0x5E6835) at minimum
speed. The hold's height worker and final transform write must also run, so a
bird placed on terrain can climb to PreferredHeight before circling. The
hold remains valid across updates; an explicit movement order invalidates it.
No render timer or render-side random draw drives movement. Existing logic
snapshots interpolate it between the five logic updates per second.

The WINGS branch and its height/transform tail are ported from that target
body. S-081 still applies to numeric parity. S-1750 remains for the unported
height/constant-calling tail of other appearances. Perched doves have no such
flying locomotor and remain where the map placed them.

Ground AnimalAIUpdate and civilian WanderAIUpdate previously ran only the
base AI class. AmbientAIUpdate now implements their ordinary idle wandering
from Open-BFME-2's AnimalAIUpdate.cpp and WanderAIUpdate.cpp, with the donor
module-data defaults. Animal wandering preserves the original position,
percentage roll, angle/distance draw order, authored radius and nearby
SALT_LICK center; ordinary civilian wandering chooses offsets within
WanderDistance. Active orders are preserved. Origin state participates in
native saves and the simulation hash.

The port is checked against RotWK's own functions (RW 0x882058, 0x8ACAFA) and
is explicitly incomplete (S-5910): animal enemy fleeing, the special-layer
check (RW 0x881FCC) and WildBeast acquisition remain unported. Destination validation uses the existing pathfinder. WildBeast
acquisition is refused with that report rather than treated as civilian
wandering. This does not add arbitrary movement to static scenery or replace
map-scripted cinematic paths.

Validation covers clockwise/counterclockwise flight, ground-to-air ascent,
perched birds, percentage-zero animals, civilian wandering, preserving active
orders, and all 41 flying birds plus seven perched doves on pure retail
Harlindon.

Retail save regressions cover the new AI subclass declaration, snapshot writes,
loading an earlier running game and saving the same bytes again. Enabling
animal wandering consumes its donor logic RNG draws and therefore changes
subsequent skirmish outcomes compared with the previously stationary animals.

## Target update comparison

The available RotWK image's AnimalAIUpdate::update at 0x882058 is 2,916
bytes, matching the BFME2 1.06 donor at 0x87EDC9. All 870 decoded
instructions match after normalizing image addresses, source-file paths and
the runtime member-layout shift of +0xC4 (old offsets 0x3D8 through 0x3E9,
new offsets 0x49C through 0x4AD). The branch layout, idle random-draw order,
integer ranges, SALT_LICK search and destination calculations agree.

WanderAIUpdate::update at RotWK 0x8ACAFA and BFME2 0x8A9424 are 499 bytes
and 159 instructions each. Their normalized instruction sequences agree;
only image addresses and source paths differ. KindOf eligibility, idle
check, selectable flag, WildBeast branch and XY random draws agree.

This comparison supports using the matching Open-BFME-2 source to describe
these target functions. It does not establish full runtime parity: fleeing,
special-layer checks and WildBeast acquisition remain S-5910, and
the available target executable retains the disputed-provenance caveat S-001.
No binary bytes or disassembly are included here.

Idle movement consumes simulation RNG and changes state hashes. PERF-1,
Uber and affected engine reference vectors must be measured from this branch;
they must not be copied from another branch or changed to guessed values.

Validation of the review follow-up: both local target/donor instruction checks
passed. The native ambient cases passed 77 assertions. PERF-1's 400-frame
battle and 6,000-frame skirmish vectors and Uber's 40-frame vector were
remeasured from this branch's core. The Uber run agreed at 1, 4 and 16
logic threads and a repeated 4-thread run. Evendim's three AI outcome pins
were also remeasured and remain open / 4103 / 5701 (seeds 2 / 7 / 1234);
the existing exact expectations are retained, with an explanatory test note.

These measurements were taken on the PR's own branch (public main of
2026-10-20). Ported onto the later archive, the branch keeps the archive's
own pins (the PERF-1 / Uber vectors, the Evendim outcomes and the save4b
class census, all re-measured by other lanes since); the merge queue that
takes this port measures them again once.
