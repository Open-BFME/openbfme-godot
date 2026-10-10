// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// GameLogic: the object list, the BFME update scheduler, the six-tick logic frame and object destruction (ZH Include/GameLogic/
// GameLogic.h; spec ini-and-object-model.md 5.4-5.5, horde-and-movement.md 3). Lane LOGIC-1.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; every address below was read from the disassembly and matches the B1 decompile named in
// the spec, except where "RW DIFFERS" says otherwise):
//   * State: `updates[4]` (RW GameLogic + 0xC8, 12 bytes per phase vector) and `sleeping` (+0xF8), `current` (+0x104), the destroy list
//     (+0x108), the object list head (+0xAC), the frame (+0x40), the next object id. (B1 has all of these 4 bytes lower.)
//   * registerObject (the module loop at RW 0x62BD6A, the same loop that rebuilds the vectors after a load, RW 0x62BCBF): append the
//     object at the list tail, make it findable by id, `now = max(frame, 1)`; for every behavior with an update interface: a next call
//     frame of 0 becomes `now`; FOREVER goes to `sleeping` (phase -1), anything else to `updates[getUpdatePhase()]`; the module learns
//     its index and phase.
//   * friend_awakenUpdateModule(obj, u, when) RW 0x62B921: ignored when u is the current module, when `when` equals its next call
//     frame, or when now > 0, next == now and when == now + 1; for an object in the list a sleeping module with when < FOREVER is
//     swap-removed from `sleeping` and appended to updates[phase]; the next call frame is set (clamped to FOREVER). An object not in
//     the list yet: a module index other than -1 is a fatal error, else just the frame is set.
//   * update(phase) RW 0x62E4E8, called with 1..6 (one per engine tick; RW DIFFERS: ++frame happens at the START of phase 1, right
//     after the freeze gate, RW 0x62E577, where B1 increments at the end of phase 1; the condition is RW 0x625130: the frame-advance
//     flag +0x44 set, +0xA8 and +0x9D clear):
//       phase 1: logic-debug-frame helper; freeze gate; ++frame; command-only transition (flag +0x125); frame-2 object block;
//                Pathfinder queue (RW 0x6F2364) and setFPMode; subsystem updates (script engine, Lua, terrain logic, victory system);
//                CRC, recorder, statistics; command list; per object the drawable callback; then (after the scheduler work below and
//                the deferred-entries drain RW 0x629DA6) the end-of-frame checks of every object: disabled expiry (RW 0x690A42),
//                IGNORE_AI_COMMAND (0x690AB9), NO_COLLISIONS (0x690AE5), updatePendingDamage (0x697EB6);
//       phase 2: partition and collision managers; every object whose recorded frame is not the frame records its transform (RW 0x6260E1);
//       phase 3: updates[0] over [0, size / 2); phase 4: updates[0] over [size / 2, size), then FOREVER migration (reverse scan,
//                swap-pop, into `sleeping`); phase 5: updates[1], updates[2] (same migration), then the AI update (RW 0xDE4B40 vslot
//                0x28), the deferred-entries drain, the subsystem batch, processDestroyList (RW 0x62A2C9); phase 6: updates[3] + migration.
//     The loop: `i` walks the vector by index while it can grow; the phase 3 stop and the phase 4 start are `size() / 2` evaluated
//     as the code evaluates them (RW 0x62EA12 .. 0x62EA1E / 0x62E9D4 .. 0x62E9E4). Per module: skip when next call frame > frame;
//     sleep = NONE; if the object has no disabled types, or its mask intersects getDisabledTypesToProcess: current = u; sleep =
//     DESTROYED ? FOREVER : max(update(), NONE); current = null; next call frame = min(frame + sleep, FOREVER).
//   * processDestroyList RW 0x62A2C9: for each queued object, in order, including objects appended during the pass: swap-pop its update
//     modules out of their vectors (fixing the moved module's index), remove it from the pathfind map, unlink it from the object list,
//     erase it from the id table, delete it.
// DONOR: ZH GameLogic.cpp (destroyObject, 3959-4010).
//
// Not ported (stop S-143): the phase work that belongs to other lanes is listed in phaseWork() with a `ported` flag; GameLogic runs
// the ported rows, calls a row's installed function when a lane registered one, and reports every other row.
//
// Determinism (PLAN: lockstep multiplayer): the object list, the id table, the scheduler vectors, the destroy list, the team and player
// lists are vectors or intrusive lists walked in a defined order; the logic RNG is the instance below (explicit algorithm); no pointer
// value or hash order reaches any state; all logic floats go through SimMath / plain float arithmetic compiled without FMA contraction.

#pragma once

#include "Common/PlayerList.h"
#include "Common/RandomValue.h"
#include "Common/Team.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/LargeGroupAudioLink.h"
#include "GameLogic/Map/CastleTemplates.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ProductionSettings.h"
#include "GameLogic/ObjectTypes.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

class Object;
class Drawable;
class ModuleFactory;
class ThingFactory;
class ThingTemplate;
class UpdateModule;
class TerrainLogic;
class UpgradeTypeTable;
class AIWorld;
class Economy;
class CombatState;
class ExperienceWorld;
class CreateAHeroGame;
class GlobalWeatherSystem;
class VictoryConditions;
class InvisibilityManager;
class SkirmishAIManager;
class ShroudManager;
class PartitionManager;
class EmotionWorld;
class ScriptEngine;
struct CrushTemplateInfo;
struct LuaEventArgs;

