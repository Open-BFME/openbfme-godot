// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The client side of retail's map object creation (lane MAPOBJ-1): which of a map's objects get a Drawable (or a client-only tree /
// shrub / prop) at map start, where, how big, with which model condition flags and which draw modules. No Godot, no game logic: the
// result is data (MapObjectDrawables) plus a deterministic report (MapObjectReport).
//
// Sources, in PLAN rule 1 priority order. Facts are tagged TARGET (RotWK game.dat, caveat S-001; "RW 0x..." addresses, found by
// disassembly: tools/mapobj/README in the commit message, addresses below), DONOR (Open-BFME-1 / Open-BFME-2 matched files and the
// Zero Hour source) or INFERENCE.
//
//   TARGET RW 0x70F403  the object chunk reader (the BFME1 twin Rva00088B70ReadMapObject is matched code): x y z angle flags name
//                       dict; for chunk version <= 2 z is forced to 0; an object with z < -1000.0 (RW 0xBDD420) or z > 12799.8046875
//                       (RW 0xC1EEF0) is DROPPED (ZH drops outside [-1000, 1593.75], MAP-1 keeps every object and counts the ZH
//                       range: here the retail range decides); the template is looked up only when ThingFactory contains the name
//                       (RW 0x6D12E5 / 0x6D1305: a hash map on the case-SENSITIVE name, so a name that differs in case has NO
//                       template); the MapObject runtime flags are |= 4 for a waypointID that is an INT, |= 0x20 for a
//                       GenericAIObjectID INT (BFME addition), |= 2 for a lightHeightAboveTerrain REAL, |= 8 for a scorchType INT.
//   TARGET RW 0x70EE32  MapObject::getLocation: when the object has a template the location is moved by the template's
//                       GeometryRotationAnchorOffset (the Coord2D at template +0xA8, parsed by RW 0xAD14C0): the code at RW 0xAD1A60 adds
//                       x += ax*cos(a) + ay*sin(a), y += ay*cos(a) + ax*sin(a) (a = the object's angle at +0x1C: the constructor RW 0x70F1A3 normalises it with RW 0x644FD0). For ay == 0 that is the
//                       offset rotated by the angle (the Minas Tirith city pieces, ax ~ 1000); for ay != 0 it is NOT a rotation. Ported
//                       as the binary has it. BFME addition: ZH has no such offset.
//   TARGET RW 0x62DCE4  the object loop of map start (GameLogic::startNewGame in ZH: GameLogic.cpp 4600-4880): parameters (out pair
//                       list, excluded KindOf, required KindOf, loadingSaveGame); per MapObject in list order:
//                         1. flags & 0x36 (ROAD_POINT1|2, BRIDGE_POINT1|2): skipped (terrain side);
//                         2. no template: skipped; a template with IsBridge (+0x5F5) is created by the earlier bridge pass;
//                         3. KindOf SHRUBBERY (bit 6) and the trees-off flag: skipped;
//                         4. position = location + TerrainLogic::getGroundHeight(x, y) (z only), angle = normalizeAngle(angle)
//                            (RW 0x644FD0: into (-PI, PI]);
//                         5. prop = KindOf OPTIMIZED_PROP (bit 100) or (CLEARED_BY_BUILD (bit 51) and FenceWidth (+0x4B0) == 0, "fluff");
//                            sound = KindOf OPTIMIZED_SOUND (bit 192) and the object has no non-empty objectName;
//                            tree/shrub = KindOf TREE (bit 94) / SHRUB (bit 95);
//                         6. prop || sound || tree || shrub: CLIENT-ONLY path: matrix = rotation about Z by the angle, or with
//                            alignToTerrain (Dict bool, RW 0x62E09E) the matrix RW 0x67D208 builds from the terrain normal; scale =
//                            template Scale (+0x4F0, default 1.0 from the constructor RW 0x74008C) times objectPrototypeScale (REAL,
//                            RW 0x62E1CE) when present; then TREE -> RW 0x683D89, else SHRUB -> RW 0x6808ED, else sound -> RW 0x647939,
//                            else prop -> RW 0x67D742 (TerrainLogic forwards to the terrain visual, vtable +0x50);
//                         7. anything else: a full Object (ThingFactory::newObject, RW 0x6D165E) for the team of originalOwner, whose
//                            Drawable is made by the object; FLAG_DRAWS_IN_MIRROR (1) or KindOf CAN_CAST_REFLECTIONS (bit 5) sets the
//                            drawable mirror status; orientation = alignToTerrain matrix or the angle; then
//                            updateObjValuesFromMapProperties (RW 0x695A06).
//                       FLAG_DONT_RENDER (0x100) is NOT tested anywhere in this loop nor elsewhere in the image (a scan of the code
//                       finds no test of the flag byte): retail draws those objects.
//   TARGET RW 0x695A06  updateObjValuesFromMapProperties: objectName; objectInitialHealth ... and the Drawable keys of interest here:
//                       objectPrototypeScale (REAL, != 1.0): drawable instance scale (+0x200) *= value (RW 0x695E5A); objectTime 1 clears,
//                       2 sets MODELCONDITION_NIGHT (bit 7) in the object's model condition flags; objectWeather 1 clears, 2 sets
//                       MODELCONDITION_SNOW (bit 8) (RW 0x695EBD-0x695F2F).
//   TARGET RW 0x679FD7  Drawable constructor: instance scale = template Scale; draw modules are created in template list order, each
//                       skipped only when the GameLOD module gate (RW 0xDE4364 +0x24) is on and the module's minimum LOD exceeds the
//                       static LOD (RW 0xDE3B84 +0x1774).
//   DONOR  ZH Object::friend_bindToDrawable (Object.cpp 2975-3010): with GameData ForceModelsToFollowTimeOfDay / ForceModelsToFollowWeather
//          (both Yes in RotWK GameData.ini, RW 0xBFFAC0 / 0xBFFAD0) a new drawable gets NIGHT when the map's time of day is night and
//          SNOW when the map's weather is snowy; ZH W3DTerrainVisual::addProp does the same for props.
//   INFERENCE  initial model condition flags other than NIGHT/SNOW stay empty (S-110).
//
// What is NOT retail knowledge (stops, docs/STOPS.md): S-110 (initial object state), S-111 / S-112 / S-113 (draw modules),
// S-114 (GameData switches the loop reads), S-115 (floors that start hidden), S-116 (map.ini).

