// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/ScriptEngine/ScriptEngine.h for the target facts and the stops.

#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/AI/AIWaypointPath.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/SimMath.h"

#include <algorithm>

namespace
{

// the engine's own action ordinals (RW 0x60C1C9)
enum EngineAction
{
	ACT_SET_FLAG = 1,
	ACT_SET_COUNTER = 2,
	ACT_NO_OP = 5,
	ACT_SET_TIMER = 6,
	ACT_ENABLE_SCRIPT = 8,
	ACT_DISABLE_SCRIPT = 9,
	ACT_CALL_SUBROUTINE = 10,
	ACT_INCREMENT_COUNTER = 15,
	ACT_DECREMENT_COUNTER = 16,
	ACT_SET_MILLISECOND_TIMER = 20,
	ACT_SET_TREE_SWAY = 103,
	ACT_CAMERA_FADE_FIRST = 124,
	ACT_CAMERA_FADE_LAST = 127,
	ACT_SET_ATTACK_PRIORITY_THING = 132,
	ACT_SET_ATTACK_PRIORITY_KIND_OF = 133,
	ACT_SET_DEFAULT_ATTACK_PRIORITY = 134,
	ACT_SET_RANDOM_TIMER = 150,
	ACT_SET_RANDOM_MSEC_TIMER = 151,
	ACT_STOP_TIMER = 152,
	ACT_RESTART_TIMER = 153,
	ACT_ADD_TO_MSEC_TIMER = 154,
	ACT_SUB_FROM_MSEC_TIMER = 155,
	ACT_SET_RANDOM_COUNTER = 373,
	ACT_SET_COUNTER_TO_COUNTER = 374,
	ACT_SET_FLAG_TO_FLAG = 375,
	ACT_SET_COUNTER_IN_SECONDS = 415,
	ACT_SET_RANDOM_COUNTER_IN_SECONDS = 416,
	ACT_SET_COUNTER_TO_UNIT_THREAT = 439,
	ACT_SET_COUNTER_TO_TEAM_THREAT = 440,
	ACT_SET_COUNTER_TO_CLIENT_RANDOM_VALUE = 508
};

// Parameter access as RotWK compiles getParameter(i): a missing parameter is a null pointer the callers dereference; the parsers guarantee the
// template's count, so an absent one is an internal error here (never invented)
const ScriptParameter &param(const std::vector<ScriptParameter> &p, size_t i)
{
	static const ScriptParameter kEmpty;
	return i < p.size() ? p[i] : kEmpty;
}

bool compareOp(int op, std::int32_t a, std::int32_t b)
{
	// RW 0x608B28 / 0x7E888B / 0x7E8954: 0 <, 1 <=, 2 ==, 3 >=, 4 >, 5 !=, anything else false
	switch (op)
	{
	case 0: return a < b;
	case 1: return a <= b;
	case 2: return a == b;
	case 3: return a >= b;
	case 4: return a > b;
	case 5: return a != b;
	default: return false;
	}
}

} // namespace

ScriptEngine::ScriptEngine(GameLogic &logic)
	: m_logic(logic)
{
}

ScriptEngine::~ScriptEngine() = default;

void ScriptEngine::reset()
{
	m_loaded = false;
	m_firstUpdate = true;
	m_gameDifficulty = 1;                  // lane CAMP-1H: RW 0x609851 (+ 0x1A5C4 = 1)
	m_objectsReceiveDifficultyBonus = true; // lane CAMP-1H: RW 0x609857 (+ 0x1A5D5 = 1)
	m_sides.clear();
	m_counters.clear();
	m_flags.clear();
	m_namedObjects.clear();
	m_objectLists.clear();
	m_triggers = nullptr;
	m_runtimeTriggers.clear(); // lane HUD-5
	m_cameras = nullptr;
	m_currentPlayer = nullptr;
	m_currentSide.clear();
	m_currentScript = nullptr;
	m_clientRequests.clear();
	m_endRequests.clear();
	m_sequential.clear();
	for (size_t i = 0; i < m_acquiredSciences.size(); ++i) // donor Open-BFME-2 ScriptEngine::reset (0x00209ABE): the 20 m_acquiredSciences vectors cleared
	{
		const Player *p = m_logic.players().getNthPlayer((int)i);
		m_acquiredSciences[i].clear(p ? &p->science() : nullptr);
	}
	m_audioTimers.clear();
	m_genericScripts.clear(); // lane CAMP-1H
	m_thisTeam = nullptr;
	m_stats = Stats();
}

bool ScriptEngine::didPlayerAcquireScience(const Player &p, ScienceType st, bool consume)
{
	const int index = p.getPlayerIndex();
	if (index < 0 || index >= (int)m_acquiredSciences.size()) // RW 0x759646: -1 < index && index < 0x14
	{
		return false;
	}
	return m_acquiredSciences[(size_t)index].didAcquire(p.science(), st, consume);
}

// RW 0x60A377 / 0x60BCE5 want a list's scripts and its groups apart (ScriptList + 0x10 / + 0xC; a group's + 8 / + 4): the file's interleaved
// items are split, each kind keeping its file order
void ScriptEngine::buildGroup(const std::vector<ScriptItem> &items, const std::string &side, RGroup &out)
{
	for (const ScriptItem &it : items)
	{
		if (it.script)
		{
			std::unique_ptr<RScript> s = std::make_unique<RScript>();
			s->def = it.script.get();
			s->sideName = side;
			s->active = it.script->isActive; // RW 0x7B83CC: + 0x40 = + 0x28 = the stored active flag
			out.scripts.push_back(std::move(s));
			++m_stats.scripts;
		}
		else if (it.group)
		{
			std::unique_ptr<RGroup> g = std::make_unique<RGroup>();
			g->name = it.group->name;
			g->active = it.group->isActive;
			g->subroutine = it.group->isSubroutine;
			buildGroup(it.group->items, side, *g);
			out.groups.push_back(std::move(g));
			++m_stats.groups;
		}
	}
}

void ScriptEngine::newGame(const std::vector<SideScripts> &sides, const std::vector<TriggerArea> *triggers, const std::vector<NamedCamera> *cameras,
	bool warOfTheRing)
{
	reset();
	m_triggers = triggers;
	m_cameras = cameras;
	m_warOfTheRing = warOfTheRing;
	m_sides.resize(sides.size());
	for (size_t i = 0; i < sides.size(); ++i)
	{
		m_sides[i].name = sides[i].sideName;
		for (const ScriptList *list : sides[i].lists)
		{
			if (list)
			{
				buildGroup(list->items, sides[i].sideName, m_sides[i].root);
			}
		}
	}
	m_stats.sides = m_sides.size();
	m_loaded = true;
}

// ---- names ------------------------------------------------------------------------------------------------------------------------------------

// RW 0x72C43C: "Side/name" -> (Side, name); a name without '/' belongs to the current side
std::pair<std::string, std::string> ScriptEngine::qualify(const std::string &name) const
{
	const size_t slash = name.find('/');
	if (slash != std::string::npos)
	{
		return { name.substr(0, slash), name.substr(slash + 1) };
	}
	return { m_currentSide, name };
}

const ScriptEngine::Counter *ScriptEngine::findCounter(const std::string &name) const
{
	auto it = m_counters.find(qualify(name));
	return it == m_counters.end() ? nullptr : &it->second;
}

ScriptEngine::Counter &ScriptEngine::counter(const std::string &name)
{
	return m_counters[qualify(name)];
}

bool *ScriptEngine::findFlag(const std::string &name)
{
	auto it = m_flags.find(qualify(name));
	return it == m_flags.end() ? nullptr : &it->second;
}

bool &ScriptEngine::flag(const std::string &name)
{
	return m_flags[qualify(name)];
}

ScriptEngine::RSide *ScriptEngine::findSide(const std::string &name)
{
	for (RSide &s : m_sides)
	{
		if (s.name == name)
		{
			return &s;
		}
	}
	return nullptr;
}

ScriptEngine::RScript *ScriptEngine::findScriptIn(RGroup &g, const std::string &name)
{
	for (std::unique_ptr<RScript> &s : g.scripts)
	{
		if (s->def->name == name)
		{
			return s.get();
		}
	}
	for (std::unique_ptr<RGroup> &c : g.groups)
	{
		if (RScript *s = findScriptIn(*c, name))
		{
			return s;
		}
	}
	return nullptr;
}

ScriptEngine::RGroup *ScriptEngine::findGroupIn(RGroup &g, const std::string &name)
{
	for (std::unique_ptr<RGroup> &c : g.groups)
	{
		if (c->name == name)
		{
			return c.get();
		}
		if (RGroup *r = findGroupIn(*c, name))
		{
			return r;
		}
	}
	return nullptr;
}

