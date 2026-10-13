// OpenBFME unit tests. GPL-3.0.
// Lane ONLINE-3, spectators (docs/ONLINE.md "Spectators"): the spectator ticket and START, the relay's spectator binding (its DATA refused, never logged
// or forwarded; its log held back by the delay; a finished game readable for its delay; an idle one dropped; never in STATUS), a real Transport as a
// spectator, and on the retail game (SKIPs loudly without ROTWK_INSTALL / BFME2_INSTALL): a game loaded with no local slot has the observer's
// ReplayObserver side as its local player with the whole map revealed, runs the same frames to the same state hashes as a player's game, and retail's
// observe-next-player (RW 0x6A8D2B) steps the view through the human players and back to "all".

#include "doctest.h"

#include "StartTestUtil.h"

#include "Common/Crc32.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LogicSnapshot.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameNetwork/LockstepDriver.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/OnlineStart.h"
#include "GameNetwork/RelayLog.h"
#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/Rejoin.h"
#include "GameNetwork/Transport.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "RelayCore.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace
{
std::vector<std::uint8_t> key32()
{
	std::vector<std::uint8_t> k;
	for (int i = 0; i < 32; ++i)
	{
		k.push_back((std::uint8_t)i);
	}
	return k;
}
RelayProtocol::KeyRing ring()
{
	return { { 1, key32() } };
}
constexpr std::uint64_t kGame = 0x00aa00bb00cc00ddull;
RelayProtocol::Ticket ticket(std::uint8_t slot, std::uint8_t mask = 0x03, bool spectator = false, std::uint16_t delay = 0)
{
	RelayProtocol::Ticket t;
	t.keyId = 1;
	t.gameId = kGame;
	t.slot = slot;
	t.slotMask = mask;
	t.expires = 2000000000u;
	t.spectator = spectator;
	t.delaySeconds = delay;
	return t;
}
std::vector<std::uint8_t> bytesOf(const RelayProtocol::Ticket &t)
{
	if (t.spectator)
	{
		return RelayProtocol::signSpectatorTicket(t, key32());
	}
	const auto b = RelayProtocol::signTicket(t, key32());
	return std::vector<std::uint8_t>(b.begin(), b.end());
}
RelayAddress at(std::uint16_t port)
{
	return RelayAddress{ 0x0A000002u, port };
}
std::vector<std::uint8_t> packetOf(std::uint8_t sender, std::uint32_t seq, const std::vector<std::uint8_t> &record)
{
	std::vector<std::uint8_t> d(4, 0);
	auto put16 = [&](std::uint16_t v) {
		d.push_back((std::uint8_t)v);
		d.push_back((std::uint8_t)(v >> 8));
	};
	auto put32 = [&](std::uint32_t v) {
		put16((std::uint16_t)v);
		put16((std::uint16_t)(v >> 16));
	};
	put16(0x4642);
	d.push_back(2);
	d.push_back(sender);
	put32(0);
	put16(1);
	put32(seq);
	put16(0);
	put16(1);
	put32((std::uint32_t)record.size());
	d.insert(d.end(), record.begin(), record.end());
	const std::uint32_t crc = crc32Bytes(d.data() + 4, d.size() - 4);
	for (int i = 0; i < 4; ++i)
	{
		d[(size_t)i] = (std::uint8_t)(crc >> (8 * i));
	}
	return d;
}
std::vector<std::uint8_t> frameInfo(std::uint8_t slot, std::uint32_t frame)
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_FRAMEINFO;
	c.slot = slot;
	c.executionFrame = frame;
	return NetPacket::encodeCommand(c, nullptr);
}
std::filesystem::path tempDir(const std::string &name)
{
	const std::filesystem::path p = std::filesystem::temp_directory_path() / ("openbfme_spectate_" + name + "_" + std::to_string(NetMilliseconds()));
	std::filesystem::create_directories(p);
	return p;
}
// everything a client at `from` may read of the log now (LOG_REQUEST / LOG_CHUNK until an empty chunk)
std::vector<std::uint8_t> readLog(RelayCore &relay, const RelayAddress &from, std::uint64_t now)
{
	std::vector<std::uint8_t> out;
	for (int guard = 0; guard < 10000; ++guard)
	{
		std::vector<std::uint8_t> req = RelayProtocol::encodeLogRequest((std::uint32_t)out.size());
		RelayCore::Action a = relay.handle(from, req.data(), req.size(), now, 1700000000u);
		REQUIRE(a.kind == RelayCore::Action::REPLY);
		REQUIRE(a.reply[3] == RelayProtocol::LOG_CHUNK);
		const size_t n = a.reply.size() - RelayProtocol::kHeaderBytes - 8;
		if (n == 0)
		{
			break;
		}
		out.insert(out.end(), a.reply.begin() + (long)(RelayProtocol::kHeaderBytes + 8), a.reply.end());
	}
	return out;
}
std::uint32_t maxFrameIn(const std::vector<std::uint8_t> &log)
{
	RelayLogFile f;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(log, f, &error, true), error);
	std::uint32_t m = 0;
	for (const auto &c : f.commands)
	{
		m = std::max(m, c.msg.executionFrame);
	}
	return m;
}
} // namespace

