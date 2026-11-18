# War of the Ring battle audit

Edition scope: **RotWK only** for strategic battle behavior. BFME1 keeps its
existing RotWK-rules-on-BFME1-data mode (S-5017). The BFME2 developer data mode
retains the existing phase-0 RotWK-rules contract (`docs/EDITIONS.md`); its native
strategic startup remains blocked by S-1321. The known edition differences below
are not a claim of BFME2 strategic parity. Shared parser checks are listed with
the validation below.

The audit uses the installed RotWK image, MD5
`daed3668006cd90f01c34e5a7da1901f`, and the clean BFME2 1.06 image, MD5
`34391b253b0ee12d6cec1178f46d3bf9`. **S-001 still applies:** the available
RotWK image has community modifications. Static instruction observations below
are evidence from that image, not proof of an independently clean 2.01 executable
or a comparison with a running original game. Open-BFME-2 is a reading aid;
the BFME2 executable remains its authority.

## Army membership at creation

RotWK registers trained units with the producer's army at RW
`0x8A277C..0x8A2789`. The hero-revival branch skips this registration and retains
the hero record's army. Structures inherit their builder's army through the plot
path (RW `0x858B43..0x858B55`), dozer phantom path (RW
`0x88D40A..0x88D41C`), and immediate BuildAssistant path (RW
`0x7978BE..0x7978CB`). Registration goes through RW `0x625E0A` to
`0x80FD9C`; it copies the army id, not the entry spawn id or region-building id.
The paired BFME2 registration is `ArmySummarySystem::rva0040D280`
(BFME2 `0x80D280`, `Common/Rva0040D280Set.cpp`); production's paired body is
`ProductionUpdateQueue.cpp`, BFME2 RVA `0x49E3B5`.

The port now records those army ids when construction or normal production
creates the object. The original-data regression constructs a barracks from a
tagged builder, trains a fighter horde, and reads the tactical result: the horde
returns to army 701 rather than the unrelated garrison 702. It also checks that
spawn ids and strategic building ids are not inherited.

This does **not** establish every creation path. OCL, replacement, summoning,
horde-member and other module-specific creation sites still require an individual
source audit. The untagged-object-to-garrison behavior remains an explicitly
reported S-1329 inference for those paths.

## Too many sides for the map

The stop's old statement that RW `0x62D7EA` had not been read is obsolete.
The inspected routine is the ordinary `populateRandomStartPosition`; the paired
BFME2 body is BFME2 `0x64485C`, `System/GameLogicInit.cpp`. Neither inspected
body establishes a third-side sharing rule.

The Living World slot setup RW `0x6498AE..0x6498CC` calls the region helper
RW `0x7F104D`: the region owner gets start index 0 and other players get -1.
At RW `0x62DA1E` the free-start search initializes its candidate to -1, skips
every taken position, and writes the result at RW `0x62DB2E`. If all positions
are taken it remains -1; the subsequent indexed writes are not a usable
placement specification. BFME2's donor has the same exhaustion boundary.

Consequently **S-1342 remains open**. The port's named refusal and LAN
auto-resolve notice are a deliberate containment policy, not a claim that retail
does the same. The existing allied sharing policy is also still labeled an
S-1329 inference. Closing this stop needs a trace of original-game setup and
placement for an actual over-capacity battle, including whether another caller
prevents or transforms that battle before this routine executes. Inventing an
extra start position or copying an allied placement rule to hostile sides would
not resolve it.

## Spawn-point selection

RW `0x80F754..0x80F9FF` selects army anchors from the strategic battle's original
side/player counts and region ownership. A two-side battle with one player per
side uses the farthest valid connected-region gather-point pair for a neutral
region; a friendly owner uses start 0 initially and defender reinforcement points
on call-in; a hostile owner uses the gather point farthest from start 0's gather.
Other battles first try home/origin placement. RotWK then retries with defender
reinforcement points or the farthest hostile point before finally trying start 1.
That retry (RW `0x80F8CB..0x80F8FC`) is absent from the paired BFME2
`ArmySummarySpawnPositions.cpp`, BFME2 RVA `0x40CCC6`.