// RW 0x604A5D: the qualified name's side list (RW 0x604986: the player whose name key is the side part), then the script by name in it
// (INFERENCE: RW 0x7B72EC's name index covers the list's scripts at every group depth; the first in file order wins)
ScriptEngine::RScript *ScriptEngine::findScript(const std::string &name)
{
	const std::pair<std::string, std::string> q = qualify(name);
	RSide *side = findSide(q.first);
	return side ? findScriptIn(side->root, q.second) : nullptr;
}

ScriptEngine::RGroup *ScriptEngine::findGroup(const std::string &name)
{
	const std::pair<std::string, std::string> q = qualify(name);
	RSide *side = findSide(q.first);
	return side ? findGroupIn(side->root, q.second) : nullptr;
}

bool ScriptEngine::scriptActive(const std::string &name) const
{
	RScript *s = const_cast<ScriptEngine *>(this)->findScript(name);
	return s && s->active;
}

const TriggerArea *ScriptEngine::findTrigger(const std::string &name) const
{
	if (m_triggers)
	{
		for (const TriggerArea &t : *m_triggers)
		{
			if (t.name == name)
			{
				return &t;
			}
		}
	}
	return nullptr;
}

// lane HUD-5 (see ScriptEngine.h)
int ScriptEngine::addPolygonTrigger(const TriggerArea &area, TriggerCallback callback)
{
	RuntimeTrigger t;
	t.area = std::make_shared<TriggerArea>(area);
	t.callback = std::move(callback);
	m_runtimeTriggers.push_back(std::move(t));
	return (int)triggerAreaCount() - 1;
}

size_t ScriptEngine::triggerAreaCount() const
{
	return (m_triggers ? m_triggers->size() : 0) + m_runtimeTriggers.size();
}

const TriggerArea &ScriptEngine::triggerAreaAt(size_t i) const
{
	const size_t mapCount = m_triggers ? m_triggers->size() : 0;
	return i < mapCount ? (*m_triggers)[i] : *m_runtimeTriggers[i - mapCount].area;
}

void ScriptEngine::triggerCallback(size_t i, Object &obj, bool entered) const
{
	const size_t mapCount = m_triggers ? m_triggers->size() : 0;
	if (i >= mapCount && i - mapCount < m_runtimeTriggers.size() && m_runtimeTriggers[i - mapCount].callback)
	{
		m_runtimeTriggers[i - mapCount].callback(obj, entered); // RW 0x6E4B21 / 0x6E4B3E (the map's triggers carry no callback)
	}
}

int ScriptEngine::triggerIndex(const TriggerArea *t) const
{
	if (!m_triggers || !t || t < m_triggers->data() || t >= m_triggers->data() + m_triggers->size())
	{
		return -1;
	}
	return (int)(t - m_triggers->data());
}

const NamedCamera *ScriptEngine::findNamedCamera(const std::string &name) const
{
	if (m_cameras)
	{
		for (const NamedCamera &c : *m_cameras)
		{
			if (c.name == name)
			{
				return &c;
			}
		}
	}
	return nullptr;
}

// ---- object type lists ---------------------------------------------------------------------------------------------------------------------------

// RW 0x759D77 (list name, type name, add): RW 0x759158 finds the list (a new ObjectTypes of that name is appended when there is none); RW 0x778F6C adds
// the type (ZH ObjectTypes::addObjectType: once), RW 0x778F3A removes it; an empty list is removed (RW 0x604067)
void ScriptEngine::objectListAdd(const std::string &list, const std::string &type, bool add)
{
	size_t i = 0;
	for (; i < m_objectLists.size(); ++i)
	{
		if (m_objectLists[i].first == list)
		{
			break;
		}
	}
	if (i == m_objectLists.size())
	{
		m_objectLists.emplace_back(list, std::vector<std::string>());
	}
	std::vector<std::string> &types = m_objectLists[i].second;
	auto it = std::find(types.begin(), types.end(), type);
	if (add)
	{
		if (it == types.end())
		{
			types.push_back(type);
		}
	}
	else if (it != types.end())
	{
		types.erase(it);
	}
	if (types.empty())
	{
		m_objectLists.erase(m_objectLists.begin() + (long)i);
	}
}

const std::vector<std::string> *ScriptEngine::findObjectList(const std::string &list) const
{
	for (const auto &l : m_objectLists)
	{
		if (l.first == list)
		{
			return &l.second;
		}
	}
	return nullptr;
}

// ---- the named object cache ---------------------------------------------------------------------------------------------------------------------

void ScriptEngine::objectNamed(Object &obj)
{
	if (obj.getName().empty())
	{
		return;
	}
	for (auto &e : m_namedObjects)
	{
		if (e.first == obj.getName())
		{
			e.second = obj.getID();
			return;
		}
	}
	m_namedObjects.emplace_back(obj.getName(), obj.getID());
}

void ScriptEngine::nameInCache(const std::string &name, Object &obj)
{
	if (name.empty())
	{
		return;
	}
	for (auto &e : m_namedObjects)
	{
		if (e.first == name)
		{
			e.second = obj.getID();
			return;
		}
	}
	m_namedObjects.emplace_back(name, obj.getID());
}

bool ScriptEngine::isInNamedCache(const Object &obj) const
{
	for (const auto &e : m_namedObjects)
	{
		if (e.second == obj.getID())
		{
			return true;
		}
	}
	return false;
}

Object *ScriptEngine::getUnitNamed(const std::string &name) const
{
	if (name.empty())
	{
		return nullptr;
	}
	for (const auto &e : m_namedObjects)
	{
		if (e.first == name)
		{
			return m_logic.findObjectByID(e.second);
		}
	}
	return nullptr;
}

bool ScriptEngine::didUnitExist(const std::string &name) const
{
	for (const auto &e : m_namedObjects)
	{
		if (e.first == name)
		{
			return m_logic.findObjectByID(e.second) == nullptr;
		}
	}
	return false;
}

// ---- seconds -> frames --------------------------------------------------------------------------------------------------------------------------

// RW 0x60911C: fld [0.005f]; fmul [seconds]; fmul [1000.0f]; fstp qword; call ceil; fstp dword; fld dword; fistp (PC24, round to nearest)
std::int32_t ScriptEngine::secondsToFrames(float seconds)
{
	const double product = NumericState::pc24MulW(NumericState::pc24MulW((double)0.005f, (double)seconds), (double)1000.0f);
	const float rounded = NumericState::fstpDword(NumericState::ceilD(product));
	return NumericState::fistp32((double)rounded);
}

// ---- the frame ----------------------------------------------------------------------------------------------------------------------------------

void ScriptEngine::update()
{
	if (!m_loaded)
	{
		return;
	}
	// RW 0x60CC67 runs unless the War of the Ring per-player gate says no (not ported: a War of the Ring battle runs every frame here, S-1185)
	if (m_firstUpdate)
	{
		// RW 0x60A3CB (createNamedCache): the cache is rebuilt from the live objects in object list order, so objects named before the game's scripts
		// were loaded are found; later names reach it through Object::setName (objectNamed)
		m_firstUpdate = false;
		m_namedObjects.clear();
		for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
		{
			objectNamed(*o);
		}
	}
	++m_stats.updates;
	// (the close-window / end-game timers and the fades, RW 0x1A208 / 0x1A204 / 0x1A238, belong to the client requests here: S-1182)
	// RW 0x60CDA5: every countdown counter with a value >= 0 decrements
	for (auto &kv : m_counters)
	{
		if (kv.second.countdown && kv.second.value > -1)
		{
			--kv.second.value;
		}
	}
	PlayerList &players = m_logic.players();
	for (size_t i = 0; i < m_sides.size(); ++i)
	{
		m_currentPlayer = players.getNthPlayer((int)i);
		m_currentSide = m_currentPlayer ? m_currentPlayer->getPlayerName() : m_sides[i].name;
		if (m_currentPlayer && m_currentPlayer->getPlayerName() != m_sides[i].name)
		{
			note("side " + std::to_string(i) + " '" + m_sides[i].name + "' is player '" + m_currentPlayer->getPlayerName() + "' (the lists are not aligned)");
		}
		executeScripts(m_sides[i].root.scripts);
		executeGroups(m_sides[i].root.groups);
		m_currentPlayer = nullptr;
		m_currentSide.clear();
	}
	// lane CAMP-1H: RW 0x6A8541 (ThePlayerList->updateTeamStates); RW 0x862535 is not ported (S-1185); then the sequential scripts (RW 0x60C441)
	updateTeamStates();
	updateSequentialScripts();
}

// RW 0x60A377: the scripts of one list, a subroutine script skipped
void ScriptEngine::executeScripts(std::vector<std::unique_ptr<RScript>> &scripts)
{
	for (size_t i = 0; i < scripts.size(); ++i)
	{
		RScript &s = *scripts[i];
		if (!s.def->isSubroutine)
		{
			executeScript(s);
		}
	}
}

