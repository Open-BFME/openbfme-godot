# Sixteen-player skirmish map contract

P16 is an OpenBFME extension of RotWK 2.01, not a retail feature or a new
simulation profile. Use the same game data for both Enhanced and Uber. A map
with at most eight starts retains the legacy eight-slot format, four team
choices and retail colors. A map with 9–16 starts enables sixteen-slot records,
eight team choices and six additional colors; only its actual starts may be
occupied. Seventeen starts are not supported.

## Mapmaker checklist

1. Export a valid RotWK map with **exactly one** waypoint of each name
   `Player_1_Start` through `Player_16_Start`. Spelling and case are exact;
   `Player_01_Start`, gaps, duplicate names and `Player_17_Start` are errors.
   These are one-based start identifiers, not zero-based lobby slot numbers.
2. Put every start on finite, traversable terrain inside the active boundary.
   Leave room for the faction's fortress layout, starting builders and the AI
   base footprint. Ordinary skirmish starts use waypoint type **0** (the default).
   A nonzero type suppresses the automatically created starting structure:
   use it only when deliberately implementing a prebuilt fortress scenario.
3. Include sixteen `MPPositionInfo` records in `MPPositionList`, in start order.
   Permit humans and computers, enable AI scripts, use team `-1` for unrestricted
   lobby teams and leave side restrictions empty for all seven retail factions.
   Do not silently copy an eight-position list into a sixteen-start map.
4. Place neutral **`FarmTemplate`** objects at suitable economy sites near each
   base. These are the AI economy's site markers, not prebuilt player farms.
   Keep them clear of fortress/building footprints and provide traversable
   routes to them. The generated test map uses four per start at offsets
   `(±300, ±300)` game-world units; this is a fixture design, not a required
   layout or proof of balance. Test the real map's construction/pathfinding.
5. Retain normal neutral, civilian and creep sides. Do not add a seventeenth
   playable slot or another synthetic replay observer. The engine creates
   `Player_N` for an occupied start and one `ReplayObserver`; the ordinary
   sixteen-player setup is exactly **20 players**: 16 + neutral + civilian +
   creeps + replay observer. Duplicate retained side names are rejected.
6. No extra per-player victory or surrender scripts are needed for an ordinary
   skirmish. The lobby supplies teams, hostility, AI difficulty and colors.
   Objects intentionally owned by a start use `Player_N`, not the player index
   in the engine's twenty-entry arrays. Don't pre-place another starting
   fortress at a type-0 start unless that duplication is intentional.
7. Make sure each playable faction has an applicable AI base template. Standard
   data may select a `<ANY>` template; a map-specific `AIBase` must match the map
   filename and allow the selected start in `PlayerPositions` (or leave that
   restriction empty). A restriction listing only starts 1–8 does not enable
   starts 9–16. Do not disable generic templates without providing replacements.

## Discovery and cache metadata

The lobby currently reads the mounted **`maps/mapcache.ini`**; copying a `.map`
file into an arbitrary directory does not register it. The integrator must
mount the custom map and a cache containing its entry through the supported
asset/package path. Preserve the other cache entries. Do not edit or redistribute
retail archives to achieve this. Automatic loose-map discovery is not supplied
by P16-CORE.

For `maps/my_map/my_map.map`, supply a corresponding `MapCache` block with:

- `numPlayers = 16`, `isMultiplayer = yes`, normally `isScenarioMP = no`;
- a nonempty `displayName` (the lobby omits maps with no display name);
- accurate `fileSize`, `fileCRC`, extents and optional camera/description;
- all sixteen `Player_N_Start = X:... Y:... Z:...` entries, matching the map's
  waypoint coordinates and numbering.

MapCache block names use the existing escaped-path convention, for example
`maps_5Cmy_5Fmap_5Cmy_5Fmap_2Emap`. Display labels use the existing cache's
UTF-16 escaping convention. Use the map-packaging/cache tooling rather than
inventing checksums or hand-copying another map's metadata. The headless fixture
supplies its cache directly in memory; it does not exercise map installation.

Startup validates both the cache start set and the loaded map start set.
Missing, duplicate, noncanonical or out-of-capacity starts fail startup; there
is no center-of-map/default-start fallback. Refresh the cache after moving,
adding or removing starts. A stale count is an error, not a request to invent
additional positions. All peers need the same map and effective data identity.

## Terrain limits and reference fixture

The heightmap reader accepts versions 3–5, positive dimensions up to 16,384
samples per axis, a nonnegative border, and exactly `width * height` samples.
These are **parser limits**, not a supported performance envelope. Do not build
a maximum-sized map on that basis.