TEST_CASE("online3 spectators: the spectator ticket (version 3, pinned with the lobby) and the spectator START; a player's START never takes a spectator "
		  "ticket and the reverse")
{
	std::string error;
	RelayProtocol::Ticket p;
	p.keyId = 1;
	p.gameId = 0x0123456789abcdefull;
	p.slot = 2;
	p.slotMask = 0x07;
	p.expires = 2000000000u;
	p.spectator = true;
	p.delaySeconds = 60;
	const std::vector<std::uint8_t> pinned = RelayProtocol::signSpectatorTicket(p, key32());
	CHECK(RelayProtocol::base64UrlEncode(pinned.data(), pinned.size()) == "AwHvzauJZ0UjAQIHAJQ1dzwADqM2vSmJqqkoEsrChDi_PA");
	RelayProtocol::Ticket out;
	REQUIRE(RelayProtocol::verifyTicket(pinned.data(), pinned.size(), ring(), 1700000000u, out, &error));
	CHECK(out.spectator);
	CHECK(out.slot == 2);
	CHECK(out.delaySeconds == 60);
	CHECK(out.generation == 0);
	std::vector<std::uint8_t> bad = pinned;
	bad[16] = 61; // another delay under the same MAC
	CHECK_FALSE(RelayProtocol::verifyTicket(bad.data(), bad.size(), ring(), 1700000000u, out, &error));
	p.slot = 16;
	const std::vector<std::uint8_t> tooHigh = RelayProtocol::signSpectatorTicket(p, key32());
	CHECK_FALSE(RelayProtocol::verifyTicket(tooHigh.data(), tooHigh.size(), ring(), 1700000000u, out, &error));
	// the START
	const ProfileIdentity profile = ProfileIdentity::fromText("openbfme-profile 1\ntest\n");
	OnlineStart::Limits limits;
	limits.playerTemplateCount = 20;
	limits.colorCount = 10;
	auto json = [&](const std::string &kind, const std::vector<std::uint8_t> &t, const std::string &extra) {
		return std::string("{\"v\":1,\"kind\":\"") + kind + "\",\"game\":\"00aa00bb00cc00dd\"" + extra + ",\"relay\":\"10.0.0.1:7350\",\"ticket\":\""
			+ RelayProtocol::base64UrlEncode(t.data(), t.size()) + "\",\"runAhead\":2,\"crcInterval\":100,\"profile\":\"" + profile.digest
			+ "\",\"setup\":{\"map\":\"maps/x/x.map\",\"seed\":5,\"startingCash\":1000,\"superweaponRestriction\":0,\"rules\":null,\"legendaryHeroes\":1,"
			  "\"slots\":[{\"state\":\"player\",\"name\":\"A\",\"faction\":1,\"color\":0,\"startPos\":0,\"team\":-1},"
			  "{\"state\":\"player\",\"name\":\"B\",\"faction\":2,\"color\":1,\"startPos\":1,\"team\":-1}]}}";
	};
	const std::vector<std::uint8_t> spect = bytesOf(ticket(3, 0x03, true, 60));
	LobbyStart ls;
	REQUIRE_MESSAGE(OnlineStart::parse(json("openbfme-spectate", spect, ",\"spectator\":3,\"delay\":60"), profile, limits, ls, &error, OnlineStart::Kind::Spectate),
		error);
	CHECK(ls.localSlot == -1);
	CHECK(ls.spectatorIndex == 3);
	CHECK(ls.spectatorDelay == 60);
	CHECK(ls.online);
	CHECK(ls.networkSlot[0]);
	CHECK(ls.networkSlot[1]);
	CHECK_FALSE(OnlineStart::parse(json("openbfme-spectate", spect, ",\"spectator\":3,\"delay\":0"), profile, limits, ls, &error, OnlineStart::Kind::Spectate));
	CHECK_FALSE(OnlineStart::parse(json("openbfme-spectate", spect, ",\"spectator\":2,\"delay\":60"), profile, limits, ls, &error, OnlineStart::Kind::Spectate));
	CHECK_FALSE(OnlineStart::parse(json("openbfme-spectate", spect, ",\"slot\":0,\"spectator\":3,\"delay\":60"), profile, limits, ls, &error,
		OnlineStart::Kind::Spectate));
	// a player's ticket in a spectator START, a spectator ticket in a player's START
	CHECK_FALSE(OnlineStart::parse(json("openbfme-spectate", bytesOf(ticket(0)), ",\"spectator\":0,\"delay\":0"), profile, limits, ls, &error,
		OnlineStart::Kind::Spectate));
	CHECK_FALSE(OnlineStart::parse(json("openbfme-start", spect, ",\"slot\":1"), profile, limits, ls, &error));
	CHECK_FALSE(OnlineStart::parse(json("openbfme-start", spect, ",\"slot\":1"), profile, limits, ls, &error, OnlineStart::Kind::Spectate));
}