// RW 0x60BCE5: an active non-subroutine group runs its scripts, then its child groups
void ScriptEngine::executeGroups(std::vector<std::unique_ptr<RGroup>> &groups)
{
	for (size_t i = 0; i < groups.size(); ++i)
	{
		RGroup &g = *groups[i];
		if (g.active && !g.subroutine)
		{
			executeScripts(g.scripts);
			executeGroups(g.groups);
		}
	}
}

// RW 0x603878
bool ScriptEngine::shouldEvaluate(RScript &s) const
{
	if (!s.active)
	{
		return false;
	}
	// the difficulty (RW 0x6AA61B): a player with an AI (+ 0x2FC) answers its AI's difficulty (RW 0x9A5E0C: the skirmish difficulty here), any other
	// player and no player the game's (+ 0x1A5C4, setGameDifficulty); a value outside 0 .. 3 checks no flag
	int difficulty = m_gameDifficulty;
	if (m_currentPlayer && m_currentPlayer->isSkirmishAI() && m_currentPlayer->getSkirmishDifficulty() >= 0)
	{
		difficulty = m_currentPlayer->getSkirmishDifficulty();
	}
	bool allowed = true;
	if (difficulty == 0)
	{
		allowed = s.def->easy;
	}
	else if (difficulty == 1)
	{
		allowed = s.def->normal;
	}
	else if (difficulty > 1 && difficulty < 4)
	{
		allowed = s.def->hard;
	}
	if (!allowed)
	{
		return false;
	}
	return s.frameToEvaluateAt <= m_logic.getFrame();
}

// RW 0x60A15C (the condition team iteration RW 0x6099DC .. 0x609A99 is not ported: S-1183)
void ScriptEngine::executeScript(RScript &s)
{
	if (!shouldEvaluate(s))
	{
		return;
	}
	++m_stats.scriptsEvaluated;
	if (s.def->delayEvaluationSeconds > 0)
	{
		s.frameToEvaluateAt = (UnsignedInt)(5 * s.def->delayEvaluationSeconds) + m_logic.getFrame(); // RW 0xD9F608 = 5
	}
	const RScript *saved = m_currentScript;
	m_currentScript = &s;
	if (s.def->actionsFireSequentially)
	{
		// RW 0x609C3A: the conditions true -> a sequential record for the script's team / unit; neither found -> the script goes inactive at once
		if (evaluateConditions(*s.def))
		{
			++m_stats.scriptsFired;
			if (!startSequentialScript(s))
			{
				s.active = false;
				m_currentScript = saved;
				return;
			}
			if (s.def->isOneShot)
			{
				s.active = false;
			}
		}
		else if (!s.def->falseActions.empty())
		{
			s.active = false;
		}
		m_currentScript = saved;
		return;
	}
	if (evaluateConditions(*s.def))
	{
		++m_stats.scriptsFired;
		executeActions(s.def->actions, s);
		if (s.def->isOneShot)
		{
			s.active = false; // RW 0x609BF1: + 0x29 -> + 0x40 = 0 (after the actions)
		}
	}
	else if (!s.def->falseActions.empty())
	{
		++m_stats.falseFired;
		executeActions(s.def->falseActions, s);
		if (s.def->isOneShot)
		{
			s.active = false; // lane SCRIPT-3: RW 0x609C0F -> 0x609C16: the false actions end a one-shot script too
		}
	}
	m_currentScript = saved;
}

// RW 0x60930F (team, player) as executeScript calls it (RW 0x609B89 / 0x609C55: no team, no player): + 0x1A210 is cleared around the conditions (lane
// CAMP-1H: inside a team script a called script's conditions do not see its team; its actions do), the current player stays
bool ScriptEngine::evaluateConditions(const Script &s)
{
	return evaluateConditions(s, nullptr);
}

// RW 0x60930F: + 0x1A210 = team; the current player (+ 0x1A230) = the team's controlling player (RW 0x79FD6F), else unchanged; both restored after
bool ScriptEngine::evaluateConditions(const Script &s, Team *team)
{
	Team *const savedTeam = m_thisTeam;
	Player *const savedPlayer = m_currentPlayer;
	m_thisTeam = team;
	if (team && team->getControllingPlayer())
	{
		m_currentPlayer = team->getControllingPlayer();
	}
	const bool r = evaluateConditionsNoContext(s);
	m_thisTeam = savedTeam;
	m_currentPlayer = savedPlayer;
	return r;
}

bool ScriptEngine::evaluateConditionsNoContext(const Script &s)
{
	for (const OrCondition &orc : s.orConditions)
	{
		if (orc.conditions.empty())
		{
			continue;
		}
		bool all = true;
		for (const ScriptCondition &c : orc.conditions)
		{
			if (c.enabled && !evaluateCondition(c))
			{
				all = false;
				break;
			}
		}
		if (all)
		{
			return true;
		}
	}
	return false;
}

// RW 0x6092A9
bool ScriptEngine::evaluateCondition(const ScriptCondition &c)
{
	++m_stats.conditionsEvaluated;
	const ScriptTemplate *t = ScriptTemplates::condition(c.resolved);
	if (!t || (t->modeMask & gameMode()) == 0)
	{
		return false;
	}
	switch (c.resolved)
	{
	case 0: return false;
	case 1: return evaluateCounter(c);
	case 2: return evaluateFlag(c);
	case 3: return true;
	case 4: return evaluateTimer(c);
	default:
	{
		// lane CAMP-1: TheScriptConditions vslot 0x38 is RW 0x7ED72C (vtable RW 0xC4BE78 + 0x38), which runs RW 0x7EB7CD and inverts the answer when the
		// condition's + 0x4D (the v5 second flag, the editor's NOT) is set; the ordinals 0 .. 4 above are the engine's own and never inverted
		const bool result = ScriptConditions::evaluate(*this, c);
		return c.flagB4D ? !result : result;
	}
	}
}

// RW 0x60C1C9
void ScriptEngine::executeActions(const std::vector<ScriptActionRec> &actions, RScript &s)
{
	(void)s;
	for (const ScriptActionRec &a : actions)
	{
		const ScriptTemplate *t = ScriptTemplates::action(a.resolved);
		if (!t || (t->modeMask & gameMode()) == 0)
		{
			continue;
		}
		++m_stats.actionsRun;
		if (!executeEngineAction(a))
		{
			ScriptActions::execute(*this, a); // TheScriptActions vslot 0x38 (RW 0x7CAFA5)
		}
	}
}

void ScriptEngine::executeAction(const ScriptActionRec &a)
{
	const ScriptTemplate *t = ScriptTemplates::action(a.resolved);
	if (!t || (t->modeMask & gameMode()) == 0)
	{
		return;
	}
	++m_stats.actionsRun;
	if (!executeEngineAction(a))
	{
		ScriptActions::execute(*this, a);
	}
}

// ---- sequential scripts (lane SCRIPT-2) -------------------------------------------------------------------------------------------------------

namespace
{

// RW 0x76FCBF: every member with an AI that is alive is idle (vslot 0x1B8)
bool teamIdle(const Team &t)
{
	for (Object *o = t.getFirstMember(); o; o = o->friend_teamNext())
	{
		const AIUpdateInterface *ai = o->getAIUpdateInterface();
		if (ai && !ai->isIdle() && !o->isEffectivelyDead())
		{
			return false;
		}
	}
	return true;
}

// RW 0x76FDA1: every member dead (true for no member)
bool teamAllDead(const Team &t)
{
	for (Object *o = t.getFirstMember(); o; o = o->friend_teamNext())
	{
		if (!o->isEffectivelyDead())
		{
			return false;
		}
	}
	return true;
}

// RW 0x771F49 (an AI group of the team stops): every member's AI idles from the script
void idleTeam(Team &t)
{
	for (Object *o = t.getFirstMember(); o; o = o->friend_teamNext())
	{
		if (AIUpdateInterface *ai = o->getAIUpdateInterface())
		{
			ai->aiIdle(CMD_FROM_SCRIPT);
		}
	}
}

} // namespace

bool ScriptEngine::startSequentialScript(RScript &s)
{
	const Script &d = *s.def;
	SequentialScript r;
	if (d.sequentialTargetType == 0)
	{
		Team *t = ScriptConditions::team(*this, d.sequentialTargetName);
		if (!t)
		{
			return false;
		}
		idleTeam(*t);
		r.team = t->getID();
	}
	else
	{
		Object *o = getUnitNamed(d.sequentialTargetName);
		if (!o)
		{
			return false;
		}
		r.object = o->getID();
	}
	r.scriptName = d.name;
	r.side = m_currentSide;
	r.script = &d;
	r.loopCount = d.loopCount;
	appendSequentialScript(r);
	return true;
}

