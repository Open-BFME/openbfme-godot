// OpenBFME. SMOOTH-1 review r4 (the MP-1 integration): the logic worker in a lockstep game, in one process, for the regular suite and the race detector
// (tools/smooth/sanitize_threaded.sh builds this file under TSan and ASan).
//
// The local peer is a real retail skirmish (LiveGame + LockstepDriver + Network + the UDP Transport on localhost, with dropped, delayed and reordered
// datagrams and fragmented commands); the remote peer is a protocol peer without a game (a Network on its own Transport) that keeps the lockstep: it
// relays every frame once all frame data is in, sends commands of its own, and sends the CRC of every CRC frame with the value the local peer sent (it has
// no state to hash), so the CRC exchange runs end to end through the completions the worker captured. The local input comes from an input source keyed to
// protocol frames (LockstepDriver::setInputSource): it is stamped against an explicit protocol frame, whatever the render rate or the worker's timing.
//
// Every scenario (the single-thread fallback, the six-tick boundary, the worker at different render rates and worker delays, a stall longer than the
// catch-up cap, a pause, driver replacement and worker switches) must give the same ordered command record of every batch, the same state hash and RNG
// after every frame, CRC checks that pass, no batch before its input is complete, no skipped or repeated frame number, and an uninterrupted presentation
// while input is missing. The recording plays back (single thread and threaded) with every frame's hash equal.
#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Recorder.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameNetwork/LockstepDriver.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/Transport.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr int kRunAhead = 2;
constexpr int kCrcInterval = 25;
constexpr int kFrames = 130;

int templateIndexOf(const PlayerTemplateStore &store, const std::string &name)
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

NewGameMessage twoHumans(const starttest::Shared &s)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = 4242u;
	m.game.startingCash = 1500;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &h = m.game.slots[(size_t)i];
		h.state = SLOT_PLAYER;
		h.name = i == 0 ? u"Local" : u"Remote";
		h.playerTemplate = templateIndexOf(s.world->playerTemplates(), factions[i]);
		h.startPos = i;
		h.color = i;
		h.teamNumber = i;
	}
	return m;
}

const std::vector<MapCacheEntry> &maps(starttest::Shared &s)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		std::string error;
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	return cache;
}

// the receive side of the remote peer's transport, peeking at the local peer's MSG_LOGIC_CRCs (execution frame -> CRC)
struct CrcPeek : NetTransport
{
	explicit CrcPeek(Transport &t) : transport(t) {}
	bool sendTo(int slot, const std::vector<std::uint8_t> &bytes, std::string *error) override { return transport.sendTo(slot, bytes, error); }
	bool receive(int &fromSlot, std::vector<std::uint8_t> &bytes) override
	{
		if (!transport.receive(fromSlot, bytes))
		{
			return false;
		}
		NetCommandMsg c;
		if (NetPacket::decodeCommand(bytes, c, nullptr) && c.type == NETCOMMANDTYPE_GAMECOMMAND && c.message && c.message->getType() == MSG_LOGIC_CRC)
		{
			crcs[c.executionFrame] = (std::uint32_t)c.message->getArgument(0)->integer;
		}
		return true;
	}
	void service() override { transport.service(); }
	void retire(int slot) override { transport.retire(slot); }
	Transport &transport;
	std::map<UnsignedInt, std::uint32_t> crcs;
};