The source-only reference in `engine/tests/P16TestUtil.h` generates a flat
512×512 version-5 heightmap, border 10, active boundary 492×492 cells
(4920×4920 game-world units). Its starts form a 4×4 grid:

```text
i = start number - 1
x = 600 + 1200 * (i % 4)
y = 600 + 1200 * (i / 4)       // integer division
z = 0
```

It contains sixteen start waypoints and 64 neutral farm-site markers. Map bytes
are generated and mounted in memory; no `.map`, BIG archive or extracted retail
data is committed. The test still requires the user's verified RotWK 2.01 and
BFME2 1.06 installations for objects, AI data and scripts. A decorated real map
needs separate navigation, economy, rendering and performance checks.

## Extension policies

- Teams: four numbered choices on legacy maps, eight on extended maps; existing
  singleton-team/alliance and no-team semantics are retained.
- Colors: the ten retail entries retain their order and values. Extended maps
  add Cyan `00DCDC`, Magenta `E600AC`, Lime `B7F000`, Yellow `FFE119`, Navy
  `16255C`, Brown `633B20`, with equal day/night RGB. Parsed retail settings are
  not modified. Visual distinction on units/minimap is a separate UI check.
- Production: shipped 2.01 GameData defines only MP1–MP8, all `1.0`, for unit
  and building speed multipliers. The explicit P16 policy uses **MP8** for
  9–16 playable players rather than inventing absent MP9–MP16 entries. A
  shortened production table is refused for an extended game. Both tables must
  contain matching contiguous MP keys, without duplicates or malformed entries. Values for
  games with at most eight players follow the existing arithmetic.
- Compatibility: an extended setup uses a tagged sixteen-slot encoding. Old
  readers must refuse it. Do not force engine identities equal to enable play;
  byte-identical legacy simulation and network capability are separate gates.

## Headless checks and evidence status

Build the existing `openbfme_tests` target, set `ROTWK_INSTALL` and
`BFME2_INSTALL`, and run:

```sh
engine/build/openbfme_tests --test-case='p16 core:*,p16 fixture:*,p16 retail:*'
```

Startup tests cover capacity, starts, 0/8/16-human mixtures, all four AI levels,
teams, shroud, score tracking, high-slot surrender and alliance victory.
The extended palette and farm-site additions pass the focused startup checks.
Ten-minute CRC/save continuation, performance measurements and the complete
unchanged legacy gate are **not yet acceptance-complete**. This document does
not certify network/UI integration or the mapmaker's as-yet-unsupplied map.

The opt-in acceptance harness (not part of the default short suite) runs two
identical ten-minute sixteen-AI games, then catches up a minute-five command-log
save and compares the remaining five minutes frame by frame:

```sh
mkdir -p workspace/p16
OPENBFME_P16_OUTPUT_PREFIX=workspace/p16/enhanced OPENBFME_P16_PROFILE=enhanced \
  /usr/bin/time -v engine/build/openbfme_tests --no-skip=true --test-case='p16 soak:*'
```

Repeat with profile `uber` and a different output prefix. The archive installs
must be set; this explicitly requested test fails rather than skipping if they
are unavailable. Outputs are two full per-frame state-hash/census CSVs, a resumed
hash CSV, per-minute per-player AI activity and diagnostic/identity manifests.
The state hash uses OpenBFME's rotate-add hasher, not the standard CRC-32 polynomial.
Logic time measures only `GameLogic::runLogicFrame`, excluding hashing/rendering;
reported windows are the 300 frames ending at minutes 1, 5 and 10. Frame 1 is the
startup state; the 3000 measured ticks produce absolute frames 2–3001. The save
is at frame 1501. `/usr/bin/time -v` peak RSS covers the **whole test process**
(mounting, three sequential worlds, retained logs and replay catch-up), not just
one simulation world. Other hosts should record an equivalent peak working set.

## Source/evidence references

- `GameLogic/NewGame/StartingBase.cpp`: RW `0x62AD24` (waypoint type),
  `0x62AE37` (starting units); base placement and missing-start errors.
- `GameLogic/SkirmishAI/AIBaseBuilder.cpp`: RW `0x90CE76` (start lookup),
  `0x9BC734` / `0x9BCBAC` (base eligibility/selection), `0xDE9EE8` and
  `0x97D986` (`FarmTemplate` sites).
- `GameClient/MapChunks.h`: MPPosition writers from the BFME2 decomp;
  `GameClient/MapCache.cpp` and `LiveGame.cpp`: P16 validation.
- `GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.cpp`: heightmap parser.
- `Common/BuildAssistant.cpp`: RW `0x73C39E` build-time arithmetic;
  MP8 extension is new policy, not a claim that retail supports sixteen players.

Recovered-address evidence retains the repository's S-001 image-provenance caveat.