void ScriptEngine::appendSequentialScript(const SequentialScript &in)
{
	SequentialScript r = in;
	r.again = false;
	r.index = -1; // RW 0x606FDA: + 0x18 = -1, + 0x28 = 0
	for (std::vector<SequentialScript> &chain : m_sequential)
	{
		if (chain.empty())
		{
			continue;
		}
		const SequentialScript &head = chain.front();
		if ((r.object != INVALID_ID && r.object == head.object) || (r.team != 0 && r.team == head.team))
		{
			chain.push_back(r);
			return;
		}
	}
	m_sequential.push_back({ r });
}

void ScriptEngine::removeSequentialScriptsOf(ObjectID object, std::uint32_t team)
{
	for (size_t i = 0; i < m_sequential.size();)
	{
		std::vector<SequentialScript> &chain = m_sequential[i];
		if (object != INVALID_ID)
		{
			// RW 0x604C9F: the slots of that object (or null ones) are erased. Inside the update (an action of a sequential script stops a queue) the
			// slot is only emptied: the update erases an empty slot when it reaches it, so its position and the records it holds stay valid
			if (chain.empty() || chain.front().object == object)
			{
				if (m_updatingSequential)
				{
					chain.clear();
				}
				else
				{
					m_sequential.erase(m_sequential.begin() + (std::ptrdiff_t)i);
					continue;
				}
			}
		}
		else if (chain.empty() || chain.front().team == team)
		{
			chain.clear(); // RW 0x604CD9: emptied, the slot stays (the update erases it)
		}
		++i;
	}
}

// RW 0x60C441
void ScriptEngine::updateSequentialScripts()
{
	TeamFactory &teams = m_logic.players().teams();
	struct Updating
	{
		bool &flag;
		explicit Updating(bool &f) : flag(f) { flag = true; }
		~Updating() { flag = false; }
	} updating(m_updatingSequential);
	size_t i = 0;
	size_t prev = (size_t)-1;
	int sameSlot = 0;
	while (i < m_sequential.size())
	{
		sameSlot = i == prev ? sameSlot + 1 : 0;
		if (sameSlot >= 0x15) // the same slot at most 21 times a frame
		{
			++i;
			continue;
		}
		prev = i;
		if (m_sequential[i].empty())
		{
			m_sequential.erase(m_sequential.begin() + (std::ptrdiff_t)i); // RW 0x6040B6(.., 0, 1) on a null slot
			prev = (size_t)-1;
			continue;
		}
		SequentialScript *r = &m_sequential[i].front();
		Object *obj = r->object != INVALID_ID ? m_logic.findObjectByID(r->object) : nullptr;
		Team *team = r->team != 0 ? teams.findTeamByID(r->team) : nullptr;
		if (!obj && !team)
		{
			m_sequential[i].erase(m_sequential[i].begin()); // the head goes; an emptied slot is erased
			if (m_sequential[i].empty())
			{
				m_sequential.erase(m_sequential.begin() + (std::ptrdiff_t)i);
			}
			prev = (size_t)-1;
			continue;
		}
		m_currentPlayer = obj ? obj->getControllingPlayer() : team->getControllingPlayer();
		AIUpdateInterface *ai = obj ? obj->getAIUpdateInterface() : nullptr;
		bool again = false;
		if (ai || team)
		{
			if (r->framesToWait >= 1)
			{
				--r->framesToWait;
			}
			else
			{
				// INFERENCE (S-1185): the AI's + 0x3CA (a busy flag next to isIdle) and the group form's RW 0x76FD74 read as clear
				const bool idle = (!ai || ai->isIdle()) && (!team || teamIdle(*team));
				if (idle)
				{
					if (!r->again)
					{
						++r->index;
					}
					else
					{
						r->again = false;
					}
					const Script &def = *r->script;
					if (r->index < 0 || (size_t)r->index >= def.actions.size())
					{
						// the end of the actions: a looping record is appended again (RW 0x60C930), the head goes
						if (r->loopCount != 0)
						{
							if (r->loopCount != -1)
							{
								--r->loopCount;
							}
							r->framesToWait = -1;
							const SequentialScript copy = *r;
							appendSequentialScript(copy);
						}
						m_sequential[i].erase(m_sequential[i].begin());
						if (m_sequential[i].empty())
						{
							m_sequential.erase(m_sequential.begin() + (std::ptrdiff_t)i);
						}
						prev = (size_t)-1;
						m_currentPlayer = nullptr;
						continue;
					}
					// RW 0x604243: the record's side for the action's names (restored after), the condition team / object (+ 0x1A218 / + 0x1A21C)
					const std::string savedSide = m_currentSide;
					m_currentSide = r->side;
					r->framesToWait = -1;
					const ScriptActionRec &a = def.actions[(size_t)r->index];
					const std::string actionName = ScriptTemplates::action(a.resolved) ? ScriptTemplates::action(a.resolved)->name : std::string();
					if (actionName == "SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_ALL" || actionName == "SKIRMISH_WAIT_FOR_COMMANDBUTTON_AVAILABLE_PARTIAL" ||
						actionName == "TEAM_WAIT_FOR_NOT_CONTAINED_ALL" || actionName == "TEAM_WAIT_FOR_NOT_CONTAINED_PARTIAL" || actionName == "GATE_READY")
					{
						// RW 0x60C6F0 .. 0x60C7F0: the wait actions ask TheScriptActions' sequential interface (RW 0xDE8844 vslots 0x3C / 0x40 / 0x44) and
						// repeat (+ 0x24) while it says wait: not ported, they pass at once (S-1185)
						note("[S-1185] sequential wait action passes at once: " + actionName);
					}
					else
					{
						// RW 0x60C674: the record's team and object are the condition team / object (+ 0x1A218 / + 0x1A21C) of the action ("<This Team>")
						const std::uint32_t savedTeam = m_conditionTeam;
						m_conditionTeam = r->team;
						executeAction(a);
						m_conditionTeam = savedTeam;
					}
					// the action may have appended records (the vector may have moved) or stopped this queue (the slot is empty now: erased when the
					// loop comes back to it, as retail's null slot): the slot is fetched again
					m_currentSide = savedSide;
					if (m_sequential[i].empty())
					{
						m_currentPlayer = nullptr;
						continue;
					}
					r = &m_sequential[i].front();
					obj = r->object != INVALID_ID ? m_logic.findObjectByID(r->object) : nullptr;
					team = r->team != 0 ? teams.findTeamByID(r->team) : nullptr;
					ai = obj ? obj->getAIUpdateInterface() : nullptr;
					m_currentSide = savedSide;
					if (r->again)
					{
						++i;
						m_currentPlayer = nullptr;
						continue;
					}
					if (ai && ai->isIdle())
					{
						again = true;
					}
					if (team && teamIdle(*team))
					{
						again = true;
					}
					if (again)
					{
						const bool objDead = obj && obj->isEffectivelyDead();
						const bool teamGone = team && teamAllDead(*team) &&
							!(m_currentPlayer && m_currentPlayer->getDefaultTeam() == team);
						if (objDead || (team && !obj && teamGone) || (!obj && !team))
						{
							m_sequential.erase(m_sequential.begin() + (std::ptrdiff_t)i); // RW 0x6040B6(.., 1, 1): the whole chain
							prev = (size_t)-1;
						}
						m_currentPlayer = nullptr;
						continue;
					}
				}
			}
		}
		m_currentPlayer = nullptr;
		++i;
	}
	m_currentPlayer = nullptr;
}

