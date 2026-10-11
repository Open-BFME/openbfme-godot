// OpenBFME retail tests, lane AUTOCAST-1 round 2 (review r1 #2 / #3): autocast in whole games. GPL-3.0.
//
//  * a recorded skirmish replay: the Witch-king's Morgul Blade toggled on and off by MSG_DO_AUTO_ABILITY commands of the local player, auto casts at an
//    enemy hero in between; the replay plays back with every frame's state hash equal, and with the toggle taken out of it the playback names the frame
//    after the toggle as the first difference (the toggle and its casts are in the replayed state);
//  * the scripts that switch autocast in retail data, loaded from the maps: the Evil campaign's Erebor turns Gloin's slam on (NAME_SET_AUTO_ABILITY, RW
//    0x7C3C88); the skirmish AI never does: a skirmish does not load lib_hero_behaviors (only the obsolete __old_ai_scripts reference it), and its
//    TEAM_SET_AUTO_ABILITY (RW 0x7C3D61) names buttons whose AutoAbility is No, which retail ignores (RW 0x7C3DD8).
// The tests SKIP loudly without the installs.

#include "doctest.h"

#include "CampaignTestUtil.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/ScriptEngine/Scripts.h"
#include "GameNetwork/LockstepDriver.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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

// a 1 v 1 on Tournament Udun: slot 0 the (idle) human as `local`, slot 1 a Medium computer as `enemy`
NewGameMessage duel(starttest::Shared &s, const char *local, const char *enemy, std::uint32_t seed)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp tournament udun/map mp tournament udun.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &slot = m.game.slots[i];
		slot.state = i == 0 ? SLOT_PLAYER : SLOT_MED_AI;
		slot.name = i == 0 ? u"Player0" : u"Player1";
		slot.playerTemplate = factionIndex(s.world->playerTemplates(), i == 0 ? local : enemy);
		REQUIRE(slot.playerTemplate >= 0);
		slot.startPos = i;
		slot.color = i;
		slot.teamNumber = i;
	}
	return m;
}

struct Game
{
	std::unique_ptr<NewGameStart> start;
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
	Player *local = nullptr;
	Player *enemy = nullptr;
	void load(starttest::Shared &s, const NewGameMessage &message)
	{
		std::string error;
		start = std::make_unique<NewGameStart>(RandomAlgorithm::ZH_CarryChain);
		REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, mapCache(s), RandomAlgorithm::ZH_CarryChain, *start, &error), error);
		source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
		assets = std::make_unique<WW3DAssetManager>(*source);
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *assets, s.options);
		LiveGame::Options o;
		o.start = start.get();
		REQUIRE_MESSAGE(game->load(o, &error), error);
		local = game->players().getLocalPlayer();
		REQUIRE(local);
		for (int i = 0; i < game->players().getPlayerCount(); ++i)
		{
			Player *p = game->players().getNthPlayer(i);
			if (p && p != local && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && !enemy && p->getPlayerName() == "Player1")
			{
				enemy = p;
			}
		}
		for (int i = 0; !enemy && i < game->players().getPlayerCount(); ++i)
		{
			Player *p = game->players().getNthPlayer(i);
			enemy = p && p != local && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && !p->getPlayerTemplate()->m_isObserver ? p : nullptr;
		}
		REQUIRE(enemy);
	}
	~Game()
	{
		if (game)
		{
			game->setFrameDriver(nullptr);
			game->logic().reset();
		}
	}
};

// the local player's first structure (the citadel)
Coord3D homeOf(LiveGame &g, Player *p)
{
	for (Object *o = g.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == p && o->isKindOfName("STRUCTURE"))
		{
			return *o->getPosition();
		}
	}
	FAIL("no structure of the local player");
	return {};
}

