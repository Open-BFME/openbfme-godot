# BFME1 recruitment and scripted-transport runtime

Scope: BFME1 1.03 only. Authority is the original unpacked executable identified
in `docs/BFME1.md`. Open-BFME-1 C++ supplies address-qualified reading aids;
the observations below were checked against the native instructions and
constructor-installed vtables. The existing BFME2/RotWK behavior is retained.
This is not a replay-parity claim for the entire container or movement system.

## Combination callback

Object's constructor stores -1 at +0x370 (`0x001D2DD8`). Setter `0x001BF2E0`
stores its integer argument and always calls `0x001BF2A0`, even when unchanged.
The latter walks the behavior array at +0x1F0 in order and passes the index to
each +0x0C behavior interface's +0xA0 virtual.

This is an indicator-color palette index: `0x00361250` selects an entry of the
palette at GameLogic +0x170, uses its day/night indicator color on the Drawable,
and then invokes this setter with that entry's index. The independent palette
application and the setter's module broadcast are separate native operations.
The combination at `0x002461B2` copies the RECEIVER's index to the new horde;
it does not reapply a palette color to the new Drawable.

HordeContain's behavior table `0x00CAF200`, TransportContain's `0x00CAD420` and
HordeTransportContain's `0x00CB05C0` all route +0xA0 through ILT `0x00017521`
to `0x0011A110`, a no-op accepting the integer. No invented tint, ownership or
experience effect is attached to this callback. Other systems' palette
assignment/propagation, such as castle and strategic-map color handling, are
outside this recruitment path.

OpenBFME stores/hashes the BFME1 index and writes Object snapshot version 3.
BFME2/RotWK still write version 2 and do not add this field to their hashes.
Loading old BFME1 objects initializes the previously absent index to -1.

## Rank and member adoption

At `0x00246178..0x002461AD`, the native comparison selects the HIGHER horde
rank, then calls gainExpForLevel(rank - 1). The earlier OpenBFME implementation
incorrectly selected the minimum. The banner comparison independently keeps the
higher-ranked banner; ties keep the receiver's banner.

GameLogic +0x90 is m_isScoringEnabled (also identified by GameLogicInit.cpp).
The combination clears it across the rank/color/adoption sequence and restores
it at `0x00246258`, before retiring the source hordes. This avoids crediting
new level-up scores for transferring existing ranks.

The new horde's +0xE4 interface +0x74 routes to `0x0023FDB0`:
remove each member's old pathfinder goal, join the new contain, gain the
positive difference to the new horde's rank, apply formation conditions and
ForcedLocomotorSet, refresh the formation, and release the formation movement
hold. The original soldier objects survive; no new InitialPayload is made.

## Transport differences

- The Create interface installed at +0x34 is `0x00CAD1EC`. Its first entry
  (`0x0022CDD0`) does nothing. The second (`0x0022CDE0`) re-dispatches the
  primary createPayload virtual. BFME1 InitialPayload is therefore created
  at build completion, not onObjectCreated. The accepted payload inherits an
  indestructible body's protection (`0x0022E150`).
- OpenContain validation `0x00226A80` has no Ring rejection, extra mounted
  cavalry exclusion, or CAN_ENTER_ANYTHING relationship bypass. Its ordinary
  passenger filters, combo restrictions and relationship flags still apply.
- Transport validation `0x0022DBA0` unwraps the first passenger of a transparent
  zero-slot wrapper for entry tests. Object's slot query `0x001C2F90` recursively
  sums such a wrapper's payload instead of its own signed-byte slot field.
  The reinforcement partitioner queries capacity through the contain interface.
  Validation `0x0022DC25..0x0022DC76` keeps the first rider as the capacity
  candidate too, while `onContaining` (`0x0022DD72`) charges the whole wrapper.
  This native quirk can overfill a carrier; the regression pins it explicitly.
- GrabWeapon at `0x0022DEAC..0x0022DF06` is fired at the carrier's position
  when the passenger is not allied. Unless FireGrabWeaponOnVictim is true,
  the passenger is UNATTACKABLE during that area shot; that status then clears.
- Removal `0x0022E340` idles the passenger from the script command source.
  ResetMoodCheckTimeOnExit calls `0x0026F9D0`: an idle AI checks on the current
  frame with randomized timing, rather than merely waking its scheduler.
- Exit permission `0x0022D0E0` first asks the carrier AI's free-to-exit virtual.
  An airborne carrier passes; otherwise the rider needs a locomotor and a
  valid terrain cell on the CARRIER's layer. There is no BFME2 ship-shore branch.
- Healing update `0x0022D660`, especially `0x0022D6E8..0x0022D70B`, uses x87
  PC24 in this order: max health * 0.2f * regeneration percent * 0.01f, one final
  dword store. The later-edition automatic throw/upgrade update calls are absent
  from this native routine. MOVING is mirrored to passenger TRANSPORT_MOVING,
  then it tail-calls OpenContain's update.