// ZH GameLogic::sendObjectCreated / Object::~Object's sendObjectDestroyed go to the client; the client layer implements this.
// Lane SMOOTH-1 (S-810): the simulation never holds or calls a Drawable. Everything it tells the client is a call on these hooks, which the
// client layer records as ordered, data-only events (GameClient/ClientEvents.h) that the render side applies later, on its own thread. A
// hook that answers the simulation (showModule) answers from logic-owned data (the object's template), never from a drawable's state, so
// the answer cannot depend on whether or when the render side consumed an event.
class ObjectClientHooks
{
public:
	virtual ~ObjectClientHooks() = default;
	// RW 0x628882: make the Drawable of a new object and bind it (Object::friend_bindToClient)
	virtual void objectCreated(Object &obj) = 0;
	// the object is being deleted (processDestroyList): drop its drawable
	virtual void objectDestroyed(Object &obj) = 0;
	// a model condition bit of the object changed (ZH Object::setModelConditionFlags -> Drawable::setModelConditionFlags)
	virtual void modelConditionChanged(Object &obj, int bit, bool on) { (void)obj; (void)bit; (void)on; }
	// lane ANIM-1, RW 0x67449C(0): the drawable's changed flags reach its draw modules now (see Object::flushDrawableModelConditions); without this call they
	// reach them once at the end of the logic frame (RW 0x6759C4)
	virtual void flushModelConditions(Object &obj) { (void)obj; }
	// RW 0x6789B4 Drawable::showModule: true when one of the template's draw modules has the tag (the drawable makes every draw module of its
	// template, S-114, so the template answers exactly what the drawable would); the visibility change goes to the client
	virtual bool showModule(Object &obj, const std::string &tag, bool visible, bool permanent) { (void)obj; (void)tag; (void)visible; (void)permanent; return false; }
	// RW 0x672823 Drawable::showSubObject
	virtual void showSubObject(Object &obj, const std::string &name, bool visible, bool permanent) { (void)obj; (void)name; (void)visible; (void)permanent; }
	// lane STEALTH-2, RW 0x776F03 (StealthUpdate's disguise): the object's drawable is destroyed (TheGameClient vslot 0x74) and a new one of `tmpl` made
	// (RW 0x6CFE8C) and bound (RW 0x625AA4) at the object's position and orientation with its model conditions, in `color` when `hasColor`
	virtual void replaceDrawable(Object &obj, const ThingTemplate *tmpl, bool hasColor, std::uint32_t color) { (void)obj; (void)tmpl; (void)hasColor; (void)color; }
	// lane BUILD-4, RW 0x670AA2 Drawable::fadeIn(frames) called by the simulation (a wall span's tiles, RW 0x7954A8): the object's drawable fades in over
	// `frames` client frames (0: shown at once)
	virtual void fadeIn(Object &obj, UnsignedInt frames) { (void)obj; (void)frames; }
	// lane CAH-2, RW 0x80AF0B (a Create-a-Hero record's flag 8): the drawable's house colour set (RW 0x80959A: kind 3 and the record's three colours,
	// RW 0x6727B0 -> every draw module's vslot 0x7C); client only (HouseColor.h)
	virtual void setCustomColors(Object &obj, int kind, std::uint32_t c0, std::uint32_t c1, std::uint32_t c2) { (void)obj; (void)kind; (void)c0; (void)c1; (void)c2; }
};

class StateHasher;

// What the creation path of an object needs from the game's data (read by the loaders from the mounted INI files, never hard coded).
struct ObjectFilter;

