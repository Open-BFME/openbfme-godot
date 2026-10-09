// OpenBFME. GPL-3.0.
//
// The draw runtime of a map's drawables (lane MAPOBJ-1): for every drawable the object loop made (MapObjectDrawables.h), one entry per
// draw module that shows a model, with the merged W3DScriptedModelDraw / W3DHordeModelDraw state machine (DRAW-1) behind the model
// draw modules, so the model and the idle animation are the ones retail's draw module selects for the object's model condition flags.
// No Godot: the device layer (GodotDevice/GodotMapObjectBuilder) turns the entries into instances.
//
// Sources: the draw module runtime is W3DScriptedModelDraw.h (TARGET RW 0x4BF2D8 and following, see there). What this file adds:
//   * the horde draw module uses the HIGH row of its LodOptions (the dynamic game LOD is a GameLOD setting this lane does not read;
//     RW 0xDE3B84 +0x1788, stop S-093 / S-114);
//   * bones are not built (turrets, barrels, particle bones are not drawn: S-097 / S-095);
//   * the Lua BeginScript bodies run on Lua 4.0.1 (EA's fork, lane LUA-1) in the drawable state, through W3DLuaDrawScriptHost (what its api
//     cannot answer is the reported stop S-126, what remains of S-091);
//   * every full object, bridge and horde member goes through LuaScriptEngine::sendObjectCreated (RW 0x628882): the unconditional logic draw
//     GetGameLogicRandomValue(1, 999, GameLogic.cpp, 0x19A7) first (hook or not; the generator and its seed are S-080), then the drawable's draw modules;
//   * the OnCreated creation hooks run: after a full object's (or horde member's) draw modules exist, the object enters the script engine's
//     world (LuaScriptEngine::objectEnteredWorld, retail RW 0x68E31F) and receives OnCreated (dispatchInternal, retail RW 0x628882 / 0x6288FA:
//     sendObjectCreated creates and binds the Drawable first, then dispatches), so the handlers of its AILuaEventsList hide or show sub objects
//     of its draw modules (RangerFunctions -> OnGondorArcherCreated hides FireArowTip). The hide / show reaches the drawable through
//     LuaGameHost::drawableShowSubObject / drawableShowModule; every other engine callee a handler needs is the reported stop S-124 (stop S-110);
//   * the client random is the donor carry chain generator named explicitly (stop S-093).
// Entries whose draw runtime cannot start (a model that is not registered, a state machine that throws) are reported in `errors`,
// never dropped silently.

#pragma once

#include "GameClient/MapObjectDrawables.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLuaDrawScriptHost.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameLogic/ScriptEngine/LuaScriptEngine.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <memory>
#include <string>
#include <vector>

struct MapPlacedModel
{
	size_t drawable = 0;             ///< index in MapObjectDrawables::drawables
	size_t module = 0;               ///< index in that drawable's draws
	W3DDrawKind kind = W3D_DRAWKIND_MODEL;
	std::string className;
	std::string modelName;           ///< the model of the state (case as the INI wrote it)
	std::string textureName;         ///< W3DTreeDraw TextureName (not applied: S-111)
	std::unique_ptr<W3DScriptedModelDraw> draw; ///< MODEL kind only
	bool animated = false;           ///< the idle animation moves (more than one frame, not MANUAL): advance() every render frame
	bool hordeMember = false;
	bool moduleHidden = false;       ///< a CurDrawableHideModule / ObjectHideModule request of a script hid this module (Drawable::showModule, RW 0x6789B4): not instanced
	bool missingModel = false;       ///< the model is not registered in the mounted archives (a retail data defect, counted in dataDefects): not instanced
};