// the remote peer: the lockstep protocol without a game
struct ProtocolPeer
{
	ProtocolPeer(const NetworkConfig &config, const Transport::Options &o, UnsignedInt startFrame)
		: transport(socket, 1, o)
		, peek(transport)
		, player(config.slotPlayerIndex[1])
		, protocolFrame(startFrame)
		, nextRelay(startFrame)
	{
		std::string error;
		REQUIRE_MESSAGE(socket.bind(0x7F000001u, 0, &error), error);
		NetworkConfig c = config;
		c.localSlot = 1;
		net = std::make_unique<Network>(c, peek);
	}
	// one tick; `stalled`: the peer is frozen (services its socket, announces nothing new: the local peer's input for the next frames stays incomplete)
	void tick(bool stalled)
	{
		net->update(protocolFrame, pending);
		if (stalled)
		{
			return;
		}
		while (net->isFrameReady(nextRelay) && net->allLoaded())
		{
			CommandList out;
			net->relayCommands(nextRelay, out);
			++nextRelay;
		}
		while (nextRelay > protocolFrame)
		{
			const UnsignedInt next = protocolFrame + 1;
			if (next % kCrcInterval == 0)
			{
				// its CRC of frame `next` goes with execution frame next + run-ahead, as the local peer's does: the value the local peer sent
				const auto it = peek.crcs.find(next + kRunAhead);
				if (it == peek.crcs.end())
				{
					break; // not here yet: this peer waits (so its frame infos stay behind the CRC)
				}
				GameMessage m(MSG_LOGIC_CRC, player);
				m.appendIntegerArgument((int)it->second);
				m.appendBooleanArgument(false);
				pending.append(m);
				++crcsSent;
			}
			if (next % 7 == 0)
			{
				pending.append(GameMessage(MSG_DO_STOP, player)); // a command of its own (its player's empty selection stops)
			}
			protocolFrame = next;
			net->update(protocolFrame, pending);
		}
	}
	UDP socket;
	Transport transport;
	CrcPeek peek;
	std::unique_ptr<Network> net;
	int player;
	UnsignedInt protocolFrame, nextRelay;
	CommandList pending;
	unsigned long long crcsSent = 0;
};

struct Scenario
{
	const char *label;
	bool threaded = false;
	bool sixTick = false;
	std::vector<int> stepMs{ 200 };   ///< render steps, cycled (varied render rates)
	int workerDelayMs = 0;             ///< the simulation owner sleeps after every frame
	int stallAt = -1, stallMs = 0;     ///< the remote peer freezes once the local protocol frame reaches stallAt
	int pauseAt = -1, pauseMs = 0;     ///< presentation-only advance(0) for pauseMs
	int replaceAt = -1;                ///< driver removed and reinstalled
	int toggleAt = -1;                 ///< the worker switched off and on
	int dropPerMille = 0, jitterMs = 0;
};

struct FrameRecord
{
	UnsignedInt frame;
	std::uint32_t hash, rng;
	bool operator==(const FrameRecord &o) const { return frame == o.frame && hash == o.hash && rng == o.rng; }
};

struct Result
{
	std::vector<FrameRecord> frames;     ///< from the completions, in the order consumed
	ReplayFile replay;                   ///< the batches and hashes the driver recorded
	unsigned long long crcPassed = 0, desyncs = 0, stalled = 0, held = 0, presentGaps = 0;
	unsigned long long remoteCrcs = 0;
	std::vector<std::string> errors;
	UnsignedInt startFrame = 0;
	std::uint32_t finalHash = 0;
};

