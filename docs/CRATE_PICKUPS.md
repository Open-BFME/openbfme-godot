# Walk-over treasure pickups

`SalvageCrateCollide` and `VeterancyCrateCollide` now have runtime modules.
Stationary pickups are queried during AIWorld's partition/collision phase,
including when only one moving unit exists. Successful execution emits the
configured pickup FX, records the executed flag, and destroys the crate.
The flag participates in snapshots and state hashing. Rewards execute only
on the logic thread, independently of rendering.

## Evidence

Target facts (subject to S-001): SalvageCrateCollide's registry entries are
RW create 0x651052 / data 0x655980; its existing common and reward field
parsers are retained. Veterancy's target field table 0xC05EB8 specifies
EffectRange as unsigned, AddsOwnerVeterancy and IsPilot as booleans, and
AffectsUpToLevel as integer.

Donor facts: Open-BFME-2 matching sources under
`Code/GameEngine/Source/GameLogic/Object/Collide/CrateCollide/`:
`CrateCollideIsValidToExecute.cpp`, `SalvageCrateCollide.cpp`,
`VeterancyCrateCollide.cpp`, `CrateCollide.cpp` (the merged CrateCollideModuleData constructor),
`VeterancyCrateCollideModuleDataCtor.cpp`. Salvage draws a probability roll
even at zero or one, selects a single-unit level award first, then a player
upgrade if configured, otherwise money. The money range draws only when its
endpoints differ. AI disallowance precedes reward RNG. PorterChance,
BannerChance and ResourceChance do not participate in this donor selector;
we do not invent additional rolls. Veterancy checks trainability and the
configured current-rank limit, then grants one level. The zero-range shield
promotes its collector; the nonzero-range donor branch promotes other owned
objects within range. AddsOwnerVeterancy's donor zero-level gate refuses it.

The base consume/FX/destroy sequence is also supported by Open-BFME-1's
`CrateCollide_onCollide.cpp`. Money uses the existing economy multiplayer /
handicap multiplier and floating-income event path. Experience uses the
existing tracker and level-up feedback.

## Explicit gaps

S-5900: Contact discovery is an inference: ground bounding-circle overlaps
queried through the partition, sorted by crate and collector ID, before
AI-to-AI collision processing. Retail's collision geometry and pair order
remain unported. This scan is needed because stationary treasure is absent
from the moving-AI pair list.

S-5901: The unresolved CREEP-controller predicate and pilot transfer are
refused. The undetected-defector crate predicate is not reproduced. The
veterancy height gate currently requires ground contact rather than the
donor's gravity-dependent significant-height tolerance. ExecuteAnimation,
pilot script-name transfer and the separate GUI rank label are not rendered.
Both stops appear in the logic report while active crates exist. These gaps
do not prevent ordinary ground gold chests or experience shields from paying
once. This is a donor port, not a claim of verified RotWK binary equivalence.

Tests in `test_mod1_retail.cpp` mount retail templates and use a real AI world.
Gold, shields, soldiers, battalions, rejected collectors, module caches and
exact stop text are covered. No retail assets are committed.

The derived hasCrateCollide flag is recomputed with the collision interface
list so contact discovery sees pickups after module creation or replacement.

Editions: these runtime modules serve the shared BFME2/RotWK simulation.
BFME1 data also reaches these shared modules; BFME1 pickup parity has not
been verified. Target-binary provenance remains subject to S-001.

Ordinary context clicks on CRATE objects issue a move to their position,
including enemy-owned crates. Force-attack retains its existing path. This
priority is an input adaptation, not a verified retail context-command port;
`treasure hud:*` checks the actual click and eventual chest consumption.
