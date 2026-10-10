// OpenBFME. GPL-3.0.
// See GameClient/EndGame.h for the target facts, the donors and what is inference (lane END-1). Client code: no logic state is written here.

#include "GameClient/EndGame.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/Audio/AudioEntryPoints.h"
#include "Common/Audio/EvaEvents.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine/Scripts.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace
{
// BFME2 condition / action ordinals (Open-BFME-2 ScriptEngine_initConditionTemplates.cpp / _initActionTemplates.cpp); the RotWK evaluator's table RW 0x7ED414
// maps 44 / 45 / 46 to the VictoryConditions calls (see the header)
enum
{
	C_CONDITION_TRUE = 3,
	C_TIMER_EXPIRED = 4,
	C_MULTIPLAYER_ALLIED_VICTORY = 44,
	C_MULTIPLAYER_ALLIED_DEFEAT = 45,
	C_MULTIPLAYER_PLAYER_DEFEAT = 46
};
enum
{
	A_VICTORY = 3,
	A_DEFEAT = 4,
	A_SET_TIMER = 6,
	A_ENABLE_SCRIPT = 8,
	A_DISABLE_SCRIPT = 9
};

const ScriptParameter &param(const std::vector<ScriptParameter> &p, size_t i)
{
	static const ScriptParameter kNone;
	return i < p.size() ? p[i] : kNone;
}

// RW 0x441B7C (LAN / Internet) or the skirmish mode 2: the modes whose defeats tell the local player through the end screen and Eva (RW 0x8090F7 .. 0x809112)
bool endScreenMode(int mode)
{
	return mode == 1 || mode == 5 || mode == 2;
}
// RW 0x625456: the multiplayer game test of hideEndGame (LAN, skirmish, Internet)
bool multiplayerMode(int mode)
{
	return mode == 1 || mode == 2 || mode == 5;
}
// RW 0x6253BF: a single player game (modes 0 and 6)
bool singlePlayerMode(int mode)
{
	return mode == 0 || mode == 6;
}
} // namespace

// ---- EndGameView -----------------------------------------------------------------------------------------------------------------------------------

bool EndGameView::sameAs(const EndGameView &o) const
{
	return singleAllianceRemaining == o.singleAllianceRemaining && endFrame == o.endFrame && observer == o.observer && localPlayerIndex == o.localPlayerIndex &&
		localEvil == o.localEvil && localAlive == o.localAlive && alliedVictory == o.alliedVictory && alliedDefeat == o.alliedDefeat &&
		playerOnlyDefeat == o.playerOnlyDefeat && events.size() == o.events.size() && displayNames == o.displayNames && localRelationship == o.localRelationship &&
		logicScripts == o.logicScripts && scriptEnds == o.scriptEnds;
}

std::shared_ptr<const EndGameView> EndGameView::build(GameLogic &logic, const std::shared_ptr<const EndGameView> &previous)
{
	const VictoryConditions &v = static_cast<const GameLogic &>(logic).victory();
	auto view = std::make_shared<EndGameView>();
	view->frame = logic.getFrame();
	view->singleAllianceRemaining = v.singleAllianceRemaining();
	view->endFrame = v.endFrame();
	view->observer = v.isObserver();
	const PlayerList &players = logic.players();
	const Player *local = players.getLocalPlayer();
	view->localPlayerIndex = local ? local->getPlayerIndex() : -1;
	view->localEvil = local && local->getPlayerTemplate() && local->getPlayerTemplate()->m_evil;
	view->localAlive = local && !local->isDefeated(); // RW 0x6AAC52 (+ 0x35A is not modelled)
	view->alliedVictory = v.localAlliedVictory();
	view->alliedDefeat = v.localAlliedDefeat();
	view->playerOnlyDefeat = v.localPlayerOnlyDefeat();
	view->events = v.events();
	// lane SCRIPT-1: the logic script engine's VICTORY / DEFEAT of the local player's side
	const ScriptEngine &scripts = static_cast<const GameLogic &>(logic).scriptEngine();
	view->logicScripts = scripts.loaded();
	for (const ScriptEngine::EndRequest &e : scripts.endRequests())
	{
		if (e.playerIndex == view->localPlayerIndex)
		{
			view->scriptEnds.push_back(e.victory);
		}
	}
	const int n = players.getPlayerCount();
	view->displayNames.resize((size_t)n);
	view->localRelationship.assign((size_t)n, (int)NEUTRAL);
	for (int i = 0; i < n; ++i)
	{
		const Player *p = players.getNthPlayer(i);
		if (!p)
		{
			continue;
		}
		view->displayNames[(size_t)i] = p->getPlayerDisplayName();
		if (local)
		{
			view->localRelationship[(size_t)i] = p->getDefaultTeam() ? (int)local->getRelationship(p->getDefaultTeam()) : (int)NEUTRAL; // RW 0x6ADBEB
		}
	}
	if (previous && previous->sameAs(*view))
	{
		return previous;
	}
	return view;
}

// ---- EndGameScripts ----------------------------------------------------------------------------------------------------------------------------------

