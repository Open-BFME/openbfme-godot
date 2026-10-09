// OpenBFME. GPL-3.0.
// See GameLogic/GameLogic.h for the sources of every rule.

#include "GameLogic/GameLogic.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/NotifyCrushModules.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/BuildStops.h"
#include "GameLogic/StructureStops.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Common/NumericState.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/ObjectHelper.h"
#include "GameLogic/Module/SquishCollide.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Economy.h"
#include "GameLogic/ExperienceWorld.h"
#include "GameLogic/CreateAHeroSystem.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/InvisibilityManager.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/Module/AreaScanModules.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/HitReactionBehavior.h"
#include "GameLogic/System/EmotionSystem.h"

#include <algorithm>
#include <stdexcept>

GameLogic::GameLogic(ThingFactory &things, ModuleFactory &modules, PlayerList &players, RandomAlgorithm rng)
	: m_things(things)
	, m_modules(modules)
	, m_players(players)
	, m_random(rng)
	, m_economy(std::make_unique<Economy>(*this))
	, m_combat(std::make_unique<CombatState>(*this))
	, m_victory(std::make_unique<VictoryConditions>(*this))
	, m_skirmishAI(std::make_unique<SkirmishAIManager>(*this))
	, m_experience(std::make_unique<ExperienceWorld>(*this))
	, m_createAHeroes(std::make_unique<CreateAHeroGame>(*this))
	, m_weather(std::make_unique<GlobalWeatherSystem>(*this))
	, m_invisibility(std::make_unique<InvisibilityManager>(*this))
	, m_scriptEngine(std::make_unique<ScriptEngine>(*this)) // lane SCRIPT-1
{
	buildPhaseWorkTable();
	m_partition = std::make_unique<PartitionManager>(*this);
	m_emotions = std::make_unique<EmotionWorld>(*this);
	m_inertBit = ObjectTemplateInfoBuilder::kindOfIndex("INERT");
	m_projectileBit = ObjectTemplateInfoBuilder::kindOfIndex("PROJECTILE");
}

// lane MODULES-2: RW 0x68E31F registers the object with ThePartitionManager (RW 0x68E3B6 -> 0xA39050) unless its template's KindOf has INERT (+ 0x110 bit 25) or
// PROJECTILE (+ 0x108 bit 25); RW 0x68C18F unregisters it (RW 0x68C1B4 -> 0xA39060) when it has an entry
void GameLogic::partitionEnter(Object &obj)
{
	if ((m_inertBit >= 0 && obj.isKindOf((unsigned)m_inertBit)) || (m_projectileBit >= 0 && obj.isKindOf((unsigned)m_projectileBit)))
	{
		return;
	}
	m_partition->registerObject(obj);
}

void GameLogic::partitionLeave(Object &obj)
{
	m_partition->unRegisterObject(obj);
}

void GameLogic::friend_objectEnteredWorld(Object &obj)
{
	invisibilityNoteCreated(obj); // lane STEALTH-2: RW 0x69A6BF, just before the world entry RW 0x69A6C6
	partitionEnter(obj); // lane MODULES-2: RW 0x68E3B6, before the script world (RW 0x68E3E2)
	if (m_enteredWorld)
	{
		m_enteredWorld(obj);
	}
	for (auto &h : m_extraWorldHooks)
	{
		if (h.entered)
		{
			h.entered(obj);
		}
	}
	obj.friend_setInWorld(true); // RW 0x68E3FB: + 0x474 = 1
}

void GameLogic::friend_objectLeftWorld(Object &obj)
{
	const bool inWorld = obj.isInWorld();
	if (inWorld)
	{
		partitionLeave(obj); // lane MODULES-2: RW 0x68C1B4
		if (m_leftWorld)
		{
			m_leftWorld(obj);
		}
	}
	for (auto &h : m_extraWorldHooks)
	{
		if (h.left && (inWorld || !h.containment))
		{
			h.left(obj);
		}
	}
	obj.friend_setInWorld(false); // RW 0x68C1E1: + 0x474 = 0
}

// lane GARRISON-1: RW 0x68E31F from a contain (the order of the ported parts: the shroud record RW 0x68E352, the partition RW 0x68E3B6, the script world RW 0x68E3E2;
// the radar RW 0xB6C470 and RW 0x688354 / 0x6A9D9C are not ported, S-142)
void GameLogic::friend_containEnterWorld(Object &obj)
{
	if (obj.isInWorld())
	{
		return;
	}
	for (auto &h : m_extraWorldHooks)
	{
		if (h.entered && h.containment)
		{
			h.entered(obj);
		}
	}
	partitionEnter(obj);
	if (m_enteredWorld)
	{
		m_enteredWorld(obj);
	}
	obj.friend_setInWorld(true);
	++m_containWorldTransitions;
}

// lane GARRISON-1: RW 0x68C18F from a contain (the shroud record RW 0x68C19B, the partition RW 0x68C1B4, the script world RW 0x68C1D0; the radar RW 0xB6C480 and
// RW 0x68836C / 0x6A99CB are not ported, S-142)
void GameLogic::friend_containLeaveWorld(Object &obj)
{
	if (!obj.isInWorld())
	{
		return;
	}
	for (auto &h : m_extraWorldHooks)
	{
		if (h.left && h.containment)
		{
			h.left(obj);
		}
	}
	partitionLeave(obj);
	if (m_leftWorld)
	{
		m_leftWorld(obj);
	}
	obj.friend_setInWorld(false);
	++m_containWorldTransitions;
}

GameLogic::~GameLogic()
{
	reset();
}

void GameLogic::invalidateTemplateInfo()
{
	m_templateInfo.clear();
	m_crushInfo.clear();
}

const CrushTemplateInfo &GameLogic::crushInfo(const ThingTemplate *finalOverride)
{
	auto it = m_crushInfo.find(finalOverride);
	if (it == m_crushInfo.end())
	{
		it = m_crushInfo.emplace(finalOverride, std::make_unique<CrushTemplateInfo>(CrushTemplateInfo::of(*finalOverride))).first;
	}
	return *it->second;
}