Result runScenario(starttest::Shared &s, const Scenario &sc, const std::string &replayPath)
{
	Result res;
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::string error;
	REQUIRE_MESSAGE(NewGame::prepareNewGame(twoHumans(s), s.world->playerTemplates(), s.settings, maps(s), RandomAlgorithm::ZH_CarryChain, start, &error), error);
	start.localSlot = 0;
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options lo;
	lo.start = &start;
	lo.logicThread = sc.threaded;
	lo.sixTickPacing = sc.sixTick;
	REQUIRE_MESSAGE(game.load(lo, &error), error);
	const LiveGame::Report rep = game.report();
	NetworkConfig cfg;
	cfg.localSlot = 0;
	cfg.runAhead = kRunAhead;
	cfg.crcInterval = kCrcInterval;
	std::vector<ObjectID> units;
	Coord3D home{ 0, 0, 0 };
	for (int slot = 0; slot < 2; ++slot)
	{
		const Player *p = game.players().findPlayerWithName(rep.startSlotPlayers[(size_t)slot]);
		REQUIRE(p != nullptr);
		cfg.slotPlayerIndex[(size_t)slot] = p->getPlayerIndex();
		if (slot == 0)
		{
			for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
			{
				if (o->getControllingPlayer() == p && !o->isKindOfName("STRUCTURE") && units.size() < 40)
				{
					units.push_back(o->getID());
					home = *o->getPosition();
				}
			}
		}
	}
	REQUIRE(!units.empty());
	res.startFrame = game.logic().getFrame();

	Transport::Options to;
	to.resendMs = 5;
	to.packetBytes = 160; // a selection of 40 objects travels in fragments
	to.dropPerMille = sc.dropPerMille;
	to.jitterMs = sc.jitterMs;
	UDP socket;
	REQUIRE_MESSAGE(socket.bind(0x7F000001u, 0, &error), error);
	Transport transport(socket, 0, to);
	to.dropSeed = 7;
	ProtocolPeer remote(cfg, to, res.startFrame);
	transport.setPeer(1, remote.socket.localAddress());
	remote.transport.setPeer(0, socket.localAddress());
	Network net(cfg, transport);
	ReplayWriter writer;
	ReplayHeader h;
	h.game = twoHumans(s);
	h.recordingSlot = 0;
	h.network = cfg;
	REQUIRE_MESSAGE(writer.open(replayPath, h, &error), error);
	LockstepDriver driver(&net, &writer);
	RegisterLogicCRCHandler(game.dispatch());
	const int me = cfg.slotPlayerIndex[0];
	driver.setInputSource([&units, home, me](UnsignedInt pf, CommandList &out) {
		if (pf % 15 != 0)
		{
			return;
		}
		GameMessage sel(MSG_CREATE_SELECTED_GROUP, me);
		sel.appendBooleanArgument(true);
		for (ObjectID id : units)
		{
			sel.appendObjectIDArgument(id);
		}
		out.append(sel);
		GameMessage mv((pf / 15) % 3 == 2 ? MSG_DO_ATTACKMOVETO : MSG_DO_MOVETO, me);
		Coord3D at = home;
		at.x += (pf / 15) % 2 ? 150.0f : -150.0f;
		at.y += 80.0f;
		mv.appendLocationArgument(at);
		out.append(mv);
	});
	const int delay = sc.workerDelayMs;
	driver.setAfterFrame([delay](GameLogic &) {
		if (delay > 0)
		{
			NetSleepMilliseconds(delay); // a slow simulation owner (on the worker when threaded)
		}
	});
	driver.setOnCompleted([&res](const FrameCompletion &c) { res.frames.push_back({ c.frame, c.hash, c.rng }); });
	const UnsignedInt endFrame = res.startFrame + (UnsignedInt)kFrames;
	driver.setStopFrame(endFrame);
	game.setFrameDriver(&driver);
	net.sendLoadComplete();
	remote.net->sendLoadComplete();

	size_t step = 0;
	bool stalled = false, stallDone = false, pauseDone = false, replaceDone = false, toggleDone = false;
	std::uint64_t stallStart = 0;
	UnsignedInt lastPresented = 0;
	const std::uint64_t t0 = NetMilliseconds();
	while (game.protocolFrame() < endFrame && NetMilliseconds() - t0 < 240000)
	{
		const UnsignedInt pf = game.protocolFrame();
		if (!stallDone && sc.stallAt >= 0 && pf >= res.startFrame + (UnsignedInt)sc.stallAt && !stalled)
		{
			stalled = true;
			stallStart = NetMilliseconds();
		}
		if (stalled && NetMilliseconds() - stallStart >= (std::uint64_t)sc.stallMs)
		{
			stalled = false;
			stallDone = true;
		}
		remote.tick(stalled);
		if (!pauseDone && sc.pauseAt >= 0 && pf >= res.startFrame + (UnsignedInt)sc.pauseAt)
		{
			pauseDone = true;
			const UnsignedInt before = game.nextBatchFrame();
			const std::uint64_t p0 = NetMilliseconds();
			while (NetMilliseconds() - p0 < (std::uint64_t)sc.pauseMs)
			{
				remote.tick(false);
				game.advance(0.0); // presentation only: pumped, nothing acquired
				NetSleepMilliseconds(2);
			}
			CHECK(game.nextBatchFrame() == before);
		}
		if (!replaceDone && sc.replaceAt >= 0 && pf >= res.startFrame + (UnsignedInt)sc.replaceAt)
		{
			replaceDone = true;
			game.setFrameDriver(nullptr);
			CHECK(game.logicIdle());
			game.setFrameDriver(&driver);
		}
		if (!toggleDone && sc.toggleAt >= 0 && pf >= res.startFrame + (UnsignedInt)sc.toggleAt)
		{
			toggleDone = true;
			game.setLogicThread(!game.logicThread());
			game.setLogicThread(!game.logicThread());
		}
		game.advance((double)sc.stepMs[step++ % sc.stepMs.size()] / 1000.0);
		// no batch beyond the horizon the remote peer announced (its frame infos reach protocol frame + run-ahead - 1)
		if (game.nextBatchFrame() > remote.protocolFrame + (UnsignedInt)kRunAhead)
		{
			res.errors.push_back("batch " + std::to_string(game.nextBatchFrame() - 1) + " acquired beyond the remote peer's frame data (its frame "
				+ std::to_string(remote.protocolFrame) + ")");
		}
		// the presentation never goes away or backwards while input is missing
		const std::shared_ptr<const LogicSnapshot> shown = game.presentedSnapshot();
		if (!shown || shown->frame < lastPresented)
		{
			++res.presentGaps;
		}
		else
		{
			lastPresented = shown->frame;
		}
		if (sc.threaded || sc.workerDelayMs == 0)
		{
			NetSleepMilliseconds(1);
		}
	}
	CHECK(stallDone == (sc.stallAt >= 0));
	game.drainFrames();
	res.finalHash = game.logic().computeStateHash();
	writer.close(game.protocolFrame(), res.finalHash);
	res.crcPassed = net.crcChecksPassed();
	res.desyncs = net.desyncs().size();
	res.stalled = game.stalledFrames();
	res.held = game.heldPresentations();
	res.remoteCrcs = remote.crcsSent;
	for (const std::string &e : net.errors())
	{
		res.errors.push_back("network: " + e);
	}
	for (const std::string &e : remote.net->errors())
	{
		res.errors.push_back("remote network: " + e);
	}
	for (const std::string &e : driver.errors())
	{
		res.errors.push_back("driver: " + e);
	}
	for (const std::string &e : writer.errors())
	{
		res.errors.push_back("replay: " + e);
	}
	CHECK(driver.batchesAcquired() == (unsigned long long)kFrames);
	CHECK(driver.completionsConsumed() == (unsigned long long)kFrames);
	game.setFrameDriver(nullptr);
	REQUIRE_MESSAGE(ReplayFile::load(replayPath, res.replay, &error), error);
	return res;
}