struct EndGameScripts::Node
{
	const Script *script = nullptr;
	std::string name;
	bool active = false, subroutine = false, group = false;
	unsigned frameToEvaluate = 0;
	std::vector<std::unique_ptr<Node>> children;
};

EndGameScripts::EndGameScripts() = default;
EndGameScripts::~EndGameScripts() = default;

std::string EndGameScripts::libraryPath(const std::string &libraryName)
{
	return "Libraries\\" + libraryName + "\\" + libraryName + ".map"; // INFERENCE: the music library's pattern (AudioSettings MusicScriptLibraryName)
}

bool EndGameScripts::load(ArchiveFileSystem &fs, const std::string &libraryName, std::string *error)
{
	const std::string path = libraryPath(libraryName);
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile(path, bytes, error))
	{
		return false;
	}
	auto map = std::make_unique<LoadedMap>();
	MapReadOptions options;
	if (!MapReader::load(bytes, path, options, *map, error))
	{
		return false;
	}
	if (!map->hasPlayerScripts)
	{
		if (error)
		{
			*error = "the script library " + path + " has no PlayerScriptsList";
		}
		return false;
	}
	m_source = std::move(map);
	loadScripts(m_source->playerScripts);
	return true;
}

void EndGameScripts::build(const std::vector<ScriptItem> &items, std::vector<std::unique_ptr<Node>> &out)
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
			n->group = true;
			n->name = it.group->name;
			n->active = it.group->isActive;
			n->subroutine = it.group->isSubroutine;
			build(it.group->items, n->children);
		}
		out.push_back(std::move(n));
	}
}

void EndGameScripts::loadScripts(const PlayerScriptsList &scripts)
{
	m_lists.clear();
	m_timers.clear();
	for (const ScriptList &list : scripts.lists)
	{
		auto root = std::make_unique<Node>();
		root->name = "<list>";
		root->active = true;
		root->group = true;
		build(list.items, root->children);
		m_lists.push_back(std::move(root));
	}
}

size_t EndGameScripts::scriptCount() const
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

EndGameScripts::Node *EndGameScripts::find(std::vector<std::unique_ptr<Node>> &list, const std::string &name)
{
	for (auto &n : list)
	{
		if (n->name == name && n->name != "<list>")
		{
			return n.get();
		}
		if (Node *f = find(n->children, name))
		{
			return f;
		}
	}
	return nullptr;
}

EndGameScripts::Actions EndGameScripts::update(const EndGameView &view, unsigned frame)
{
	Actions a;
	m_frame = frame;
	// ZH ScriptEngine::update 5503: the countdown timers first
	for (auto &kv : m_timers)
	{
		if (kv.second >= 0)
		{
			--kv.second;
		}
	}
	for (auto &root : m_lists)
	{
		executeList(root->children, view, a);
	}
	return a;
}

void EndGameScripts::executeList(std::vector<std::unique_ptr<Node>> &list, const EndGameView &view, Actions &a)
{
	for (auto &n : list)
	{
		if (n->group)
		{
			if (n->active && !n->subroutine)
			{
				executeList(n->children, view, a);
			}
			continue;
		}
		if (!n->subroutine)
		{
			executeScript(*n, view, a);
		}
	}
}

void EndGameScripts::executeScript(Node &n, const EndGameView &view, Actions &a)
{
	// ZH ScriptEngine::executeScript 6950
	if (!n.active || !n.script)
	{
		return;
	}
	const Script &s = *n.script;
	if (!s.normal)
	{
		return; // INFERENCE: a human side's scripts run as "normal" (every script of the library has all three difficulty flags)
	}
	if (m_frame < n.frameToEvaluate)
	{
		return;
	}
	if (s.delayEvaluationSeconds > 0)
	{
		n.frameToEvaluate = m_frame + (unsigned)s.delayEvaluationSeconds * LOGICFRAMES_PER_SECOND;
	}
	++m_scriptsRun;
	bool result = false;
	for (const OrCondition &orc : s.orConditions)
	{
		if (orc.conditions.empty())
		{
			continue;
		}
		bool all = true;
		for (const ScriptCondition &c : orc.conditions)
		{
			if (!evaluateCondition(c, view))
			{
				all = false;
				break;
			}
		}
		if (all)
		{
			result = true;
			break;
		}
	}
	if (result)
	{
		executeActions(s.actions, a);
		if (s.isOneShot)
		{
			n.active = false;
		}
	}
	else if (!s.falseActions.empty())
	{
		executeActions(s.falseActions, a);
		if (s.isOneShot)
		{
			n.active = false;
		}
	}
}

bool EndGameScripts::evaluateCondition(const ScriptCondition &c, const EndGameView &view)
{
	switch (c.type)
	{
		case C_CONDITION_TRUE:
			return true;
		case C_TIMER_EXPIRED:
		{
			auto it = m_timers.find(param(c.params, 0).stringValue);
			return it != m_timers.end() && it->second < 1;
		}
		case C_MULTIPLAYER_ALLIED_VICTORY:
			return view.alliedVictory;
		case C_MULTIPLAYER_ALLIED_DEFEAT:
			return view.alliedDefeat;
		case C_MULTIPLAYER_PLAYER_DEFEAT:
			return view.playerOnlyDefeat;
		default:
			++m_unported["condition " + std::to_string(c.type) + " " + c.internalName];
			return false;
	}
}

