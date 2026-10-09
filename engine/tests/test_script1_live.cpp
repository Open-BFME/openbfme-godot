// OpenBFME unit tests: the map script engine in live games (lane SCRIPT-1). GPL-3.0.
//
// Retail runs (they SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset): a skirmish on Evendim whose human sides run the Multiplayer_Human library in
// the logic (the end screen of END-1's flow then comes from the logic's VICTORY), and the opening of the first Angmar campaign mission (MAP ANG Angmar,
// LinearCampaignExpansion1.ini: Mission Foundation): its intro cinematic's timer chain, captions, camera moves and hero spawns. The expected frames
// follow from the map's own data (the timers the scripts set, read with tools/script/script_census.py --dump) and the timer rules of ScriptEngine.h.

#include "doctest.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/EndGame.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/ScriptCameraDirector.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <map>
#include <memory>

namespace
{

int factionIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

const std::vector<MapCacheEntry> &mapCache(starttest::Shared &s)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		std::string error;
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	return cache;
}

struct Game
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	EndGameController end;
	std::vector<EndGameRequest> requests;
	std::vector<ScriptClientRequest> scriptRequests;
	unsigned lastFrame = 0;

	void loadSkirmish(starttest::Shared &s, const NewGameMessage &message, int localSlot)
	{
		std::string error;
		REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, mapCache(s), RandomAlgorithm::ZH_CarryChain, start, &error), error);
		start.localSlot = localSlot;
		make(s);
		LiveGame::Options o;
		o.start = &start;
		REQUIRE_MESSAGE(game->load(o, &error), error);
		end.start(game->logic().economy().context().gameMode, game->logic().economy().context().gameKind);
		take();
	}
	void loadCampaign(starttest::Shared &s, const std::string &map)
	{
		std::string error;
		make(s);
		LiveGame::Options o;
		o.mapName = map;
		o.campaign = true;
		REQUIRE_MESSAGE(game->load(o, &error), error);
		take();
	}
	void make(starttest::Shared &s)
	{
		source.reset(new ArchiveW3DFileSource(*s.mount->fs));
		assets.reset(new WW3DAssetManager(*source));
		game.reset(new LiveGame(*s.world, *s.mount->fs, *assets, s.options));
	}
	void take()
	{
		for (ScriptClientRequest &r : game->takeScriptRequests())
		{
			scriptRequests.push_back(std::move(r));
		}
	}
	void step(double nowMs)
	{
		game->advance(0.2);
		take();
		const std::shared_ptr<const LogicSnapshot> snap = game->presentedSnapshot();
		const EndGameView *view = snap && snap->endGame && snap->frame != lastFrame ? snap->endGame.get() : nullptr;
		if (view)
		{
			lastFrame = snap->frame;
		}
		end.update(view, lastFrame, nowMs);
		for (EndGameRequest &r : end.takeRequests())
		{
			requests.push_back(r);
		}
	}
	GameLogic &logic() { return game->logic(); }
	const ScriptClientRequest *first(const std::string &action, const std::string &param0 = std::string()) const
	{
		for (const ScriptClientRequest &r : scriptRequests)
		{
			if (r.action == action && (param0.empty() || (!r.params.empty() && r.params[0].stringValue == param0)))
			{
				return &r;
			}
		}
		return nullptr;
	}
};

Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

} // namespace