// the actions RW 0x60C1C9 runs itself; false: not one of them
bool ScriptEngine::executeEngineAction(const ScriptActionRec &a)
{
	switch (a.resolved)
	{
	case ACT_SET_FLAG: // RW 0x608FCC(a, 0)
		flag(param(a.params, 0).stringValue) = param(a.params, 1).intValue != 0;
		return true;
	case ACT_SET_FLAG_TO_FLAG: // RW 0x608FCC(a, 1): the other flag's value when it exists, else false
	{
		bool &target = flag(param(a.params, 0).stringValue);
		const bool *source = findFlag(param(a.params, 1).stringValue);
		target = source ? *source : false;
		return true;
	}
	case ACT_SET_COUNTER: setCounter(a, 0, false, false); return true;
	case ACT_SET_COUNTER_IN_SECONDS: setCounter(a, 0, false, true); return true;
	case ACT_SET_RANDOM_COUNTER: setCounter(a, 1, false, false); return true;
	case ACT_SET_COUNTER_TO_COUNTER: setCounter(a, 0, true, false); return true;
	case ACT_SET_RANDOM_COUNTER_IN_SECONDS: setCounter(a, 1, false, true); return true;
	case ACT_SET_COUNTER_TO_CLIENT_RANDOM_VALUE: setCounter(a, 2, false, false); return true;
	case ACT_NO_OP: return true;
	case ACT_SET_TIMER: setTimer(a, false, false); return true;
	case ACT_SET_MILLISECOND_TIMER: setTimer(a, true, false); return true;
	case ACT_SET_RANDOM_TIMER: setTimer(a, false, true); return true;
	case ACT_SET_RANDOM_MSEC_TIMER: setTimer(a, true, true); return true;
	case ACT_ENABLE_SCRIPT: enableScript(a, true); return true;
	case ACT_DISABLE_SCRIPT: enableScript(a, false); return true;
	case ACT_CALL_SUBROUTINE: callSubroutine(a); return true;
	case ACT_INCREMENT_COUNTER: // RW 0x608EBF: counter(param 1) += param 0
		counter(param(a.params, 1).stringValue).value += param(a.params, 0).intValue;
		return true;
	case ACT_DECREMENT_COUNTER: // RW 0x608F04
		counter(param(a.params, 1).stringValue).value -= param(a.params, 0).intValue;
		return true;
	case ACT_STOP_TIMER: // RW 0x609191
		counter(param(a.params, 0).stringValue).countdown = false;
		return true;
	case ACT_RESTART_TIMER: // RW 0x6091C6
	{
		Counter &c = counter(param(a.params, 0).stringValue);
		if (c.value > 0)
		{
			c.countdown = true;
		}
		return true;
	}
	case ACT_ADD_TO_MSEC_TIMER: adjustMsecTimer(a, true, true); return true;
	case ACT_SUB_FROM_MSEC_TIMER: adjustMsecTimer(a, true, false); return true;
	case ACT_SET_TREE_SWAY:
	case ACT_SET_ATTACK_PRIORITY_THING:
	case ACT_SET_ATTACK_PRIORITY_KIND_OF:
	case ACT_SET_DEFAULT_ATTACK_PRIORITY:
	case ACT_SET_COUNTER_TO_UNIT_THREAT:
	case ACT_SET_COUNTER_TO_TEAM_THREAT:
		noteUnportedAction(ScriptTemplates::action(a.resolved)->name); // engine-owned, not ported (S-1180)
		return true;
	default:
		if (a.resolved >= ACT_CAMERA_FADE_FIRST && a.resolved <= ACT_CAMERA_FADE_LAST)
		{
			ScriptActions::clientRequest(*this, a); // RW 0x6030C1: the screen fade (a client effect here, S-1182)
			return true;
		}
		return false;
	}
}

// RW 0x608B28
bool ScriptEngine::evaluateCounter(const ScriptCondition &c)
{
	const std::int32_t value = counter(param(c.params, 0).stringValue).value; // get or create
	return compareOp(param(c.params, 1).intValue, value, param(c.params, 2).intValue);
}

// RW 0x608F49
bool ScriptEngine::evaluateFlag(const ScriptCondition &c)
{
	const bool value = flag(param(c.params, 0).stringValue); // get or create
	if ((param(c.params, 1).intValue != 0) == value)
	{
		return true;
	}
	// the UI interaction list of this frame (+ 0x1A364, NAMEKEY of the flag name): the client's UI is not in the logic here (S-1182)
	return false;
}

// RW 0x60904D
bool ScriptEngine::evaluateTimer(const ScriptCondition &c)
{
	const Counter &t = counter(param(c.params, 0).stringValue);
	return t.countdown && t.value < 1;
}

// RW 0x609092 (action, milliseconds, random)
void ScriptEngine::setTimer(const ScriptActionRec &a, bool milliseconds, bool random)
{
	Counter &c = counter(param(a.params, 0).stringValue);
	std::int32_t value = 0;
	if (!milliseconds)
	{
		value = param(a.params, 1).intValue;
		if (random)
		{
			value = m_logic.random().getValue(value, param(a.params, 2).intValue, "ScriptEngine.cpp", 0x9EA);
		}
		c.milliseconds = false;
	}
	else
	{
		float seconds = param(a.params, 1).realValue;
		if (random)
		{
			// RW 0x6090F2 .. 0x609117: cvttss2si of both bounds, the integer draw, cvtsi2ss
			const int lo = SimMath::cvttss2si(seconds), hi = SimMath::cvttss2si(param(a.params, 2).realValue);
			seconds = SimMath::sseFromInt32(m_logic.random().getValue(lo, hi, "ScriptEngine.cpp", 0x9FC));
		}
		value = secondsToFrames(seconds);
		c.milliseconds = true;
	}
	c.value = value;
	c.countdown = true;
}

// RW 0x608D0B (action, random mode 0 none / 1 logic / 2 client, from a counter, seconds)
void ScriptEngine::setCounter(const ScriptActionRec &a, int randomMode, bool fromCounter, bool seconds)
{
	Counter &c = counter(param(a.params, 0).stringValue);
	if (fromCounter)
	{
		if (const Counter *src = findCounter(param(a.params, 1).stringValue))
		{
			c.value = src->value; // the milliseconds flag is left as it was (RW 0x608EB6)
			return;
		}
	}
	if (seconds)
	{
		float s = param(a.params, 1).realValue;
		if (randomMode == 1)
		{
			s = m_logic.random().getValueReal(s, param(a.params, 2).realValue, "ScriptEngine.cpp", 0xA36);
		}
		c.value = secondsToFrames(s);
		c.milliseconds = true;
		return;
	}
	std::int32_t v = param(a.params, 1).intValue;
	if (randomMode == 1)
	{
		v = m_logic.random().getValue(v, param(a.params, 2).intValue, "ScriptEngine.cpp", 0xA48);
	}
	else if (randomMode == 2)
	{
		// RW 0x6D32E4 is the CLIENT generator: a draw of it in the logic would split lockstep peers; the port keeps the low bound and reports it
		note("[S-1184] SET_COUNTER_TO_CLIENT_RANDOM_VALUE kept its low bound (retail draws the client generator)");
	}
	c.value = v;
	c.milliseconds = false;
}

// RW 0x609200 (action, milliseconds, add): param 1 is the timer, param 0 the amount
void ScriptEngine::adjustMsecTimer(const ScriptActionRec &a, bool milliseconds, bool add)
{
	Counter &c = counter(param(a.params, 1).stringValue);
	if (!milliseconds)
	{
		const std::int32_t v = param(a.params, 0).intValue;
		c.value += add ? v : -v;
		return;
	}
	float s = param(a.params, 0).realValue;
	if (!add)
	{
		s = SimMath::sseSub(0.0f, s); // RW 0x609278: fsubr from 0
	}
	c.value += secondsToFrames(s);
}

// RW 0x604ADD / 0x604BA1: the group of that name, then the script of that name (the team instance bookkeeping RW 0x7A4DDE for a condition team
// is part of S-1183)
void ScriptEngine::enableScript(const ScriptActionRec &a, bool enable)
{
	const std::string &name = param(a.params, 0).stringValue;
	if (enable)
	{
		if (RGroup *g = findGroup(name))
		{
			g->active = true;
		}
		if (RScript *s = findScript(name))
		{
			s->active = true;
		}
	}
	else
	{
		if (RScript *s = findScript(name))
		{
			s->active = false;
		}
		if (RGroup *g = findGroup(name))
		{
			g->active = false;
		}
	}
}

// RW 0x60BFBD
void ScriptEngine::callSubroutine(const ScriptActionRec &a)
{
	const std::string &name = param(a.params, 0).stringValue;
	if (++m_callDepth > 64)
	{
		--m_callDepth;
		note("CALL_SUBROUTINE recursion deeper than 64: " + name);
		return;
	}
	// RW 0x60BFBD: the lookups give back the side of the qualified name, and RW 0x604243 makes it the current side (+ 0x1A20C) around the call (restored
	// after it, RW 0x60428D): the called scripts read and write that side's counters and flags and find their names there; the current player stays
	const std::string calledSide = qualify(name).first;
	const std::string savedSide = m_currentSide;
	if (RGroup *g = findGroup(name))
	{
		if (!g->subroutine)
		{
			note("***Attempting to call script that is not a subroutine: " + name);
		}
		else if (g->active)
		{
			// the group's scripts (RW 0x60A377 on + 8: a subroutine SCRIPT inside is still skipped); its child groups are not run
			m_currentSide = calledSide;
			executeScripts(g->scripts);
			m_currentSide = savedSide;
		}
	}
	else if (RScript *s = findScript(name))
	{
		if (s->def->isSubroutine)
		{
			m_currentSide = calledSide;
			executeScript(*s);
			m_currentSide = savedSide;
		}
		else
		{
			note("***Attempting to call script that is not a subroutine: " + name);
		}
	}
	else
	{
		note("***Script not defined: " + name);
	}
	--m_callDepth;
}

