# Stops B1: recovered closures

Target recovery uses the supplied RotWK image through local read-only Ghidra;
the PLAN rule 9 / S-001 provenance caveat applies. Retail-data regressions do
not constitute a clean-retail executable oracle. Private inputs are not part
of these changes.

## S-1366 — campaign gate script dispatch

Closed at the script boundary. `GATE_OPEN` (RW `0x7BD8B8`) and `GATE_CLOSE`
(`0x7BD826`) resolve the named object and its `GateOpenAndCloseBehavior`, not a
proxy. They check `isOpen` (slot `0x18`) against the requested state, then
`isSettled` (slot `0x28`), then call `open` / `close` (slots `0x1C` / `0x20`).
Missing objects/modules do nothing. In-progress transitions are not reversed.

`GATE_IS_OPEN` (condition 135, RW dispatcher `0x7EB7CD` → `0x7E4AA0`) resolves
the same module and returns `isOpen` alone: opening/closing/closed are false,
open is true. Missing objects/modules are false. BFME2's byte-matched donor
`ScriptConditions_evaluateNamedUnit.cpp::rva003E3EC3` corroborates this.

The existing HUD-5 gate worker already handles state timing, named geometry
changes and pathfinder obstruction refresh. No terrain, partition or Xfer
system is introduced. The separate S-1940..S-1942 gate-worker gaps (push-out,
proxy, sounds and related integration) remain open; closing S-1366 does not
claim those features or complete campaign playthroughs.

Regression `stops-b S-1366 retail:*` loads retail Helm's Deep and executes the
script actions/condition on `Main Gate`. It checks duplicate requests,
unsettled requests, both completed transitions, blocking footprint and named
geometry, missing names, and disappearance of the unported-action/condition
reports. Before the port: 14 failures in 46 assertions, after successful
map/module/human-owner preconditions.

Simulation impact: gate scripts now change gate state and pathing, and the
condition can select different script branches. Existing gate hashing is
reused; no new RNG calls or pinned-vector edits are introduced.

## S-932 — passive spell-book purchase dispatch

Closed at the purchase-command boundary. The store destructor (RW `0x8232ED`)
passes each pending **button** to `ControlBar::processCommandUI` (`0x940435`).
Its purchase-science case `0x19` sends `MSG_PURCHASE_SCIENCE` (`0x414`), then
resolves that button's ordered `CommandTrigger` vector (`+0x188`) and sends
`MSG_DO_SPELLBOOK_SPECIAL_POWER` (`0x456`) for each trigger. The arguments are
the trigger's special-power ID, its options, and object ID zero. The logic
handler (`0x77B886`) resolves the issuing player's spell book. These are
ordinary simulation messages, not direct client-side module activation.

Both retail Scavenger and Fuel the Fires purchase buttons name their passive
spell-book button as a trigger. The store previously discarded that identity
and sent only the science purchase. It now retains the clicked buttons and
emits their triggers after each purchase, preserving purchase and trigger
order. Reset, duplicate clicks and an already-owned science do not produce
extra casts. Missing trigger definitions/powers are explicit errors.

Science acquisition itself does **not** cast the power: RW `0x6AE1C9` and
`0x896B56` do not invoke Scavenger's activation override. The existing
science-grant-only test remains as a control rather than being changed to
invent automatic activation. Scavenger activation now reaches the existing
bounty setter. Fuel the Fires uses plain `SpecialPowerModule`; its retail
INI assigns the actual lumber bonus to science checks, not a player-upgrade
cast. This closure does not claim validation of lumber collection/economy.

Regression `stops-b S-932 retail:*` uses retail Wild and Isengard store and
spell-book definitions, real pending purchases/reset/close, and the existing
logic dispatcher. Preconditions establish the correct trigger and module
types. Red: 1 case / 90 assertions, with 8 behavioral failures across both
passives (missing trigger messages/casts, inactive Scavenger and zero bounty).
It also asserts that client operations leave the simulation hash unchanged
until messages are dispatched. No clean-retail executable oracle is claimed.