void EndGameScripts::executeActions(const std::vector<ScriptActionRec> &actions, Actions &a)
{
	for (const ScriptActionRec &r : actions)
	{
		switch (r.type)
		{
			case A_VICTORY:
				a.victory = true;
				break;
			case A_DEFEAT:
				a.defeat = true;
				break;
			case A_SET_TIMER:
				m_timers[param(r.params, 0).stringValue] = param(r.params, 1).intValue * LOGICFRAMES_PER_SECOND; // ZH setTimer: seconds -> frames
				break;
			case A_ENABLE_SCRIPT:
			case A_DISABLE_SCRIPT:
				for (auto &root : m_lists)
				{
					if (Node *n = find(root->children, param(r.params, 0).stringValue))
					{
						n->active = r.type == A_ENABLE_SCRIPT;
						break;
					}
				}
				break;
			default:
				++m_unported["action " + std::to_string(r.type) + " " + r.internalName];
				break;
		}
	}
}

// ---- EndGameController -------------------------------------------------------------------------------------------------------------------------------

void EndGameController::start(int mode, int kind)
{
	m_mode = mode;
	m_kind = kind;
	m_eventsSeen = 0;
	m_scriptEndsSeen = 0; // lane SCRIPT-1: a reused controller sees the next game's end requests from the start
	m_showing = m_shownOnce = m_hiddenOnce = m_fadeToScore = m_lastWasVictory = false;
	m_singleAlliance = false;
	m_shownAt = 0.0;
	m_endGameTimer = -1;
	m_endGameTimerFrame = 0;
	m_requests.clear();
}

// RW 0x602FFE (lane PLAY-1, stop S-1921: client-side)
void EndGameController::startEndGameTimer(unsigned frame)
{
	if (m_endGameTimer == 0)
	{
		return; // the game is already being left
	}
	m_endGameTimer = kEndGameTimerFrames;
	m_endGameTimerFrame = frame;
}

// RW 0x808E1F
void EndGameController::showEndGame(const std::string &label, bool evil, const std::string &sound, const std::string &cheer, double nowMs, bool victory)
{
	if (m_showing)
	{
		return; // + 0x10: the end screen is up
	}
	EndGameRequest r;
	r.kind = EndGameRequest::SHOW_END_GAME;
	r.text = label;
	r.evil = evil;
	r.sound = sound;
	r.cheer = cheer;
	m_requests.push_back(r);
	m_showing = true;
	m_shownOnce = true;
	m_lastWasVictory = victory;
	m_shownAt = nowMs;
}

// RW 0x808CCD
void EndGameController::hideEndGame()
{
	if (!m_showing)
	{
		return;
	}
	EndGameRequest r;
	r.kind = EndGameRequest::HIDE_END_GAME;
	m_requests.push_back(r);
	m_showing = false;
	m_hiddenOnce = true;
	if (multiplayerMode(m_mode) && m_singleAlliance)
	{
		EndGameRequest t;
		t.kind = EndGameRequest::TRANSITION;
		t.text = "MPorSkirmishFadeToScoreScreen";
		m_requests.push_back(t);
		m_fadeToScore = true;
	}
}