struct GameLogicSettings
{
	bool enableRepulsors = true;                     ///< AIData EnableRepulsors (RW 0xDE4B40 + 0x18 + 0x64): the RepulsorHelper condition
	float repulsedDistance = 0.0f;                   ///< AIData RepulsedDistance (+ 0x60, lane MODULES-3): the safe path radius beyond the vision range
	bool forceModelsToFollowTimeOfDay = false;       ///< GameData ForceModelsToFollowTimeOfDay (a new drawable gets NIGHT)
	bool forceModelsToFollowWeather = false;         ///< GameData ForceModelsToFollowWeather (SNOW)
	bool bodyThresholdsLoaded = false;               ///< the two thresholds below were read from GameData (an ActiveBody that needs them throws when not)
	float unitDamagedThreshold = 0.0f;               ///< GameData UnitDamagedThreshold (ActiveBody fallback fraction, stop S-149)
	float unitReallyDamagedThreshold = 0.0f;         ///< GameData UnitReallyDamagedThreshold
	bool startingCashLoaded = false;                 ///< the three below were read from GameData / MultiplayerSettings
	unsigned defaultStartingCash = 0;                ///< GameData DefaultStartingCash: a faction without StartMoney starts with it (ZH Player::init)
	// lane BUILD-1 (BuildAssistant, GameData): AllowedHeightVariationForBuilding (ZH: the footprint's highest minus lowest ground), SupplyBuildBorder, MaxLineBuildObjects;
	// buildRulesLoaded is true when the loader read all three (the placement code reports its absence)
	bool buildRulesLoaded = false;
	float allowedHeightVariationForBuilding = 0.0f;
	float supplyBuildBorder = 0.0f;
	int maxLineBuildObjects = 0;
	// lane BUILD-3 (GameData, RW GlobalData + 0x11E4): BuilderMoveFromNewStructureDistance, the walk of a porter out of the structure it built (RW 0x88D6AE); loaded when a block read it
	bool builderMoveLoaded = false;
	float builderMoveFromNewStructureDistance = 0.0f;
	// lane COMBAT-2 (GameData): Gravity (per frame^2, negative) for the structure collapse and DefaultStructureRubbleHeight for a structure that reaches the rubble state; structureRulesLoaded is true
	// when the loader read both (a collapse or a rubble state without them is reported)
	bool structureRulesLoaded = false;
	float gravity = 0.0f;
	float defaultStructureRubbleHeight = 0.0f;
	// lane COMBAT-2 (GameData, RW GlobalData + 0xEBC / + 0xEC0 / + 0x110C): the object filters that decide whether a player still has a base (VictoryConditionStructureObjectFilter) or an
	// army (VictoryConditionUnitObjectFilter) and SecondsBeforeBaseCheckActive (a RotWK game.dat default 5.0, RW 0x6438FD; gamedata.ini sets none) in seconds; victoryRulesLoaded is true
	// when the loader read both filters
	bool victoryRulesLoaded = false;
	std::shared_ptr<const ObjectFilter> victoryStructureFilter;
	std::shared_ptr<const ObjectFilter> victoryUnitFilter;
	float secondsBeforeBaseCheckActive = 5.0f;
	// lane END-1 (GameData, RW GlobalData + 0x1168 .. + 0x11B8, rows RW 0xC00F00 .. 0xC01040, parseReal RW 0x42ED00 / ObjectFilter RW 0x76392F): the ScoreKeeper's
	// object filter (ObjectsThatScore) and multipliers; the GlobalData constructor zeroes the twenty floats (RW 0x643D3C rep stosd), so a GameData without them
	// starts every multiplier at 1.0 (RW 0x643D31 / 0x643D42: `rep stosd` of 1.0f over the twenty floats) and PlayerEliminatedMultiplier at 0.1 (RW 0x643D24 loads
	// the float RW 0xBD83D4 = 0.1f, RW 0x643D48 stores it at + 0x11B8); a gamedata.ini row overrides each
	std::shared_ptr<const ObjectFilter> objectsThatScore; ///< + 0x1168
	struct ScoreMultipliers
	{
		float unitsBuilt = 1.0f;            ///< + 0x116C
		float unitsDestroyed = 1.0f;        ///< + 0x1170
		float structuresBuilt = 1.0f;       ///< + 0x1174
		float structuresDestroyed = 1.0f;   ///< + 0x1178
		float heroesVetted = 1.0f;          ///< + 0x117C
		float unitsVetted = 1.0f;           ///< + 0x1180
		float objectivesCompleted = 1.0f;   ///< + 0x1184
		float suppliesCollected = 1.0f;     ///< + 0x1188
		float powerPoints = 1.0f;           ///< + 0x118C
		float regionCommandPoints = 1.0f;   ///< + 0x1190
		float regionResources = 1.0f;       ///< + 0x1194
		float regionPowerPoints = 1.0f;     ///< + 0x1198
		float timeTakenMultiplier = 1.0f;   ///< + 0x119C
		float timeTakenMaximumScore = 1.0f; ///< + 0x11A0
		float timeTakenMinimumScore = 1.0f; ///< + 0x11A4
		float totalVictoryRequiredScore = 1.0f;  ///< + 0x11A8
		float normalVictoryRequiredScore = 1.0f; ///< + 0x11AC
		float normalVictoryRequiredObjectivesPercentage = 1.0f; ///< + 0x11B0
		float skillPoints = 1.0f;           ///< + 0x11B4
		float playerEliminated = 0.1f;      ///< + 0x11B8
	} score;
	// lane RENDER-2 (GameData StandardPublicBone, row RW 0xC006D0, parser RW 0x42E59E = parseAsciiStringVectorAppend, GlobalData + 0xBA8): the bones every model's pristine bone
	// set looks for first (RW 0x4BD9A7 walks [0xDE4364] + 0xBA8); an empty list when gamedata.ini names none
	std::vector<std::string> standardPublicBones;
	// lane SPELL-2 (GameData SpecialPowerViewObject, row RW 0xC006C0, parseAsciiString, GlobalData + 0xBA4): the object a special power with a ViewObjectDuration /
	// ViewObjectRange leaves at its target (RW 0x896FD9); empty when gamedata.ini names none
	std::string specialPowerViewObject;
	// lane STEALTH-1 (GameData, RW GlobalData + 0xEB4 / + 0x11CC / + 0x11D0 / + 0x11D4 / + 0x11D8; rows RW 0xC00C30, 0xC01070, 0xC01080, 0xC01090, 0xC010A0): the camouflage
	// detectors' filter (RW 0x76392F), the frames before a revealed object may turn invisible again (ReinvisibityDelay [sic], parseDurationUnsignedInt RW 0x73A429) and the
	// client's opacity pulse of an own invisible object (InvisibilityOpacityMin / Max parseReal, InvisibilityOpacityCycleFrames parseUnsignedInt RW 0x42ECB2);
	// invisibilityRulesLoaded is true when the loader read all five (the InvisibilityManager reports their absence)
	bool invisibilityRulesLoaded = false;
	std::shared_ptr<const ObjectFilter> camouflageDetectorFilter;
	unsigned reinvisibilityDelay = 0;
	float invisibilityOpacityMin = 0.0f;
	float invisibilityOpacityMax = 0.0f;
	unsigned invisibilityOpacityCycleFrames = 0;
	// lane GARRISON-1 (GameData rows RW 0xC011D0 GarrisonedRangeMultiplier + 0x1224 parseReal RW 0x42ED00, RW 0xC011F0 MaxNumMembersToForceToImmediatelyEnter
	// + 0x1230 and RW 0xC01200 WaitToForceMemberToEnterDelay + 0x1234, parseUnsignedInt RW 0x42ECB2): optional rows; the defaults are the GlobalData constructor's
	// (RW 0x6439DE / 0x643A48: -1.0 = no range change; RW 0x643A5D: 1; RW 0x6439B7 / 0x643A67: 5 frames). Retail 2.01 gamedata.ini sets only the multiplier (1.25)
	float garrisonedRangeMultiplier = -1.0f;
	unsigned maxNumMembersToForceToImmediatelyEnter = 1;
	unsigned waitToForceMemberToEnterDelay = 5;
	// lane GARRISON-2: MaxTunnelCapacity (GameData row RW 0xC00360, parseInt RW 0x42EC5E, + 0xA98; the GlobalData constructor's 0, RW 0x642FB5): the riders a
	// player's tunnel network holds (TunnelTracker RW 0x8FA152)
	int maxTunnelCapacity = 0;
	unsigned initialCredits[5] = { 0, 0, 0, 0, 0 };  ///< MultiplayerSettings InitialCreditsVeryLow .. VeryHigh: the lobby's money choices
	// MultiplayerSettings colours (`MultiplayerColor <Name>` blocks of data\\ini\\multiplayer.ini, lane START-1): the lobby's colour list, in file order; the index is the
	// slot's colour. ZH MultiplayerColorDefinition: RGBColor and RGBNightColor are the day and night player colours (0xRRGGBB here; Player::setPlayerColor
	// takes 0xFF000000 | rgb), TooltipName a game text label, AvailableInWotR whether the strategic (War of the Ring) lobby offers it.
	struct MultiplayerColorDef
	{
		std::string name;
		std::uint32_t rgb = 0, nightRgb = 0, livingWorldRgb = 0, livingWorldBannerRgb = 0;
		std::string tooltipName;
		bool availableInWotR = true;
	};
	std::vector<MultiplayerColorDef> multiplayerColors;
	bool night = false;                              ///< the map's time of day is night (TerrainLogic / GlobalLighting)
	bool snowy = false;                              ///< the map's weather is snow
	// not simulation inputs: what the loader read from and left unread (stop S-152); never hashed
	std::set<std::string> unappliedFields;           ///< "Block.Field" of the GameData / AIData / MultiplayerSettings fields no row of this lane reads
	std::vector<std::string> filesLoaded;
	// the mutable simulation settings in explicit order (lockstep hash): everything above except the two diagnostics
	void crc(StateHasher &hasher) const;
};

