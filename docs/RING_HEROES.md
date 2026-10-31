# Ring delivery and ring heroes

A ring carrier can now enter its own fortress to deliver the One Ring. The
delivery cursor uses retail Mouse.ini `DeliverRing` and its animated `OneRing`
asset. The fortress consumes the dropped-ring object, clears the carrier's ring status,
grants the configured player and fortress upgrades, emits the configured FX,
and sends the carrier back outside. A battalion delivers once, after its members
have reached the entrance. As in RotWK, the delivery is checked only when an
object enters (RW 0x883FC3..0x88403F): if the final approaching member dies
after the others entered, the battalion stays inside with the ring until it is
evacuated and enters again. Existing hero recruitment uses these upgrades to
unlock the faction's ring hero.

The missing runtime was `CitadelSlaughterHordeContain`: it previously registered
as an unported module, so the fortress could not accept the enter command.
The implementation now uses the existing garrison pathfinding, entry and exit
machinery, with the citadel's ring-specific acceptance and delivery callback.
Destroying the ring directly avoids its death behavior spawning another ring.
The pending carrier exit is included in snapshots and simulation CRCs.

## Sources and limits

Target facts: the RotWK 2.01 module registry and field tables in
`engine/data/rotwk-201/` identify the inheritance chain and parse fields at
0xC5EFE8 / 0xC5F080. Retail fortress INIs configure ring status, upgrades,
ring-object filter and delivery FX. Retail ring-hero templates already contain
the corresponding recruitment and upgrade-removal behaviors.

Behavior donor: Open-BFME-2's `CitadelSlaughterHordeContainIsValidContainerFor`
(0x48071D), status predicate (0x4807C7), delivery callback (0x480E9A), and
`SlaughterHordeContainUpdate` citadel update (0x480FE2). The donor's
`SlaughterHordeContainAddHordeMember` waits for all members before delivering
through the parent battalion. Our adaptation uses existing containment and
AI orders rather than reproducing the donor's internal container layout.
RotWK's update (RW 0x884244 -> 0x8840D4) only sends the carrier out; it never
checks for a delivery.
This is a donor port, not a verified RotWK binary-equivalent implementation.

S-6010 tracks the remaining ordinary troop slaughter / cashback /
CanAlwaysEnter branches and target parity. Ordinary troops are refused; this
change does not implement the unrelated slaughterhouse behavior.

## Validation

`ring delivery retail:*` exercises real RotWK templates and enter orders for
infantry, archer and cavalry battalions, and a hero. It checks pickup, delayed
delivery, both upgrades, one delivery FX, ring destruction, carrier survival and
exit, the Men ring-hero record, and a pending-exit snapshot roundtrip. Another
case refuses ordinary troops and an enemy ring carrier. FACT-B inventory pins
reflect that citadel containment now has its own runtime.

A regression kills the final approaching battalion member after fourteen have
entered, and checks that, as in RotWK, nothing is delivered: no FX, no
upgrades, the ring stays, and the carrier stays inside holding it.