- The return/delete state's appended final node (`0x0017A530`) permits its
  returning carrier to travel beyond the pathfinder edge. A synthetic regression
  observes the carrier at negative X before deletion, and saves/restores its
  departure state and locomotor flag. No generic off-map mover parity is inferred.

## Client attachment and fades

BFME1 transport entry `0x0022DF06..0x0022E05C` applies FadeFilter to the
passenger, converts EnterFadeTime with `ftol2(milliseconds * 0.03f)` and
starts fade-in, or fade-out with FadeReverse. For a horde it applies the fade
to the members. Removal `0x0022E450..0x0022E557` uses ExitFadeTime and always
fades in. A zero millisecond setting skips the request; a nonzero setting that
truncates to zero client frames still starts a fade. Native Drawable
`0x00410D30` / `0x00410D80` starts mode 2 / 1 even for zero frames, unlike
RotWK `0x00670A50` / `0x00670AA2`'s immediate completion.

The completed logic snapshot captures the selected parent and passenger bone,
including members inside a horde inside a transport. After all parent drawables
have committed their poses, the client attaches to the current animated bone.
Serial and parallel client schedules use the same dependency order. No live
logic object or simulation RNG is read by the attachment pass. A missing bone
is reported as S-5017, rather than silently counted as successful attachment.

Native removal releases the attachment with ReleaseSnappyness
(`0x00417D70`), except for RIDER_IS_PILOT. Native draw adjustment `0x007629F0`
blends the saved matrix toward the ordinary pose with Matrix3D::Lerp and adds
1/30 to the blend on each client frame. This is implemented for the transform
and release. Armed passenger aiming (`0x0075F210`, `0x0075B550`) now
steps by 0.1 radians per client tick toward the AI goal while its current
victim is in the current weapon's range; otherwise it returns toward the
carrier bone's facing. The native wrap branches, including their boundary
overshoot, are preserved. Snapshot capture freezes the permission, range gate
and goal on the logic thread before parallel capture; client sync reads no live
AI or weapon. Client aiming state does not enter the simulation hash. The retail regression checks the release at blend zero, after one client tick,
and after completion, and checks that the simulation hash is unchanged. No
Godot before/after capture has been performed for this change.

## Rampage and falling riders

AIRampageState's constructor `0x001719F0` installs `0x00C984D8`; the AI machine
registers it as state 45. Entry `0x0017EA20` selects ENRAGED locomotion, drops
the pathfinder goal, locks player/script commands, selects the RAMPAGE weapon
set, and sets UNSELECTABLE/RAMPAGING. Duration accessor `0x00279750` returns
zero when RampageRequiresAflame is true and the owner is not AFLAME.

Update `0x00184FE0` calls the contain interface's +0xE4 virtual at
TimeToEjectPassengersOnRampage, once; zero eject time disables the call. With
zero rampage duration, a nonzero eject period fires immediately. The
HordeTransport implementation `0x0024A270` arms sequential falling riders.
Its update `0x0024CD10` draws intervals 3..5 and 0..4 at sites 0x350/0x35D;
its thrown member `0x0024CF00` draws 3..8 at site 0x2FD. The original BFME1
source label is retained in RNG recordings; later-edition sites are unchanged.

The caller is Lua ObjectEnterRampageState (`0x002E9F70`), through AI command
0x3A and private dispatcher `0x0026FB40`. Repeated calls do not restart the
current rampage. ActiveBody's +0x94 virtual in this binding is a no-op
(`0x0020DC10`); it is not setIndestructible. DelayedDeathBody overrides the same slot with
`0x00212870`, setting its checked flag, as the port's setter does (RW
`0x008C57A1`). The live BFME1 Lua host now reaches
this state. The later-edition host retains its reported unimplemented path. Rampage timers
and the firing latch are included in AI state save/load; the latch is also
hashed while the state is inactive. Older BFME1 snapshots missing state 45 are
rejected by the machine's state-definition check; later-edition definitions
are unchanged.

This state is partial. Selection notification and the Lua entry module guard
are reported as unverified. At completion, Object `0x001CADC0` dispatches
RunOffMapBehavior if present, otherwise kills the object; state exit
`0x0017EB90` clears RAMPAGING/RAMPAGE/command locks, sets CHARGING and kills.
The no-handler end is implemented. A RunOffMapBehavior end raises a named
S-5017 error, because its callback, last-damager scoring and death/movement
interaction are not ported. Retail cinematic-Mumak checks stop after the eject deadline,
so they do not establish complete rampage/death behavior. The ordinary retail
MordorMumakil has no TimeToEjectPassengersOnRampage setting; zero disables
timed ejection. CINE_MordorMumakil declares 2300 ms (12 logic frames) and
7960 ms duration (40 logic frames); no arbitrary timer is supplied to the
ordinary Mumak.