class ArchiveFileSystem;

namespace GameLogicSettingsLoader
{
// Reads data\ini\gamedata.ini (GameData: ForceModelsToFollowTimeOfDay, ForceModelsToFollowWeather, UnitDamagedThreshold, UnitReallyDamagedThreshold,
// DefaultStartingCash), data\ini\default\aidata.ini (AIData: EnableRepulsors) and data\ini\multiplayer.ini (InitialCredits*) through the shared INI
// pipeline (macros, retail field parsers, later blocks override). A missing file, a missing key or a malformed value is an error (*error), never a
// default (PLAN rule 10). The scan* forms read one block's text (tests, tools).
bool load(ArchiveFileSystem &fs, GameLogicSettings &out, std::string *error);
bool scanGameData(const std::string &text, GameLogicSettings &out, std::string *error);
bool scanAIData(const std::string &text, GameLogicSettings &out, std::string *error);
bool scanMultiplayer(const std::string &text, GameLogicSettings &out, std::string *error);
} // namespace GameLogicSettingsLoader

class GameLogic
{
public:
	GameLogic(ThingFactory &things, ModuleFactory &modules, PlayerList &players, RandomAlgorithm rng);
	~GameLogic();
	GameLogic(const GameLogic &) = delete;
	GameLogic &operator=(const GameLogic &) = delete;

	ThingFactory &things() const { return m_things; }
	ModuleFactory &modules() const { return m_modules; }
	PlayerList &players() const { return m_players; }
	GameLogicRandom &random() { return m_random; }
	GameLogicSettings &settings() { return m_settings; }
	// lane BUILD-1: the castle base layouts (.bse files of Bases.big), loaded on first use by the CastleBehavior that unpacks one
	CastleTemplateStore &castleTemplates() { return m_castleTemplates; }
	// lane ECON-1: the world-level economy (GameData values, the game context, the TerrainResourceManager, the PlayerEconomyHost)
	Economy &economy() { return *m_economy; }
	const Economy &economy() const { return *m_economy; }
	// lane COMBAT-1: the in-flight shots, the counters and the projectile seam of this game
	CombatState &combat() { return *m_combat; }
	// lane COMBAT-2: the skirmish win / lose rules (init() after the players and their bases exist; update() runs in the logic frame's first phase)
	VictoryConditions &victory() { return *m_victory; }
	const VictoryConditions &victory() const { return *m_victory; }
	// lane AI-1: TheSkirmishAIManager (newGame after the starting bases; update() runs in phase 5 after the destroy list)
	SkirmishAIManager &skirmishAI() { return *m_skirmishAI; }
	const SkirmishAIManager &skirmishAI() const { return *m_skirmishAI; }
	// lane XP-1: TheDelayedExperienceLevelGrantSystem (update in phase 5 after the destroy list, RW 0x62EBD9) and the skill point awards
	ExperienceWorld &experience() { return *m_experience; }
	const ExperienceWorld &experience() const { return *m_experience; }
	// lane HERO-2: the players' Create-a-Heroes (TheCreateAHeroSystem's game side: the slot records, RW 0x61B17D / 0x809FFB / 0x73C28F)
	CreateAHeroGame &createAHeroes() { return *m_createAHeroes; }
	const CreateAHeroGame &createAHeroes() const { return *m_createAHeroes; }
	GlobalWeatherSystem &weather() { return *m_weather; } // lane SPELL-2: TheGlobalWeatherSystem (RW 0xDE772C)
	const GlobalWeatherSystem &weather() const { return *m_weather; }
	// lane SCRIPT-1: TheScriptEngine (RW 0xDE3BAC), the map script engine: idle until a game loads its scripts (ScriptEngine::newGame); its update runs
	// first in phase 1's subsystem row (RW 0x62E6AF); hashed (section "script engine") once loaded
	ScriptEngine &scriptEngine() { return *m_scriptEngine; }
	const ScriptEngine &scriptEngine() const { return *m_scriptEngine; }
	// lane STEALTH-1: TheGameLogic + 0x178, the InvisibilityManager (update in phase 1 before the command list, RW 0x62E8DA)
	InvisibilityManager &invisibility() { return *m_invisibility; }
	const InvisibilityManager &invisibility() const { return *m_invisibility; }
	const CombatState &combat() const { return *m_combat; }
	const GameLogicSettings &settings() const { return m_settings; }
	void setClientHooks(ObjectClientHooks *hooks) { m_clientHooks = hooks; }
	// lane FX-2: the logic's calls into the client effect system (FireFX, death / damage / collapse FX ...), forwarded to the client's sink at the moment
	// of the call; not hashed, never read back (GameLogic/FXEvents.h). The frame's list is dropped at the start of each logic frame
	FXEventLog &fxEvents() { return m_fxEvents; }
	const FXEventLog &fxEvents() const { return m_fxEvents; }
	ObjectClientHooks *clientHooks() const { return m_clientHooks; }
	// lane AUDIO-4: TheLargeGroupAudio's face to the logic (the gate the LargeGroupAudioUpdate modules read, their calls forwarded to the client's sink;
	// GameLogic/LargeGroupAudioLink.h). The gate is set once at the new game (RW 0x62FD90); the events are not hashed
	LargeGroupAudioLink &largeGroupAudio() { return m_largeGroupAudio; }
	const LargeGroupAudioLink &largeGroupAudio() const { return m_largeGroupAudio; }