Pair helpers RW `0x781A85` / `0x781C9F` require both spawn and gather. The start
helper RW `0x781B17` uses `Player_N_Start_Rally`; without it, gather keeps the
original start and spawn moves +10 in x. The farthest selectors RW `0x7824FD` /
`0x781E6E` compare gather coordinates, keep the last equal candidate, and differ
on zero distance: a valid zero-distance pair succeeds, whereas farthest-from-start
requires a positive distance. Initial non-start placement moves the anchor to
gather and faces along the old spawn-to-gather direction (RW
`0x80F907..0x80F9C6`).

The port now carries this placement context through the strategic-to-tactical
handoff and native replay format 14 (format 13 introduced placement context;
14 adds revival ranks). Format 12 remains readable with its reported legacy
placement when hero-rank data is not needed. Reinforcement snapshots retain the chosen waypoint names.
Synthetic selector tests distinguish spawn distance from gather distance and
cover missing pairs, ties, owner-dependent retries, and the final start fallback.
Original Rohan map checks pin the independently read Helms Deep/Brown Lands
neutral pair and Gap of Rohan hostile point; a started battle exercises call-in.

Native replay/dictionary spawn plans reject more than 64 connections and exact
duplicate connection names before placement. Direct native startup and selector
calls enforce the same rule. This is an explicit OpenBFME input policy, not a
retail graph limit: it bounds neutral placement to 2,016 unordered pairs per
plan without silently truncating or deduplicating the connection order. Waypoint
lookup remains case-sensitive. A malformed plan is an error, never a reason to
try another placement method.

**Formation layout remains open:** the four-abreast offsets and individual
reinforcement move orders are still S-1329 approximations. Reading anchor
selection does not establish `WalkOnTemplate` formation rotation, optimization,
airborne height or group movement. The existing `FormationAssistant::layout`
already reports an inferred layout and cannot supply missing retail proof.

## Rank, garrison ties and retreat eligibility

Living World battles use GameData `InitialMaxRingLevel`, not the normal player
template's single-player cap (RW `0x7827D9` -> `0x6B34EE`; BFME2
`SciencePurchasePointTracker` RVA `0x38030D` and selector RVA `0x2B2AFD`). The
constructor default is 2; all three checked installed GameData files set it to
60. The port passes this cap before constructing the tactical players, and saves
and hashes it. Older active battle snapshots that lack the cap must be refused
explicitly rather than silently using the single-player rule.

Garrison consumption sorts through STLport introsort plus final insertion sort
(RW `0x6BCA3C` -> `0x6BC6BA`, `0x6BC19C`, `0x6B9D4C`;
BFME2 `ConsumeGarrisonUnitsFromRTS.cpp`, RVA `0x2BBD52`). The previous stable
sort preserved ties that retail can reorder. A twenty-entry regression checks
which equal entry transfers to a hero army, rather than only checking the sort
helper in isolation.

Retreat eligibility's elimination check differs by edition. RotWK
`0x7F6789` calls `0x6B68F3`, which tests the applicable team's
`LoseIfCapitalLost` condition before comparing the player's home region.
BFME2 RVA `0x2B5CFB` compares the home region unconditionally. Neither inspected
function forbids retreat merely because the player owns only one region. The
port removes that invented last-region test and respects the selected victory
type and condition's team list.

Strategic revival records now preserve rank and base rank, including zero,
instead of replacing them with rank one during tactical handoff. RW `0x6E42DA`
copies records through `0x6E267E`; RW `0x6E2E4E` initially copies summary rank
to both record fields. BFME2 `LivingWorldPlayerCreateRevival.cpp`, RVA
`0x2E1AA0`, follows the same structure. Strategic version 9 and replay version 14
preserve these fields. A fresh unbought hero uses the existing template-initial
constructor; an old dead/experienced record without its ranks is refused by
name. This does not close the constructor's other S-852 limitations.

## Known native save and replay compatibility limits

These are deliberate named refusals, not a promise that every older native file
loads:

- Strategic saves before version 9 and battle replays before version 14 do not
  contain revival rank/base rank. Fresh, unbought hero records still load;
  strategic records retain their original hash stream. Dead or experienced records are refused because
  their missing ranks cannot be reconstructed from experience alone, including
  the valid rank-zero case.
- Active tactical snapshots whose PlayerScience block lacks the Living World
  rank ceiling are refused. Ordinary skirmish/campaign snapshots keep their
  existing version-1 layout and hash stream.