// the recording played back by a ReplayPlayback that also records each completion's RNG fold
struct RecordingPlayback : ReplayPlayback
{
	explicit RecordingPlayback(const ReplayFile &r) : ReplayPlayback(r) {}
	void completed(const FrameCompletion &c) override
	{
		ReplayPlayback::completed(c);
		frames.push_back({ c.frame, c.hash, c.rng });
	}
	std::vector<FrameRecord> frames;
};

std::vector<FrameRecord> playBack(starttest::Shared &s, const ReplayFile &replay, bool threaded, unsigned long long &compared, bool &mismatch)
{
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::string error;
	REQUIRE_MESSAGE(NewGame::prepareNewGame(replay.header.game, s.world->playerTemplates(), s.settings, maps(s), RandomAlgorithm::ZH_CarryChain, start, &error), error);
	start.localSlot = replay.header.recordingSlot;
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options lo;
	lo.start = &start;
	lo.logicThread = threaded;
	REQUIRE_MESSAGE(game.load(lo, &error), error);
	RecordingPlayback playback(replay);
	RegisterLogicCRCHandler(game.dispatch());
	game.setFrameDriver(&playback);
	while (!playback.finished())
	{
		game.advance(0.05);
	}
	game.drainFrames();
	compared = playback.hashesCompared();
	mismatch = playback.hasMismatch();
	game.setFrameDriver(nullptr);
	return playback.frames;
}

// the ordered command record of every batch, as bytes (GameMessage has no comparison)
std::map<UnsignedInt, std::vector<std::vector<std::uint8_t>>> commandRecord(const ReplayFile &r)
{
	std::map<UnsignedInt, std::vector<std::vector<std::uint8_t>>> out;
	for (const auto &kv : r.frames)
	{
		for (const GameMessage &m : kv.second)
		{
			NetByteWriter w;
			std::string error;
			CHECK_MESSAGE(NetPacket::writeGameMessage(w, m, &error), error);
			out[kv.first].push_back(w.take());
		}
	}
	return out;
}

std::string describe(const Result &r)
{
	char b[256];
	std::snprintf(b, sizeof(b), "%zu frames, %zu recorded batches, CRC checks %llu, desyncs %llu, remote CRCs %llu, stalled %llu, held presentations %llu",
		r.frames.size(), r.replay.frames.size(), r.crcPassed, r.desyncs, r.remoteCrcs, r.stalled, r.held);
	return b;
}
} // namespace