	// ---- terrain and error reporting ----------------------------------------------------------------------------------------
	// the map's terrain (TerrainLogic::getGroundHeight); null until a map is loaded: ground height is then 0 everywhere and hasTerrain() says so
	void setTerrain(const TerrainLogic *terrain) { m_terrain = terrain; }
	bool hasTerrain() const { return m_terrain != nullptr; }
	const TerrainLogic *terrain() const { return m_terrain; }
	// lane PROD-1: what production asks the produced objects' AI to do (GameLogic/AI/AICommandSink.h)
	AICommandSink &aiCommands() { return m_aiCommands; }
	// lane PROD-1: the producer of the NEXT object newObject makes (consumed by its constructor, so the creation modules already see it: HordeContain
	// makes no payload for a produced horde, B1 HordeContainCreatePayload.cpp)
	// the GameData numbers of the build arithmetic and command points (ProductionSettings::load), and the upgrade name -> type table the CommandSet
	// availability test needs (stop S-205); both must be set before production runs (PLAN rule 10)
	ProductionSettings &productionSettings() { return m_productionSettings; }
	const ProductionSettings &productionSettings() const { return m_productionSettings; }
	void setUpgradeTypes(const UpgradeTypeTable *table) { m_upgradeTypes = table; }
	const UpgradeTypeTable *upgradeTypes() const { return m_upgradeTypes; }
	void setPendingProducer(ObjectID id) { m_pendingProducer = id; }
	ObjectID takePendingProducer()
	{
		const ObjectID id = m_pendingProducer;
		m_pendingProducer = INVALID_ID;
		return id;
	}
	float getGroundHeight(float x, float y) const;
	// problems nothing explains; each distinct text is kept once and reaches report().errors
	void reportError(const std::string &text);
	// lane BUILD-1: a rule met at runtime that is registered as a stop but not ported (a castle's PreBuiltList ...): each distinct text reaches report().stops once
	void noteStop(const std::string &text)
	{
		m_notedStops.insert(text);
		++m_notedStopHits[text]; // lane QA-1: how often a game meets each stop (diagnostics, not simulation state)
	}
	const std::map<std::string, unsigned> &notedStopHits() const { return m_notedStopHits; }

	// lane CAMP-1H: TheGameLogic + 0xA4, the game's difficulty (0 easy, 1 normal, 2 hard, 3 brutal): reset to 1 (GameLogic::reset RW 0x62D3EB),
	// prepareNewGame (RW 0x77948E) stores the new game message's difficulty here (the script engine's stays 1). Read by the difficulty bonus of a computer
	// player's objects outside a multiplayer game (Player::applyDifficultyBonusesForObject RW 0x6AC3E3) and the mission restart (RW 0x9221B6). Hashed.
	void setGameDifficulty(int d) { m_gameDifficulty = d; }
	int getGameDifficulty() const { return m_gameDifficulty; }

	// ---- frame ------------------------------------------------------------------------------------------------------------
	UnsignedInt getFrame() const { return m_frame; }
	// RW 0x625130: the logic is running (the frame advances in phase 1)
	void setFrameAdvance(bool on) { m_frameAdvance = on; }
	// one engine tick: update(phase) of RW 0x62E4E8, phase 1..6
	void update(int phase);
	// the six phases of one logic frame, back to back
	void runLogicFrame();
	int getLastPhase() const { return m_lastPhase; }
	bool isInUpdate() const { return m_inUpdate; }

	// ---- objects ----------------------------------------------------------------------------------------------------------
	// ZH GameLogic::friend_createObject / ThingFactory::newObject (Common/Thing/ThingFactoryObjects.cpp): the whole creation order
	Object *newObject(const ThingTemplate *tt, Team *team, const ObjectStatusMaskType &status, ObjectID id = INVALID_ID);
	Object *findObjectByID(ObjectID id) const;
	Object *getFirstObject() const { return m_objectHead; }
	Object *getLastObject() const { return m_objectTail; }
	size_t getObjectCount() const { return m_objectCount; }
	ObjectID getNextObjectID() const { return m_nextObjectID; }
	ObjectID allocateObjectID() { return m_nextObjectID++; }
	// an object made with an explicit id keeps later allocations above it
	void noteExplicitObjectID(ObjectID id)
	{
		if (id >= m_nextObjectID)
		{
			m_nextObjectID = id + 1;
		}
	}
	// what the object layer reads from a template, parsed once. The cache is keyed by the final-override template pointer: call reset()
	// (or invalidateTemplateInfo) before ThingFactory::reset deletes a map's overrides.
	const ObjectTemplateInfo &templateInfo(const ThingTemplate *finalOverride);
	void invalidateTemplateInfo();
	// lane MODULES-2 (performance): the crush fields of a template (CrushTemplateInfo::of, about 15 field lookups by name) read once per final-override
	// template, under the same lifetime rule as templateInfo
	const CrushTemplateInfo &crushInfo(const ThingTemplate *finalOverride);
	// RW 0x62BA64 addObjectToLookupTable: the object is findable by id from the moment it has its id (Object::setID, RW 0x68BC01, called by the
	// constructor), BEFORE registerObject; registerObject adds it again (RW 0x62BAAF), which changes nothing
	void friend_addObjectToLookup(Object *obj);
	void friend_removeObjectFromLookup(Object *obj);
	// RW 0x62BA8B (see the header comment)
	void registerObject(Object *obj);
	// RW 0x62B921
	void friend_awakenUpdateModule(Object *obj, UpdateModule *u, UnsignedInt when);
	// lane HORDE-2 (the formation swap): `old` leaves the scheduler and `replacement` joins it as registerObject adds a module (RW keeps the swapped module's own entry)
	void friend_replaceUpdateModule(UpdateModule *old, UpdateModule *replacement);

	// ---- destruction (spec 5.5) -------------------------------------------------------------------------------------------
	// ZH GameLogic::destroyObject: queues the object; it is deleted by processDestroyList (phase 5)
	void destroyObject(Object *obj);
	// RW 0x62A2C9
	void processDestroyList();
	size_t destroyQueueSize() const { return m_objectsToDestroy.size(); }
	// deletes every object at once (a new game: ZH GameLogic::reset); the frame and the id counter restart
	void reset();