TEST_CASE("online3 spectators: the relay binds a spectator, refuses its commands (never logged or forwarded), keeps it out of STATUS, serves its log held "
		  "back by its delay, keeps a finished game readable for it, and drops it when idle")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("relay");
	RelayCore::Options o;
	o.keys = ring();
	o.logDir = dir.string();
	o.spectatorIdleMs = 30000;
	o.spectatorGraceMs = 10000;
	RelayCore relay(o);
	std::uint64_t now = 1000;
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), now, 1700000000u); };
	REQUIRE(send(at(10), encodeHello(bytesOf(ticket(0)))).kind == RelayCore::Action::REPLY);
	REQUIRE(send(at(11), encodeHello(bytesOf(ticket(1)))).kind == RelayCore::Action::REPLY);
	// a spectator without delay (index 0) and one 2 s behind (index 1: 10 frames)
	RelayCore::Action w0 = send(at(20), encodeHello(bytesOf(ticket(0, 0x03, true, 0))));
	REQUIRE(w0.reply.size() == kHeaderBytes + 9 + kSessionTokenBytes);
	CHECK(w0.reply[3] == WELCOME);
	CHECK(w0.reply[kHeaderBytes + 8] == kSpectatorWelcome + 0);
	RelayCore::Action w1 = send(at(21), encodeHello(bytesOf(ticket(1, 0x03, true, 2))));
	CHECK(w1.reply[kHeaderBytes + 8] == kSpectatorWelcome + 1);
	CHECK(relay.stats().spectatorsBound == 2);
	// its ticket from another address without the session token takes nothing; a spectator ticket never moves a player's address
	CHECK(send(at(30), encodeHello(bytesOf(ticket(0, 0x03, true, 0)))).reply == encodeRefused(REFUSE_SLOT_TAKEN));
	CHECK(send(at(10), encodeHello(bytesOf(ticket(2, 0x03, true, 0)))).reply == encodeRefused(REFUSE_SLOT_TAKEN));
	// the players play frames 0 .. 40
	std::uint32_t seq0 = 0, seq1 = 0;
	for (std::uint32_t f = 0; f <= 40; ++f)
	{
		send(at(10), encodeData(1, packetOf(0, seq0++, frameInfo(0, f))));
		send(at(11), encodeData(0, packetOf(1, seq1++, frameInfo(1, f))));
	}
	const unsigned long long logged = relay.stats().commandsLogged;
	const unsigned long long forwarded = relay.stats().forwarded;
	// a forged command from the spectator: REFUSED, neither logged nor forwarded (to any target, its own index included)
	for (std::uint8_t target : { (std::uint8_t)0, (std::uint8_t)1 })
	{
		CHECK(send(at(20), encodeData(target, packetOf(0, 0, frameInfo(0, 41)))).reply == encodeRefused(REFUSE_SPECTATOR));
	}
	CHECK(relay.stats().spectatorDataRefused == 2);
	CHECK(relay.stats().commandsLogged == logged);
	CHECK(relay.stats().forwarded == forwarded);
	// no STATUS for spectators: player 1 goes silent, the STATUS goes to player 0 only, naming the two player slots
	now += 4000;
	send(at(10), encodeData(1, packetOf(0, seq0++, frameInfo(0, 41))));
	std::vector<RelayCore::Action> statuses;
	relay.tick(now, &statuses);
	REQUIRE(statuses.size() == 2); // slot 0 and slot 1 (bound, though silent)
	for (const RelayCore::Action &a : statuses)
	{
		CHECK((a.to == at(10) || a.to == at(11)));
		std::vector<SlotStatus> st;
		REQUIRE(decodeStatus(a.reply.data(), a.reply.size(), st));
		CHECK(st.size() == 2);
	}
	// the log: all of it for the spectator without delay; for the delayed one, only records of frames at least 10 behind the latest (41)
	const std::vector<std::uint8_t> full = readLog(relay, at(20), now);
	CHECK(maxFrameIn(full) == 41);
	const std::vector<std::uint8_t> late = readLog(relay, at(21), now);
	CHECK(maxFrameIn(late) == 31);
	CHECK(std::equal(late.begin(), late.end(), full.begin())); // a prefix of the same log
	CHECK(relay.stats().spectatorLogBytes == full.size() + late.size());
	// the players leave: the game closes (spectators keep no game open), the delayed spectator still reads it, its frontier moving on at 5 frames a
	// second, until it has all of it 2 s later
	send(at(10), encodeEmpty(BYE));
	send(at(11), encodeEmpty(BYE));
	REQUIRE(relay.closedLogs().size() == 1);
	CHECK(send(at(12), encodeHello(bytesOf(ticket(0)))).reply == encodeRefused(REFUSE_GAME_CLOSED));
	CHECK(maxFrameIn(readLog(relay, at(21), now)) == 31); // closed at `now`: its frontier has not moved on yet
	now += 2500;
	const std::vector<std::uint8_t> end = readLog(relay, at(21), now);
	RelayLogFile f;
	std::string error;
	REQUIRE_MESSAGE(RelayLogFile::parse(end, f, &error), error);
	CHECK(f.complete); // the END record too
	// the finished game goes once its spectators had their delay and the grace; an idle spectator is dropped
	relay.tick(now + 5000, nullptr);
	CHECK(relay.openGames() == 1);
	relay.tick(now + 40000, nullptr);
	CHECK(relay.stats().spectatorsDropped == 2);
	CHECK(relay.openGames() == 0);
	std::filesystem::remove_all(dir);
}

TEST_CASE("online3 spectators: a real Transport takes the spectator WELCOME (0x80 + its index) and keeps its binding when the relay refuses its data")
{
	using namespace RelayProtocol;
	UDP relaySocket, client;
	std::string error;
	REQUIRE(relaySocket.bind(0x7F000001u, 0, &error));
	REQUIRE(client.bind(0x7F000001u, 0, &error));
	Transport::Options o;
	o.relay.enabled = true;
	o.relay.address = relaySocket.localAddress();
	o.relay.ticket = bytesOf(ticket(5, 0x03, true, 0));
	o.relay.spectatorIndex = 5;
	o.relay.helloMs = 10;
	Transport t(client, 0, o);
	CHECK(t.errors().empty());
	auto deliver = [&](const std::vector<std::uint8_t> &d) {
		relaySocket.sendTo(client.localAddress(), d.data(), d.size());
		for (int i = 0; i < 50; ++i)
		{
			t.service();
			NetSleepMilliseconds(1);
		}
	};
	std::array<std::uint8_t, kSessionTokenBytes> token{};
	deliver(encodeWelcome(kGame, 0, token)); // slot 0: a player's WELCOME, not this spectator's
	CHECK_FALSE(t.relayBound());
	deliver(encodeWelcome(kGame, kSpectatorWelcome + 5, token));
	CHECK(t.relayBound());
	deliver(encodeRefused(REFUSE_SPECTATOR));
	CHECK(t.spectatorRefusals() == 1);
	CHECK(t.relayBound());
	CHECK(t.relayRefusal() == 0);
	CHECK(t.stats().commandsSent == 0);
}

// ---- on the retail game -----------------------------------------------------------------------------------------------------------------------
namespace
{
int factionOf(const PlayerTemplateStore &store, const std::string &name)
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
struct Loaded
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::unique_ptr<ArchiveW3DFileSource> source;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<LiveGame> game;
};
void loadAs(starttest::Shared &s, const NewGameMessage &m, int localSlot, Loaded &out)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, out.start, &error), error);
	out.start.localSlot = localSlot;
	out.start.observer = localSlot < 0;
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	REQUIRE_MESSAGE(out.game->load(o, &error), error);
}
// the same scripted input in both games: every 20 frames slot 0's first units move
void inputFor(LiveGame &game, int player, CommandList &out)
{
	const UnsignedInt f = game.frame();
	if (f % 20 != 0)
	{
		return;
	}
	GameLogic &logic = game.logic();
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	Coord3D where{ 0, 0, 0 };
	int n = 0;
	for (Object *o = logic.getFirstObject(); o && n < 6; o = o->getNextObject())
	{
		if (o->getControllingPlayer() && o->getControllingPlayer()->getPlayerIndex() == player && !o->isEffectivelyDead() && !o->isKindOfName("STRUCTURE"))
		{
			sel.appendObjectIDArgument(o->getID());
			where = *o->getPosition();
			++n;
		}
	}
	if (n == 0)
	{
		return;
	}
	out.append(sel);
	GameMessage mv(MSG_DO_MOVETO, player);
	where.x += (f / 20) % 2 ? 90.0f : -90.0f;
	where.z = logic.getGroundHeight(where.x, where.y);
	mv.appendLocationArgument(where);
	out.append(mv);
}
} // namespace

