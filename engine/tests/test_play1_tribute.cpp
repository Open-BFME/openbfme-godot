// OpenBFME unit tests. GPL-3.0.
// Lane PLAY-1: the tribute, MSG_GIVE_MONEY (GameLogic/TributeCommands.h, RW 0x6264E1): refused before NumMinutesBeforePlayersCanTransferMoney, then the
// giver's cash (at most what it has) moves to the receiver with both ScoreKeepers counting it, through the command list, deterministically and hashed. The
// binary facts the port reads SKIP without RW_GAME_DAT; the games SKIP without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"
#include "HudTestUtil.h"
#include "PeImage.h"

#include "Common/Player.h"
#include "Common/ScoreKeeper.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/TributeCommands.h"

#include <cstdint>
#include <vector>

using namespace hudtest;
using retailtest::PeImage;

namespace
{
GameMessage giveMsg(int sender, int from, int to, int amount)
{
	GameMessage m(MSG_GIVE_MONEY, sender);
	m.appendIntegerArgument(from);
	m.appendIntegerArgument(to);
	m.appendIntegerArgument(amount);
	return m;
}

struct Run
{
	std::vector<std::uint32_t> hashes;
	std::uint32_t giverCash = 0, receiverCash = 0, given = 0, received = 0;
};

// one game: the minutes are set to 1 (300 logic frames) so the test is short; a tribute before them is refused, one after them moves the money
Run play(SharedWorld &s, bool record)
{
	Run out;
	Rig r(s);
	Player *a = r.local;
	Player *b = r.game->players().findPlayerWithName("Player_2");
	REQUIRE(b != nullptr);
	CHECK(r.logic().economy().settings().numMinutesBeforePlayersCanTransferMoney == 5); // retail GameData (and the constructor's value)
	r.logic().economy().settings().numMinutesBeforePlayersCanTransferMoney = 1;
	const std::uint32_t a0 = a->getMoney()->countMoney(), b0 = b->getMoney()->countMoney();
	REQUIRE(a0 >= 300);
	// too early: refused (RW 0x626087)
	r.game->commands().append(giveMsg(a->getPlayerIndex(), a->getPlayerIndex(), b->getPlayerIndex(), 200));
	r.frame(2);
	CHECK(a->getMoney()->countMoney() == a0);
	CHECK(b->getMoney()->countMoney() == b0);
	while (r.logic().getFrame() < 300)
	{
		r.game->advance(0.2);
		if (record)
		{
			out.hashes.push_back(r.logic().computeStateHash());
		}
	}
	const std::uint32_t a1 = a->getMoney()->countMoney(), b1 = b->getMoney()->countMoney();
	r.game->commands().append(giveMsg(a->getPlayerIndex(), a->getPlayerIndex(), b->getPlayerIndex(), 200));
	r.frame(1);
	const std::int64_t da = (std::int64_t)a->getMoney()->countMoney() - (std::int64_t)a1, db = (std::int64_t)b->getMoney()->countMoney() - (std::int64_t)b1;
	CHECK(db - da >= 400 - 20); // 200 left a and reached b (the economy may pay both a few coins in the same frame: the deltas, not the totals)
	CHECK(a->getScoreKeeper().moneyGivenToAllies() == 200u);
	CHECK(b->getScoreKeeper().moneyReceivedFromAllies() == 200u);
	// more than the giver has: capped at its cash (RW 0x6264E1: min(cash, request))
	const std::uint32_t all = a->getMoney()->countMoney();
	r.game->commands().append(giveMsg(a->getPlayerIndex(), a->getPlayerIndex(), b->getPlayerIndex(), 1000000));
	r.frame(1);
	CHECK(a->getScoreKeeper().moneyGivenToAllies() >= 200u + all);
	CHECK(a->getMoney()->countMoney() < 50u);
	// malformed (two arguments): the dispatcher counts it, nothing moves
	GameMessage bad(MSG_GIVE_MONEY, a->getPlayerIndex());
	bad.appendIntegerArgument(a->getPlayerIndex());
	bad.appendIntegerArgument(b->getPlayerIndex());
	r.game->commands().append(bad);
	r.frame(1);
	CHECK(r.game->dispatch().unhandled().count(MSG_GIVE_MONEY) == 1);
	// a defeated receiver: refused (RW 0x6AAC4B)
	b->setDefeated(true);
	const std::uint32_t given = a->getScoreKeeper().moneyGivenToAllies();
	r.game->commands().append(giveMsg(a->getPlayerIndex(), b->getPlayerIndex(), a->getPlayerIndex(), 100));
	r.frame(1);
	CHECK(a->getScoreKeeper().moneyReceivedFromAllies() == 0u);
	CHECK(a->getScoreKeeper().moneyGivenToAllies() == given);
	b->setDefeated(false);
	if (record)
	{
		out.hashes.push_back(r.logic().computeStateHash());
	}
	out.giverCash = a->getMoney()->countMoney();
	out.receiverCash = b->getMoney()->countMoney();
	out.given = a->getScoreKeeper().moneyGivenToAllies();
	out.received = b->getScoreKeeper().moneyReceivedFromAllies();
	return out;
}
} // namespace