// ---- lane CAMP-1H: the team scripts ------------------------------------------------------------------------------------------------------------
// TARGET FACTS (rotwk201_game.exe, caveat S-001); the donor is ZH Team::updateState / updateGenericScripts (Team.cpp) and ScriptEngine::runScript,
// RotWK differs where noted.

// RW 0x60BD42 (side, name, team): the side (the team prototype's owner, prototype + 0x10, from Team::updateState) is made current (RW 0x60BDAE ->
// RW 0x604243) before the name is looked up, so a plain name is that side's script
void ScriptEngine::runScript(const std::string &side, const std::string &name, Team *team)
{
	if (name.empty() || name == "<none>")
	{
		return;
	}
	if (++m_callDepth > 64)
	{
		--m_callDepth;
		note("team script recursion deeper than 64: " + name);
		return;
	}
	Team *const savedTeam = m_thisTeam;
	Player *const savedPlayer = m_currentPlayer;
	const std::uint32_t savedConditionTeam = m_conditionTeam;
	const std::string savedSide = m_currentSide;
	m_thisTeam = team;          // + 0x1A210
	m_conditionTeam = 0;        // + 0x1A218
	if (team)
	{
		m_currentPlayer = team->getControllingPlayer(); // + 0x1A230 (RW 0x79FD6F; a team without one: none)
	}
	else
	{
		m_currentPlayer = nullptr;
	}
	m_currentSide = side;
	const std::string calledSide = qualify(name).first;
	// RW 0x6049DD (a group of that name) first, then RW 0x604A5D (a script)
	if (RGroup *g = findGroup(name))
	{
		if (!g->subroutine)
		{
			note("***Attempting to call script that is not a subroutine: " + name);
		}
		else if (g->active)
		{
			m_currentSide = calledSide; // RW 0x604243 (+ 0x1A20C)
			executeScripts(g->scripts);
		}
	}
	else if (RScript *sc = findScript(name))
	{
		if (sc->def->isSubroutine)
		{
			m_currentSide = calledSide;
			executeScript(*sc);
		}
		else
		{
			note("***Attempting to call script that is not a subroutine: " + name);
		}
	}
	else
	{
		note("***Script not defined: " + name);
	}
	m_currentSide = savedSide;
	m_currentPlayer = savedPlayer;
	m_conditionTeam = savedConditionTeam;
	m_thisTeam = savedTeam;
	--m_callDepth;
}

void ScriptEngine::updateTeamStates()
{
	// RW 0x6A8541: the 20 slots (RW 0x6ABE5B per player: Player + 0x34C, the prototypes in the order the player received them; RW 0x7A676D each
	// prototype's instance list from its head). The port's prototypes are in creation order, which is the order a player received them
	PlayerList &players = m_logic.players();
	for (int i = 0; i < players.getPlayerCount(); ++i)
	{
		const Player *p = players.getNthPlayer(i);
		if (!p)
		{
			continue;
		}
		for (const auto &proto : players.teams().prototypes())
		{
			if (proto->getControllingPlayer() != p)
			{
				continue;
			}
			// the instance list from its head: the newest first (a team script may add instances: the ones made now are not visited in this pass)
			const std::vector<Team *> instances(proto->teams().rbegin(), proto->teams().rend());
			for (Team *t : instances)
			{
				updateTeamState(*t);
			}
		}
	}
}

namespace
{
// RW 0x7A208C's member count: a member whose contain is a horde (Object + 0x258, its slot 0x7C) counts its members (slot 0x180(0): the contain count;
// the field the horde slot adds, interface + 0x58, is taken as 0 as in HordeBanner.cpp), any other member counts 1
std::int32_t unitCount(const Object &o)
{
	const ContainModuleInterface *c = o.getContain();
	if (c && const_cast<ContainModuleInterface *>(c)->getHordeContainInterface())
	{
		return (std::int32_t)c->getContainCount();
	}
	return 1;
}

bool kindOf(const Object &o, int bit)
{
	return bit >= 0 && o.isKindOf((unsigned)bit);
}
} // namespace

// RW 0x7A208C Team::updateState
void ScriptEngine::updateTeamState(Team &t)
{
	TeamPrototype *proto = t.getPrototype();
	if (!proto)
	{
		return;
	}
	Team::ScriptState &st = t.scriptState();
	// (+ 0x5C, the entered / exited flag, is cleared here in retail; the port computes it from the members' enter / exit frames: S-1365)
	if (!st.active)
	{
		return;
	}
	const TeamTemplateScripts &ts = proto->templateScripts();
	const std::string ownerSide = proto->getControllingPlayer() ? proto->getControllingPlayer()->getPlayerName() : std::string(); // prototype + 0x10
	// the creation step (RW 0x7A20C8 .. 0x7A221B): once the team is not being built (+ 0x113) and ready (RW 0x7A09E4)
	if (st.created && !st.beingBuilt && teamReady(t))
	{
		st.created = false;
		st.ready = true;
		if (!ts.onCreate.empty())
		{
			++m_stats.teamScripts["OnCreate"];
		}
		runScript(ownerSide, ts.onCreate, &t); // + 0x1EC (TeamTemplateInfo + 0xC0)
		if (!ts.eventsList.empty())
		{
			// + 0x1F0: the event list (RW 0x7396E8) handed to every member with an AI (RW 0x662597): not ported (S-1365)
			note("[S-1365] teamEventsList '" + ts.eventsList + "' of team " + proto->getName() + " is not handed to the members");
		}
		if (!ts.onDestroyed.empty())
		{
			// + 0x208: the units now (every member, the dead too: RW 0x7A2188 .. 0x7A21C9) and the threshold (+ 0x20C real): RW 0x7A21CE .. 0x7A21E2
			// cvtsi2ss cur; mulss threshold * cur; subss cur - that; cvttss2si; clamped to [0, cur - 1]
			for (const Object *o = t.getFirstMember(); o; o = o->friend_teamNext())
			{
				st.curUnits += unitCount(*o);
			}
			const float cur = SimMath::sseFromInt32(st.curUnits);
			std::int32_t th = SimMath::cvttss2si(SimMath::sseSub(cur, SimMath::sseMul(ts.destroyedThreshold, cur)));
			if (th > st.curUnits - 1)
			{
				th = st.curUnits - 1;
			}
			if (th < 0)
			{
				th = 0;
			}
			st.destroyThreshold = th;
		}
		// RW 0x695003 per member (the object's upgrade list + 0x4B4 and veterancy + 0x4B8): the port's creators give them when they make the unit
	}
	if (!st.ready)
	{
		return;
	}
	if (!t.hadMembers() && t.getFirstMember()) // RW 0x7A2230: + 0x128 (RW 0x7A11FF: a first member)
	{
		t.setHadMembers();
	}
	// enemy sighted / all clear (RW 0x7A2252 ..): every 8th frame of this team ((team id ^ frame) & 7 == 0), a living member that sees an enemy
	if (st.checkEnemySighted && ((t.getID() ^ m_logic.getFrame()) & 7u) == 0)
	{
		st.prevSeeEnemy = st.seeEnemy;
		st.seeEnemy = false;
		static const int kInert = ObjectTemplateInfoBuilder::kindOfIndex("INERT"), kMoveOnly = ObjectTemplateInfoBuilder::kindOfIndex("MOVE_ONLY");
		for (Object *m = t.getFirstMember(); m; m = m->friend_teamNext())
		{
			if (m->isEffectivelyDead()) // + 0x458 bit 0
			{
				continue;
			}
			// ThePartitionManager's closest object (RW 0xA39090, 2D centres) within the member's vision range (RW 0x68E43B) with the filters RW 0xC1D66C
			// (canSee(o, -1) == true), RW 0x46E72F(0, 0x59, 0x86) (not INERT / MOVE_ONLY), RW 0xC0F374 (the same off-map status, Object + 0x458 bit 3:
			// INFERENCE: every object is on the map here), RW 0xC10E20 (alive), RW 0xC11DC0 (the member's relationship to it is ENEMIES)
			const float range = AIUpdateInterface::objectVisionRangeOf(*m);
			PartitionFilterFn filter([&](Object &o) {
				if (&o == m || o.isEffectivelyDead() || kindOf(o, kInert) || kindOf(o, kMoveOnly))
				{
					return false;
				}
				if (m->getRelationship(o) != ENEMIES)
				{
					return false;
				}
				return EmotionModules::canSeeObject(*m, o, range);
			});
			if (m_logic.partition().getClosestObject(*m->getPosition(), range, FROM_CENTER_2D, { &filter }))
			{
				st.seeEnemy = true;
				break;
			}
		}
		if (st.prevSeeEnemy != st.seeEnemy)
		{
			++m_stats.teamScripts[st.seeEnemy ? "EnemySighted" : "AllClear"];
			runScript(ownerSide, st.seeEnemy ? ts.enemySighted : ts.allClear, &t); // + 0xD0 / + 0xD4
		}
	}
	// the destroyed check (RW 0x7A22F0 ..): the living units now; a change at or under the threshold runs the script once
	if (!ts.onDestroyed.empty())
	{
		const std::int32_t prev = st.curUnits;
		st.curUnits = 0;
		for (const Object *o = t.getFirstMember(); o; o = o->friend_teamNext())
		{
			if (!o->isEffectivelyDead())
			{
				st.curUnits += unitCount(*o);
			}
		}
		if (st.curUnits != prev && st.curUnits <= st.destroyThreshold)
		{
			++m_stats.teamScripts["OnDestroyed"];
			runScript(ownerSide, ts.onDestroyed, &t); // + 0xDC
			st.destroyThreshold = -1;
		}
	}
	// the idle check (RW 0x7A2377 ..): every living member with an AI idle (AI vslot 0x1B8), twice in a row
	if (!ts.onIdle.empty())
	{
		bool idle = true, any = false;
		for (Object *o = t.getFirstMember(); o; o = o->friend_teamNext())
		{
			if (o->isEffectivelyDead())
			{
				continue;
			}
			if (const AIUpdateInterface *ai = o->getAIUpdateInterface())
			{
				any = true;
				if (!ai->isIdle())
				{
					idle = false;
				}
			}
		}
		if (any && idle && st.wasIdle)
		{
			++m_stats.teamScripts["OnIdle"];
			runScript(ownerSide, ts.onIdle, &t); // + 0xC8
		}
		st.wasIdle = idle;
	}
	// RW 0x7A23E4 ..: when every living member with an AI is SUPPORT (template + 0x123 bit 2), the team goes to its owner's default team (RW 0x7A12F4)
	{
		static const int kSupport = ObjectTemplateInfoBuilder::kindOfIndex("SUPPORT");
		bool any = false, other = false;
		for (Object *o = t.getFirstMember(); o; o = o->friend_teamNext())
		{
			if (!o->isEffectivelyDead() && o->getAIUpdateInterface())
			{
				any = true;
				if (!kindOf(*o, kSupport))
				{
					other = true;
					break;
				}
			}
		}
		if (any && !other)
		{
			handTeamToDefault(t);
		}
	}
	// NOT PORTED (S-1365): the attacker records' expiry (RW 0x7A1FB1 / 0x7A2004: + 0x140 after 900 frames, + 0x13C after 3000) and the team's vslot 0x14
	// (RW 0x7A59CB, a threat record)
}