// ---- Sol r1 of ONLINE-3: the relay's bindings across games, its metadata and its log reads -------------------------------------------------------
namespace
{
RelayProtocol::Ticket ticketOf(std::uint64_t game, std::uint8_t slot, bool spectator = false, std::uint16_t delay = 0)
{
	RelayProtocol::Ticket t = ticket(slot, 0x03, spectator, delay);
	t.gameId = game;
	return t;
}
} // namespace

TEST_CASE("online3 r1 (Sol: cross-game bindings): an address that leaves a spectator place for a player slot of another game leaves only that place; "
		  "the old game's players stay bound and the place's timeout never touches the new binding (also for spectator indices 8 .. 15)")
{
	using namespace RelayProtocol;
	for (std::uint8_t index : { (std::uint8_t)0, (std::uint8_t)8, (std::uint8_t)15 })
	{
		INFO("spectator index " << (int)index);
		RelayCore::Options o;
		o.keys = ring();
		RelayCore relay(o);
		std::uint64_t now = 1000;
		auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), now, 1700000000u); };
		const std::uint64_t A = 0x0A, B = 0x0B;
		REQUIRE(send(at(11), encodeHello(bytesOf(ticketOf(A, 0)))).reply[3] == WELCOME);
		REQUIRE(send(at(12), encodeHello(bytesOf(ticketOf(A, 1)))).reply[3] == WELCOME);
		REQUIRE(send(at(10), encodeHello(bytesOf(ticketOf(A, index, true)))).reply[3] == WELCOME);
		// the same address becomes slot 0 of game B
		REQUIRE(send(at(10), encodeHello(bytesOf(ticketOf(B, 0)))).reply[3] == WELCOME);
		REQUIRE(send(at(20), encodeHello(bytesOf(ticketOf(B, 1)))).reply[3] == WELCOME);
		// game A's slot 0 is still bound: slot 1's data reaches it (round 1 unbound it: the spectator index was taken for a player slot)
		RelayCore::Action fwd = send(at(12), encodeData(0, packetOf(1, 0, frameInfo(1, 3))));
		CHECK(fwd.kind == RelayCore::Action::FORWARD);
		CHECK(fwd.to == at(11));
		// past the spectator idle time: nothing of A's spectator place is left to drop, and the address's binding in B stays
		now += o.spectatorIdleMs + 1000;
		send(at(11), encodeData(1, packetOf(0, 0, frameInfo(0, 3)))); // the games stay heard
		send(at(20), encodeData(0, packetOf(1, 0, frameInfo(1, 3))));
		relay.tick(now);
		CHECK(relay.stats().spectatorsDropped == 0);
		RelayCore::Action inB = send(at(10), encodeData(1, packetOf(0, 0, frameInfo(0, 3))));
		CHECK(inB.kind == RelayCore::Action::FORWARD);
		CHECK(inB.to == at(20));
		relay.shutdown(now);
	}
}

TEST_CASE("online3 r1 (Sol: relay metadata): the per-frame command counts and the delay frontier are held bytes under the caps; the frontier stays "
		  "bounded and a delayed spectator's cut-off after its thinning is never later than the exact one")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("r1meta");
	{
		// Sol's probe: 128-byte caps, 5,000 distinct commands of one frame: round 1 logged all 5,000 and held 10,000 count nodes with 0 bytes counted
		RelayCore::Options o;
		o.keys = ring();
		o.logDir = dir.string();
		o.maxStreamBuffer = o.maxSlotBuffer = o.maxGameBuffer = o.maxTotalBuffer = 128;
		RelayCore relay(o);
		auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
		send(at(1), encodeHello(bytesOf(ticket(0))));
		send(at(2), encodeHello(bytesOf(ticket(1))));
		for (std::uint32_t i = 0; i < 5000; ++i)
		{
			NetCommandMsg c;
			c.type = NETCOMMANDTYPE_FRAMEINFO;
			c.slot = 0;
			c.executionFrame = 10;
			c.commandCount = (std::uint16_t)i; // distinct bytes
			send(at(1), encodeData(1, packetOf(0, i, NetPacket::encodeCommand(c, nullptr))));
			CHECK(relay.retainedBytes() == relay.heldBytes());
		}
		// lane ONLINE-3 r2 (Sol r2): admitted before they are made, not even the first command's four count nodes fit 128 bytes: the stream stops at once
		// and nothing stays held (the frontier's first 16 entries did not fit either)
		CHECK(relay.stats().commandsLogged == 0);
		CHECK(relay.stats().logGaps == 1);
		CHECK(relay.heldBytes() == 0);
		relay.shutdown(2000);
	}
	{
		// 10,001 frames of one command each, default caps: one frontier entry per frame, thinned to 2,049 whenever it reaches 4,097 (pushes 4,097,
		// 6,145 and 8,193), then 1,808 more: 3,857 entries, every frame from 8,193 on still its own
		RelayCore::Options o;
		o.keys = ring();
		o.logDir = dir.string();
		RelayCore relay(o);
		std::uint64_t now = 1000;
		auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), now, 1700000000u); };
		send(at(1), encodeHello(bytesOf(ticket(0))));
		send(at(2), encodeHello(bytesOf(ticket(1))));
		REQUIRE(send(at(3), encodeHello(bytesOf(ticket(0, 0x03, true, 60)))).reply[3] == WELCOME); // 300 frames behind
		for (std::uint32_t f = 0; f <= 10000; ++f)
		{
			now += 200; // 5 frames a second (the slot's data budget)
			send(at(1), encodeData(1, packetOf(0, f, frameInfo(0, f))));
			if (f % 1000 == 0)
			{
				send(at(3), encodeLogRequest(0)); // the spectator stays bound
			}
		}
		REQUIRE(relay.stats().commandsLogged == 10001);
		CHECK(relay.frontierEntries(kGame) == 3857);
		CHECK(relay.retainedBytes() == relay.heldBytes());
		CHECK(maxFrameIn(readLog(relay, at(3), now)) == 10000 - 300);
		relay.shutdown(2000);
	}
	std::filesystem::remove_all(dir);
}