TEST_CASE("script live: a skirmish on Evendim runs Multiplayer_Human for each human slot side in the logic; its VICTORY drives the end screen (S-1181)")
{
	OPENBFME_REQUIRE_START(s);
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 77u;
	m.game.startingCash = 1500;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = SLOT_PLAYER;
		slot.name = i == 0 ? u"Gimli" : u"Gothmog";
		slot.playerTemplate = factionIndex(s->world->playerTemplates(), factions[i]);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	Game peers[2];
	peers[0].loadSkirmish(*s, m, 0);
	peers[1].loadSkirmish(*s, m, 1);
	REQUIRE(peers[0].game->mapScriptsRunning());
	const ScriptEngine &e = static_cast<const GameLogic &>(peers[0].logic()).scriptEngine();
	REQUIRE(e.loaded());
	// the setup report: the two slot sides run the human library (Player_1, Player_2), no map Player_N list
	const std::vector<std::string> &setup = peers[0].game->report().mapScripts;
	size_t humanLibs = 0;
	for (const std::string &l : setup)
	{
		humanLibs += l.rfind("side '", 0) == 0 && l.find("Multiplayer_Human") != std::string::npos ? 1 : 0;
	}
	CHECK(humanLibs == 2);
	Player *gondor = playerAtStart(peers[0].logic(), 0);
	Player *mordor = playerAtStart(peers[0].logic(), 1);
	REQUIRE(gondor != nullptr);
	REQUIRE(mordor != nullptr);
	double now = 0.0;
	for (int f = 0; f < 10; ++f)
	{
		now += 200.0;
		peers[0].step(now);
		peers[1].step(now);
		CHECK(peers[0].logic().computeStateHash() == peers[1].logic().computeStateHash());
	}
	// "MP - Init Timer" set gt_Init_Window_MP (5 frames) at frame 1 and is one-shot; "Decide If Unpacking Is Appropriate" took its TRUE branch (a skirmish:
	// IS_GAME_IN_SKIRMISH_OR_MULTIPLAYER(1)), so PLAYER_GIVE_MONEY 5000 did not run and the money is the lobby's
	const std::string side = gondor->getPlayerName();
	CHECK_FALSE(peers[0].logic().scriptEngine().scriptActive(side + "/MP - Init Timer"));
	CHECK_FALSE(peers[0].logic().scriptEngine().scriptActive(side + "/Decide If Unpacking Is Appropriate"));
	CHECK(gondor->getMoney()->countMoney() <= 1500u);
	const ScriptEngine::Counter *t = peers[0].logic().scriptEngine().findCounter(side + "/gt_Init_Window_MP");
	REQUIRE(t != nullptr);
	CHECK(t->countdown);
	CHECK(t->value == -1);
	// Gondor destroys every Mordor object; the logic's "Multiplayer Win" of Gondor's side runs VICTORY, the local client shows it
	for (int k = 0; k < 2; ++k)
	{
		GameLogic &logic = peers[k].logic();
		Player *g = playerAtStart(logic, 0);
		Player *victim = playerAtStart(logic, 1);
		Object *src = nullptr;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == g && o->isKindOfName("STRUCTURE"))
			{
				src = o;
				break;
			}
		}
		REQUIRE(src != nullptr);
		std::vector<Object *> list;
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == victim && !o->isEffectivelyDead() && o->getBodyModule())
			{
				list.push_back(o);
			}
		}
		for (Object *o : list)
		{
			if (o->isEffectivelyDead() || o->isDestroyed())
			{
				continue;
			}
			DamageInfo info;
			info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
			info.m_input.m_deathType = DEATH_NORMAL;
			info.m_input.m_sourceID = src->getID();
			info.m_input.m_amount = 1.0e6f;
			o->getBodyModule()->attemptDamage(info);
		}
	}
	for (int f = 0; f < 60; ++f)
	{
		now += 200.0;
		peers[0].step(now);
		peers[1].step(now);
		REQUIRE(peers[0].logic().computeStateHash() == peers[1].logic().computeStateHash());
	}
	REQUIRE(mordor->isDefeated());
	// each side's copy of the library runs for its own player: Gondor's "Multiplayer Win" (MULTIPLAYER_ALLIED_VICTORY) and Mordor's "Multiplayer Lose"
	// (MULTIPLAYER_ALLIED_DEFEAT), after the defeat frame
	const std::vector<ScriptEngine::EndRequest> &ends = peers[0].logic().scriptEngine().endRequests();
	REQUIRE(ends.size() == 2);
	for (const ScriptEngine::EndRequest &r : ends)
	{
		CHECK(r.victory == (r.playerIndex == gondor->getPlayerIndex()));
		CHECK((r.playerIndex == gondor->getPlayerIndex() || r.playerIndex == mordor->getPlayerIndex()));
		CHECK(r.frame > mordor->getDefeatFrame());
	}
	CHECK_FALSE(peers[0].logic().scriptEngine().scriptActive(side + "/Multiplayer Win"));
	auto requested = [](const Game &g, const std::string &text) {
		return std::any_of(g.requests.begin(), g.requests.end(), [&](const EndGameRequest &r) { return r.kind == EndGameRequest::SHOW_END_GAME && r.text == text; });
	};
	CHECK(requested(peers[0], "APT:EndVictorious"));
	CHECK(peers[0].end.victoryScreen());
	CHECK(requested(peers[1], "APT:EndDefeat"));
	CHECK_FALSE(requested(peers[1], "APT:EndVictorious"));
	// the reports name the stops
	bool s1181 = false;
	for (const std::string &l : peers[0].logic().report().stops)
	{
		s1181 = s1181 || l.rfind("[S-1181]", 0) == 0;
	}
	CHECK(s1181);
}