void EndGameController::update(const EndGameView *view, unsigned frame, double nowMs)
{
	// updateEndGame (RW 0x808A20): real time
	if (m_showing && nowMs - m_shownAt > kEndGameMs)
	{
		hideEndGame();
	}
	if (!view)
	{
		return;
	}
	// lane PLAY-1: the end-game timer (ScriptEngine::update RW 0x60CC67 decrements + 0x1A204 once per logic frame; at 0, RW 0x603533 sends
	// MSG_CLEAR_GAME_DATA). The client sees presented frames, so it compares frame numbers: a frame skipped by the presentation still counts
	if (m_endGameTimer > 0 && frame - m_endGameTimerFrame >= (unsigned)m_endGameTimer)
	{
		m_endGameTimer = 0;
		EndGameRequest r;
		r.kind = EndGameRequest::CLEAR_GAME_DATA;
		r.text = "MSG_CLEAR_GAME_DATA";
		m_requests.push_back(r);
	}
	m_singleAlliance = view->singleAllianceRemaining;
	// the defeats of the frame (VictoryConditions::update, phase 5)
	for (; m_eventsSeen < view->events.size(); ++m_eventsSeen)
	{
		const VictoryConditions::Event &e = view->events[m_eventsSeen];
		if (e.kind != VictoryConditions::Event::PLAYER_DEFEATED)
		{
			continue;
		}
		const bool local = e.playerIndex == view->localPlayerIndex;
		const std::string name = e.playerIndex >= 0 && (size_t)e.playerIndex < view->displayNames.size() ? view->displayNames[(size_t)e.playerIndex] : std::string();
		// RW 0x8090AB .. 0x80911F (RW 0x5FF924, a replay or a loaded game, is false for a live game)
		if (m_kind != 0 || !local)
		{
			if (!singlePlayerMode(m_mode) && m_kind != 0)
			{
				EndGameRequest r;
				r.kind = EndGameRequest::MESSAGE;
				r.text = "GUI:PlayerHasBeenDefeated";
				r.name = name;
				m_requests.push_back(r);
			}
		}
		else
		{
			EndGameRequest r;
			r.kind = EndGameRequest::MESSAGE;
			r.text = "GUI:YouHaveBeenDefeated";
			m_requests.push_back(r);
		}
		if (endScreenMode(m_mode))
		{
			const int rel = e.playerIndex >= 0 && (size_t)e.playerIndex < view->localRelationship.size() ? view->localRelationship[(size_t)e.playerIndex] : (int)NEUTRAL;
			if (rel == (int)ALLIES || local)
			{
				if (local)
				{
					showEndGame("APT:EndDefeat", view->localEvil, "Gui_DefeatScreen", std::string(), nowMs, false);
					continue;
				}
				EndGameRequest r;
				r.kind = EndGameRequest::EVA;
				r.eva = 6; // AllyDefeated
				m_requests.push_back(r);
			}
			else
			{
				EndGameRequest r;
				r.kind = EndGameRequest::EVA;
				r.eva = 7; // EnemyDefeated
				m_requests.push_back(r);
			}
		}
	}
	// the human side's scripts (the script engine runs in the next frame's phase 1: they see this frame's state). lane SCRIPT-1: when the logic's
	// ScriptEngine runs the library, its VICTORY / DEFEAT of the local side are taken from the view and the client's copy stays idle
	if (view->logicScripts)
	{
		const std::string overLabel = "APT:EndGameOver";
		for (; m_scriptEndsSeen < view->scriptEnds.size(); ++m_scriptEndsSeen)
		{
			if (view->scriptEnds[m_scriptEndsSeen])
			{
				showEndGame(view->localAlive ? "APT:EndVictorious" : overLabel, view->localEvil, "Gui_VictoryScreen", view->localEvil ? "Gui_VictoryCheerEvil" : "Gui_VictoryCheerGood", nowMs, true);
			}
			else
			{
				showEndGame(view->localAlive ? "APT:EndDefeat" : overLabel, view->localEvil, "Gui_DefeatScreen", std::string(), nowMs, false);
			}
			startEndGameTimer(frame); // RW 0x7C45E0 / 0x7BF15E -> RW 0x602FFE (lane PLAY-1)
		}
	}
	else if (m_scriptsLoaded && view->localPlayerIndex >= 0)
	{
		const EndGameScripts::Actions a = m_scripts.update(*view, frame);
		const std::string overLabel = "APT:EndGameOver";
		if (a.victory)
		{
			// RW 0x7BF05C
			showEndGame(view->localAlive ? "APT:EndVictorious" : overLabel, view->localEvil, "Gui_VictoryScreen", view->localEvil ? "Gui_VictoryCheerEvil" : "Gui_VictoryCheerGood", nowMs, true);
			startEndGameTimer(frame); // RW 0x7C45E0 -> RW 0x602FFE (lane PLAY-1)
		}
		if (a.defeat)
		{
			// RW 0x7BF15E
			showEndGame(view->localAlive ? "APT:EndDefeat" : overLabel, view->localEvil, "Gui_DefeatScreen", std::string(), nowMs, false);
			startEndGameTimer(frame); // RW 0x7BF15E's end -> RW 0x602FFE (lane PLAY-1)
		}
	}
}