// the scenario's setup, the same before the recording and before every playback (it is not a command: the test makes the same objects at the same
// frame in each run): the Witch-king (nine levels: the level-up grants Upgrade_WitchkingMorgulBlade) beside the local citadel and an enemy Boromir
// 300 further on; their ids
struct Setup
{
	ObjectID witchKing = INVALID_ID, boromir = INVALID_ID;
};
Setup setUp(Game &g)
{
	Setup s;
	const Coord3D home = homeOf(*g.game, g.local);
	std::string error;
	Object *wk = g.game->createObject("AngmarWitchking", g.local->getPlayerIndex(), Coord3D{ home.x + 250.0f, home.y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(wk, error);
	REQUIRE(wk->getExperienceTracker());
	wk->getExperienceTracker()->gainExpForLevel(9, true, false);
	Object *b = g.game->createObject("GondorBoromir", g.enemy->getPlayerIndex(), Coord3D{ home.x + 330.0f, home.y, 0.0f }, 3.14f, &error);
	REQUIRE_MESSAGE(b, error);
	s.witchKing = wk->getID();
	s.boromir = b->getID();
	return s;
}

GameMessage toggle(Game &g, ObjectID wk)
{
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityWitchkingMorgulBlade");
	REQUIRE(t);
	GameMessage m(MSG_DO_AUTO_ABILITY, g.local->getPlayerIndex());
	m.appendIntegerArgument((int)t->getID());
	m.appendObjectIDArgument(wk);
	return m;
}

AutoAbilityBehavior *bladeModule(Game &g, ObjectID wk)
{
	Object *o = g.game->logic().findObjectByID(wk);
	return o ? AutoAbilityBehavior::forPower(*o, "SpecialAbilityWitchkingMorgulBlade") : nullptr;
}

// the depth-first walk of a script list as read from a file (ScriptItem: a script or a group)
const Script *findInItems(const std::vector<ScriptItem> &items, const std::string &name)
{
	for (const ScriptItem &it : items)
	{
		if (it.script && it.script->name == name)
		{
			return it.script.get();
		}
		if (it.group)
		{
			if (const Script *s = findInItems(it.group->items, name))
			{
				return s;
			}
		}
	}
	return nullptr;
}

// the depth-first walk of a side's scripts (ScriptEngine::RGroup)
const Script *findLoadedScript(const ScriptEngine::RGroup &g, const std::string &name)
{
	for (const auto &s : g.scripts)
	{
		if (s->def && s->def->name == name)
		{
			return s->def;
		}
	}
	for (const auto &sub : g.groups)
	{
		if (const Script *s = findLoadedScript(*sub, name))
		{
			return s;
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("autocast game retail: a recorded skirmish with the Morgul Blade toggled on and off by commands replays with every frame's hash equal")
{
	OPENBFME_REQUIRE_START(s);
	const int kFrames = 520, kOn = 30, kOff = 400;
	const std::string path = "autocast1_replay.replay";
	const NewGameMessage message = duel(*s, "FactionAngmar", "FactionMen", 4242u);
	std::uint32_t recordedFinal = 0;
	unsigned long long recordedCasts = 0;
	UnsignedInt onFrame = 0;
	{
		Game g;
		g.load(*s, message);
		const Setup ids = setUp(g);
		ReplayWriter writer;
		ReplayHeader h;
		h.game = message;
		h.recordingSlot = 0;
		std::string error;
		REQUIRE_MESSAGE(writer.open(path, h, &error), error);
		LockstepDriver driver(nullptr, &writer);
		g.game->setFrameDriver(&driver);
		float hp0 = -1.0f;
		for (int i = 0; i < kFrames; ++i)
		{
			if (i == kOn || i == kOff)
			{
				if (i == kOn)
				{
					onFrame = g.game->frame();
					Object *b = g.game->logic().findObjectByID(ids.boromir);
					REQUIRE(b);
					hp0 = b->getBodyModule()->getHealth();
				}
				g.game->commands().append(toggle(g, ids.witchKing));
			}
			REQUIRE(g.game->advance(0.2) == 1);
			if (i == kOn + 2)
			{
				REQUIRE(bladeModule(g, ids.witchKing));
				CHECK(bladeModule(g, ids.witchKing)->autoButton() == "Command_WitchkingMorgulBlade");
			}
		}
		AutoAbilityBehavior *aa = bladeModule(g, ids.witchKing);
		REQUIRE(aa);
		CHECK(aa->autoButton().empty()); // toggled off at kOff
		recordedCasts = aa->castRecord().casts;
		CHECK(recordedCasts >= 1);
		CHECK(aa->castRecord().target == ids.boromir);
		Object *b = g.game->logic().findObjectByID(ids.boromir);
		CHECK((!b || b->getBodyModule()->getHealth() < hp0));
		MESSAGE("recorded: " << recordedCasts << " auto casts, the first at frame " << aa->castRecord().frame << " (toggle at " << onFrame << ")");
		recordedFinal = g.game->logic().computeStateHash();
		writer.close(g.game->frame(), recordedFinal);
		CHECK(writer.errors().empty());
	}
	ReplayFile replay;
	std::string error;
	REQUIRE_MESSAGE(ReplayFile::load(path, replay, &error), error);
	CHECK(replay.complete);
	CHECK(replay.hashes.size() == (size_t)kFrames);
	unsigned toggles = 0;
	UnsignedInt toggleFrame = 0;
	for (const auto &f : replay.frames)
	{
		for (const GameMessage &m : f.second)
		{
			if (m.getType() == MSG_DO_AUTO_ABILITY)
			{
				toggleFrame = toggles == 0 ? f.first : toggleFrame;
				++toggles;
			}
		}
	}
	CHECK(toggles == 2);
	auto play = [&](const ReplayFile &r, unsigned long long &casts) {
		Game g;
		g.load(*s, r.header.game);
		const Setup ids = setUp(g);
		auto playback = std::make_unique<ReplayPlayback>(r);
		RegisterLogicCRCHandler(g.game->dispatch());
		g.game->setFrameDriver(playback.get());
		while (!playback->finished(g.game->logic()))
		{
			g.game->advance(0.2);
		}
		AutoAbilityBehavior *aa = bladeModule(g, ids.witchKing);
		casts = aa ? aa->castRecord().casts : 0;
		g.game->setFrameDriver(nullptr);
		return playback;
	};
	{
		unsigned long long casts = 0;
		std::unique_ptr<ReplayPlayback> p = play(replay, casts);
		CHECK(p->hashesCompared() == (unsigned long long)kFrames);
		CHECK_FALSE(p->hasMismatch());
		CHECK(casts == recordedCasts);
	}
	{
		// the replay without its first toggle: the playback differs first right after the toggle's frame (the auto button is in the hash), and casts nothing
		ReplayFile cut = replay;
		std::vector<GameMessage> &cmds = cut.frames[toggleFrame];
		for (size_t i = 0; i < cmds.size(); ++i)
		{
			if (cmds[i].getType() == MSG_DO_AUTO_ABILITY)
			{
				cmds.erase(cmds.begin() + (long)i);
				break;
			}
		}
		unsigned long long casts = 0;
		std::unique_ptr<ReplayPlayback> p = play(cut, casts);
		REQUIRE(p->hasMismatch());
		CHECK(p->firstMismatch().frame == toggleFrame + 1);
		MESSAGE("without the toggle: first difference at frame " << p->firstMismatch().frame << " (toggle recorded at frame " << toggleFrame << ")");
	}
	std::remove(path.c_str());
}

TEST_CASE("autocast game retail: the skirmish AI never switches autocast on: lib_hero_behaviors is not loaded, and its TEAM_SET_AUTO_ABILITY names AutoAbility No buttons")
{
	OPENBFME_REQUIRE_START(s);
	// RotWK's computer players are the C++ SkirmishAI (PlayerTemplate DefaultPlayerAIType); the hero behaviour library is referenced only by the
	// obsolete libraries\__old_ai_scripts\ai_* maps, so a skirmish's sides do not hold its scripts
	Game g;
	g.load(*s, duel(*s, "FactionAngmar", "FactionMen", 7u));
	ScriptEngine &e = g.game->logic().scriptEngine();
	const std::string name = "b_Faramir - Use Captain of Gondor when Ready";
	for (const ScriptEngine::RSide &sd : e.sides())
	{
		CHECK_MESSAGE(findLoadedScript(sd.root, name) == nullptr, "side " << sd.name << " loaded lib_hero_behaviors");
	}
	// the library itself, read as the script setup reads a library map (MapReader, its second side's list, RW 0x731426)
	std::vector<std::uint8_t> bytes;
	std::string error;
	const std::string path = "libraries\\lib_hero_behaviors\\lib_hero_behaviors.map";
	REQUIRE_MESSAGE(s->mount->fs->readFile(path, bytes, &error), error);
	LoadedMap lib;
	REQUIRE_MESSAGE(MapReader::load(bytes, path, MapReadOptions(), lib, &error), error);
	REQUIRE(lib.playerScripts.lists.size() >= 2);
	const Script *script = findInItems(lib.playerScripts.lists[1].items, name);
	REQUIRE(script);
	CHECK(script->isSubroutine);
	const ScriptActionRec *act = nullptr;
	for (const ScriptActionRec &a : script->actions)
	{
		act = a.internalName == "TEAM_SET_AUTO_ABILITY" ? &a : act;
	}
	REQUIRE(act);
	REQUIRE(act->params.size() == 3);
	CHECK(act->params[0].stringValue == "<This Team>");
	CHECK(act->params[1].stringValue == "Command_SpecialAbilityCaptainOfGondor");
	CHECK(act->params[2].intValue == 1);
	const CommandButton *captain = TheCommandStore->findCommandButton("Command_SpecialAbilityCaptainOfGondor");
	REQUIRE(captain);
	CHECK_FALSE(captain->m_autoAbility); // retail 2.01 data: AutoAbility = No
	// the record run for a computer's Faramir as the AI's team script would ("<This Team>" is the calling team, ScriptEngine::runScript RW 0x60BD42):
	// RW 0x7C3DD8: the button's AutoAbility (+ 0x10C) is 0, nothing
	const Coord3D home = homeOf(*g.game, g.enemy);
	Object *faramir = g.game->createObject("GondorFaramir", g.enemy->getPlayerIndex(), Coord3D{ home.x + 200.0f, home.y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(faramir, error);
	g.game->advance(0.2);
	AutoAbilityBehavior *aa = AutoAbilityBehavior::first(*faramir);
	REQUIRE(aa);
	REQUIRE(faramir->getTeam());
	{
		ScriptEngine::ThisTeamScope scope(e, faramir->getTeam());
		e.executeAction(*act);
	}
	CHECK(aa->autoButton().empty());
	CHECK(e.stats().unportedActions.count("TEAM_SET_AUTO_ABILITY") == 0);
	// the same record naming one of his AutoAbility Yes buttons (the Wound Arrow) does switch it on
	const CommandSet *set = TheCommandStore->findCommandSet(faramir->getCommandSetName());
	REQUIRE(set);
	const CommandButton *autoButton = nullptr;
	for (int i = 0; i < CommandSet::MAX_BUTTONS && !autoButton; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		autoButton = b && b->m_autoAbility ? b : nullptr;
	}
	REQUIRE(autoButton);
	ScriptActionRec yes = *act;
	yes.params[1].stringValue = autoButton->m_name;
	{
		ScriptEngine::ThisTeamScope scope(e, faramir->getTeam());
		e.executeAction(yes);
	}
	CHECK(aa->autoButton() == autoButton->m_name);
}

TEST_CASE("autocast game retail: the Evil campaign's Erebor turns Gloin's slam autocast on through its loaded script (NAME_SET_AUTO_ABILITY, RW 0x7C3C88)")
{
	OPENBFME_REQUIRE_START(s);
	campaigntest::Mission m;
	m.load(*s, "map evil erebor");
	ScriptEngine &e = m.engine();
	const std::string name = "Set Gloin smash to autoability";
	const Script *script = nullptr;
	for (const ScriptEngine::RSide &sd : e.sides())
	{
		script = script ? script : findLoadedScript(sd.root, name);
	}
	REQUIRE_MESSAGE(script, "map evil erebor has no script \"" << name << "\"");
	const CommandButton *slam = TheCommandStore->findCommandButton("Command_SpecialAbilityDwarvenGloinSlam");
	REQUIRE(slam);
	CHECK(slam->m_autoAbility);
	// the map spawns Gloin by script once the player's army reaches a trigger area ("Gloin Arrives": DwarvenGloin named Gloin, Dwalins retinue, the
	// dwarves' side); the test stands in for that spawn: the same template and name for the same side, then the map's own one-shot script runs
	m.step(5);
	CHECK(e.getUnitNamed("Gloin") == nullptr);
	Player *dwarves = m.logic().players().findPlayerWithName("PlyrDwarves");
	REQUIRE_MESSAGE(dwarves, "map evil erebor has no PlyrDwarves");
	Coord3D at{ 0.0f, 0.0f, 0.0f };
	for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == dwarves)
		{
			at = *o->getPosition();
			break;
		}
	}
	std::string error;
	Object *gloin = m.game->createObject("DwarvenGloin", dwarves->getPlayerIndex(), Coord3D{ at.x + 60.0f, at.y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(gloin, error);
	gloin->setName("Gloin");
	AutoAbilityBehavior *aa = AutoAbilityBehavior::first(*gloin);
	REQUIRE(aa);
	CHECK(aa->autoButton().empty());
	for (int i = 0; i < 60 && aa->autoButton().empty(); ++i)
	{
		m.step(1);
	}
	CHECK(aa->autoButton() == "Command_SpecialAbilityDwarvenGloinSlam"); // only the map's script sets it (nothing else names Gloin's slam)
	CHECK(e.stats().unportedActions.count("NAME_SET_AUTO_ABILITY") == 0);
	MESSAGE("Gloin's slam autocast on at frame " << m.frame());
}