const ObjectTemplateInfo &GameLogic::templateInfo(const ThingTemplate *finalOverride)
{
	auto it = m_templateInfo.find(finalOverride);
	if (it == m_templateInfo.end())
	{
		ObjectTemplateInfo info = ObjectTemplateInfoBuilder::build(*finalOverride);
		if (!info.kindOfError.empty())
		{
			m_errors.push_back("template " + finalOverride->getName() + ": " + info.kindOfError);
		}
		it = m_templateInfo.emplace(finalOverride, std::move(info)).first;
	}
	return it->second;
}

Object *GameLogic::newObject(const ThingTemplate *tt, Team *team, const ObjectStatusMaskType &status, ObjectID id)
{
	return m_things.newObject(*this, tt, team, status, id);
}

float GameLogic::getGroundHeight(float x, float y) const
{
	return m_terrain ? m_terrain->getGroundHeight(x, y, nullptr) : 0.0f;
}

void GameLogic::reportError(const std::string &text)
{
	if (std::find(m_errors.begin(), m_errors.end(), text) == m_errors.end())
	{
		m_errors.push_back(text);
	}
}

// ---- the object list --------------------------------------------------------------------------------------------------
void GameLogic::linkObject(Object *obj)
{
	// B1 GameLogicRegisterObject.cpp: bfmeInsertXN(&objList, &objTail): append at the tail
	obj->friend_setListLinks(m_objectTail, nullptr);
	if (m_objectTail)
	{
		m_objectTail->friend_setNext(obj);
	}
	else
	{
		m_objectHead = obj;
	}
	m_objectTail = obj;
	++m_objectCount;
}

void GameLogic::unlinkObject(Object *obj)
{
	Object *prev = obj->getPrevObject(), *next = obj->getNextObject();
	if (prev)
	{
		prev->friend_setNext(next);
	}
	else
	{
		m_objectHead = next;
	}
	if (next)
	{
		next->friend_setPrev(prev);
	}
	else
	{
		m_objectTail = prev;
	}
	obj->friend_setListLinks(nullptr, nullptr);
	--m_objectCount;
}

Object *GameLogic::findObjectByID(ObjectID id) const
{
	return id < m_objectsById.size() ? m_objectsById[id] : nullptr;
}

void GameLogic::friend_addObjectToLookup(Object *obj)
{
	if (obj->getID() >= m_objectsById.size())
	{
		m_objectsById.resize((size_t)obj->getID() + 1 + (m_objectsById.size() >> 1), nullptr);
	}
	if (m_objectsById[obj->getID()] != nullptr && m_objectsById[obj->getID()] != obj)
	{
		throw std::logic_error("GameLogic: object id " + std::to_string(obj->getID()) + " is already in use");
	}
	m_objectsById[obj->getID()] = obj;
}

void GameLogic::friend_removeObjectFromLookup(Object *obj)
{
	if (obj->getID() < m_objectsById.size() && m_objectsById[obj->getID()] == obj)
	{
		m_objectsById[obj->getID()] = nullptr;
	}
}

// RW 0x62BA8B: the list append (0x68BB6A), the id table (0x62BA64), then the module loop (RW 0x62BD6A .. 0x62BE0E)
void GameLogic::registerObject(Object *obj)
{
	linkObject(obj);
	friend_addObjectToLookup(obj);
	UnsignedInt now = m_frame;
	if (now == 0)
	{
		now = 1;
	}
	for (const std::unique_ptr<BehaviorModule> &b : obj->modules())
	{
		UpdateModule *u = b->asUpdateModule();
		if (!u)
		{
			continue;
		}
		if (u->friend_getNextCallFrame() == 0)
		{
			u->friend_setNextCallFrame(now);
		}
		if (u->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER)
		{
			u->friend_setIndexInLogic((int)m_sleeping.size(), -1);
			m_sleeping.push_back(u);
		}
		else
		{
			const int phase = (int)u->getUpdatePhase();
			std::vector<UpdateModule *> &vec = m_updates[phase];
			u->friend_setIndexInLogic((int)vec.size(), phase);
			vec.push_back(u);
		}
	}
}

// RW 0x62BCBF: the same loop over the whole object list after clearing the vectors
void GameLogic::rebuildUpdateVectors()
{
	for (std::vector<UpdateModule *> &vec : m_updates)
	{
		for (UpdateModule *u : vec)
		{
			u->friend_setIndexInLogic(-1);
		}
		vec.clear();
	}
	for (UpdateModule *u : m_sleeping)
	{
		u->friend_setIndexInLogic(-1);
	}
	m_sleeping.clear();
	UnsignedInt now = m_frame == 0 ? 1 : m_frame;
	for (Object *o = m_objectHead; o; o = o->getNextObject())
	{
		for (const std::unique_ptr<BehaviorModule> &b : o->modules())
		{
			UpdateModule *u = b->asUpdateModule();
			if (!u)
			{
				continue;
			}
			if (u->friend_getNextCallFrame() == 0)
			{
				u->friend_setNextCallFrame(now);
			}
			if (u->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER)
			{
				u->friend_setIndexInLogic((int)m_sleeping.size(), -1);
				m_sleeping.push_back(u);
			}
			else
			{
				const int phase = (int)u->getUpdatePhase();
				u->friend_setIndexInLogic((int)m_updates[phase].size(), phase);
				m_updates[phase].push_back(u);
			}
		}
	}
}

