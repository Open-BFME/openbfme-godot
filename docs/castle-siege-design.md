# Castle / siege map implementation design

> **Status:** design blueprint. No code was written and no runner was executed for this
> document. Every feasibility grade is an engineering estimate.
> **Author lane:** `opus-castle-design` (branch `opus-castle-design`, base `6051250`)
> **Date:** 2026-08-11
> **Retail oracle:** `.private/retail-work/editions/rotwk/cache/effective-assets` (pure 2.01).
> The contaminated layered tree was never opened.
> **Secondary oracle (shared SAGE only):** `.private/scratch/opensage-source`. Not authority
> on BFME horde/wall behavior.
> **Predecessor:** `.private/orchestration/fable-wave/report-castle-maps.md`

## 1. Problem statement

Ten retail castle/siege maps are admitted to the skirmish catalog but refuse to launch.
`game/src/retail_slice/retail_map_data.gd:28-34` seals a `castleSiege` contract naming five
blockers, applied uniformly to all ten maps:

```gdscript
const CASTLE_SIEGE_BLOCKERS: Array[String] = [
	"walkable-walls", "defendable-gates", "wall-garrisons",
	"wall-mounted-defenses", "skirmish-ai-libraries",
]
```

This document specifies what retail actually does per blocker, the minimal faithful
implementation here, and an ordered lane decomposition.

### 1.1 Finding A — the shipped pack does not yet carry the contract

The admission lane's work has **not** reached the mounted pack. All ten castle maps in
`.private/content-packs/rotwk-skirmish-maps-private/` currently have
`"category": "wotr-battle"` and **no `castleSiege` key**; `content_db.gd:2518`
`LOBBY_MAP_CATEGORIES := ["skirmish"]` therefore excludes them from the lobby entirely.
`castle_map_admission_runner.gd` injects the contract synthetically over immutable
documents (its own header says so), and it has **no invocation anywhere in the repo** —
no `.ps1`, `.bat`, `.yml`, or `.py` runs it.

So "visible in the map list but refusing to launch" describes the branch, not the shipped
build. The first republish after the admission merge is what makes these maps appear at
all, and orchestrator note N6 in the predecessor report warns map-ids will churn on that
republish. **Any lane must confirm `data/maps.json` for the ten rows before believing
anything about lobby state.**

### 1.2 Finding B — the blockers are not uniform across the ten maps

One five-blocker seal for all ten maps is over-broad. Resolving every map-placed
`typeName` against a retail object index (`Object` **and** `ChildObject` forms with parent
inheritance resolved: 3867 objects, 569 of them `ChildObject`) and joining against each
compiled map's `objects.json` gives placement counts per map:

| Map | gates | garrison (real) | walk-on-top | scaleable | wall-mounted |
|---|---:|---:|---:|---:|---:|
| Minas Tirith | 1 | 0 | 99 | 0 | 14 |
| Helm's Deep | 2 | 0 | 36 | 0 | 0 |
| Erebor | 1 | 6 | 5 | 0 | 0 |
| Isengard | 1 | 0 | 19 | 0 | 0 |
| Black Gate | 0 | 0 | 6 | 0 | 0 |
| Dol Guldur | 5 | 0 | 6 | 251 | 16 |
| Grey Havens | 0 | 9 | 0 | 0 | 0 |
| Minas Morgul | 5 | 0 | 34 | 0 | 0 |
| Carn Dum | 5 | 0 | 0 | 0 | 39 |
| Fornost | 5 | 1 | 0 | 0 | 0 |

Detection keys: gate = `BLOCKING_GATE`/`WALL_GATE`/`GateOpenAndCloseBehavior`; garrison =
a real `*Contain` module (**not** the `GARRISON` KindOf — see the trap below); walk-on-top
= `WALK_ON_TOP_OF_WALL`; scaleable = `SCALEABLE_WALL`; wall-mounted =
`WALL_UPGRADE`/`WALL_HUB`.

**Two traps that produced wrong numbers during this very analysis, both worth stating so
the implementation lane does not repeat them:**

1. **The `ChildObject` keyword.** A first pass ignored it and reported Carn Dum as having
   **zero** gates and zero wall-mounted defenses. It has five and thirty-nine.
   *Mechanism, precisely:* Carn Dum's four castle types are declared
   `ChildObject <name> <parent>` at `evilfaction/structures/angmar/angmarcastlewalls.ini`
   lines 2612, 2686, 2722 and 2780, and each **re-declares its full `KindOf` line
   directly**. No parent-inheritance resolution is required for these — the index only has
   to *recognize the `ChildObject` keyword as a definition site*. A lane that resolves
   inheritance but never registers `ChildObject` names still reports zero. (Resolving
   parents remains worthwhile for other families, but it is not what fixes Carn Dum.)
2. **`GARRISON` KindOf does not mean garrisonable.** A first pass counted Erebor at 8
   garrison objects; the real figure is 6. `Erebortower2`
   (`data/ini/object/civilian/ereborbuildings.ini:4928`) carries `GARRISON` **and** a
   5-slot `EreborGateTowerCommandSet` of `Command_ExitGarrison`, but authors **no contain
   module at all** — it can never hold anyone. This is a retail defect; replicate it, do
   not "fix" it. Detection must key on the contain module, not the KindOf.

Consequences that drive the plan:

- **Black Gate needs the least.** No gates with gate modules, no garrisons, no
  wall-mounted defenses.
- **Grey Havens needs only garrisons.**
- **Walkable walls — the hardest blocker — are needed by four maps.** Three need zero.
- **Dol Guldur's 251 `SCALEABLE_WALL` pieces** are a distinct capability (siege ladders
  dock and climb) that the original five-blocker list never named.

The first lane is therefore not a gameplay lane: it is replacing one global seal with a
per-map derived requirement set, so maps ship as capabilities land.

### 1.3 Finding C — the unlisted prerequisite

Four of the five blockers are gated behind something the blocker list never mentions:
**map-placed walls, gates and towers are currently classified `bound / renderable` — static
decorative meshes with no sim entity, no health, no targeting, and no collision.**

Verified in the shipped pack's `object-bindings.json`: Carn Dum's 125 `AngmarWallCarnDum`,
24 towers, 10 wall catapults and 3 gates are all `bound / renderable`; Minas Tirith's ~166
wall/gate objects likewise. Only `lifecycle-structure` bindings get an `object_id` into the
content DB (`retail_map_data.gd:1515-1521`), and today that is used only for Inns and creep
lairs.

Worse, `retail_map_data.gd:978-1009` (`_load_objects`) reads only `typeName`, `index`,
`godotPosition`, `sagePosition`, `godotYawRadians`, `roadType`. `grep -n "properties"` on
that file returns **zero hits** — the authored ownership and health data is dropped at the
front door, even though it is already on disk.

This prerequisite is lanes L2a/L2b below and it is the largest single piece of work after
walkable walls.

## 2. Retail semantics, per blocker

### 2.1 defendable-gates

`GateOpenAndCloseBehavior` is the only door module in retail: 33 occurrences, 32 live.
Canonical block, `data/ini/object/goodfaction/structures/men/campsandcastles.ini:5731`
(`Object MenWallGateSmall`):

```
Behavior = GateOpenAndCloseBehavior ModuleTag_GATE
    ResetTimeInMilliseconds = 3000   //important! this must be longer than the gates animation, ir it will twitch
    OpenByDefault           = No
    PercentOpenForPathing   = 50
    SoundOpeningGateLoop    = GateOpenStart
    ...
End
Behavior = FakePathfindPortalBehaviour ModuleTag_FAKEPATHFIND
    AllowEnemies            = No
    AllowNonSkirmishAIUnits = No
End
Behavior = AIGateUpdate ModuleTage_AIGateUpdate
    TriggerWidthX = 300.0
    TriggerWidthY = 150.0
End
```

Precise semantics, several of which contradict the obvious guess:

- **There is no open/close duration field anywhere in retail.** Duration is the W3D
  animation length. `ResetTimeInMilliseconds` is the auto-*reset-to-default* timer, and
  every retail comment insists it must exceed the animation. Values: 3000 (faction wall
  gates), 5000 (`EreborGateDoors`), 10500 (`MinisGateDoor`), 12200 (`GBMGateDoor`, both
  Helm's Deep doors).
- **The auto-trigger is an axis-aligned rectangle, not a radius** — `AIGateUpdate
  TriggerWidthX/Y`. Only two values tree-wide: `450.0 x 225.0` on map gates, `300.0 x
  150.0` on buildable faction gates.
- **Ally-only lives on a different module**, and it is **two distinct rules**, uniform
  across every gate:
  - `AllowEnemies = No` — hostile units may not path through; they must destroy the gate.
  - `AllowNonSkirmishAIUnits = No` — a *separate* restriction on which **friendly** units
    may use the portal, gating traversal on the unit being skirmish-AI-controlled. This is
    the rule **L7 depends on**: AI armies routing out through their own castle gate pass
    this test, and anything the pipeline fails to mark as a skirmish-AI unit will silently
    fail to path through a friendly open gate. Conflating the two rules will produce an AI
    that builds an army and never leaves its castle.
- **`OpenByDefault` splits map gates from built gates.** Map/scenario gates idle **open**
  (`Yes`: both Helm's Deep doors, `MinisGateDoor`, `EreborGateDoors`); player-built faction
  gates idle **closed** (`No`). Castle-map gates therefore start open.

**Pathing switch = named collision volumes.** `EreborGateDoors`
(`data/ini/object/civilian/ereborbuildings.ini`):

```
Geometry = BOX  GeometryMajorRadius = 130.0  GeometryMinorRadius = 7.5  GeometryHeight = 140
GeometryName = Closed
AdditionalGeometry = BOX  7.5 / 60 / 140   GeometryOffset = X:-115 Y:68 Z:0   GeometryName = OpenLeft
AdditionalGeometry = BOX  7.5 / 60 / 140   GeometryOffset = X:115  Y:68 Z:0   GeometryName = OpenRight
```

At `PercentOpenForPathing` the engine hides `Closed` and shows `OpenLeft`/`OpenRight`.
Unnamed pillar boxes never swap. OpenSAGE independently confirms the names are a hardcoded
contract — `GateOpenAndCloseBehavior.cs` holds `_closedGeometry = "Closed"` and
`_openGeometries = { "OpenLeft", "OpenRight" }`, calling `HideCollider`/`ShowCollider`.
(OpenSAGE caveats: it parses `OpenByDefault` then discards it; `AIGateUpdate` is
ModuleData-only so auto-open is unimplemented; its `Toggle()` is called directly from a
command callback marked `//TODO: proper toggle gate order`, i.e. lockstep-unsafe. Do not
copy its command path.)

**Empirical:** sampling compiled Erebor `impassability.bit` (600x600, 10 units/cell,
`meaning = one-is-impassable`) in a 13x7 window around the gate cell (358, 368) returns
**all passable**. Retail bakes the gateway open in terrain and relies entirely on the gate
object. With no gate logic, castle maps let units walk straight through every gate.

**Death has three different outcomes and this drives post-breach pathing:**

1. `KeepObjectDie` — object and geometry persist, repairable (`RebuildTimeSeconds = 40`,
   `Command_StartSelfRepair`). Used by all the map gates: Helm's Deep, `MinisGateDoor`,
   `EreborGateDoors` (`CollapsingTime = 10000`). **No rubble object is spawned** — rubble
   is a second Draw module on the same object.
2. `SlowDeathBehavior DestructionDelay = 5000` alone — object deleted, breach permanent
   (`IsengardCastleWallGate`, `MenWallGateSmall`).
3. `StructureCollapseUpdate DestroyObjectWhenDone = Yes` — removed, no rubble
   (`Object HelmsDeepGate`).

Locomotors govern breach traversal: `Surfaces = GROUND RUBBLE` is near-universal, but
`locomotor.ini:1328` notes `Surfaces = GROUND ; note - huge creatue (mumakil) cant path
through RUBBLE, ie broken gates & buildings.`

Armor: `DefaultWallArmor` (`armor.ini:2806`) DEFAULT 10% / SIEGE 100% / PIERCE 1% /
FLAME 1% / FROST 30% / STRUCTURAL 25%. **Trap:** `Armor HelmsDeepGates` exists
(`armor.ini:2893`) but **neither Helm's Deep gate uses it**. That is retail; do not fix it.

Battering rams use `GeometryContactPoint ... Ram` tags (`GBMGateDoor`, both Helm's Deep
doors, `minasmorgul`, `ministirith`; commented out on `EreborGateDoors`).

**Animation authoring has the same split-vs-embedded trap that bit the structure lane.**
Helm's Deep `RBHelmsDeepGateDoorBig` uses one clip played forward and `ONCE_BACKWARDS` with
motion in `TransitionState`s selected by inline Lua; `IsengardCastleWallGate` and
`MenWallGateSmall` use four separate clips (`_OP`, `_OPN`, `_CL`, `_CLS`). A converter
binding only two of four produces a static gate.

**Not every "gate" is a gate.** `MordorBlackGateLeft/Right`
(`data/ini/object/evilfaction/structures/mordor/blackgate.ini:1013/1141`) have **no**
`GateOpenAndCloseBehavior`, no `BLOCKING_GATE`, no named geometry — they open via map
script through `AnimationState = USER_1`/`USER_2` and are `UNATTACKABLE`. Likewise
`IsengardCastleGate1/2` are decorative. Also `EreborMainGate` is the static arch
(`objectIndestructible: true` in the map); the operable object is `EreborGateDoors`.

Ownership is authored per placement, from compiled `maps/erebor/objects.json`:

```json
{"typeName": "EreborGateDoors",
 "properties": {"originalOwner": "Player_1/teamPlayer_1", "objectEnabled": true,
   "objectIndestructible": false, "objectInitialHealth": 100, "objectTargetable": false}}
```

### 2.2 wall-garrisons

**Hard negative first: no wall segment in retail is garrisonable.** No garrison module on
any Minas Tirith wall, Helm's Deep gatehouse, Isengard wall, Erebor wall, Grey Havens wall
or Black Gate piece. The `; buildings automatically stick around because GarrisonContain
has it's own DieModule` lines appearing ~30x in `ministirithbuildings.ini` are
**copy-paste comments, not modules**. The blocker name is misleading: this is
*tower* garrison.

Module census (live `Behavior =` lines): `HordeContain` 120, `CitadelSlaughterHordeContain`
28, `HordeGarrisonContain` **18** (matching section 3.9's declaration-site count; an
earlier draft said 26), `HorseHordeContain` 20, `TransportContain` 15,
`HordeTransportContain` 8, `TunnelContain` 4, `SiegeEngineContain` 4,
`ProductionQueueHordeContain` 3, `GarrisonContain` **2**, `OpenContain` 0 (all commented).

The real module is `HordeGarrisonContain` and **`ContainMax` counts hordes**.
`data/ini/object/civilian/ereborbuildings.ini:5279` (`EBGarrisonableTower`):

```
Behavior = HordeGarrisonContain ModuleTag_hordeGarrison
    ObjectStatusOfContained = UNSELECTABLE CAN_ATTACK ENCLOSED
    ContainMax              = 3
    DamagePercentToUnits    = 0%
    PassengerFilter         = GENERIC_FACTION_GARRISONABLE
    AllowEnemiesInside      = No
    AllowNeutralInside      = Yes
    NumberOfExitPaths       = 1
    PassengerBonePrefix     = PassengerBone:ARROW_ KindOf:INFANTRY
    EntryOffset / EntryPosition / ExitOffset ...
    KillPassengersOnDeath   = No
End
```

`GHGarrisonableTower` (`greyhavenbuildings.ini:2171`) differs: `ContainMax = 1`,
`AllowNeutralInside = Yes ; Cause this structure is normally neutral.` Command sets match
capacity (`DormitoryCommandSet` = 3x `Command_ExitGarrison`;
`NeutralBattleTowerCommandSet` = 1x).

`PassengerFilter` resolves through `gamedata.ini:80`:
```
#define GENERIC_FACTION_GARRISONABLE  ANY +INFANTRY +BANNER -CAVALRY -SUMMONED -WildSpiderling
    -WildSpiderlingHorde -COMBO_HORDE -IsengardSharku -AngmarThrallMaster
```

**Fire-from-inside is not a field.** It is `ObjectStatusOfContained = ... CAN_ATTACK` plus
`PassengerBonePrefix = PassengerBone:ARROW_ KindOf:INFANTRY` binding passengers to public
bones. `EBGarrisonableTower` declares `ExtraPublicBone = ARROW_01 … ARROW_12` and has **no
WeaponSet of its own** — all offense comes from the garrison. The field is
`DamagePercentToUnits = 0%`; `PassengerDamage` does not exist. No `ExitDelay` on any
garrison tower.

**Retail's "garrisoned wall tower" for player-built walls is a fake garrison.** In
`gondorbuildings.ini` (`Object GondorCastleUpgrade`) the `HordeGarrisonContain` is
**commented out** and shipped instead as `WeaponSetUpgrade` +
`ArmorUpgrade ArmorSetFlag = AS_TOWER` + `GeometryUpgrade` on `Upgrade_OpenGarrison`. This
is a materially cheaper implementation target that matches what players see, and it is the
retail-faithful answer for wall plots.

Cross-check: OpenSAGE models none of this. `HordeGarrisonContain` is ModuleData-only with
no `CreateModule`; `IContainModule` is implemented by nothing, so `GameObject.Contain` is
permanently null.

### 2.3 walkable-walls

The mechanism is not geometry — it is named sub-meshes on the Draw module plus KindOf
flags. Tree-wide census: `WallBoundsMesh` 199, `RampMesh1` 113, `RampMesh2` 57,
`RaisedWallMesh` 12. Example, `helmsdeepbuildings.ini:1875`
(`Object HelmsDeepGatehouseLeft`):

```
WallBoundsMesh  = P1
RampMesh1       = P2
RaisedWallMesh  = P3
```

**Two KindOf flags that must not be conflated:**

- `WALK_ON_TOP_OF_WALL` — units path along the top.
- `SCALEABLE_WALL` — siege ladders can dock and climb it.

`MenWallSegmentSmall` has `SCALEABLE_WALL` but **not** `WALK_ON_TOP_OF_WALL` — climbable,
not walkable. `GondorCastleWallSegment` has both. Dol Guldur's 251 scaleable pieces are a
capability the original blocker list never named.

Three separate systems get units up there:

1. **Authored ramp objects** — `MenWallRamp`, `HelmsDeepdwStairsA1/A2`, `EreborStair*`,
   `MordorCirithUngolStairA-D`, `ForBRamp1/2`. Notably `HelmsDeepRampartInvisible`
   (`helmsdeepbuildings.ini:3248`): `KindOf = STRUCTURE IMMOBILE WALK_ON_TOP_OF_WALL
   NOT_AUTOACQUIRABLE NO_COLLIDE` with `RampMesh1 = P01` — an invisible, non-colliding
   pure-navigation connector.
2. **Siege ladders/towers via `DynamicPortalBehaviour`** — a waypoint graph welded onto the
   wall (`isengard/siegeladder.ini:302`): `BonePrefix = Ladder`, `NumberOfBones = 4`,
   `WayPoint Index:n Type:PreClimb|Climb`, `Link From:0 Via:4 Via:5 To:3`, `AboveWall = 3`,
   `ObjectFilter = ANY +CAN_USE_SIEGE_TOWER`. Wall side: `SiegeDockingBehavior`
   (parameterless, 35 sites; dock points from bones such as `ExtraPublicBone = SIEGELADDER`).
3. **Wall-scaling locomotors** — `locomotor.ini:2967` `WallScalingMeleeHordeLocomotor`:
   `ScalesWalls = Yes`, `ZAxisBehavior = SCALING_WALLS`, `Surfaces = GROUND RUBBLE OBSTACLE`.
   Plus `CAN_CLIMB_WALLS` KindOf (goblins, Shelob, corsairs, CAH).

Important negatives:

- **Erebor's walls are not walkable.** `EreborWall01` is plain `STRUCTURE IMMOBILE`, and a
  sub-object is explicitly `IMMOBILE UNATTACKABLE INERT ; note: INERT is important
  otherwise things will collide`. Erebor stairs carry no `WallBoundsMesh` either.
- **Grey Havens walls are decoration with a deliberately nulled collider.** `GBGHWallA`
  (`greyhavenbuildings.ini:332`) has `InactiveBody`, no death module, and a 5/5/10 stub box
  with the real 90/115/180 volume commented out immediately above.
- OpenSAGE gives nothing here: `WALK_ON_TOP_OF_WALL` exists only as an enum declaration
  (`ObjectKinds.cs:181-182`); navigation is a single-layer 2D grid
  (`Navigation.cs:126-156`); `GeometryUpgrade.cs` carries
  `//TODO: WallBoundsMesh, RampMesh1, RampMesh2 ... e.g. P4 where is that defined?`.

**Largest remaining unknown:** whether `P1`/`P2`/`R1` exist as real sub-meshes in the W3D
assets and survive W3D-to-GLB conversion here. Neither research lane opened a `.w3d`. L6
must verify this first — if the meshes are dropped in conversion, L6's cost rises sharply.

### 2.4 wall-mounted-defenses

Retail uses three distinct mechanisms, all on the plot/segment:

**(a) Spawn a slave object on the wall top** — `ObjectCreationUpgrade` + `SlaveWatcherBehavior`
(`men/campsandcastles.ini:6586`, `MenWallTrebuchetSmall`): `ThingToSpawn =
GondorTrebuchetWall`, `Offset = X:0 Y:0 Z:51.0` (wall-top height), with `SlaveWatcherBehavior`
re-enabling the purchase button when the slave dies. Slave carries `SlavedUpdate
{ DieOnMastersDeath = Yes }`. Minas Tirith's variant is
`OCL_MinisWallBTTrebuchetUpgrade` (`objectcreationlist.ini:7125`) at `Z:148.0`.

**(b) The object becomes a tower** — `ReplaceSelfUpgrade` with five mutually-exclusive
replacements (hub / gate / postern gate / tower / trebuchet), each `ConflictsWith` the
others. The resulting `GondorCastleWallTower` mounts the weapon directly
(`WeaponSet { Weapon = PRIMARY CastleWallUpgradeBow }`, `KindOf = ... WALL_HUB CAN_ATTACK
WALK_ON_TOP_OF_WALL`).

**(c) In-place geometry/weapon swap** — `GondorCastleUpgrade`: `GeometryUpgrade
ShowGeometry/HideGeometry` over `TowerGeom`/`TrebGeom`/`DummyGeom`, `ArmorUpgrade`,
`AttributeModifierUpgrade PreventFromShooting` during build-up. Note it **re-tags the walk
surface live**: `GeometryUpgrade ... WallBoundsMesh = P2  RampMesh1 = P3`.

Postern gates are a **pathing tunnel**, not a swinging door: `DynamicPortalBehaviour` gated
on the upgrade (`gondorbuildings.ini:1962`, `Type:Walk` waypoints,
`ObjectFilter = POSTERNGATE_ALLOWABLE_OBJECTFILTER`).

Maps needing this: Carn Dum 39, Dol Guldur 16 (`DoGoldurWallCatapultSmall`,
`DoGoldurWallTowerSmall`), Minas Tirith 14 (`MinisWallAUpgrade`, `MinisWallBTUpgrade`).

### 2.5 skirmish-ai-libraries — the weakest blocker of the five

**Retail intends these maps to be played as skirmish with AI. Three independent oracles agree.**

**(i) The auto-generated multiplayer registry**, `maps/mapcache.ini` (header: *"This INI file
is auto-generated - do not modify"*): all ten castle maps are `isMultiplayer=yes` with
their authored player counts. The near-homonym *campaign* maps in the same file —
`maps\map ang carn dum\` and `maps\map ang fornost\` (no `wor`) — are correctly
`isMultiplayer=no, numPlayers=1`. The `wor` variants are the skirmish versions.

**(ii) — WITHDRAWN.** An earlier draft cited "eight lobby slots with `isComputer = true`
and `loadAIScript = true`" as authorial intent. It is not: the
`(isHuman, isComputer, loadAIScript) = (1,1,1) x 8` signature is **identical on all 128
maps in the corpus**, including the shell map, the Create-A-Hero map and every cinematic.
It is a WorldBuilder default. Do not cite it. The argument stands on (i) and (iii) alone.

**(iii) Five of the ten tune skirmish AI in their own `map.ini`**:
```
SkirmishAIData TheSkirmishAIData
    AnyTypeTemplateDisabledSlots = 1
End
```

The starting-porter evidence is narrower than an earlier draft claimed and is corrected
here: **exactly two maps — Helm's Deep and Minas Tirith — re-point starting porters, with
seven `PlayerTemplate` blocks each** (not "every faction on five maps"). Erebor, Grey
Havens and Dol Guldur author **zero** `PlayerTemplate` overrides. Two maps hand-tuning
seven factions' skirmish start positions is still strong evidence of skirmish intent, but
it is evidence about those two maps, not about the family.

**Retail even ships hand-authored, per-faction, per-map AI base layouts for these maps.**
`data/ini/default/skirmishaidata.ini` binds base templates to maps by name:

```
AIBase MOWBase
    Side = Men
    Map  = "AI BASE - MOTW - Tech Up Base"
    GameMapToUseOn = "<ANY>"
End
AIBase MOWBase_MinasTirith
    Side = Men
    Map  = "AI BASE - MOTW - MinasTirith"
    GameMapToUseOn = "map wor minas tirith.map"
End
```

Distinct `GameMapToUseOn` values: 33 generic `<ANY>`, then **8 entries each** for
`map wor minas tirith`, `helms deep`, `erebor`, `dol guldur`, `grey havens`,
`ANG Carn Dum`, and 7 for `ANG Fornost`. The layouts live in
`bases/ai base - <faction> - <name>/*.bse` (EAR-refpack castle templates decoding to
per-structure offset/angle/priority/phase). Isengard, Black Gate and Minas Morgul have no
binding and fall back to the generic `<ANY>` bases — retail lets AI run there anyway.

Three corrections to how this evidence may be used:

- **The 8 `Side`s are the 7 playable factions plus ARNOR**, and Arnor is
  `PlayableSide = No` (`playertemplate.ini:446-448`). "All eight playable factions" is
  wrong, and Fornost's 7 is not "missing Arnor".
- **Map-specific base authoring is not castle-exclusive.** `MAP MP Amon Sul Fortress.map`
  and `map wor rivendell.map` also receive 8 bases. This weakens the "castle maps are
  special" framing — note especially that Amon Sul is the map this programme deliberately
  kept as a *plain* skirmish map. The evidence shows retail expected AI here; it does not
  establish castle maps as a distinct AI category.
- **Carn Dum's 8 entries span three case-variant filename spellings.** Any repo lookup must
  be case-insensitive or it finds only 5.

**Correction to the premise: no retail map references the AI libraries at all.** Decoding
the `LibraryMapLists` chunk of every map in the corpus (50 `map wor`, 22 `map mp`, plus
campaign) yields exactly two referenced libraries — `Lib_GollumSpawn` (56 maps) and
`Lib_End_Mission` (**18** campaign maps; 74 maps carry any library reference at all, and no
map references both). `ai_initialize.map` and `ai_mp_inherit_management.map` are referenced
by **zero** maps and zero `.ini` files. The engine attaches them; map data never does. So
the castle maps do not "lack" a hookup the `map mp` maps have — **no retail map has one**,
and **14** of the 22 shipped `map mp` maps have zero scripts at all yet play skirmish with
AI.

The library logic is start-position-driven (`START_POSITION_IS 1..8`) and faction-driven
(`SKIRMISH_PLAYER_FACTION`), never map-name-driven. Map-specific base authoring is an
*optimization layer* selected by `GameMapToUseOn`, not a precondition.

Our gate is therefore a repo invention with no retail counterpart —
`importer/openbfme_importer/map_profile.py:763-790`:

```python
if (target.category == SKIRMISH_CATEGORY
        and is_canonical_multiplayer_map_virtual_path(map_entry.name)):
    script_libraries = [AI_INITIALIZE_LIBRARY_PATH, AI_MP_INHERIT_LIBRARY_PATH]
    ...
    map_resources.append({... "converter": "sage-script-composite",
        "output": f"{output_root}/scripts.json", ...})
```

**The gate is a conjunct, and castle maps fail BOTH halves.** `target.category ==
SKIRMISH_CATEGORY` fails because castle maps are still `wotr-battle` until the admission
lane's category change lands (section 1.1), *and*
`is_canonical_multiplayer_map_virtual_path` (`profile.py:96-125`) requires exactly
`maps/map mp <name>/<name>.map`. **Fixing the path grammar alone cooks nothing** — L7 must
clear both conditions, which makes it dependent on L1's category work rather than merely
"benefiting" from it.

Injection nuance: the AI pair is injected **unconditionally** once the gate passes — it is
not a filter on the map's own `LibraryMapLists`. `GOLLUM_SPAWN` is different: it is
content-filtered via `_gollum_spawn_library_referenced`.

Verified in the shipped pack: 22 of 76 maps have `scripts.json`, **all `map mp`; zero
castle maps**.

The maps also carry the structural prerequisites `ai_initialize` needs: `Player_N_Start`
waypoints for `START_POSITION_IS` are present on **all ten**, and `Player_N_BuildPlot_1..4`
on eight. `Player_N_Inherit` teams (for `TEAM_TRANSFER_TO_PLAYER`) exist on only Black
Gate — but that is not a discriminator, since Amon Sul Fortress, Evendim, the Tournament
maps and 13 other shipped `map mp` maps have none either; the inherit scripts simply no-op.

The blocker's claim that AI "cannot honestly run until that composition is authored"
overstates the gap substantially. The fix is to replace the path-grammar test with a
content test, then verify.

Two honest residual risks:

- **Content, not plumbing.** `ai_initialize` was written for symmetric `map mp` topology
  and may behave badly on an asymmetric castle map even once loaded. That needs a playtest
  oracle, not a unit test.
- **`BASE_FLAG_N` provenance — largely resolved, and it changes L7's shape.** An earlier
  draft said the string appears nowhere in retail. That was a **grep artifact**: the
  library `.map` files are refpack-compressed, so naive search misses them. `BASE_FLAG`
  does exist in retail, in 19 library `.map` files — `ai_initialize.map` (x32) and
  `ai_mp_inherit_management.map` (x24), so it is consumed by **both** libraries, not
  `ai_initialize` alone.

  The bridge is almost certainly
  `libraries\multiplayer_start_teams\multiplayer_start_teams.map` — the **only** file in
  the corpus containing both `BASE_FLAG` (x24) and `BASE_SPAWN` (x16), i.e. the translation
  from the map-authored `BASE_SPAWN_N` objects to the `BASE_FLAG_N` names the AI consumes.
  `NAMED_BASE_UNPACK_FREE` occurs in `ai_initialize.map` and `multiplayer_human.map`.

  Restated precisely: **`BASE_FLAG` appears only inside `libraries/*.map` — zero playable
  maps, zero `.bse`, zero `.ini`.** The practical consequence is that L7's library set is
  wrong: attaching only the AI pair may not be enough, and `multiplayer_start_teams` plus
  `multiplayer_human` must be considered. Maps still place `BASE_SPAWN_1..N`
  `SkirmishSpawnPoint` objects (Minas Tirith 4, Helm's Deep 4, Dol Guldur 4, Black Gate 3,
  Grey Havens 3, Minas Morgul 3, Isengard 2; **Erebor, Carn Dum and Fornost have 0**), so
  the residual risk on those three maps stands.

### 2.6 What the maps' own scripts do — and one rule we must honour

All ten castle maps were script-decoded. They author thin housekeeping, not gameplay:
Black Gate has **zero** scripts; Grey Havens has one; the rest have 2-6, except Carn Dum
(21) and Fornost (16), whose bulk is the 11-script `SkirmishGollum_*` ring-hero packet that
Amon Sul Fortress also carries.

**No gate triggers, no garrison spawns, no wave spawns, no victory conditions anywhere in
the ten maps.** Gate opening, wall walking and garrisoning are engine-baked object
behaviors, exactly as this design assumes. What the scripts actually do:

- `Setup Player` — `OVERRIDE_PLAYER_COMMAND_POINTS(750, 1000)`, `PLAYER_SET_MONEY(2500)`.
- `Transfer Capture Flags` — cosmetic only: `UNIT_SET_MODELCONDITION_FOR_DURATION('Capture
  Flag 01', 'CAPTURED', 0, 0)`. No ownership changes.
- `Reveal <place>` — `MAP_REVEAL_PERMANENTLY_IN_TRIGGER`.
- Carn Dum's `Entry Wall NN Destroyed` — `NAMED_DESTROYED('Entry wall 01')` →
  `NAMED_KILL('Scaffolding 01')`, i.e. decorative scaffolding cleanup when a wall dies.

**The one real gameplay rule is `Build Restrictions`**, present on eight of the ten:

```
IF   CONDITION_TRUE()
THEN ALLOW_DISALLOW_ONE_BUILDING('<This Player>', 'MenFortress', 0)
     ... 14 entries: every faction fortress plus wall hubs
```

You cannot plant your own castle on a castle map — you fight over the authored one. If we
ship castle maps without honouring this, players will build a normal fortress base and the
map's entire point is lost. This is cheap to implement and must not be deferred: it belongs
in the first lane that makes a castle map launchable.

## 3. Current engine audit

### 3.1 Navigation — single-layer grid, built once

`retail_map_data.gd:1737-1743` builds a Godot `AStarGrid2D`, `cell_size = Vector2.ONE`,
`DIAGONAL_MODE_NEVER`, Manhattan. Walkability is one bit per cell from cooked
`passability_bits` (`:195`, read at `:2134-2140`, 1 = impassable). Cell pitch = cooked
`horizontal_scale` (10 SAGE units on shipped maps). Query `query_route()` `:1819-1843`,
capped `MAX_ROUTE_CELLS := 1024`.

**Multi-level is not representable.** Every index is `y * width + x`. Entity positions are
`Vector2` throughout the sim. The only `elevation` field (castle pieces,
`retail_slice_sim.gd:8865`) is purely presentational. Height is a separate buffer that
**never feeds a walkability or slope test**.

### 3.2 Collision — structures are invisible to the nav grid

This corrects the obvious assumption and it is the most important engine fact in this
document. From `retail_slice_sim.gd:16810-16813`: *"Grid routes ignore structure
footprints, so a waypoint can sit inside a blocked disc."*

`set_point_solid` appears exactly **once** in `game/src` (`retail_map_data.gd:1755`),
driven only by cooked passability and the water mask, and `_build_navigation()` is called
once from `:387`. **There is no add/remove/toggle API on the grid.**

Runtime blocking is instead a per-tick circle-deflection pass:
`STRUCTURE_BLOCK_RADIUS` (`retail_slice_sim.gd:15981-15994`) →
`_deflect_around_structures()` (`:16338-16463`), which already skips `health <= 0`
(`:16410`) and `construction_progress < 1.0` (`:16414`).

Two consequences, one good and one bad:

- **Good: a mutable blocker is already the architecture.** Blocking is re-derived every
  tick from the live structure row, so a gate needs `if row.get("gate_open", false):
  continue` in the deflection pass — no nav rebuild, no `set_point_solid`, deterministic by
  construction. A razed wall also stops blocking automatically, because there was never a
  nav stamp.
- **Bad: blockers are circles, and gates are line segments.** A wide gate as a single
  2.2-radius disc will feel wrong. Retail's named `Closed`/`OpenLeft`/`OpenRight` boxes
  need at minimum a multi-disc or capsule approximation.

A coarser deterministic alternative exists — `retail_slice_parity.gd:277-284`
`set_path_impassable(pos, bool)`, hashed and restorable — but at 25-world-unit granularity
it is far too coarse for a gate.

### 3.3 Garrison — bookkeeping exists, semantics do not

Exists: `containment` and `entity_container` (`retail_slice_sim.gd:10463-10464`),
`contain_entity()` / `exit_entity_container()` / `passenger_count()` (`:10702-10733`),
hashed empty-is-absent (`:19475-19478`), restored (`:19641`), script verbs in
`retail_slice_script_world.gd:5793-5845` and `:7330-7440`.

Absent: `contain_entity` does not remove the unit from the world, stop it moving, hide it,
or change collision or targetability. Nothing in movement, combat, vision or damage reads
either table. There is no player command. The script surface is **formally declared
blocked** — `game/src/script/handlers/wp03_blocked_transport_garrison.gd:1-38`: *"The
simulation has no passenger-transport or garrison model: nothing can be inside anything
else."*

**Capacity is currently fabricated.** `retail_slice_parity.gd:14-23` invents
`DEFAULT_TRANSPORT_CAPACITY` (`fortress:8, castle:6, wall:0, …`) and `:377-387` does
substring matching on the kind slug. This is not retail. The root cause is importer-side:
`importer/openbfme_importer/module_contracts.py:706-858` puts `GarrisonContain`,
`HordeGarrisonContain`, `TransportContain`, `TunnelContain`, `SiegeEngineContain` in
`OPAQUE_DEFERRED_MODULE_KINDS`, emitted with `extraction="opaque-authored"` — **no
`ContainMax`, no `PassengerFilter`**. So the first real garrison task is importer work:
promote these from opaque to typed.

### 3.4 Targeting and damage on structures — healthiest subsystem

Targets carry `target_kind ∈ {"battalion","structure"}`. Range is surface-to-surface via
authored footprint (`_target_footprint_radius()` `:16143-16171`). One damage funnel,
`_apply_structure_damage()` `:17622-17726`, with per-kind compiled armor. Death at health 0
(`:17705-17726`). **The structure row is never erased** on death — dead structures persist
at health 0 and are filtered by `health > 0` predicates.

**KindOf is almost unused at runtime**: `grep -rn "kindOf" game/src` returns 3 hits, and
the consumed token set is `SPAWNS_ARE_THE_WEAPONS`, `MOVE_ONLY`, `HERO`/`CAVALRY`,
`IMMOBILE`/`UNATTACKABLE`. `STRUCTURE`, `WALL`, `GATE`, `GARRISON` are never read. Identity
comes instead from a hand-authored slug map (`retail_faction_manifest.gd:1890-1912`, which
already contains `"wallgate"`, `"wallposterngate"`, `"castlewalltrebuchet"`,
`"garrisontowerexpansion"`).

Inert bits already on rows, written and never read: `retail_slice_script_world.gd:6246-6282`
`set_gate_state()` writes `row["gate_open"]` / `row["gate_ready"]`, and `gate_is_open()`
reads them back — **nothing else consults them**. `retail_slice_parity.gd:299-312` has
`is_wall_kind()` and `apply_wall_upgrade()`. These are useful seams, already hash-adjacent.

### 3.5 Castle behaviors already compiled — the contract template

`importer/openbfme_importer/castle_behavior.py:109` reads `runtimeModules` from lifecycle
evidence, filters `moduleKind == castlebehavior`, and emits a sealed contract into
`registration.gameplay.castleBehavior` (`faction_import.py:1122`). The sim consumes it at
`retail_slice_sim.gd:2517 configure_castle_behaviors`, spawning pieces via
`_spawn_castle_piece_structure()` (`:8842-8882`) with per-piece `offset_source`,
`angle_radians`, `elevation_source`, `maximum_health`, tagging `castle_piece_of_fortress`.

**This is the reuse template for every new module contract here**, and
`_spawn_castle_piece_structure` is very close to the shape a map-placed wall ring needs.

Retail's ownership rule for unpacked castles is one line, repeated on all 40
`CastleBehavior` sites (`civilianbuildings.ini:24`):

```
FilterValidOwnedEntries = ANY +STRUCTURE +WALK_ON_TOP_OF_WALL +BASE_FOUNDATION +TACTICAL_MARKER
    ;Anything that does not fit this filter will be given to the neutral player, ...
```

Note `WALK_ON_TOP_OF_WALL` is listed *separately from* `STRUCTURE` — walls are claimed by
the walk flag, so mis-tagging a wall silently hands it to the neutral player.

Naming hazard: `configure_castle_behaviors` means **player-built fortress castles**. New
work must not overload the name.

### 3.6 Determinism — the exact pattern, and one specific trap

Hash: `retail_slice_sim.gd:19257-19272` `state_hash()`, static/dynamic split at
`:19298-19311` with the static digest memoised. Pin:
`game/tests/retail_state_pin_runner.gd:164` `a436bb59…`, `PIN_TICKS := 3000`, rule at
`:53-55` — *"A DIFFERING HASH IS A DEFECT, NEVER A NEW BASELINE."*

The convention is named EMPTY-IS-ABSENT, stated at `:19390-19395`, and ~35 stores follow
`if not X.is_empty(): state[...] = X`, including `containment` (`:19475`). A second, finer
shape exists for per-row flags: object status bits erase the key when the set empties
(`:10125-10159`) — that is the right shape for a gate flag.

**Trap: `ford_gates` is in the *static* key list (`:19300`).** A mutable gate flag must not
ride there, or the memoised static digest goes stale. Use a new top-level empty-is-absent
store or the per-row erase-when-default shape.

New state must be registered in six places, all mandatory: `_authoritative_state()`,
`_restore_authoritative_state()`, `restore()`'s required-key list + static-digest
invalidation, `setup()` reset, `_state_hash_static_keys()` (only if immutable), and
`state_snapshot()` (only if presentation-visible). The executable checklist is
`game/tests/retail_state_boundary_runner.gd`, whose header enumerates seven prior defects
of exactly this class (`EXPECTED_CHECKS := 152`).

Two caveats to record rather than block on: the pin samples t=3000 only, and it injects no
`route_provider`, so **`AStarGrid2D` is never exercised by the pin** — any pathing change
ships with zero determinism coverage until a second pin is minted.

### 3.7 Map pipeline — objects survive on disk, gameplay does not

`sage_map.py:1150 _parse_objects` preserves `typeName`, `sagePosition`,
`sageAngleRadians`, `properties`, and derived Godot transforms. The `properties` bag
carries `originalOwner`, `objectEnabled`, `objectIndestructible`, `objectInitialHealth`,
`objectMaxHPs`, `objectTargetable`, `objectUpgradesList`, `objectName`, `objectLayer`,
`uniqueID` — everything ownership needs.

The compiled map pack contains **models only**: `data/maps.json`, `assets/models/{props,
props-animated,props-hierarchical,structures}`, and per-map `objects.json` /
`object-bindings.json`. `object-bindings.json` is a visual join
(`"matchPolicy": "explicit-exact-type-name-only"`, records carrying `glb` +
`sourceVirtualModel`). There is no gameplay document for any map object.

Also cooked but **never read by the game**: `setup.json` (sides, teams, build lists,
`libraryMapLists`) and `triggers.json`.

Maps are a separate pack (`rotwk-skirmish-maps-private`), so map-document work republishes
independently of faction packs.

### 3.8 Importer object corpus — castle map objects are never compiled at all

This is stronger than "they bind as `renderable`". `faction_import.py::build_faction_import_plan`
(line 389 — the docstring describing command-reachability lives here; there is **no**
`_account_objects` symbol in the importer) walks **only command-reachable objects** — the
CommandSet/CommandButton closure. Map-authored civilian objects never enter the corpus. Cross-matching every castle
map type-name against every compiled descriptor slug in every pack yields an **empty
intersection**: there is no compiled definition for `EreborMainGate`, `MinisWallA`,
`AngmarWallCarnDum` or any of their peers, even though retail authors them fully
(`EreborMainGate`: `ActiveBody 2000`, `ArmorSet EreborGateArmour`, `SlowDeathBehavior`,
`StructureCollapseUpdate`, four BOX geometries).

So L2 needs a **new corpus admission path** for map-referenced objects, not merely a
binding reclassification. That is the single largest importer change in this programme.

**What does exist compiled** — the buildable castle-expansion families, reachable from
CommandSets: `GateOpenAndCloseBehavior` on 5 objects (`MenWallGate`,
`DwarvenCastleWallGate`, `ElvenCastleWallGate`, `IsengardCastleWallGate`,
`AngmarWallGateSmall`), `WallHubBehavior` on 12 across 6 factions,
`HordeGarrisonContain` on 8 towers. `MenWallGate` already ships its full authored gate
contract (`OpenByDefault=No`, `PercentOpenForPathing=50`, `ResetTimeInMilliseconds=3000`,
all four sounds) plus a real `CastleGateCommandSet` with `Command_ToggleGate`. **These are
a working test bed for gate/garrison runtime work before the map corpus lands** — a useful
lane-parallelism opportunity. (Mordor and Wild have no wall objects at all. `MenFortress`'s
`castleBehavior` unpacks 7 pieces — 4 corner pads, 2 side pads, 1 citadel — no walls,
gates or towers.)

### 3.9 Module census — the authored data is already in the packs, as text

Tooling: `importer/openbfme_importer/module_census.py`, committed output
`game/data/retail_module_census.json`, run via
`python -m openbfme_importer.module_census [--check]`.

**Read its `consumed` status literally: it means the pipeline names the string, not that
the behavior runs.** Every castle-relevant module is `consumed` but **opaque only** —
`module_contracts.py` lists ~150 kinds in `OPAQUE_DEFERRED_MODULE_KINDS`, and
`compile_opaque_deferred_module` stores every authored assignment verbatim as text with
`runtimeStatus: "deferred"`, `extraction: "opaque-authored"`. It never emits `executable`.

RotWK declaration-site counts for the modules that matter: `GateOpenAndCloseBehavior` 32,
`AIGateUpdate` 18, `FakePathfindPortalBehaviour` 18, `DynamicPortalBehaviour` 17,
`HordeGarrisonContain` 18, `GarrisonContain` 2, `WallHubBehavior` 23,
`CastleMemberBehavior` 177, `CastleBehavior` 69 (**genuinely compiled**),
`CreateObjectDie` 68 (typed extractor). `ManTheWallsSpecialPower` and
`WeaponModeSpecialPowerUpdate` are explicitly **refused**. `OpenContain`, `WallUpgrade` and
`ParkingPlaceBehavior` are **never authored in retail** — do not plan for them.

This is good news for sequencing: the authored gate/garrison fields already survive into
the packs as preserved text, so a typed extractor is a *reinterpretation* of data already
present, not a new extraction from INI.

**Cost warning that changes lane sequencing:** `module_contracts.py` is in both the unit
and structure compiler dependency manifests, so adding a typed extractor invalidates every
cached object in those lanes and **requires a full faction re-convert**
(`import-faction --convert` then `publish-faction-to-slice`), not a map republish. Gates and
garrisons therefore ride a faction rebuild; only the map-document/admission work rides the
cheap maps republish.

**Dependency for wall-mounted defenses — narrower than first stated, and now root-caused.**

Correction to an earlier draft of this document: `_structure_combat_contract`
(`playable_structure_compiler.py:160-316`) **does** produce combat contracts on current
main — `MenTrebuchetExpansion` compiles a `weaponId` of `GondorTrebuchetRock`. The
"0 of 2716" figure counted **every pack revision on disk**, including stale ones; across
the *selected* packs the figure is **0 of 182**. Neither number means the lane is inert.

What actually fails is the **direct-weapon** structure shape (`SentryTowerBow`,
`CastleWallUpgradeBow`), where the contract returns `None` for two specific reasons:

1. **`DelayBetweenShots` uses the `Min:` / `Max:` form**, which `_resolved_definition_field`
   cannot resolve (it expects a scalar).
2. **Damage is absent as a scalar** because these weapons author multiple
   upgrade-conditional `ProjectileNugget`s rather than one damage value.

Fix those two field shapes and every base defense tower compiles. This reframes L5 from
"blocked on an unknown" to "fix two known field shapes" — see the regrade in section 5.

(`AIUpdateInterface` is still not in the opaque module list, so it vanishes silently; that
is a separate, smaller gap.)

Unsupported modules are **not warned**: anything neither typed nor in the opaque set is
silently absent. Object-level failures surface as `status: "converter-gap"` rows in
`<state>/reports/faction-import/<faction>-coverage.json`, which `publish-faction-to-slice`
refuses on (exit 7) absent `--allow-incomplete-coverage`.

## 4. Design, per blocker

Principles: absent unless authored; fail closed with named diagnostics; derive requirements
from the map's object inventory rather than hardcoding; reuse the `castle_behavior.py`
contract template.

### 4.1 The prerequisite (L2a/L2b): map objects compiled, then made sim entities

Three concrete changes, following precedents that already exist:

1. **Stop discarding `properties`** in `retail_map_data.gd:978-1009`.
2. **Classify wall/gate/tower types as `lifecycle-structure`**, not `renderable`, in
   `object-bindings.json`, so they carry an `object_id` into ContentDB and gain
   armor/health/weapon/geometry.
3. **Pass castle placements through `simulation_configuration()`**
   (`retail_map_data.gd:2172-2235`) following the `creep_lair_placements` precedent, and
   spawn them following `_spawn_castle_piece_structure()` (`:8842-8882`).

Emit a gameplay counterpart to `object-bindings.json` — `maps/<slug>/fixtures.json`:

```json
{"schema": "openbfme.sage-map-fixtures",
 "capabilities": ["defendable-gates", "wall-garrisons"],
 "fixtures": [
   {"typeName": "EreborGateDoors", "role": "gate",
    "kindOf": ["STRUCTURE","IMMOBILE","SELECTABLE","BLOCKING_GATE","WALL_GATE"],
    "maxHealth": 20000.0, "armor": "DefaultWallArmor",
    "gate": {"openByDefault": true, "resetMilliseconds": 5000, "percentOpenForPathing": 50,
             "allowEnemies": false, "triggerWidth": [450.0, 225.0], "deathRule": "keep-object",
             "geometries": {"Closed":   {"shape":"BOX","major":130.0,"minor":7.5,"height":140,"offset":[0,0,0]},
                            "OpenLeft": {"shape":"BOX","major":7.5,"minor":60,"height":140,"offset":[-115,68,0]},
                            "OpenRight":{"shape":"BOX","major":7.5,"minor":60,"height":140,"offset":[115,68,0]}}},
    "placements": [{"index": 812, "position": [3585.2,3686.6,0.0], "angle": 1.57,
                    "originalOwner": "Player_1/teamPlayer_1", "initialHealth": 100,
                    "indestructible": false, "enabled": true}]}]}
```

`capabilities` is the derived per-map requirement set; `castleSiege.blockers` becomes
`required - implemented`, computed rather than hardcoded.

**Contract-seal migration.** `retail_map_data.gd:395-417` demands `contract.size() == 4`
and an exact-order, exact-length blocker array — it cannot express "three blockers". Bump
to a versioned schema (`"version": 2`) validating blockers as a **subset of the known
vocabulary in canonical order**, while still accepting v1. Get this wrong and every mounted
map document fails to load.

**Performance warning.** Carn Dum = 125 wall segments + 24 towers + 10 catapults + 5 gates;
Minas Tirith ≈ 166. That is a 2-4x increase in live structure rows, and
`_deflect_around_structures` iterates **all** structures per moving entity per tick
(`:16363`). Use the existing spatial hash (`_spatial_key()` `:641`) for structure
broad-phase before shipping. This is an inference from the loop shape, not a measurement —
measure it in-lane.

### 4.2 defendable-gates — feasibility **B+**

Runtime: each gate placement is a sim structure with health, owner, armor, and a
`gate_state` of `closed | opening | open | closing | destroyed`, honouring
`OpenByDefault = Yes` (map gates start open). Blocking is handled in
`_deflect_around_structures` by consulting the row — **not** by touching the nav grid.
Reuse the existing `row["gate_open"]` / `row["gate_ready"]` bits, which are already
written by the script layer and read by nobody.

Approximate the named boxes with a small set of discs (`Closed` = a span of discs across
the gateway; `OpenLeft`/`OpenRight` = one disc each at the authored offsets) rather than a
single radius. Enemy passage is refused by data (`AllowEnemies = No`), not by a new rule.
Auto-open uses the `AIGateUpdate` rectangle. Death follows the per-object rule
(`KeepObjectDie` for all castle-map gates: geometry persists, breach is permanent,
repairable).

State: a new top-level `if not gate_states.is_empty(): state["gate_states"] = gate_states`,
or per-row erase-when-default. **Never** the static key list where `ford_gates` lives.

Tests, failing-first: routing blocked/unblocked through a gate; enemy refusal; auto-close
after `ResetTimeInMilliseconds`; destruction leaves permanent passage; two-sim determinism;
`a436bb59…` unchanged on non-castle maps; oracle pytest pinning `PercentOpenForPathing`,
`ResetTimeInMilliseconds`, `OpenByDefault` and the three geometry names.

### 4.3 wall-garrisons — feasibility **B**

Importer first: promote `GarrisonContain` / `HordeGarrisonContain` from
`OPAQUE_DEFERRED_MODULE_KINDS` to typed, so `ContainMax`, `PassengerFilter`,
`ObjectStatusOfContained`, `DamagePercentToUnits`, `AllowNeutralInside` and the exit fields
become facts — and **delete the fabricated `DEFAULT_TRANSPORT_CAPACITY`** in
`retail_slice_parity.gd:14-23`.

Then promote `containment` to a real system: capacity in **hordes**, `PassengerFilter`
admission, occupant status (`UNSELECTABLE`, `ENCLOSED`, retains `CAN_ATTACK`), 0% damage
routing, eviction on death (`KillPassengersOnDeath = No`), exit via `ExitOffset`, neutral
capture (`AllowNeutralInside = Yes` — first garrison claims). Unblock the 11 WP03 verbs.

Fire-from-inside: attack from the structure centre with the tower's vision range. Modelling
`ARROW_*` bone positions is presentation. Flag as a deliberate simplification.

Unlocks Grey Havens (needs nothing else), Erebor, Fornost.

### 4.4 wall-mounted-defenses — feasibility **B**

Implement mechanism (b)/(c) — `ReplaceSelfUpgrade` and in-place `GeometryUpgrade` — before
(a), since the slave-object path adds a second entity with `SlavedUpdate` lifetime rules.
Structures already shoot (`_step_structure_weapons()` `:8514`), already take damage
through one funnel, and already resolve authored footprints. The picker affordance mirrors
the existing expansion-pad UI.

Sequence **Carn Dum and Dol Guldur before Minas Tirith**: their wall-mounted defenses are
not coupled to walkable walls, whereas Minas Tirith's armed segments also carry
`WALK_ON_TOP_OF_WALL`.

### 4.5 walkable-walls — feasibility **D**, scope separately

The authored data exists; the blocker is entirely ours. Requirements for a real
implementation:

1. A layer axis in passability indexing (`retail_map_data.gd:2134`) — new cooked layer data
   the importer does not produce.
2. Replacing `AStarGrid2D` (Godot has no multi-layer variant) with a custom A* over a
   layered node graph including transition nodes at stairs, ramps and siege docks.
3. Widening entity position from `Vector2` to a layer-tagged position across ~20k lines of
   `retail_slice_sim.gd` — combat range, separation, spatial hash, targeting, formations,
   fog.
4. Re-minting the state pin (positions are hashed).
5. Minting a **new** pin that actually covers pathing, since the existing one never
   exercises `AStarGrid2D`.

**Cheaper interim worth considering:** treat wall tops as non-walkable and ship walls as
destructible obstacles plus firing platforms for pre-placed defenses. That makes Carn Dum
and Dol Guldur genuinely playable without the rewrite, but it does **not** deliver Minas
Tirith or Helm's Deep as the owner imagines them.

## 5. Lane decomposition

Ordered; each sized for one implementor session except L6. Every lane: failing-first tests,
own branch, no push, no pack builds while a republish is running.

### L1 — per-map capability derivation
- **Scope.** Retail object index registering `Object`, `ChildObject` **and `ObjectReskin`**
  definition sites; derive per-map capabilities; replace the hardcoded five-blocker seal
  with `required - implemented`; version the `castleSiege` contract to v2 while still
  accepting v1. Add `scaleable-walls` to the capability vocabulary.
- **Depends on.** Nothing.
- **DoD.** Importer pytest against the real oracle reproduces the section 1.2 matrix
  exactly, including Carn Dum = 5 gates / 39 wall-mounted (the `ChildObject`-keyword trap)
  and Erebor = 6 real garrisons with `Erebortower2` explicitly excluded (the
  `GARRISON`-KindOf trap). Unresolved type-name residue is 1-10 scenery types per map or
  explained. `retail_map_data.gd` accepts v1 and v2, red-first. `a436bb59…` unchanged;
  `retail_slice_runner` failure names unchanged vs main.

### L2a — admit map objects to the compiled corpus
- **Scope.** Section 3.8: `faction_import.build_faction_import_plan` (line 389) walks only
  command-reachable objects, so castle map objects have no compiled definition at all. Add
  a corpus admission path for map-referenced types; emit `maps/<slug>/fixtures.json`
  (section 4.1).
- **Depends on.** L1.
- **Cost note.** This lane **rides a full faction re-convert** (`import-faction --convert`
  then `publish-faction-to-slice`), not the cheap maps republish. Of all the lanes, this is
  the one where that matters most — plan the session around the rebuild time.
- **DoD.** Real-oracle pytest: `EreborGateDoors` and `EBGarrisonableTower` produce compiled
  definitions carrying their authored health, armor and module contracts; Erebor emits one
  `gate` fixture with three named geometries and `Player_1` owner, and six `garrison`
  fixtures with `ContainMax = 3`.

### L2b — map placements become sim entities
- **Scope.** Stop dropping `properties` in `retail_map_data.gd:978-1009`; classify castle
  types as `lifecycle-structure`; route placements into `simulation_configuration()`; spawn
  via the `_spawn_castle_piece_structure()` precedent. Add structure broad-phase via the
  existing spatial hash (`_spatial_key()`).
- **Depends on.** L2a.
- **Ship behind a feature gate.** This flips ~166 objects per map from inert decoration
  into live sim structures. It must be a flag with a clean rollback, not a one-way door —
  the blast radius covers pathing, targeting, performance and presentation simultaneously.
- **DoD.** A castle map's walls exist as sim structures with authored health/owner and are
  targetable. Measured tick cost on Carn Dum reported honestly, before and after
  broad-phase. Feature gate proven to restore prior behavior when off. `a436bb59…`
  unchanged on non-castle maps.

> **The L2a/L2b split is the default, not a fallback.** An earlier draft graded the combined
> lane B- and offered splitting as a contingency; that was generous. Brief them separately.

### L3 — defendable gates
- **Scope.** Section 4.2. Typed `GateOpenAndCloseBehavior` /
  `FakePathfindPortalBehaviour` / `AIGateUpdate` extractors (promote from
  `OPAQUE_DEFERRED_MODULE_KINDS`); gate state machine, `OpenByDefault`, auto-open
  rectangle, auto-reset, enemy refusal, disc-set blocking in the deflection pass, death
  rules, hash-safe state. Reuse the existing unread `row["gate_open"]` bits.
- **Depends on.** L2a for map gates. **But the runtime half can start immediately against
  the already-compiled buildable `MenWallGate`** (section 3.8), which ships the full
  authored contract and a `Command_ToggleGate` command set. Exploit that parallelism.
- **Cost note.** Touching `module_contracts.py` invalidates the unit and structure compiler
  caches: this lane rides a **full faction re-convert**, not a map republish.
- **DoD.** Red-first sim tests for all five behaviors; two-sim determinism; `a436bb59…`
  unchanged; oracle pytest pins `OpenByDefault`, `PercentOpenForPathing`,
  `ResetTimeInMilliseconds` and the three geometry names. **The gate toggle must ride the
  lockstep command queue** — prove MP determinism of the *command*, not merely of the
  resulting state, with a two-peer test. (OpenSAGE's `Toggle()` is called straight from a
  command callback marked `//TODO: proper toggle gate order`; do not copy that path.)
  **Owner-visible milestone:** a castle map launches and its gate works.

### L4 — wall (tower) garrisons
- **Scope.** Section 4.3. Promote `GarrisonContain`/`HordeGarrisonContain` from opaque to
  typed, then sim semantics, then unblock the 11 WP03 verbs.
- **Depends on.** L2a for map towers; the runtime half can start against the 8
  already-compiled buildable garrison towers. Independent of L3.
- **DoD.** Red-first tests for horde capacity (3, and 1 for `GHGarrisonableTower`),
  `PassengerFilter` refusal, damage isolation at 0%, death eviction, neutral capture, and
  `Erebortower2` accepting nobody. Fabricated `DEFAULT_TRANSPORT_CAPACITY` deleted, with a
  test proving capacity now derives from the compiled contract.
  *(An earlier draft claimed "Grey Havens becomes fully playable" as this lane's DoD. It is
  not: Grey Havens additionally needs L1, L2 and L7. L4's own testable contribution is the
  garrison contract above; the map-playable milestone belongs to whichever lane lands last.)*

### L5 — wall-mounted defenses
- **Scope.** Section 4.4, mechanisms (b)/(c), scoped to Carn Dum and Dol Guldur.
  **Regraded from "spike-gated" to a bounded fix.** An earlier draft claimed no structure
  can shoot; in fact `_structure_combat_contract`
  (`playable_structure_compiler.py:160-316`) already compiles expansion weapons
  (`MenTrebuchetExpansion` → `GondorTrebuchetRock`). Only the **direct-weapon** shape fails,
  for two identified reasons: `DelayBetweenShots` in `Min:`/`Max:` form that
  `_resolved_definition_field` cannot resolve, and damage absent as a scalar because the
  weapon authors multiple upgrade-conditional `ProjectileNugget`s. **Fix those two field
  shapes and every base defense tower compiles** — this is lane task 1, and it is a known
  fix rather than an open spike.
- **Depends on.** L2a.
- **DoD.** The two field shapes are handled; `SentryTowerBow` and `CastleWallUpgradeBow`
  resolve; `GondorCastleWallTower` compiles with its authored weapon. Carn Dum's 39
  wall-upgrade placements resolve; a catapult can be bought, fires, and is destructible.
  Mutual exclusion (`ConflictsWith`) proven. Determinism preserved.

### L6 — walkable walls (gated on owner decision D1; multi-session)
- **Scope.** Section 4.5. **First task is a spike**: confirm `P1`/`P2`/`R1` sub-meshes
  survive W3D-to-GLB conversion. If they do not, stop and re-scope.
- **Depends on.** L2b, and D1.
- **DoD.** A unit routes from ground up a stair onto a Helm's Deep wall and back; enemies
  cannot reach the top without a dock or stair; a new pathing-covering determinism pin is
  minted; the four walkable maps drop the blocker. **Do not brief as one session.**

### L7 — skirmish AI on castle maps
- **Task 1: decode `multiplayer_start_teams.map`'s scripts** (section 2.5). It is the only
  file in the corpus containing both `BASE_FLAG` (x24) and `BASE_SPAWN` (x16) and is almost
  certainly the bridge from map-authored `BASE_SPAWN_N` objects to the `BASE_FLAG_N` names
  the AI consumes. Widen the library set L7 considers attaching to include
  `multiplayer_start_teams` and `multiplayer_human`, not just the AI pair.
- **Scope.** Clear **both halves** of the `map_profile.py:763-790` conjunct — the
  `category == skirmish` test *and* the `map mp` path grammar. Fixing the path grammar
  alone cooks nothing. Verify the composer's placeholder-player contract still holds.
  Stretch: compile the per-map AI base layouts retail ships (`skirmishaidata.ini`
  `GameMapToUseOn` + `bases/ai base - <faction> - <name>/*.bse`) for the six bound maps —
  **using case-insensitive filename lookup**, or Carn Dum's three case-variant spellings
  yield 5 entries instead of 8.
- **Depends on.** L1 — hard, not "benefits from": the category conjunct means L7 cannot
  cook anything until L1's category change lands. Also benefits from L3.
- **DoD.** Red-first importer test that a castle map gets `scripts.json` with the correct
  library composition and a non-admitted `map wor` map does not. Headless match proving AI
  **actually expands and attacks** on at least two castle maps — including one with zero
  `SkirmishSpawnPoint` (Erebor or Carn Dum), since that is where `BASE_FLAG` resolution
  will fail if it is going to. Assert on structures built and attacks issued with an
  explicit check-count liveness assertion; a runner that reports green while the AI stood
  still is the failure mode. Honest report of which maps get base-building vs defend-only AI.

### L8 — lobby admission for asymmetric castle maps
- **Scope.** `mapcache.ini` gives these maps `numPlayers = 2-4`, not 8. Decide and enforce
  how many AI opponents may be seated and on which authored start positions. No other lane
  addresses this, and without it a 2-player map like Erebor or Carn Dum can be handed a
  seat list it has no start positions for.
- **Depends on.** L1; owner decision D5.
- **DoD.** Red-first test that lobby seat count for each castle map matches its authored
  `numPlayers`, and that seating an AI beyond that is refused with a named diagnostic.

### L9 — presentation for map-placed castle structures
- **Scope.** L2b turns ~166 objects per map into live, selectable sim structures. Nothing
  presents them: minimap icons, selection decals, health bars, hover/tooltip, and fog
  reveal all need to handle map-placed structures. Today the only consumer of the structure
  classification is `retail_fords_battlefield.gd:1313`, which is fords-specific.
- **Depends on.** L2b.
- **DoD.** A castle map's walls, gates and towers select, show health, appear on the
  minimap, and reveal correctly under fog, without per-frame visibility flicker (the known
  hazard that kills clicks and hover). Frame cost measured on Carn Dum and Minas Tirith.

### L10 — honour `Build Restrictions`
- **Scope.** Apply the maps' authored `ALLOW_DISALLOW_ONE_BUILDING` fortress/wall-hub
  lockout (section 2.6). Without it, players build a normal base and the map's point is lost.
- **Depends on.** **L7, not nothing.** An earlier draft called this trivial and suggested
  folding it into L1. That was wrong: `Build Restrictions` lives in the map's own script
  chunk, castle maps cook no `scripts.json` at all, and the composite converter bundles map
  scripts and libraries together — so this needs L7's cook fix, or an explicit
  map-scripts-only cook path added here.
- **DoD.** Red-first test that a castle map refuses fortress construction for every faction
  in the authored 14-entry list, and that non-castle maps are unaffected.

**Suggested order:** L1 → L2a → L2b → {L3, L4 parallel} → L5 → L7 → L10 → L8 → L9 → L6.
After L1+L2+L3+L4+L7+L10: Black Gate, Grey Havens, Erebor, Fornost playable (Black Gate
first, per D4). After L5: Carn Dum, Dol Guldur. Minas Tirith, Helm's Deep, Minas Morgul and
Isengard need L6. L9 should not lag L2b by long — selectable-but-unpresented structures are
a bad intermediate state to leave in main.

## 6. Owner decisions requested

- **D1 — walkable walls (most important).** Ship six maps without wall-walking and leave
  Minas Tirith and Helm's Deep blocked, or fund the multi-session nav-layer rewrite
  (section 4.5)? The owner's headline request is specifically those two maps, so the cheap
  path does **not** deliver the ask.
- **D2 — AI scope on castle maps.** Retail's evidence (per-map AI base layouts for six of
  these maps, per-map `SkirmishAIData`, and starting-porter overrides on Minas Tirith and
  Helm's Deep) says **standard base-building AI everywhere** is the faithful answer, with
  generic `<ANY>` layouts on Isengard, Black Gate and Minas Morgul exactly as retail does.
  Recommend adopting that rather than a defend-only v1. Confirm.
  **Coupled constraint nobody owns yet:** `mapcache.ini` gives these maps
  `numPlayers = 2-4`, not 8. An 8-slot AI seat list on a 2-player asymmetric map is a
  **lobby-admission** problem — how many AI opponents may be seated, and on which start
  positions — and no lane in section 5 addresses it. It is assigned to **L8** below.
- **D5 — lobby admission on asymmetric castle maps.** Follows from D2: on a 2-player map
  like Erebor or Carn Dum, does the lobby offer 1 AI opponent, or do we allow team stacking
  onto authored start positions that do not exist? Needs an owner call before L8 is briefed.
- **D3 — fake vs real wall garrison.** Retail ships player wall "garrison" as a
  weapon/armor/geometry upgrade, not real occupants (section 2.2). Adopt retail's fake
  garrison for wall plots (cheap, visually identical) while implementing real containment
  only for the neutral towers that authored it?
- **D4 — Black Gate (default chosen, confirm only).** Its gates are script-driven
  `USER_1`/`USER_2` animations on `UNATTACKABLE` objects, not gate modules, and the map
  authors zero scripts. **Default: ship Black Gate as a no-gate battlefield.** It needs no
  gates, no garrisons and no wall-mounted defenses, so this makes it the first castle map
  to become playable. Chasing the engine-side open is explicitly out of scope unless the
  owner overrides.

*(A question about neutral gate ownership was withdrawn: `FakePathfindPortalBehaviour
AllowEnemies = No` is uniform across every retail gate, so the rule is data, not a
decision.)*

## 7. Honest limitations

- **Nothing here was implemented or runtime-verified.** No Godot runner was executed. The
  `castle_map_admission_runner.gd` result quoted in the predecessor report was not
  reproduced, and that runner has no invocation in the repo.
- The capability matrix's 8-53 unresolved `typeName`s per map were a **tooling artifact of
  this lane's index, not a data gap**: the index registered `Object` and `ChildObject` but
  missed **759 `ObjectReskin` definitions**. With reskins registered, the residue falls to
  **1-10 purely scenery types per map**. L1 should still drive it to zero-or-explained, but
  the matrix is sounder than the first draft implied.
- The retail index resolves single-level `ChildObject` inheritance and `KindOf`/`Behavior`
  lines only; it does not evaluate `#include`, macro expansion, or `.inc` conditionals, so
  some objects may carry modules it misses.
- **No `.w3d` asset was opened.** Whether `WallBoundsMesh`/`RampMesh` names correspond to
  real sub-meshes, and whether they survive conversion here, is unverified and is the
  largest single unknown for L6.
- All ten maps' script chunks **were** decoded (section 2.6), but the Black Gate's
  script-driven `USER_1`/`USER_2` gate open remains unexplained: that map authors **zero**
  scripts, so whatever opens it is not map script. D4 stands.
- **`BASE_FLAG_N` is now largely resolved** (section 2.5) — an earlier draft called it
  absent from retail, which was a grep artifact against refpack-compressed library `.map`
  files. What remains genuinely unverified is whether
  `multiplayer_start_teams.map` really performs the `BASE_SPAWN` → `BASE_FLAG` translation;
  its scripts were **not** decoded. Erebor, Carn Dum and Fornost (zero spawn points) remain
  the likeliest silent-AI-failure candidates.
- **The structure-weapon gap is root-caused but unfixed.** Correcting an earlier draft:
  `_structure_combat_contract` does compile expansion weapons today, and the "0 of 2716"
  figure counted every pack revision on disk (selected packs: 0 of 182). The real defect is
  two field shapes in the direct-weapon path (`DelayBetweenShots` `Min:`/`Max:` form;
  damage absent as a scalar under upgrade-conditional `ProjectileNugget`s). Identified by
  review, **not verified by a fix in this lane**.
- One cross-lane claim was investigated and **disproved**: a report that the importer
  silently drops space-indented `ArmorSet` blocks was traced to a researcher reading
  `.private/retail-work/cache/effective-assets` — a different, non-current tree — instead of
  the `editions/rotwk` oracle. `sage_cst.parse_sage_document` handles space indentation
  correctly. Recorded here so it is not "fixed".
- The performance claim in section 4.1 is an inference from an O(structures) loop, not a
  measurement.
- Retail per-frame gate/garrison update rates, and animation-to-pathing coupling beyond
  `PercentOpenForPathing`, were not mined.
- OpenSAGE was surveyed read-only, never built or run. "Implemented" there means
  "non-trivial runtime code reachable from `CreateModule`", not "verified correct".
