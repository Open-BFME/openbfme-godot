// OpenBFME unit tests. GPL-3.0.
// Lane CHEAT-1: the open Cheat Mode (GameLogic/CheatMode.h): MSG_OPENBFME_CHEAT runs only in a Cheat Mode game and only for the cheat seat's player; every
// command (run or refused) is logged and announced; the effects are exact and deterministic; a game without Cheat Mode keeps its hash. The wire, replay and
// snapshot forms of the setup's Cheat Mode. The games SKIP without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/CheatMode.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/VictoryConditions.h"
#include "GameNetwork/NetPacket.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace hudtest;
using CheatMode::Command;

namespace
{
int templateId(GameLogic &logic, const char *name)
{
	const ThingTemplate *tt = logic.things().findTemplate(name);
	REQUIRE_MESSAGE(tt != nullptr, name);
	return (int)tt->getTemplateID();
}

std::vector<Object *> objectsOf(GameLogic &logic, const Player *owner, const std::string &templateName)
{
	std::vector<Object *> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == owner && o->getTemplate()->getName() == templateName && !o->isEffectivelyDead())
		{
			out.push_back(o);
		}
	}
	return out;
}

const CheatMode::LogEntry &lastEntry(GameLogic &logic)
{
	REQUIRE(!logic.cheatMode().log().empty());
	return logic.cheatMode().log().back();
}

struct Run
{
	std::vector<std::uint32_t> hashes;
	std::uint32_t money = 0;
	std::vector<std::string> lines;
};

// `cheats`: the game is a Cheat Mode game with Player_1 (the local player) in the cheat seat; `commands`: the scripted commands are sent
Run play(SharedWorld &s, bool cheats, bool commands)
{
	Run out;
	Rig r(s);
	GameLogic &logic = r.logic();
	Player *host = r.local;
	Player *other = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(other != nullptr);
	if (cheats)
	{
		logic.cheatMode().configure(0, host->getPlayerIndex());
	}
	r.frame(2);
	const int h = host->getPlayerIndex(), o = other->getPlayerIndex();
	if (commands)
	{
		r.game->commands().append(CheatMode::makeMessage(h, Command::AddResources, h, 2000));
		r.game->commands().append(CheatMode::makeMessage(o, Command::AddResources, o, 5000)); // not the cheat seat: refused
		r.game->commands().append(CheatMode::makeMessage(h, Command::AddCommandPoints, h, 50));
		r.game->commands().append(CheatMode::makeMessage(h, Command::AddPowerPoints, h, 7));
	}
	for (int i = 0; i < 10; ++i)
	{
		r.frame(1);
		out.hashes.push_back(logic.computeStateHash());
	}
	out.money = host->getMoney()->countMoney();
	for (const CheatMode::LogEntry &e : logic.cheatMode().log())
	{
		out.lines.push_back(e.text);
	}
	return out;
}

} // namespace

TEST_CASE("cheat1 mode: a game without Cheat Mode refuses every command, announces the refusal and keeps its hash")
{
	if (!haveWorld("cheat1 mode"))
	{
		return;
	}
	SharedWorld &s = shared();
	const Run quiet = play(s, false, false);
	const Run refused = play(s, false, true);
	CHECK(refused.hashes == quiet.hashes); // nothing ran and nothing is hashed for a game without Cheat Mode
	CHECK(refused.money == quiet.money);
	REQUIRE(refused.lines.size() == 4);
	for (const std::string &line : refused.lines)
	{
		CHECK(line == "Cheat Mode: refused: this is not a Cheat Mode game");
	}
}

TEST_CASE("cheat1 mode: resources, command points and power points go to the target exactly; another seat is refused; deterministic")
{
	if (!haveWorld("cheat1 mode"))
	{
		return;
	}
	SharedWorld &s = shared();
	const Run base = play(s, true, false);
	const Run one = play(s, true, true);
	const Run two = play(s, true, true);
	CHECK(one.hashes == two.hashes);
	CHECK(one.lines == two.lines);
	CHECK(one.money == base.money + 2000u); // the same game without the commands: the economy paid both the same
	REQUIRE(one.lines.size() == 4);
	CHECK(one.lines[0] == "Cheat Mode: Player_1 gave 2000 resources to Player_1");
	CHECK(one.lines[1] == "Cheat Mode: refused: Player_2 is not the host");
	CHECK(one.lines[2] == "Cheat Mode: Player_1 gave 50 command points to Player_1");
	CHECK(one.lines[3] == "Cheat Mode: Player_1 gave 7 power points to Player_1");
	CHECK(one.hashes.front() != base.hashes.front()); // a Cheat Mode game hashes its counts
}