## Special horde variants

The BFME1 SlaughterHordeContain runtime constructor `0x0024E7A0` calls
HordeGarrisonContain `0x00248F90`. Its data factory `0x00116D80` calls
`0x0024F390`, with CashBackPercent default 0.5 and field table `0x00CB0E98`.
The similarly named constructor at `0x0022B160` belongs to another class.

Slaughter entry `0x0024ECA0` accepts the wrapper through the garrison path;
member callback `0x0024ED40` refunds BountyValue (HERO value times rank),
floored after multiplying CashBackPercent, then applies multiplayer money
scaling, the building's production modifier, ceil, and the owner's handicap.
It hides each arrival. HORDE_MEMBER arrivals rejoin their garrisoned horde
through `0x002489E0`; after the last arrival, `0x0024F105..0x0024F150`
destroys the wrapper and its members. Standalone passengers are killed and
destroyed immediately. Update `0x0024F350` traverses both
contained and entering members and sends entry commands unless the member is
already in state 15 or 56. The BFME1 typed/runtime factory is isolated; later
editions retain the unported Slaughter module. Nonzero CashBackPercent
also runs `0x0024F002..0x0024F07B`: an exact Team change clears object upgrades
and calls each upgrade mux's removal before clearing its executed flag
(`0x001C36B0`, `0x002D9A70`), including permanent upgrades. Same-team arrivals
union their object upgrade masks. The original-team field is the base
GarrisonContain field, not a separate remembered player.

The final horde arrival records the whole horde count under that arrival's
template name. Before the next ownership handoff, `0x0024E990` credits units
lost to the previous house owner and units destroyed to the next owner,
unconditionally (`0x000EAC60`, `0x000EAA90`); same-owner arrival clears the
pending record without scoring. These corrections leave alive counts alone.
The pending count and template are saved and hashed. Refunds also grant the
house experience (`0x0024EF91..0x0024EFD0`), and nonzero-cashback entry idles the
house and kills its separate builder (`0x0024F0AE..0x0024F0F6`).

AODHordeContain `0x00230580` calls HordeContain `0x0023EAF0`, with its own
typed data (`0x0022F7E0`, table `0x00CAE090`), formation initialization
(`0x00231C50`), path history (`0x00230AE0` / `0x0022F960`), member placement
(`0x00230BF0`), and position evaluation (`0x002310F0`). The BFME1 variant now
implements the six per-slot random calls in native order and with native call
sites, 60 history samples, 20 distinct-X flow rows, ten-degree row turns,
XY/Z waves, speed/still-amplitude blending, water/ground height, and oath
scatter (the counter advances under OATH_FULLFILLED, not MOVING). It saves and hashes its waves, history, rows, tracked unit and scatter
distance. Position-dependent initial history is seeded again at the port's
placed/build-complete boundary without repeating the random calls.

The tracking callback `0x00231980` tests **DeadCollideSize LARGE**; it does not
test locomotor appearance. Repeated contacts with the same id do not refresh
its timestamp. The height hump uses the native 17-unit lateral exclusion,
height clamps, tail-off and half-time fade. AODCrushCollide (`0x00215E50`, data
table `0x00CA98A8`) now feeds this callback and applies normal/special damage,
self damage, size-selected FX/OCL and the FIRING_A deadline (`0x00216100`:
strictly after ten frames). Ally, dead, stunned, missing-physics and fulfilled
oath guards retain native order. Contact generation uses the BFME1 persistent contact system described below.

HorseHordeContain has a BFME1 variant for the member order `0x0024E310`,
positive-speed clamp `0x0024D4E0` and template-equal, optionally adjacent odd/even
slot exchange `0x0024D610` (0.3 distance margin; the adjacent-pair restriction
applies with fewer than two RankInfo rows). Its attack callback
`0x0024D340` forwards to ordinary HordeContain attack `0x00241F10`.
Ordinary, horse and AOD hordes share the BFME1 formation/melee variant described below. Low-level locomotor and pathfinder stops still apply; neither special variant establishes full native combat or movement parity. The oath-facing helper follows BFME1
`0x00150510` (normalized dot/acos and cross-product sign, zero separation
returns zero), retaining the deterministic acos facade's existing S-167 limit.

## Shared BFME1 formation and melee

Edition: **BFME1 only**. `Bfme1HordeContain` is the common base for ordinary,
horse and AOD hordes; BFME2/RotWK retain `HordeContain` and their existing
strategy records. BFME1 slot geometry (`0x00233F30`, `0x00242A30`,
`0x002458B0`) uses radius/atan2, owner orientation and `NarrowPassageScale`
(default 40%, typed percent), keeping the owner's height. Ordinary reform
`0x002435F0` exchanges same-template, compatible-weapon pairs in contain order
with the strict improvement and 0.3 margin. It does not use RotWK's
farthest-first reassignment (`RW 0x877E12`).

