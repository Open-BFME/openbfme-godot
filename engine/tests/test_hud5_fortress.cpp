// OpenBFME retail tests, lane HUD-5 (the owner's report: on Helm's Deep the player got a normal castle instead of the map's own fortress). GPL-3.0.
//
// RW 0x62AD24 (placeNetworkBuildingsForPlayer): a start waypoint whose + 0x60 (the map object's waypointType, TerrainLogic::addWaypoint RW 0x682D4C) is not zero
// gets no StartingBuilding; the slot's side is "Player_<start>" (SkirmishSides, RW 0x627C1F), so it owns the map's objects of that side (the fortress); without a
// slot on that start, those objects fall to the neutral default team (RW 0x6A9199). The expected data (the fortress maps and their typed start waypoints) is
// read independently from the retail maps by tools/maps/oracle (the waypointType of every Player_N_Start of every multiplayer map: 5 on Player_1_Start of the
// eleven maps below, none elsewhere). The tests SKIP loudly without the installs.

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/NewGame/StartingBase.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace starttest;

namespace
{
// the multiplayer maps whose Player_1_Start carries waypointType 5 (tools/maps/oracle, the retail maps), with their player count
const std::vector<std::pair<std::string, int>> &fortressMaps()
{
	static const std::vector<std::pair<std::string, int>> maps = {
		{ "maps/map mp amon sul fortress/map mp amon sul fortress.map", 3 },
		{ "maps/map wor ang carn dum/map wor ang carn dum.map", 2 },
		{ "maps/map wor ang fornost/map wor ang fornost.map", 3 },
		{ "maps/map wor dol guldur/map wor dol guldur.map", 4 },
		{ "maps/map wor erebor/map wor erebor.map", 2 },
		{ "maps/map wor helms deep/map wor helms deep.map", 4 },
		{ "maps/map wor isengard/map wor isengard.map", 2 },
		{ "maps/map wor minas morgul/map wor minas morgul.map", 3 },
		{ "maps/map wor minas tirith/map wor minas tirith.map", 4 },
		{ "maps/map wor rivendell/map wor rivendell.map", 3 },
	};
	return maps;
}

const char *const kFactions[] = { "FactionMen", "FactionMordor", "FactionElves", "FactionIsengard" };

int templateIndex(Shared &s, const std::string &name)
{
	for (int i = 0; i < (int)s.world->playerTemplates().getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = s.world->playerTemplates().getNthPlayerTemplate(i);
		if (pt && pt->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

// slot i at start starts[i] with faction kFactions[i % 4], slot 0 human; explicit colours
NewGameMessage fortressMessage(Shared &s, const std::string &map, const std::vector<int> &starts)
{
	NewGameMessage m;
	m.game.mapName = map;
	m.game.seed = 5;
	m.game.startingCash = 1000;
	for (size_t i = 0; i < starts.size(); ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[i];
		sl.state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
		sl.name = i == 0 ? u"Host" : u"Easy";
		sl.accepted = true;
		sl.playerTemplate = templateIndex(s, kFactions[i % 4]);
		REQUIRE(sl.playerTemplate >= 0);
		sl.startPos = starts[i];
		sl.color = (int)i;
	}
	return m;
}

void runGame(Shared &s, const NewGameMessage &message, const std::function<void(LiveGame &, const NewGameStart &, const LiveGame::Report &)> &inspect)
{
	std::string error;
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	inspect(game, start, game.report());
}

// the objects `player` controls at frame 0, by template name
std::map<std::string, int> owned(LiveGame &game, const Player *player)
{
	std::map<std::string, int> out;
	for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player)
		{
			++out[o->getTemplate()->getName()];
		}
	}
	return out;
}
} // namespace

TEST_CASE("hud5 fortress maps retail: Player_1_Start's waypointType 5 means no StartingBuilding; the slot there owns the map's Player_1 fortress, the others get theirs")
{
	OPENBFME_REQUIRE_START(s);
	for (const auto &fm : fortressMaps())
	{
		CAPTURE(fm.first);
		std::vector<int> starts;
		for (int i = 0; i < fm.second; ++i)
		{
			starts.push_back(i);
		}
		runGame(*s, fortressMessage(*s, fm.first, starts), [&](LiveGame &game, const NewGameStart &start, const LiveGame::Report &report) {
			const TerrainLogic *terrain = game.logic().terrain();
			REQUIRE(terrain);
			for (int i = 0; i < fm.second; ++i)
			{
				const std::string wpName = "Player_" + std::to_string(i + 1) + "_Start";
				const Waypoint *wp = terrain->findWaypointByName(wpName);
				REQUIRE_MESSAGE(wp, wpName);
				CHECK(wp->type == (i == 0 ? 5 : 0)); // the map data, as the independent oracle read it
				Player *p = game.players().findPlayerWithName(report.startSlotPlayers[(size_t)i]);
				REQUIRE(p);
				CHECK(p->getPlayerName() == "Player_" + std::to_string(start.message.game.slots[i].startPos + 1));
				const PlayerTemplate *pt = p->getPlayerTemplate();
				REQUIRE(pt);
				const std::map<std::string, int> objects = owned(game, p);
				const bool hasFortress = objects.count(pt->m_startingBuilding) != 0;
				int structures = 0;
				for (const StartingBase::Placed &pl : report.startingObjects)
				{
					structures += pl.structure && game.logic().findObjectByID(pl.id)->getControllingPlayer() == p ? 1 : 0;
				}
				int unitCount = 0;
				for (const std::string &u : pt->m_startingUnit)
				{
					unitCount += u.empty() ? 0 : 1;
				}
				if (i == 0)
				{
					// no StartingBuilding at a typed start; the map's own Player_1 objects are this player's (more than its starting units)
					CHECK_FALSE(hasFortress);
					CHECK(structures == 0);
					int total = 0;
					for (const auto &kv : objects)
					{
						total += kv.second;
					}
					CHECK(total > unitCount);
				}
				else
				{
					CHECK(hasFortress);
					CHECK(structures == 1);
				}
			}
			for (const std::string &e : report.startErrors)
			{
				MESSAGE(e);
			}
		});
	}
}

TEST_CASE("hud5 fortress maps retail: Helm's Deep's gates and keep are the start-1 player's; with start 1 empty they are the neutral player's (RW 0x6A9199)")
{
	OPENBFME_REQUIRE_START(s);
	const std::string helms = "maps/map wor helms deep/map wor helms deep.map";
	auto findNamed = [](LiveGame &game, const std::string &name) -> Object * {
		for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getName() == name)
			{
				return o;
			}
		}
		return nullptr;
	};
	runGame(*s, fortressMessage(*s, helms, { 0, 1, 2, 3 }), [&](LiveGame &game, const NewGameStart &, const LiveGame::Report &report) {
		Player *p1 = game.players().findPlayerWithName(report.startSlotPlayers[0]);
		REQUIRE(p1);
		for (const char *name : { "Main Gate", "Inner Keep Gate", "Gatehouse Left", "Gatehouse Right" }) // objectName of the map's Player_1 objects
		{
			Object *o = findNamed(game, name);
			REQUIRE_MESSAGE(o, name);
			CHECK(o->getControllingPlayer() == p1);
		}
		CHECK(owned(game, p1).count("MenFortress") == 0);
	});
	runGame(*s, fortressMessage(*s, helms, { 1, 2 }), [&](LiveGame &game, const NewGameStart &, const LiveGame::Report &) {
		Object *gate = findNamed(game, "Main Gate");
		REQUIRE(gate);
		CHECK(gate->getControllingPlayer() == game.players().getNeutralPlayer());
	});
}

