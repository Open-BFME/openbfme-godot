// OpenBFME retail tests: starting a skirmish from the real menu (lane START-1).  They mount pure RotWK 2.01 + BFME2 1.06 (SKIP when ROTWK_INSTALL / BFME2_INSTALL are
// unset), click from MainMenu through Skirmish with input events (as the APT-4 acceptance tests do), press Start, and run the logic's consumer of the new-game message:
// the random resolution in retail's order, the sides and players, the map's objects and every player's starting base, with the load progress milestones.  GPL-3.0.

#include "doctest.h"

#include "StartShellFx.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/NewGame/StartingBase.h"
#include "GameLogic/NewGame/SkirmishRandom.h"
#include "GameLogic/Object/Object.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include "StartGolden.h"

#include <cstdio>
#include <map>
#include <set>

using namespace starttest;
using namespace startgolden;

namespace
{
// the lobby clicked from the main menu to a NewGameMessage: map `mapKey`, slot 0 the faction `faction` (a template name; "" = Random), slot 1 an AI of the row `aiRow`
// of its Player combo (Open, Closed, Easy, Medium, Hard, Brutal -> 2..5), the colour of slot 0 the row `colorRow` of the image combo (0 = random)
NewGameMessage lobbyMessage(Shared &s, const std::string &mapKey, const std::string &faction, int aiRow, int colorRow, std::uint32_t seed, std::string *error)
{
	ShellFx fx(s, seed);
	fx.shell->push("MainMenu.apt");
	fx.tick(10);
	REQUIRE(fx.shell->screenCount() == 1); // MainMenu (AptLevel0 is the window manager's slot 0)
	const int menuLevel = fx.shell->top()->level();
	std::string asError;
	// the engine reveals the main menu (S-139: who calls ShowMainMenu is not read)
	REQUIRE_MESSAGE(fx.wm->invokeAS(menuLevel, "ShowMainMenu", {}, nullptr, &asError), asError);
	fx.tick(60);
	clickNavItem(fx, menuLevel, "SoloPlayNav", "Skirmish");
	REQUIRE(fx.shell->errors().empty());
	auto *screen = dynamic_cast<AptSkirmish *>(fx.shell->top());
	REQUIRE(screen);
	fx.tick(60);
	REQUIRE(screen->setup());
	// the profile popup: type a name and accept
	REQUIRE(screen->profileEntryWindow());
	typeText(fx, screen->profileEntryWindow(), u"Gimli");
	AptButtonInst *select = popupButton(fx, "ProfilePopup.Main.Select");
	REQUIRE(select);
	clickButton(fx, *select);
	fx.tick(40);
	// the map
	GameWindow *list = screen->mapListWindow();
	REQUIRE(list);
	const std::vector<std::string> keys = screen->mapListKeys();
	int row = -1;
	for (size_t i = 0; i < keys.size(); ++i)
	{
		if (keys[i] == mapKey)
		{
			row = (int)i;
		}
	}
	REQUIRE_MESSAGE(row >= 0, "the map " << mapKey << " is not in the lobby's list");
	wheelAndClickRow(fx, list, row);
	REQUIRE(screen->setup()->info().mapName == mapKey);
	if (!faction.empty())
	{
		int tIndex = -1;
		for (int i = 0; i < (int)fx.setup.factions().size(); ++i)
		{
			if (fx.setup.factions()[(size_t)i].templateName == faction)
			{
				tIndex = i;
			}
		}
		REQUIRE(tIndex >= 0);
		GameWindow *combo = screen->slotGadget(0, "PlayerTemplate");
		int pos = -1;
		for (int r = 0; r < GadgetComboBoxGetLength(combo); ++r)
		{
			if (GadgetComboBoxGetItemData(combo, r) == reinterpret_cast<void *>(static_cast<std::intptr_t>(tIndex)))
			{
				pos = r;
			}
		}
		REQUIRE(pos >= 0);
		chooseComboRow(fx, combo, pos);
	}
	chooseComboRow(fx, screen->slotGadget(1, "Player"), aiRow);
	if (colorRow > 0)
	{
		chooseImageComboRow(fx, screen->slotGadget(0, "Color"), colorRow);
	}
	AptButtonInst *start = popupButton(fx, "lobby.StartGame");
	REQUIRE(start);
	REQUIRE(fx.sink.messages.empty());
	clickButton(fx, *start);
	fx.tick(30);
	REQUIRE_MESSAGE(fx.sink.messages.size() == 1, "refusals " << fx.sink.refusals.size());
	(void)error;
	return fx.sink.messages[0];
}
} // namespace