	// ---- the scheduler's view (tests, reports, the state hash) ----------------------------------------------------------
	const std::vector<UpdateModule *> &updateVector(int phase) const { return m_updates[phase]; }
	const std::vector<UpdateModule *> &sleepingVector() const { return m_sleeping; }
	UpdateModule *currentUpdate() const { return m_current; }
	// RW 0x62BCBF: rebuild the vectors from the object list (what a load does); exposed for the test that the registration loop and
	// the incremental path agree
	void rebuildUpdateVectors();

	// ---- the phase work table -------------------------------------------------------------------------------------------
	struct PhaseWork
	{
		int phase = 0;
		std::string name;
		std::string source;     ///< the RW address(es) the row was read at
		bool builtin = false;   ///< ported here
		std::function<void()> installed; ///< a lane's implementation (empty for a builtin and for an unported row)
		unsigned long long runs = 0;
	};
	const std::vector<PhaseWork> &phaseWork() const { return m_phaseWork; }
	// installs a lane's implementation of a row (the name must be one of phaseWork()); false when there is no such row or it is builtin
	bool installPhaseWork(const std::string &name, std::function<void()> fn);
	// the rows that have neither a builtin nor an installed function: the stop S-143 report
	std::vector<std::string> unportedPhaseWork() const;

	// ---- sendObjectCreated (RW 0x628882) and the OnCreated dispatch (LUA-1) ------------------------------------------------------
	// TARGET (RW 0x628882, the start of Object::initObject, RW 0x693D2A): the ONE function every creation of a full object goes through: (1)
	// GetGameLogicRandomValue(1, 999, "GameLogic.cpp", 0x19A7), unconditional (the drawable's seed); (2) the Drawable is made and bound; (3) the
	// object enters the script world and OnCreated is dispatched. LuaScriptEngine::sendObjectCreated (lane LUA-1) owns that order for the real
	// script engine; it is installed here as the ObjectCreatedProc, which gets a `bindDrawable` closure (the client hook) to run at step (2) and
	// returns the drawn seed. With no proc installed GameLogic makes the draw and the bind itself, counts the object in `creationsWithoutDispatch`
	// and reports the missing OnCreated dispatch (stop S-147). The live game (LiveGame / LiveScripting) installs the proc: ONE creation path, one
	// creation draw per object.
	typedef std::function<int(Object &, const std::function<void()> &bindDrawable)> ObjectCreatedProc;
	void setObjectCreatedProc(ObjectCreatedProc proc) { m_objectCreatedProc = std::move(proc); }
	bool hasObjectCreatedProc() const { return (bool)m_objectCreatedProc; }
	// RW 0x628882: returns the drawn seed
	int sendObjectCreated(Object &obj);
	// TARGET RW 0x68E31F (the call at the end of the Object constructor, RW 0x69A6C6) and RW 0x68C18F (in the destructor, RW 0x69A89D): an object
	// ENTERS the world (the radar, the partition and script-world registration of LUA-1's objectEnteredWorld) and LEAVES it. The same two functions are
	// called from the contain modules when an object is put inside / taken out of a container. The registrations themselves are not ported here
	// (stop S-142); these are the seams the lanes that port them (and LUA-1's LuaScriptEngine::objectEnteredWorld / objectLeftWorld) install.
	// lane HERO-2: the engine's other dispatch sites of the script engine (RW 0x7379CB(slot, object, args): LuaScriptEngine::dispatchInternal; slot i is the
	// built-in event i of RW 0x73449A, e.g. 8 BeScary from SpecialAbilityUpdate's SPECIAL_SCREECH trigger RW 0x854968). The live game (LiveScripting) installs
	// the proc; with none installed the call is counted and reported (stop S-1220), never dropped silently
	typedef std::function<void(int slot, Object &self, const LuaEventArgs &args)> ScriptEventProc;
	void setScriptEventProc(ScriptEventProc proc) { m_scriptEventProc = std::move(proc); }
	void dispatchScriptEvent(int slot, Object &self, const LuaEventArgs &args);
	unsigned long long scriptEventsWithoutDispatch() const { return m_scriptEventsWithoutDispatch; }
	// lane HERO-2: RW 0x663E32, the ModelCondition script events, called first in the AI update (RW 0x66964D) with the snapshot the AI keeps (+ 0x290):
	// before logic frame 2 only the snapshot is taken; afterwards, when the object's flags (+ 0x10C) differ from it, the installed proc dispatches the
	// ModelConditionEvent records the change entered (RW 0x7378DA) and the snapshot takes the flags. The ObjectStatusEvent half (RW 0x737954 with + 0x2DC)
	// is not ported (S-129)
	typedef std::function<void(Object &self, const std::array<std::uint32_t, 19> &before)> ModelConditionEventProc;
	void setModelConditionEventProc(ModelConditionEventProc proc) { m_modelConditionEventProc = std::move(proc); }
	void scriptModelConditionEvents(Object &self, std::array<std::uint32_t, 19> &snapshot);
	typedef std::function<void(Object &)> WorldHook;
	void setWorldHooks(WorldHook entered, WorldHook left)
	{
		m_enteredWorld = std::move(entered);
		m_leftWorld = std::move(left);
	}
	// MOVE-1: further listeners of the same two seams (the pathfinder world); they run after the one setWorldHooks installed, in the order added
	// returns a token for removeWorldHooks: a listener whose owner is destroyed before the logic must remove itself.
	// lane GARRISON-1: `containment` marks a listener that is part of RW 0x68E31F / 0x68C18F themselves (the shroud registration, RW 0xB4D900 / 0xB4E2A0): it also
	// runs when a contain takes an object out of the world and puts it back (friend_containLeaveWorld / friend_containEnterWorld). The other listeners (the
	// pathfinder's adapters) only see the object's creation and its destruction
	int addWorldHooks(WorldHook entered, WorldHook left, bool containment = false)
	{
		const int token = ++m_nextListenerToken;
		m_extraWorldHooks.push_back(ExtraHooks{ token, std::move(entered), std::move(left), containment });
		return token;
	}
	void removeWorldHooks(int token)
	{
		for (size_t i = 0; i < m_extraWorldHooks.size(); ++i)
		{
			if (m_extraWorldHooks[i].token == token)
			{
				m_extraWorldHooks.erase(m_extraWorldHooks.begin() + (long)i);
				return;
			}
		}
	}
	// the Object constructor's world entry (RW 0x69A6C6 -> 0x68E31F) and the destructor's exit (RW 0x69A89D -> 0x68C18F). An object a contain already took out of
	// the world (Object::isInWorld false) does not leave the partition, the script world or the containment listeners a second time; the other listeners still
	// see its destruction
	void friend_objectEnteredWorld(Object &obj);
	void invisibilityNoteCreated(Object &obj); // lane STEALTH-2 (GameLogic.cpp: InvisibilityManager::noteCreated, RW 0x69A6BF just before the world entry)
	void friend_objectLeftWorld(Object &obj);
	// lane GARRISON-1: RW 0x68E31F / 0x68C18F called by a contain module (OpenContain::addOrRemoveObjFromWorld RW 0x865D3D, the horde garrison RW 0x990E5F /
	// 0x990EE6): the partition, the script world and the containment listeners; Object::isInWorld (RW + 0x474) follows. Calling them for an object already in
	// (out of) the world does nothing (the callers test + 0x474 first)
	void friend_containEnterWorld(Object &obj);
	void friend_containLeaveWorld(Object &obj);
	// how many times a contain moved an object into or out of the world (a cache key: TargetFinder rebuilds its grid when it changes within a frame; not game state)
	unsigned containWorldTransitions() const { return m_containWorldTransitions; }
	// lane MODULES-2: ThePartitionManager of this game (GameLogic/Object/PartitionManager.h; RW 0xDE4354): an object enters it with the world (RW 0x68E31F, not an
	// INERT or PROJECTILE template), a transform or team change marks it, the partition row re-links the marked entries before the collisions (RW 0x62E93B)
	PartitionManager &partition() { return *m_partition; }
	const PartitionManager &partition() const { return *m_partition; }
	// lane MODULES-2: the game's side of TheEmotionSystem (GameLogic/System/EmotionSystem.h): the SCARY / HERO object list the fear and hero scans walk
	EmotionWorld &emotions() { return *m_emotions; }
	const EmotionWorld &emotions() const { return *m_emotions; }
	// VIS-1: TheShroudManager of this game (GameLogic/System/ShroudManager.h; RW 0xDE4358): null until a game attaches one (ShroudManager::attach)
	void setShroud(ShroudManager *shroud) { m_shroud = shroud; }
	ShroudManager *shroud() const { return m_shroud; }
	// MOVE-1: the pathfinder / AI world of this game (GameLogic/AI/AIWorld.h; TheAI): null until the game that has one installs it
	void setAIWorld(AIWorld *ai) { m_aiWorld = ai; }
	AIWorld *aiWorld() const { return m_aiWorld; }
	// objects created without the OnCreated dispatch (no ObjectCreatedProc installed): counted and reported (S-147)
	unsigned long long creationsWithoutDispatch() const { return m_creationsWithoutDispatch; }