Freeze/unfreeze (`0x00243EA0`, `0x0023D850`) retains one 28-byte logical record
per slot: phase, destination, invalid flag, retry deadline and last active
frame. Invalid frozen records answer the member's current position. The
member order and hub (`0x00245420`, `0x002417E0`) snap before turning, apply
the native phase-specific turn gates, preserve explicit paths, and reserve
member goals. The independent gather flag (`0x002325E0`) selects explicit
movement; freezing alone does not select that goal type. Unfreeze drops
member reservations and recovers a MACHINE member's transform.

Readiness (`0x002439F0`) uses the native fixed squared edge threshold 10000
and member active/victim checks, instead of the RW Amoeba weapon-range and
height gates (`RW 0x98FA8E`). The shared AI retains the 15-frame cache.
The path predicate (`0x001B3FE0`) skips special waypoint/portal paths, not
ordinary walking. Ground member acquisition follows Object `0x001CBDC0(false)`
with actual-footprint and perimeter candidates (`0x003E4330`, `0x003E49F0`)
and the ground adjacency branch of `0x003EA570`. It supports HORDE_MEMBER;
the shared general mood scan rejects that status and cannot replace this path.
The attack predicates use the attacking state, matching native virtual +0x184.
`0x002440E0` schedules members in contain order with budget
max(member count / 2, 2), phases 0..3 and retry deadlines. Losing a member
(`0x002408E0`, tails `0x00240A4F/0x00240A89`) bypasses outstanding delays
on the next pass. Destination search
`0x00238D10` scans 10-unit square perimeters in x/y order, preserves strict
score comparisons, the 0.7071 early exit and 10/20-frame retry delays. The
fallback start search deliberately retains the executable's use of X for
both offsets (`0x00238F0E..0x00238F3C`). The recently-active argument is passed
by the native caller but is not read by this body. Goal acceptance follows
`0x003DF250`, including PATH_THROUGH_INFANTRY and HORDE exceptions.

## BFME1 special melee views and recipients

Edition: **BFME1 only**. `Bfme1MeleeRuntime` follows the original 1.03
instructions, with the clone's names used only as reading aids. The melee view
(`0x003EA570`) retains actual/perimeter identity priority, the current-position
non-structure overlap exception, wall geometry and the deployed siege-tower
branch. Wall distance (`0x001C2380`) uses each active shape's projected
rectangle, planar offset transform and corner support extent. Ordinary walls
also require the different-layer type-5 rim (`0x003E4DA0`); WALL_UPGRADE skips
that gate and uses a strict 20-unit gap. Siege towers require deployment phase
3 and strict top-attack XY/radius and 20-unit height bounds. This verifies the
query against the existing portal/deployment state, not the entire deployment
runtime against BFME1.

The recipient (`0x001CFF30`) retains re-entry, current/explicit target, busy,
melee-weapon, own/horde crush-speed, mood, container permission, idle/attack,
fear, melee-status and stealth guards. The moving-away test (`0x00175820`)
resolves a HORDE_MEMBER to its horde, tests facing, and rejects speed strictly
above one quarter of maximum (`0x001B9C10`). The native attack predicate
(AI virtual +0x184, `0x0026F5C0`) dispatches the current state's virtual +0x20;
it is OpenBFME's `isStateActive`, not a test of root attack state IDs.
Container permission follows `0x001C14E0` -> `0x00243990` (busy becomes idle;
otherwise only idle members pass). Handoff (`0x0027D420`) preserves an active
attack, attack-move or attack-follow machine and its lock, changes its goal,
and destroys its path. A retained temporary machine prevents a new ordinary
attack command. The guarded callback asks the enemy non-MINE recipient in
turn. Ground acquisition (`0x001CBDC0`) now runs these guards and checks the
horde's locked temporary state and footprint movement predicate.

Crush eligibility/speed (`0x001C7600`, `0x001CC790`, `0x001C7530`) uses native
signed-byte template/mounted levels, relationship/weapon checks and the
horde-root speed threshold. RW's added attribute-level sums are not used in
this BFME1 path. The underlying quick path query still has the broader
pathfinder equivalence boundary below.