// RW 0x62B921
void GameLogic::friend_awakenUpdateModule(Object *obj, UpdateModule *u, UnsignedInt when)
{
	const UnsignedInt now = m_frame;
	if (u == m_current)
	{
		return;
	}
	if (when == u->friend_getNextCallFrame())
	{
		return;
	}
	if (now > 0 && u->friend_getNextCallFrame() == now && when == now + 1)
	{
		return;
	}
	const int idx = u->friend_getIndexInLogic();
	const int phase = u->friend_getPhaseInLogic();
	if (obj->isInList(&m_objectHead))
	{
		if (phase < 0 && when < (UnsignedInt)UPDATE_SLEEP_FOREVER)
		{
			// the module sits in `sleeping` (a negative phase): swap-remove it and append it to its phase vector
			const int newPhase = (int)u->getUpdatePhase();
			if (idx < (int)m_sleeping.size() - 1)
			{
				m_sleeping[(size_t)idx] = m_sleeping.back();
				m_sleeping[(size_t)idx]->friend_setIndexInLogic(idx);
			}
			m_sleeping.pop_back();
			std::vector<UpdateModule *> &vec = m_updates[newPhase];
			const int newIndex = (int)vec.size();
			vec.push_back(u);
			u->friend_setIndexInLogic(newIndex, newPhase);
		}
		u->friend_setNextCallFrame(when);
	}
	else
	{
		if (idx != -1)
		{
			throw std::logic_error("fatal error! sleepy update module index mismatch."); // RW 0x62BA0E .. 0x62BA4B
		}
		// object initialization may wake a module before list insertion
		u->friend_setNextCallFrame(when);
	}
}

// ---- the logic frame ----------------------------------------------------------------------------------------------------
void GameLogic::buildPhaseWorkTable()
{
	struct Row
	{
		int phase;
		const char *name;
		const char *source;
		bool builtin;
	};
	// phase 0 = every phase. RW order inside a phase (RW 0x62E4E8 .. 0x62EE6A).
	static const Row rows[] = {
		{ 1, "logicDebugFrame", "RW 0x62E515 (-> 0x604152): the script engine's debug-frame helper", false },
		{ 1, "freezeGate", "RW 0x62E520 .. 0x62E552 and 0x62E57E .. 0x62E595: tactical view / script engine freeze, MSG_CLEAR_GAME_DATA", false },
		{ 1, "frameAdvance", "RW 0x62E568 .. 0x62E577: ++frame when RW 0x625130 (advance flag set, +0xA8 and +0x9D clear) and not frozen", true },
		{ 1, "commandOnlyTransition", "RW 0x62E5AC .. 0x62E5D8: GameLogic + 0x125 set: processCommandList only (mission save load)", false },
		{ 1, "frame2ObjectBlock", "RW 0x62E640 .. 0x62E697: at frame 2, path destruction of objects whose AI asks (several global predicates)", false },
		{ 0, "pathfinderQueue", "RW 0x62E69F (-> 0x6F2364): Pathfinder::processPathfindQueue on TheAI + 0x10, then setFPMode (0x440809); every phase", false },
		{ 1, "subsystemsPhase1", "RW 0x62E6AF .. 0x62E6DD: script engine, Lua script engine, terrain logic and victory system updates (vslot 0x28)", false },
		{ 1, "logicCrc", "RW 0x62E6E2 .. 0x62E880: the MSG_LOGIC_CRC message every CRCInterval frames (not in game modes 4, 7, 9)", false },
		{ 1, "recorderAndStats", "RW 0x62E885 .. 0x62E8CF: statistics collector, recorder, two more subsystems, and GameLogic + 0x174 (+ 0x178, the InvisibilityManager, runs after the row: lane STEALTH-1)", false },
		{ 1, "commandList", "RW 0x62E8D4 .. 0x62E905: every pending command goes to the command dispatcher (RW 0x779A3D), then the list resets", false },
		{ 1, "drawableCallback", "RW 0x62E908 .. 0x62E933: per object, getDrawable()->callback(0)", false },
		{ 2, "partitionAndCollision", "RW 0x62E93B .. 0x62E94E: the partition manager and the collision manager update (vslot 0x28)", false },
		{ 2, "recordTransforms", "RW 0x62E951 .. 0x62E97E: every object whose recorded frame is not the frame records its transform (RW 0x6260E1)", true },
		{ 0, "scheduler", "RW 0x62E982 .. 0x62EB63: the sleepy update vectors (phases 3 .. 6)", true },
		{ 5, "aiUpdate", "RW 0x62EB63 .. 0x62EB71: TheAI vslot 0x28", false },
		{ 0, "deferredEntriesDrain", "RW 0x62EB74 (-> 0x629DA6): deletes the entries appended to GameLogic + 0x164 during the phase", false },
		{ 5, "subsystemsBeforeDestroy", "RW 0x62EB85 .. 0x62EBAE: four subsystem updates (vslot 0x28) before the destroy list", false },
		{ 5, "processDestroyList", "RW 0x62EBB1 (-> 0x62A2C9)", true },
		{ 5, "subsystemsAfterDestroy", "RW 0x62EBB8 .. 0x62EC0B: eight more subsystem updates (a group audio, weapon / locomotor stores, victory conditions, the experience level system, ...)", false },
		{ 1, "endOfFrameObjectChecks", "RW 0x62EC55 .. 0x62ECAA: per object disabled expiry (0x690A42), IGNORE_AI_COMMAND (0x690AB9), NO_COLLISIONS (0x690AE5)", true },
		{ 1, "updatePendingDamage", "RW 0x62EC9D (-> 0x697EB6): per object, the queued damage whose delay passed is applied (lane COMBAT-1)", true },
		{ 1, "logicTimeNotify", "RW 0x62ECC3 .. 0x62EE39: frame * 10 to a client subsystem and the mission save thumbnail", false },
	};
	static_assert(sizeof(rows) / sizeof(rows[0]) == (size_t)ROW_COUNT, "the phase work rows and the Row enum must agree");
	for (const Row &r : rows)
	{
		PhaseWork w;
		w.phase = r.phase;
		w.name = r.name;
		w.source = r.source;
		w.builtin = r.builtin;
		m_phaseWork.push_back(std::move(w));
	}
}