TEST_CASE("smooth1 r4 retail: lockstep batches on the logic worker: every scenario gives the single-thread game's commands, hashes and RNG at every frame")
{
	OPENBFME_REQUIRE_START(s);
	std::vector<Scenario> scenarios;
	{
		Scenario a;
		a.label = "single thread, 200 ms steps";
		scenarios.push_back(a);
		Scenario six;
		six.label = "six tick pacing, 33 ms steps";
		six.sixTick = true;
		six.stepMs = { 33 };
		scenarios.push_back(six);
		Scenario b;
		b.label = "worker, 16 ms steps";
		b.threaded = true;
		b.stepMs = { 16 };
		scenarios.push_back(b);
		Scenario c;
		c.label = "worker, uneven steps, slow worker, lossy reordering links, a stall beyond the catch-up cap, a pause, driver replacement, worker switch";
		c.threaded = true;
		c.stepMs = { 5, 40, 16, 250, 33 };
		c.workerDelayMs = 25;
		c.dropPerMille = 150;
		c.jitterMs = 25;
		c.stallAt = 40;
		c.stallMs = 2500; // 2.5 s: more than maxFramesPerAdvance (10) frames of clock time
		c.pauseAt = 60;
		c.pauseMs = 400;
		c.replaceAt = 80;
		c.toggleAt = 100;
		scenarios.push_back(c);
	}
	std::vector<Result> results;
	for (size_t i = 0; i < scenarios.size(); ++i)
	{
		const std::string path = "smooth1_net_" + std::to_string(i) + ".replay";
		results.push_back(runScenario(*s, scenarios[i], path));
		std::remove(path.c_str());
		const Result &r = results.back();
		MESSAGE(std::string(scenarios[i].label) << ": " << describe(r));
		for (const std::string &e : r.errors)
		{
			MESSAGE("  error: " << e);
		}
		CHECK(r.errors.empty());
		CHECK(r.desyncs == 0);
		CHECK(r.crcPassed >= (unsigned long long)(kFrames / kCrcInterval - 1));
		CHECK(r.presentGaps == 0);
		// each frame exactly once, in order: batch N -> completion N + 1
		REQUIRE(r.frames.size() == (size_t)kFrames);
		for (size_t f = 0; f < r.frames.size(); ++f)
		{
			CHECK(r.frames[f].frame == r.startFrame + 1 + (UnsignedInt)f);
		}
		CHECK(r.replay.hashes.size() == (size_t)kFrames);
	}
	REQUIRE(results.size() == scenarios.size());
	const Result &base = results[0];
	for (size_t i = 1; i < results.size(); ++i)
	{
		INFO(std::string(scenarios[i].label));
		CHECK(results[i].frames == base.frames);         // hash and RNG after every frame
		CHECK(commandRecord(results[i].replay) == commandRecord(base.replay)); // the ordered command record of every batch
		CHECK(results[i].replay.hashes == base.replay.hashes);
		CHECK(results[i].finalHash == base.finalHash);
	}
	CHECK(results[3].stalled > 0); // the stall held frames (their clock time dropped), none was skipped
	size_t commands = 0;
	for (const auto &kv : base.replay.frames)
	{
		commands += kv.second.size();
	}
	CHECK(commands > 20); // the local selections and moves, the remote peer's stops, both CRCs
	MESSAGE("recorded commands: " << commands);

	// the recording is the single-thread baseline: played back on one thread and on the worker, every frame's hash and RNG equal
	for (bool threaded : { false, true })
	{
		unsigned long long compared = 0;
		bool mismatch = true;
		const std::vector<FrameRecord> played = playBack(*s, results[3].replay, threaded, compared, mismatch);
		INFO("playback threaded=" << threaded);
		CHECK(compared == (unsigned long long)kFrames);
		CHECK_FALSE(mismatch);
		CHECK(played == base.frames);
	}
}

TEST_CASE("smooth1 r4 retail: the client's snapshot read of the shroud does not change the state hash (a peer's local player is not hashed)")
{
	OPENBFME_REQUIRE_START(s);
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	std::string error;
	REQUIRE_MESSAGE(NewGame::prepareNewGame(twoHumans(*s), s->world->playerTemplates(), s->settings, maps(*s), RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s->world, *s->mount->fs, assets, s->options);
	LiveGame::Options lo;
	lo.start = &start;
	REQUIRE_MESSAGE(game.load(lo, &error), error);
	game.shroud().setDisplayed(true);
	for (int f = 0; f < 30; ++f)
	{
		game.logic().runLogicFrame();
		const std::uint32_t before = game.logic().computeStateHash();
		game.refreshClient(200.0, 0.0); // builds the snapshot: the local player's object statuses (shroudedForLocal)
		CHECK(game.logic().computeStateHash() == before);
	}
}