Footprint queries (`0x003DEE30`, `0x003E4330`, `0x003E4680`, `0x003E49F0`)
retain native centering, unclipped cells, traversal, scalar owners and the
16-result cap. The raised goal query deliberately preserves the native
`0x003E487E` asymmetry: it tests/deduplicates the goal owner but returns the
position owner, including zero/repeated results and raised-to-ground fallback.
The native Bridge-end extra ground query now runs for registered plain
Bridge geometry (`0x001A7F70`, `0x001A24B0`). Retail map Bridge creation
and cell classification remain S-161; the new registration hook does not
claim to provide that producer. Movement clearance (`0x003E5E40`) distinguishes a stationary
position/goal owner from a moving owner's separate goal. BFME1 reservations
replace scalar owners without reviving an older list entry. A position can
only be displaced by its goal owner (`0x003EA195..0x003EA1AB`); a goal at
the current position also claims position, except for a HORDE
(`0x003E9A69`, `0x003E9BDF`). Horde positions use the same scalar as other
units. A stationary
position/goal owner is protected from another goal (`0x003E9BBE`). A horde
with fewer than two contained members leaves its previous goal unchanged
(`0x003E9755..0x003E9777`). Later editions retain linked reservations.
The melee line walk selects the unit-ignoring native mode 0x0C (`0x003EE970`,
record `0x003E5A50`), paired with the existing `RW 0x6F2FFF` primitive.

These control paths still run on OpenBFME's general pathfinder, terrain,
locomotor dependencies. Their complete BFME1 equivalence, mesh-backed Bridge
heights/damage, contested-building and AssaultTransport callbacks, and native CRT transcendentals remain
S-220/S-590/S-5017/S-167. The mood/target and horde emotion variants below
replace the previously inherited control paths. The runtime reports this boundary and never selects
the RW melee strategy when native destination search fails. Frozen records,
gather state, the death retry latch and force-refresh flag are saved/hashed.
The force flag clears only after a member pass finishes without further work
(`0x0023FA80`). The auto-acquisition byte (AI +0x335) is saved/hashed only when
set, through AI-module version 6. Idle, attack-move, attack-follow-waypoint
and giant-bird attack-move acquisition set it (`0x001881DF`, `0x001886AD`,
`0x00188DCF`, `0x002BF9F7`); accepted orders clear it. Turret retargeting in
the pursue/approach states remains part of the general attack-machine boundary
(S-325). The transient
reciprocal callback guard is cleared on load and is not persistent state.

## BFME1 persistent contact scheduling

Edition: **BFME1 only**. `Bfme1ContactManager` follows dirty marking
`0x009A2350`, invalidation `0x009A2FE0`, movement scoring `0x009A2420`,
three-axis sweep `0x009A3AD0`, overlap counts `0x009A3960`, hash-head insertion
`0x009A3770` and dispatch `0x009A30B0`. Endpoint ordering, incremental strict
crossings and the native alternating/reversed merge-sort input
(`0x009A2750`) are retained, including history-dependent equal endpoints.
The collision group (`0x001CAB00`) is the IMMOBILE sentinel, the container,
or the object itself. Native KindOf bit 2 means IMMOBILE, not STRUCTURE.
Objects without AI, including crates and projectiles, enter this same pass;
the separate later-edition crate/projectile passes are suppressed in BFME1.

Candidate pairs persist with three-axis overlap counts. A contact caches
unknown/hit/miss until either source is dirty; contacts dispatch in ascending
0x493 bucket order and bucket-head order, lower ObjectID side first. Both
sources are checked again between responses. Removing a source during the
first response suppresses the reverse response. All callbacks run serially.
Bounds follow `0x001BFC20/0x001BFC80`; the shape test `0x001BFCE0` calls
`0x0087F2F0`, whose existing shape-pair primitive is shared with
`RW 0xAD2CE0`. Native sources do not populate contact point/normal storage;
OpenBFME deliberately supplies zero coordinates. Collision-ignore-ID
writers/guards are implemented below; the original uninitialized storage
remains an explicit S-5017 boundary. No arbitrary normal or native replay parity is claimed.

The native OpenBFME AI snapshot writes BFME1-only version 4, retaining
nodes, dirty order, endpoint order, overlap counts, contact bucket order and
cached tests. This is an OpenBFME continuation format, not a retail save
format. Older BFME1 AI blocks are rejected because they lack persistent contacts
or scalar reservation semantics. BFME2/RotWK keep version 2 and their prior collision path/hash.

## BFME1 general target and mood variant

Edition: **BFME1 only**. `AIEditionRules` selects native mood scan
`0x00279A50` instead of `RW 0x66844A`, and target search `0x0014D120`
instead of `RW 0x7006D7`. The native variant retains its contained-object
and stealth guards, cadence draw site `AIUpdate.cpp:0x1D67`, default-priority
closest search, raw integer template priorities (`0x0034C9B0`), integer
priority-plus-100 score and PC24 arithmetic, passenger/horde priorities,
and preference for reachable candidates. It omits the later game's added
vision, wall and friendly-fire scoring gates. The default info ignores the
scored search's optional filter. BFME2/RotWK keep their existing target path.

The reach query (`0x0014CA60`) uses the native horde representative
(`0x00238A60`, longest positive range for MELEE_HORDE `0x00237E50`), the
horde as weapon owner, its layer guards, and `MeleeAcquireLimitDist`.
The BFME1 AIData field is parsed as a real at table `0x00C94DB0`, default
100 (`0x0014B150`, field +0x98); only BFME1 consumes it. The approach probe
(`0x003F5E20`, callback `0x003EE9F0`; RW `0x6FB754`) keeps the native PC24
sample points and always tests cell centers, including even footprints.
Its quick-path, zone, terrain and movement dependencies are still subject
to the general pathfinder stop.