bool GameLogic::installPhaseWork(const std::string &name, std::function<void()> fn)
{
	for (PhaseWork &w : m_phaseWork)
	{
		if (w.name == name)
		{
			if (w.builtin)
			{
				return false;
			}
			w.installed = std::move(fn);
			return true;
		}
	}
	return false;
}

std::vector<std::string> GameLogic::unportedPhaseWork() const
{
	std::vector<std::string> out;
	for (const PhaseWork &w : m_phaseWork)
	{
		if (!w.builtin && !w.installed)
		{
			out.push_back(std::string("phase ") + (w.phase ? std::to_string(w.phase) : std::string("all")) + " " + w.name + ": " + w.source);
		}
	}
	return out;
}

void GameLogic::runLogicFrame()
{
	for (int phase = 1; phase <= 6; ++phase)
	{
		update(phase);
	}
}

// lane STEALTH-2: RW 0x69A6BF (see InvisibilityManager::noteCreated)
void GameLogic::invisibilityNoteCreated(Object &obj)
{
	m_invisibility->noteCreated(obj);
}

// RW 0x62E4E8 (B1 GameLogic.cpp:8264-8392)
void GameLogic::update(int phase)
{
	if (phase < 1 || phase > 6)
	{
		throw std::logic_error("GameLogic::update: phase must be 1..6");
	}
	if (m_depth == 0)
	{
		// RW + 0x1B4 == 0: the outermost update. Retail runs setFPMode here (RW 0x440809); the port establishes the complete canonical floating-point
		// environment (nearest rounding, masked exceptions, FTZ / DAZ off: MXCSR 0x1F80) so that nothing a driver, a plug-in or the host thread did to
		// it reaches the simulation (lockstep: NumericState::normalizeFloatingPointEnvironment).
		NumericState::setFPMode();
	}
	++m_depth;
	if (phase == 1)
	{
		runRow(ROW_LOGIC_DEBUG_FRAME);
		runRow(ROW_FREEZE_GATE);
		// RW 0x62E568: the frame advances at the START of phase 1 (RW DIFFERS from B1, which increments at the end)
		if (m_frameAdvance)
		{
			++m_frame;
		}
		m_fxEvents.beginFrame(); // lane FX-2: the event list holds the current frame's calls only
		runRow(ROW_COMMAND_ONLY);
	}
	m_lastPhase = phase;
	const bool wasInUpdate = m_inUpdate;
	m_inUpdate = true;
	if (phase == 1)
	{
		runRow(ROW_FRAME2_OBJECT_BLOCK);
	}
	runRow(ROW_PATHFINDER_QUEUE);
	if (phase == 1)
	{
		m_scriptEngine->update(); // lane SCRIPT-1: RW 0x62E6AF, TheScriptEngine vslot 0x28, the first of the row
		runRow(ROW_SUBSYSTEMS_PHASE1);
		runRow(ROW_LOGIC_CRC);
		runRow(ROW_RECORDER_STATS);
		m_weather->update(); // RW 0x62E8BE: TheGlobalWeatherSystem vslot 0x28 (RW 0x71A09E), one of that row's subsystems (lane SPELL-2)
		m_invisibility->update(); // RW 0x62E8D4 .. 0x62E8DA: GameLogic + 0x178, the last of that row, before the command list (lane STEALTH-1)
		runRow(ROW_COMMAND_LIST);
		runRow(ROW_DRAWABLE_CALLBACK);
	}
	if (phase == 2)
	{
		m_partition->update(); // RW 0x62E93B: ThePartitionManager vslot 0x28 (RW 0xA39020 -> 0xA3B4E0), then the row's collision manager (lane MODULES-2)
		runRow(ROW_PARTITION_COLLISION);
		// RW 0x62E951 .. 0x62E97E
		for (Object *o = m_objectHead; o; o = o->getNextObject())
		{
			if (o->getRecordedFrame() != m_frame)
			{
				o->recordTransform(m_frame);
			}
		}
	}
	else if (phase > 2)
	{
		runModules(phase);
		if (phase == 5)
		{
			runRow(ROW_AI_UPDATE);
		}
	}
	runRow(ROW_DEFERRED_DRAIN);
	if (phase == 5)
	{
		runRow(ROW_SUBSYSTEMS_BEFORE_DESTROY);
		processDestroyList();
		runRow(ROW_SUBSYSTEMS_AFTER_DESTROY);
		// lane END-1: RW 0x62EBCE: TheVictoryConditions (RW 0xDE89AC) vslot 0x28 runs here, after the destroy list and the weapon / locomotor stores and before the
		// experience system (lane COMBAT-2 had it in the phase 1 row; RW 0x62E6AF .. 0x62E6DD updates four other subsystems)
		m_victory->update();
		m_experience->update(); // RW 0x62EBD9: TheDelayedExperienceLevelGrantSystem vslot 0x28, one of that row's subsystems (lane XP-1)
		m_skirmishAI->update(); // RW 0x62EBEF: TheSkirmishAIManager vslot 0x28, one of that row's subsystems (after the victory conditions; lane AI-1)
	}
	if (phase == 1)
	{
		endOfFrameObjectChecks();
		// RW 0x62EC9D (-> 0x697EB6): object list order; an object that dies from a pending hit stays in the list until the destroy list
		for (Object *o = m_objectHead; o; o = o->getNextObject())
		{
			if (o->pendingDamageCount() != 0)
			{
				o->updatePendingDamage();
			}
		}
		runRow(ROW_PENDING_DAMAGE);
		runRow(ROW_LOGIC_TIME_NOTIFY);
	}
	if (phase == 6)
	{
		// lane END-1: Player::update (RW 0x6AF269, from PlayerList::update RW 0x6A84DB over the 20 player slots) records each player's per-frame score
		// statistics (ScoreKeeper::recordPerFrameStats RW 0x79F704, RW 0x6AF3AD with the frame). The PlayerList's update was not found in the frame (its
		// caller is a subsystem table): the end of the frame is INFERENCE (S-1061)
		for (int i = 0; i < m_players.getPlayerCount(); ++i)
		{
			Player *p = m_players.getNthPlayer(i);
			// lane CAMP-1H: RW 0x6AF2FA .. 0x6AF32B, before the score: the player's teams run their generic scripts (Team::updateGenericScripts RW 0x7A267D;
			// a game without map scripts has none)
			if (m_scriptEngine->loaded())
			{
				m_scriptEngine->updateGenericScripts(*p);
			}
			p->getScoreKeeper().recordPerFrameStats(*this, *p, m_frame);
		}
	}
	m_inUpdate = wasInUpdate;
	--m_depth;
}