TEST_CASE("cheat1 mode: every command's effect and limits in a Cheat Mode game")
{
	if (!haveWorld("cheat1 mode"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	GameLogic &logic = r.logic();
	Player *host = r.local;
	Player *other = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(other != nullptr);
	logic.cheatMode().configure(0, host->getPlayerIndex());
	const int h = host->getPlayerIndex(), o = other->getPlayerIndex();
	auto send = [&](const GameMessage &m) {
		r.game->commands().append(m);
		r.frame(1);
		return lastEntry(logic);
	};
	SUBCASE("limits and malformed commands: refused, logged, never the dispatcher's unhandled count")
	{
		CHECK(!send(CheatMode::makeMessage(h, Command::AddResources, o, 0)).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: Player_1 (not done): resources must be 1 .. 100000");
		CHECK(!send(CheatMode::makeMessage(h, Command::AddResources, o, 100001)).executed);
		CHECK(!send(CheatMode::makeMessage(h, Command::AddCommandPoints, o, 1001)).executed);
		CHECK(!send(CheatMode::makeMessage(h, Command::AddPowerPoints, o, 101)).executed);
		CHECK(!send(CheatMode::makeMessage(h, Command::AddResources, 99, 10)).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: Player_1 (not done): there is no player 99 in the game");
		GameMessage shortOne(MSG_OPENBFME_CHEAT, h);
		shortOne.appendIntegerArgument((int)Command::AddResources);
		CHECK(!send(shortOne).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: refused: a malformed command");
		GameMessage unknown(MSG_OPENBFME_CHEAT, h);
		for (int v : { 42, h, 1 })
		{
			unknown.appendIntegerArgument(v);
		}
		CHECK(!send(unknown).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: refused: unknown command 42");
		CHECK(r.game->dispatch().unhandled().count(MSG_OPENBFME_CHEAT) == 0);
		CHECK(logic.cheatMode().executedCount() == 0);
		CHECK(logic.cheatMode().refusedCount() == 7);
	}
	SUBCASE("command points and power points: exact")
	{
		const int bonus = other->commandPoints().getBonus();
		const int points = other->science().getSciencePurchasePoints();
		CHECK(send(CheatMode::makeMessage(h, Command::AddCommandPoints, o, 120)).executed);
		CHECK(send(CheatMode::makeMessage(h, Command::AddPowerPoints, o, 3)).executed);
		CHECK(other->commandPoints().getBonus() == bonus + 120);
		CHECK(other->science().getSciencePurchasePoints() == points + 3);
	}
	SUBCASE("spawn: units and heroes for a player on the map; structures, unknown ids and off-map points refused")
	{
		const Coord3D at{ 600.0f, 600.0f, 0.0f };
		const std::size_t before = objectsOf(logic, other, "GondorFighterHorde").size();
		CHECK(send(CheatMode::makeMessage(h, Command::Spawn, o, 3, templateId(logic, "GondorFighterHorde"), &at)).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: Player_1 spawned 3 GondorFighterHorde for Player_2");
		CHECK(objectsOf(logic, other, "GondorFighterHorde").size() == before + 3);
		CHECK(send(CheatMode::makeMessage(h, Command::Spawn, h, 1, templateId(logic, "GondorBoromir"), &at)).executed);
		CHECK(objectsOf(logic, host, "GondorBoromir").size() == 1);
		CHECK(!send(CheatMode::makeMessage(h, Command::Spawn, h, 1, templateId(logic, "GondorBarracks"), &at)).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: Player_1 (not done): GondorBarracks is a structure or a projectile, not a unit");
		CHECK(!send(CheatMode::makeMessage(h, Command::Spawn, h, 1, templateId(logic, "CreateAHero"), &at)).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: Player_1 (not done): CreateAHero is a Create-a-Hero: it is made from its player's own record, never on its own");
		CHECK(!send(CheatMode::makeMessage(h, Command::Spawn, h, 11, templateId(logic, "GondorBoromir"), &at)).executed);
		CHECK(!send(CheatMode::makeMessage(h, Command::Spawn, h, 1, 0xFFFF, &at)).executed);
		const Coord3D off{ -100000.0f, 600.0f, 0.0f };
		CHECK(!send(CheatMode::makeMessage(h, Command::Spawn, h, 1, templateId(logic, "GondorBoromir"), &off)).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: Player_1 (not done): a spawn point is off the map");
		CHECK(!send(CheatMode::makeMessage(h, Command::Spawn, h, 1, templateId(logic, "GondorBoromir"), nullptr)).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: Player_1 (not done): a spawn needs a location");
	}
	SUBCASE("level up: a hero of the target gains the levels; another player's object or a dead one is refused")
	{
		const Coord3D at{ 600.0f, 600.0f, 0.0f };
		REQUIRE(send(CheatMode::makeMessage(h, Command::Spawn, h, 1, templateId(logic, "GondorBoromir"), &at)).executed);
		const std::vector<Object *> heroes = objectsOf(logic, host, "GondorBoromir");
		REQUIRE(heroes.size() == 1);
		ExperienceTracker *xp = heroes[0]->getExperienceTracker();
		REQUIRE(xp != nullptr);
		const int rank = xp->getRank();
		CHECK(send(CheatMode::makeMessage(h, Command::LevelUp, h, 2, (int)heroes[0]->getID())).executed);
		CHECK(xp->getRank() == rank + 2);
		CHECK(lastEntry(logic).text == "Cheat Mode: Player_1 gave 2 levels to Player_1's GondorBoromir");
		CHECK(!send(CheatMode::makeMessage(h, Command::LevelUp, o, 1, (int)heroes[0]->getID())).executed);
		CHECK(!send(CheatMode::makeMessage(h, Command::LevelUp, h, 11, (int)heroes[0]->getID())).executed);
	}
	SUBCASE("reveal: once per player, undo once")
	{
		CHECK(send(CheatMode::makeMessage(h, Command::RevealMap, o, 1)).executed);
		CHECK(logic.cheatMode().revealed(o));
		CHECK(!send(CheatMode::makeMessage(h, Command::RevealMap, o, 1)).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: Player_1 (not done): the map is revealed for Player_2 already");
		CHECK(send(CheatMode::makeMessage(h, Command::RevealMap, o, 0)).executed);
		CHECK(!logic.cheatMode().revealed(o));
		CHECK(!send(CheatMode::makeMessage(h, Command::RevealMap, o, 0)).executed);
	}
	SUBCASE("end game: every player outside the target's alliance is killed")
	{
		CHECK(send(CheatMode::makeMessage(h, Command::EndGame, h, 0)).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: Player_1 ended the game: Player_1's alliance wins");
		CHECK(other->isDefeated());
		CHECK(!host->isDefeated());
		CHECK(!send(CheatMode::makeMessage(h, Command::EndGame, h, 0)).executed);
	}
	SUBCASE("void game: every player is killed, no winner, once")
	{
		CHECK(send(CheatMode::makeMessage(h, Command::VoidGame, -1, 0)).executed);
		CHECK(logic.cheatMode().voided());
		CHECK(other->isDefeated());
		CHECK(host->isDefeated());
		CHECK(!send(CheatMode::makeMessage(h, Command::VoidGame, -1, 0)).executed);
		CHECK(lastEntry(logic).text == "Cheat Mode: Player_1 (not done): the game is void already");
	}
}

TEST_CASE("cheat1 mode: the command names and the message")
{
	for (int c = CheatMode::kFirstCommand; c <= CheatMode::kLastCommand; ++c)
	{
		CHECK(CheatMode::commandFromName(CheatMode::commandName(c)) == c);
	}
	CHECK(CheatMode::commandFromName("give_everything") == 0);
	CHECK(std::string(GameMessageTypeName(MSG_OPENBFME_CHEAT)) == "MSG_OPENBFME_CHEAT");
	CHECK(IsNetworkLogicMessageType(MSG_OPENBFME_CHEAT));
	CHECK(!IsNetworkLogicMessageType(32000));
	// through the network's encoding and back
	const Coord3D at{ 100.5f, 200.25f, 0.0f };
	const GameMessage m = CheatMode::makeMessage(3, Command::Spawn, 2, 4, 1234, &at);
	NetByteWriter w;
	std::string err;
	REQUIRE(NetPacket::writeGameMessage(w, m, &err));
	NetByteReader rd(w.data());
	std::unique_ptr<GameMessage> back = NetPacket::readGameMessage(rd, &err);
	REQUIRE_MESSAGE(back != nullptr, err);
	CHECK(back->getType() == MSG_OPENBFME_CHEAT);
	REQUIRE(back->getArgumentCount() == 5);
	CHECK(back->getArgument(3)->integer == 1234);
	CHECK(back->getArgument(4)->location.x == 100.5f);
	CHECK(back->getArgument(4)->location.y == 200.25f);
}

TEST_CASE("cheat1 mode: the setup's Cheat Mode on the wire; an older format reads as off; a seat that is not a human slot is refused")
{
	SkirmishGameInfo g;
	g.slots[0].state = SLOT_PLAYER;
	g.slots[1].state = SLOT_HARD_AI;
	g.mapName = "maps/map mp fall back 4p";
	g.cheatMode = 0;
	NetByteWriter w;
	NetPacket::writeGameInfo(w, g);
	SkirmishGameInfo back;
	std::string err;
	{
		NetByteReader r(w.data());
		REQUIRE_MESSAGE(NetPacket::readGameInfo(r, back, &err), err);
		CHECK(r.atEnd());
		CHECK(back == g);
		CHECK(back.cheatModeOn());
	}
	{
		// the replay format 7 / LAN protocol 3 shape: no Cheat Mode field (the reader is told so): off, and the 4 bytes are left over
		NetByteReader r(w.data());
		REQUIRE_MESSAGE(NetPacket::readGameInfo(r, back, &err, false, true, false), err);
		CHECK(back.cheatMode == -1);
		CHECK(r.remaining() == 4);
	}
	for (int bad : { 1, 8, -2 })
	{
		g.cheatMode = bad; // slot 1 is a computer: never a cheat seat
		NetByteWriter w2;
		NetPacket::writeGameInfo(w2, g);
		NetByteReader r(w2.data());
		CHECK(!NetPacket::readGameInfo(r, back, &err));
		CHECK(err == "GameInfo Cheat Mode seat " + std::to_string(bad) + " is not a human slot");
	}
}