struct MapRuntimeReport
{
	size_t placed = 0;               ///< entries
	size_t modelDraws = 0, animated = 0, staticModels = 0;
	size_t treeDraws = 0, propDraws = 0, floorDraws = 0;
	std::map<std::string, size_t> distinctModels; ///< model name (lower case) -> entries
	std::vector<std::string> errors; ///< runtime / model problems nothing explains, one line each
	// Problems the retail data itself has, found by running the draw runtime over the corpus: an animation or model an INI names that no
	// mounted archive holds (retail's lookup fails the same way), and hide / show requests naming a sub object the model does not have
	// (retail's Get_Sub_Object_By_Name returns NULL for those and nothing happens; ZH doHideShowSubObjs). Counted per message / name.
	std::map<std::string, size_t> dataDefects;  ///< "animation X of state S ... is not registered" / "model X is not available ..." -> entries
	std::map<std::string, size_t> hideMisses;   ///< lower-cased name -> requests that matched no sub object (some name a draw module tag: HideSubObject("ModuleTag_DrawFloor"); no retail object has such a module hidden at map start)
	std::map<std::string, size_t> ignoredDrawFields; ///< "W3DFloorDraw WeatherTexture" ... -> placed models whose static draw module sets a field that is stored but not applied (S-115)
	std::vector<std::string> stops;  ///< stop lines of the draw runtime that were hit (first occurrence of each), and the reports of the Lua script engine and the Lua draw host ("[S-124] ...")
	size_t scriptsRun = 0;           ///< BeginScript bodies the Lua draw host ran (each state entry counts)
	// OnCreated creation hooks (S-110)
	size_t creationListObjects = 0;  ///< full objects / horde members whose AILuaEventsList the real event registry resolved: they entered the script world and were sent OnCreated
	size_t creationHookObjects = 0;  ///< of those, the ones whose list has an OnCreated handler (the handler runs; a handler naming a missing global is retail's silent failure, an alert in the logic state)
	std::map<std::string, size_t> creationHandlers; ///< OnCreated handler function that ran -> objects (the child list's own handler: a child replaces an inherited one, RW 0x733FAF)
	size_t creationHookTemplatesWithSeveralLists = 0; ///< objects whose template names AILuaEventsList in more than one AI module (the first list is used: one AI per object, S-110)
	bool creationScriptsGiven = false; ///< setCreationScripts was called with the raw script files (the report-only inventory scan is not required)
	size_t moduleRequests = 0;       ///< Hide / ShowModule requests of the creation handlers that matched a draw module tag (the module takes the request: no fall through to a sub object)
	size_t modulesHidden = 0;        ///< placed models whose module a script hid: not instanced
	size_t moduleRequestsWithoutModel = 0; ///< matched module requests whose module shows no model here: nothing to change, reported as S-124
	size_t creationBridgePassObjects = 0;  ///< of the creation draws, the objects of retail's earlier bridge / wall pass (IsBridge or KindOf WALK_ON_TOP_OF_WALL)
	size_t creationOrderDeviations = 0;    ///< bridge / wall pass objects that come AFTER a normal full object here: retail creates (draws, OnCreated) them first; reported as S-110
	size_t creationDraws = 0;        ///< GetGameLogicRandomValue(1, 999) draws of RW 0x628892: one per full object / bridge / horde member, with or without a creation hook (S-080 is the stop of the generator)
	double buildSeconds = 0.0;
};

// How a message of the draw runtime (W3DScriptedModelDraw::errors()) is counted: "sub object X not found in model Y" is a hide / show of a name the
// model has no sub object of (retail's Get_Sub_Object_By_Name returns NULL and nothing happens), "animation ... is not registered" and "model ... is
// not available" name assets no mounted archive holds (retail's lookup fails the same way), and retail's "W3DScriptedModelDraw::adjustAnimation: Unable to
// find transition state named ..." (a script asks for a state the module lacks; retail logs it and goes on, lane FX-3) is a data defect too; everything
// else is an error. One function for the static
// map runtime and LOGIC-1's live drawables.
struct DrawMessageClass
{
	enum Kind { HIDE_MISS, DATA_DEFECT, ERROR } kind = ERROR;
	std::string subObject; ///< HIDE_MISS: the lower-cased sub object name
};
DrawMessageClass classifyDrawMessage(const std::string &message);

class MapObjectRuntime
{
public:
	MapObjectRuntime(WW3DAssetManager &assets);
	~MapObjectRuntime();
	MapObjectRuntime(const MapObjectRuntime &) = delete;

	// Builds one entry per drawing draw module of `drawables`. The drawables (and their template infos) must outlive the runtime.
	void build(const MapObjectDrawables &drawables, bool createDrawRuntime = true);
	// The scriptevents.xml / scripts.lua text the creation hooks run from (MapCreationHooks::load). Without it no hook runs and the report says so.
	// Must be set before build(); the data must outlive build().
	void setCreationScripts(const CreationScriptData *scripts) { m_scripts = scripts; }
	LuaScriptEngine *scriptEngine() { return m_lua.get(); }
	// The logic generator the creation draws and the script bindings use: this runtime's own ZH carry chain unless the owner supplies the game's
	// (LOGIC-1 passes TheGameLogic's); must be set before build().
	void setLogicRandom(GameLogicRandom *rng) { m_logicRandomPtr = rng ? rng : &m_logicRandom; }
	GameLogicRandom &logicRandom() { return *m_logicRandomPtr; }

	std::vector<MapPlacedModel> &models() { return m_models; }
	const MapRuntimeReport &report() const { return m_report; }
	W3DDrawRandom &random() { return m_random; }

private:
	void classifyRuntimeError(const std::string &object, const std::string &className, const std::string &message);
	void createDrawModules(const MapObjectDrawables &drawables, size_t drawableIndex, bool createDrawRuntime);

	WW3DAssetManager &m_assets;
	WW3DDrawAssets m_drawAssets;
	W3DClientRandom m_random;
	W3DLuaDrawScriptHost m_host;
	const CreationScriptData *m_scripts = nullptr;
	// the script engine of the creation hooks: its own name keys and logic random (the hooks draw none), the map objects as its object table
	NameKeyGenerator m_keys;
	GameLogicRandom m_logicRandom;
	GameLogicRandom *m_logicRandomPtr = &m_logicRandom;
	class LuaMapHost;
	std::unique_ptr<LuaMapHost> m_luaHost;
	std::unique_ptr<LuaScriptEngine> m_lua;
	std::vector<MapPlacedModel> m_models;
	MapRuntimeReport m_report;
};