namespace
{
const char kEvendimKey[] = "maps/map mp evendim/map mp evendim.map";

// one game from a prepared start: the load, the progress calls, the per-frame state hashes
struct StartedGame
{
	NewGameStart start;
	std::vector<int> progress;
	std::vector<std::uint32_t> hashes; // frame 1 (the load's first frame), then after each advanced frame
	LiveGame::Report report;
	StartedGame() : start(RandomAlgorithm::ZH_CarryChain) {}
};

void runStartedGame(Shared &s, const NewGameMessage &message, int frames, StartedGame &out, const std::function<void(LiveGame &)> &inspect)
{
	std::string error;
	const std::vector<MapCacheEntry> *maps = nullptr;
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	maps = &cache;
	REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, *maps, RandomAlgorithm::ZH_CarryChain, out.start, &error), error);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	o.progress = [&out](int p) { out.progress.push_back(p); };
	REQUIRE_MESSAGE(game.load(o, &error), error);
	out.hashes.push_back(game.logic().computeStateHash());
	for (int i = 0; i < frames; ++i)
	{
		game.advance(0.2);
		out.hashes.push_back(game.logic().computeStateHash());
	}
	out.report = game.report();
	if (inspect)
	{
		inspect(game);
	}
}
} // namespace

TEST_CASE("retail start: the same 30 (scenario, seed) vectors as the synthetic test resolve identically on the RETAIL store, colours and map cache")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	std::vector<MapCacheEntry> cache;
	REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	const MapCacheEntry *entry = nullptr;
	for (const MapCacheEntry &m : cache)
	{
		if (m.name == kEvendimKey)
		{
			entry = &m;
		}
	}
	REQUIRE(entry);
	CHECK(entry->numPlayers == 8);
	REQUIRE(s->world->playerTemplates().getPlayerTemplateCount() == 12);
	REQUIRE(s->settings.multiplayerColors.size() == 10);
	SkirmishResolveInput in;
	in.templates = &s->world->playerTemplates();
	in.numColors = (int)s->settings.multiplayerColors.size();
	in.map = entry;
	for (const Golden &g : goldens())
	{
		SkirmishGameInfo info = makeInfo(scenario(g.name));
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.enableCallLog(true);
		rng.initGameLogicRandom(g.seed, -1);
		INFO(g.name << " seed " << g.seed);
		REQUIRE_MESSAGE(SkirmishRandom::resolve(info, in, rng, &error), error);
		size_t k = 0;
		for (const SkirmishGameSlot &sl : info.slots)
		{
			if (!sl.isOccupied())
			{
				continue;
			}
			REQUIRE(k < g.slots.size());
			CHECK(sl.startPos == g.slots[k][0]);
			CHECK(sl.playerTemplate == g.slots[k][1]);
			CHECK(sl.color == g.slots[k][2]);
			++k;
		}
		REQUIRE(rng.callLog().size() == g.draws.size());
		for (size_t i = 0; i < g.draws.size(); ++i)
		{
			CHECK(rng.callLog()[i].result == g.draws[i][2]);
		}
	}
}