Green: 1 case / 108 assertions. Related SPELL-2, SPELL-3 and BACKLOG2 tests:
37 cases / 4,219 assertions passed. Release build, precommit and simulation
audit passed; the audit's S-230 AST subcheck is unavailable without libclang.
GDScript lint/parser tools are unavailable on this box.

Simulation impact: passive purchases now deliver additional deterministic
commands, changing cast counters/readiness and Scavenger bounty state and
therefore potentially hashes/economy. Existing module/player hashing is
reused; no RNG calls or pinned-vector edits are added. No terrain, partition
or retail Xfer dependency is introduced.

## S-1991 — Bloodthirsty sacrifice

Closed at the actual HUD and simulation-message boundary. The real button
enters target-selection mode; clicking an eligible member emits one
`MSG_SACRIFICE` whose object ID is its horde. The FACT-B dispatcher uses the
first controlled selected object, and `BloodthirstyUpdate` records reciprocal
source/target IDs before issuing the recovered horde order (RW `0x851080` to
`0x771380`, command `0xc`), then applies the source and victim horde statuses
and posts voice event `0x7e9`. This horde order is distinct from member-side
`amoebaAttack` (RW `0x66D658`), which uses the ordinary AI attack path with
source 2. Target predicates resolve the clicked object to its horde. Base
`AIUpdateInterface::acceptCommand` runs cancellation after stances but before
derived command handling; only commands `0xc` and `0x36` preserve the valid-
target exception.

The member ownership prerequisite follows `HordeContain::onContaining` at RW
`0x872D0A` (`HordeContain_onContaining.cpp` in the BFME2 donor): normal containment
and accepted created members record the horde as producer. Member attack
eligibility follows RW `0x6F2956`: a `BLOODTHIRSTY` member's horde target matches
a candidate's own ID or producer ID to bypass the relationship veto. The
non-forced relationship veto applies to player source 0, not AI source 2 (RW
`0x6C9147`). Weapon delivery retains its ordinary relationship mask (RW
`0x90D77C`); no damage-filter exception was added. Candidate enumeration and
partition-query approximations tracked by S-588 remain open.

The pre-damage hook follows `ActiveBody::attemptDamage` after armor, scalar and
healing handling but before health-change gating. A valid Bloodthirsty source
marks the hit lethal and calls `addSacrifice`, which advances the kill count,
sets model condition `0x40` on the first kill, grants experience with the
retail modifier, and releases the sacrifice before a last member's fatal
health change. Mutable reciprocal IDs and kills participate in the module
state hash. Voice lookup checks the object, then its horde, and uses the
existing EVA / `+SOUND:` parsing and audio sink.

