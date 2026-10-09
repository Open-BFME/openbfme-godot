// OpenBFME. GPL-3.0. See MusicScripts.h.

#include "GameClient/MusicScripts.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/Audio/GameAudio.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Science.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/Eva.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/Scripts.h"

#include <algorithm>
#include <cmath>

// Common/ModelState.h cannot be included next to Object.h (two ModelConditionFlags typedefs); the one function used is declared here.
namespace ModelCondition
{
int indexOf(const std::string &name);
}

namespace
{
// BFME2 condition ordinals (Open-BFME-2 ScriptEngine_initConditionTemplates.cpp)
enum
{
	C_COUNTER = 1,
	C_FLAG = 2,
	C_CONDITION_TRUE = 3,
	C_TIMER_EXPIRED = 4,
	C_SKIRMISH_PLAYER_FACTION = 87,
	C_PLAYER_ACQUIRED_SCIENCE = 100,
	C_PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION = 160,
	C_PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT = 161,
	C_PLAYER_HAS_REACHED_LEVEL_CAP = 173,
	C_HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS = 186,
	C_NUM_UNITS_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION = 187
};
// BFME2 action ordinals (Open-BFME-2 ScriptEngine_initActionTemplates.cpp)
enum
{
	A_SET_FLAG = 1,
	A_SET_COUNTER = 2,
	A_ENABLE_SCRIPT = 8,
	A_DISABLE_SCRIPT = 9,
	A_CALL_SUBROUTINE = 10,
	A_INCREMENT_COUNTER = 15,
	A_SET_MILLISECOND_TIMER = 20,
	A_SET_RANDOM_MSEC_TIMER = 151,
	A_MUSIC_SCRIPT_SET_TRACK = 456,
	A_MUSIC_SCRIPT_POP_MUSIC = 458,
	A_MUSIC_SCRIPT_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY = 476,
	A_MUSIC_SCRIPT_PUSH_TRACK_FINITE_TIMES_AND_NOTIFY = 478,
	A_FIND_HOME_BASE_OF_PLAYER = 488,
	A_SET_COUNTER_TO_CLIENT_RANDOM_VALUE = 508
};

const ScriptParameter &param(const std::vector<ScriptParameter> &p, size_t i)
{
	static const ScriptParameter kNone;
	return i < p.size() ? p[i] : kNone;
}

// ZH ConvertDurationFromMsecsToFrames + REAL_TO_INT_CEIL: seconds to logic frames (5 per second). Client time (the music timers), not simulation
int secondsToFrames(float seconds)
{
	return (int)std::ceil((double)seconds * 1000.0 * (double)LOGICFRAMES_PER_SECOND / 1000.0);
}
} // namespace

struct MusicScripts::Node
{
	const Script *script = nullptr;     // exactly one of script / group
	const ScriptGroup *group = nullptr;
	std::string name;
	bool active = false;
	bool subroutine = false;
	unsigned frameToEvaluate = 0;
	std::vector<std::unique_ptr<Node>> children;
};

MusicScripts::MusicScripts(AudioManager &audio, GameLogic &logic, PlayerList &players, Eva *eva)
	: m_audio(audio)
	, m_logic(logic)
	, m_players(players)
	, m_eva(eva)
{
}

MusicScripts::~MusicScripts() = default;

bool MusicScripts::load(ArchiveFileSystem &fs, const std::string &mapPath, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile(mapPath, bytes, error))
	{
		return false;
	}
	auto map = std::make_unique<LoadedMap>();
	MapReadOptions options;
	if (!MapReader::load(bytes, mapPath, options, *map, error))
	{
		return false;
	}
	if (!map->hasPlayerScripts)
	{
		if (error)
		{
			*error = "the music script map " + mapPath + " has no PlayerScriptsList";
		}
		return false;
	}
	m_source = std::move(map);
	loadScripts(m_source->playerScripts);
	return true;
}