	// ---- state hash and reports -------------------------------------------------------------------------------------------
	// MOVE-1: logic state that lives outside the objects (the pathfinder's queues) joins the hash through a contributor; they run after the players, in the order added
	typedef std::function<void(StateHasher &)> StateHashContributor;
	// returns a token for removeStateHashContributor (the contributor's owner removes it when it goes first)
	// `name` (lane MP-1) labels the contributor's section of the desync report's per-subsystem breakdown ("" = "contributor N")
	int addStateHashContributor(StateHashContributor c, std::string name = std::string())
	{
		const int token = ++m_nextListenerToken;
		m_hashContributors.push_back(HashContributor{ token, std::move(c), std::move(name) });
		return token;
	}
	void removeStateHashContributor(int token)
	{
		for (size_t i = 0; i < m_hashContributors.size(); ++i)
		{
			if (m_hashContributors[i].token == token)
			{
				m_hashContributors.erase(m_hashContributors.begin() + (long)i);
				return;
			}
		}
	}
	size_t hashContributorCount() const { return m_hashContributors.size(); }
	size_t extraWorldHookCount() const { return m_extraWorldHooks.size(); }
	// the deterministic world state hash (Common/StateHash.h): frame, id counter, RNG state, scheduler vectors, objects in list order,
	// players and teams. O(objects + modules).
	std::uint32_t computeStateHash() const;
	// lane MP-1: the same hash cut into its sections (desync reports name the diverging subsystem): every section hashed on its own, in the order
	// computeStateHash feeds them; `objects` (optional) gets every object's own hash in list order. `total` is computeStateHash().
	struct StateHashSection
	{
		std::string name;
		std::uint32_t value = 0;       ///< the section hashed on its own (from 0)
		// the total's running value before and after this section was fed: consecutive sections chain (before = the previous after, the first starts at 0,
		// the last ends at the total), so the sections cover every word of the total (local diagnostics; not exchanged)
		std::uint32_t chainBefore = 0, chainAfter = 0;
	};
	struct ObjectStateHash
	{
		ObjectID id = 0;
		std::string templateName;
		std::uint32_t value = 0;
	};
	std::uint32_t computeStateHashBreakdown(std::vector<StateHashSection> &sections, std::vector<ObjectStateHash> *objects) const;