TEST_CASE("retail start: from the main menu through Skirmish and Start to the live game: every slot's player, faction, team, colour, start position, money and starting base")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	// the lobby: Evendim, slot 0 Elves with the second colour (Red), slot 1 the Brutal AI with a random faction and colour; the seed 4242 is the lobby's
	const NewGameMessage message = lobbyMessage(*s, kEvendimKey, "FactionElves", 5, 2, 4242, &error);
	CHECK(message.game.startingCash == 1500);
	StartedGame g;
	runStartedGame(*s, message, 3, g, [&](LiveGame &game) {
		const SkirmishGameInfo &r = g.start.message.game;
		// the resolution (independent model, seed 4242, slot 0 Elves + Red, slot 1 random): positions 1 and 6, Dwarves, colour 4
		CHECK(r.slots[0].startPos == 1);
		CHECK(r.slots[0].playerTemplate == 5);
		CHECK(r.slots[0].color == 1);
		CHECK(r.slots[1].startPos == 6);
		CHECK(r.slots[1].playerTemplate == 6);
		CHECK(r.slots[1].color == 4);
		CHECK(r.slots[0].origStartPos == -1);
		CHECK(r.slots[1].origPlayerTemplate == -1);
		CHECK(r.unresolvedRandomChoices().empty());
		PlayerList &players = game.players();
		Player *p2 = players.findPlayerWithName("Player_2");
		Player *p7 = players.findPlayerWithName("Player_7");
		REQUIRE(p2);
		REQUIRE(p7);
		CHECK(g.report.startSlotPlayers[0] == "Player_2");
		CHECK(g.report.startSlotPlayers[1] == "Player_7");
		CHECK(g.report.startSlotPlayers[2].empty());
		CHECK(p2->getPlayerTemplate()->getName() == "FactionElves");
		CHECK(p7->getPlayerTemplate()->getName() == "FactionDwarves");
		CHECK(p2->getPlayerType() == PLAYER_HUMAN);
		CHECK(p7->getPlayerType() == PLAYER_COMPUTER);
		CHECK(p7->isSkirmishAI());
		CHECK(p7->getSkirmishDifficulty() == 3);
		CHECK(players.getLocalPlayer() == p2);
		CHECK(p2->getMultiplayerStartIndex() == 1);
		CHECK(p7->getMultiplayerStartIndex() == 6);
		// the lobby's colours (ColorRed 158,56,42 and ColorOrange 206,135,69), day and night
		CHECK(p2->getPlayerColor() == 0xFF9E382Au);
		CHECK(p7->getPlayerColor() == 0xFFCE8745u);
		CHECK(p2->getPlayerNightColor() == 0xFF9E382Au);
		// money: the game's starting cash for both (the templates' StartMoney is 0)
		CHECK(p2->getMoney()->countMoney() == 1500u);
		CHECK(p7->getMoney()->countMoney() == 1500u);
		// no teams chosen: enemies
		CHECK(p2->getRelationship(p7) == ENEMIES);
		CHECK(p7->getRelationship(p2) == ENEMIES);
		CHECK(p2->getRelationship(players.getNeutralPlayer()) == NEUTRAL);
		// the sides the retail start keeps and adds: the neutral player, the civilian and creeps sides, the two slots, ReplayObserver
		CHECK(players.findPlayerWithName("PlyrCivilian"));
		CHECK(players.findPlayerWithName("ReplayObserver"));
		CHECK_FALSE(players.findPlayerWithName("Player_1"));
		CHECK_FALSE(players.findPlayerWithName("Player_3"));
		// the starting bases: a fortress on the start waypoint and the porters at their offsets
		const TerrainLogic *terrain = game.logic().terrain();
		REQUIRE(terrain);
		struct Expect
		{
			const char *player;
			const char *structure;
			int startIndex;
			std::vector<std::string> units;
		};
		for (const Expect &e : { Expect{ "Player_2", "ElvenFortress", 2, { "ElvenPorter", "ElvenPorter" } }, Expect{ "Player_7", "DwarvenFortress", 7, { "DwarvenPorter", "DwarvenPorter" } } })
		{
			INFO(e.player);
			Player *p = players.findPlayerWithName(e.player);
			const Waypoint *wp = terrain->findWaypointByName("Player_" + std::to_string(e.startIndex) + "_Start");
			REQUIRE(wp);
			const PlayerTemplate *pt = p->getPlayerTemplate();
			std::vector<const StartingBase::Placed *> mine;
			for (const StartingBase::Placed &pl : g.report.startingObjects)
			{
				const ::Object *o = game.logic().findObjectByID(pl.id);
				REQUIRE(o);
				if (o->getControllingPlayer() == p)
				{
					mine.push_back(&pl);
				}
			}
			REQUIRE(mine.size() == 1 + e.units.size());
			CHECK(mine[0]->structure);
			CHECK(mine[0]->templateName == e.structure);
			CHECK(mine[0]->position.x == wp->location.x);
			CHECK(mine[0]->position.y == wp->location.y);
			CHECK(mine[0]->position.z == game.logic().getGroundHeight(wp->location.x, wp->location.y));
			const ::Object *yard = game.logic().findObjectByID(mine[0]->id);
			// lane CASTLE-1: the load's first logic frame unpacked the fortress, and the unpack moves the script name BASE_FLAG_n to the keep and renames the centre "No Name"
			// (RW 0x79C0FD: ScriptEngine RW 0x759467, the string RW 0xC30F30)
			const CastleBehavior *castle = dynamic_cast<const CastleBehavior *>(yard->findModule("CastleBehavior"));
			REQUIRE(castle);
			REQUIRE(castle->state() == CastleBehavior::STATE_UNPACKED);
			const ::Object *keep = game.logic().findObjectByID(castle->keepId());
			REQUIRE(keep);
			CHECK(keep->getName() == "BASE_FLAG_" + std::to_string(e.startIndex));
			CHECK(yard->getName() == "No Name");
			int unit = 0;
			for (int i = 0; i < PlayerTemplate::NUM_STARTING_UNITS; ++i)
			{
				if (pt->m_startingUnit[i].empty())
				{
					continue;
				}
				REQUIRE(unit + 1 < (int)mine.size());
				const Coord3D want = StartingBase::unitPositionFromOffset(mine[0]->position, pt->m_startingUnitOffset[i]);
				CHECK(mine[(size_t)unit + 1]->templateName == pt->m_startingUnit[i]);
				CHECK(mine[(size_t)unit + 1]->position.x == want.x);
				CHECK(mine[(size_t)unit + 1]->position.y == want.y);
				++unit;
			}
			CHECK(unit == (int)e.units.size());
		}
		CHECK(g.report.startErrors.empty());
	});
	CHECK(g.report.errors.empty());
	// the stops of the start are reported at runtime
	for (const char *id : { "[S-270]", "[S-271]", "[S-272]", "[S-273]" })
	{
		bool found = false;
		for (const std::string &st : g.report.stops)
		{
			found = found || st.rfind(id, 0) == 0;
		}
		CHECK_MESSAGE(found, "the report lacks " << id);
	}
	// the load progress: monotonic, with the retail milestones, one more per slot, ending at 95 (the device layer adds 96 ff)
	REQUIRE_FALSE(g.progress.empty());
	for (size_t i = 1; i < g.progress.size(); ++i)
	{
		CHECK(g.progress[i] >= g.progress[i - 1]);
	}
	CHECK(g.progress == std::vector<int>{ 1, 2, 12, 13, 14, 30, 40, 41, 42, 95 });
	CHECK(g.report.progressCalls == g.progress);
	// the logic has run: the hashes of the four frames differ from each other (the world moves) and the first equals the load's own
	std::set<std::uint32_t> distinct(g.hashes.begin(), g.hashes.end());
	CHECK(distinct.size() >= 2);
	std::printf("  info: %zu objects, hashes %08x %08x %08x %08x\n", g.report.logic.objects, g.hashes[0], g.hashes[1], g.hashes[2], g.hashes[3]);
}