std::vector<std::string> EndGameController::stopLines()
{
	return {
		"[S-1060] end sequence: the human side's script library Multiplayer_Human runs on the client for the local player only, answering CONDITION_TRUE, TIMER_EXPIRED and "
		"MULTIPLAYER_ALLIED_VICTORY / _DEFEAT / PLAYER_DEFEAT and executing VICTORY, DEFEAT, SET_TIMER, ENABLE / DISABLE_SCRIPT; every other condition is false and every other action "
		"is counted, not run (the library's base unpacking and money scripts need a logic script engine); the library path pattern is the music library's; the end screen movie is "
		"GuiFX.apt at its BFME1 level 11 (RotWK calls _level13); doVictory's menu / input calls (RW 0x779173, RW 0x7BC586) and RW 0x61632F are not ported; the end screen timer runs "
		"on the presentation clock; a presented frame that skips logic frames runs the scripts once; the defeat messages are drawn by a presentation label (the Palantir message area "
		"is not drawn yet) and the MPorSkirmishFadeToScoreScreen sound fade takes one second",
		"[S-1921] end-game timer (lane PLAY-1): ScriptEngine::startEndGameTimer (RW 0x602FFE, 25 logic frames after the local side's VICTORY / DEFEAT) and its "
		"MSG_CLEAR_GAME_DATA (RW 0x603533) run on the client's end sequence, counting presented logic frames; the logic's side scripts are not stopped while it runs "
		"(retail: only while ScriptEngine + 0x1A204 < 0); the campaign's messages 0x7ED / 0x7D9 (RW 0x602E64 true) are not sent",
		"[S-1063] score screen: TimeLine.apt (AptTimeLine) gets its providers, names, faction icon records (RW 0x92632E), axis texts and the statistics page (StatsList: RW "
		"0x9CDEC1 / 0x9F0B50, SetPlayerFocus RW 0x9CD723) from the logic's statistics; the movie's tabs switch _graphMode (the interpreter resolves eval(\"TabButtons.\" + tab)); not "
		"ported: Save Replay, the Create-a-Hero awards and the War of the Ring modes; the graph (RenderGraph RW 0x9257F2) is drawn by the device layer from AptTimeLine::graph(); the "
		"flag providers answer numbers (WindowManager::markNumericProvider: INFERENCE from TimeLine.apt's Apt version 7 branches); the value cells' + 4 field (RW 0x725D96(.., 2)) is "
		"taken as centred text (INFERENCE: its reader was not located); the ratio cells use '.' as the decimal separator (retail: the locale's)",
		"[S-1064] leaving the game: QuitMenu.apt (AptQuitMenu: Resume, Options, Restart in a skirmish / Forfeit in a LAN game, Exit and the movie's confirmations) over the game; the "
		"surrender and the exit of a LAN game are MSG_SELF_DESTRUCT on the lockstep; Continue shows the screens the game started from (TheShell + 0x9C: MainMenu and Skirmish) or opens "
		"the LAN lobby again (the LAN game is started without the shell's LAN screens, MP-1). Not ported: the save / load screens of the menu, the Living World branches, the network quit "
		"of the first six frames (RW 0xBFD298), the asset transfer to an ally (RW 0x6AF598), the network session's disconnected slots (RW 0x90313E); INFERENCE: a network game answers "
		"the quit menu with mode 1 / kind 1 (retail's LAN new-game message, RW 0x648EED), the exiting peer's own logic is cleared before it runs its MSG_SELF_DESTRUCT(true)",
	};
}

std::string EndGameController::evaName(int index)
{
	return index >= 0 && index < EvaEventStore::kPredefinedCount ? std::string(EvaEventStore::predefinedNames()[index]) : std::string();
}

bool EndGameController::playEva(int index)
{
	const std::string name = evaName(index);
	return !name.empty() && AudioApi::reportEva(name, nullptr);
}

std::vector<EndGameRequest> EndGameController::takeRequests()
{
	std::vector<EndGameRequest> out;
	out.swap(m_requests);
	return out;
}

// ---- ScoreScreenData ---------------------------------------------------------------------------------------------------------------------------------

const char *const ScoreScreenData::kTimeFormatLabels[3] = { "APT:TimeMinuteSecond", "APT:TimeHoursMinute", "APT:TimeDaysHoursMinute" };
const char *const ScoreScreenData::kTimeDescriptionLabels[3] = { "APT:TimeDescriptionMinuteSecond", "APT:TimeDescriptionHoursMinute", "APT:TimeDescriptionDaysHoursMinute" };

ScoreScreenData ScoreScreenData::build(GameLogic &logic, int type, const std::vector<int> &slotPlayerIndices, const std::vector<int> &disconnected)
{
	ScoreScreenData d;
	d.type = type;
	d.valid = true;
	d.frames = logic.getFrame();
	const VictoryConditions &v = static_cast<const GameLogic &>(logic).victory();
	const PlayerList &players = logic.players();
	const Player *local = players.getLocalPlayer();
	d.localIsObserver = !local || local->isObserver(); // RW 0x6AAC44 (+ 0x294)
	if (!d.localIsObserver)
	{
		d.entries.resize(1); // RW 0x92708A(1): the local player's row is the first
	}
	for (int idx : slotPlayerIndices)
	{
		const Player *p = players.getNthPlayer(idx);
		if (!p || p->isObserver())
		{
			continue;
		}
		Entry e;
		e.playerIndex = idx;
		e.name = p->getPlayerDisplayName();
		e.color = p->getPlayerColor() & 0xFFFFFFu;
		e.side = p->getSide();
		e.local = p == local;
		e.result = 3;
		if (std::find(disconnected.begin(), disconnected.end(), idx) != disconnected.end())
		{
			e.result = 2;
		}
		else if (v.wouldBeDefeated(p))
		{
			e.result = 1; // vslot 0x40
		}
		else if (v.hasAchievedVictory(p))
		{
			e.result = 0; // vslot 0x38
		}
		const ScoreKeeper &s = p->getScoreKeeper();
		e.score = s.computeScore(logic.settings(), logic.getFrame());
		e.perFrame = s.perFrameStats();
		e.fortressMarks = s.fortressMarks();
		e.unitsBuilt = s.unitsBuilt();
		e.unitsLost = s.unitsLost();
		e.unitsDestroyed = s.totalUnitsDestroyed();
		e.structuresBuilt = s.structuresBuilt();
		e.structuresLost = s.structuresLost();
		e.structuresDestroyed = s.totalStructuresDestroyed();
		e.moneyEarned = s.moneyEarned();
		e.moneySpent = s.moneySpent();
		e.purchasePoints = s.sciencePurchasePointsEarned();
		e.skillPoints = s.skillPointsEarned();
		// lane END-2: the statistics page (RW 0x9CDEC1)
		e.sessionSeconds = (s.endFrame() != 0 ? s.endFrame() : logic.getFrame()) / 5u; // RW 0x79DC0C: div by RW 0xD9F608 (5)
		e.firstHeroSeconds = (std::uint32_t)s.firstHeroEntry() / 5u;                  // RW 0x79DC27
		e.fortressesBuilt = (int)s.fortressMarks().size();
		const std::string favorite = s.favoriteUnit(logic);
		e.favoriteUnitFound = !favorite.empty();
		if (!favorite.empty())
		{
			const ThingTemplate *tt = logic.things().findTemplate(favorite);
			const FieldValue *dn = tt ? tt->findField("DisplayName") : nullptr;
			if (const std::string *label = dn ? std::get_if<std::string>(dn) : nullptr)
			{
				e.favoriteUnitLabel = *label; // template + 0x30
			}
			else if (const RawTokens *raw = dn ? std::get_if<RawTokens>(dn) : nullptr)
			{
				e.favoriteUnitLabel = raw->tokens.empty() ? std::string() : raw->tokens.front(); // the label as the row gives it (parseLabel)
			}
		}
		e.moneyReceived = s.moneyReceivedFromAllies();
		e.moneyGiven = s.moneyGivenToAllies();
		e.spentStructures = s.moneySpentOnStructures();
		e.spentUnits = s.moneySpentOnUnits();
		e.spentHeroes = s.moneySpentOnHeroes();
		e.heroesBuilt = s.heroesBuilt(logic);
		e.heroesLost = s.heroesLost(logic);
		if (e.local && !d.entries.empty())
		{
			d.entries[0] = std::move(e);
		}
		else
		{
			d.entries.push_back(std::move(e));
		}
	}
	if (!d.localIsObserver && !d.entries.empty() && d.entries[0].playerIndex < 0)
	{
		d.entries.erase(d.entries.begin()); // the local player had no slot (a game not started from slots)
	}
	return d;
}