void GameLogic::runModules(int phase)
{
	int start = 0, end = 0;
	switch (phase)
	{
	case 3:
	case 4: start = 0; end = 1; break;
	case 5: start = 1; end = 3; break;
	case 6: start = 3; end = 4; break;
	default: return;
	}
	for (int p = start; p < end; ++p)
	{
		std::vector<UpdateModule *> &vec = m_updates[p];
		// RW 0x62E9D4 .. 0x62E9F7: phase 4 starts at size / 2 (i = size / 2 - 1, then ++), the others at 0 (i = -1, then ++)
		int i = phase == 4 ? (int)(vec.size() >> 1) - 1 : -1;
		size_t next = (size_t)(i + 1);
		bool brokeOut = false;
		while (next < vec.size())
		{
			++i;
			++next;
			if (phase == 3 && (size_t)i == (vec.size() >> 1)) // RW 0x62EA10 .. 0x62EA1E: stop at half of the CURRENT size
			{
				brokeOut = true;
				break;
			}
			UpdateModule *u = vec[(size_t)i];
			if (!u || u->friend_getNextCallFrame() > m_frame)
			{
				continue;
			}
			Object *obj = u->getObject();
			int sleep = (int)UPDATE_SLEEP_NONE;
			const DisabledMaskType disabled = obj->getDisabledMask();
			if (disabled == DISABLEDMASK_NONE || (disabled & u->getDisabledTypesToProcess()) != 0)
			{
				m_current = u;
				if (obj->testStatus(OBJECT_STATUS_DESTROYED))
				{
					sleep = (int)UPDATE_SLEEP_FOREVER; // RW 0x62EA85 `test byte [obj + 0x94], 1`
				}
				else
				{
					sleep = (int)u->update();
					if (sleep < (int)UPDATE_SLEEP_NONE)
					{
						sleep = (int)UPDATE_SLEEP_NONE;
					}
				}
				m_current = nullptr;
			}
			u->friend_setNextCallFrame(m_frame + (UnsignedInt)sleep);
		}
		(void)brokeOut;
		if (phase > 3)
		{
			migrateForeverModules(p);
		}
	}
}

// RW 0x62EADF .. 0x62EB4D: the reverse scan that moves FOREVER modules from the phase vector to `sleeping`
void GameLogic::migrateForeverModules(int vectorIndex)
{
	std::vector<UpdateModule *> &vec = m_updates[vectorIndex];
	size_t j = vec.size();
	while (j > 0)
	{
		--j;
		UpdateModule *u = vec[j];
		if (u && u->friend_getNextCallFrame() >= (UnsignedInt)UPDATE_SLEEP_FOREVER)
		{
			if (j < vec.size() - 1)
			{
				vec[j] = vec.back();
				vec[j]->friend_setIndexInLogic((int)j, vectorIndex);
			}
			vec.pop_back();
			u->friend_setIndexInLogic((int)m_sleeping.size()); // phase -1
			m_sleeping.push_back(u);
		}
	}
}

// RW 0x62EC55 .. 0x62ECAA
void GameLogic::endOfFrameObjectChecks()
{
	for (Object *o = m_objectHead; o; o = o->getNextObject())
	{
		if (o->getDisabledMask() != DISABLEDMASK_NONE)
		{
			o->checkDisabledStatus(m_frame);
		}
		if (o->testStatus(OBJECT_STATUS_IGNORE_AI_COMMAND))
		{
			o->checkIgnoreAICommandStatus(m_frame);
		}
		if (o->testStatus(OBJECT_STATUS_NO_COLLISIONS))
		{
			o->checkNoCollisionsStatus(m_frame);
		}
	}
}

// ---- destruction ----------------------------------------------------------------------------------------------------------
// ZH GameLogic.cpp:3959-4010 (spec 5.5)
void GameLogic::destroyObject(Object *obj)
{
	if (!obj || obj->isDestroyed())
	{
		return;
	}
	for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
	{
		if (DestroyModuleInterface *d = m->getDestroy())
		{
			d->onDestroy();
		}
	}
	obj->setStatus(OBJECT_STATUS_DESTROYED, true);
	// stop AI locomotion and the path: the AI update interface is not ported (S-142)
	m_objectsToDestroy.push_back(obj);
	obj->friend_onDestroy();
}

void GameLogic::friend_replaceUpdateModule(UpdateModule *old, UpdateModule *replacement)
{
	if (old)
	{
		removeUpdateModule(old);
	}
	if (!replacement)
	{
		return;
	}
	const UnsignedInt now = m_frame == 0 ? 1 : m_frame;
	if (replacement->friend_getNextCallFrame() == 0)
	{
		replacement->friend_setNextCallFrame(now);
	}
	if (replacement->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER)
	{
		replacement->friend_setIndexInLogic((int)m_sleeping.size(), -1);
		m_sleeping.push_back(replacement);
	}
	else
	{
		const int phase = (int)replacement->getUpdatePhase();
		std::vector<UpdateModule *> &vec = m_updates[phase];
		replacement->friend_setIndexInLogic((int)vec.size(), phase);
		vec.push_back(replacement);
	}
}