void MusicScripts::build(const std::vector<ScriptItem> &items, std::vector<std::unique_ptr<Node>> &out)
{
	for (const ScriptItem &it : items)
	{
		auto n = std::make_unique<Node>();
		if (it.script)
		{
			n->script = it.script.get();
			n->name = it.script->name;
			n->active = it.script->isActive;
			n->subroutine = it.script->isSubroutine;
		}
		else if (it.group)
		{
			n->group = it.group.get();
			n->name = it.group->name;
			n->active = it.group->isActive;
			n->subroutine = it.group->isSubroutine;
			build(it.group->items, n->children);
		}
		out.push_back(std::move(n));
	}
}

void MusicScripts::loadScripts(const PlayerScriptsList &scripts)
{
	m_lists.clear();
	for (const ScriptList &list : scripts.lists)
	{
		auto root = std::make_unique<Node>();
		root->name = "<list>";
		root->active = true;
		build(list.items, root->children);
		m_lists.push_back(std::move(root));
	}
}

size_t MusicScripts::scriptCount() const
{
	size_t n = 0;
	std::vector<const Node *> todo;
	for (const auto &l : m_lists)
	{
		todo.push_back(l.get());
	}
	while (!todo.empty())
	{
		const Node *x = todo.back();
		todo.pop_back();
		n += x->script ? 1 : 0;
		for (const auto &c : x->children)
		{
			todo.push_back(c.get());
		}
	}
	return n;
}

MusicScripts::Node *MusicScripts::findIn(std::vector<std::unique_ptr<Node>> &list, const std::string &name)
{
	for (auto &n : list)
	{
		if (n->name == name && n.get()->name != "<list>")
		{
			return n.get();
		}
		if (Node *f = findIn(n->children, name))
		{
			return f;
		}
	}
	return nullptr;
}

MusicScripts::Node *MusicScripts::find(const std::string &name)
{
	return findIn(m_lists, name);
}

void MusicScripts::unported(const char *kind, int type, const std::string &note)
{
	++m_stats.unported[std::string(kind) + " " + std::to_string(type) + (note.empty() ? "" : " " + note)];
}

void MusicScripts::update(unsigned frame)
{
	m_frame = frame;
	// ZH ScriptEngine::update 5503: the countdown timers first, then the scripts
	for (auto &kv : m_counters)
	{
		if (kv.second.isCountdownTimer && kv.second.value >= 0)
		{
			--kv.second.value;
		}
	}
	for (auto &root : m_lists)
	{
		executeList(root->children);
	}
	updateMusic();
}

void MusicScripts::executeList(std::vector<std::unique_ptr<Node>> &list)
{
	for (auto &n : list)
	{
		if (n->group)
		{
			// active, non subroutine groups run their scripts (ZH update: inactive groups are skipped)
			if (n->active && !n->subroutine)
			{
				executeList(n->children);
			}
			continue;
		}
		if (n->subroutine)
		{
			continue; // ZH executeScripts: subroutines run only when called
		}
		executeScript(*n);
	}
}

void MusicScripts::executeScript(Node &n)
{
	// ZH ScriptEngine::executeScript 6950. The difficulty filter: the music scripts are marked for all three difficulties (easy / normal / hard = 1)
	if (!n.active || !n.script)
	{
		return;
	}
	const Script &s = *n.script;
	if (!s.normal)
	{
		return; // INFERENCE: the local player's difficulty is taken as normal (every retail music script has all three flags)
	}
	if (m_frame < n.frameToEvaluate)
	{
		return;
	}
	if (s.delayEvaluationSeconds > 0)
	{
		n.frameToEvaluate = m_frame + (unsigned)s.delayEvaluationSeconds * LOGICFRAMES_PER_SECOND;
	}
	++m_stats.scriptsRun;
	if (evaluate(n))
	{
		executeActions(s.actions);
		if (s.isOneShot)
		{
			n.active = false;
		}
	}
	else if (!s.falseActions.empty())
	{
		executeActions(s.falseActions);
		if (s.isOneShot)
		{
			n.active = false;
		}
	}
}