int ScoreScreenData::graphMode(const std::string &name)
{
	if (name == "Units")
	{
		return 0;
	}
	if (name == "Structures")
	{
		return 1;
	}
	if (name == "Resources")
	{
		return 2;
	}
	if (name == "Territories")
	{
		return 3;
	}
	return 4; // "FinalScore", the default when the movie has no _graphMode
}

float ScoreScreenData::graphValue(const ScoreKeeper::PerFrameStats &e, int mode)
{
	switch (mode)
	{
		case 0: return (float)(std::int16_t)e.unitsAlive;      // movsx + 0xC
		case 1: return (float)(std::int16_t)e.structuresAlive; // movsx + 0xE
		case 2: return (float)(std::int32_t)e.money;           // fild + 4
		case 4: return e.score;                                 // fld + 8
		default: return 0.0f;                                   // RW 0x9252B8: 0.0 (mode 3 reads another source in the War of the Ring)
	}
}

std::string ScoreScreenData::formatTime(const std::string &format, int seconds)
{
	// RW 0x925D3C .. 0x925E33: s -> %02d of seconds % 60, m -> %02d of minutes % 60, h -> %02d of hours % 24, d -> %d of days, any other character copied
	std::string out;
	char b[32];
	for (char c : format)
	{
		if (c == 's')
		{
			std::snprintf(b, sizeof(b), "%02d", seconds % 60);
			out += b;
		}
		else if (c == 'm')
		{
			std::snprintf(b, sizeof(b), "%02d", (seconds / 60) % 60);
			out += b;
		}
		else if (c == 'h')
		{
			std::snprintf(b, sizeof(b), "%02d", (seconds / 3600) % 24);
			out += b;
		}
		else if (c == 'd')
		{
			std::snprintf(b, sizeof(b), "%d", seconds / 86400);
			out += b;
		}
		else
		{
			out += c;
		}
	}
	return out;
}

ScoreScreenData::Axis ScoreScreenData::axis(int mode, const std::string formats[3]) const
{
	Axis a;
	float maxValue = -FLT_MAX; // RW 0xBD190C
	for (const Entry &e : entries)
	{
		a.samples = std::max(a.samples, (int)e.perFrame.size());
		for (const ScoreKeeper::PerFrameStats &f : e.perFrame)
		{
			maxValue = std::max(maxValue, graphValue(f, mode));
		}
	}
	if (a.samples == 0)
	{
		maxValue = 0.0f;
	}
	a.maxValue = maxValue;
	// RW 0x925A06 .. 0x925A6C
	const int imax = (int)maxValue;
	char b[32];
	std::snprintf(b, sizeof(b), "%d", imax);
	int digits = (int)std::string(b).size() - 2;
	if (digits < 1)
	{
		digits = 1;
	}
	int p = 1;
	for (int i = 0; i < digits; ++i)
	{
		p *= 10; // RW 0x924D18(10, n)
	}
	int step = ((imax / p + 1) * p) / 10;
	if (step == 0)
	{
		step = 1;
	}
	a.step = step;
	a.top = (float)(step * 10);
	for (int i = 0; i <= 10; ++i)
	{
		std::snprintf(b, sizeof(b), "%d", i * step); // Timeline:YAxis:%d, UnicodeString "%d"
		a.yLabels.push_back(b);
	}
	// the x axis (RW 0x925BC1 ..): the label of mark i is the time of entry (samples + 0.5) * i * 0.1; the format by the total time
	const int totalSeconds = a.samples / 5; // / LOGICFRAMES_PER_SECOND (RW 0xD9F608)
	a.timeFormat = totalSeconds < 3600 ? 0 : (totalSeconds < 86400 ? 1 : 2);
	for (int i = 0; i <= 10; ++i)
	{
		const int frame = (int)(((float)a.samples + 0.5f) * (float)i * 0.1f);
		a.xLabels.push_back(formatTime(formats[a.timeFormat], frame / 5));
	}
	a.totalTime = a.xLabels.back();
	return a;
}