void GameLogic::removeUpdateModule(UpdateModule *u)
{
	const int index = u->friend_getIndexInLogic();
	const int phase = u->friend_getPhaseInLogic();
	if (index == -1)
	{
		return;
	}
	u->friend_setPhaseInLogic(-1);
	u->friend_setIndexInLogic(-1);
	std::vector<UpdateModule *> &vec = phase < 0 ? m_sleeping : m_updates[phase];
	if (index < (int)vec.size() - 1)
	{
		vec[(size_t)index] = vec.back();
		vec[(size_t)index]->friend_setIndexInLogic(index, phase);
	}
	vec.pop_back();
}

// RW 0x62A2C9
void GameLogic::processDestroyList()
{
	for (size_t k = 0; k < m_objectsToDestroy.size(); ++k) // the list may grow during the pass (a destroyed horde destroys its members)
	{
		Object *obj = m_objectsToDestroy[k];
		for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
		{
			if (UpdateModule *u = m->asUpdateModule())
			{
				removeUpdateModule(u);
			}
		}
		// the module destructors run at RW's delete: GettingBuiltBehavior's (RW 0x85750D) first kills the worker the structure spawned (RW 0x85730B, lane AI-2 r6).
		// Called before the object leaves the lists so the builder is still found by id; the dead worker goes on this same list (the loop walks its growth)
		for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
		{
			if (GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(m.get()))
			{
				gb->killSpawnedWorkerOnDelete();
			}
		}
		// the pathfinder's map removal (RW 0x6E85FB): not ported (PATH-1 hooks in through the object's modules)
		unlinkObject(obj);
		if (obj->getID() < m_objectsById.size())
		{
			m_objectsById[obj->getID()] = nullptr;
		}
		delete obj;
		++m_destroyedTotal;
	}
	m_objectsToDestroy.clear();
}

void GameLogic::reset()
{
	// ZH GameLogic::reset: every object goes; the scheduler vectors are cleared with their modules' indices
	m_current = nullptr;
	while (m_objectHead)
	{
		Object *o = m_objectHead;
		for (const std::unique_ptr<BehaviorModule> &m : o->modules())
		{
			if (UpdateModule *u = m->asUpdateModule())
			{
				u->friend_setIndexInLogic(-1);
			}
		}
		unlinkObject(o);
		// lane SMOOTH-3: the object leaves the id lookup before it is deleted. Its modules' destructors run AI state exits (e.g. the garrison enter state's
		// RW 0x752B0F onExit looks its container up by id); with the lookup still holding objects deleted earlier in this loop they used a freed object
		// (AddressSanitizer: heap-use-after-free in GameLogic::reset, test_hud_garrison_input).
		friend_removeObjectFromLookup(o);
		delete o;
	}
	for (std::vector<UpdateModule *> &vec : m_updates)
	{
		vec.clear();
	}
	m_sleeping.clear();
	m_objectsToDestroy.clear();
	m_objectsById.clear();
	m_templateInfo.clear();
	m_crushInfo.clear();
	m_frame = 0;
	m_gameDifficulty = 1; // lane CAMP-1H: GameLogic::reset RW 0x62D3EB (+ 0xA4 = 1)
	m_nextObjectID = 1;
	m_objectCount = 0;
	m_lastPhase = 0;
	m_depth = 0;
	m_creationsWithoutDispatch = 0;
	m_scriptEventsWithoutDispatch = 0;
	m_destroyedTotal = 0;
	m_errors.clear();
	m_economy->reset();
	m_combat->reset();
	m_victory->reset();
	m_skirmishAI->reset();
	m_experience->reset();
	m_partition->reset();
	m_emotions->reset();
	m_weather->reset(); // lane SPELL-2 (the objects are gone: nothing to remove from them)
	m_invisibility->reset();
	m_scriptEngine->reset(); // lane SCRIPT-1
}

// RW 0x628882 (see GameLogic.h): the LUA-1 function when installed, else the same steps
int GameLogic::sendObjectCreated(Object &obj)
{
	auto bindDrawable = [this, &obj]() {
		if (m_clientHooks)
		{
			m_clientHooks->objectCreated(obj); // RW 0x6288D3 / 0x6288DC: the Drawable is made and bound
		}
	};
	if (m_objectCreatedProc)
	{
		return m_objectCreatedProc(obj, bindDrawable);
	}
	const int seed = m_random.getValue(1, 999, "GameLogic.cpp", 0x19A7); // RW 0x628892 .. 0x6288A5
	bindDrawable();
	++m_creationsWithoutDispatch;
	return seed;
}

// lane HERO-2: RW 0x7379CB(slot, object, args) -> LuaScriptEngine::dispatch (RW 0x735F53), through the proc the live game installed (see GameLogic.h)
void GameLogic::dispatchScriptEvent(int slot, Object &self, const LuaEventArgs &args)
{
	if (m_scriptEventProc)
	{
		m_scriptEventProc(slot, self, args);
		return;
	}
	++m_scriptEventsWithoutDispatch;
}

// lane HERO-2: RW 0x663E32 (see GameLogic.h)
void GameLogic::scriptModelConditionEvents(Object &self, std::array<std::uint32_t, 19> &snapshot)
{
	const Object::ModelConditionBits &now = self.getModelConditionBits();
	if (m_frame < 2) // RW 0x663E41: TheGameLogic + 0x40 < 2
	{
		snapshot = now;
		return;
	}
	if (snapshot == now) // RW 0x444D9B
	{
		return;
	}
	if (m_modelConditionEventProc)
	{
		m_modelConditionEventProc(self, snapshot); // RW 0x7378DA(flags, snapshot, object)
	}
	snapshot = now;
}

// ---- the state hash ---------------------------------------------------------------------------------------------------------
std::uint32_t GameLogic::computeStateHash() const
{
	return hashState(nullptr, nullptr);
}

std::uint32_t GameLogic::computeStateHashBreakdown(std::vector<StateHashSection> &sections, std::vector<ObjectStateHash> *objects) const
{
	sections.clear();
	if (objects)
	{
		objects->clear();
	}
	return hashState(&sections, objects);
}