namespace
{
// the map objects `player` controls at frame 0 (not the starting structures and units of StartingBase, nor the player's own spell book object, its faction's
// <Side>SpellBook at the origin): template and position, sorted
std::vector<std::string> mapObjectsOf(LiveGame &game, const LiveGame::Report &report, const Player *player)
{
	std::set<unsigned> placed;
	for (const StartingBase::Placed &pl : report.startingObjects)
	{
		placed.insert(pl.id);
	}
	std::vector<std::string> out;
	for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
	{
		const std::string &name = o->getTemplate()->getName();
		const bool spellBook = name.size() > 9 && name.compare(name.size() - 9, 9, "SpellBook") == 0;
		if (o->getControllingPlayer() == player && !placed.count((unsigned)o->getID()) && !spellBook)
		{
			const Coord3D &p = *o->getPosition();
			out.push_back(o->getTemplate()->getName() + "@" + std::to_string((int)std::lround(p.x)) + "," + std::to_string((int)std::lround(p.y)));
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

// the slot at `start` in `message`
int slotAt(const NewGameMessage &message, int start)
{
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		if (message.game.slots[i].state != SLOT_OPEN && message.game.slots[i].state != SLOT_CLOSED && message.game.slots[i].startPos == start)
		{
			return i;
		}
	}
	return -1;
}
} // namespace

// Sol's review (HUD-5 r1): whoever takes start 1 owns the fortress - the human, a computer player or a second human - with the same map objects exactly; the
// other starts get their StartingBuilding and none of the fortress' objects
TEST_CASE("hud5 fortress maps retail: the fortress' objects are exactly the start-1 slot's, human or computer, with the human slot swapped to another start")
{
	OPENBFME_REQUIRE_START(s);
	for (const auto &fm : fortressMaps())
	{
		CAPTURE(fm.first);
		std::vector<std::string> reference;
		// 0: the human at start 1 (index 0); 1: the human at start 2, a computer at start 1; 2: two humans, the second at start 1
		for (int variant = 0; variant < 3; ++variant)
		{
			CAPTURE(variant);
			NewGameMessage m = fortressMessage(*s, fm.first, { variant == 0 ? 0 : 1, variant == 0 ? 1 : 0 });
			if (variant == 2)
			{
				m.game.slots[1].state = SLOT_PLAYER;
				m.game.slots[1].name = u"Guest";
			}
			runGame(*s, m, [&](LiveGame &game, const NewGameStart &, const LiveGame::Report &report) {
				const int fortressSlot = slotAt(m, 0), otherSlot = slotAt(m, 1);
				REQUIRE(fortressSlot >= 0);
				REQUIRE(otherSlot >= 0);
				Player *fortress = game.players().findPlayerWithName(report.startSlotPlayers[(size_t)fortressSlot]);
				Player *other = game.players().findPlayerWithName(report.startSlotPlayers[(size_t)otherSlot]);
				REQUIRE(fortress);
				REQUIRE(other);
				CHECK(fortress->getPlayerName() == "Player_1");
				CHECK(fortress->getPlayerType() == (m.game.slots[fortressSlot].state == SLOT_PLAYER ? PLAYER_HUMAN : PLAYER_COMPUTER));
				const std::vector<std::string> objects = mapObjectsOf(game, report, fortress);
				CHECK_FALSE(objects.empty());
				if (variant == 0)
				{
					reference = objects;
				}
				else
				{
					std::vector<std::string> onlyHere, onlyRef;
					std::set_difference(objects.begin(), objects.end(), reference.begin(), reference.end(), std::back_inserter(onlyHere));
					std::set_difference(reference.begin(), reference.end(), objects.begin(), objects.end(), std::back_inserter(onlyRef));
					std::string diff;
					for (size_t k = 0; k < onlyHere.size() && k < 6; ++k) diff += " +" + onlyHere[k];
					for (size_t k = 0; k < onlyRef.size() && k < 6; ++k) diff += " -" + onlyRef[k];
					CAPTURE(diff);
					CHECK(objects == reference); // the same map objects, whoever holds start 1
				}
				CHECK(owned(game, fortress).count(fortress->getPlayerTemplate()->m_startingBuilding) == 0);
				// the other start: its StartingBuilding, none of the fortress' objects
				CHECK(owned(game, other).count(other->getPlayerTemplate()->m_startingBuilding) == 1);
				const std::vector<std::string> others = mapObjectsOf(game, report, other);
				std::vector<std::string> both;
				std::set_intersection(objects.begin(), objects.end(), others.begin(), others.end(), std::back_inserter(both));
				CHECK(both.empty());
			});
		}
	}
}