// ---- lane END-2: the statistics page -------------------------------------------------------------------------------------------------------------------

// RW 0x9F19DC (the strings RW 0xC8F3F0 .. 0xC8F11C)
const char *const ScoreScreenData::kStatLabels[kStatRows] = { "STAT:RTS_SESSION_LENGTH", "STAT:RTS_STRUCTURES_CREATED", "STAT:RTS_STRUCTURES_LOST",
	"STAT:RTS_STRUCTURES_DESTROYED", "STAT:RTS_FORTRESSES_BUILT", "STAT:RTS_UNITS_CREATED", "STAT:RTS_UNITS_LOST", "STAT:RTS_UNIT_KILL_DEATH_RATIO",
	"STAT:RTS_UNITS_KILLED", "STAT:RTS_FAVORITE_UNIT", "STAT:RTS_MAXIMUM_INCOME_RATE_PER_MINUTE", "STAT:RTS_TOTAL_RESOURCES_GATHERED",
	"STAT:RTS_MONEY_GIVEN_TO_ALLIES", "STAT:RTS_MONEY_RECEIVED_FROM_ALLIES", "STAT:RTS_RESOUCES_SPENT_ON_STRUCTURES", "STAT:RTS_RESOUCES_SPENT_ON_UNITS",
	"STAT:RTS_RESOUCES_SPENT_ON_HEROES", "STAT:RTS_TIME_SPENT_TO_REACH_LAST_SPELL_LEVEL", "STAT:RTS_STRATEGIC_SKILL", "STAT:RTS_TACTICAL_SKILL",
	"STAT:RTS_TIME_SPENT_TO_BUILD_FIRST_HERO", "STAT:RTS_TIMES_EACH_HERO_WAS_PURCHASED", "STAT:RTS_HEROES_BUILT", "STAT:RTS_HEROES_LOST" };

namespace
{
// RW 0x9F07C7 / 0x9F0E7A: the value is the number as an unsigned 32-bit integer (a negative int gains 2^32: RW 0xBD8698 = 4294967296.0f)
float unsignedValue(std::int32_t v)
{
	float f = (float)v;
	if (v < 0)
	{
		f += 4294967296.0f;
	}
	return f;
}
} // namespace

ScoreScreenData::StatCell ScoreScreenData::intCell(std::int32_t v)
{
	StatCell c;
	c.present = true;
	c.value = unsignedValue(v);
	c.text = std::to_string(v); // "%d" (RW 0xBDF1B0)
	return c;
}

// RW 0x9F0885
ScoreScreenData::StatCell ScoreScreenData::ratioCell(float a, float b, const std::string &decimal)
{
	StatCell c;
	c.present = true;
	if (!(std::fabs(a) >= 1.0e-4f)) // RW 0xBD19E0 (9.9999997e-05f)
	{
		c.value = 0.0f;
		c.text = "-"; // RW 0xC540B0
		return c;
	}
	const float d = std::fabs(b) >= 1.0e-4f ? b : 1.0f; // RW 0xBD1908 (1.0f)
	const float r = a / d;
	c.value = r;
	char buf[256];
	std::snprintf(buf, sizeof(buf), "%1.2f", (double)r); // RW 0xC8F0C4
	std::string t = buf;
	const size_t dot = t.find('.');
	if (dot != std::string::npos)
	{
		t = t.substr(0, dot) + decimal + t.substr(dot + 1); // "%hs%hs%hs" with the locale's separator (RW 0xC8F0B0)
	}
	c.text = t;
	return c;
}