// RW 0x7A09E4: a team flagged + 0x112 (built by an AIPlayer, not ported: never set here) waits until no member is mid-creation (RW 0x44DDEC(2)) and every
// HORDE member's horde has members and is complete (horde slots 0xA4 / 0x9C)
bool ScriptEngine::teamReady(const Team &t) const
{
	return !t.scriptState().wasBuilt;
}

// RW 0x7A12F4: a team with an owner gives its members to the owner's default team (RW 0x7A0F74, not to itself); without an owner (+ 8 of the prototype)
// every member leaves its team (RW 0x69954A(null)): not ported (the port's teams always have an owner; noted)
void ScriptEngine::handTeamToDefault(Team &t)
{
	Player *owner = t.getPrototype() ? t.getPrototype()->getControllingPlayer() : nullptr;
	if (!owner)
	{
		note("[S-1365] a SUPPORT-only team without an owner (RW 0x7A12F4's null branch) keeps its members");
		return;
	}
	Team *def = owner->getDefaultTeam();
	if (!def || def == &t)
	{
		return;
	}
	while (Object *o = t.getFirstMember())
	{
		o->setTeam(def); // RW 0x7A0F74 -> RW 0x69954A
	}
}

void ScriptEngine::updateGenericScripts(Player &p)
{
	// RW 0x6AF2FA .. 0x6AF32B: a copy of Player + 0x34C, each prototype's instances from the head
	for (const auto &proto : m_logic.players().teams().prototypes())
	{
		if (proto->getControllingPlayer() != &p)
		{
			continue;
		}
		const std::vector<Team *> instances(proto->teams().rbegin(), proto->teams().rend());
		for (Team *t : instances)
		{
			updateGenericScripts(*t);
		}
	}
}

// RW 0x7A267D Team::updateGenericScripts
void ScriptEngine::updateGenericScripts(Team &t)
{
	TeamPrototype *proto = t.getPrototype();
	Team::ScriptState &st = t.scriptState();
	if (!proto || !st.ready)
	{
		return;
	}
	// RW 0x7A1759: the 32 hooks looked up once by name (RW 0x758ECB: the prototype owner's side) and duplicated (RW 0x7B7F2B)
	auto it = m_genericScripts.find(proto->getID());
	if (it == m_genericScripts.end())
	{
		std::vector<GenericCopy> copies(32);
		const TeamTemplateScripts &ts = proto->templateScripts();
		const std::string owner = proto->getControllingPlayer() ? proto->getControllingPlayer()->getPlayerName() : std::string();
		for (int i = 0; i < 32; ++i)
		{
			if (ts.generic[i].empty())
			{
				continue;
			}
			const size_t slash = ts.generic[i].find('/');
			const std::string side = slash == std::string::npos ? owner : ts.generic[i].substr(0, slash);
			const std::string name = slash == std::string::npos ? ts.generic[i] : ts.generic[i].substr(slash + 1);
			RSide *rs = findSide(side);
			RScript *sc = rs ? findScriptIn(rs->root, name) : nullptr;
			if (!sc)
			{
				note("generic script not found: '" + ts.generic[i] + "' (team " + proto->getName() + ")");
				continue;
			}
			copies[(size_t)i].def = sc->def;
			copies[(size_t)i].side = side;
			copies[(size_t)i].active = sc->active; // the duplicate's + 0x40 (copied now)
		}
		it = m_genericScripts.emplace(proto->getID(), std::move(copies)).first;
	}
	const UnsignedInt frame = m_logic.getFrame();
	for (int i = 0; i < 32; ++i)
	{
		if (!st.attemptGeneric[i])
		{
			continue;
		}
		const GenericCopy &g = it->second[(size_t)i];
		if (!g.def || !g.active)
		{
			st.attemptGeneric[i] = false;
			continue;
		}
		// the script's DelayEvaluationSeconds (+ 0x20): evaluated again from + 0x90 + 4i on
		if (g.def->delayEvaluationSeconds >= 1 && frame < st.genericNextFrame[i])
		{
			continue;
		}
		const std::string savedSide = m_currentSide;
		m_currentSide = g.side; // RW 0x609D97 / 0x60D053: RW 0x604243 with the hook's side
		if (evaluateConditions(*g.def, &t)) // RW 0x609D97(side, script, team, 0)
		{
			if (g.def->isOneShot) // + 0x29
			{
				st.attemptGeneric[i] = false;
			}
			if (!g.def->actionsFireSequentially) // + 0x10
			{
				// RW 0x60D053: the actions with + 0x1A210 = the team and its controlling player current
				Team *const savedTeam = m_thisTeam;
				Player *const savedPlayer = m_currentPlayer;
				m_thisTeam = &t;
				m_currentPlayer = t.getControllingPlayer();
				RScript tmp;
				tmp.def = g.def;
				tmp.sideName = g.side;
				tmp.active = true;
				executeActions(g.def->actions, tmp);
				m_thisTeam = savedTeam;
				m_currentPlayer = savedPlayer;
				++m_stats.genericScriptsFired;
			}
			else if (g.def->isOneShot)
			{
				// RW 0x7A2830 ..: a sequential one-shot hook becomes a sequential record of this team (RW 0x604121 / 0x606FDA); a sequential hook that is
				// not one-shot runs nothing (RW's else-if)
				SequentialScript r;
				r.team = t.getID();
				r.side = g.side;
				r.scriptName = g.def->name;
				r.script = g.def;
				r.loopCount = g.def->loopCount; // + 0x1C = script + 0x14
				appendSequentialScript(r);
				++m_stats.genericScriptsFired;
			}
		}
		m_currentSide = savedSide;
		if (g.def->delayEvaluationSeconds > 0)
		{
			st.genericNextFrame[i] = (UnsignedInt)(5 * g.def->delayEvaluationSeconds) + frame; // [0xD9F608] * + 0x20 + frame
		}
	}
}

// ---- output -------------------------------------------------------------------------------------------------------------------------------------

void ScriptEngine::addClientRequest(ScriptClientRequest r)
{
	r.frame = m_logic.getFrame();
	r.playerIndex = m_currentPlayer ? m_currentPlayer->getPlayerIndex() : -1;
	r.script = m_currentScript ? m_currentScript->def->name : std::string();
	++m_stats.clientRequests[r.action];
	m_clientRequests.push_back(std::move(r));
}