TEST_CASE("online3 r2 (Sol r2: caps before growth): every cap holds before the relay accepts work and after a stream stops, across several games; a "
		  "stopped stream's sender releases its counts")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("r2caps");
	const size_t frontier = 16 * sizeof(std::pair<std::uint64_t, std::uint32_t>); // a game's first frontier allocation
	const size_t perCommand = 4 * RelayCore::kMetaNodeBytes;                       // a new frame's counts: the stream's and the sender's, 2 nodes each
	auto play = [&](RelayCore &relay, std::uint64_t game, std::uint16_t portBase, size_t cap) {
		auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
		send(at(portBase), encodeHello(bytesOf(ticketOf(game, 0))));
		send(at((std::uint16_t)(portBase + 1)), encodeHello(bytesOf(ticketOf(game, 1))));
		for (std::uint32_t f = 0; f < 40; ++f)
		{
			send(at(portBase), encodeData(1, packetOf(0, f, frameInfo(0, f))));
			// tolerance: the claim is a cap
			CHECK(relay.heldBytes() <= cap); // never over the cap, not even for a moment of one datagram
			CHECK(relay.retainedBytes() == relay.heldBytes());
		}
	};
	{
		// Sol's probe: the game and the relay capped at 128 bytes, two games: nothing fits (a frontier's 256, a command's 256), every stream stops at its
		// first command and nothing is held (round 1 held 160 after the first game, 320 after the second)
		RelayCore::Options o;
		o.keys = ring();
		o.logDir = dir.string();
		o.maxGameBuffer = o.maxTotalBuffer = 128;
		RelayCore relay(o);
		play(relay, 0xA1, 10, 128);
		CHECK(relay.heldBytes() == 0);
		play(relay, 0xA2, 20, 128);
		CHECK(relay.heldBytes() == 0);
		CHECK(relay.stats().commandsLogged == 0);
		CHECK(relay.stats().logGaps == 2);
		relay.shutdown(2000);
	}
	{
		// 1,024 bytes for everything: game A's frontier (256) and three commands (3 x 256) fit, the fourth does not: the stream stops and its sender's
		// counts go (the frontier stays: 256); game B then has 768: its frontier and two commands, then it stops the same way
		RelayCore::Options o;
		o.keys = ring();
		o.logDir = dir.string();
		o.maxStreamBuffer = o.maxSlotBuffer = o.maxGameBuffer = o.maxTotalBuffer = 1024;
		RelayCore relay(o);
		play(relay, 0xB1, 30, 1024);
		CHECK(relay.stats().commandsLogged == (1024 - frontier) / perCommand);
		CHECK(relay.heldBytes() == frontier);
		play(relay, 0xB2, 40, 1024);
		CHECK(relay.stats().commandsLogged == (1024 - frontier) / perCommand + (1024 - 2 * frontier) / perCommand);
		CHECK(relay.heldBytes() == 2 * frontier);
		CHECK(relay.stats().logGaps == 2);
		relay.shutdown(2000);
		CHECK(relay.heldBytes() == 0); // the games' ends release the rest
		CHECK(relay.retainedBytes() == 0);
	}
	std::filesystem::remove_all(dir);
}

