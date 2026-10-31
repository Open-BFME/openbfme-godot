# Retail saves: the inventory (SAVE-A0)

Lane SAVE-A0, 2026-10-12. The reference for SAVE-4 and SAVE-7: retail's save file format, every
RotWK 2.01 xfer with its version window and transfer order, and the answers to the open questions
of `docs/SAVES.md`. The owner decided to implement retail import (not export; SAVE-7 is not built) and that every
class ported 1:1 from retail transfers its fields in retail's order with retail's version numbers.

Sources, in PLAN's priority: the RotWK 2.01 `game.dat` (disassembly, Ghidra), the RotWK -> BFME2 ->
decomp map (`tools/re/rw2decomp.py`), the Open-BFME-2 decomp (BFME2 1.06) as the donor, Zero Hour's
`GameState.cpp` / `Xfer*.cpp` for names. RW addresses are RotWK 2.01 VAs, with S-001's caveat (the
analysed `game.dat` carries community-added sections). **(inference)** marks a conclusion that is not
a binary fact.

Tools (lane SAVE-A0), run from the repository root on Linux, macOS or Windows
with Python 3 (`python3` below; use the installed Python command on Windows):

- `python3 tools/save/retail_save.py <file> [--json] [--walk]`: a read-only reader for retail saves. It
  lists the container, every top-level block (token, offset, size, the snapshot's version byte), the
  end token, and decodes the save game info the Load screen shows. It uses only the standard
  library. Container and decoded-header format errors are named `SaveFormatError`s with file
  offsets; other block payloads are skipped by their end offsets, not validated. Trailing bytes
  are reported, not refused. Tagged `--walk` stops at an unknown/opaque transfer rather than
  decoding every field. Tested on synthetic files that the tests build.
- `python3 tools/save/xfer_inventory.py --md docs/SAVES-RETAIL.md`: regenerates the table at the end of this
  file. It needs the `pefile` and `capstone` Python packages, RotWK 2.01 `game.dat`
  (`--bin` or `RW_GAME_DAT`) and the xmap (`--map` or `OPENBFME_XMAP`, default
  `workspace/rebuild/xmap/rw_map.csv` in the main checkout). `--csv <path>` writes the full
  inventory; keep it under ignored `workspace/`. `--show <RW>` prints one function's scan
  (a hexadecimal VA) and returns without writing CSV or Markdown.

Linux / POSIX shell, from the repository root, with Python 3 and a save file
at the example path (no third-party packages needed for the reader):

```sh
python3 tools/save/retail_save.py workspace/sample.BfME2Skirmish --json
```

## 1. The file

All of it is tier A code (identical to BFME2 1.06, byte-matched in the decomp) unless noted.

```
u32 0x45414C41 "ALAE"
u32 0x52545332 "2STR"        or 0x52545331 "1STR": then no version word (XferLoad::Open RW 0xA200A3)
u32 version                  saveGame RW 0x6DE498 passes 1 to XferSave::Open RW 0xA20DD0; the
                             loaders refuse > 1 (RW 0x6DEA25: version < 2; the header reader RW 0x6DEEC3)
u32 tagged                   XferSave::Open's third argument; see "Tagged files" below. Retail writes 0.
per block of the chosen list (GameState::xferSaveData RW 0x6DDF2F):
  string token               "CHUNK_..."
  u32 0x424C4F4B "BLOK"      XferSave::beginBlock RW 0xA2111A
  u32 end                    absolute file offset just past the block
  payload                    the snapshot's xfer, starting with its version byte
string "SG_EOF"
```

- **The word after BLOK is an absolute end offset, not a size** (correcting SAVES.md). beginBlock
  writes a zero placeholder and pushes its position; XferSave::endBlock RW 0xA206EF takes the stream
  position, seeks back, writes that position over the placeholder and seeks forward again.
  Xfer::skipBlock RW 0xA203D6 and BlockStreamReader::skipBadBlock RW 0xA201CA seek to it
  (`skip(n, 0)` is an absolute seek; `skip(0, 1)` is the tell).
- **EBLK exists only in tagged files** (correcting SAVES.md). endBlock writes "EBLK" only when the
  tagged flag is set, and Xfer::endBlock RW 0xA20149 reads it only then.
- A string (Xfer::operator==(AsciiString&) RW 0xA1FB84, UnicodeString RW 0xA1FC7E, PooledString
  RW 0xA1FD80) is a u8 length, or 255 then a u32 length, then the bytes (UTF-16LE for wide strings).
- A version (Xfer::operator==(Version&) RW 0xA1F5B9) is one byte, the class's *current* version.
  The class's local holds two bytes, (earliest, current); on load a byte below earliest or above
  current throws "Found version %i but the earliest supported is %i" / "Don't know how to load
  version %i (most recent is %i)".
- Loading: an unknown token is skipped by its end offset (`skipBlock`); a known one runs
  `beginBlock`, the snapshot's xfer (slot 0x30), `endBlock`. Primitive values have no framing in an
  untagged file: the payload is only parseable by the classes' xfer code, which is why the
  inventory below matters.

### Tagged files

When the tagged flag is nonzero (`m_10` of XferLoad, `m_flag` of XferSave):

- every typed transfer is preceded by a u32 type tag (Xfer.cpp in the decomp): `ver`, `bool`, `byte`,
  `ubyt`, `shrt`, `usht`, `int`, `uint`, `i64`, `real`, `astr` (also PooledString), `ustr`, `raw`
  (+ u32 size), `enu1`..`enu4`, and the composites `c3d`, `ic3d`, `c2d`, `ic2d`, `rnge`, `rgb`,
  `rgbr`, `rgbi`, `r3d`, `ir3d`, `r2d`, `ir2d` (a tag, then the fixed-size bytes). XferLoad::XferImpl
  RW 0xA2048D throws "Expected '%s' but found '%s'" on a wrong tag;
- BLOK is followed by the block's name as a name record (RW 0xA20FCE): u8 length (at most 254) and
  the bytes; 0 = no name; 0xFF + u32 = a name already written (an id in XferSave's name table);
- **'DSCR' (u32 0x44534352) + a name record is a field label.** XferSave's slot 0x2C (RW 0xA20B26)
  writes it, tagged files only, for a struct whose first member is the label text; XferLoad's slot
  0x2C (RW 0x92BB7C) does nothing; the loaders skip a DSCR record wherever one precedes a tag or a
  BLOK (XferImpl, beginBlock). So it is a debugging annotation of the self-describing format.
- **Retail never writes tagged saves (inference from a static scan).** saveGame passes the byte at
  RW 0xDE87BB as the flag (RW 0x6DE5E5). It is zero-initialised `.data`; its only writer, RW 0x7BA505
  (`mov byte [0xDE87BB], 1; ret` among the command-line handlers), has no reference anywhere in the
  image (no absolute pointer, no relative call or jump). So retail saves carry 0 there.
- The reader supports tagged files (and walks them with `--walk`) for completeness, because XferLoad
  accepts them.

### The save game info (CHUNK_GameState)

The CHUNK_GameState snapshot is GameState's Snapshot subobject (TheGameState + 0xC, registered at
RW 0x6DF79B). Its vtable RW 0xC1A3B4 puts GameState::xfer at RW 0x6DCE1C, **which rw_map.csv
mislabels `AIGuardReturnState::xfer` (tier B)**. It transfers its own version (1, 1), then
`xferSnapshot` of the save game info at GameState + 0x24 (vtable RW 0xBFE680, xfer RW 0x6DEFD0, tier
B of the decomp's `BfmeSubobject0022CE19::xfer`), window (1, 3):

| # | Transfer | Field (meaning) |
|---|---|---|
| 1 | version | (1, 3) |
| 2 | enum, 4 bytes, `SaveFileType` (RW 0x707527) | the save mode 0-6 (section 3) |
| 3 | AsciiString | GameState + 0x50; saveGame RW 0x6DE498 clears it (empty in its saves) |
| 4 | 8 x u16 | the local time, in the order saveGame copies SYSTEMTIME: year, month, **day, day of week**, hour, minute, second, milliseconds |
| 5 | UnicodeString | the description (saveGame's argument) |
| 6 | AsciiString | GameState + 0x30, which saveGame copies from GameState + 0x2C: GameStateMap's pristine map path (RW 0x82B8E4 keeps its two map names at GameState + 0x28 / + 0x2C) |
| 7 | version < 2 only: AsciiString, int | ZH's campaign side and mission number (**inference**, from ZH SaveGameInfo) |
| 8 | version >= 3 only: enum, 4 bytes, `IsAutoSaveOrNot` (RW 0x6DCB35) | saveGame's last argument |
| 9 | UnicodeString | GameState + 0x54 (**inference**: the local player's display name, set from a game slot at RW 0x6DE71F) |
| 10 | UnicodeString | GameState + 0x58: the human players' names joined (RW 0x6DE6EC) |
| 11 | save types 4 and 6 only | a count + per army a preview record (RW 0x6DCB85, (1, 1): a position, an id, an RGB colour), then the game slots snapshot (virtual xfer) |

`retail_save.py` decodes rows 1-10 (the Load screen's data) and reports how many bytes remain.

## 2. The block lists

GameState::init RW 0x6DF73E registers the five lists of SAVES.md section 1. The load dispatcher
RW 0x6DEA25 picks the list from the mode it is given: mode 1 reads list 4, mode 6 list 3, any other
list 0; saveGame RW 0x6DE498 uses list 4 for mode 1 when its caller passes list 0. The block for
each token, and its xfer where one is identified:

| Token | Snapshot (decomp GameStateInit.cpp) | xfer | Window |
|---|---|---|---|
| CHUNK_GameState | TheGameState + 0xC | RW 0x6DCE1C, then the save game info RW 0x6DEFD0 | (1, 1), (1, 3) |
| CHUNK_GameStateMap | GameStateMap | RW 0x82B8E4 `GameStateMap::DoXfer` | (1, 2) |
| CHUNK_TerrainLogic | TheTerrainLogic | RW 0x46253F `W3DTerrainLogic::xfer` -> RW 0x683F8C `TerrainLogic::xfer` | (1, 1), (1, 2) |
| CHUNK_GameLogic | TheGameLogic | RW 0x6308F3 (unnamed; **inference**: it transfers the object TOC through `GameLogic::xferObjectTOC` RW 0x62EF07 like ZH's GameLogic::xfer) | (1, 8) |
| CHUNK_Players | ThePlayerList | RW 0x6A8788 `PlayerList::DoXfer` | (1, 1) |
| CHUNK_TeamFactory | TheTeamFactory | RW 0x7A634B `TeamFactory::xfer` | (1, 3) |
| CHUNK_Radar | TheRadar | RW 0x6D962D `Radar::xfer` | (1, 3) |
| CHUNK_SidesList | TheSidesList | RW 0x72B570 `SidesList::DoXfer` | (1, 1) |
| CHUNK_TacticalView | TheTacticalView | RW 0x65ED04 `View::xfer` (**inference** by name; W3DView may wrap it) | (1, 3) |
| CHUNK_GameClient | TheGameClient | RW 0x647ABF `GameClient::xfer` | (1, 4) |
| CHUNK_InGameUI | TheInGameUI | RW 0x6A73C6 `InGameUI::xfer` | (1, 2) |
| CHUNK_TerrainVisual | the terrain visual | RW 0x4920F4 `W3DTerrainVisual::xfer` | (1, 1) |
| CHUNK_GhostObject | TheGhostObjectManager | RW 0x463ACA `W3DGhostObjectManager::xfer` -> RW 0x707122 | (1, 1) |
| CHUNK_Campaign | TheLinearCampaignManager | RW 0x5EB2AC `LinearCampaignManager::DoXfer` | (1, 1) |
| CHUNK_SpellStore | the spell store | RW 0x8227E9 `SpellStore::xfer` | (1, 1) |
| CHUNK_FireLogicSystem | TheTriggerManager | RW 0x688384 `FireLogicSystem::xfer` | (1, 2) |
| CHUNK_LivingWorldLogic, CHUNK_Audio, CHUNK_Palantir, CHUNK_ScriptEngine, CHUNK_Partition, CHUNK_Shroud, CHUNK_Collision, CHUNK_ParticleSystem, CHUNK_VictorySystem, CHUNK_TaintManager, CHUNK_WeatherSystem, CHUNK_SkirmishAISystem, CHUNK_MineshaftPortalNetworkManager, CHUNK_AiOrdersManager, CHUNK_ObjectivesMenu, CHUNK_MissionObjectives | see GameStateInit.cpp | not identified by name: their classes' xfers are in the table below under the map's names (or unnamed); tying each global to its vtable is SAVE-7's first step | |

The "by name" rows rely on the map's names, and the map is wrong at least once (RW 0x6DCE1C): treat
them as **(inference)** until the vtable of each registered global is read (S-4021).

## 3. The questions of SAVES.md, settled

**Object::xfer keeps the module skip.** RotWK's Object::xfer is RW 0x697F28 (tier B, edited, of
BFME2 0x697684; no decomp source), version window **(1, 29)** (BFME2: 26). Its module loop is ZH's:
a u16 count; saving, per module its tag name (AsciiString) and BLOK "BehaviorModule" (string RW
0xC04E0C), the module's snapshot, EBLK; loading, per entry the tag name and, when the object has no
module with that tag, `skipBlock("BehaviorModule")` (the call at RW 0x6987A5), else the module's
snapshot inside its block. So a module that a newer data set removed is skipped by its end offset,
as in ZH, and a module the save does not mention keeps its constructed state.

**GameStateMap embeds the map.** RW 0x82B8E4 (tier B of `GameStateMap::DoXfer`), window (1, 2):

1. Saving on the War of the Ring strategic map (TheLivingWorldLogic's selection lock, RW 0x441E4A)
   or with the linear campaign's flag + 0xD2 set (RW 0x5EA8AD; **inference**: between missions), it
   writes an empty map name and embeds nothing. Otherwise: the save-directory name of the map, then
   the "pristine" map path (both made portable, RW 0x6DD947).
2. Two ints: GameLogic + 0x110 (the game mode) and + 0x114 (the War of the Ring battle kind,
   section 4).
3. The map file inside a nested block: "PristineMap" on the first save of an original map,
   "InUseMap" when the game itself came from a save (the map already lives in the save directory).
   The block holds a u32 size and the file's bytes. In version 2, the map's `.wak` sibling (the path
   with its 4-character suffix replaced by ".wak", RW 0x82B5A2) follows the same way after a bool
   "exists". Loading reads either block back as "EmbeddedMap" and **writes the map file into the
   save directory** (FileSystem, RW 0x82B802), then makes it TheGlobalData's map. An empty name on
   load sets the map to "NOMAP".
4. The highest ObjectID and DrawableID (XferObjectID RW 0x707887, XferDrawableID RW 0x70789F).
5. TheSkirmishGameInfo's snapshot when the game mode is 2 (skirmish) or the battle kind is 0 (a
   single-player War of the Ring battle); it is created on load if missing, and deleted otherwise.
6. On load, unless the game mode is 9 or the save type is 4 or 6, it calls GameLogic's
   start-new-game RW 0x6314CD with "loading a save" set: the map is loaded and the objects are
   created **before** the later blocks overwrite their state, as in ZH.

So a retail save carries the map it was played on, and an importer can read the map from the save
instead of from the player's map folders. The embedded map is retail-format data; an imported save
is never committed and never uploaded (it is the player's own file).

**'DSCR'** is a field-label record of tagged files (section 1); it never occurs in retail's own
saves.

**CHUNK_VENDOR is not a save chunk.** The string RW 0xC15F68 is referenced only from RW 0xDA1078,
an entry of the KindOf name table (between URUK and ARCHER, beside ROCK_VENDOR): it is the KindOf
`CHUNK_VENDOR`. GameState::init never registers it, and nothing else reads it as a token.

**The modes 0-6** (GameState::determineCurrentGameSaveFileMode RW 0x6DCFDF, tier A; extension
RW 0x6DCDE9; list RW 0x6DEA25):

| Mode | When (RW 0x6DCFDF) | Meaning | Extension | List |
|---|---|---|---|---|
| 0 | in the linear campaign and game mode != 9, or outside it with battle kind 3 and game mode 6 | a campaign mission | .BfME2Campaign | 0 |
| 1 | in the linear campaign (TheLinearCampaignManager + 0x10) and game mode 9 | the campaign between missions (**inference**: game mode 9 is that screen; GameStateMap does not start a game for it) | .BfME2Campaign | 4 |
| 2 | no GameLogic / LivingWorld yet, or battle kind 3 and game mode != 6 | a skirmish | .BfME2Skirmish | 0 |
| 3 | battle kind 0, not on the strategic map | a single-player War of the Ring battle | .BfME2WotR | 0 |
| 4 | on the strategic map (selection locked), battle kind 0 | the single-player War of the Ring strategic map | .BfME2WotR | 0 |
| 5 | battle kind 1 or 2 (RW 0x610A21), not on the strategic map | a multiplayer War of the Ring battle | .BfME2Campaign (the default arm of RW 0x6DCDE9; **inference**: an oversight, or never reached because the save page refuses) | 0 |
| 6 | on the strategic map, battle kind != 0 | the multiplayer War of the Ring strategic map | .BfME2WotRMP | 3 |

The mode is also field 2 of the save game info. GameLogic + 0x114 is the "battle kind": 0 a
single-player War of the Ring battle, 1 or 2 a multiplayer one, 3 not a War of the Ring battle
(AptQuitMenu's Save/Load RW 0x92179F / 0x9217F0 open the page as WOTRSP for 0 and WOTRMP for 1-2).

**A multiplayer skirmish cannot be saved as a game in retail.** The in-game menu's Save
(AptQuitMenu RW 0x92179F, identical to BFME2) opens the save page with the filter WOTRSP (8) for
battle kind 0, WOTRMP (16) for kinds 1-2, **Replay (4) when GameLogic::isInMultiplayerGame (game
mode 1 LAN or 5 online, RW 0x441B7C)**, and Campaign (1) / Skirmish (2) otherwise. On the Replay page,
Save names the file with `.BfME2Replay` (RW 0x77DEE3, the extension string at RW 0xC2EEB4) and goes
through RW 0x77FDF0 / 0x77FB6D instead of GameState::saveGame (**inference**: they copy the running
replay; both are unmatched code). The two-player save confirmation (state 5, "APT:SavingWaitOnMultiplayer",
RW 0x817719) runs only for the WOTRMP filter with TheGameInfo holding more than one occupied slot
that RW 0x8009A7 accepts (**inference**: a human player), and the
lobby's Load buttons (AptLanLobby RW 0x84929F, RW 0x9A8478) open the page with WOTRMP. So the
multiplayer game save is War of the Ring only; a multiplayer skirmish's "save" is its replay.

## 4. What waits for sample saves

At SAVE-A0 (2026-10-12), there were no retail saves on the Deck and the reference PC was off limits
(owner pause). The findings above are static binary research, not a real-save round trip.
These checks need real files (the owner's own `Save` folder, or saves made under the retail-run
rules). The reader covers the container/header checks; nested map and object records still need
a hex view and the relevant xfer layouts:

1. The container header of real files: "ALAE" "2STR", version word 1, tagged word 0 (confirms the
   static finding that retail never writes tagged saves).
2. The block order of each mode against the lists: a skirmish (mode 2), a campaign mission (0), a
   save between missions (1, list 4), a WotR battle (3), the WotR strategic map (4), and if the
   owner has one, a multiplayer WotR save (6, list 3). The reader must parse each to SG_EOF with
   zero trailing bytes.
3. Every top-level block's first payload byte equals the current version in the table (for example
   CHUNK_GameState 1, CHUNK_GameStateMap 2, CHUNK_GameLogic 8, CHUNK_Radar 3).
4. The save game info: the description, the date order (day before day of week), the map label,
   the meaning of field 3 (empty?), field 9 and field 10, and IsAutoSaveOrNot for the campaign's
   `__AUTO#SAVE__`.
5. GameStateMap: which nested block a first save carries (PristineMap) and a save of a loaded game
   (InUseMap), the embedded size against the map file's size, and whether the `.wak` sibling is
   present in practice.
6. Mode 5's extension: whether a multiplayer WotR battle save exists at all, and if so its name.
7. The Object::xfer module records of a few objects (tag names, block ends), to check the
   inventory's transfer order of Object and two common modules against real bytes.
8. A tagged file can only come from a modified game; no check of the tagged path is possible with
   retail saves, and the name-table id numbering (0xFF + u32) stays unverified (S-4020).

## 5. loadPostProcess

The map names 18 RotWK `loadPostProcess` functions (13 tier A, 5 tier B). They resolve the IDs the
xfers saved into pointers after every block has loaded (GameState's post-process list, RW 0x6DD65C).
`python3 tools/re/rw2decomp.py loadPostProcess` lists matching rows (the default
limit is 20; use `--limit <n>` for more). Run from the repository root with Python 3 and the ignored xmap and decomp clone
configured as `tools/re/rw2decomp.py --help` describes.

## 6. The inventory

Generated by `python3 tools/save/xfer_inventory.py --md docs/SAVES-RETAIL.md` with the prerequisites
and inputs listed above. This is the recorded SAVE-A0 scan, not a live count of ported runtimes. One row per function
that transfers a version or that the map names `::xfer` / `::DoXfer`. Read the columns like this:

- **(earliest, current)**: the class's own version window, the first version transfer in the
  function; "+N" counts further version transfers inside it (nested structs, both arms of a
  branch); "none" = the function transfers no version.
- **Transfer sequence**: the Xfer calls in code order. `ver(e, c)` a version; `u8 i8 u16 i16 u32 i32
  i64 real bool` the primitives; `astr ustr pstr` strings; `coord3d icoord3d coord2d icoord2d
  region3d iregion3d region2d iregion2d realRange rgb rgbaReal rgbaInt` composites; `raw[n]` raw
  bytes; `enumN` an N-byte enum (XferEnum); `snapshot` xferSnapshot (a member's own xfer);
  `BLOK EBLK skipBLOK` blocks; `label` a DSCR label; `->Class::xfer` a direct call to another xfer
  (a base class or a member); `->virtual xfer` a member snapshot's xfer through its vtable;
  `call:Name` another function that receives the Xfer (ID, enum and container helpers). `xN`
  repeats. Both arms of every branch appear and loops appear once: version gates (`if (version >=
  2)`) and the save/load split are not shown; read the RW address for those.
- The tier and names come from the map: a tier-B/C/D name can be wrong (RW 0x6DCE1C is GameState's
  xfer, not AIGuardReturnState's). Rows without a decomp name are RotWK-only or unmatched code.

<!-- BEGIN GENERATED: tools/save/xfer_inventory.py -->
695 functions transfer a version or are named `::xfer` by the map (421 named `::xfer` / `::DoXfer`, 274 found by their version transfer only); 687 with a version window read from the code. Tiers: - 63, A 524, B 97, C 2, D 9. Version helpers (whole body = one version transfer on ecx): RW 0x404F15 (1, 1).

| RW | Tier | Class / function | (earliest, current) | Transfer sequence (code order, both branch arms) |
|---|---|---|---|---|
| 0x8C2921 | A | `$0BF::BitFlags<>::xfer` | (1, 1) | ver(1, 1) call:Rva004BE722::rva004BE722 i32 astr i32 astr |
| 0x4BAEE4 | A | `$0CEP::BitFlags<>::xfer` | (1, 1) | ver(1, 1) call:Rva000B95B1::rva000B95B1 i32 astr i32 astr |
| 0x692CDC | B | `$0GF::BitFlags<>::xfer` | (1, 1) | ver(1, 1) call:Rva00291679::rva00291679 i32 astr i32 astr |
| 0x691C46 | A | `$0GI::BitFlags<>::xfer` | (1, 1) | ver(1, 1) call:Rva0028F808::rva0028F808 i32 astr i32 astr |
| 0x692E18 | B | `$0JK::BitFlags<>::xfer` | (1, 1) | ver(1, 1) call:Rva002916DA::rva002916DA i32 astr i32 astr |
| 0x692BA0 | B | `$0L::BitFlags<>::xfer` | (1, 1) | ver(1, 1) call:Rva0029161A::rva0029161A i32 astr i32 astr |
| 0x6ACB39 | B | `$0NK::BitFlags<>::xfer` | (1, 1) | ver(1, 1) call:Rva002ABBBE::rva002ABBBE i32 astr i32 astr |
| 0x71A1A7 | A | `$0P::BitFlags<>::xfer` | (1, 1) | ver(1, 1) call:Rva003187A8Owner::rva003187A8 i32 astr i32 astr |
| 0x654A1D | A | `ActivateModuleSpecialPowerModuleData::Rva00254CCF` | (1, 1) | ver(1, 1) snapshot |
| 0x8C3388 | A | `ActiveBody::xfer` | (1, 1) | call:FUN_00993419 ver(1, 1) realx6 call:XferBodyDamageType call:XferBodySideDestroyedType u32 call:XferDamageFXType u32x2 boolx4 u16 call:XferParticleSystemIDx2 real bool ->$0BF::BitFlags<>::xfer call:XferObjectID |
| 0x742BE8 | A | `AIAttackAimAtTargetState::xfer` | (1, 1) | ver(1, 1) boolx3 |
| 0x741FD0 | A | `AIAttackApproachTargetState00C12678::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer u32 boolx2 coord3d u32 bool |
| 0x741E38 | B | `AIAttackApproachTargetState::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer coord3d u32 boolx4 coord3d u32 boolx2 |
| 0x7421C7 | A | `AIAttackFireDuringApproachState::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer coord3d icoord2d u32 call:XferObjectID bool |
| 0x742229 | A | `AIAttackMeleeApproachState::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer coord3d icoord2d u32 |
| 0x7420F9 | A | `AIAttackMeleeHordeWaitPathState::xfer` | (1, 2) | ver(1, 2) u32 i32 |
| 0x7420C7 | A | `AIAttackMeleeHordeWaitState::xfer` | (1, 1) | ver(1, 1) u32 |
| 0x74243F | A | `AIAttackMeleeSquishState::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer coord3d icoord2d u32 bool |
| 0x742C26 | A | `AIAttackPositionAimAtTargetState::xfer` | (1, 1) | ver(1, 1) boolx2 |
| 0x742144 | A | `AIAttackPursueTargetState::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer coord3d u32 boolx5 |
| 0x74CD94 | A | `AIAttackState::xfer` | (1, 3) | ver(1, 3) bool coord3d raw BLOK snapshot EBLK |
| 0x9BCE7D | A | `AIBase::DoXfer` | (1, 1) | ver(1, 1) i32 coord3d real astr u32 call:Rva005DCE72::rva005DCEB7 bool virtual+0x30 |
| 0x90D573 | A | `AIBaseBuilder::DoXfer` | (1, 2) | ver(1, 2) coord3d bool coord3d u32 ->AIBase::DoXfer |
| 0x8F03FA | A | `AIBuilder::DoXfer` | (1, 5) | ver(1, 5) u32 bool ->AIDozerManager::DoXfer ->AIBaseBuilder::DoXfer ->AIEconomyBuilder::DoXfer ->AIWallBuilder::DoXfer call:Rva00598DA2::rva00598DA2 call:Rva0059761B::rva0059761B |
| 0x743395 | B | `AIDockApproachState::xfer` | (1, 1) | ver(1, 1) |
| 0x94B2E1 | A | `AIDockMachine::xfer` | (1, 1) | ->StateMachine::xfer ver(1, 1) i32 |
| 0x743011 | A | `AIDockState::xfer` | (1, 1) | ver(1, 1) bool snapshot bool |
| 0x9A1DEB | A | `AIDozerManager::DoXfer` | (1, 1) | ver(1, 1) bool u32 call:XferObjectIDx2 |
| 0x8EF0D3 | A | `AIEconomyBuilder::DoXfer` | (1, 1) | ver(1, 1) u32x2 i32 u32x4 |
| 0x7434E9 | A | `AIExitState::xfer` | (1, 2) | ver(1, 2) call:XferObjectID bool |
| 0x743D72 | B | `AIFaceState::xfer` | (1, 1) | ->AIIdleState::xfer ver(1, 1) |
| 0x99F41D | A | `AIFarm::DoXfer` | (1, 1) | ver(1, 1) bool i32x2 u32 call:Rva00573F03::Rva00573F9F |
| 0x9BC050 | A | `AIFarmKillSquad::xfer` | (1, 1) | ver(1, 1) ->AITactic::xfer call:XferObjectIDx2 |
| 0x9B4805 | A | `AIFlankAttackTacticImp::DoXfer` | none | u32x2 coord3dx2 |
| 0x742AC5 | A | `AIFollowWaypointPathExactState::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer u32 |
| 0x8EDC02 | A | `AIGameTeam::rva004E951C` | (1, 1) | ver(1, 1) ->SkirmishAI::DoXfer |
| 0x8B4A69 | A | `AIGateUpdate::xfer` | (1, 2) | ver(1, 2) ->UpdateModule::xfer bool i32x2 bool i32x2 |
| 0x769297 | A | `AIGiantBirdFollowThruState::xfer` | (1, 2) | ver(1, 2) bool i32 |
| 0x7706EC | A | `AIGroup::xfer` | (1, 1) | ver(1, 1) call:XferObjectID u32 real bool u32 coord3dx2 |
| 0x9495F7 | D | `AIGuardIdleState::xfer` | none | call:State::State |
| 0x949575 | A | `AIGuardInnerState::xfer` | (1, 2) | ver(1, 2) snapshot u32 |
| 0x949872 | B | `AIGuardMachine::xfer` | (1, 3) | ->StateMachine::xfer ver(1, 3) call:XferObjectID u32 call:XferObjectID coord3dx2 bool raw u32 astr real |
| 0x949665 | B | `AIGuardOuterState::xfer` | (1, 1) | ver(1, 1) snapshot |
| 0x7438EB | A | `AIGuardRetaliateState::xfer` | (1, 1) | ver(1, 1) bool snapshot |
| 0x6DCE1C | B | `AIGuardReturnState::xfer` | (1, 1) | ver(1, 1) snapshot |
| 0x94B306 | A | `AIHarvestPrepareSiteState::xfer` | (1, 1) | ver(1, 1) u32 |
| 0x7431E7 | A | `AIHarvestState::xfer` | (1, 1) | ver(1, 1) bool snapshot bool |
| 0x7434B1 | A | `AIHordeExitState::xfer` | (1, 2) | ver(1, 2) bool |
| 0x741850 | B | `AIIdleState::xfer` | (1, 2) | ver(1, 2) u16 boolx2 u16 |
| 0x7428F3 | A | `AIMoveAndDeleteState::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer bool |
| 0x742821 | A | `AIMoveAndEvacuateState::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer coord3d |
| 0x7456DD | A | `AIMoveOntoWallState::xfer` | (1, 1) | ver(1, 1) bool snapshot |
| 0x741A2B | A | `AIMoveToStateSA::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer u32 bool |
| 0x8F5756 | B | `AIPlayer::xfer` | (1, 2) | ver(1, 2) call:XferObjectID u16 snapshotx2 u16 snapshotx2 i32 boolx2 i32x5 call:XferObjectID i32 call:XferGameDifficulty i32 coord3d bool real call:XferObjectIDx2 i32 boolx2 i32 coord3d u32 |
| 0x743E6C | A | `AIRampageState::xfer` | (1, 1) | ver(1, 1) bool u32x2 |
| 0x9B8675 | A | `AIReturnTheRingTactic::xfer` | (1, 1) | ver(1, 1) ->AITactic::xfer u32 call:XferObjectIDx2 boolx2 |
| 0x9B3256 | A | `AISiegeGatesTactic::xfer` | (1, 1) | ver(1, 1) ->AITacticSiege::xfer |
| 0x9B50C2 | A | `AISimpleExpansionTactic::xfer` | (1, 1) | ver(1, 1) ->AITactic::xfer bool virtual+0x30x2 u32 |
| 0x9B3385 | A | `AISimpleSiegeTactic::xfer` | none | ->AITacticSiege::xfer u32 |
| 0x8F3F6C | B | `AISkirmishPlayer::xfer` | (1, 1) | ->AIPlayer::xfer ver(1, 1) i32x2 realx6 |
| 0x8B74A6 | D | `AISpecialPowerUpdate::xfer` | (1, 4) | ver(1, 4) ->UpdateModule::xfer boolx2 |
| 0x9B84A9 | A | `AIStartWoTRBattleTactic::xfer` | (1, 1) | ver(1, 1) ->AITactic::xfer |
| 0x755174 | A | `AIStateMachine::xfer` | (1, 2) | ver(1, 2) ->StateMachine::xfer i32 coord3d astr bool snapshot u32 snapshot u32 call:XferObjectID coord3d |
| 0x9B8B42 | B | `AIStructureCreepTactic::xfer` | (1, 1) | ver(1, 1) ->AITactic::xfer call:XferObjectID u32x2 coord3d bool virtual+0x30x2 |
| 0x8F2466 | A | `AITactic::xfer` | (0, 2) | ver(0, 2) u32x4 bool u32 call:Rva004ECDC8::rva004ECDC8x2 i32 u32 bool astr u32 bool coord3dx2 boolx2 u32 |
| 0x9EF74B | A | `AITacticSiege::xfer` | (1, 1) | ver(1, 1) ->AITactic::xfer call:XferObjectID |
| 0x90B6B3 | A | `AITargetChooser::xfer` | (1, 2) | ver(1, 2) u32 call:Rva002C589B::rva002C5B46x3 u32 call:Rva002C589B::rva002C5B46 call:Rva005A91BE::rva005A91BE u32 |
| 0x94C45F | A | `AITNGuardInnerState::xfer` | (1, 1) | ver(1, 1) bool |
| 0x94C781 | B | `AITNGuardMachine::xfer` | (1, 2) | ->StateMachine::xfer ver(1, 2) call:XferObjectID coord3d raw |
| 0x94C4E7 | A | `AITNGuardReturnState::xfer` | (1, 1) | ->Rva00341930::xfer ver(1, 1) u32 |
| 0x743A14 | A | `AITunnelNetworkGuardState::xfer` | (1, 1) | ver(1, 1) bool snapshot |
| 0x741D60 | A | `AIUncontrollableCower::xfer` | (1, 1) +1 | ver(1, 1) bool ver(1, 1) |
| 0x667416 | B | `AIUpdateInterface::xfer` | (1, 6) | ver(1, 6) ->UpdateModule::xfer u32x2 snapshot bool snapshot bool snapshot boolx2 u32 call:XferObjectID real call:XferCommandSourceType call:Rva00263569Xfer coord3d call:XferObjectID u32 astrx2 i32 coord3d i32 bool u32 bool call:XferObjectID bool call:FUN_00767eb7 call:XferObjectID coord3dx2 call:XferObjectID real u32 coord3d boolx10 call:XferObjectIDx4 call:LocomotorSet::xferSelfAndCurLocoPtr call:XferLocomotorSetType raw coord3d snapshot call:XferWhichTurretType call:XferAttitudeType u32 astr ->virtual xfer call:XferObjectID bool ->$0CEP::BitFlags<>::xferx2 bool call:Rva0026AFDAMember::doXfer bool real boolx13 real raw real u32 ->$0GF::BitFlags<>::xfer boolx2 u32 bool call:Rva0026AFDAMember::doXfer |
| 0x99F94C | B | `AIUpgrade::DoXfer` | (1, 1) | ver(1, 1) call:Rva0055B0CC::Rva0055AED6 i32 astr |
| 0x8EF6DB | A | `AIWall::DoXfer` | (1, 2) | ver(1, 2) i32 u32 virtual+0x30 call:Rva00596CDF::Rva00596CEA u32 bool virtual+0x30x2 u32 i32x4 u32x2 call:Rva00149101XferNameKey |
| 0x8EE594 | A | `AIWallBuilder::DoXfer` | (1, 1) | ver(1, 1) u32x3 |
| 0x742B6E | A | `AIWanderInPlaceState::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer coord3d i32x2 |
| 0x6D7C58 | A | `Anim2D::xfer` | (1, 2) | ver(1, 2) u16 u32 u8 u16x2 u32 real icoord2d |
| 0x881E3E | B | `AnimalAIUpdate::xfer` | (1, 1) | ver(1, 1) call:XferObjectID bool coord3d bool ->AIUpdateInterface::xfer |
| 0x8C004C | A | `AODCrushCollide::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer u32 call:XferObjectID bool |
| 0x8BC857 | B | `ArmorUpgrade::xfer` | (1, 1) | ver(1, 1) |
| 0x811B0B | B (attempt) | `ArmySummary::xfer` | (1, 2) | ver(1, 2) bool astr call:XferLivingWorldArmyID astr i32x5 call:XferReinforcementState i32 astr coord2d call:XferArmySummaryEntryID call:Rva0040E19DXfer i32 call:VRva0040CB11Entry::vector<>::reserve call:XferArmySummaryEntryID ->virtual xfer i32 call:XferArmySummaryEntryID ->virtual xfer |
| 0x80EE5A | B | `ArmySummaryEntry::xfer` | (1, 2) | ver(1, 2) ->CarryoverUnit::xfer bool call:Rva004E075FGet call:XferLivingWorldArmyID bool call:Rva004E075FGet call:Rva004E12D7Parse bool |
| 0x894327 | A | `ArrowStormUpdate::xfer` | (1, 1) | ->SpecialAbilityUpdate::xfer ver(1, 1) call:xferSTLObjectIDList call:XferObjectID i32 bool i32 |
| 0x892B3C | B | `AssaultTransportAIUpdate::xfer` | (1, 1) | ->AIUpdateInterface::xfer ver(1, 1) i32 call:XferObjectID bool coord3d call:XferObjectID i32 u32 boolx3 |
| 0x894F38 | A | `AttachUpdate::xfer` | (1, 3) | ver(1, 3) ->UpdateModule::xfer call:XferObjectID bool call:Eva::xferEvaEventID |
| 0x759E98 | A | `AttackPriorityInfo::xfer` | (1, 1) | ver(1, 1) astr i32 u16 astr i32 |
| 0x89ED19 | A | `AttributeModifierAuraUpdate::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) call:UpgradeMux::upgradeMuxXfer |
| 0x805E89 | A (attempt) | `AttributeModifierPoolUpdate::xfer` | (1, 3) | ver(1, 3) i32 call:Rva004031F3::rva004031F3 i32 call:Rva004031F3::rva004031F3 u32 i32 ->UpdateModule::xfer |
| 0x8BBF93 | A | `AudioLoopUpgrade::xfer` | (1, 1) | ver(1, 1) virtual+0x160 ->UpdateModule::xfer call:UpgradeMux::upgradeMuxXfer |
| 0x85D2FF | A | `AutoAbilityBehavior::xfer` | (1, 2) | ver(1, 2) ->UpdateModule::xfer astr bool coord3d |
| 0x899405 | A | `AutoPickUpUpdate::xfer` | (1, 2) | ver(1, 2) ->UpdateModule::xfer u32 boolx2 |
| 0x741C8E | A | `BackAwayAndCowerStateMachine::xfer` | (1, 1) | ver(1, 1) |
| 0x467CBE | A | `BaseHeightMapRenderObjClass::xfer` | (1, 4) | ver(1, 4) snapshotx4 i32 snapshot i32 |
| 0x89BCD2 | A | `BattlePlanUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) rawx4 u32 boolx2 real i32x3 real ->$0NK::BitFlags<>::xferx2 call:XferObjectID virtual+0x160 |
| 0x8CDA03 | A | `BeaconClientUpdate::xfer` | (1, 1) | ver(1, 1) ->ObjectModule::xfer call:XferParticleSystemID u32 |
| 0x85FAD7 | A | `BezierProjectileBehavior::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) call:XferObjectIDx2 i32 real coord3dx2 bool call:xferSTLObjectIDList coord3d call:Rva00390911XferCoordVector real astrx2 i32x2 u32 |
| 0xAD57E0 | A | `BfmeCellFC::rva006C1680` | (1, 2) | ver(1, 2) u8 raw i32 |
| 0x6DEFD0 | B | `BfmeSubobject0022CE19::xfer` | (1, 3) | ver(1, 3) call:XferSaveFileType astr u16x8 ustr astrx2 i32 call:Rva002DB9EFGet ustrx2 i32 call:Rva002DBA3F::rva002DBA3F i32 ->virtual xferx2 |
| 0x7EE63F | A | `BitRange::rva003ED5A5` | (1, 1) | ver(1, 1) astr |
| 0x7EEBB4 | A | `BitRange::rva003EDAEB` | (1, 1) | ver(1, 1) astr |
| 0x850CD0 | A | `BloodthirstyUpdate::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer call:XferObjectIDx2 u32 |
| 0x88AF21 | A | `BoneFXUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u16 call:XferParticleSystemIDx2 rawx8 bool |
| 0x85AEE4 | A | `BridgeBehavior::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) call:XferObjectID bool u16 call:XferObjectID u32 bool |
| 0x85B6A5 | A | `BridgeScaffoldBehavior::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) call:XferScaffoldTargetMotion coord3dx3 realx2 coord3d |
| 0x85B836 | A | `BridgeTowerBehavior::xfer` | (1, 1) | ver(1, 1) ->Rva004C8A95::xfer call:XferObjectID call:XferBridgeTowerType |
| 0x76438C | A | `BuffManager::xfer` | (1, 1) | ver(1, 1) i32 ->virtual xfer |
| 0x76455D | A | `BuffTransfer003628D3::transfer` | (1, 5) | ver(1, 5) bool rawx2 i32x2 bool astrx2 bool skipBLOK BLOK ->virtual xfer EBLK rgb real ->virtual xfer |
| 0x72B645 | A | `BuildListInfo::xfer` | (1, 1) | ver(1, 1) astrx2 coord3d coord2d real bool u32 astr i32 boolx4 call:XferObjectID u32 bool call:XferObjectID bool i32 bool i32 |
| 0x8BF61F | A | `CallHelpOnDamage::xfer` | (1, 1) | ->ExperienceTrackerAutoResolve::xfer ver(1, 1) u32 |
| 0x780303 | B | `CarryoverUnit::xfer` | (1, 1) | ver(1, 1) astr i32 real call:Rva004E0513::rva004E0513 call:rva003064CB i32 |
| 0x79B9D6 | A | `CastleBehavior::DoXfer` | (1, 2) | ver(1, 2) raw call:XferObjectID boolx2 real call:Rva00398280Xfer call:XferLivingWorldArmyID i32 call:XferObjectIDx2 i32 call:XferObjectIDx2 call:Rva00398280Xfer u32 bool call:FUN_008581a1 call:Rva00398280Xfer astr i32x2 u32 i32x2 u32 |
| 0x974022 | A | `CellGrid::rva0056C50E` | (1, 1) | ver(1, 1) u32x3 realx4 u32x2 |
| 0x94D460 | A | `ChangeStanceGroupOrder::xfer` | (1, 1) | ->GroupOrder::xfer ver(1, 1) call:XferStancesEnum |
| 0x85C5DE | A | `ClickReactionBehavior::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer i32x2 |
| 0x898B7C | B | `CommandButtonHuntUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) astr |
| 0x8C0760 | A | `CrateCollide::xfer` | (1, 2) | ver(1, 2) ->ExperienceTrackerAutoResolve::xfer bool |
| 0x8CD256 | A | `CritterEmitterUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) boolx2 |
| 0x894C09 | A | `DamageFieldUpdate::xfer` | (1, 1) | ver(1, 1) call:xferSTLObjectIDList ->FireWeaponUpdate::xfer |
| 0x88927E | A | `DamageFilteredCreateObjectDie::xfer` | (1, 1) | ver(1, 1) ->ExperienceTrackerAutoResolve::xfer u32 i32 |
| 0x8A9D7C | A | `DefaultProductionExitUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) coord3d bool |
| 0x8C99FB | B | `DeflectSpecialPower::xfer` | (1, 1) | ->Rva00589079::xfer ver(1, 1) boolx2 |
| 0x8C5976 | A | `DelayedDeathBody::xfer` | (1, 1) | ver(1, 1) boolx2 ->Rva004CE56D::xfer |
| 0x8215F3 | A | `DelayedExperienceLevelGrantSystem::xfer` | (1, 1) | ver(1, 1) i32 call:XferObjectID bool astr i32 call:XferObjectID bool astr |
| 0x733042 | A | `DelayedLuaEventList::xfer` | (1, 1) | ver(1, 1) ->virtual xfer |
| 0x8AC4E0 | A | `DelayedLuaEventUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) real boolx2 raw ->virtual xfer |
| 0x88B863 | B | `DeletionUpdate::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer u32 |
| 0x899202 | A | `DemoTrapUpdate::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer i32 bool |
| 0x891D88 | B | `DeployStyleAIUpdate::xfer` | (1, 1) | ->AIUpdateInterface::xfer ver(1, 1) boolx2 call:XferCommandResultTypes u32 call:XferDeployStateTypes call:XferCommandResultTypes call:XferObjectIDx2 coord3d boolx7 call:Rva0026AFDAMember::doXfer |
| 0x8AFD35 | A | `DestroyEnvironmentUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u32 call:XferObjectID |
| 0x8C5EB5 | A | `DetachableRiderBody::xfer` | (1, 2) | ver(1, 2) ->ActiveBody::xfer call:UpgradeMux::upgradeMuxXfer |
| 0x8B285F | D | `DetachableRiderUpdate::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer boolx2 |
| 0x99248C | A | `DockUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) coord3dx3 i32 bool i32 coord3d i32 call:XferObjectID i32 bool call:XferObjectID boolx3 |
| 0x88C966 | B | `DozerAIUpdate::xfer` | (1, 2) | ->AIUpdateInterface::xfer ver(1, 2) i32 call:XferObjectID u32 snapshot raw i32 bool coord3d raw virtual+0x160 bool call:XferObjectID |
| 0x678A04 | A | `Drawable::xferDrawableModules` | (1, 1) | ver(1, 1) u16x2 astr BLOK snapshot EBLK astr skipBLOK BLOK snapshot EBLK |
| 0x8CE37F | A | `DrawModule::xfer` | (1, 1) | ->ObjectModule::xfer ver(1, 1) |
| 0x83F4C2 | A (attempt) | `DynamicAudioEventInfo::xferNoName` | (1, 1) | ver(1, 1) u8x2 bool realx4 u8 |
| 0x863A5F | A | `DynamicPortalBehaviour::xfer` | (1, 2) | ->UpgradeModule::xfer ver(1, 2) call:XferWaypointIDx2 boolx2 |
| 0x88EB76 | A | `DynamicShroudClearingRangeUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) i32x2 u32x5 bool realx3 |
| 0x8B5FD0 | A | `EmotionTrackerUpdate::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) bool u32 call:XferObjectID ->Rva004DD489::xfer i32x3 call:XferObjectIDx2 raw i32 call:XferObjectID u32 bool |
| 0x8E3632 | B | `Energy::xfer` | (1, 1) | ver(1, 1) i32 |
| 0x85C0B5 | A | `EnragedBehavior::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer real |
| 0x5DE35C | A | `Eva::xferEvaEventID` | (1, 1) | ver(1, 1) astrx2 |
| 0x8CDDD7 | A | `EvaAnnounceClientCreate::xfer` | (1, 2) | ver(1, 2) ->ObjectModule::xfer bool u32 |
| 0x8BEDCC | A | `EvacuateDamage::xfer` | (1, 2) | ver(1, 2) ->ExperienceTrackerAutoResolve::xfer |
| 0x733224 | A | `EventParameter::xfer` | (1, 1) | ver(1, 1) real bool call:XferObjectID astr raw |
| 0x79D4E6 | A | `ExperienceTracker::xfer` | (1, 5) | ver(1, 5) real i32 astr real i32x3 bool i32 call:Rva00149101XferNameKey i32 |
| 0x9933FE | A | `ExperienceTrackerAutoResolve::xfer` | (1, 1) | ver(1, 1) |
| 0x9EE079 | A | `ExperienceTrackerAutoResolve::xfer` | (1, 1) | ver(1, 1) |
| 0x864BFE | A | `FakePathfindPortalBehaviour::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) call:XferWaypointIDx2 boolx3 |
| 0x8CB167 | A | `FellBeastSwoopPower::xfer` | (1, 1) | ver(1, 1) ->SpecialAbilityUpdate::xfer bool |
| 0x688384 | A (attempt) | `FireLogicSystem::xfer` | (1, 2) | ver(1, 2) i32x2 enum4 u16x2 i32 icoord2d i32 icoord2d |
| 0x8BF9A5 | A | `FireWeaponCollide::xfer` | (1, 1) | ->ExperienceTrackerAutoResolve::xfer ver(1, 1) bool snapshot bool |
| 0x88F113 | A | `FireWeaponUpdate::xfer` | (1, 2) | ver(1, 2) ->UpdateModule::xfer bool snapshot boolx2 u32 coord3d snapshot bool u32 coord3d snapshot |
| 0x8A8547 | A | `FireWeaponWhenDamagedBehavior::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) u32x2 raw realx2 coord3d real |
| 0x8E2F63 | A | `FiringTracker::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) i32 call:XferObjectIDx2 u32x3 coord3d bool coord3d u32 virtual+0x160 |
| 0x88FBEE | A | `FlammableUpdate::xfer` | (1, 3) | ->UpdateModule::xfer ver(1, 3) call:XferFlammabilityStatusType u32x3 real u32x2 virtual+0x160 bool call:XferObjectID bool |
| 0x891B72 | A | `FloodUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) i32 call:XferObjectID call:Rva00390911XferCoordVector i32 call:XferObjectID call:Rva00390911XferCoordVector i32 bool |
| 0x5FA202 | A | `FXParticleSystem::CPUParticle::rva001FAA8D` | (1, 1) | ver(1, 1) call:Rva001F37C4::rva001F37C4 call:Rva001FA7AC::rva001FA7AC u32 coord3d u32x2 call:XferParticleSystemID |
| 0x9686AE | A | `FXParticleSystem::CylinderEmissionVolumeModuleTemplate::parse` | (1, 1) | call:INI::initFromINI ver(1, 1) bool realx3 coord3d |
| 0x96B147 | A | `FXParticleSystem::DefaultUpdateModuleInfo::xfer` | (1, 2) | ver(1, 2) call:xferRandomVariablex5 call:XferRotationType call:xferRandomVariablex3 |
| 0x769DA1 | B | `FXParticleSystem::GpuDrawModuleInfo::DoXfer` | (1, 1) | ver(1, 1) u32 coord3d i32 real |
| 0x96B6F3 | A | `FXParticleSystem::LifeEventModuleInfo::xfer` | (1, 1) | ver(1, 1) call:xferRandomVariable astr |
| 0x9629A6 | A | `FXParticleSystem::LightningDrawModuleInfo::xfer` | (1, 1) | ver(1, 1) call:xferRandomVariablex3 real bool |
| 0x968A48 | A (attempt) | `FXParticleSystem::LightningEmissionInfo::LightningEmissionInfo` | (1, 1) | ver(1, 1) coord3dx2 call:xferRandomVariablex9 |
| 0x5F461D | A | `FXParticleSystem::ParticleSystemInfo::DoXfer` | (1, 3) | ver(1, 3) bool call:XferParticleShaderType call:XferParticleType astr call:xferRandomVariable u32x2 call:xferRandomVariablex5 astr coord3d astr call:XferParticlePriorityType boolx4 u32 raw bool |
| 0x964696 | A | `FXParticleSystem::RenderObjectDrawModuleInfo::xfer` | (1, 1) | ver(1, 1) bool astr i32 real call:XferParticleShaderType astr i32 real call:XferParticleShaderType astr i32 real call:XferParticleShaderType bool real |
| 0x966009 | A | `FXParticleSystem::RenderObjectUpdateModuleInfo::xfer` | (1, 1) | ver(1, 1) call:xferRandomVariablex12 call:XferRotationType |
| 0x5FA976 | A | `FXParticleSystem::Rva001FB201::DoXfer` | (3, 3) | ->FXParticleSystem::ParticleSystemInfo::DoXfer ver(3, 3) call:Rva001FA7AC::rva001FA7AC call:XferParticleSystemID call:XferDrawableID call:XferObjectID bool call:Rva003062FEXfer bool call:Rva003062FEXfer u32x5 bool real bool astr boolx2 coord3d realx4 bool call:Rva0030612AXfer coord3dx2 bool call:XferParticleSystemIDx2 snapshot |
| 0x96966C | A | `FXParticleSystem::TerrainFireEmissionModuleTemplate::parse` | (1, 1) | call:INI::initFromINI ver(1, 1) call:xferRandomVariablex3 real |
| 0x647ABF | A (attempt) | `GameClient::xfer` | (1, 4) | ver(1, 4) u32 call:GameClient::xferDrawableTOC u16x2 BLOK call:XferObjectID snapshot EBLK u16 skipBLOK call:XferObjectID snapshot EBLK i32 ustr i32 ustr bool real call:XferObjectID snapshotx2 astr boolx2 i32x2 astrx2 call:DynamicAudioEventInfo::xferNoName i32 virtual+0x160 i32 astrx2 call:DynamicAudioEventInfo::xferNoName i32 virtual+0x160 |
| 0x6471EC | A | `GameClient::xferDrawableTOC` | (1, 1) | ver(1, 1) u32 astr u16 u32 astr u16 |
| 0x625D37 | D | `GameLogic::rva0023CFE4` | (1, 1) | ver(1, 1) |
| 0x62EF07 | A | `GameLogic::xferObjectTOC` | (1, 1) | ver(1, 1) u32 astr u16 u32 astr u16 |
| 0x800FD8 | B | `GameSlot::xfer` | (1, 3) | ver(1, 3) call:XferSlotState boolx3 i32x9 ustr astr call:XferLivingWorldPlayerID u32x2 i32 bool i32 ->virtual xfer bool astr bool |
| 0x82B8E4 | B | `GameStateMap::DoXfer` | (1, 2) | ver(1, 2) astrx3 i32x2 astrx2 i32x2 boolx3 call:XferObjectID call:XferDrawableID snapshot |
| 0x87B50B | B | `GarrisonContain::xfer` | (1, 1) | ->OpenContain::xfer ver(1, 1) u32 bool u16 call:XferObjectIDx2 u32x2 call:XferDrawableID i32 coord3d boolx2 coord3d |
| 0xAD50B0 | A | `Gen_008812D0::rva006C0F50` | (1, 1) | ver(1, 1) region3d realx2 i32x2 u32 call:BfmeCellFC::rva006C1680 u32 |
| 0x8575D5 | A (attempt) | `GettingBuiltBehavior::xfer` | (1, 9) | ver(1, 9) ->UpdateModule::xfer bool real boolx3 virtual+0x160 u32 boolx3 i32 bool i32 call:XferObjectID coord3d real i32 call:XferObjectID coord3d real call:Rva0036ABAFXfer boolx2 |
| 0x7071CA | A | `GhostObject::xfer` | (1, 1) | ver(1, 1) call:XferObjectID real coord3d |
| 0x707122 | A | `GhostObjectManager::xfer` | (1, 1) | ver(1, 1) i32 |
| 0x76C96B | B | `GiantBirdAIUpdate::xfer` | (1, 2) | ver(1, 2) ->AIUpdateInterface::xfer call:Rva0026AFDAMember::doXfer coord3d call:Rva0036ABAFXfer i32 real call:XferObjectIDx2 raw real call:XferObjectID bool raw boolx2 coord3d realx4 coord3dx2 bool |
| 0x769521 | A | `GiantBirdAttackMoveToState::xfer` | (1, 1) | ver(1, 1) ->Rva0055CA8A::xfer i32 snapshot call:XferCommandSourceType |
| 0x769C70 | A | `GiantBirdGuardMachine::xfer` | (1, 1) | ver(1, 1) ->AIGuardMachine::xfer |
| 0x86502B | A | `GiantBirdSlowDeathBehavior::xfer` | (1, 1) | ver(1, 1) ->SlowDeathBehavior::xfer u32 bool coord3d real |
| 0x89FB9F | A | `GiveUpgradeUpdate::xfer` | (1, 1) | ->SpecialAbilityUpdate::xfer ver(1, 1) bool real bool |
| 0x8B0F0E | A | `GloriousChargeUpdate::xfer` | (1, 1) | ->SpecialAbilityUpdate::xfer ver(1, 1) call:xferSTLObjectIDList bool u32 |
| 0x76414E | A | `GlowMaterial::xfer` | (1, 1) | ver(1, 1) u32x2 |
| 0x94F2DC | A | `GroupOrder::xfer` | (1, 1) | ver(1, 1) call:Rva00398280Xfer u32x2 |
| 0x869CCC | A | `HealContain::xfer` | (1, 1) | ver(1, 1) |
| 0x8BD56C | A | `HealCrateCollide::xfer` | (1, 1) | ver(1, 1) |
| 0x8C1442 | A | `HealCrateCollide::xfer` | (1, 1) | ver(1, 1) |
| 0x89326E | A | `HeightDieUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) boolx2 coord3d u32 |
| 0x85C1E1 | A | `HitReactionBehavior::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer real i32 |
| 0x8780E4 | A | `HordeContain::xfer` | (1, 13) | call:FUN_00869e86 ver(1, 13) bool u32 boolx2 i32x4 call:XferObjectID i32 call:XferObjectID i32 ->$0CEP::BitFlags<>::xfer i32 call:XferObjectIDx2 i32 call:Rva00468FB4::rva00468FB4x2 bool call:XferObjectID call:XferStancesEnum virtual+0x3c call:XferObjectID u32 call:XferObjectID i32 call:XferObjectID u32 i32x2 coord3d u32 bool ->$0CEP::BitFlags<>::xfer i32 call:XferObjectID coord3d u32 i32 call:XferObjectID coord3d u32 call:XferObjectIDx2 ->$0CEP::BitFlags<>::xferx2 i32 call:XferObjectID i32x2 call:XferObjectID i32x2 u16 real i32 u16 real u32 call:XferObjectID i32 boolx2 call:XferObjectID bool real boolx2 coord3d call:XferObjectID u32 call:Rva00473FD4Xfer call:Rva00470222Xfer bool u32 i32 |
| 0x87CFA6 | A | `HordeGarrisonContain::xfer` | (1, 2) | ver(1, 2) ->GarrisonContain::xfer u32 |
| 0x98F157 | A | `HordeMeleeFormation::rva00587075` | (1, 1) | ver(1, 1) astr bool i32 coord3d bool call:Rva00586231Xfer |
| 0x98CAFF | A | `HordeMeleeSwarm::rva00584A9B` | (1, 1) | ver(1, 1) astr bool i32 coord3d bool |
| 0x8805FA | A | `HordeSiegeEngineContain::xfer` | (1, 1) | ->HordeTransportContain::xfer ver(1, 1) u32 call:XferObjectID u32 call:XferObjectID bool i32 call:XferObjectID i32x2 call:XferObjectID i32 |
| 0x87A3EB | A | `HordeTransportContain::xfer` | (1, 1) | ver(1, 1) call:FUN_00869e86 i32 bool |
| 0x87992C | A | `HorseHordeContain::xfer` | (1, 1) | ver(1, 1) |
| 0x8C6606 | A | `ImmortalBody::xfer` | (1, 1) | ver(1, 1) |
| 0x6A73C6 | B | `InGameUI::xfer` | (1, 2) | real ver(1, 2) i32 boolx2 i32 astr ustr bool i32 astr ustr boolx2 i32 astrx2 call:XferObjectID u32 boolx3 i32x2 call:XferDrawableID |
| 0x8BD648 | A | `InheritUpgradeCreate::xfer` | (1, 1) | call:FUN_008bce8f ver(1, 1) |
| 0x8A6FFA | B | `InvisibilityUpdate::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) bool |
| 0x60D899 | A | `LargeGroupAudio::xfer` | (1, 5) | ver(1, 5) bool i32 boolx2 i32 astr skipBLOK call:LargeGroupAudioAudioMap::rva003EE23C EBLK astr BLOK call:LargeGroupAudioAudioMap::rva003EE23C EBLK |
| 0x8AF0E5 | A | `LargeGroupAudioUpdate::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) coord2d ->$0CEP::BitFlags<>::xfer ->$0GF::BitFlags<>::xfer bool i32 bool |
| 0x764C4D | A | `LaserUpdate::xfer` | (1, 2) | ->ObjectModule::xfer ver(1, 2) coord3dx2 bool call:XferParticleSystemIDx2 boolx2 u32x2 real u32x2 call:XferDrawableIDx2 u32 |
| 0x5EB2AC | B | `LinearCampaignManager::DoXfer` | (1, 1) | ver(1, 1) bool astr call:XferGameDifficulty skipBLOK snapshot EBLK astr call:XferGameDifficulty BLOK snapshot EBLK |
| 0x7F7300 | A (attempt) | `LivingWorldBattle::BattlePlayer::DoXfer` | (1, 4) | ver(1, 4) call:XferLivingWorldPlayerID call:Rva002BA8F1Logic::find call:XferLivingWorldPlayerID call:Rva002BA8F1Logic::rva002B8D06 call:Rva003F3FFE::rva003F3FFE call:Rva003F5729Xfer u32 ->virtual xfer |
| 0x8E52F7 | B | `LivingWorldBuilding::DoXfer` | (1, 6) | ver(1, 6) coord2d i32 bool astr i32 astr BLOK snapshot EBLK skipBLOK astr i32 astr BLOK snapshot EBLK bool snapshot call:Rva004E075FGet call:Rva003EFE82Get bool |
| 0x902E0A | B | `LivingWorldBuildPlot::DoXfer` | (1, 1) | ver(1, 1) call:XferLivingWorldBuildPlotID coord2d i32 bool call:Rva003EFE82Get call:Rva004E075FGet astr snapshotx2 astr call:Rva003EFE82Get call:Rva004E075FGet astr snapshot astr |
| 0x6BBD55 | A | `LivingWorldLogic::rva002BB2B7` | (1, 7) | ver(1, 7) call:Rva003EFE82Get boolx2 i32 boolx4 call:XferGameDifficulty u32x2 boolx2 u32 call:LivingWorldLogic::XferPlayers call:XferOwningLivingWorldArmyVec call:Rva002BA8F1Logic::rva002B8D06x2 call:Rva002B2435Getx2 boolx2 call:LivingWorldLogic::XferDelayedRegionVictories call:Rva002B9CFC::rva002B9CFC |
| 0x6BA784 | A | `LivingWorldLogic::XferDelayedRegionVictories` | (1, 2) | ver(1, 2) i32 call:XferLivingWorldPlayerID call:Rva003EFE82Get call:Rva002B24F0Get i32 call:XferLivingWorldPlayerID call:Rva003EFE82Get call:Rva002B24F0Get |
| 0x6BB501 | B | `LivingWorldLogic::XferPlayers` | (1, 1) | ver(1, 1) i32 snapshot raw i32 snapshot raw |
| 0x7F5279 | B | `LivingWorldRegion::DoXfer` | (1, 3) | ver(1, 3) call:Rva003EFE82Get bool call:XferLivingWorldPlayerIDx2 i32 coord2d boolx2 i32x3 coord2d call:Rva003F2394Xferx2 i32 snapshot call:Rva004E075FGet i32 snapshot call:Rva004E075FGet bool i32 call:Rva002BA8F1Logic::rva002B8D06 i32x2 |
| 0x7F46E0 | A | `LivingWorldRegionConnection::DoXfer` | (1, 1) | ver(1, 1) astr call:Rva003EFE82Get call:Rva003F2C0CXfer |
| 0x60FFC4 | A | `LivingWorldRegionManager::DoXfer` | (1, 1) | ver(1, 1) i32 call:XferLivingWorldBattleID astrx2 call:LivingWorldRegionManager::XferRegions call:Rva0020EE29::rva002101C6 call:XferLivingWorldBattleIDx2 |
| 0x60ECD6 | B | `LivingWorldRegionManager::XferRegions` | (1, 1) | ver(1, 1) i32 astr call:Rva003EFE82Get ->virtual xfer i32 astr call:Rva003EFE82Get ->virtual xfer |
| 0x5E7F12 | A | `LocomotorSet::DoXfer` | (1, 2) | ver(1, 2) u16 astr snapshot i32 boolx2 |
| 0x5E68A4 | A | `LocomotorSet::xferSelfAndCurLocoPtr` | (1, 1) | ver(1, 1) snapshot astrx2 |
| 0x775D6F | A | `MineshaftPortalBehaviour::rva00373A0F` | (1, 1) | ->UpdateModule::xfer ver(1, 1) call:XferWaypointIDx2 boolx2 u32 call:XferObjectIDx2 |
| 0x8AB040 | A | `MissileUpdate::xfer` | (1, 1) | ver(1, 1) ->BezierProjectileBehavior::xfer coord3dx2 u32 call:XferObjectIDx2 bool u32 real boolx2 coord3d u32 rawx2 |
| 0x894C52 | A | `MonitorConditionUpdate::xfer` | (1, 1) | ver(1, 1) astr ->UpdateModule::xfer |
| 0x8A4B72 | A | `MonsterDockUpdate::xfer` | (1, 1) | ->DockUpdate::xfer bool i32 bool ver(1, 1) |
| 0x94E6B8 | A | `MoveToFormationGroupOrder::xfer` | (1, 1) | ->GroupOrder::xfer ver(1, 1) i32 coord3d real bool call:Rva0054755BXfer bool |
| 0x94E0D4 | A | `MoveToGroupOrder::xfer` | (1, 2) | ->GroupOrder::xfer ver(1, 2) coord3d bool call:Rva0054755BXfer bool |
| 0x8B83A6 | A | `ObjectCreationUpgrade::xfer` | (1, 1) | ver(1, 1) bool u32 ->UpdateModule::xfer call:UpgradeMux::upgradeMuxXfer |
| 0x8E3B04 | A | `ObjectDefectionHelper::xfer` | (1, 1) | ->Rva004DF81B::xfer ver(1, 1) u32x2 real bool |
| 0x763EDF | A | `ObjectFilter::DoXfer` | (1, 2) | ver(1, 2) bool i32 ->$0NK::BitFlags<>::xferx2 i32 call:ObjectFilter::DoTemplatesXferx2 call:FUN_007631bdx2 i32 |
| 0x7082E2 | A | `ObjectModule::xfer` | (1, 1) +1 | ver(1, 1)x2 |
| 0x8E2CB6 | B | `ObjectSMCHelper::xfer` | (1, 1) | ->Rva004DF81B::xfer ver(1, 1) i32 call:XferModelConditionFlagType u32 call:XferModelConditionFlagType u32 |
| 0x778F8C | B | `ObjectTypes::xfer` | (1, 1) | ver(1, 1) astr u16 astrx2 |
| 0x89D109 | A | `OneRingPenaltyUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) call:XferObjectID u32 real u32 |
| 0x868FEF | A | `OpenContain::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u32 call:XferObjectID u32 call:XferObjectID i32 call:XferObjectID astr i32 call:XferObjectID astr i32 call:XferObjectID i32x2 call:XferObjectID i32 u32x5 ->$0CEP::BitFlags<>::xfer coord3d bool call:XferObjectIDx2 i32 u16 call:XferObjectID raw call:XferObjectID raw i32 u32 call:XferObjectID u32 call:XferObjectID u32 boolx3 |
| 0x8B0125 | A | `PartTheHeavensUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u32 |
| 0x7683B8 | A | `PathfindLayer::rva003666FD` | (1, 1) | ver(1, 1) call:FUN_009341b7 i32x4 icoord2dx2 i32x2 bool i32 |
| 0x93A43D | A | `PathfindZoneManager::Block::DoXfer` | none | i32 call:XferWaypointID |
| 0x9382DD | A | `PathfindZoneManager::CellType::DoXfer` | none | u16x7 |
| 0x93AC0F | A | `PathfindZoneManager::DoXfer` | none | bool ->PathfindZoneManager::Block::DoXfer u16 bool ->PathfindZoneManager::CellType::DoXfer u32x2 call:Rva00531A44::rva00531BA7x2 |
| 0x793113 | A | `PhysicsBehavior::xfer` | (1, 2) | ver(1, 2) ->UpdateModule::xfer i32 realx2 coord3dx2 i32 boolx2 i32 boolx2 i32 call:Rva00390911XferCoordVector |
| 0x888247 | A | `PillageModule::xfer` | (1, 1) | ver(1, 1) ->Rva00589051::xfer u32 |
| 0x6B205F | B (attempt) | `Player::DoXfer` | (1, 10) | ver(1, 10) bool snapshotx2 u16 bool call:Rva001ECA2DXferx2 astr snapshot astr snapshot i32 bool i32 bool call:rva003064CBx2 snapshot bool u16 u32x2 u16 snapshot bool snapshotx2 bool snapshot bool snapshot u32 call:Rva001ECA2DXfer call:Rva00380200::rva00380499 i32 real i32x4 ustr snapshotx2 boolx4 i32 real boolx2 i32 u32 snapshot u16 call:Rva002AE71D::rva002AE71Dx2 u16 real call:XferObjectID real i32 u16 astr real i32 u16 u32x4 u16 snapshot bool snapshot bool realx2 i32x3 ->$0NK::BitFlags<>::xferx2 i32x3 bool snapshot real call:Rva002AC1A9XferList u32 call:Player::rva002AF236x2 call:Player::rva002AF33B call:XferOrderMode u32 call:Rva002B0A05::rva002B0A05x2 |
| 0x6A8788 | A | `PlayerList::DoXfer` | (1, 1) | ver(1, 1) i32 snapshot |
| 0x6AE9B7 | A | `PlayerRelationMap::xfer` | (1, 1) | ver(1, 1) u16 i32 call:XferRelationship i32 call:XferRelationship |
| 0x886326 | A | `PoisonedBehavior::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u32x2 real call:XferDeathType |
| 0x8A3111 | B | `ProductionUpdate::xfer` | (1, 8) | ->UpdateModule::xfer ver(1, 8) u16 raw astr raw realx2 i32 real i32x3 bool raw i32 real call:Rva00460216XferList bool raw astr raw realx2 i32 real i32x3 bool raw i32 real call:Rva00460216XferList bool raw u32x3 call:XferObjectID u32 bool u32x3 bool ->$0CEP::BitFlags<>::xferx2 bool u32 virtual+0x160 call:xferAsciiStringVector bool |
| 0x740954 | A | `ProjectileStreamUpdate::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) call:XferObjectID i32x2 call:XferObjectIDx2 coord3d |
| 0x884A95 | A | `PropagandaTowerBehavior::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer u32 u16 call:XferObjectIDx2 |
| 0x8A38D6 | B | `QueueProductionExitUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u32 coord3d bool real call:XferObjectID u32 |
| 0x6D962D | A (attempt) | `Radar::xfer` | (1, 3) | ver(1, 3) boolx2 u16 call:XferRadarEventType bool u32x3 rgbaIntx2 coord3d icoord2d bool i32x2 realx4 iregion2d bool iregion2d u32 i32 u32 |
| 0x6D9E1D | B | `RadarObject::xfer` | (1, 1) | ver(1, 1) call:XferObjectID i32 |
| 0x8A438F | A | `RadarUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u32 boolx2 |
| 0x89F91F | A | `RadiateFearUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) call:UpgradeMux::upgradeMuxXfer |
| 0x8AF835 | A | `RainOfFireUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u32 realx4 |
| 0x8D01C2 | A | `RandomSoundSelectorClientBehavior::xfer` | (1, 1) | ->ObjectModule::xfer ver(1, 1) real u32 |
| 0x886669 | A | `RebuildHoleBehavior::xfer` | (1, 3) | ver(1, 3) ->UpdateModule::xfer call:XferObjectIDx3 u32 astrx2 bool real |
| 0x8899A4 | A | `RebuildHoleExposeDie::xfer` | (1, 1) | ver(1, 1) ->ExperienceTrackerAutoResolve::xfer |
| 0x7F0EE3 | C | `RegionBonusInfo::xfer` | (1, 3) | ver(1, 3) i32x14 |
| 0x8A4900 | A | `RepairDockUpdate::xfer` | (1, 1) | ->DockUpdate::xfer ver(1, 1) call:XferObjectID real |
| 0x8B6AE3 | A | `ReplaceObjectUpdate::xfer` | (1, 1) | ->SpecialAbilityUpdate::xfer ver(1, 1) |
| 0x8B319D | B | `RespawnUpdate::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer call:XferRespawnModeType call:XferObjectID i32x2 bool i32 real bool |
| 0x8815F7 | A | `RiderChangeContain::xfer` | (1, 1) | ver(1, 1) ->SiegeEngineContain::xfer i32 u32 |
| 0x8B0923 | A | `RousingSpeechUpdate::xfer` | (1, 2) | ->SpecialAbilityUpdate::xfer ver(1, 2) call:xferSTLObjectIDList bool u32 realx2 |
| 0x45C33E | A | `Rva0005CC28Xfer` | (1, 1) | ver(1, 1) label call:FUN_0045140dx2 |
| 0x463FDD | A | `Rva000647D4::xfer` | (1, 1) | ver(1, 1) |
| 0x49A527 | A | `Rva0009AAFB::rva0009AAFB` | (1, 1) | ver(1, 1) call:Rva002BE833::rva002BE833 realx3 call:Rva0030612AXfer |
| 0x4A85A9 | A | `Rva000A8B6D::rva000A8B79` | (1, 1) | ver(1, 1) real i32x2 |
| 0x4A865B | A | `Rva000A8C2BInit` | (1, 1) | ver(1, 1) real i32x2 |
| 0x4BBD4B | A | `Rva000BC559Xfer` | (1, 1) | ver(1, 1) label call:XferDrawableIDx2 |
| 0x4E504B | A | `Rva000E5725::rva000E5725` | (1, 1) | ver(1, 1) astr call:XferDrawableID call:Rva003062FEXfer boolx3 realx2 rawx2 boolx2 |
| 0x4E5D02 | A | `Rva000E63DCDo` | (1, 1) | ver(1, 1) |
| 0x5E4AC2 | A | `Rva001E3E43::xfer` | (1, 3) | ver(1, 3) coord3d realx8 u32 realx5 bool realx2 coord3d real u32x2 bool raw bool u32 |
| 0x5EC166 | A | `Rva001ECA2DXfer` | (1, 1) | ver(1, 1) label call:Rva00306232XferSciencex2 |
| 0x5F2E58 | A | `Rva001F37C4::rva001F37C4` | (1, 1) | ver(1, 1) coord3dx4 u32 bool |
| 0x5F88C6 | A | `Rva001F9105Snapshot::rva001F9105` | (1, 2) | ver(1, 2) call:XferParticleSystemID u32 i32x2 astr snapshot astr |
| 0x606358 | A | `Rva00206923XferList` | (1, 1) | ver(1, 1) label call:Rva00203D86Chainx2 |
| 0x606537 | A | `Rva00206B02Xfer` | (1, 1) | ver(1, 1) label call:Rva00203E2CXferx2 |
| 0x606A44 | A | `Rva0020700FXfer` | (1, 1) | ver(1, 1) label astrx2 |
| 0x6079BA | A | `Rva00207F91XferList` | (1, 1) | ver(1, 1) label call:Rva00203D6AChainx2 |
| 0x607B03 | A (attempt) | `Rva002080DAXferList` | (1, 1) | ver(1, 1) label call:Rva00203DA2Chainx2 |
| 0x607BF0 | A | `Rva002081C7XferList` | (1, 1) | ver(1, 1) label call:Rva00203E11Xferx2 |
| 0x60FEA8 | A | `Rva0020EE29::rva002100E6` | (1, 1) | ver(1, 1) i32 call:W4ScienceType::vector<>::reserve snapshot i32 snapshot |
| 0x60FF88 | A | `Rva0020EE29::rva002101C6` | (1, 1) | ver(1, 1) call:Rva0020EE29::rva002100E6x2 |
| 0x613AFD | B | `Rva00213D93::rva00213D93` | (1, 1) | ver(1, 1) call:FUN_00611f8b call:FUN_006139d8 ->virtual xfer |
| 0x81B157 | B | `Rva002542F3Member::rva00438592` | (1, 1) | ver(1, 1) u32 ->$0GI::BitFlags<>::xfer call:XferInvisibilityType call:rva003064CB u32 astrx4 ->$0GF::BitFlags<>::xfer |
| 0x8DB33A | A | `Rva00263895Member::rva004D6F86` | (1, 1) | ver(1, 1) |
| 0x754ECC | A | `Rva0026AFDAMember::doXfer` | (1, 2) | ver(1, 2) call:XferAICommandType call:XferCommandSourceType coord3d call:XferObjectIDx2 astrx2 i32 coord3dx2 u32 astr i32 real astr bool call:FUN_00767eb7 |
| 0x734289 | B (attempt) | `Rva0026E7C8Ctor::rva00332975` | (1, 1) | ver(1, 1) i32 astrx4 call:xferSTLObjectIDList |
| 0x66F189 | A | `Rva0026EE11::rva0026EE11` | (1, 1) | ver(1, 1) |
| 0x671D09 | A | `Rva00271A0F::rva00271A0F` | (1, 1) | ver(1, 1) call:Rva0030612AXferx4 u32 bool i8 |
| 0x680753 | A | `Rva0027C230::rva0028002F` | (1, 2) | ver(1, 2) coord3d call:XferDrawableID astr bool coord3d i32 i16 boolx2 u32 |
| 0x686412 | B | `Rva00285CEDHost::rva00285CED` | (1, 1) | ver(1, 1) icoord2d i32 |
| 0x8DB489 | A | `Rva0028C6D6::rva004D70D5` | (1, 1) | ver(1, 1) |
| 0x6A9702 | B (partial-authored) | `Rva002A8CCB::rva002A8CCB` | (1, 2) | ver(1, 2) call:AIEconomyBuilder::DoXferFarmLibrary call:Rva004E99F1Xfer call:AIGameTeam::rva004E951C call:Rva004DFA0E::rva004DFA0E |
| 0x6ACC78 | A | `Rva002AC1A9XferList` | (1, 1) | ver(1, 1) label u16x2 |
| 0x830D3F | A | `Rva002AC340::xfer` | (1, 1) | ver(1, 1) i32x5 |
| 0x6A823A | B | `Rva002AE5EB::rva002A7818` | (1, 7) | ver(1, 7) i32x8 call:XferObjectID ->ObjectFilter::DoXfer i32x2 call:XferObjectID ->ObjectFilter::DoXfer i32 bool |
| 0x6AF216 | A | `Rva002AE71D::rva002AE71D` | (1, 1) | ver(1, 1) ->ObjectFilter::DoXfer call:FUN_006ae8d2 call:XferObjectID bool |
| 0x6B155B | A | `Rva002B0A05::rva002B0A05` | (1, 1) | ver(1, 1) astr u32x2 realx2 u32 astrx2 real |
| 0x6BA8C9 | A | `Rva002B9CFC::rva002B9CFC` | (1, 1) | ver(1, 1) u32 snapshot u32 snapshot |
| 0x6B98E0 | A | `Rva002BA8F1Logic::rva002B8D06` | (1, 1) | ver(1, 1) i32 call:XferLivingWorldArmyID i32 call:XferLivingWorldArmyID |
| 0x6BC07B | A | `Rva002BB5DD::rva002BB5DD` | (1, 3) | ver(1, 3) call:LivingWorldLogic::rva002BB2B7 i32 call:Rva002B22AF::rva002B22AF call:FUN_006bbf3ax2 call:Rva002B8BFE::rva002B8BFE |
| 0x6BC4E3 | A | `Rva002BBA45::rva002BBA45` | (1, 3) | ver(1, 3) call:LivingWorldLogic::rva002BB2B7 i32 call:Rva002B22AF::rva002B22AF call:FUN_006bbf3ax2 call:Rva002B8BFE::rva002B8BFE |
| 0x6BD0D5 | A | `Rva002BC653Snapshot::rva002BC653` | (1, 6) | ver(1, 6) call:XferLivingWorldArmyID call:Rva004E075FGet call:XferLivingWorldBuildPlotID call:Rva004E12D7Parse call:XferLivingWorldPlayerID bool call:Rva002BBA45::rva002BBA45 bool call:Rva002BB5DD::rva002BB5DD ->virtual xfer bool ->virtual xfer call:GameLogic::rva0023CFE4 ->virtual xfer snapshot i32 |
| 0x6BF3DE | A | `Rva002BE833::rva002BE833` | (1, 1) | ver(1, 1) realx2 bool realx2 coord3dx2 realx2 |
| 0x6C67C4 | A | `Rva002C589B::rva002C5B46` | (1, 2) | ver(1, 2) u32x3 coord3d boolx2 i32x2 u32 real i32 call:XferObjectID u32 |
| 0x6D7494 | A (partial-authored) | `Rva002D65EEForwarder::rva002D65EE` | (1, 4) | ver(1, 4) call:Rva002D2D13::rva002D2D13 call:Rva002D342AXfer20 bool call:FUN_0092c86f i32x2 bool i32x2 boolx3 |
| 0x6DB0F1 | A | `Rva002D9F9FOwner::rva002D9F9F` | (1, 6) | ver(1, 6) call:AudioEventRTS::internalXfer |
| 0x6DB12B | A | `Rva002D9FD9Owner::rva002D9FD9` | (1, 6) | ver(1, 6) bool u32 call:AudioEventRTS::internalXfer |
| 0x6DCB85 | A | `Rva002DBA3F::rva002DBA3F` | (1, 1) | ver(1, 1) call:Rva003EFE82Get rgb |
| 0x6DCF31 | B | `Rva002DBDB4::rva002DBDB4` | (1, 3) | ver(1, 3) i32 ->virtual xfer call:Rva002DBD05Get i32 bool i32 |
| 0x932478 | B | `Rva002E0A0A::xfer` | (1, 2) | ver(1, 2) ustr astrx3 i32 astr i32 bool i32 |
| 0x6E3402 | A | `Rva002E208AXfer` | (1, 1) | ver(1, 1) label call:XferLivingWorldRegionBonusRuleIDx2 |
| 0x6EC0F4 | A (attempt) | `Rva002EAC9F::rva002EAC9F` | (1, 1) | ver(1, 1) u32x2 call:XferObjectID |
| 0x6F3110 | A | `Rva002F1CDDXfer` | (1, 1) | ver(1, 1) label call:XferObjectIDx2 |
| 0x6FFE8D | C | `Rva002FE76BListPrefix::find` | (1, 1) | snapshot astr snapshot astr snapshot ver(1, 1) u32 raw |
| 0x707CA0 | B | `rva003064CB` | (1, 1) | ver(1, 1) u16 astr u16 astr |
| 0x7080E5 | A | `Rva00306910::xfer` | (1, 1) | ver(1, 1) call:XferObjectID astr call:XferObjectID astr call:Z::YAPAXI::operator new |
| 0x71A2E3 | A | `Rva003189ADOwner::rva003189AD` | (1, 4) | ver(1, 4) call:XferGlobalWeatherType virtual+0x160 call:XferGlobalWeatherAffectsType ->ObjectFilter::DoXfer astr call:XferAttributeModifierCategoryType u32x3 ->$0P::BitFlags<>::xfer |
| 0x741C2A | A | `Rva0033F33D::xfer` | (1, 1) | ver(1, 1) snapshot |
| 0x7437B7 | A | `Rva0033F6DA::xfer` | (1, 1) | ver(1, 1) bool snapshot |
| 0x741923 | A | `Rva0033FF2B::xfer` | (1, 3) | ver(1, 3) coord3d call:XferPathfindLayerEnum bool coord3d u32x2 bool virtual+0x160 bool real |
| 0x741AD0 | A | `Rva00340123::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer i32 bool |
| 0x741DBE | A | `Rva00340411::xfer` | (1, 1) | ver(1, 1) |
| 0x741E0E | A | `Rva00340461::xfer` | (1, 1) | ver(1, 1) bool ->Rva00340123::xfer |
| 0x742495 | A | `Rva00340BDC::xfer` | (1, 3) | ver(1, 3) ->Rva0033FF2B::xfer coord3d icoord2d u32 bool i32 snapshot u32 bool |
| 0x742661 | A | `Rva00340D1F::xfer` | (1, 2) | ver(1, 2) ->Rva0033FF2B::xfer i32 boolx2 i32 |
| 0x742721 | A | `Rva00340D70::xfer` | (1, 3) | ver(1, 3) ->Rva0033FF2B::xfer i32 boolx3 i32 call:XferCommandSourceType snapshot real boolx2 |
| 0x7429EC | A | `Rva0034103B::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer coord2d real i32 u32x2 bool |
| 0x742B2D | A | `Rva0034117C::xfer` | (1, 1) | ver(1, 1) ->Rva0034103B::xfer snapshot |
| 0x742BB6 | A | `Rva00341205::xfer` | (1, 1) | ver(1, 1) ->Rva0034103B::xfer i32x2 |
| 0x74334C | A | `Rva00341930::xfer` | (1, 2) | ver(1, 2) ->Rva0033FF2B::xfer call:XferObjectID u32 |
| 0x743849 | B | `Rva00341E22::xfer` | (1, 1) | ver(1, 1) |
| 0x743EB4 | A | `Rva003424B1::xfer` | (1, 1) | ver(1, 1) boolx3 i32 ->Rva0033FF2B::xfer |
| 0x743F05 | A | `Rva00342502::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer bool |
| 0x743F2F | A | `Rva0034252C::xfer` | (1, 1) | ver(1, 1) |
| 0x7441AC | B | `Rva003427A9::rva003427A9` | (1, 1) | ver(1, 1) u32 coord3d |
| 0x742052 | A | `Rva003429B9::xfer` | (1, 3) | ver(1, 3) ->Rva0033FF2B::xfer coord3d u32 boolx2 |
| 0x7425DE | A | `Rva00342B87::xfer` | (1, 1) | ver(1, 1) ->Rva0033FF2B::xfer i32 coord3d |
| 0x742848 | A | `Rva00345CAB::xfer` | (1, 2) | ver(1, 2) ->Rva0033FF2B::xfer u32 i32 snapshot coord3d call:XferObjectID bool call:XferCommandSourceType |
| 0x756A00 | A | `Rva00354EFCXfer` | (1, 1) | ver(1, 1) call:Rva00149101XferNameKeyx2 snapshot |
| 0x756B9A | A | `Rva00355096Xfer` | (1, 1) | ver(1, 1) call:Rva0054840A::rva0054840A |
| 0x75706D | A | `Rva0035552AXfer` | (1, 1) | ver(1, 1) label call:Rva00355100Xferx2 |
| 0x7571D2 | A | `Rva0035568FXfer` | (1, 1) | ver(1, 1) label call:Rva0035511BXferx2 |
| 0x7572B3 | A | `Rva00355770::rva00355770` | (1, 1) | ver(1, 1) u32 call:Rva0035552AXfer call:Rva0035568FXfer |
| 0x769308 | A | `Rva00367647::xfer` | (1, 1) | ver(1, 1) i32 bool |
| 0x76C8A7 | A | `Rva0036ABAFXfer` | (1, 1) | ver(1, 1) label call:XferObjectIDx2 |
| 0x782982 | A | `Rva00380200::rva00380499` | (1, 2) | ver(1, 2) realx2 i32x4 |
| 0x95C3FD | B | `Rva003844D7::rva00555109` | (1, 1) | call:Rva00553E47StatsCore::rva00555109 ver(1, 1) call:PSPlayerStats::rva00554A4A call:PSPlayerStats::XferMapx2 call:PSPlayerStats::rva0055499Ax2 |
| 0x95C1B0 | B | `Rva0038454E::rva00555109` | (1, 1) | call:Rva00553E47StatsCore::rva00555109 ver(1, 1) call:PSPlayerStats::rva0055499Ax8 call:PSPlayerStats::XferMapx2 call:PSPlayerStats::rva0055499Ax5 |
| 0x95C851 | B | `Rva00385333::rva00555DDC` | (1, 1) | call:Rva00553E47StatsCore::rva005550A0 ver(1, 1) u16x2 i32x2 astrx2 |
| 0x792CD4 | A | `Rva00390911XferCoordVector` | (1, 1) | ver(1, 1) label coord3dx2 |
| 0x79A7AD | A | `Rva00398280Xfer` | (1, 1) | ver(1, 1) label call:XferObjectIDx2 |
| 0x79D62B | A | `Rva0039ADF3::xfer` | (1, 1) | ver(1, 1) ->ExperienceTracker::xfer call:XferObjectID |
| 0x79D229 | A | `Rva0039AE75::xfer` | (1, 1) | ver(1, 1) real i32 |
| 0x79EA0A | A | `Rva0039C4F7XferSnapshotVector` | (1, 1) | ver(1, 1) label snapshotx2 |
| 0x7A7796 | A | `Rva003A4322::xfer` | (1, 2) | ver(1, 2) ->UpdateModule::xfer call:XferObjectID real |
| 0x7A7E88 | A | `Rva003A49D1::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) u32x2 bool |
| 0x7A814E | A | `Rva003A4CAE::xfer` | (1, 3) | ver(1, 3) ->UpdateModule::xfer coord3dx2 real i32 bool u32 |
| 0x9668E4 | A | `Rva003A547E::rva003A547E` | (1, 1) | ver(1, 1) call:XferWindMotion realx13 i8 realx2 |
| 0x966BEA | A | `Rva003A578A::DoXfer` | (1, 1) | ver(1, 1) real |
| 0x9661D1 | A | `Rva003A57A9::DoXfer` | (1, 1) | ver(1, 1) |
| 0x96B811 | A | `Rva003AE0AF::xfer` | (1, 2) | ver(1, 2) call:Rva003B0E38::rva003B0E38 boolx2 |
| 0x96BE27 | A | `Rva003AE2A9::xfer` | (1, 2) | ver(1, 2) call:Rva00564522::rva00564522 boolx2 |
| 0x9629F4 | A | `Rva003AE94E::xfer` | (1, 1) | ver(1, 1) |
| 0x967737 | A | `Rva003AF22E::xfer` | (1, 1) | ver(1, 1) |
| 0x7B095A | A | `Rva003AFDAA::xfer` | (1, 1) | ver(1, 1) u32 |
| 0x96B79E | A | `Rva003B0E38::rva003B0E38` | (1, 1) | ver(1, 1) u32 |
| 0x7F3A11 | A | `Rva003F2394Xfer` | (1, 1) | ver(1, 1) label call:XferLivingWorldArmyIDx2 |
| 0x7F4266 | A | `Rva003F2C0CXfer` | (1, 1) | ver(1, 1) label coord2dx2 |
| 0x7F5697 | A | `Rva003F3FFE::rva003F3FFE` | (1, 1) | ver(1, 1) i32x4 call:XferLivingWorldPlayerID call:Rva002BA8F1Logic::find call:XferLivingWorldPlayerID |
| 0x7F6D80 | A | `Rva003F5729Xfer` | (1, 1) | ver(1, 1) label snapshotx2 |
| 0x7F70B0 | A | `Rva003F5A72Xfer` | (1, 1) | ver(1, 1) label call:XferLivingWorldPlayerIDx2 |
| 0x7F7DE8 | A | `Rva003F6672::rva003F6672` | (1, 2) | ver(1, 2) i32x2 ->LivingWorldBattle::BattlePlayer::DoXfer call:Rva003F5A72Xfer |
| 0x7FC619 | A | `Rva003FAFB9::xfer` | (1, 1) | ver(1, 1) virtual+0x160 boolx4 |
| 0x7FEF94 | A | `Rva003FD789::xfer` | none | astr coord3d |
| 0x806752 | A | `Rva00404AB1::rva00404AB1` | (1, 1) | ver(1, 1) realx2 u32x2 |
| 0x8112C6 | A | `Rva0040DB52::xfer` | (1, 1) | ver(1, 1) coord2d bool i32 call:Rva003F2394Xfer call:Rva0040E269Xfer |
| 0x810D6D | A | `Rva0040E19DXfer` | (1, 1) | ver(1, 1) label call:XferArmySummaryEntryIDx2 |
| 0x810E39 | A | `Rva0040E269Xfer` | (1, 1) | ver(1, 1) label i32x2 |
| 0x8255CC | A (attempt) | `Rva00414C92` | (1, 1) | ver(1, 1) label snapshotx2 |
| 0x835E3F | A | `Rva004266BCProcess` | (1, 1) | ver(1, 1) astr boolx3 |
| 0x836835 | A | `Rva00426EB0::xfer` | (1, 1) | ver(1, 1) call:Rva00426F17Xfer |
| 0x83669A | A | `Rva00426F17Xfer` | (1, 1) | ver(1, 1) label call:Rva004266BCProcessx2 |
| 0x81B31D | B | `Rva004382FC::rva00438758` | (1, 1) | ver(1, 1) call:Rva002542F3Member::rva00438592 u32x2 bool |
| 0x859324 | A | `Rva004561A2::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer boolx2 |
| 0x85BB36 | A | `Rva00458AAE::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer real i32x2 |
| 0x85DF3D | A | `Rva0045ADC1::xfer` | (1, 2) | ver(1, 2) ->UpdateModule::xfer bool u32 |
| 0x85E1C7 | A | `Rva0045B134::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer bool |
| 0x8A44F8 | A | `Rva0045CE8C::xfer` | (1, 1) | ver(1, 1) |
| 0x8632C6 | A | `Rva00460216XferList` | (1, 1) | ver(1, 1) label i32x2 |
| 0x86C0D1 | A | `Rva00468FB4::rva00468FB4` | (1, 1) | ver(1, 1) call:Rva003063A9XferThingTemplate i32 |
| 0x8722AE | A | `Rva0046EFDBXfer` | (1, 1) | ver(1, 1) label realx2 |
| 0x873449 | A | `Rva00470222Xfer` | (1, 1) | ver(1, 1) label call:FUN_0086c28ax2 |
| 0x87720C | A | `Rva00473FD4Xfer` | (1, 1) | ver(1, 1) label call:Rva00469103Xferx2 |
| 0x89A1AC | A | `Rva0047F94C::xfer` | (1, 2) | ver(1, 2) ->UpdateModule::xfer |
| 0x88588B | A | `Rva00482641::xfer` | (1, 1) | ->UpdateModule::xfer call:UpgradeMux::upgradeMuxXfer ver(1, 1) bool snapshot bool snapshot bool snapshot bool snapshot bool snapshot bool snapshot bool snapshot bool snapshot |
| 0x885E47 | A | `Rva00482E96::xfer` | (1, 3) | ver(1, 3) ->UpdateModule::xfer call:UpgradeMux::upgradeMuxXfer i32 |
| 0x886E5C | A | `Rva004839D3::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u32x2 |
| 0x887082 | A | `Rva00483C35::xfer` | (1, 1) | ver(1, 1) |
| 0x8884D3 | D | `Rva004851E3::xfer` | none | call:DieModule::isDieApplicable |
| 0x89DF93 | A | `Rva004851E3::xfer` | (1, 1) | ver(1, 1) |
| 0x888E36 | A | `Rva00485BB8::xfer` | (1, 1) | ver(1, 1) u32 |
| 0x888E52 | A | `Rva00485BD4::xfer` | (1, 2) | ver(1, 2) call:FUN_008c651d u32 ->Rva00485BB8::xfer |
| 0x88BAD5 | A | `Rva004886C7::rva004886F5` | (1, 1) | ver(1, 1) u32 i32 bool |
| 0x88C57A | A | `Rva004892B5::xfer` | (1, 1) | ver(1, 1) raw i32 |
| 0x88C763 | A | `Rva0048947D::xfer` | (1, 1) | ver(1, 1) raw u32 |
| 0x88C78F | A | `Rva004894A9::xfer` | (1, 1) | ver(1, 1) raw |
| 0x88C7B0 | A | `Rva004894CA::xfer` | (1, 1) | ver(1, 1) raw snapshot |
| 0x8955A4 | A | `Rva00491DD3::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) bool u32 |
| 0x89659F | A | `Rva00492C59::xfer` | (1, 1) | ver(1, 1) ->SpecialAbilityUpdate::xfer |
| 0x89679D | A | `Rva00493009::xfer` | (1, 3) | ver(1, 3) ->Rva004C8A95::xfer u32x2 i32 u32 real bool call:XferObjectID bool |
| 0x898357 | A | `Rva00494A97::xfer` | (1, 1) | ver(1, 1) ->Rva00589079::xfer bool |
| 0x89881B | A | `Rva0049506E::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer i32 |
| 0x89D7C5 | A | `Rva00499F45::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) |
| 0x89EBE8 | A | `Rva0049B2A2::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) u32x2 i32 |
| 0x8A51F6 | A | `Rva004A1855::xfer` | (1, 3) | ->UpdateModule::xfer ver(1, 3) call:XferObjectID coord3d i32 raw bool call:XferObjectID boolx2 |
| 0x8A7623 | A | `Rva004A3F8B::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) call:XferObjectID coord3d boolx3 |
| 0x8A8F1B | A | `Rva004A54A8::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u32 coord2d raw realx4 i32 coord3d |
| 0x8ABDBD | A | `Rva004A8091::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) realx2 coord3d raw realx2 i32 bool u32 call:XferObjectID real |
| 0x8B1054 | A | `Rva004AD9B0::xfer` | (1, 2) | ver(1, 2) ->SpecialAbilityUpdate::xfer bool |
| 0x8B64CA | A | `Rva004B237B::rva004B23C5` | (1, 1) | ver(1, 1) ->UpdateModule::xfer u32 bool u32 boolx3 u32 bool |
| 0x8B90F4 | A | `Rva004B4CDF::xfer` | (1, 1) | ver(1, 1) bool ->UpgradeModule::xfer |
| 0x8B9D39 | A | `Rva004B5E06::rva004B5E06` | (1, 2) | ver(1, 2) bool ->UpgradeModule::xfer |
| 0x8C1AEA | A | `Rva004BD78F::xfer` | (1, 1) | ver(1, 1) call:FUN_00993419 bool |
| 0x8C5B1E | A | `Rva004C1866::xfer` | (1, 1) | ver(1, 1) bool real bool u32x2 ->Rva004CE56D::xfer |
| 0x8CD4AA | A | `Rva004C8A95::xfer` | (1, 1) | ver(1, 1) |
| 0x8D0B8A | A | `Rva004CC63A::xfer` | (1, 3) | ver(1, 3) ->UpdateModule::xfer u32x2 call:XferObjectID |
| 0x8C6E67 | A | `Rva004CE0BE::xfer` | (1, 1) | ver(1, 1) |
| 0x8C5524 | A | `Rva004CE56D::xfer` | (1, 1) | ver(1, 1) |
| 0x8D2B05 | A | `Rva004CE6E4::rva004CE6E4` | (1, 1) | ver(1, 1) |
| 0x8E1853 | A (attempt) | `Rva004DD489::xfer` | (1, 1) | ver(1, 1) call:XferObjectID u16 u32 i32 call:XferObjectID u32 call:XferObjectID u32 i32 u16 u32 u16 u32x3 |
| 0x8E3BB1 | A | `Rva004DF81B::xfer` | (1, 1) | ver(1, 1) |
| 0x8E3C3D | D | `Rva004DF87C::xfer` | (1, 1) | ver(1, 1) |
| 0x8E3DA1 | A | `Rva004DFA0E::rva004DFA0E` | (1, 1) | ver(1, 1) call:Rva00596389::rva0059640C |
| 0x8E492F | A | `Rva004E0513::rva004E0513` | (1, 5) | ver(1, 5) i32x4 ustr astrx2 bool |
| 0x8E5817 | A | `Rva004E179A::DoXfer` | (1, 1) | ver(1, 1) astrx3 bool |
| 0x8E5781 | A | `Rva004E1B00::DoXfer` | (1, 1) | ver(1, 1) astrx2 |
| 0x8E57D4 | A | `Rva004E1B72::DoXfer` | (1, 1) | ver(1, 1) astr bool |
| 0x8E8010 | A | `Rva004E3184::xfer` | (1, 3) | ver(1, 3) call:Rva004E12D7Parse astrx2 coord2d astrx3 call:xferAsciiStringVector bool astr real bool astrx3 |
| 0x8F1186 | A | `Rva004ECDC8::rva004ECDC8` | (1, 1) | ver(1, 1) u32 coord3d i32 |
| 0x8F2B36 | A | `Rva004EE15B::rva004EE15B` | (1, 1) | ver(1, 1) i16x3 |
| 0x8F3439 | A (attempt) | `Rva004EE9E9Xfer` | (1, 1) | ver(1, 1) label snapshotx2 |
| 0x8F3A89 | A | `Rva004EED02::xfer` | (1, 7) | ver(1, 7) call:Rva004EE9E9Xfer call:Rva0040E269Xferx3 call:Rva0060C253Xfer u32 i32x5 call:xferThingTemplateCountMap i32x8 u32 call:xferThingTemplateCountMapx3 i32x2 call:XferLivingWorldPlayerID bool |
| 0x8FA77F | A | `Rva004F599E::xfer` | (1, 1) | ver(1, 1) i32x4 |
| 0x8FFA70 | A | `Rva004FACD0Xfer` | (1, 1) | ver(1, 1) label call:Rva004E12D7Parsex2 |
| 0x8FFD84 | A | `Rva004FAF73::rva004FAF73` | (1, 1) | ver(1, 1) call:Rva004CE6E4::rva004CE6E4 i32 call:Rva004FACD0Xfer |
| 0x90C6FE | A | `Rva00506909::xfer` | (1, 1) | ver(1, 1) u32x5 astr virtual+0x14 astr virtual+0x14 u32 astr virtual+0x14 astr virtual+0x14 u32 astr virtual+0x14 astr virtual+0x14 u32 astr virtual+0x14 astr virtual+0x14 |
| 0x92C52E | A (attempt) | `Rva00525ABF::rva00525ABF` | (1, 3) | ver(1, 3) i32 call:XferObjectID i32x3 call:XferObjectID i32x2 call:XferObjectID i32x3 call:XferObjectID i32 |
| 0x932570 | D | `Rva0052CFB1::xfer` | (1, 1) | ver(1, 1) i32 |
| 0x939E1F | A | `Rva005334DBXfer` | (1, 1) | ver(1, 1) label call:Rva00531F79Chainx2 |
| 0x93F666 | B | `Rva00538CB4::rva00538CB4` | (1, 1) | ver(1, 1) coord2d call:Rva003EFE82Get |
| 0x94B325 | A | `Rva005445D4::xfer` | (1, 2) | ver(1, 2) call:XferObjectID u32 |
| 0x94DE2A | A | `Rva0054755BXfer` | (1, 1) | ver(1, 1) label call:Rva00547245Xferx2 |
| 0x94ECF2 | A | `Rva0054840A::rva0054840A` | (1, 1) | ver(1, 1) call:XferObjectID u32x3 call:xferListInt |
| 0x95BB15 | B | `Rva00553E47StatsCore::rva005550A0` | (1, 1) | ver(1, 1) call:PSPlayerStats::XferMapx2 u32x2 u16 |
| 0x95BB7E | B | `Rva00553E47StatsCore::rva00555109` | (1, 2) | ver(1, 2) call:PSPlayerStats::XferMapx6 call:PSPlayerStats::rva0055499Ax8 u16x4 |
| 0x9619D6 | A | `Rva0055B0CC::Rva0055AED6` | (1, 3) | ver(1, 3) real call:XferObjectID astr u32 real boolx2 call:XferObjectID bool |
| 0x969842 | A | `Rva0055B266::rva0055B266` | (1, 1) | ver(1, 1) call:xferRandomVariable u32 |
| 0x969FC6 | A | `Rva0055B30F::rva0055B30F` | (1, 1) | ver(1, 1) |
| 0x969943 | A | `Rva0055B367::rva0055B367` | (1, 1) | ver(1, 1) real u32 |
| 0x969F42 | A | `Rva0055B8CD::rva0055B8CD` | (1, 1) | ver(1, 1) rgb u32 call:xferRandomVariable |
| 0x9698EB | A | `Rva0055B951::rva0055B951` | (1, 1) | ver(1, 1) |
| 0x96A029 | A | `Rva0055BC8B::rva0055B96D` | (1, 1) | ver(1, 1) rgb u32 real |
| 0x967B07 | A | `Rva0055CA8A::xfer` | (1, 1) | ver(1, 1) bool |
| 0x967DB5 | A | `Rva0055CD38::rva0055CD38` | (1, 1) | ver(1, 1) bool coord3dx2 |
| 0x968456 | A | `Rva0055D3F5::rva0055D3F5` | (1, 1) | ver(1, 1) bool real |
| 0x96A99D | A | `Rva0055EECB::rva0055EECB` | (1, 3) | ver(1, 3) real coord3d call:xferRandomVariable boolx2 |
| 0x96AA0B | A | `Rva0055EF39::xfer` | (1, 1) | ver(1, 1) |
| 0x96AA40 | A | `Rva0055EF6E::rva0055EF6E` | (1, 2) | ver(1, 2) real boolx2 |
| 0x96AA97 | A | `Rva0055EFC5::rva0055EFC5` | (1, 1) | ver(1, 1) |
| 0x96B1DB | A | `Rva0055F710::rva0055F710` | (1, 1) | ver(1, 1) |
| 0x96B237 | A | `Rva0055F76C::rva0055F76C` | (1, 2) | ver(1, 2) realx6 call:XferRotationType realx3 |
| 0x96B2D0 | A | `Rva0055F805::rva0055F805` | (1, 1) | ver(1, 1) |
| 0x965860 | A | `Rva00561527::rva00561527` | (1, 1) | ver(1, 1) |
| 0x9660BA | A | `Rva00562233::rva00562233` | (1, 1) | ver(1, 1) |
| 0x96612D | A | `Rva005622A6::rva005622A6` | (1, 1) | ver(1, 1) realx12 call:XferRotationType |
| 0x966C09 | A | `Rva0056234A::rva0056234A` | (1, 1) | ver(1, 1) |
| 0x96476A | A | `Rva00562945::rva00562945` | (1, 1) | ver(1, 1) |
| 0x96B73B | A | `Rva00563ED1::rva00563ED1` | (1, 1) | ver(1, 1) ->FXParticleSystem::LifeEventModuleInfo::xfer boolx3 |
| 0x96BD81 | A | `Rva005644A9::rva005644A9` | (1, 1) | ver(1, 1) call:FUN_0096bd1f boolx4 u32 |
| 0x96BDFA | A | `Rva00564522::rva00564522` | (1, 1) | ver(1, 1) real bool |
| 0x97D9BB | A | `Rva00573B23::Rva00573A35` | (1, 2) | ver(1, 2) call:XferObjectID astr coord3d real call:Rva0055B0CC::Rva0055AED6 |
| 0x97DF3D | A | `Rva00573F03::Rva00573F9F` | (1, 1) | ver(1, 1) coord3d real i32 bool u32x2 call:Rva00573B23::Rva00573A35 |
| 0x98E313 | A | `Rva00586231Xfer` | (1, 1) | ver(1, 1) label coord3dx2 |
| 0x991117 | A | `Rva00589051::xfer` | (1, 1) | ver(1, 1) ->Rva004C8A95::xfer call:XferObjectID |
| 0x9912DB | A | `Rva00589079::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer u32 i32 u32 real |
| 0x99E7DE | A | `Rva00596389::rva0059640C` | (1, 1) | ver(1, 1) u32 i32 |
| 0x99F0CE | A | `Rva00596CDF::Rva00596CEA` | (1, 1) | ver(1, 1) call:Rva00573B23::Rva00573A35 coord3d call:XferObjectID u32 i32 bool |
| 0x99F6F4 | A | `Rva005970ED::rva0059728F` | (1, 2) | ver(1, 2) ->AIUpgrade::DoXfer bool i32x2 |
| 0x99FA77 | A | `Rva0059761B::rva0059761B` | (1, 1) | ver(1, 1) i32 |
| 0x9A11BE | A | `Rva00598DA2::rva00598DA2` | (1, 1) | ver(1, 1) u32 astrx2 |
| 0x9B2A23 | A | `Rva005A91BE::rva005A91BE` | (1, 2) | ver(1, 2) u32x2 i32 |
| 0x9D38C7 | A | `Rva005C4469::rva005C4469` | (1, 1) | ver(1, 1) call:Rva004CE6E4::rva004CE6E4 bool |
| 0x9D3FDE | A | `Rva005C45D2::rva005C45D2` | (1, 1) | ver(1, 1) call:Rva004CE6E4::rva004CE6E4 |
| 0x9D3C76 | A | `Rva005C4808::rva005C4808` | (1, 2) | ver(1, 2) call:Rva004CE6E4::rva004CE6E4 i32 |
| 0x9ED711 | A | `Rva005DAAB6::Rva005DAAC1` | (1, 1) | ver(1, 1) call:Rva0055B0CC::Rva0055AED6 bool coord3d real |
| 0x9EFE96 | B | `Rva005DCE72::rva005DCEB7` | (1, 2) | ver(1, 2) boolx2 i32 bool u32 virtual+0x30 |
| 0xA1FF33 | A | `Rva0060C253Xfer` | (1, 1) | ver(1, 1) label boolx2 |
| 0x8985E5 | A | `ScaleWallSpecialAbilityUpdate::xfer` | (1, 1) | ver(1, 1) ->SpecialAbilityUpdate::xfer |
| 0x8257DB | A | `ScoredKillEvaAnnouncer::DoXfer` | (1, 1) | ver(1, 1) call:Rva00414C92 i32x2 |
| 0x961529 | A | `ScoredKillTracker::DoXfer` | (1, 1) | ver(1, 1) u32 ->ObjectFilter::DoXfer u32 i32x2 u32x2 coord3d |
| 0x79ECCB | B | `ScoreKeeper::DoXfer` | (1, 12) | ver(1, 12) i32x26 u32 call:xferThingTemplateCountMap u16 call:xferThingTemplateCountMapx3 call:Rva0039C4F7XferSnapshotVector real u32 i32x2 call:xferThingTemplateCountMap i32 real i32x3 |
| 0x79DD41 | A | `ScoreKeeper::PerFrameStats::xfer` | (1, 3) | ver(1, 3) i32 i16x2 real u16 |
| 0x7B3063 | A | `Script::xfer` | (1, 2) | ver(1, 2) bool u32 |
| 0x7B2F8A | A | `ScriptGroup::xfer` | (1, 1) | ver(1, 1) bool |
| 0x60331E | A | `SequentialScript::xfer` | (1, 1) | ver(1, 1) u32 call:XferObjectID astrx2 i32x3 bool |
| 0x861D51 | A | `ShipSlowDeathBehavior::xfer` | (1, 1) | ->SlowDeathBehavior::xfer ver(1, 1) |
| 0xB52D50 | A | `ShroudManagerImpl008FBA40Element::xfer` | (1, 2) | ver(1, 2) u16 call:rva0073ea90 call:Rva0073EB00Xfer |
| 0xB51A50 | A (attempt) | `ShroudManagerImpl::rva0073D8B0` | (1, 1) | ver(1, 1) u32 region3d realx2 i32x2 u32 i32 bool |
| 0x72B570 | A | `SidesList::DoXfer` | (1, 1) | ver(1, 1) i32 bool snapshot bool |
| 0x8CA8A5 | A | `SiegeDeployHordeSpecialPower::xfer` | (1, 2) | ->Rva00589079::xfer ver(1, 2) call:XferObjectID coord3d i32 |
| 0x8C9FC8 | A | `SiegeDeploySpecialPower::xfer` | (1, 2) | ->Rva00589079::xfer ver(1, 2) raw u32 call:XferObjectIDx2 coord3d boolx2 coord3dx2 |
| 0x85D08F | A | `SiegeDockingBehavior::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer u32 call:XferSiegeTypeEnum i32 coord3dx2 call:XferObjectID u32 call:XferSiegeTypeEnum i32 coord3dx2 call:XferObjectID bool |
| 0x87F5E4 | A | `SiegeEngineContain::xfer` | (1, 1) | call:FUN_00869e86 ver(1, 1) u32 call:XferObjectID u32 call:XferObjectID bool i32 call:XferObjectID i32x2 call:XferObjectID i32 |
| 0x6C7D4F | A | `SkirmishAI::DoXfer` | (1, 1) | ver(1, 1) bool u32 real u32 i32 ->AIBuilder::DoXfer ->TacticalAI::DoXfer u32 astr i32 astr i32 |
| 0x800DB8 | A | `SkirmishGameInfo::xfer` | (1, 1) | ver(1, 1) i32 boolx3 i32x2 ->virtual xfer call:xferMapName u32x2 i32x5 u32 |
| 0x8834F7 | A | `SlaughterHordeContain::xfer` | (1, 2) | ver(1, 2) u32 astr ->HordeGarrisonContain::xfer |
| 0x8603F9 | A | `SlowDeathBehavior::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u32x4 real u32 bool u32 bool |
| 0x863636 | A | `SpawnBehavior::xfer` | (1, 3) | ->Rva004C8A95::xfer ver(1, 3) ->UpdateModule::xfer bool call:UpgradeMux::upgradeMuxXfer u32 astr i32x3 call:Rva00460216XferList call:Rva0036ABAFXfer boolx2 i32 u32 |
| 0x852782 | A | `SpecialAbilityUpdate::xfer` | (1, 3) | ->UpdateModule::xfer ver(1, 3) bool u32x2 call:XferObjectID coord3d i32x2 u32 call:Rva0036ABAFXfer u32 bool raw boolx4 real boolx3 virtual+0x160x2 u32 coord3d i32 |
| 0x8B4301 | A | `SpecialDisguiseUpdate::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) bool real ->SpecialAbilityUpdate::xfer |
| 0x889D73 | A | `SpecialPowerCompletionDie::xfer` | (1, 1) | ->ExperienceTrackerAutoResolve::xfer ver(1, 1) call:XferObjectID bool |
| 0x8227E9 | A | `SpellStore::xfer` | (1, 1) | ver(1, 1) bool |
| 0x8DB278 | A | `Squad::xfer` | (1, 1) | ver(1, 1) u16 call:XferObjectIDx2 |
| 0x861DDF | A | `StancesBehavior::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer call:XferStancesEnum |
| 0x8DBAEC | A | `StateMachine::xfer` | (1, 4) | ver(1, 4) u32 bool i32 u32 snapshotx2 call:XferObjectID coord3d boolx2 call:XferObjectIDx2 real |
| 0x8A62D2 | A | `StealthDetectorUpdate::xfer` | (1, 3) | ver(1, 3) ->UpdateModule::xfer bool |
| 0x776C7D | B | `StealthUpdate::xfer` | (1, 4) | ->UpdateModule::xfer ver(1, 4) u32x3 bool i32 astr u32 boolx7 i32 bool |
| 0x8C4BB2 | A | `StructureBody::xfer` | (1, 1) | ver(1, 1) ->ActiveBody::xfer call:XferObjectID |
| 0x8A8110 | A | `StructureCollapseUpdate::xfer` | (1, 1) | ->UpdateModule::xfer ver(1, 1) u32x2 raw realx5 |
| 0x8AA20B | B | `SupplyTruckAIUpdate::xfer` | (1, 1) | ->AIUpdateInterface::xfer ver(1, 1) snapshot call:XferObjectID coord3d bool i32 boolx2 |
| 0x8AB451 | A | `SupplyWarehouseDockUpdate::xfer` | (1, 1) | ver(1, 1) ->DockUpdate::xfer i32 |
| 0x8CD93F | D | `SwayClientUpdate::xfer` | (1, 1) | ->ObjectModule::xfer ver(1, 1) realx5 i16 bool realx2 |
| 0x8C4C07 | A | `SymbioticStructuresBody::xfer` | (1, 1) | ver(1, 1) ->ActiveBody::xfer call:XferObjectID |
| 0x94D303 | A | `SynchronizeGroupOrder::xfer` | (1, 1) | ->GroupOrder::xfer ver(1, 1) call:Rva002F1CDDXfer bool |
| 0x6C70D8 | B | `TacticalAI::DoXfer` | (1, 2) | ver(1, 2) bool ->AITargetChooser::xfer ->Rva00506909::xfer u32 call:FUN_006c6b6ex2 |
| 0x6FF49C | B | `TAiData::xfer` | (1, 1) | realx2 i32x2 u32 realx9 u32x3 realx5 bool i32x4 ver(1, 1) |
| 0x4D18E9 | A | `TBuff::xfer` | (1, 5) | ver(1, 5) i32 raw real bool coord3d real |
| 0x7A634B | B | `TeamFactory::xfer` | (1, 3) | ver(1, 3) u32 u16 u32 snapshot bool astr u32 snapshot i32 bool snapshot |
| 0x8F637B | A | `TeamInQueue::xfer` | (1, 1) | ver(1, 1) u16 snapshotx2 bool u32 i32 boolx3 call:XferObjectID |
| 0x7A7184 | D | `TeamPrototype::xfer` | (1, 2) | ver(1, 2) i32 astr bool snapshot u16 bool realx3 boolx3 u32x2 snapshot u32 snapshot |
| 0x7A56B6 | A | `TeamRelationMap::xfer` | (1, 1) | ver(1, 1) u16 u32 call:XferRelationship u32 call:XferRelationship |
| 0x79FB68 | A | `TeamTemplateInfo::xfer` | (1, 3) | ver(1, 3) i32 u32x7 i32 call:Rva0039D533::rva0039D533 ->$0NK::BitFlags<>::xferx2 call:Rva002ABBBE::rva002ABBBEx2 |
| 0x683F8C | A | `TerrainLogic::xfer` | (1, 2) | ver(1, 2) boolx2 i32x4 realx4 call:XferWaypointID bool ->Waypoint::xfer bool ->Waypoint::xfer bool i32 call:Rva0027C230::rva0028002F i16 u32 bool u32 |
| 0x8853F9 | A | `TerrainResourceBehavior::xfer` | (1, 2) | ->UpdateModule::xfer ver(1, 2) boolx2 real |
| 0x75C7AE | B | `TerrainResourceManager::DoXfer` | (1, 3) | ver(1, 3) i32 call:XferObjectID real bool call:XferObjectID real bool region3d i32x2 realx2 i32x4 call:XferObjectIDx2 i32x3 call:XferObjectID |
| 0x7EDF70 | B | `ThreatFinderUpdate::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer call:FUN_007edc93 |
| 0x8B1938 | A | `ToggleHiddenSpecialAbilityUpdate::xfer` | (1, 1) | ver(1, 1) u32 ->SpecialAbilityUpdate::xfer |
| 0x46850B | A | `TScorch::xfer` | (1, 1) | ver(1, 1) realx4 i32 bool |
| 0x8FA569 | A | `TunnelTracker::xfer` | (1, 1) | ver(1, 1) call:Rva0036ABAFXfer i32 call:XferObjectIDx2 u32 call:XferObjectID u32 |
| 0x8DC2C3 | A | `TurretAI::xfer` | (1, 1) | ver(1, 1) snapshot realx2 u32 call:XferTurretTargetType u32 boolx8 u32 virtual+0x160 u32 |
| 0x8DC73F | A | `TurretAIIdleScanState::xfer` | (1, 1) | ver(1, 1) real |
| 0x7FB5E5 | B (partial-authored) | `UBfmeE16::vector<>::_M_set` | (1, 1) | ver(1, 1) i32 coord2dx2 |
| 0x7808FD | B | `UnitRevivalEntry::rva0037E473` | (1, 10) | ver(1, 10) astr real i32 call:rva003064CB u32 i32 u32 bool i32 call:XferLivingWorldArmyID call:Rva004E12D7Parse bool call:Rva004E0513::rva004E0513 i32x2 astrx2 real astr i32 |
| 0x781870 | B | `UnitRevivalTracker::xfer` | (1, 1) | ver(1, 1) u32 call:UnitRevivalEntry::rva0037E473 i32 call:UnitRevivalEntry::rva0037E473 i32 |
| 0x850C60 | B | `UpdateModule::xfer` | (1, 1) | ver(1, 1) ->Rva004C8A95::xfer u32 |
| 0x8D284A | A | `UpgradeModule::xfer` | (1, 1) | ver(1, 1) ->Rva004C8A95::xfer call:UpgradeMux::upgradeMuxXfer |
| 0x8D27E8 | A | `UpgradeMux::upgradeMuxXfer` | (1, 1) | ver(1, 1) bool |
| 0x80B4F5 | B (partial-authored) | `VAsciiString::map<>::map<>` | (1, 8) | ver(1, 8) call:Rva00407099Update call:FUN_008099e4 call:Rva0040707EUpdatex7 call:Rva00407218::rva00407218 call:Rva0040707EUpdate call:Rva0040718FForward call:Rva0040707EUpdatex2 call:Rva0040718FForward call:Rva0040707EUpdate astr call:Rva004070B6Update u32x2 |
| 0x80731E | B | `VictorySystemSnapshotView::rva004056ED` | (1, 1) | ver(1, 1) bool real u32 realx4 call:Rva00404AB1::rva00404AB1 raw u32 call:FUN_00806337 u32 call:FUN_00806337 u32 call:CellGrid::rva0056C50Ex2 |
| 0x65ED04 | B | `View::xfer` | (1, 3) | ver(1, 3) realx4 i32x4 realx6 bool realx4 call:XferObjectID call:XferDrawableID raw realx4 boolx3 coord2d i32 call:Rva0025ECFC::rva0025ECFCx2 bool call:XferObjectID icoord3d realx7 |
| 0x4D0463 | A | `W3DBoatWakeModelDraw::xfer` | (1, 2) | ->DrawModule::xfer ver(1, 2) real |
| 0x4B16E8 | A | `W3DDebrisDraw::xfer` | (1, 1) | ->DrawModule::xfer ver(1, 1) astr i32 astrx3 i32x2 bool |
| 0x4E5AAC | A | `W3DFloorBuffer::DoXfer` | (3, 3) | ver(3, 3) i32 call:Rva000E5725::rva000E5725x2 |
| 0x4CE868 | A | `W3DFloorDraw::xfer` | (1, 3) | ver(1, 3) ->W3DPropDraw::xfer boolx2 |
| 0x463751 | A | `W3DGhostObject::xfer` | (1, 1) | ->GhostObject::xfer ver(1, 1) call:XferObjectID i32 call:XferDrawableID u8 astr real u32 snapshot astr real u32 snapshot |
| 0x463ACA | A | `W3DGhostObjectManager::xfer` | (1, 1) | ->GhostObjectManager::xfer ver(1, 1) u16 call:XferObjectID snapshot call:XferObjectID snapshot |
| 0x4C899A | A | `W3DLaserDraw::xfer` | (1, 1) | ver(1, 1) ->DrawModule::xfer realx2 i32 real |
| 0x4CF488 | A | `W3DLightDraw::xfer` | (1, 1) | ver(1, 1) realx4 ->DrawModule::xfer |
| 0x4D09B5 | A | `W3DProjectileStreamDraw::xfer` | (1, 1) | ver(1, 1) ->DrawModule::xfer |
| 0x4EE2C2 | A (attempt) | `W3DPropBuffer::xfer` | (1, 1) | ver(1, 1) i32 call:Rva0030612AXfer real astr i32x3 coord3d bool call:Rva003062FEXfer real boolx2 i32 |
| 0x4B1B3F | A | `W3DPropDraw::xfer` | (1, 1) | ver(1, 1) |
| 0x4C984A | A | `W3DPropDraw::xfer` | (1, 1) | ver(1, 1) |
| 0x4634C2 | A | `W3DRenderObjectSnapshot::xfer` | (1, 1) | ver(1, 1) call:Rva003062FEXfer i32 astrx2 bool call:Rva003062FEXfer |
| 0x4CA0EF | A | `W3DRopeDraw::xfer` | (1, 1) | ->DrawModule::xfer ver(1, 1) realx3 rgb realx8 |
| 0x4D0009 | A | `W3DSailModelDraw::xfer` | (1, 1) | ->W3DScriptedModelDraw::xfer ver(1, 1) real bool |
| 0x4C52FE | A (attempt) | `W3DScriptedModelDraw::xfer` | (1, 6) | ->DrawModule::xfer ver(1, 6) bool i32 real boolx2 i32 real bool call:XferDrawableID call:Rva000BC559Xfer astr i32x2 ->$0CEP::BitFlags<>::xfer i32x2 astr bool realx4 i32 astr bool realx4 i32 astr bool realx4 i32 astr bool realx4 i32 astrx2 coord3d i32x2 astrx2 coord3d i32 boolx14 u32x2 i32 astr i32x2 realx4 bool u32x3 call:Rva0030612AXfer astrx2 bool realx4 bool i32 astrx2 |
| 0x4E9075 | A (attempt) | `W3DShrubBuffer::DoXfer` | (1, 3) | call:Rva000E63DCDo ver(1, 3) i32x2 call:Rva0030612AXferx2 real coord2dx4 bool astrx3 i32 astrx3 realx4 call:Rva003062FEXfer call:XferDrawableID bool astr region2d |
| 0x4CA6B8 | B | `W3DSupplyDraw::xfer` | (1, 1) | ver(1, 1) i32x2 ->W3DScriptedModelDraw::xfer |
| 0x4CD830 | A | `W3DTankDraw::xfer` | (1, 1) | ver(1, 1) ->W3DScriptedModelDraw::xfer |
| 0x46253F | A | `W3DTerrainLogic::xfer` | (1, 1) | ver(1, 1) |
| 0x4920F4 | A | `W3DTerrainVisual::xfer` | (1, 1) | call:FUN_0094bc51 ver(1, 1) bool snapshot bool i32 raw snapshot |
| 0x4D0F87 | A | `W3DTornadoDraw::xfer` | (1, 1) | ->DrawModule::xfer ver(1, 1) call:RadiusDecal::rva00330F7D |
| 0x4ECE3E | A | `W3DTreeBuffer::xfer` | (1, 4) | call:Rva000E63DCDo ver(1, 4) i32x2 call:Rva0030612AXferx2 real coord2dx4 bool astrx3 i32 astrx3 realx4 call:Rva003062FEXfer call:XferDrawableID realx2 coord3d call:FUN_004e9b9c real u32 call:Rva003062FEXfer u32 bool i32x2 bool astr |
| 0x4CA936 | B | `W3DTruckDraw::xfer` | (1, 1) | ver(1, 1) boolx2 ->W3DScriptedModelDraw::xfer call:Rva002D9FD9Owner::rva002D9FD9x2 |
| 0x855FC5 | A | `WallHubBehavior::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer |
| 0x8AE96E | A | `WallUpgradeUpdate::xfer` | (1, 1) | ver(1, 1) ->UpdateModule::xfer call:XferObjectID i32 boolx2 |
| 0x67FD1B | A | `Waypoint::xfer` | (1, 4) | ver(1, 4) call:XferWaypointID astr coord3d call:XferWaypointID i32 call:XferWaypointIDx3 i32 call:XferWaypointIDx3 bool i32 astrx3 bool raw astr boolx4 call:XferObjectID ->$0NK::BitFlags<>::xferx2 u32 ->ObjectFilter::DoXfer bool |
| 0x6CEA67 | A | `Weapon::xfer` | (1, 3) | ver(1, 3) astr call:XferObjectID call:XferWeaponSlotType u32x7 i32x3 u16 i32x2 bool u32 i32 call:XferWeaponStatus u32 i32 call:XferObjectID |
| 0x895C75 | A | `WeaponFireSpecialAbilityUpdate::xfer` | (1, 2) | ver(1, 2) ->SpecialAbilityUpdate::xfer bool skipBLOK BLOK snapshot EBLK |
| 0x6C95BD | A | `WeaponSet::xfer` | (1, 1) | ver(1, 1) astr ->$0GI::BitFlags<>::xfer astr ->$0GI::BitFlags<>::xfer bool snapshot bool rawx2 u32 i32 u32 boolx2 call:XferObjectID |
| 0x8F50B0 | B | `WorkOrder::xfer` | (1, 1) | ver(1, 1) call:Rva003063A9XferThingTemplate call:XferObjectID i32x2 boolx2 i32 astr u32 boolx2 i32 |
| 0x8CADED | A | `WoundArrowUpdate::xfer` | (1, 1) | ->SpecialAbilityUpdate::xfer ver(1, 1) bool |
| 0x459874 | B | `xferAsciiStringVector` | (1, 1) | ver(1, 1) label astrx2 |
| 0x606296 | A | `xferListInt` | (1, 1) | ver(1, 1) label u32x2 |
| 0x71C1DB | B | `XferOwningLivingWorldArmyVec` | (1, 1) | ver(1, 1) i32 call:W4ScienceType::vector<>::reserve snapshot i32 snapshot |
| 0x707958 | A | `xferRandomVariable` | (1, 1) | ver(1, 1) realx2 call:XferDistributionType |
| 0x73385A | A | `xferSTLObjectIDList` | (1, 1) | ver(1, 1) label call:XferObjectIDx2 |
| 0x9617A0 | A | `xferThingTemplateCountMap` | (1, 1) | ver(1, 1) u16 astr i32 |
| 0x45C457 | A | `xferUnicodeStringVector` | (1, 1) | ver(1, 1) label ustrx2 |
| 0x4584DC | A (tu-only) | `FUN_004584dc (unnamed)` | (1, 1) | ver(1, 1) label astrx2 |
| 0x458DFF | A (tu-only) | `FUN_00458dff (unnamed)` | (1, 1) | ver(1, 1) bool i32 call:FUN_00455be8x2 bool i32 call:Rva000532B7Getx2 |
| 0x458F93 | B (tu-only) | `FUN_00458f93 (unnamed)` | (1, 1) | ver(1, 1) bool i32 u32 call:Rva002D9F9FOwner::rva002D9F9F |
| 0x45C594 | A (none) | `FUN_0045c594 (unnamed)` | (1, 1) | ver(1, 1) real call:FUN_004584dc real bool realx6 call:Rva0005CC28Xfer |
| 0x45DAFB | A (none) | `FUN_0045dafb (unnamed)` | (1, 4) | ver(1, 4) call:Rva00050C61Dispatch call:Rva00050C79Dispatch bool call:Rva00050C91Dispatch call:FUN_0045c594 call:FUN_004584dc call:BfmeThingDWH::bfmeGoDWH i32x2 real i32x2 real i32 u32 i32 call:VUnicodeString::vector<>::clear i32 call:VUnicodeString::vector<>::clear call:FUN_00450762 i32 call:VUnicodeString::vector<>::clear i32 call:VUnicodeString::vector<>::clear call:xferUnicodeStringVector call:xferAsciiStringVector |
| 0x478461 | - (none) | `FUN_00478461 (unnamed)` | (1, 1) | ->W3DScriptedModelDraw::xfer i32 ver(1, 1) |
| 0x5DC78F | - (none) | `FUN_005dc78f (unnamed)` | (1, 1) | ver(1, 1) bool |
| 0x5EB7D9 | B (none) | `FUN_005eb7d9 (unnamed)` | (1, 1) | ver(1, 1) label snapshotx2 |
| 0x5EC2E6 | B (none) | `FUN_005ec2e6 (unnamed)` | (1, 6) | ver(1, 6) i32 bool call:FUN_005eb7d9 boolx3 i32 call:FUN_005eb7d9 real call:Rva001ECA2DXfer call:rva003064CB real |
| 0x606425 | A (tu-only) | `FUN_00606425 (unnamed)` | (1, 1) | ver(1, 1) label call:Rva00203DBEChainx2 |
| 0x60A45B | B (tu-only) | `FUN_0060a45b (unnamed)` | (1, 5) | ver(1, 5) bool astr u16 snapshotx2 u16 astrx2 i32 bool u16 astrx2 bool astrx2 i32 bool astrx2 bool i32 call:Rva00203D4EChain call:XferLivingWorldArmyID i32 call:Rva00203D4EChain astr i32 call:Rva00203D4EChain call:XferLivingWorldPlayerID i32 call:Rva00203D4EChain call:XferLivingWorldArmyID i32 call:Rva00203D4EChain astr i32 call:Rva00203D4EChain call:XferLivingWorldPlayerID u16 snapshot i32x3 u16 astr call:XferObjectID astr call:XferObjectID bool raw realx3 i32x4 call:Rva0020700FXfer call:xferListInt call:Rva0020700FXfer call:xferListInt call:Rva00207F91XferList call:Rva00206923XferList call:Rva00207F91XferList call:Rva00206923XferList call:Rva0020700FXfer call:xferListInt call:Rva00209A09Xfer call:Rva002070F7Xfer call:Rva00209A09Xfer call:Rva002070F7Xfer call:Rva00209A09Xfer call:Rva002070F7Xfer call:Rva00209A09Xfer call:Rva002070F7Xfer call:Rva00208081Xfer call:Rva002080DAXferList call:FUN_00606425 realx6 i16x2 call:XferGameDifficulty bool u16 u32x3 astr real astr u16 u32 astrx2 real astr snapshotx2 boolx2 u32 astrx2 call:XferObjectID u32 astrx2 u32x2 astrx2 call:XferObjectID u32 astrx2 u32x2 astr bool u32 astr bool |
| 0x611F8B | B (none) | `FUN_00611f8b (unnamed)` | (1, 1) | ver(1, 1) i32 ->Rva003FD789::xfer i32 ->virtual xfer |
| 0x6308F3 | B (none) | `FUN_006308f3 (unnamed)` | (1, 8) | ver(1, 8) i32x2 ->virtual xfer ustr u32x2 i32 bool coord3d call:GameLogic::xferObjectTOC u32 u16 call:XferObjectID BLOK snapshot EBLK u16 call:XferObjectID skipBLOK BLOK snapshot EBLK snapshotx6 bool i32 astr call:XferBuildableStatus astrx2 call:XferBuildableStatus boolx3 i32 call:XferGameDifficulty u32 i32x3 astrx3 i32 bool snapshot |
| 0x67A544 | A (none) | `FUN_0067a544 (unnamed)` | (1, 11) | ver(1, 11) call:XferDrawableID ->$0CEP::BitFlags<>::xferx3 call:Rva003062FEXfer bool snapshot bool snapshot u32 realx9 u32 real bool u32 realx3 u32x2 call:XferObjectID u32x3 raw u32x2 real bool realx17 i32x2 call:Drawable::xferDrawableModules call:XferStealthLookType i32x2 boolx2 real bool call:Rva003062FEXferx3 u32 real call:XferObjectID u32 u8 astr u32 astrx2 u32 astr snapshot boolx7 astr call:DynamicAudioEventInfo::xferNoName astr call:DynamicAudioEventInfo::xferNoName virtual+0x160x2 astrx2 i32x2 u32x3 call:XferWaypointID u32 call:XferWaypointID rgb u32x3 realx2 u32 boolx2 i32 astrx2 boolx3 call:Rva0030612AXferx2 bool icoord2d i32 realx2 u32 real i32 iregion2d raw call:Drawable::xferDrawableModules boolx3 ->virtual xfer bool realx4 u32 |
| 0x697F28 | B (tu-only) | `FUN_00697f28 (unnamed)` | (1, 29) | ver(1, 29) astrx2 call:XferObjectID call:Rva003062FEXfer u8 call:rva003064CB call:Rva00291440::rva00291440 snapshot u32 realx2 call:XferObjectIDx2 icoord3d astrx2 u32 call:XferObjectID coord3d snapshot u32 call:XferObjectIDx2 call:XferDrawableID astr ->$0GF::BitFlags<>::xfer icoord2dx2 ->$0CEP::BitFlags<>::xfer u8 u32 snapshot realx3 ->$0L::BitFlags<>::xfer bool u32 i32 u32x2 call:XferObjectID u32 real astr i32 coord3dx2 call:XferObjectID i8 u32 icoord3d astr i8x3 call:XferPathfindLayerEnumx2 bool u32 call:XferFormationID coord2d ->$0GI::BitFlags<>::xfer i8 snapshot u16 astr BLOK snapshot EBLK astr skipBLOK BLOK snapshot EBLK call:XferObjectID u32 snapshot ->$0GI::BitFlags<>::xfer i8 snapshot ->$0JK::BitFlags<>::xfer astrx3 boolx3 u32x2 call:Rva00263895Member::rva004D6F86 u32 real call:XferObjectID bool u8 call:XferLivingWorldArmyID call:Rva004E12D7Parse call:Rva004E075FGet i32 astr call:Rva004E0513::rva004E0513 i32x3 boolx3 i32 realx2 u32x2 bool u32 call:XferObjectIDx2 realx4 call:XferObjectID bool call:FUN_008e1a22 u8 coord2d call:Rva0028C5CCGet u32x2 i32 u32x3 |
| 0x6B99D6 | A (none) | `FUN_006b99d6 (unnamed)` | (1, 1) | ver(1, 1) i32 call:W4ScienceType::vector<>::reserve call:XferLivingWorldPlayerID i32 call:XferLivingWorldPlayerID |
| 0x6C6B6E | - (none) | `FUN_006c6b6e (unnamed)` | (1, 5) | ver(1, 5) u32 coord3d u32 real u32 real u32 call:Rva003ECA69Element::rva003ECAE0x2 |
| 0x6E445E | B (none) | `FUN_006e445e (unnamed)` | (1, 6) +1 | ver(1, 6) call:XferLivingWorldPlayerID snapshot astrx2 bool i32 astr call:XferOwningLivingWorldArmyVec ver(1, 1) call:Rva001ECA2DXfer astr real i32 call:rva003064CB i32x2 snapshotx2 i32x3 rgbx3 snapshot u32 i32 call:UnitRevivalEntry::rva0037E473 i32 call:UnitRevivalEntry::rva0037E473 call:Rva002E0856DivMovField::get call:Rva002E208AXferx2 |
| 0x71BA5E | A (none) | `FUN_0071ba5e (unnamed)` | (1, 1) | ver(1, 1) label snapshotx2 |
| 0x71BFD9 | A (none) | `FUN_0071bfd9 (unnamed)` | (1, 6) | ver(1, 6) call:XferLivingWorldArmyID astr coord2dx2 call:Rva003EFE82Get i32 coord2d bool real bool astr bool astr bool raw call:FUN_0071ba5e snapshot virtual+0x160 astr coord2d bool call:XferArmyIconSize ustrx2 |
| 0x742EF5 | - (none) | `FUN_00742ef5 (unnamed)` | (1, 1) | ver(1, 1) bool snapshot |
| 0x743CA3 | - (none) | `FUN_00743ca3 (unnamed)` | (1, 1) | ver(1, 1) bool snapshot u32 |
| 0x767EB7 | A (none) | `FUN_00767eb7 (unnamed)` | (1, 1) | ver(1, 1) i32x2 coord3d call:XferPathfindLayerEnum bool i32 u32 i32x2 coord3d call:XferPathfindLayerEnum bool i32 u32 i32 bool i32 |
| 0x769C97 | - (none) | `FUN_00769c97 (unnamed)` | (1, 1) | ver(1, 1) u32 coord3d |
| 0x769CD2 | - (none) | `FUN_00769cd2 (unnamed)` | (1, 1) | ver(1, 1) u32 coord3d |
| 0x76AF56 | - (none) | `FUN_0076af56 (unnamed)` | (1, 1) | ver(1, 1) raw coord3d bool u32 real |
| 0x7755E6 | A (none) | `FUN_007755e6 (unnamed)` | (1, 1) | ver(1, 1) |
| 0x793971 | - (none) | `FUN_00793971 (unnamed)` | (1, 1) | ->UpdateModule::xfer ver(1, 1) call:RadiusDecal::rva00330F7D bool |
| 0x797CF2 | A (tu-only) | `FUN_00797cf2 (unnamed)` | (1, 1) | ver(1, 1) call:XferObjectID bool u32 call:XferObjectID ->Rva004C8A95::xfer call:XferObjectID u32 bool virtual+0x160 |
| 0x7A5C06 | - (none) | `FUN_007a5c06 (unnamed)` | (1, 2) +1 | ver(1, 2) u32 u16 call:XferObjectIDx2 astr boolx8 i32x2 u32 u16 bool u32 boolx4 call:XferObjectID snapshotx2 real u32 call:Rva002F1CDDXfer bool ver(1, 1) i32x3 u8x2 u16 u32 realx3 call:XferObjectID i32x2 u32x3 call:XferObjectID i32x3 u8x2 u16 u32 realx3 call:XferObjectID i32x2 u32x3 call:XferObjectID |
| 0x7B5269 | A (tu-only) | `FUN_007b5269 (unnamed)` | (1, 2) | ver(1, 2) u16 snapshotx2 u16 snapshot |
| 0x7B99D9 | A (none) | `FUN_007b99d9 (unnamed)` | (1, 1) | ver(1, 1) astr ->virtual xfer astr ->virtual xfer bool |
| 0x7EDC93 | - (none) | `FUN_007edc93 (unnamed)` | (1, 1) | ver(1, 1) call:Rva003ECA69Element::rva003ECAE0 astr coord3d real u32 bool |
| 0x7F87B2 | - (none) | `FUN_007f87b2 (unnamed)` | (1, 1) | ver(1, 1) coord2d i32 call:XferLivingWorldBattleID i32 call:XferLivingWorldBattleResolutionType call:Rva003EFE82Getx2 i32 call:Rva003F6672::rva003F6672 |
| 0x81C567 | - (none) | `FUN_0081c567 (unnamed)` | (1, 2) | ver(1, 2) u32 call:FUN_0081c45f |
| 0x8210D3 | - (none) | `FUN_008210d3 (unnamed)` | (1, 1) | ver(1, 1) i32 call:XferObjectID call:Rva0043B0B1::rva0043B0B1 call:XferObjectID call:Rva0043B0B1::rva0043B0B1 |
| 0x82192F | - (none) | `FUN_0082192f (unnamed)` | (1, 1) | ver(1, 1) u16 snapshotx2 |
| 0x824EC4 | A (none) | `FUN_00824ec4 (unnamed)` | (1, 1) | ver(1, 1) i32 astr BLOK snapshot EBLK skipBLOK astr BLOK snapshot EBLK |
| 0x8363F2 | A (none) | `FUN_008363f2 (unnamed)` | (1, 1) | ver(1, 1) bool snapshot |
| 0x84A6E8 | - (none) | `FUN_0084a6e8 (unnamed)` | (1, 1) | ver(1, 1) ->SkirmishGameInfo::xfer |
| 0x855466 | - (none) | `FUN_00855466 (unnamed)` | (1, 2) | ver(1, 2) ->UpdateModule::xfer call:UpgradeMux::upgradeMuxXfer u32 bool u32 |
| 0x8581A1 | - (none) | `FUN_008581a1 (unnamed)` | (1, 1) | ver(1, 1) ->UpdateModule::xfer virtual+0x160 call:XferObjectID bool |
| 0x869655 | - (none) | `FUN_00869655 (unnamed)` | (1, 1) | ->OpenContain::xfer ver(1, 1) bool i32 u32 |
| 0x869E86 | - (none) | `FUN_00869e86 (unnamed)` | (1, 1) | ver(1, 1) ->OpenContain::xfer i32 u32 bool |
| 0x880E44 | - (none) | `FUN_00880e44 (unnamed)` | (1, 1) | ->HordeGarrisonContain::xfer ver(1, 1) boolx2 |
| 0x88356A | - (none) | `FUN_0088356a (unnamed)` | (1, 2) | ver(1, 2) ->SlaughterHordeContain::xfer call:XferObjectID |
| 0x8844DB | - (none) | `FUN_008844db (unnamed)` | (1, 1) | ver(1, 1) ->HordeGarrisonContain::xfer |
| 0x88C5A6 | - (none) | `FUN_0088c5a6 (unnamed)` | (1, 1) | ver(1, 1) raw |
| 0x88FA81 | - (none) | `FUN_0088fa81 (unnamed)` | (1, 1) | ver(1, 1) ->FireWeaponUpdate::xfer i32 coord3d |
| 0x89123E | - (none) | `FUN_0089123e (unnamed)` | (1, 1) | ver(1, 1) ->UpdateModule::xfer bool |
| 0x8936C9 | - (none) | `FUN_008936c9 (unnamed)` | (1, 2) | ->UpdateModule::xfer ver(1, 2) boolx3 u32 |
| 0x89C37A | - (none) | `FUN_0089c37a (unnamed)` | (1, 3) | ->Rva004C8A95::xfer ver(1, 3) ->UpdateModule::xfer i32x2 bool i32 call:XferObjectID i32 virtual+0x160 bool raw realx2 |
| 0x8A6DCB | - (none) | `FUN_008a6dcb (unnamed)` | (1, 2) | ->UpdateModule::xfer ver(1, 2) call:UpgradeMux::upgradeMuxXfer u32 call:XferObjectID u32 call:XferObjectID |
| 0x8AD8D0 | - (none) | `FUN_008ad8d0 (unnamed)` | (1, 2) | ->AIUpdateInterface::xfer ver(1, 2) i32 call:XferObjectID u32 snapshot raw i32 bool coord3d raw snapshot call:XferObjectID coord3d bool i32 boolx3 snapshot virtual+0x160 u32 |
| 0x8B1EE7 | - (none) | `FUN_008b1ee7 (unnamed)` | (1, 1) | ver(1, 1) ->SpecialAbilityUpdate::xfer bool |
| 0x8BAB72 | - (none) | `FUN_008bab72 (unnamed)` | (1, 1) | ver(1, 1) astr ->UpgradeModule::xfer |
| 0x8BB126 | - (none) | `FUN_008bb126 (unnamed)` | (1, 1) | ver(1, 1) ->UpgradeModule::xfer |
| 0x8BC8E4 | - (none) | `FUN_008bc8e4 (unnamed)` | (1, 2) | ver(1, 2) ->UpdateModule::xfer bool |
| 0x8BCB69 | - (none) | `FUN_008bcb69 (unnamed)` | (1, 3) | ver(1, 3) ->UpdateModule::xfer real bool |
| 0x8BCE8F | - (none) | `FUN_008bce8f (unnamed)` | (1, 1) | ->Rva004C8A95::xfer ver(1, 1) bool |
| 0x8C651D | - (none) | `FUN_008c651d (unnamed)` | (1, 1) | ver(1, 1) |
| 0x8C891C | - (none) | `FUN_008c891c (unnamed)` | (1, 2) | ver(1, 2) ->Rva00493009::xfer bool |
| 0x8D3244 | - (none) | `FUN_008d3244 (unnamed)` | (1, 1) | ver(1, 1) ->UpdateModule::xfer call:Rva004CE6E4::rva004CE6E4 |
| 0x8DB378 | A (none) | `FUN_008db378 (unnamed)` | (1, 3) | ver(1, 3) bool real |
| 0x8E1A22 | A (none) | `FUN_008e1a22 (unnamed)` | (1, 1) | ver(1, 1) icoord2d |
| 0x8FAD17 | - (none) | `FUN_008fad17 (unnamed)` | (1, 1) | ver(1, 1) call:Rva0036ABAFXferx2 |
| 0x900D79 | A (none) | `FUN_00900d79 (unnamed)` | (1, 2) | ver(1, 2) call:Rva003EFE82Get call:XferRegionAwardDisputeDisputeType call:Rva002B24F0Get bool call:FUN_006b99d6 |
| 0x94BAE0 | - (none) | `FUN_0094bae0 (unnamed)` | (1, 1) | ver(1, 1) ->StateMachine::xfer call:XferObjectID coord3d |
| 0x94BB1E | - (none) | `FUN_0094bb1e (unnamed)` | (1, 1) | ver(1, 1) u32 |
| 0x94BC51 | - (none) | `FUN_0094bc51 (unnamed)` | (1, 1) | ver(1, 1) |
| 0x94D587 | - (none) | `FUN_0094d587 (unnamed)` | (1, 1) | ->GroupOrder::xfer ver(1, 1) call:XferObjectID coord3d |
| 0x94D83B | - (none) | `FUN_0094d83b (unnamed)` | (1, 1) | ->GroupOrder::xfer ver(1, 1) call:XferObjectID bool coord3d |
| 0x965810 | - (none) | `FUN_00965810 (unnamed)` | (1, 1) | ver(1, 1) i32x2 astr real |
| 0x966BBD | - (none) | `FUN_00966bbd (unnamed)` | (1, 1) | ver(1, 1) |
| 0x968040 | A (none) | `FUN_00968040 (unnamed)` | (1, 1) | ver(1, 1) bool coord3d |
| 0x9699B5 | - (none) | `FUN_009699b5 (unnamed)` | (1, 1) | ver(1, 1) call:Rva0055B367::rva0055B367 realx2 i32 |
| 0x96BD1F | - (none) | `FUN_0096bd1f (unnamed)` | (1, 1) | ver(1, 1) call:xferRandomVariable astr bool |
| 0x98B896 | A (none) | `FUN_0098b896 (unnamed)` | (1, 1) | ver(1, 1) astr call:XferException::XferException |
| 0x990B81 | A (none) | `FUN_00990b81 (unnamed)` | (1, 3) | ver(1, 3) bool u32 coord3d bool |
| 0x9930D8 | - (none) | `FUN_009930d8 (unnamed)` | (1, 3) | ver(1, 3) u32 realx2 bool u32 i32x2 |
| 0x993419 | - (none) | `FUN_00993419 (unnamed)` | (1, 1) | ver(1, 1) ->Rva004C8A95::xfer real |
| 0x9B549E | - (none) | `FUN_009b549e (unnamed)` | (1, 1) | ver(1, 1) ->AITactic::xfer call:XferObjectID u32x2 boolx2 virtual+0x30x2 |
| 0x9B5D0F | - (none) | `FUN_009b5d0f (unnamed)` | (1, 1) | ver(1, 1) call:XferObjectID realx2 |
| 0x9B6D01 | - (none) | `FUN_009b6d01 (unnamed)` | (1, 1) | ver(1, 1) ->AITactic::xfer call:XferObjectID u32x2 i32x2 call:FUN_009b5d0fx2 u32 call:XferObjectIDx2 coord3d real i32x2 u32 call:XferObjectID bool u32 bool virtual+0x30x2 |
| 0x9B72A4 | - (none) | `FUN_009b72a4 (unnamed)` | (1, 2) | ver(1, 2) ->AITactic::xfer bool i32x2 |
| 0x9BA100 | - (none) | `FUN_009ba100 (unnamed)` | (1, 1) | ver(1, 1) ->AITactic::xfer call:XferObjectID u32x2 coord3d boolx2 virtual+0x30x2 |
| 0x9E8474 | - (none) | `FUN_009e8474 (unnamed)` | (1, 1) | ver(1, 1) call:FUN_00a01d19 call:XferObjectIDx2 i32x2 coord3dx2 u32 |
| 0x9E9090 | - (none) | `FUN_009e9090 (unnamed)` | (1, 1) | ver(1, 1) call:FUN_00a01d19 call:XferObjectIDx3 i32x2 coord3dx2 u32 |
| 0x9E93C8 | - (none) | `FUN_009e93c8 (unnamed)` | (1, 1) | ver(1, 1) |
| 0x9EB654 | - (none) | `FUN_009eb654 (unnamed)` | (1, 1) | ver(1, 1) call:FUN_00a01d19 call:XferObjectID |
| 0x9EDF73 | - (none) | `FUN_009edf73 (unnamed)` | (1, 1) | ver(1, 1) ->AIUpgrade::DoXfer bool i32x2 |
| 0xA01D19 | - (none) | `FUN_00a01d19 (unnamed)` | (1, 1) | call:FUN_009930d8 ver(1, 1) coord3d |
| 0xA3B040 | - (none) | `FUN_00a3b040 (unnamed)` | (1, 1) | ver(1, 1) |
<!-- END GENERATED -->