	struct UnportedClassStat
	{
		size_t modules = 0;  ///< live module instances of the class
		size_t objects = 0;  ///< live objects that have at least one
		int interfaceMask = 0;
	};
	struct Report
	{
		UnsignedInt frame = 0;
		size_t objects = 0;
		size_t destroyed = 0;                                 ///< objects deleted by processDestroyList so far
		size_t updateModules = 0, sleepingModules = 0;
		std::map<std::string, UnportedClassStat> unportedModules; ///< live, by class (stop S-140)
		std::map<std::string, size_t> unportedCreated;            ///< ever made, by class
		std::map<std::string, size_t> helperShells;               ///< live helper shells by name (stop S-141)
		unsigned long long unportedUpdateCalls = 0;
		unsigned long long creationsWithoutDispatch = 0;
		std::vector<std::string> errors;                      ///< problems nothing explains (a bad KindOf line, ...), never dropped
		std::vector<std::string> stops;                       ///< the stop lines that apply to this run
	};
	Report report() const;

private:
	friend class Object;
	void linkObject(Object *obj);
	void unlinkObject(Object *obj);
	// the rows of the phase work table, in table order (buildPhaseWorkTable keeps the same order)
	enum Row
	{
		ROW_LOGIC_DEBUG_FRAME, ROW_FREEZE_GATE, ROW_FRAME_ADVANCE, ROW_COMMAND_ONLY, ROW_FRAME2_OBJECT_BLOCK, ROW_PATHFINDER_QUEUE, ROW_SUBSYSTEMS_PHASE1,
		ROW_LOGIC_CRC, ROW_RECORDER_STATS, ROW_COMMAND_LIST, ROW_DRAWABLE_CALLBACK, ROW_PARTITION_COLLISION, ROW_RECORD_TRANSFORMS, ROW_SCHEDULER, ROW_AI_UPDATE,
		ROW_DEFERRED_DRAIN, ROW_SUBSYSTEMS_BEFORE_DESTROY, ROW_PROCESS_DESTROY_LIST, ROW_SUBSYSTEMS_AFTER_DESTROY, ROW_END_OF_FRAME_CHECKS, ROW_PENDING_DAMAGE,
		ROW_LOGIC_TIME_NOTIFY, ROW_COUNT
	};
	void runRow(Row row)
	{
		PhaseWork &w = m_phaseWork[(size_t)row];
		if (w.installed)
		{
			w.installed();
			++w.runs;
		}
	}
	void runModules(int phase);
	void migrateForeverModules(int vectorIndex);
	void endOfFrameObjectChecks();
	void buildPhaseWorkTable();
	void removeUpdateModule(UpdateModule *u);

	ThingFactory &m_things;
	ModuleFactory &m_modules;
	PlayerList &m_players;
	GameLogicRandom m_random;
	GameLogicSettings m_settings;
	std::unique_ptr<Economy> m_economy;
	std::unique_ptr<CombatState> m_combat;
	FXEventLog m_fxEvents; // lane FX-2 (not hashed)
	LargeGroupAudioLink m_largeGroupAudio; // lane AUDIO-4
	std::unique_ptr<VictoryConditions> m_victory;
	std::unique_ptr<SkirmishAIManager> m_skirmishAI;
	std::unique_ptr<ExperienceWorld> m_experience; // lane XP-1
	std::unique_ptr<CreateAHeroGame> m_createAHeroes; // lane HERO-2
	std::unique_ptr<GlobalWeatherSystem> m_weather; // lane SPELL-2
	std::unique_ptr<InvisibilityManager> m_invisibility; // lane STEALTH-1
	std::unique_ptr<ScriptEngine> m_scriptEngine;        // lane SCRIPT-1
	CastleTemplateStore m_castleTemplates;
	std::set<std::string> m_notedStops;
	std::map<std::string, unsigned> m_notedStopHits;
	ObjectClientHooks *m_clientHooks = nullptr;
	ObjectCreatedProc m_objectCreatedProc;
	ScriptEventProc m_scriptEventProc;
	ModelConditionEventProc m_modelConditionEventProc;
	unsigned long long m_scriptEventsWithoutDispatch = 0;
	WorldHook m_enteredWorld, m_leftWorld;
	struct ExtraHooks
	{
		int token;
		WorldHook entered, left;
		bool containment = false;
	};
	std::uint32_t hashState(std::vector<StateHashSection> *sections, std::vector<ObjectStateHash> *objects) const; // lane MP-1
	struct HashContributor
	{
		int token;
		StateHashContributor fn;
		std::string name;
	};
	std::vector<ExtraHooks> m_extraWorldHooks;
	std::vector<HashContributor> m_hashContributors;
	int m_nextListenerToken = 0;
	AIWorld *m_aiWorld = nullptr;
	ShroudManager *m_shroud = nullptr;
	std::unique_ptr<PartitionManager> m_partition; // lane MODULES-2
	std::unique_ptr<EmotionWorld> m_emotions;      // lane MODULES-2
	int m_inertBit = -1, m_projectileBit = -1;     // the KindOf bits RW 0x68E31F tests (template + 0x110 bit 25, + 0x108 bit 25)
	void partitionEnter(Object &obj);
	void partitionLeave(Object &obj);
	unsigned long long m_creationsWithoutDispatch = 0;

	UnsignedInt m_frame = 0;
	int m_gameDifficulty = 1;     ///< lane CAMP-1H: RW GameLogic + 0xA4
	bool m_frameAdvance = true;   ///< RW GameLogic + 0x44
	bool m_inUpdate = false;      ///< RW + 0x70
	int m_lastPhase = 0;          ///< RW + 0x17C
	int m_depth = 0;              ///< RW + 0x1B4 (the reentrancy counter)

	Object *m_objectHead = nullptr, *m_objectTail = nullptr;
	size_t m_objectCount = 0;
	unsigned m_containWorldTransitions = 0;
	ObjectID m_nextObjectID = 1;
	std::vector<Object *> m_objectsById; ///< index = id; null for a free or destroyed id

	std::vector<UpdateModule *> m_updates[PHASE_COUNT];
	std::vector<UpdateModule *> m_sleeping;
	UpdateModule *m_current = nullptr;

	std::vector<Object *> m_objectsToDestroy;
	size_t m_destroyedTotal = 0;

	std::vector<PhaseWork> m_phaseWork;
	std::map<const ThingTemplate *, ObjectTemplateInfo> m_templateInfo; ///< looked up only, never iterated
	std::map<const ThingTemplate *, std::unique_ptr<CrushTemplateInfo>> m_crushInfo; ///< looked up only, never iterated
	std::vector<std::string> m_errors;
	const TerrainLogic *m_terrain = nullptr;
	AICommandSink m_aiCommands;
	ObjectID m_pendingProducer = INVALID_ID;
	ProductionSettings m_productionSettings;
	const UpgradeTypeTable *m_upgradeTypes = nullptr;
};