// One walk feeds the total and, when asked, the per-section hashes (lane MP-1): a section is hashed into the total and into its own hasher, so the
// breakdown can never drift from the hash it explains.
std::uint32_t GameLogic::hashState(std::vector<StateHashSection> *sections, std::vector<ObjectStateHash> *objects) const
{
	StateHasher h;
	auto section = [&](const std::string &name, const std::function<void(StateHasher &)> &feed) {
		const std::uint32_t before = h.value();
		feed(h);
		if (sections)
		{
			StateHasher own;
			feed(own);
			StateHashSection sec;
			sec.name = name;
			sec.value = own.value();
			sec.chainBefore = before;
			sec.chainAfter = h.value();
			sections->push_back(sec);
		}
	};
	section("frame, object ids, RNG", [&](StateHasher &x) {
		x.addU32(m_frame);
		x.addBool(m_frameAdvance); // RW GameLogic + 0x44: when false the frame counter does not advance
		x.addI32(m_gameDifficulty); // lane CAMP-1H: RW GameLogic + 0xA4
		x.addU32(m_nextObjectID);
		x.addU32((std::uint32_t)m_objectCount);
		for (std::uint32_t w : m_random.seedArray())
		{
			x.addU32(w);
		}
	});
	// the scheduler: the vectors in order (their order is state: iteration order decides module order)
	section("scheduler", [&](StateHasher &x) {
		for (int p = 0; p < PHASE_COUNT; ++p)
		{
			x.addU32((std::uint32_t)m_updates[p].size());
			for (const UpdateModule *u : m_updates[p])
			{
				x.addU32(u->getObject()->getID());
				x.addU32(u->friend_getNextCallFrame());
			}
		}
		x.addU32((std::uint32_t)m_sleeping.size());
		for (const UpdateModule *u : m_sleeping)
		{
			x.addU32(u->getObject()->getID());
			x.addU32(u->friend_getNextCallFrame());
		}
		x.addU32((std::uint32_t)m_objectsToDestroy.size());
		for (const Object *o : m_objectsToDestroy)
		{
			x.addU32(o->getID());
		}
	});
	section("objects", [&](StateHasher &x) {
		for (const Object *o = m_objectHead; o; o = o->getNextObject())
		{
			o->crc(x);
		}
	});
	if (objects)
	{
		for (const Object *o = m_objectHead; o; o = o->getNextObject())
		{
			StateHasher own;
			o->crc(own);
			objects->push_back(ObjectStateHash{ o->getID(), o->getTemplate() ? o->getTemplate()->getName() : std::string(), own.value() });
		}
	}
	// lane MODULES-2: after the objects, before the contributors (the MP-1 section order keeps its fixed head and tail)
	section("partition manager", [&](StateHasher &x) { m_partition->crc(x); }); // the trees' link order is history, hence state
	section("emotion system", [&](StateHasher &x) { m_emotions->crc(x); });     // the scary / hero list (its order is state)
	for (size_t i = 0; i < m_hashContributors.size(); ++i)
	{
		// MOVE-1: state outside the objects (the pathfinder; VIS-1: the shroud), named by their owners
		const std::string name = m_hashContributors[i].name.empty() ? "contributor " + std::to_string(i) : m_hashContributors[i].name;
		section(name, [&](StateHasher &x) { m_hashContributors[i].fn(x); });
	}
	section("players and teams", [&](StateHasher &x) { m_players.crc(x); });
	section("settings", [&](StateHasher &x) { m_settings.crc(x); });
	section("economy", [&](StateHasher &x) { m_economy->crc(x); });
	section("combat", [&](StateHasher &x) { m_combat->crc(x); });
	section("victory", [&](StateHasher &x) { m_victory->crc(x); });
	section("skirmish AI", [&](StateHasher &x) { m_skirmishAI->crc(x); }); // lane AI-1
	section("experience", [&](StateHasher &x) { m_experience->crc(x); }); // lane XP-1
	if (!m_createAHeroes->heroes().empty()) // lane HERO-2: only a game with a Create-a-Hero has the section
	{
		section("create-a-hero", [&](StateHasher &x) { x.addU32(m_createAHeroes->crc()); });
	}
	section("weather", [&](StateHasher &x) { m_weather->crc(x); }); // lane SPELL-2
	section("invisibility", [&](StateHasher &x) { m_invisibility->crc(x); }); // lane STEALTH-1
	section("large group audio", [&](StateHasher &x) { m_largeGroupAudio.crc(x); }); // lane AUDIO-4: TheLargeGroupAudio's gate (+ 0x38 / + 0x3C)
	if (m_scriptEngine->loaded())
	{
		section("script engine", [&](StateHasher &x) { m_scriptEngine->crc(x); }); // lane SCRIPT-1 (a game without map scripts keeps its hash)
	}
	return h.value();
}