#pragma once

#include "Common/MapObject.h"
#include "Common/ModelState.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/MapCreationHooks.h"
#include "GameClient/MapUtil.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameLogic/Map/TerrainLogic.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// What became of one object of the ObjectsList.
enum MapObjectFate
{
	MAPOBJ_CULLED_Z = 0,        ///< the reader drops it (z outside [-1000, 12799.8046875])
	MAPOBJ_ROAD_BRIDGE_POINT,   ///< flags & 0x36: roads and bridges are built by the terrain side
	MAPOBJ_NO_TEMPLATE_EMPTY,   ///< empty template name
	MAPOBJ_WAYPOINT,            ///< no template, waypointID INT (TerrainLogic keeps it)
	MAPOBJ_GENERIC_AI,          ///< no template, GenericAIObjectID INT (AI wall hubs and the like)
	MAPOBJ_SCORCH,              ///< no template, scorchType INT (a terrain decal; not drawn, S-117)
	MAPOBJ_LIGHT,               ///< no template, lightHeightAboveTerrain REAL
	MAPOBJ_NO_TEMPLATE_STAR,    ///< a "*" name without a template that no property explains
	MAPOBJ_UNRESOLVED,          ///< a name that does not resolve and nothing explains: an error
	MAPOBJ_SHRUBBERY_OFF,       ///< SHRUBBERY with the trees-off switch
	MAPOBJ_CLIENT_TREE,         ///< client-only tree (KindOf TREE)
	MAPOBJ_CLIENT_SHRUB,        ///< client-only shrub (KindOf SHRUB)
	MAPOBJ_CLIENT_PROP,         ///< client-only prop or fluff
	MAPOBJ_CLIENT_SOUND,        ///< optimized sound object (no model)
	MAPOBJ_OBJECT_BRIDGE,       ///< a full object made by the bridge pass (IsBridge)
	MAPOBJ_OBJECT,              ///< a full object with a Drawable
	MAPOBJ_FATE_COUNT
};
const char *MapObjectFateName(MapObjectFate f);
bool MapObjectFateDrawsAnything(MapObjectFate f); ///< fates that may produce a model (before the draw modules are looked at)

// One draw module of a template, with its typed data.
struct MapDrawModule
{
	std::string className;
	std::string tag;
	W3DDrawKind kind = W3D_DRAWKIND_NOT_DRAWN;
	const ModuleData *data = nullptr;   ///< the typed data (W3DModelDrawModuleData / W3DTreeDrawModuleData / W3DPropDrawModuleData ...) or raw
	std::string model;                  ///< the base model this module shows at map start ("" when none)
	std::string notDrawnReason;         ///< why nothing is drawn for this module ("" when it draws `model`)
};