The focused retail tests cover the actual HUD target path and horde-ID message,
reciprocal order state, natural combat through victim removal and sacrifice
release, model conditions, and XP computed from each killed member template's
retail award and the source-horde-keyed `HordeContain` pool arithmetic
(modifier 1.1 and the first pool's +1 seed). They also cover invalid/self/busy UI targets, a non-horde
without a Bloodthirsty module producing no voice, and voice decoding. Normal
initial members and an accepted created member are checked for producer IDs.
The encoded `MSG_SACRIFICE` is replayed through two deterministic peers: their
per-frame hashes agree through victim removal and one post-stop frame, and both
peers progress and remove the victim while clearing sacrifice state. Before
runtime changes, the actual-HUD red was 1 case /
32 assertions with four behavioral failures after setup. Before the producer
ownership fix, the allied natural-combat test was 1 case / 122 assertions with
five failures; its trace showed all victim producer IDs were zero and no
Bloodthirsty damage delivery occurred. Final focused result: 3 cases / 2,066
assertions passed. The XP fixture does not pre-level the source horde.

The faction inventory regression no longer lists `BloodthirstyUpdate` as
unported for Isengard or Mordor. In the faction button sweep, the Isengard
Fighter and Mordor Black Orc Bloodthirsty buttons pass. The Mordor Fighter
button check is not reached because that sweep's click-selection precondition
leaves the selection empty; the two broad faction sweeps also report unrelated
click-selection setup failures, so they are not counted as green evidence for
every command.

Simulation impact: sacrifice state and kill/XP progression now affect
deterministic simulation state; the module hashes its mutable IDs and kill
count. Recording member producer IDs also changes horde hashes outside sacrifice
and restores producer-based targeting/ownership queries. No RNG calls, pin changes, client-side simulation shortcuts or direct
order-time kills were added. The faction inventory no longer reports
`BloodthirstyUpdate` as unported, but the broad Isengard/Mordor button sweeps
remain unverified because their click-selection setup fails before some button
assertions. Retail's unrelated SlaveWatcher expectations remain open in this
older branch.

## S-853 — respawning-hero death order

`RespawnBody::internalChangeHealth` follows RW `0x8C553F`: zero delta returns
before the base change; lethal/permanent classification precedes it. On a
non-permanent lethal hit with `RespawnUpdate`, temporary defection ends through
the existing `Object::endDefection`, then the inherited/permanent or normal
revival branch runs, followed by `Object::friend_onDie` inside the body.
`ActiveBody::attemptDamage` preserves the later kill-credit path but omits its
outer death callback when `RespawnUpdate` exists (RW `0x8C4593`). Permanent death
and bodies without `RespawnUpdate` retain their distinct retail branches.

The retail Eowyn regression checks normal and temporarily defected deaths,
nonlethal controls, original-owner revival records, and exactly one death
callback before damage notifications. Red: 1 case / 47 assertions, five
behavioral failures after successful retail preconditions. Green: 1 case / 52
assertions. Related hero, structure, hero2, S-1991 and selected hero-stop
regressions: 15 cases / 2,418 assertions; the selected BACKLOG2 r1 pinned
determinism check: 1 case / 43 assertions. The full build passed. `sim_audit`
checked 992 compile commands and 1 link command without command problems,
classified 312 simulation sources (244 excluded), and reported 344 existing
violations in 17 pinned files. Its S-230 AST subcheck remains unavailable
without `libclang==18.1.1`; no pins changed. This changes simulation
death/ownership timing and hashes; no RNG calls, pinned vectors, terrain,
partition or Xfer systems are added.

## S-261 — hero bounty scaling

RW `0x68DDF1` uses the victim tracker's current rank and the last valid level
in its target-name list. `ExperienceLevelSystem::lastLevel` follows RW
`0x689045`: reverse definition order, final overrides, and MP/SP validity.
It deliberately does not choose the maximum rank. `Economy::awardBounty`
uses that source automatically when no explicit test hook is installed;
its existing PC24 divide/multiply/add/truncate sequence is unchanged.
A missing level list legitimately leaves the bounty unchanged and no longer
reports an unported stop.

The retail ordinary-kill regression reached Eowyn ranks 1, 5 and 10 before
each kill, with no bounty hook. Red: 1 case / 22 assertions, three failures:
150 was paid at every rank instead of 165, 225 and 300. Green verification
passed: the retail and new core denominator cases passed together (2 cases /
33 assertions). The related bounty, economy-report, remaining XP core, S-853
and selected BACKLOG2 determinism regressions passed (12 cases / 274
assertions). The core fixture distinguishes definition order from rank order,
checks final overrides and MP/SP filters, and retains arithmetic hook tests.
The full build passed. `sim_audit` checked 992 compile commands and 1 link
command, classified 312 simulation sources (244 excluded), and reported 344
existing violations in 17 pinned files; its S-230 AST subcheck remains
unavailable without `libclang==18.1.1`. No pins changed. Hero kills now change
money and dependent simulation hashes; no RNG calls or pinned vectors are
changed.

## S-635 — experience eligibility and horde training

RW `0x6AAFC3` / `0x8A4ED8` queries the attacker's own SlavedUpdateInterface
after attacker qualification. `dieOnMastersDeath()` vetoes the award, before
victim ownership and experience processing. The existing interface supplies
that answer; no horde/slaver lookup is substituted. This also closes S-527's
sole remaining clause. ShareExperienceBehavior's kill listener was already
implemented; unrelated scorekeeper and level-grant stops remain open.

Additional S-635 references outside the inventory identified two missing
LevelUpUpgrade paths. RW `0x8B7FFA` rejects rank >= LevelCap before delegating
to UpgradeMux::wouldUpgrade. RW `0x8B7F90` calls HordeContainInterface slot
`0xC4` once per gained level. That slot is RW `0x876C23`: contained members
in list order, then entering members in ID order, gain one level without
feedback or XP scaling. Each member's resulting experience overwrites its
template's pool; missing entering objects are skipped. The nonempty worker
ends with `bannerCheck(false)`. Existing containment, trackers and pools
supply the complete worker; no terrain/partition/Xfer dependency is added.

Retail red: **2 cases / 143 assertions, 66 failures**. Real lethal damage
from MordorFortressCatapult wrongly awarded both player skill points and
score; CaveTroll_Slaved (flag disabled) and MordorCatapult (no slave module)
were passing controls. Basic training left all contained/released members
at rank 1 / experience 1, created no pool and allowed an unexecuted upgrade
at its cap. These use retail templates and existing gameplay entry points,
not a clean-retail executable oracle.

Focused green: **2 cases / 145 assertions**. The combined XP retail/core,
ShareExperienceBehavior, SlavedUpdate, logic-stop and BACKLOG2 determinism
filter passed **25 cases / 1,631 assertions**; its MOD-4 binary-facts check
was skipped because `RW_GAME_DAT` was unset. With `RW_GAME_DAT` set to the
retail `game.dat`, `MOD-4: *SlavedUpdate*` passed **2 cases / 104 assertions**.
The full build passed. `sim_audit` checked 992 compile commands and 1 link
command, classified 312 simulation sources (244 excluded), and reported 344
existing violations in 17 pinned files. Its S-230 AST subcheck remains
unavailable without `libclang==18.1.1`; no pins changed.

Simulation impact: slave kills can award fewer player skill points; basic
training now advances member trackers and pools, and changes consequent
combat and state hashes. No RNG calls or pinned vectors are changed.

## S-1224 — speech completion: still open

Rechecked RW `0x8B068D`, `0x8B0485`, `0x8B0A0D`, the base preparation
`0x853BEC` and trigger `0x853EDF`, and object command notification
`0x693919` against the local image. The speech update releases followers,
clears its speech model condition and sleeps forever; it does not call
`finishAbility`. The port follows that sequence. Base preparation sets the
USING_ABILITY and SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING statuses.
The object command notifier conditionally aborts an in-use ability, but
this is not evidence of an automatic completion caller.

The shortlist overestimated readiness to close this stop: the cleanup
producer is still unidentified. A retail trace or recovered caller proving
when and how those statuses clear is needed before changing the lifecycle.
No speculative finish call, runtime edit, closure or new red/green result.

## S-1984 — Demolisher deployment: saved-command dependency

RW `0x8B1DB3` / `0x8B1C94` request deployment/undeployment through
`0x891D3B` / `0x891D62`. State entry `0x8921E3` handles model conditions,
deadlines, turrets and the deployed modifier. Its READY_TO_MOVE and
READY_TO_ATTACK entries can replay the stored outside command:
`0x891CAF` checks the pending flag and saved command; `0x891F9B` reconstructs
AICommandParms with `0x75315B`, clears the pending flag, and dispatches it
through AI slot `0x268`. This is an owned command including its dynamic
payload, not just a deploy boolean.

The current AI has no equivalent retained outside-command producer/owner
and replay contract (the same missing saved-command family recorded by
S-861). Porting only the toggle and model conditions would not close S-1984.
That prerequisite excludes this target from the existing-systems-only task.
The old stop's `0x491CEB..0x4921E3` are RVAs; the runtime VAs are
`0x891CEB..0x8921E3`. No runtime change, new red/green result or closure.

## S-685 — slow-death phase sound ownership and resolution

RW `0x8609A8` constructs the selected sound using the dying object's ID
(`0x6DB5B3`), not a position snapshot. LiveFX now uses the existing object
audio entry point and LiveGameAudio owner resolver. Client RNG ownership
and the selected event's player remain unchanged.

RW list parsers `0x8615FE` / `0x861672` set bits 2/4 of the shared mask
only for successful OCL/weapon lookups. Unresolved entries retain a null
list slot; they do not enable the phase and cannot become valid through a
later definition. Existing OCL creation and temporary-weapon execution
remain in place. Their separate subsystem stops are not retired here.

Retail red: the normal GondorFighter kill produced one queued death sound
with object ID 0 instead of the corpse's ID. A synthetic missing-OCL/weapon
control made two incorrect logic draws. Combined red: **2 cases / 25
assertions, 3 failures**. This is retail-data testing, not an executable
oracle.

Focused green: **2 cases / 23 assertions**. The combined FX, audio, footsteps,
tier-4 summons, slow-death helper and BACKLOG2 determinism regressions passed
**19 cases / 535 assertions**. `test_hud_decomp1_retail.cpp` has no case name
matching `*DECOMP-1*slow*`; the run included its relevant `decomp1 temp
weapon` case, whose helper is also used by the slow-death phase weapon. Full
build passed. `sim_audit` checked 992 compile commands and 1 link command,
classified 312 simulation sources (244 excluded), and reported 344 existing
violations in 17 pinned files. Its S-230 AST subcheck remains unavailable
without `libclang==18.1.1`; no pins changed.

Simulation impact: unresolved-only phase lists no longer draw logic RNG
or increment the hashed diagnostic counter. Audio attachment is client
only. No pinned vectors are changed.

## S-481 — upgrade EVA gain/loss dispatch

RW `0x6AE483` / `0x6AE546` notify the player's objects before reporting
the non-silent upgrade cue. The local owner uses the local gain/loss field;
otherwise the local player's relationship to the owner's default team
selects allied or enemy (neutral also selects enemy). No local player or
no default team leaves the event at None. All six existing parsed fields
now reach Eva through the audio-owner queue, without a world position.
Initial upgrades report only when the player already has a world host;
the pre-world boot does not replay presentation events later.

Retail red: **1 case / 77 assertions, 24 failures**, over local, allied,
enemy and neutral relationships. Actual Upgrade_RingHero gain fields were
verified against the retail Eva table. Retail ring-loss fields are None;
a controlled copy uses known retail event IDs to exercise the dormant
loss branch, including worker-to-audio-owner delivery. Silent grants and
removals, incomplete research and no-position reports are covered.
This uses retail data, not a clean-retail executable oracle.

Green: `STOPS-B1 S-481*` passed (**1 case / 77 assertions**); the combined
upgrade, EVA, audio3, smooth1 r4 and BACKLOG2 determinism regressions passed
(**29 cases / 1,104 assertions**). The full build passed. `sim_audit` checked
992 compile commands and 1 link command, classified 312 simulation sources
(244 excluded), and reported 344 existing violations in 17 pinned files.
Its S-230 AST subcheck remains unavailable without `libclang==18.1.1`; no
pins changed. No simulation fields or RNG calls are changed; the old
unplayed counter was not hashed. The announcer's independent scheduling stop
S-247 remains open.