TEST_CASE("script live: the opening of the first Angmar mission (MAP ANG Angmar): the intro cinematic's timer chain, captions, camera and hero spawns")
{
	OPENBFME_REQUIRE_START(s);
	Game g;
	g.loadCampaign(*s, "map ang angmar");
	REQUIRE(g.game->mapScriptsRunning());
	GameLogic &logic = g.logic();
	CHECK(logic.economy().context().gameMode == EconomyContext::MODE_SINGLE_PLAYER);
	const Player *local = logic.players().getLocalPlayer();
	REQUIRE(local != nullptr);
	MESSAGE("local player: " << local->getPlayerName() << "; sides: " << g.game->report().mapScripts.size());
	for (const std::string &l : g.game->report().mapScripts)
	{
		MESSAGE("  " << l);
	}
	// load() ran frame 1: "Intro Cine Part 1" (CONDITION_TRUE): DISABLE_INPUT, DESELECT, CAMERA_LETTERBOX_BEGIN, timer 2 s (10 frames)
	const ScriptClientRequest *letterbox = g.first("CAMERA_LETTERBOX_BEGIN");
	REQUIRE(letterbox != nullptr);
	CHECK(letterbox->frame == 1);
	CHECK(g.first("DISABLE_INPUT") != nullptr);
	// "Map Setup": the local player's money is 1000, the heroes exist (CREATE_NAMED_ON_TEAM_AT_WAYPOINT when there is no carry-over)
	CHECK(local->getMoney()->countMoney() == 1000u);
	const ScriptEngine &e = static_cast<const GameLogic &>(logic).scriptEngine();
	CHECK(e.getUnitNamed("Witch King") != nullptr);
	CHECK(e.getUnitNamed("Morgomir") != nullptr);
	CHECK(e.getUnitNamed("Rogash") != nullptr);
	for (const char *n : { "Rogash Attacker 1", "Rogash Attacker 2", "Surrender Monkeys" })
	{
		const Object *o = e.getUnitNamed(n);
		REQUIRE_MESSAGE(o != nullptr, n);
		CHECK_MESSAGE((o->getBodyModule() && o->getBodyModule()->isIndestructible()), n);
		// the horde's members too (RW 0x86A104: a payload member of an indestructible container is made indestructible)
		REQUIRE(o->getContain() != nullptr);
		for (const Object *m : *o->getContain()->getContainedItemsList())
		{
			CHECK((m->getBodyModule() && m->getBodyModule()->isIndestructible()));
		}
	}
	double now = 0.0;
	for (int f = 0; f < 200; ++f)
	{
		now += 200.0;
		g.step(now);
	}
	// the timer chain: part 2 at frame 1 + 10, part 3 50 frames later, part 4 75 later, part 5 45 later (the countdown decrements before the scripts
	// run, TIMER_EXPIRED is value < 1)
	const ScriptClientRequest *cap1 = g.first("SHOW_MILITARY_CAPTION", "SCRIPT:ANGAngmarIntroText_01");
	const ScriptClientRequest *cap2 = g.first("SHOW_MILITARY_CAPTION", "SCRIPT:ANGAngmarIntroText_02");
	const ScriptClientRequest *move = g.first("MOVE_CAMERA_TO", "Rogash Cam Pt");
	const ScriptClientRequest *reset = g.first("RESET_CAMERA", "InitialCameraPosition");
	REQUIRE(cap1 != nullptr);
	REQUIRE(cap2 != nullptr);
	REQUIRE(move != nullptr);
	REQUIRE(reset != nullptr);
	CHECK(cap1->frame == 11);
	CHECK(cap2->frame == 61);
	CHECK(move->frame == 136);
	CHECK(reset->frame == 181);
	CHECK(move->hasPosition);
	CHECK(reset->hasPosition);
	const ScriptClientRequest *end = g.first("CAMERA_LETTERBOX_END");
	REQUIRE(end != nullptr);
	CHECK(end->frame == 181);
	// the client applies them: the director moves the camera to the waypoint in 0.5 s
	ScriptCameraDirector d;
	d.apply(*move, Coord3D{ 0.0f, 0.0f, 0.0f }, 0.0f);
	d.update(250.0);
	CHECK(d.moving());
	d.update(300.0);
	CHECK_FALSE(d.moving());
	CHECK(d.target().x == move->position.x);
	CHECK(d.target().y == move->position.y);
	// lane SCRIPT-2: the map makes Rogash's attackers indestructible (objectIndestructible) until the player reaches "AT - Rogash Becomes Vulnerable";
	// "Rogash Saved" (Objective 3: the three attackers destroyed) has not fired
	for (const char *n : { "Rogash Attacker 1", "Rogash Attacker 2", "Surrender Monkeys" })
	{
		const Object *o = e.getUnitNamed(n);
		REQUIRE_MESSAGE(o != nullptr, n);
		CHECK_MESSAGE(!o->isEffectivelyDead(), n);
	}
	{
		ScriptEngine &me = logic.scriptEngine();
		const bool *saved = me.findFlag("PlyrAngmar/Rogash Saved");
		CHECK((saved == nullptr || !*saved));
	}
	MESSAGE("script engine: " << e.stats().scriptsFired << " scripts fired, " << e.stats().actionsRun << " actions, unported actions " << e.stats().unportedActions.size()
								<< ", unported conditions " << e.stats().unportedConditions.size());
	for (const std::string &l : e.report())
	{
		MESSAGE("  " << l);
	}
}