// What the template of an object says (cached per template).
struct MapTemplateInfo
{
	const ThingTemplate *tmpl = nullptr; ///< the final override (map.ini)
	std::string name;
	std::array<std::uint32_t, 7> kindOf{};  ///< RW 0xDA0E68 bit names, 224 bits
	bool kindOfError = false;
	std::string kindOfErrorText;
	bool isBridge = false;
	bool shrubbery = false, tree = false, shrub = false, optimizedProp = false, clearedByBuild = false, optimizedSound = false;
	bool walkOnTopOfWall = false, canCastReflections = false;
	float fenceWidth = 0.0f;
	float scale = 1.0f;                  ///< Scale, default 1.0 (RW 0x74008C)
	float anchorX = 0.0f, anchorY = 0.0f; ///< GeometryRotationAnchorOffset
	std::vector<MapDrawModule> draws;
	std::vector<MapCreationHook> aiEventLists;  ///< the AILuaEventsList of each AI module, from the template alone: MapObjectRuntime resolves them in the real event registry
	std::vector<MapCreationHook> creationHooks; ///< the OnCreated Lua handlers retail runs for a full object of this template (not run here: S-110)
	bool kindOf_test(int bit) const { return (kindOf[(size_t)bit >> 5] >> (bit & 31)) & 1u; }
};

// A map side (player) as the SidesList names it.
struct MapSidePlayer
{
	std::string name;           ///< playerName ("" = the neutral side)
	bool hasColor = false;
	std::uint32_t colorRGB = 0; ///< the side's colour 0xRRGGBB: playerColor when the map sets one, else its faction's PreferredColor
	enum ColorSource { COLOR_NONE = 0, COLOR_MAP = 1, COLOR_FACTION = 2 } colorSource = COLOR_NONE;
	bool isHuman = false;
	std::string faction;
};

// One drawable (or client-only draw) the loop makes.
struct MapObjectDrawable
{
	size_t objectIndex = 0;          ///< index in LoadedMap::chunks.objects
	MapObjectFate fate = MAPOBJ_OBJECT;
	const MapTemplateInfo *info = nullptr;
	Coord3D position;                ///< SAGE world position, z = ground height + the object's z
	float angle = 0.0f;              ///< normalizeAngle(raw angle)
	bool alignToTerrain = false;
	Coord3D normal = { 0.0f, 0.0f, 1.0f };
	float basis[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }; ///< columns X, Y, Z of the orientation (row major 3x3: [col0 row0, col1 row0, col2 row0, ...])
	float scale = 1.0f;              ///< instance scale
	ModelConditionFlags flags;       ///< initial model condition flags
	bool drawsInMirror = false;
	bool hordeMember = false;        ///< a member of a horde object (objectIndex is the horde's index in the ObjectsList)
	std::vector<MapDrawModule> draws; ///< the draw modules with the model each shows for `flags`
	int sideIndex = -1;              ///< index in MapObjectDrawables::sides, -1 = no owner (client-only objects)
	std::string owner;               ///< originalOwner as written
	bool ownerResolved = false;
};

// Everything one object contributes to the report, drawn or not.
struct MapObjectRecord
{
	size_t objectIndex = 0;
	MapObjectFate fate = MAPOBJ_OBJECT;
	std::string templateName;
	std::string detail;              ///< why (non-drawn fates), or the case-insensitive match for an unresolved name
};

struct MapObjectOptions
{
	bool useTrees = true;                       ///< the trees-off switch of the loop is off (S-114)
	bool forceModelsToFollowTimeOfDay = false;  ///< GameData ForceModelsToFollowTimeOfDay (RW 0xBFFAC0)
	bool forceModelsToFollowWeather = false;    ///< GameData ForceModelsToFollowWeather (RW 0xBFFAD0)
	// PlayerTemplate name -> PreferredColor (0xRRGGBB) of the templates that define a StartingBuilding (the real factions; Civilian, Neutral
	// and Observer have none and no team colour): a side whose map dict sets no
	// playerColor takes its faction's colour (S-119: INFERENCE; retail takes the lobby colour of the slot). Filled by
	// MapObjectGameData::loadPlayerTemplates.
	std::map<std::string, std::uint32_t> factionColors;
	// scriptevents.xml / scripts.lua, for the OnCreated hooks the loop reports but does not run (S-110). Filled by MapCreationHooks::load;
	// `loaded` false = not scanned, which the report says (creationHooksScanned) instead of assuming there are none.
	CreationScriptData creationScripts;
};