// RW 0x9F0E7A (and RW 0x9F09FC for each part)
ScoreScreenData::StatCell ScoreScreenData::timeCell(std::uint32_t seconds, const std::function<std::string(const std::string &)> &fetch)
{
	StatCell c;
	c.present = true;
	c.value = unsignedValue((std::int32_t)seconds);
	std::uint32_t days = seconds / 86400u, hours = (seconds % 86400u) / 3600u, minutes = ((seconds % 86400u) % 3600u) / 60u, secs = ((seconds % 86400u) % 3600u) % 60u;
	if (days != 0)
	{
		minutes = 0; // RW 0x9F0F2C: a day clears the minutes and the seconds (the hours stay)
		secs = 0;
	}
	auto part = [&](const char *label, std::uint32_t n) -> std::string {
		if (n == 0)
		{
			return std::string();
		}
		std::string t;
		if (n == 1)
		{
			t = fetch(label); // the singular label as it is
		}
		else
		{
			t = fetch(std::string(label) + "s"); // RW 0xC8F0DC: the plural label, formatted with the count
			const size_t p = t.find("%d");
			if (p != std::string::npos)
			{
				t = t.substr(0, p) + std::to_string(n) + t.substr(p + 2);
			}
		}
		return t + " "; // RW 0xBD16E4
	};
	std::string t = part("TIME:Day", days) + part("TIME:Hour", hours) + part("TIME:Minute", minutes) + part("TIME:Second", secs);
	// UnicodeString::trim: the leading and trailing spaces go
	const size_t b = t.find_first_not_of(' ');
	const size_t e = t.find_last_not_of(' ');
	t = b == std::string::npos ? std::string() : t.substr(b, e - b + 1);
	c.text = t.empty() ? std::string("0 ") : t; // RW 0xC8F0E0
	return c;
}

// RW 0x9CDEC1, once per entry (the column is the entry's position)
std::vector<ScoreScreenData::StatRow> ScoreScreenData::statRows(const std::function<std::string(const std::string &)> &fetch, const std::string &decimal) const
{
	std::vector<StatRow> rows((size_t)kStatRows);
	for (int r = 0; r < kStatRows; ++r)
	{
		rows[(size_t)r].label = fetch(kStatLabels[r]);
		rows[(size_t)r].values.resize(std::max<size_t>(entries.size(), 8)); // RW 0x9F19DC(8)
	}
	for (size_t i = 0; i < entries.size(); ++i)
	{
		const Entry &e = entries[i];
		for (int r = 0; r < kStatRows; ++r)
		{
			StatCell c;
			switch (r)
			{
				case 0: c = timeCell(e.sessionSeconds, fetch); break;
				case 1: c = intCell(e.structuresBuilt); break;         // + 0xC8
				case 2: c = intCell(e.structuresLost); break;          // + 0xCC
				case 3: c = intCell(e.structuresDestroyed); break;     // RW 0x79DC5D
				case 4: c = intCell(e.fortressesBuilt); break;         // + 0x328 entries
				case 5: c = intCell(e.unitsBuilt); break;              // + 0x70
				case 6: c = intCell(e.unitsLost); break;               // + 0x74
				case 7: c = ratioCell((float)e.unitsDestroyed, (float)e.unitsLost, decimal); break;
				case 8: c = intCell(e.unitsDestroyed); break;          // RW 0x79DC87
				case 9:
					// RW 0x9CE0C4: GUI:None unless a favourite unit was found; the value is 0 (RW 0x9F0812(text, 0))
					c.present = true;
					c.text = !e.favoriteUnitFound ? fetch("GUI:None") : (e.favoriteUnitLabel.empty() ? std::string() : fetch(e.favoriteUnitLabel));
					c.value = 0.0f;
					break;
				case 10: c = ratioCell((float)(std::int32_t)e.moneyEarned, (float)((std::int32_t)e.sessionSeconds / 60), decimal); break; // RW 0x79DF02
				case 11: c = intCell((std::int32_t)e.moneyEarned); break;  // + 4
				case 12: c = intCell((std::int32_t)e.moneyGiven); break;   // + 0x10
				case 13: c = intCell((std::int32_t)e.moneyReceived); break; // + 0xC
				case 14: c = intCell(e.spentStructures); break;         // + 0x18
				case 15: c = intCell(e.spentUnits); break;              // + 0x14
				case 16: c = intCell(e.spentHeroes); break;             // + 0x1C
				case 18: c = ratioCell(((float)e.structuresDestroyed + (float)e.unitsDestroyed) * 100.0f, (float)(std::int32_t)e.moneySpent, decimal); break; // RW 0xBD88D8
				case 19: c = ratioCell((float)e.unitsBuilt, (float)e.unitsLost, decimal); break;
				case 20: c = timeCell(e.firstHeroSeconds, fetch); break;
				case 22: c = intCell(e.heroesBuilt); break;
				case 23: c = intCell(e.heroesLost); break;
				default: break; // 17, 21: no value (RW 0x9CE2D6 switch default)
			}
			if (!c.present)
			{
				continue;
			}
			StatRow &row = rows[(size_t)r];
			row.values[i] = c;
			row.shown = true;
			if (row.best <= c.value && c.value != row.best) // RW 0x9F0B0B
			{
				row.best = c.value;
			}
		}
	}
	return rows;
}

// RW 0x9CD723
std::array<int, 3> ScoreScreenData::focusColumns(int argument, int entryCount, bool localIsObserver, std::array<int, 3> current)
{
	if (entryCount <= 0)
	{
		return current;
	}
	const int first = localIsObserver ? 0 : 1;
	int u = argument;
	if (u < first)
	{
		u = first;
	}
	if (entryCount < u)
	{
		u = entryCount - 2;
	}
	if (!localIsObserver)
	{
		current[0] = 0; // the local player's column stays
	}
	for (int slot = first; slot < 3; ++slot)
	{
		current[(size_t)slot] = u < entryCount ? u++ : -1;
	}
	return current;
}