TEST_CASE("play1 tribute: MSG_GIVE_MONEY moves the giver's cash to the receiver after NumMinutesBeforePlayersCanTransferMoney, counted by both ScoreKeepers, deterministic")
{
	if (!haveWorld("play1 tribute"))
	{
		return;
	}
	SharedWorld &s = shared();
	const Run one = play(s, true);
	const Run two = play(s, true);
	CHECK(one.hashes == two.hashes);
	CHECK(one.giverCash == two.giverCash);
	CHECK(one.receiverCash == two.receiverCash);
	CHECK(one.given == two.given);
	CHECK(one.received == one.given);
}

TEST_CASE("play1 tribute: the GameData field and the binary facts the port reads")
{
	EconomySettings e;
	CHECK(e.numMinutesBeforePlayersCanTransferMoney == 5); // GlobalData's constructor (RW 0x6439B7)
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("play1 tribute binary facts (RW_GAME_DAT unset)");
		return;
	}
	auto bytes = [&](std::uint32_t va, size_t n) {
		std::vector<std::uint8_t> out;
		REQUIRE(pe->read(va, n, &out));
		return out;
	};
	// the GameData row { name, parseInt RW 0x42EC5E, 0, + 0x122C }
	CHECK(pe->cstring(pe->u32At(0xC011E0)) == "NumMinutesBeforePlayersCanTransferMoney");
	CHECK(pe->u32At(0xC011E4) == 0x42EC5Eu);
	CHECK(pe->u32At(0xC011EC) == 0x122Cu);
	// the constructor: push 5; pop ecx ... mov [esi + 0x122C], ecx
	CHECK(bytes(0x6439B7, 3) == std::vector<std::uint8_t>{ 0x6A, 0x05, 0x59 });
	CHECK(bytes(0x643A57, 6) == std::vector<std::uint8_t>{ 0x89, 0x8E, 0x2C, 0x12, 0x00, 0x00 });
	// RW 0x626087: GlobalData + 0x122C * [LOGICFRAMES_PER_SECOND] * 60 against the frame
	CHECK(bytes(0x62608C, 6) == std::vector<std::uint8_t>{ 0x8B, 0x80, 0x2C, 0x12, 0x00, 0x00 });
	CHECK(bytes(0x62609F, 3) == std::vector<std::uint8_t>{ 0x6B, 0xC0, 0x3C });
	// the screen's Send appends message 0x466 (RW 0x914E3E: push 0x466)
	CHECK(bytes(0x914E3E, 5) == std::vector<std::uint8_t>{ 0x68, 0x66, 0x04, 0x00, 0x00 });
	// CallPlayerGiveMoney's tail: withdraw (RW 0x7B17EF) and deposit (RW 0x7B18B8), the keepers' adders RW 0x79DD01 / 0x79DCE9
	CHECK(bytes(0x79DD13, 3) == std::vector<std::uint8_t>{ 0x01, 0x41, 0x10 });
	CHECK(bytes(0x79DCFB, 3) == std::vector<std::uint8_t>{ 0x01, 0x41, 0x0C });
}

TEST_CASE("play1 tribute: NumMinutesBeforePlayersCanTransferMoney is in the settings hash (a peer with another gate does not hash equal)")
{
	EconomySettings a, b;
	CHECK(a.numMinutesBeforePlayersCanTransferMoney == 5); // the constructor's value (RW 0x6439B7)
	b.numMinutesBeforePlayersCanTransferMoney = 0;
	StateHasher ha, hb;
	a.crc(ha);
	b.crc(hb);
	CHECK(ha.value() != hb.value());
	b.numMinutesBeforePlayersCanTransferMoney = 5;
	StateHasher hc;
	b.crc(hc);
	CHECK(ha.value() == hc.value());
}