- Battle replay version 12 has the old non-empty army layout, including named
  spawn/gather pairs, but no placement plan. It remains readable with the
  reported S-1329 legacy placement; version 13 adds the placement plan. Both
  versions are subject to the missing-hero-rank restriction above.

## Allied territory awards

RW `0x6BBA1C` awards to the winning side's active player records, preserving an
allied region owner. With several claimants, AI-only disputes use the first
claimant (RW `0x6BB9FB`); human disputes open a choice (RW `0x6BA65D`).
`GetResolvableBy` RW `0x900E58` selects the only human claimant, or the host
when several humans claim and the host participates. It returns -1 when no
eligible host participates. The paired BFME2 sources are `OnBattleComplete`,
`RegionAwardDispute.cpp` RVA `0x4FBED6`, and
`RegistryTransferRva002BAF01.cpp`; resolution is RW `0x6BB99F`.

The port saves and hashes pending choices and routes the recipient through
MSG1719 with sender authorization. A small native popup exposes the choice;
retail APT presentation remains S-1334. Retreat preparation waits for all awards
to settle so it observes the final ownership. A withdrawn player is removed
from active participants (RW `0x7F806F` -> `0x7F7496` / `0x7F7414`, paired
BFME2 side RVA `0x3F5E3F`) and cannot respawn or claim an ally's subsequent win.
The original attacker/defender labels remain historical after a retreat; the
tactical player list filters them through those active participants, as do its
garrisons and player starts. Regressions withdraw each primary role while its
ally keeps fighting, then save/load and continue the battle result.

S-1324 still covers the port's phase-4 ownership timing, unresolved absent-host
case, and explicitly reported legacy host mapping. If a claimant alliance
surrenders while an award is open, the port abandons its claim and leaves the
existing owner unchanged, with an `unverified` S-1324 report. Retail's exact
surrender-time dispute cleanup has not been established; this containment
prevents a departed player from blocking every survivor's next turn and is not
presented as a retail ownership rule.

## Defeat and retreat acceptance boundaries

`LoseCampaignIfLoseMap` is not a post-survivor team-elimination rule. In the local
tactical DEFEAT callback RW `0x62679C`, the region flag (via RW `0x6B4061`)
chooses `clearGameData(false, true)` at RW `0x7792BC` instead of the normal
`LivingWorldTacticalBattleComplete` / roster-saving path at RW `0x626662`.
The paired BFME2 callback `GameLogic.cpp` RVA `0x23DA4E` has the same branch
through the flag accessor RVA `0x2B3669`. The normal `BattleResult` winner alone
cannot express this: the local player can lose while an ally wins. Real-time
launch for a flagged region therefore has a named S-1329 refusal until the actual
local defeat outcome and client campaign-exit route are represented.

For retreats with several friendly destinations, RW `0x6BABA8` leaves the army
in the retreat list. Selection RW `0x6B6657` / `0x6B568C` accepts hero-named
armies; it does not establish how a human's surviving heroless army gets a choice.
The port's automatic first-connection choice was unsupported and is removed.
That case remains in the list with a named S-1329 report and cannot silently end
the phase. BFME2's extra home/nearest escape for zero exits is absent from the
inspected RotWK path and is not imported.

## Remaining verification

The local original-data run passed 151 cases / 5,808 assertions, including
tactical battle completion, reinforcement call-in, survivor capture, strategic
save continuation, the Rohan anchor checks, production and rank behavior. Five
online lobby end-to-end cases explicitly skipped because their service fixture
is absent. The shared parser was checked against all three installs; BFME1's
object startup loaded 2,686 templates with zero errors. The focused new
regressions passed 23 cases / 319 assertions.

The complete unit run without game files passed 5,651 cases and failed five
unchanged MP3-fixture cases: local fixture generation does not match the
committed reference golden data. The core numeric audit passed (flags, link,
classification and AST); the modified Godot handoff sources compiled against
fresh 4.7 bindings, and the new choice UI passed five headless Godot 4.7.2
checks. This is not a full GDExtension rebuild or a windowed battle comparison.

Compilation and public CI do not supply original-data runtime evidence, and
the mounted-data checks above are not original-executable comparisons. Full
ArmyPlacer formation layout, unidentified creation paths, quit-menu aftermath,
and over-capacity original-game setup remain separate acceptance boundaries
until their sources and expected results are established.