bool MusicScripts::evaluate(Node &n)
{
	// ZH evaluateConditions 7582: OR of AND lists; an empty AND list is skipped; no OR list at all is false
	for (const OrCondition &orc : n.script->orConditions)
	{
		if (orc.conditions.empty())
		{
			continue;
		}
		bool all = true;
		for (const ScriptCondition &c : orc.conditions)
		{
			if (!evaluateCondition(c))
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

bool MusicScripts::compare(int value, int op, int against)
{
	// ZH Parameter: LESS_THAN, LESS_EQUAL, EQUAL, GREATER_EQUAL, GREATER, NOT_EQUAL
	switch (op)
	{
		case 0: return value < against;
		case 1: return value <= against;
		case 2: return value == against;
		case 3: return value >= against;
		case 4: return value > against;
		case 5: return value != against;
		default: return false;
	}
}

std::vector<int> MusicScripts::players(const std::string &p) const
{
	std::vector<int> out;
	const Player *local = m_players.getLocalPlayer();
	if (p == "<Local Player>")
	{
		if (local)
		{
			out.push_back(local->getPlayerIndex());
		}
	}
	else if (p == "<All Players>")
	{
		for (int i = 0; i < m_players.getPlayerCount(); ++i)
		{
			out.push_back(i);
		}
	}
	else if (p == "<Local Player's Enemies>")
	{
		for (int i = 0; local && i < m_players.getPlayerCount(); ++i)
		{
			const Player *pl = m_players.getNthPlayer(i);
			if (pl && pl != local && pl->getRelationship(local) == ENEMIES)
			{
				out.push_back(i);
			}
		}
	}
	else
	{
		for (int i = 0; i < m_players.getPlayerCount(); ++i)
		{
			const Player *pl = m_players.getNthPlayer(i);
			if (pl && AsciiStringUtil::compareNoCase(pl->getPlayerName(), p) == 0)
			{
				out.push_back(i);
			}
		}
	}
	return out;
}

int MusicScripts::countUnitsNear(const std::vector<int> &who, const Coord3D &where, float distance) const
{
	// INFERENCE (S-710): "units" = living objects of the players that are not structures, distance in 2D
	static const int structure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	int n = 0;
	const float d2 = distance * distance;
	for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
	{
		const Player *pl = o->getControllingPlayer();
		if (!pl || o->isDestroyed() || o->isEffectivelyDead() || (structure >= 0 && o->isKindOf((unsigned)structure)))
		{
			continue;
		}
		bool mine = false;
		for (int i : who)
		{
			mine |= pl->getPlayerIndex() == i;
		}
		if (!mine)
		{
			continue;
		}
		const float dx = o->getPosition()->x - where.x, dy = o->getPosition()->y - where.y;
		n += dx * dx + dy * dy <= d2 ? 1 : 0;
	}
	return n;
}

bool MusicScripts::evaluateCondition(const ScriptCondition &c)
{
	const auto &p = c.params;
	switch (c.type)
	{
		case C_COUNTER:
			return compare(m_counters[param(p, 0).stringValue].value, param(p, 1).intValue, param(p, 2).intValue);
		case C_FLAG:
			return m_flags[param(p, 0).stringValue] == (param(p, 1).intValue != 0);
		case C_CONDITION_TRUE:
			return true;
		case C_TIMER_EXPIRED:
		{
			const Counter &t = m_counters[param(p, 0).stringValue];
			return t.isCountdownTimer && t.value < 1; // ZH evaluateTimer: timers count down to -1
		}
		case C_SKIRMISH_PLAYER_FACTION:
		{
			for (int i : players(param(p, 0).stringValue))
			{
				const Player *pl = m_players.getNthPlayer(i);
				if (pl && AsciiStringUtil::compareNoCase(pl->getSide(), param(p, 1).stringValue) == 0)
				{
					return true;
				}
			}
			return false;
		}
		case C_PLAYER_HAS_NUMBER_OBJECTS_WITH_MODELCONDITION:
		{
			const int bit = ModelCondition::indexOf(param(p, 1).stringValue);
			const std::vector<int> who = players(param(p, 0).stringValue);
			int n = 0;
			for (Object *o = m_logic.getFirstObject(); bit >= 0 && o; o = o->getNextObject())
			{
				const Player *pl = o->getControllingPlayer();
				if (!pl || o->isDestroyed() || !o->testModelCondition(bit))
				{
					continue;
				}
				for (int i : who)
				{
					n += pl->getPlayerIndex() == i ? 1 : 0;
				}
			}
			return compare(n, param(p, 2).intValue, param(p, 3).intValue);
		}
		case C_PLAYER_HAS_NUMBER_UNITS_DISTANCE_FROM_OBJECT:
		{
			// (player, comparison, count, distance, named object): units at least `distance` away from the object (the explore rule: "units have
			// moved out of the starting castle"). INFERENCE (S-710): "distance from" counts the units farther than the distance
			const auto it = m_named.find(param(p, 4).stringValue);
			const Object *base = it == m_named.end() ? nullptr : m_logic.findObjectByID(it->second);
			if (!base)
			{
				return false;
			}
			const std::vector<int> who = players(param(p, 0).stringValue);
			int all = 0;
			static const int structure = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
			for (Object *o = m_logic.getFirstObject(); o; o = o->getNextObject())
			{
				const Player *pl = o->getControllingPlayer();
				if (!pl || o->isDestroyed() || o->isEffectivelyDead() || (structure >= 0 && o->isKindOf((unsigned)structure)))
				{
					continue;
				}
				for (int i : who)
				{
					all += pl->getPlayerIndex() == i ? 1 : 0;
				}
			}
			const int nearCount = countUnitsNear(who, *base->getPosition(), param(p, 3).realValue);
			return compare(all - nearCount, param(p, 1).intValue, param(p, 2).intValue);
		}
		case C_HAS_EVA_EVENT_PLAYED_IN_LAST_N_SECONDS:
		{
			if (!m_eva)
			{
				unported("condition", c.type, "(no Eva installed)");
				return false;
			}
			return m_eva->playedRecently(m_eva->findEvent(param(p, 0).stringValue), (double)param(p, 1).realValue * 1000.0, m_audio.nowMs());
		}
		case C_NUM_UNITS_NEAR_EVA_EVENT_LAST_PLAYED_LOCATION:
		{
			Coord3D where;
			if (!m_eva || !m_eva->lastPlayedPosition(m_eva->findEvent(param(p, 2).stringValue), &where))
			{
				return compare(0, param(p, 3).intValue, param(p, 4).intValue);
			}
			return compare(countUnitsNear(players(param(p, 0).stringValue), where, param(p, 1).realValue), param(p, 3).intValue, param(p, 4).intValue);
		}
		case C_PLAYER_ACQUIRED_SCIENCE:
		{
			// lane AUDIO-4 (QA-1 U20): RW 0x7E4FE0 (the case of ordinal 100, RW 0x7EC383): the science by name (RW 0x5FEF8F; unknown: false), then the
			// first player of the parameter whose acquired-science queue holds it (RW 0x759646(index, science, consume = 1)). The queue is the music's
			// own (see m_acquiredSciences): retail's music scripts share one queue with the map's scripts (S-711)
			const ScienceType st = TheScienceStore ? TheScienceStore->getScienceFromInternalName(param(p, 1).stringValue) : SCIENCE_INVALID;
			if (st == SCIENCE_INVALID)
			{
				return false;
			}
			for (int i : players(param(p, 0).stringValue))
			{
				const Player *pl = m_players.getNthPlayer(i);
				if (pl && i < (int)m_acquiredSciences.size() && m_acquiredSciences[(size_t)i].didAcquire(pl->science(), st, true))
				{
					return true;
				}
			}
			return false;
		}
		case C_PLAYER_HAS_REACHED_LEVEL_CAP:
			// lane AUDIO-4 (QA-1 U20): RW 0x7E50F6 (ordinal 173, RW 0x7EC3F5): any player of the parameter whose rank (Player + 0x1C) is at least its
			// max rank (RW 0x7827C8)
			for (int i : players(param(p, 0).stringValue))
			{
				const Player *pl = m_players.getNthPlayer(i);
				if (pl && pl->science().getRankLevel() >= pl->science().getMaxRankLevel())
				{
					return true;
				}
			}
			return false;
		default:
			unported("condition", c.type, c.internalName);
			return false;
	}
}

void MusicScripts::executeActions(const std::vector<ScriptActionRec> &actions)
{
	for (const ScriptActionRec &a : actions)
	{
		executeAction(a);
	}
}

void MusicScripts::executeAction(const ScriptActionRec &a)
{
	++m_stats.actionsRun;
	const auto &p = a.params;
	switch (a.type)
	{
		case A_SET_FLAG:
			m_flags[param(p, 0).stringValue] = param(p, 1).intValue != 0;
			break;
		case A_SET_COUNTER:
			m_counters[param(p, 0).stringValue].value = param(p, 1).intValue;
			break;
		case A_INCREMENT_COUNTER:
			m_counters[param(p, 1).stringValue].value += param(p, 0).intValue;
			break;
		case A_ENABLE_SCRIPT:
		case A_DISABLE_SCRIPT:
			if (Node *n = find(param(p, 0).stringValue))
			{
				n->active = a.type == A_ENABLE_SCRIPT;
			}
			break;
		case A_CALL_SUBROUTINE:
			// ZH callSubroutine 6853: a subroutine group runs its scripts when active, a subroutine script runs through executeScript
			if (Node *n = find(param(p, 0).stringValue))
			{
				if (n->group && n->subroutine && n->active)
				{
					for (auto &c : n->children)
					{
						if (c->script && !c->subroutine)
						{
							executeScript(*c);
						}
					}
				}
				else if (n->script && n->subroutine)
				{
					executeScript(*n);
				}
			}
			break;
		case A_SET_MILLISECOND_TIMER:
		case A_SET_RANDOM_MSEC_TIMER:
		{
			// ZH setTimer 6738 (seconds; the random form draws between the two values: retail's GameLogicRandomValue, here the audio stream, S-711)
			float seconds = param(p, 1).realValue;
			if (a.type == A_SET_RANDOM_MSEC_TIMER)
			{
				seconds = m_audio.audioRandom().getValueReal(seconds, param(p, 2).realValue, __FILE__, __LINE__);
			}
			Counter &t = m_counters[param(p, 0).stringValue];
			t.value = secondsToFrames(seconds);
			t.isCountdownTimer = true;
			break;
		}
		case A_SET_COUNTER_TO_CLIENT_RANDOM_VALUE:
			m_counters[param(p, 0).stringValue].value = m_audio.audioRandom().getValue(param(p, 1).intValue, param(p, 2).intValue, __FILE__, __LINE__);
			break;
		case A_FIND_HOME_BASE_OF_PLAYER:
		{
			// (player, name, ?): names the player's home base. INFERENCE (S-710): the player's first living object of KindOf COMMANDCENTER (the fortress /
			// castle center), else nothing is named
			static const int cc = ObjectTemplateInfoBuilder::kindOfIndex("COMMANDCENTER");
			const std::vector<int> who = players(param(p, 0).stringValue);
			for (Object *o = m_logic.getFirstObject(); o && cc >= 0; o = o->getNextObject())
			{
				const Player *pl = o->getControllingPlayer();
				if (pl && !who.empty() && pl->getPlayerIndex() == who.front() && !o->isDestroyed() && o->isKindOf((unsigned)cc))
				{
					m_named[param(p, 1).stringValue] = o->getID();
					break;
				}
			}
			break;
		}
		case A_MUSIC_SCRIPT_SET_TRACK:
		{
			// (track, fadeout, fadein): the level 0 track, played until something else replaces it
			stopCurrent(param(p, 1).intValue != 0);
			m_stack.clear();
			Track t;
			t.event = param(p, 0).stringValue;
			t.remaining = -1;
			m_stack.push_back(t);
			startTrack(m_stack.back(), false);
			break;
		}
		case A_MUSIC_SCRIPT_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY:
		case A_MUSIC_SCRIPT_PUSH_TRACK_FINITE_TIMES_AND_NOTIFY:
		{
			// (track, times, fadeout, fadein, flag): play replaces the bottom of the stack, push goes on top; after `times` plays the flag becomes true
			// (the push "does NOT pop the music off the stack upon completion": the script pops it)
			Track t;
			t.event = param(p, 0).stringValue;
			t.remaining = std::max(1, param(p, 1).intValue);
			t.notifyFlag = param(p, 4).stringValue;
			stopCurrent(param(p, 2).intValue != 0);
			if (a.type == A_MUSIC_SCRIPT_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY)
			{
				if (m_stack.empty())
				{
					m_stack.push_back(t);
				}
				else
				{
					m_stack.front() = t;
				}
				if (m_stack.size() == 1)
				{
					startTrack(m_stack.front(), false);
				}
			}
			else
			{
				m_stack.push_back(t);
				startTrack(m_stack.back(), false);
			}
			break;
		}
		case A_MUSIC_SCRIPT_POP_MUSIC:
		{
			// (fadeout, fadein): the top track ends, the one below starts again (INFERENCE S-710: from its beginning; retail's stream position is not kept here)
			if (m_stack.empty())
			{
				break;
			}
			stopCurrent(param(p, 0).intValue != 0);
			m_stack.pop_back();
			if (!m_stack.empty())
			{
				startTrack(m_stack.back(), false);
			}
			break;
		}
		default:
			unported("action", a.type, a.internalName);
			break;
	}
}

void MusicScripts::stopCurrent(bool fade)
{
	if (!m_stack.empty() && m_stack.back().started)
	{
		m_stack.back().started = false;
		m_stack.back().handle = 0;
	}
	m_audio.stopMusic(fade);
}

void MusicScripts::startTrack(Track &t, bool)
{
	// a finite track (476 / 478) plays once per request and this class repeats it and notifies; SET_TRACK's continuous track keeps the backend loop
	const AudioHandle h = t.remaining < 0 ? m_audio.playMusic(t.event) : m_audio.playMusicOnce(t.event);
	t.handle = h;
	t.started = false;
	t.startedFrame = m_frame;
	if (h < AHSV_FirstHandle)
	{
		++m_stats.trackFailures;
		t.handle = 0;
		return;
	}
	m_lastTrack = t.event;
	++m_stats.tracksStarted;
}

void MusicScripts::updateMusic()
{
	if (m_stack.empty())
	{
		return;
	}
	Track &t = m_stack.back();
	if (!t.handle)
	{
		return;
	}
	const bool playing = m_audio.isCurrentlyPlaying(t.handle) || m_audio.isMusicPlaying();
	if (playing)
	{
		t.started = true;
		return;
	}
	if (!t.started && m_frame - t.startedFrame < 10u * LOGICFRAMES_PER_SECOND)
	{
		return; // queued, not yet audible (a stream start waits for its request and decode)
	}
	// the track ended (or never became audible within 10 s: counted as a completion, the failure is in the audio report)
	++m_stats.tracksCompleted;
	t.handle = 0;
	t.started = false;
	if (t.remaining > 0)
	{
		--t.remaining;
	}
	if (t.remaining != 0)
	{
		startTrack(t, false);
		return;
	}
	if (!t.notifyFlag.empty())
	{
		m_flags[t.notifyFlag] = true;
	}
}

bool MusicScripts::flag(const std::string &name) const
{
	const auto it = m_flags.find(name);
	return it != m_flags.end() && it->second;
}

int MusicScripts::counter(const std::string &name) const
{
	const auto it = m_counters.find(name);
	return it == m_counters.end() ? 0 : it->second.value;
}

std::vector<std::string> MusicScripts::acceptanceStops()
{
	return {
		"[S-710] in-game music: the music map's scripts run with ZH's script engine rules and the BFME2 template ordinals; inferred: the MUSIC_SCRIPT_* "
		"stack (play replaces the bottom, push goes on top, pop restarts the track below from its beginning; RW's AR_PushMusic / AR_PopMusic requests, "
		"RW 0x4A813E, not decoded), the fade flags (a fade-out stop, no fade-in), 'units' of the distance conditions (living non structures, 2D), the "
		"home base (the first COMMANDCENTER of the player), the difficulty (normal)",
		"[S-711] in-game music: retail runs the music map inside the logic script engine, where SET_RANDOM_MSEC_TIMER draws GameLogicRandomValue (ZH "
		"ScriptEngine::setTimer); here the music runs on the client with the audio random stream, so the logic random sequence of a retail game that "
		"evaluates the music scripts may contain draws this port does not make (which generator RotWK's music timer uses is not decoded); likewise "
		"PLAYER_ACQUIRED_SCIENCE consumes the music's own copy of the acquired-science queue (RW 0x759646), where retail's music and map scripts "
		"take from one queue (ScriptEngine + 0x1A3A8)",
	};
}