TEST_CASE("online3 r3 (Sol r3: held bytes): a frontier expansion stays under the caps at sizes around the real ones; a stopped log releases exactly "
		  "what it held (no counter wraps) and another game logs as before; one game's limits never touch another's")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("r3held");
	const size_t entry = sizeof(std::pair<std::uint64_t, std::uint32_t>);
	const size_t counts = 4 * RelayCore::kMetaNodeBytes; // a new frame's command counts: the stream's and the sender's, 2 nodes each
	const size_t frontier = 16 * entry;                    // a game's first frontier allocation; its first expansion (16 -> 32 entries) is as much again
	auto exact = [](RelayCore &relay) {
		CHECK(relay.retainedBytes() == relay.heldBytes());
		CHECK(relay.stats().accountingErrors == 0);
	};
	{
		// one stream's FRAMEINFOs, frame after frame: frames 0..15 hold the frontier and 16 frames of counts (4,352 bytes); frame 16 needs 256 more for
		// its counts and 256 for the frontier's expansion (4,864). Round 2 admitted the counts but charged half of them before the expansion's check:
		// 4,864 held under a 4,800 cap
		struct Case
		{
			size_t cap, logged, entries, heldAfter;
		};
		const Case cases[] = {
			{ frontier + 16 * counts - 1, 15, 15, frontier },              // frame 15's counts do not fit: 15 logged, the stream stops
			{ frontier + 16 * counts, 16, 16, frontier },                  // frames 0..15; frame 16's counts do not fit
			{ frontier + 17 * counts - 1, 16, 16, frontier },              // the same, one byte short
			{ frontier + 17 * counts, 17, 16, frontier },                  // frame 16's counts fit, the expansion does not: the last entry moves on
			{ 4800, 17, 16, frontier },                                    // Sol's cap
			{ 2 * frontier + 17 * counts - 1, 17, 16, frontier },          // one byte short of the expansion
			{ 2 * frontier + 17 * counts, 17, 17, 2 * frontier },          // the expansion fits too; frame 17's counts do not
		};
		for (const Case &c : cases)
		{
			INFO("cap " << c.cap);
			RelayCore::Options o;
			o.keys = ring();
			o.logDir = dir.string();
			o.maxGameBuffer = o.maxTotalBuffer = c.cap;
			RelayCore relay(o);
			auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
			send(at(1), encodeHello(bytesOf(ticket(0))));
			send(at(2), encodeHello(bytesOf(ticket(1))));
			for (std::uint32_t f = 0; f < 30; ++f)
			{
				send(at(1), encodeData(1, packetOf(0, f, frameInfo(0, f))));
				// tolerance: the claim is the cap: after every datagram, and (the peak) at every moment within one
				CHECK(relay.heldBytes() <= c.cap);
				// tolerance: the claim is the cap (see above)
				CHECK(relay.stats().peakBuffered <= c.cap);
				exact(relay);
			}
			CHECK(relay.stats().commandsLogged == c.logged);
			CHECK(relay.frontierEntries(kGame) == c.entries);
			CHECK(relay.stats().logGaps == 1);
			CHECK(relay.heldBytes() == c.heldAfter); // the stopped stream's and its sender's counts are released; the frontier stays
			relay.shutdown(2000);
			CHECK(relay.heldBytes() == 0);
			exact(relay);
		}
	}
	{
		// a log size limit stops game A's log after the commands that fit (the header and two EVENTs are 39 bytes, a FRAMEINFO record 18), at limits
		// around those sizes. Round 2 released the stream's counts twice there (heldBytes wrapped to 2^64 - 2688), and game B could log nothing
		for (std::uint64_t limit : { 56ull, 57ull, 399ull, 400ull, 417ull })
		{
			INFO("log limit " << limit);
			const std::uint64_t fit = (limit - 39) / 18;
			RelayCore::Options o;
			o.keys = ring();
			o.logDir = dir.string();
			o.maxLogBytes = limit;
			RelayCore relay(o);
			auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
			send(at(1), encodeHello(bytesOf(ticketOf(0xA, 0))));
			send(at(2), encodeHello(bytesOf(ticketOf(0xA, 1))));
			for (std::uint32_t f = 0; f < 40; ++f)
			{
				send(at(1), encodeData(1, packetOf(0, f, frameInfo(0, f))));
				exact(relay);
			}
			CHECK(relay.stats().commandsLogged == fit);
			CHECK(relay.stats().logErrors == 1);
			CHECK(relay.heldBytes() == 0); // the stopped log released everything it held: counts, frontier
			// game B, the same limit: its commands that fit are logged, as if game A had never been
			send(at(3), encodeHello(bytesOf(ticketOf(0xB, 0))));
			send(at(4), encodeHello(bytesOf(ticketOf(0xB, 1))));
			for (std::uint32_t f = 0; f < fit; ++f)
			{
				send(at(3), encodeData(1, packetOf(0, f, frameInfo(0, f))));
				exact(relay);
			}
			CHECK(relay.stats().commandsLogged == 2 * fit);
			CHECK(relay.stats().logErrors == 1);
			CHECK(relay.stats().logGaps == 0);
			relay.shutdown(2000);
			CHECK(relay.heldBytes() == 0);
			exact(relay);
		}
	}
	{
		// the game cap is per game: game A reaching its cap leaves game B its own, whole (the relay's total is far above both)
		for (size_t cap : { frontier + 3 * counts - 1, frontier + 3 * counts, (size_t)1024 + 1 })
		{
			INFO("game cap " << cap);
			RelayCore::Options o;
			o.keys = ring();
			o.logDir = dir.string();
			o.maxGameBuffer = cap;
			RelayCore relay(o);
			auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
			const std::uint64_t fit = (cap - frontier) / counts;
			std::uint16_t port = 10;
			for (std::uint64_t game : { 0xC1ull, 0xC2ull })
			{
				const RelayAddress p0 = at(port++), p1 = at(port++);
				send(p0, encodeHello(bytesOf(ticketOf(game, 0))));
				send(p1, encodeHello(bytesOf(ticketOf(game, 1))));
				for (std::uint32_t f = 0; f < 10; ++f)
				{
					send(p0, encodeData(1, packetOf(0, f, frameInfo(0, f))));
					exact(relay);
				}
			}
			CHECK(relay.stats().commandsLogged == 2 * fit); // each game logged what its own cap holds
			CHECK(relay.stats().logGaps == 2);
			CHECK(relay.heldBytes() == 2 * frontier);
			relay.shutdown(2000);
			CHECK(relay.heldBytes() == 0);
			exact(relay);
		}
	}
	std::filesystem::remove_all(dir);
}