The player-stop latch (AI +0x33A) is set after group idle (`0x00155C02`),
read by reach and retaliation (`0x00279E88`), and cleared on separation or
a successful harvest task (`0x0026A379`). AI module version 7 saves/hashes
it when set. Firing at a HORDE_MEMBER records its horde in the firing
tracker (`0x001B3510`); `0x001BE3D0` / `0x001B2D80` consumes that ID once
before the scored mood search. Object version 4 saves/hashes the pending
firing ID and collision-ignore ID. These are OpenBFME continuation formats.

## BFME1 emotion and collision fields

Edition: **BFME1 only**. Back-up records (`0x00246490`) retain the native
probability/distance/delay draw sites 0x1ABD/0x1AC2/0x1AC3, PC24 stores and
wide intermediates. Quarrel (`0x00244680`, end `0x002396F0`) uses integer
bounds, native centroid/nearest selection, draw sites 0x1B34/0x1B4F,
fighter and spectator conditions, and the native member pass (`0x00241050`)
for porcupine formations as well. Nearby spectators separate using raw
contained-member order (`0x00235520`). The native exclusion/auxiliary
registry is the released-to-garrison ID set, not the core's ordinary
slotted-member registry. Recent-damage and representative queries use that
same released set. Emotion entry makes members busy from SCRIPT (1),
`0x00244080`, rather than RW's AI source (2). Existing horde snapshots hold
the back-up map, quarrel map/fighters/flags and face point.

Collision-ignore ID (Object +0x3A0) follows `0x001C8A80`: the recipient
ignores its matching target, while a nonzero ignore field on the other
object blocks the reverse callback regardless of equality. Grab approach
sets the target or its horde root (`0x002A9172..0x002A91AE`); grab/throw
completion clears it (`0x002A869E`). Contact allocation `0x009A3960` passes
an incompletely initialized stack record to `0x009A3770`; neither the
allocation nor shape test supplies defined contact coordinates. OpenBFME
continues passing explicit zeros and reports that difference.

## BFME1 plain-Bridge end query

Edition: **BFME1 only**. `Bfme1BridgeGeometry` implements the plain Bridge
(+0x8C == 0) end segments (`0x001A24B0`), inclusive LineInRegion clipping
(`0x001A2020`), plane/intersection height (`0x001A5DC0` / `0x001A1CF0`),
and object-end query (`0x001A7F70`). It shortens both ends by ten along the
normalized near edge, expands the object's square by bounding radius plus
five, and accepts an absolute height difference at most ten. The footprint
consumer uses the object's actual position even for hypothetical queries,
and adds the ground scan at an end. Registration is newest first, matching
`0x001AA780`. Vertical planes cannot provide a defined native intersection
and are refused. The plain-Bridge map producer and grid classifier below now
supply this query; mesh-backed Bridge heights remain unverified.

## Remaining boundaries