std::vector<ScriptClientRequest> ScriptEngine::takeClientRequests()
{
	std::vector<ScriptClientRequest> out;
	out.swap(m_clientRequests);
	return out;
}

void ScriptEngine::addEndRequest(bool victory, const std::string &action)
{
	EndRequest e;
	e.frame = m_logic.getFrame();
	e.playerIndex = m_currentPlayer ? m_currentPlayer->getPlayerIndex() : -1;
	e.victory = victory;
	e.action = action;
	m_endRequests.push_back(e);
}

// ---- the state hash -----------------------------------------------------------------------------------------------------------------------------

namespace
{
void crcGroup(StateHasher &h, const ScriptEngine::RGroup &g)
{
	h.addBool(g.active);
	h.addU32((std::uint32_t)g.scripts.size());
	for (const auto &s : g.scripts)
	{
		h.addBool(s->active);
		h.addU32(s->frameToEvaluateAt);
	}
	h.addU32((std::uint32_t)g.groups.size());
	for (const auto &c : g.groups)
	{
		crcGroup(h, *c);
	}
}
} // namespace

void ScriptEngine::crc(StateHasher &h) const
{
	h.addBool(m_loaded);
	h.addBool(m_firstUpdate);
	h.addI32(m_gameDifficulty);
	h.addBool(m_objectsReceiveDifficultyBonus); // lane CAMP-1H
	// lane CAMP-1H: the prototypes' generic script copies made so far (when, and with which active flag, is state)
	h.addU32((std::uint32_t)m_genericScripts.size());
	for (const auto &kv : m_genericScripts)
	{
		h.addI32(kv.first);
		for (const GenericCopy &g : kv.second)
		{
			h.addBool(g.def != nullptr);
			h.addBool(g.active);
		}
	}
	h.addU32((std::uint32_t)m_sides.size());
	for (const RSide &s : m_sides)
	{
		crcGroup(h, s.root);
	}
	h.addU32((std::uint32_t)m_counters.size());
	for (const auto &kv : m_counters)
	{
		h.addString(kv.first.first);
		h.addString(kv.first.second);
		h.addI32(kv.second.value);
		h.addBool(kv.second.countdown);
		h.addBool(kv.second.milliseconds);
	}
	h.addU32((std::uint32_t)m_flags.size());
	for (const auto &kv : m_flags)
	{
		h.addString(kv.first.first);
		h.addString(kv.first.second);
		h.addBool(kv.second);
	}
	h.addU32((std::uint32_t)m_objectLists.size());
	for (const auto &l : m_objectLists)
	{
		h.addString(l.first);
		h.addU32((std::uint32_t)l.second.size());
		for (const std::string &t : l.second)
		{
			h.addString(t);
		}
	}
	h.addU32((std::uint32_t)m_namedObjects.size());
	for (const auto &e : m_namedObjects)
	{
		h.addString(e.first);
		h.addU32(e.second);
	}
	if (!m_sequential.empty()) // lane SCRIPT-2: hashed once a sequential script exists
	{
		h.addU32(0x5E0u | ((std::uint32_t)m_sequential.size() << 12));
		for (const auto &chain : m_sequential)
		{
			h.addU32((std::uint32_t)chain.size());
			for (const SequentialScript &r : chain)
			{
				h.addU32(r.team);
				h.addU32(r.object);
				h.addString(r.side);
				h.addString(r.scriptName);
				h.addI32(r.index);
				h.addI32(r.loopCount);
				h.addI32(r.framesToWait);
				h.addBool(r.again);
			}
		}
	}
	for (const AcquiredScienceQueue &q : m_acquiredSciences) // lane AUDIO-4: every queue (review r1)
	{
		q.crc(h);
	}
	if (!m_audioTimers.empty()) // lane SCRIPT-3: hashed once a timer exists
	{
		h.addU32(0x5E1u | ((std::uint32_t)m_audioTimers.size() << 12));
		for (const AudioTimer &t : m_audioTimers)
		{
			h.addString(t.name);
			h.addU32(t.endFrame);
		}
	}
}

// ---- lane SCRIPT-3: HAS_FINISHED_AUDIO ----------------------------------------------------------------------------------------------------------

bool ScriptEngine::hasFinishedAudio(const std::string &name, bool consume)
{
	const UnsignedInt frame = m_logic.getFrame();
	auto it = std::find_if(m_audioTimers.begin(), m_audioTimers.end(), [&](const AudioTimer &t) { return t.name == name; });
	if (it == m_audioTimers.end())
	{
		std::int32_t ms = 0;
		bool picked = false;
		if (!m_host || !m_host->audioLengthMs(name, ms, picked))
		{
			note(m_host ? "HAS_FINISHED_AUDIO: an event without audio info is finished (RW 0x759BDA)"
						: "[S-1187] HAS_FINISHED_AUDIO: no audio length model (no host): finished");
			return true;
		}
		if (picked)
		{
			note("[S-1187] HAS_FINISHED_AUDIO: a multi-file sound's length is its first file's (retail draws the audio generator)");
		}
		// RW 0x759C2B: fdiv by the ms per logic frame (200.0, RW 0xD9F614), _ftol (truncation) of a non-negative whole number of ms
		AudioTimer t;
		t.name = name;
		t.endFrame = frame + (UnsignedInt)(ms / 200);
		m_audioTimers.push_back(t);
		it = m_audioTimers.end() - 1;
	}
	if (frame < it->endFrame)
	{
		return false;
	}
	if (consume)
	{
		m_audioTimers.erase(it); // RW 0x702055
	}
	return true;
}

// ---- reports ------------------------------------------------------------------------------------------------------------------------------------

std::vector<std::string> ScriptEngine::report() const
{
	std::vector<std::string> out;
	for (const auto &kv : m_stats.unportedConditions)
	{
		out.push_back("[S-1180] script condition not ported (evaluated false): " + kv.first + " x" + std::to_string(kv.second));
	}
	for (const auto &kv : m_stats.unportedActions)
	{
		out.push_back("[S-1180] script action not ported (did nothing): " + kv.first + " x" + std::to_string(kv.second));
	}
	for (const auto &kv : m_stats.notes)
	{
		out.push_back("script engine: " + kv.first + " x" + std::to_string(kv.second));
	}
	return out;
}

std::vector<std::string> ScriptEngine::stopLines()
{
	return {
		"[S-1180] map scripts: the conditions and actions without a port here evaluate false / do nothing and are counted by name (ScriptEngine::stats)",
		"[S-1181] map scripts: a side runs its own list followed by its PlayerAIType library (PlayerAITypes.ini LibraryMap) and its LibraryMaps, merged in "
		"load order; in a skirmish every human slot side runs Multiplayer_Human and the map's Player_N sides are dropped (inference); the multiplayer victory / "
		"defeat conditions and, in a LAN / Internet game, the <Local Player...> parameters answer for the current script player (lockstep)",
		"[S-1182] map scripts: camera, UI, audio, input, fade and end-screen actions are recorded as client requests; the client applies the camera moves / "
		"resets / rotations / follow / zoom / pitch / look-toward, letterbox, captions, fades, notifications, objectives and (lane SCRIPT-2) the sounds, speech "
		"and music tracks, not the rest; the RotWK camera controller's ease (W3DView + 0x280) is the ZH ParabolicEase here; the conditions that read the "
		"client (CAMERA_MOVEMENT_FINISHED, NAMED_SELECTED ...) are unported and false, and FLAG's UI-interaction fallback is not implemented",
		"[S-1183] map scripts: a script with a condition team does not run once per team member",
		"[S-1184] map scripts: SET_COUNTER_TO_CLIENT_RANDOM_VALUE keeps its low bound (retail draws the client generator RW 0x6D32E4 inside the logic)",
		"[S-1185] map scripts: the War of the Ring per-player gate and RW 0x862535 are not ported (lane CAMP-1H: the team states, RW 0x6A8541, run); sequential scripts run "
		"(lane SCRIPT-2) without the wait actions' sequential interface (they pass at once) and the AI busy flag + 0x3CA",
		"[S-1186] map scripts: the AI orders without an AI state here run as stand-ins: the attack-follow waypoint orders march (attack move) to the end of "
		"the path along each waypoint's first link, the face orders turn at once; NAMED_ATTACK_TEAM and the AI recruiting flag are not ported, nor a group "
		"attack's firing passengers and giant birds (lane SCRIPT-3: hunt, attitude, the group attack and the waypoint path states are ported, S-1188 / S-1189)",
		"[S-1187] map scripts: HAS_FINISHED_AUDIO times an event by a logic-side length model (RW 0x759B84 / 0x4541FF: attack + main + decay file lengths "
		"from the first query); a multi-file sound list contributes its first file where retail draws the audio generator",
		huntStopLine(),
		waypointPathStopLine(),
	};
}