namespace MapObjectGameData
{
// Name-level scan of the first GameData block of data\ini\gamedata.ini for the two switches the drawable binding reads. Returns
// false + *error when the file is missing or a key is absent (never a silent default).
bool load(ArchiveFileSystem &fs, MapObjectOptions &out, std::string *error);
bool scanText(const std::string &gameDataIni, MapObjectOptions &out, std::string *error);
// Name-level scan of data\\ini\\playertemplate.ini: "PlayerTemplate <Name>" blocks with StartingBuilding and PreferredColor = R:n G:n B:n.
bool loadPlayerTemplates(ArchiveFileSystem &fs, MapObjectOptions &out, std::string *error);
bool scanPlayerTemplates(const std::string &playerTemplateIni, MapObjectOptions &out, std::string *error);
} // namespace MapObjectGameData

struct MapObjectReport
{
	std::string map;
	size_t objects = 0;                                           ///< ObjectsList entries
	size_t byFate[MAPOBJ_FATE_COUNT] = {};
	std::map<std::string, size_t> byTemplate;                     ///< drawn or client-only objects per template name
	std::map<std::string, size_t> byDrawClass;                    ///< draw modules of drawn objects per class
	std::map<std::string, size_t> notDrawnByReason;               ///< "reason" -> objects / modules not drawn
	std::map<std::string, size_t> unresolved;                     ///< unresolved template names -> objects (errors)
	std::map<std::string, size_t> caseMismatch;                   ///< names that resolve only case-insensitively -> objects
	size_t drawables = 0;                                         ///< objects that produce at least one model
	size_t drawnModels = 0;                                       ///< models (one per drawing draw module)
	size_t movedByAnchor = 0;                                     ///< objects moved by GeometryRotationAnchorOffset
	size_t alignedToTerrain = 0;
	size_t withPrototypeScale = 0;
	size_t nightFlagged = 0, snowFlagged = 0;
	size_t hordes = 0, hordeSlots = 0, hordePayload = 0, hordeMembers = 0, hordeUnplaced = 0, hordeRandomOffset = 0; ///< horde objects and their members (S-118)
	size_t sidesWithMapColor = 0, sidesWithFactionColor = 0, sidesWithoutColor = 0; ///< SidesList sides by where their colour comes from
	size_t unownedSides = 0;                                      ///< owners that name no side (fall back to the neutral side)
	std::map<std::string, size_t> ownerProblems;
	// objects carrying property keys whose effect on the initial state is not ported (S-110): key -> objects
	std::map<std::string, size_t> unportedKeys;
	// OnCreated Lua handlers (S-110): retail sends every full object (and every horde member) the event OnCreated; the handlers of its
	// AILuaEventsList are not run here, so their effect (sub objects hidden permanently, upgrades granted ...) is missing from the
	// initial state. Objects counted once per template hook.
	bool creationHooksScanned = false;                            ///< the scripts were available (MapObjectOptions::creationScripts.loaded)
	size_t creationHookObjects = 0;                               ///< full objects and horde members whose template has an OnCreated handler
	std::map<std::string, size_t> creationHooksByFunction;        ///< handler function -> objects
	std::map<std::string, size_t> creationHooksByTemplate;        ///< "template: list -> function, function" -> objects
	std::map<std::string, size_t> creationHookHides;              ///< sub object (lower case) the handlers hide permanently -> objects
	std::map<std::string, size_t> creationHookShows;              ///< sub object (lower case) the handlers show permanently -> objects
	std::vector<std::string> errors;                              ///< per-object problems that must reach a test (unresolved names, bad KindOf, ...)
	std::vector<std::string> stops;                               ///< the stop lines that apply, "[S-1xx] ..."
};

struct MapObjectDrawables
{
	std::vector<MapSidePlayer> sides;
	std::vector<MapObjectDrawable> drawables;
	std::vector<MapObjectRecord> records;      ///< one per ObjectsList entry, in order
	std::map<const ThingTemplate *, MapTemplateInfo> templates; ///< info per final-override template
	MapObjectReport report;
};

namespace MapObjectCreation
{
// Runs the object loop over `map` for `things` (the retail templates with the map's map.ini overrides already applied, load type 2).
// `terrain` supplies ground heights and normals. Deterministic: no randomness, no globals.
void build(const LoadedMap &map, const std::string &mapName, ThingFactory &things, const TerrainLogic &terrain, const MapObjectOptions &options,
	MapObjectDrawables &out);

// The model a draw module shows for the given flags (m.model, or m.notDrawnReason when it shows none; problems go to `errors`): the rule the
// object loop uses (GameClient/Drawable.h reuses it for live drawables).
void resolveDrawModel(MapDrawModule &m, const ModelConditionFlags &flags, const std::string &templateName, std::vector<std::string> &errors);

// KindOf names of RW 0xDA0E68 by index (-1 when unknown); exposed for tests.
int kindOfIndex(const std::string &name);
// The stops that apply to every report (S-110..S-117), as "[S-1xx] text".
std::vector<std::string> stopLines();
} // namespace MapObjectCreation