TEST_CASE("online3 r1 (Sol: log reads): a LOG_REQUEST over its budget is refused before the disk is read; the game's and the relay's read budgets bound "
		  "every client together; a game's log stops at its size cap")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("r1reads");
	{
		// the client's budget is below one chunk: refused at once. Round 1 opened the log first (here: removed from the disk, so a read fails loudly)
		RelayCore::Options o;
		o.keys = ring();
		o.logDir = dir.string();
		o.logBurstBytes = 100;
		o.logBytesPerSecond = 0;
		RelayCore relay(o);
		auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
		send(at(1), encodeHello(bytesOf(ticket(0))));
		send(at(2), encodeHello(bytesOf(ticket(1))));
		for (std::uint32_t f = 0; f < 200; ++f)
		{
			send(at(1), encodeData(1, packetOf(0, f, frameInfo(0, f))));
		}
		std::error_code ec;
		char name[32];
		std::snprintf(name, sizeof name, "%016llx.obfmerlog.part", (unsigned long long)kGame);
		std::filesystem::remove(dir / name, ec);
		REQUIRE_FALSE(ec);
		std::vector<std::uint8_t> req = encodeLogRequest(0);
		CHECK(relay.handle(at(2), req.data(), req.size(), 1000, 1700000000u).kind == RelayCore::Action::NONE);
		CHECK(relay.stats().logRateLimited == 1);
		CHECK(relay.stats().logErrors == 0);
		relay.shutdown(2000);
	}
	{
		// three clients of one game with ample budgets of their own; the game's budget is two chunks: the third waits
		RelayCore::Options o;
		o.keys = ring();
		o.logDir = dir.string();
		o.logGameBurstBytes = 2 * (kLogChunkBytes + kHeaderBytes + 8);
		o.logGameBytesPerSecond = 0;
		RelayCore relay(o);
		auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
		send(at(1), encodeHello(bytesOf(ticket(0))));
		send(at(2), encodeHello(bytesOf(ticket(1))));
		send(at(3), encodeHello(bytesOf(ticket(0, 0x03, true))));
		for (std::uint32_t f = 0; f < 2000; ++f)
		{
			send(at(1), encodeData(1, packetOf(0, f, frameInfo(0, f))));
		}
		std::vector<std::uint8_t> req = encodeLogRequest(0);
		CHECK(relay.handle(at(1), req.data(), req.size(), 1000, 1700000000u).kind == RelayCore::Action::REPLY);
		CHECK(relay.handle(at(2), req.data(), req.size(), 1000, 1700000000u).kind == RelayCore::Action::REPLY);
		CHECK(relay.handle(at(3), req.data(), req.size(), 1000, 1700000000u).kind == RelayCore::Action::NONE);
		CHECK(relay.stats().logRateLimited == 1);
		relay.shutdown(2000);
	}
	{
		// the relay's budget across games: one chunk in all
		RelayCore::Options o;
		o.keys = ring();
		o.logDir = dir.string();
		o.logTotalBurstBytes = kLogChunkBytes + kHeaderBytes + 8;
		o.logTotalBytesPerSecond = 0;
		RelayCore relay(o);
		auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
		send(at(1), encodeHello(bytesOf(ticketOf(1, 0))));
		send(at(2), encodeHello(bytesOf(ticketOf(2, 0))));
		for (std::uint32_t f = 0; f < 2000; ++f)
		{
			send(at(1), encodeData(0, packetOf(0, f, frameInfo(0, f))));
			send(at(2), encodeData(0, packetOf(0, f, frameInfo(0, f))));
		}
		std::vector<std::uint8_t> req = encodeLogRequest(0);
		CHECK(relay.handle(at(1), req.data(), req.size(), 1000, 1700000000u).kind == RelayCore::Action::REPLY);
		CHECK(relay.handle(at(2), req.data(), req.size(), 1000, 1700000000u).kind == RelayCore::Action::NONE);
		relay.shutdown(2000);
	}
	{
		// the log's size cap: the log stops (an error), as on a failed write; a LOG_REQUEST then finds no log
		RelayCore::Options o;
		o.keys = ring();
		o.logDir = dir.string();
		o.maxLogBytes = 400;
		RelayCore relay(o);
		auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), 1000, 1700000000u); };
		send(at(1), encodeHello(bytesOf(ticket(0))));
		send(at(2), encodeHello(bytesOf(ticket(1))));
		for (std::uint32_t f = 0; f < 100; ++f)
		{
			send(at(1), encodeData(1, packetOf(0, f, frameInfo(0, f))));
		}
		// the header (25 bytes) and two EVENTs (7 each) are 39 bytes; a FRAMEINFO's COMMAND record is 18: 20 fit in 400, the 21st stops the log
		CHECK(relay.stats().logErrors == 1);
		CHECK(relay.stats().commandsLogged == 20);
		REQUIRE(relay.errors().size() == 1);
		CHECK(relay.errors()[0].find("reached 400 bytes") != std::string::npos);
		std::vector<std::uint8_t> req = encodeLogRequest(0);
		RelayCore::Action a = relay.handle(at(2), req.data(), req.size(), 1000, 1700000000u);
		CHECK(a.reply[3] == REFUSED);
		relay.shutdown(2000);
	}
	std::filesystem::remove_all(dir);
}

TEST_CASE("online3 r1: a delayed spectator during a pause: the log stops growing, so the spectator's cut-off stands still however long the pause; it "
		  "moves on with the frames after the resume")
{
	using namespace RelayProtocol;
	const std::filesystem::path dir = tempDir("r1pause");
	RelayCore::Options o;
	o.keys = ring();
	o.logDir = dir.string();
	RelayCore relay(o);
	std::uint64_t now = 1000;
	auto send = [&](const RelayAddress &from, std::vector<std::uint8_t> d) { return relay.handle(from, d.data(), d.size(), now, 1700000000u); };
	send(at(1), encodeHello(bytesOf(ticket(0))));
	send(at(2), encodeHello(bytesOf(ticket(1))));
	REQUIRE(send(at(3), encodeHello(bytesOf(ticket(0, 0x03, true, 60)))).reply[3] == WELCOME); // 300 frames behind
	std::uint32_t seq = 0;
	auto play = [&](std::uint32_t from, std::uint32_t to) {
		for (std::uint32_t f = from; f <= to; ++f)
		{
			now += 200; // 5 frames a second
			send(at(1), encodeData(1, packetOf(0, seq++, frameInfo(0, f))));
		}
	};
	play(0, 400);
	CHECK(maxFrameIn(readLog(relay, at(3), now)) == 100);
	// paused before frame 401 for two minutes: only keep-alives (never logged); the spectator reads every second and stays bound
	NetCommandMsg keepAlive;
	keepAlive.type = NETCOMMANDTYPE_KEEPALIVE;
	keepAlive.slot = 0;
	const std::vector<std::uint8_t> keepAliveBytes = NetPacket::encodeCommand(keepAlive, nullptr);
	for (int s = 0; s < 120; ++s)
	{
		now += 1000;
		send(at(1), encodeData(1, packetOf(0, seq++, keepAliveBytes)));
		relay.tick(now);
		CHECK(maxFrameIn(readLog(relay, at(3), now)) == 100);
	}
	CHECK(relay.stats().spectatorsDropped == 0);
	play(401, 500);
	CHECK(maxFrameIn(readLog(relay, at(3), now)) == 200);
	relay.shutdown(now);
	std::filesystem::remove_all(dir);
}