S-5017 still reports complete general pathfinder equivalence, mesh-backed
Bridge heights and Bridge damage/repair, contested-building mood callbacks
(`0x0014D3E9`, ContestableContain's paired-target map), the AssaultTransport
member-management writers of allowed-to-chase, contact-coordinate storage,
rampage completion and broader
combat/physics/movement/script/hero-lifecycle gaps. Native CRT transcendentals
remain S-167. Passing focused skirmish and continuation checks does not
establish full original BFME1 behavior, retail replay parity or retail save
compatibility. BFME2/RotWK retain their existing simulation paths and stops.

## Previous validation (before the general AI and emotion variant)

The retail and pool checks below used mounted game files. The focused and
module save/load suites used synthetic data. The four-faction scenario retained
its 4,500-frame budget, seed 9001, Medium AI and 2000 starting cash.

| Check | Result |
| --- | --- |
| Core C++ target | Built successfully |
| Focused non-retail regressions | 152 cases, 3240 assertions passed |
| Module save/load coverage | 16 cases, 1602 assertions passed; 184 registered behavior classes |
| Serial/pool snapshot comparisons | 2 cases, 196 assertions passed |
| Retail four-faction, combination/delivery, attachment/aiming, Slaughter, both AOD sizes, cinematic-Mumak and edition-isolation checks | 3 cases, 493 assertions passed |
| Simulation flags/link/classification audit | No findings; AST audit skipped (S-230), not a full audit pass |
| Privacy/tolerance/whitespace gate | Passed |
| Godot visual before/after capture | Not run |

All four factions registered enemy damage, built and trained through their
native faction libraries. The first rerun reproduced a Rohan zero-damage
failure; correcting the native state predicate, scalar position/goal guards
and horde position representation resolved it without changing the scenario
or its assertions. The later-edition isolation case loads BFME2 and RotWK, starts
their existing skirmish AI, and advances 12 frames. It still reports BFME2's
pre-existing S-1321 Living World AddPlayer gap; it does not claim that campaign
system is ported. The extended retail case adds release interpolation checks
to the combination/delivery scenario, so its assertion count overlaps the
three-case run rather than representing a separate full game suite.

## General AI/emotion/collision validation

Edition scope: the AI, horde, reach, collision and Bridge-end changes are
**BFME1 only**. The shared settings loader now preprocesses GameData's global
macros for standalone AIData and map readers; BFME1, BFME2 and RotWK mounted
loads were checked. The macro regression also proves that preprocessing does
not apply GameData a second time and unknown macros still fail.

| Check | Result |
| --- | --- |
| Core C++ target | Built successfully |
| Focused non-retail regressions | 159 cases, 3427 assertions passed |
| Module save/load coverage | 16 cases, 1602 assertions passed; 184 registered behavior classes |
| Serial/pool snapshot comparisons | 2 cases, 196 assertions passed |
| Mounted retail scenarios, including all four factions and BFME2/RotWK isolation | 3 cases, 493 assertions passed |
| Simulation compiler/link/classification audit | 1483 compile commands, 15 link commands, 414 simulation sources; no findings; AST skipped (S-230) |
| Privacy/tolerance/whitespace gate | Passed; 99 changed paths checked with a temporary index |
| Godot visual comparison and native replay comparison | Not run |

The four-faction check kept its seed, cash, difficulty, map, 4500-frame budget
and assertions. Gondor, Rohan, Mordor and Isengard each built, trained and
registered enemy damage. An initial retail load failure exposed the missing
GameData macro context in the standalone AIData reader; fixing that dependency
resolved it. The BFME2/RotWK case retains the pre-existing S-1321 campaign
`AddPlayer` report. The focused run excludes retail cases and records its
usual unavailable binary-census/startup checks as skipped; those skips are not
additional passes. These results do not establish complete BFME1 equivalence.


## Plain Bridge creation, general pathfinder entries and charge attacks

Edition: **BFME1 only**. BFME2/RotWK retain their current pathfinder and attack
variants. The shared map-property implementation was extracted without changing
its rules and is also used by the new BFME1 Bridge producer.

`Bfme1MapBridges` follows `loadBridges` (`0x006DA8A0`): an authored first
endpoint immediately followed by a second endpoint is one Bridge, in list order;
each endpoint adds ground height and 0.25 to its authored Z. The typed roads.ini
reader inherits DefaultBridge's pristine model and scale. `W3DBridge::load`
(`0x006D8C10`) supplies the transformed BRIDGE_LEFT vertex bounds;
`getBridgeInfo` (`0x006D7CD0`) supplies their scaled XY normal and four corners.
`Bridge` construction (`0x001A98A0`) creates GenericBridge at the native diagonal
midpoint and applies the first endpoint's map properties and orientation. These
objects precede ordinary map placements and the pathfinder map build in LiveGame.
No synthetic width or flat replacement height is supplied for a missing model.
The pristine W3D is an immutable mounted gameplay input on this path, as in
retail; no active drawable, animation, render LOD or frame state is sampled.
Peers require matching mounted assets, as they require matching INI and map
inputs. Negotiating/verifying complete multiplayer asset compatibility remains
the separate cross-play gate in `docs/PLAN.md`; replacing this geometry with
an invented constant would change BFME1's map topology.

Layer allocation (`0x003D5850`) uses the first free layer 2..15. Allocation bounds
(`0x003FBED0`) and cell classification (`0x003FC790`) use native four-corner
coverage, side exclusions (`0x001A26D0`), inset ends (`0x001A24B0`), and outward
entry segments (`0x001A2950`). Entries connect both grids. Outside cells are raw
type 5, side/clearance failures raw type 6: BFME1's names for these two values
are reversed relative to this port's names. `GetCell` (`0x003FBAB0`) falls back
to ground for type 5. Ground obstacles survive the under-deck clearance pass.
The two hierarchical connection cells use the native seven-unit span offset.
Destination-layer selection (`0x001A7C20`) walks Bridges newest first and selects
a strictly closer height than the ground or prior selection, before the ramp fallback; the native plane
result stays wide through that comparison. OpenBFME snapshots now store Bridge
geometry, IDs and cell state in wall block version 3; maps without Bridges keep
version 2. This does not add retail save compatibility.

General reachability now preserves the BFME1 differences at `0x003EDF90`
(RW `0x006F5BB0`): no initial IMMOBILE rejection, surface mask 0x0F rather than
0x8F, object-aware endpoint layers, and early success for an obstacle whose
start effective zone is zero. Cell passability (`0x003D4F90`, table
`0x00EB49FC`; RW `0x006E8200`, table `0x00DA2444`) omits the later crusher
obstacle exclusion and has no type-7 deep-water surface. The possible-destination
budget of 400 is verified at `0x003EAC80`. Other general searches, incremental
zone scheduling, unresolved budgets and CRT differences retain their existing
stops; these entry-point corrections do not prove general A* equivalence.

`CanFireWhileCharging` selects native attack kind 6 (`0x001846D7`) and
AttackFireWhileChargingMachine (`0x001818D0`). Its state enters at
`0x0017ECB0`, updates at `0x00185060`, and exits at `0x0017EE60`. It follows a
target that moves more than 50 in XY, checks the strict unmodified 3D weapon
range and signed normalized-dot/acos angle (`0x0016FD30` calling `0x00150510`), clears the goal object, paths fifty units
past the target (`0x0016F4F0`), sets CHARGING, switches locomotor set to 4,
and fires at the attacker's position (`0x0017EEE0`). Exit clears CHARGING,
restores set 0 and releases the temporary lock. The machine and its move state
have native OpenBFME snapshot continuation. VoiceStartCharging dispatch is
still a client boundary; retail replay equality is not established.

The charge special-power trigger's native branch (`0x002A9F48`) locks PRIMARY
before issuing the attack. Its update (`0x002AA9D0`) handles ChargeAttackSpeedBoost
and the charge abort guards; a location-only approach faces the position and
sets the idle-abort flag (`0x002A92EC..0x002A9326`). MustFinishAbility retains
its abort exemption. Existing saved charge flags now have these BFME1 writers.
The human mood filter (`0x0026FA80`; RW `0x00662E2B`) reads an explicit saved
allowed-to-chase byte (native setter `0x002B4300`), rather than treating ordinary
movement as charge. Its AssaultTransport writers are identified at `0x002B4C00`
and `0x002B4950` but their member-management runtime remains unported. The
Object +0x1F5 constructor cache (`0x001D3829`) reads the ability's initially clear
+0xE4 byte; it is separate from a locomotor order.

The synthetic Bridge check crosses an impassable-water strip through the raised
grid and round-trips its cells/geometry. The mounted producer check places an
authored test pair over Fords of Isen using the real roads.ini and pristine W3D;
it does not claim that Fords contains endpoint Bridges. The same mounted case
selects and triggers the real Mumak charge weapon and checks its locomotor-set
changes. Bridge damage-state transitions, repairs, mesh-backed heights and
client bridge rendering remain named S-5017 boundaries.

## Bridge, general-pathfinder and charge validation

Edition scope: **BFME1 only** simulation changes; the extracted map-property
helper also serves BFME2/RotWK without changing their rules. Final checks:

| Check | Result |
| --- | --- |
| Core C++ target | Built successfully |
| Focused regressions | 165 cases, 3593 assertions passed; mount-dependent cases print SKIP without their inputs |
| Shared wall/ramp regressions | 36 cases, 6598 assertions passed |
| Native OpenBFME module save/load | 16 cases, 1602 assertions passed; 184 behavior classes |
| Serial/pool snapshot comparison | 2 cases, 196 assertions passed |
| Mounted retail scenarios | 4 cases, 575 assertions passed; all four factions built, trained and dealt enemy damage |
| Compiler/link/classification audit | 1486 compile commands, 15 link commands, 417 simulation sources; no findings; AST skipped (S-230) |
| Privacy/tolerance/whitespace gate | Passed; 115 changed paths checked with a temporary index |
| Godot visual comparison and original executable replay comparison | Not run |

The four-faction scenario keeps Fords of Isen, seed 9001, Medium AI, 2000
starting cash and its 4500-frame budget. The new charge subcases pin the
Bridge-before-ramp layer priority, negative signed angle, refusal of the positive out-of-aim angle, strict range
equality, charge-through goal, locomotor restoration and snapshot continuation.
The retail Bridge/Mumak case uses mounted BFME1 data; the existing isolation
case loads and advances BFME2 and RotWK. It continues to report the existing
BFME2 S-1321 Living World AddPlayer boundary. These results establish the
covered paths, not complete BFME1 equivalence or retail save compatibility.

Review regressions additionally pin the native frame-zero threat value and
transparent-wrapper overfill behavior: 2 cases, 44 assertions passed. Object
construction (`0x001D2DA8`) and Team construction (`0x000F7830`) initialize
threat value/deadline to zero; their queries (`0x001D2580`, `0x000F72D0`)
refresh only when the deadline is strictly less than the current frame.

PR validation preserves AOD inherited death-filter fields from native table
`0x00CB2E70` (build chain `0x0022F8A0 -> 0x002469A0 -> 0x0022DB70 ->
0x002284A0`). They remain typed data; AOD death-filter execution is an explicit,
regression-pinned S-5017 stop. The field census no longer lists the three
horde rows now accepted by the native variant as gaps.