// ---- reports --------------------------------------------------------------------------------------------------------------
GameLogic::Report GameLogic::report() const
{
	Report r;
	r.frame = m_frame;
	r.objects = m_objectCount;
	r.destroyed = m_destroyedTotal;
	for (const std::vector<UpdateModule *> &vec : m_updates)
	{
		r.updateModules += vec.size();
	}
	r.sleepingModules = m_sleeping.size();
	r.unportedCreated = m_modules.unportedCreated();
	r.creationsWithoutDispatch = m_creationsWithoutDispatch;
	r.errors = m_errors;
	for (const Object *o = m_objectHead; o; o = o->getNextObject())
	{
		std::map<std::string, size_t> perObject;
		for (const std::unique_ptr<BehaviorModule> &m : o->modules())
		{
			if (m->isHelper())
			{
				const ObjectHelperShell *h = static_cast<const ObjectHelperShell *>(m.get());
				++r.helperShells[h->helperName()];
				r.unportedUpdateCalls += h->calls();
			}
			else if (m->isUnported())
			{
				const std::string &cn = m->getModuleClassName();
				UnportedClassStat &st = r.unportedModules[cn];
				++st.modules;
				++perObject[cn];
				if (const UnportedUpdateModule *uu = dynamic_cast<const UnportedUpdateModule *>(m.get()))
				{
					st.interfaceMask = uu->interfaceMask();
					r.unportedUpdateCalls += uu->calls();
				}
				else if (const UnportedBehaviorModule *ub = dynamic_cast<const UnportedBehaviorModule *>(m.get()))
				{
					st.interfaceMask = ub->interfaceMask();
				}
			}
		}
		for (const auto &kv : perObject)
		{
			++r.unportedModules[kv.first].objects;
		}
	}
	// the stop lines
	size_t unportedModules = 0;
	for (const auto &kv : r.unportedModules)
	{
		unportedModules += kv.second.modules;
	}
	r.stops.push_back("[S-140] module runtime: " + std::to_string(m_modules.portedModuleCount()) + " of " + std::to_string(m_modules.classCount()) +
		" registered classes have a runtime class; every other class is an UnportedModule that does nothing (" + std::to_string(unportedModules) + " live instances of " +
		std::to_string(r.unportedModules.size()) + " classes, " + std::to_string(r.unportedUpdateCalls) + " calls)");
	size_t helpers = 0;
	for (const auto &kv : r.helperShells)
	{
		helpers += kv.second;
	}
	r.stops.push_back("[S-141] helper modules: " + std::to_string(helpers) + " live helper modules (SMC, Recovery, Repulsor, Defection, Guarding, WeaponStatus, FiringTracker) created in the retail order; the Recovery, Repulsor, Defection, Guarding and FiringTracker shells do nothing (the SMCHelper and the WeaponStatusHelper act: lanes PROD-1, PROJ-2)");
	r.stops.push_back("[S-142] objects: no radar, partition registration, weapons, special power mask, upgrades, physics, stealth or AI update interface; initObject only creates the drawable");
	r.stops.push_back("[S-146] players: a Player carries the index, name, faction template, type, colour, money, default team and relationships only; energy, sciences, upgrades, build list, AI, command points and the spell book belong to the lanes that port them");
	r.stops.push_back("[S-149] ActiveBody / HordeContain: the damage side of the body, the horde's member pass and the payload creation site (here: onCreate) are not ported or are inference; FX / OCL names are stored unresolved");
	for (const std::string &u : unportedPhaseWork())
	{
		r.stops.push_back("[S-143] " + u);
	}
	for (const std::string &u : m_economy->report())
	{
		r.stops.push_back(u); // lane ECON-1 (S-250 ..)
	}
	for (const std::string &u : m_combat->report())
	{
		r.stops.push_back(u); // lane COMBAT-1 (S-320 ..)
	}
	for (const std::string &u : m_experience->report()) // lane XP-1 (S-630 ..)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : CreateAHeroGame::stopLines()) // lane HERO-2 (S-1226)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : AttributeModifierPool::stopLines())
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : UpgradeModuleClasses::stopLines()) // lane UPGRADE-1 (S-480 ..)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : BuildStops::lines()) // lane BUILD-1 (S-300 ..)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : m_invisibility->report()) // lane STEALTH-1 (S-1040)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : m_victory->report()) // lane COMBAT-2 (S-344)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : SkirmishAIManager::stopLines()) // lane AI-1 (S-410 ..)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : m_skirmishAI->report())
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : StructureStops::lines()) // lane COMBAT-2 (S-340 ..)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : PartitionManager::stopLines()) // lane MODULES-2 (S-1020)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : m_emotions->report()) // lane MODULES-2 (S-1021)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : EmotionTrackerUpdate::stopLines()) // lane MODULES-2 (S-1022)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : AreaScanModules::stopLines()) // lane MODULES-2 (S-1023)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : HitReactionBehavior::stopLines()) // lane MODULES-2 (S-1024)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : AIUpdateInterface::emotionStops()) // lane MODULES-3 (S-1027 / S-1028)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : NotifyCrushUpdate::stopLines()) // lane MODULES-3 (S-1029)
	{
		r.stops.push_back(u);
	}
	for (const std::string &u : m_notedStops)
	{
		r.stops.push_back(u);
	}
	if (m_scriptEngine->loaded()) // lane SCRIPT-1 (S-1180 ..)
	{
		for (const std::string &u : ScriptEngine::stopLines())
		{
			r.stops.push_back(u);
		}
		for (const std::string &u : m_scriptEngine->report())
		{
			r.stops.push_back(u);
		}
	}
	for (const std::string &u : m_random.unverified())
	{
		r.stops.push_back("[S-080] logic RNG (" + std::string(m_random.algorithm() == RandomAlgorithm::ZH_CarryChain ? "ZH carry chain" : "RotWK game.dat LCG") + " selected explicitly): " + u);
	}
	if (!m_settings.unappliedFields.empty())
	{
		std::string names;
		size_t n = 0;
		for (const std::string &f : m_settings.unappliedFields)
		{
			if (n++ < 6)
			{
				names += (names.empty() ? "" : ", ") + f;
			}
		}
		r.stops.push_back("[S-152] settings: " + std::to_string(m_settings.unappliedFields.size()) + " GameData / AIData / MultiplayerSettings fields are read by the INI loader and not applied (no full parse table yet; first: " + names + ")");
	}
	if (m_scriptEventsWithoutDispatch > 0)
	{
		r.stops.push_back("[S-1220] script events: no ScriptEventProc (LiveScripting) was installed; " + std::to_string(m_scriptEventsWithoutDispatch) + " engine dispatches (RW 0x7379CB) reached no script engine");
	}
	if (m_creationsWithoutDispatch > 0)
	{
		r.stops.push_back("[S-147] OnCreated: no LuaScriptEngine::sendObjectCreated (setObjectCreatedProc) was installed; " + std::to_string(m_creationsWithoutDispatch) + " objects were created without the script world entry and the OnCreated dispatch");
	}
	return r;
}