TEST_CASE("online3 spectators: a frame that ran before its CRC was in the log is compared when a newer log has it (a spectator at the log's edge)")
{
	// found by the home-lab spectator run: the CRC describing frame F executes a few frames after F, so a spectator following the log's edge ran F
	// before the log carried it and compared no CRC at all
	CommandLogCatchUp c(100);
	auto older = std::make_shared<RelayLog::Assembled>();
	c.setLog(older);
	FrameCompletion f;
	f.frame = 100;
	f.hash = 0x1234u;
	f.hashed = f.breakdown = true;
	c.completed(f);
	f.frame = 200;
	f.hash = 0x5678u;
	c.completed(f);
	CHECK(c.crcsCompared() == 0);
	auto newer = std::make_shared<RelayLog::Assembled>();
	newer->hashes[100] = 0x1234u;
	c.setLog(newer);
	CHECK(c.crcsCompared() == 1);
	CHECK(c.crcMismatches() == 0);
	auto newest = std::make_shared<RelayLog::Assembled>();
	newest->hashes[100] = 0x1234u;
	newest->hashes[200] = 0x9999u; // the players' CRC of frame 200 differs: a mismatch, each frame counted once
	c.setLog(newest);
	CHECK(c.crcsCompared() == 2);
	CHECK(c.crcMismatches() == 1);
	CHECK(c.firstMismatch() == "frame 200: the players' CRC 0x00009999, the catch-up 0x00005678");
	c.setLog(newest);
	CHECK(c.crcsCompared() == 2);
}

TEST_CASE("online3 spectators retail: no local slot makes the observer's ReplayObserver side the local player with the whole map revealed; the same "
		  "frames give the same state hashes as a player's game; observe-next steps the view through the human players and back to all")
{
	OPENBFME_REQUIRE_START(s);
	NewGameMessage m;
	m.game.mapName = "maps/map mp fall back 4p/map mp fall back 4p.map";
	m.game.seed = 777u;
	m.game.startingCash = 1500;
	const char *factions[] = { "FactionMen", "FactionMordor", "FactionElves" };
	for (int i = 0; i < 3; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[i];
		sl.state = i < 2 ? SLOT_PLAYER : SLOT_HARD_AI;
		sl.name = i < 2 ? u"Human" + std::u16string(1, (char16_t)(u'0' + i)) : u"Computer";
		sl.playerTemplate = factionOf(s->world->playerTemplates(), factions[i]);
		sl.startPos = i;
		sl.color = i;
		sl.teamNumber = i;
	}
	Loaded player, spectator;
	starttest::Shared *spectatorWorld = starttest::peerWorld(); // lane MAPRESET-1 (merge q7): one live game per world (S-3808): the spectator has its own
	REQUIRE(spectatorWorld);
	REQUIRE_MESSAGE(spectatorWorld->world, spectatorWorld->error);
	loadAs(*s, m, 0, player);
	loadAs(*spectatorWorld, m, -1, spectator);
	// the spectator's local player is the observer side (RW 0x626E1F's ReplayObserver), not active
	const Player *local = spectator.game->players().getLocalPlayer();
	REQUIRE(local);
	CHECK(local->getPlayerName() == "ReplayObserver");
	CHECK(local->isObserver());
	CHECK(player.game->players().getLocalPlayer()->getPlayerName() != "ReplayObserver");
	// the whole map revealed for it (RW 0x62F91A: revealMapForPlayerPermanently), shrouded somewhere for the player
	ShroudManager &sm = spectator.game->shroud();
	int notClear = 0, playerNotClear = 0;
	for (int y = 0; y < sm.cellCountY(); ++y)
	{
		for (int x = 0; x < sm.cellCountX(); ++x)
		{
			notClear += sm.getCellStatus(local->getPlayerIndex(), x, y) != CELLSHROUD_CLEAR ? 1 : 0;
			playerNotClear += player.game->shroud().getCellStatus(player.game->players().getLocalPlayer()->getPlayerIndex(), x, y) != CELLSHROUD_CLEAR ? 1 : 0;
		}
	}
	// tolerance: a precondition: the shroud grid exists
	CHECK(sm.cellCountX() > 0);
	CHECK(notClear == 0);
	// tolerance: the claim is that a player's own view has shrouded cells (unlike the observer's); how many depends on the map
	CHECK(playerNotClear > 0);
	// 200 frames with the same input: the same state hash on every frame (the local player is no simulation input)
	const int human = player.game->players().findPlayerWithName(player.game->report().startSlotPlayers[0])->getPlayerIndex();
	REQUIRE(spectator.game->players().findPlayerWithName(spectator.game->report().startSlotPlayers[0])->getPlayerIndex() == human);
	int differ = 0;
	for (int i = 0; i < 200; ++i)
	{
		inputFor(*player.game, human, player.game->commands());
		inputFor(*spectator.game, human, spectator.game->commands());
		REQUIRE(player.game->advance(0.2) == 1);
		REQUIRE(spectator.game->advance(0.2) == 1);
		differ += player.game->logic().computeStateHash() != spectator.game->logic().computeStateHash() ? 1 : 0;
	}
	CHECK(differ == 0);
	// observe-next: the player's own game refuses (an active local player); the spectator steps Human0, Human1, then back to the observer ("all")
	CHECK_FALSE(player.game->observeNextPlayer(true));
	std::vector<std::string> seen;
	for (int i = 0; i < 4; ++i)
	{
		REQUIRE(spectator.game->observeNextPlayer(true));
		seen.push_back(spectator.game->players().getNthPlayer(spectator.game->observedPlayer())->getPlayerName());
		CHECK(spectator.game->shroud().localPlayer() == spectator.game->observedPlayer()); // the view follows (RW 0xB4D8E0)
	}
	const std::string h0 = spectator.game->report().startSlotPlayers[0], h1 = spectator.game->report().startSlotPlayers[1];
	CHECK(seen == std::vector<std::string>{ h0, h1, "ReplayObserver", h0 });
	REQUIRE(spectator.game->observeNextPlayer(false));
	CHECK(spectator.game->players().getNthPlayer(spectator.game->observedPlayer())->getPlayerName() == "ReplayObserver");
	// the snapshot's shroud view is the observed player's: back on "all", nothing is shrouded
	spectator.game->advance(0.2);
	spectator.game->drainFrames();
	const auto snap = spectator.game->latestSnapshot();
	REQUIRE(snap);
	REQUIRE(snap->shroud);
	CHECK(snap->shroud->localPlayer == local->getPlayerIndex());
	int shrouded = 0;
	for (std::uint8_t c : snap->shroud->status)
	{
		shrouded += c != CELLSHROUD_CLEAR ? 1 : 0;
	}
	CHECK(shrouded == 0);
}