TEST_CASE("retail start: two runs of the same message agree on every frame hash; another seed or another slot setup changes the world (mutation checks)")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	const NewGameMessage message = lobbyMessage(*s, kEvendimKey, "FactionElves", 5, 2, 4242, &error);
	StartedGame a, b, c, d;
	runStartedGame(*s, message, 8, a, nullptr);
	runStartedGame(*s, message, 8, b, nullptr);
	CHECK(a.hashes == b.hashes);
	NewGameMessage seeded = message;
	seeded.game.seed = 4243;
	runStartedGame(*s, seeded, 8, c, nullptr);
	CHECK(a.hashes != c.hashes);
	NewGameMessage easy = message;
	easy.game.slots[1].state = SLOT_EASY_AI;
	runStartedGame(*s, easy, 8, d, nullptr);
	CHECK(a.hashes != d.hashes); // the AI level is player state
	// the second slot's faction changed by the seed: the starting structures differ
	CHECK(a.start.message.game.slots[1].playerTemplate != c.start.message.game.slots[1].playerTemplate);
}

TEST_CASE("retail start: teams, an observer and all-random slots: every slot resolved, allies and enemies as the lobby set them, an observer is no player of the match")
{
	OPENBFME_REQUIRE_START(s);
	NewGameMessage m;
	m.game.mapName = kEvendimKey;
	m.game.seed = 77;
	m.game.startingCash = 2500;
	const char16_t *names[] = { u"One", u"Two", u"Three", u"Four", u"Watcher" };
	for (int i = 0; i < 5; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[i];
		sl.state = i == 0 ? SLOT_PLAYER : (i == 4 ? SLOT_PLAYER : SLOT_MED_AI);
		sl.name = names[i];
		sl.accepted = true;
		sl.playerTemplate = i == 4 ? PLAYERTEMPLATE_OBSERVER : PLAYERTEMPLATE_RANDOM;
		sl.teamNumber = i < 4 ? i % 2 : -1;
	}
	StartedGame g;
	runStartedGame(*s, m, 2, g, [&](LiveGame &game) {
		PlayerList &players = game.players();
		const SkirmishGameInfo &r = g.start.message.game;
		CHECK(r.unresolvedRandomChoices().empty());
		std::set<int> positions, colours;
		std::vector<Player *> ps;
		for (int i = 0; i < 4; ++i)
		{
			positions.insert(r.slots[i].startPos);
			colours.insert(r.slots[i].color);
			Player *p = players.findPlayerWithName(g.report.startSlotPlayers[(size_t)i]);
			REQUIRE(p);
			CHECK(p->getMoney()->countMoney() == 2500u);
			ps.push_back(p);
		}
		CHECK(positions.size() == 4);
		CHECK(colours.size() == 5 - 1);
		// teams: slots 0 and 2 allied, 1 and 3 allied, the two teams enemies
		CHECK(ps[0]->getRelationship(ps[2]) == ALLIES);
		CHECK(ps[1]->getRelationship(ps[3]) == ALLIES);
		CHECK(ps[0]->getRelationship(ps[1]) == ENEMIES);
		CHECK(ps[2]->getRelationship(ps[3]) == ENEMIES);
		// the observer: a side named Observer_5 with the observer template and no starting base
		Player *obs = players.findPlayerWithName("Observer_5");
		REQUIRE(obs);
		CHECK(obs->isObserver());
		CHECK(g.report.startingObjects.size() == 4 * 3);
		for (const StartingBase::Placed &pl : g.report.startingObjects)
		{
			CHECK(game.logic().findObjectByID(pl.id)->getControllingPlayer() != obs);
		}
	});
	CHECK(g.report.errors.empty());
}
