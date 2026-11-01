// OpenBFME tests. GPL-3.0. One headless process of p16_net_soak.py; no disk map or shipping mount-policy change.
#include "doctest.h"
#include "P16TestUtil.h"
#include "GameNetwork/NetGameSession.h"
#include "GameNetwork/LockstepDriver.h"
#include "GameNetwork/Rejoin.h"
#include "GameNetwork/RelayLog.h"
#include "GameNetwork/ScriptedPlayer.h"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace
{
std::string requiredEnv(const char *key)
{
	const char *value = std::getenv(key);
	REQUIRE_MESSAGE(value != nullptr, key);
	REQUIRE_MESSAGE(*value != '\0', key);
	return value;
}

int integerEnv(const char *key, int low, int high)
{
	const std::string value = requiredEnv(key);
	int number = 0;
	const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
	REQUIRE(parsed.ec == std::errc());
	REQUIRE(parsed.ptr == value.data() + value.size());
	const bool valid = number >= low && number <= high;
	REQUIRE_MESSAGE(valid, key);
	return number;
}

// ONLINE16-FIX: optional integer settings (absent: -1)
int optionalEnv(const char *key, int low, int high)
{
	const char *value = std::getenv(key);
	return value && *value ? integerEnv(key, low, high) : -1;
}

std::vector<std::uint8_t> fileBytes(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	REQUIRE_MESSAGE(f.good(), path);
	return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), {});
}
}

TEST_CASE("p16 net soak: one real UDP participant" * doctest::skip())
{
	const int slot = integerEnv("OPENBFME_P16_SLOT", 0, 15);
	const int ticks = integerEnv("OPENBFME_P16_TICKS", 30, 18000);
	const bool paced = integerEnv("OPENBFME_P16_PACED", 0, 1) != 0;
	const std::string mode = requiredEnv("OPENBFME_P16_MODE");
	const bool validMode = mode == "lan" || mode == "relay";
	REQUIRE(validMode);
	const std::filesystem::path output = requiredEnv("OPENBFME_P16_OUTPUT");
	REQUIRE(std::filesystem::is_directory(output));
	// ONLINE16-FIX (relay mode): OPENBFME_P16_MACKEY the lobby's per-slot k (32 bytes: authenticated datagrams); OPENBFME_P16_VANISH_AT this process
	// crashes at that protocol frame (no leave); OPENBFME_P16_REJOIN=1 this process is the slot's next incarnation (a rejoin ticket: catch-up from
	// the relay's log, then lockstep); OPENBFME_P16_DROP_SLOT the slot that drops and rejoins in this game (every peer's end checks know it);
	// OPENBFME_P16_DISCONNECT_MS / OPENBFME_P16_TIMEOUT_MS replace GameData's disconnect times (the drop comes sooner)
	const int vanishAt = optionalEnv("OPENBFME_P16_VANISH_AT", 3, 18000);
	const bool rejoin = optionalEnv("OPENBFME_P16_REJOIN", 0, 1) == 1;
	const int dropSlot = optionalEnv("OPENBFME_P16_DROP_SLOT", 0, 15);
	const int disconnectMs = optionalEnv("OPENBFME_P16_DISCONNECT_MS", 500, 60000);
	const int timeoutMs = optionalEnv("OPENBFME_P16_TIMEOUT_MS", 500, 600000);
	const int loadSeconds = optionalEnv("OPENBFME_P16_LOAD_S", 30, 3600); // a loaded host's sixteen worlds take longer to load (default 180)
	const std::string barrierPrefix = (output / ("peer-" + std::to_string(slot))).string();
	const std::string prefix = barrierPrefix + (rejoin ? "-rejoin" : "");
	REQUIRE_FALSE(std::filesystem::exists(prefix + ".csv"));
	REQUIRE_FALSE(std::filesystem::exists(prefix + ".replay"));
	std::ifstream ports(requiredEnv("OPENBFME_P16_PORTS"));
	REQUIRE(ports.good());
	LobbyStart lobby;
	std::set<int> uniquePorts;
	for (int i = 0; i < 16; ++i)
	{
		int port = 0;
		REQUIRE(bool(ports >> port));
		const bool valid = port > 0 && port <= 65535;
		REQUIRE(valid);
		REQUIRE(uniquePorts.insert(port).second);
		lobby.addresses[(size_t)i] = NetAddress{ 0x7F000001u, (std::uint16_t)port };
		lobby.networkSlot[(size_t)i] = true;
	}
	ports >> std::ws;
	REQUIRE(ports.eof());
	UDP socket;
	std::string error;
	REQUIRE_MESSAGE(socket.bind(0x7F000001u, lobby.addresses[(size_t)slot].port, &error), error);
	auto world = starttest::freshWorld();
	REQUIRE_MESSAGE(world, "ROTWK_INSTALL and BFME2_INSTALL are required; a soak must not skip missing data");
	REQUIRE_MESSAGE(world->world, world->error);
	auto context = world->world->enterContext();
	p16test::mount(*world);
	lobby.game = p16test::message(*world, 16);
	lobby.runAhead = 2;
	lobby.crcInterval = 10;
	lobby.profile = ProfileIdentity::compute(world->mount->identityArchives, RandomAlgorithm::ZH_CarryChain);
	LobbyStart decoded;
	REQUIRE_MESSAGE(LANLobby::decodeStart(LANLobby::encodeStart(lobby), decoded, &error), error);
	lobby = std::move(decoded);
	lobby.localSlot = slot;
	if (mode == "relay")
	{
		lobby.online = true;
		lobby.relay = NetAddress{ 0x7F000001u, (std::uint16_t)integerEnv("OPENBFME_P16_RELAY_PORT", 1, 65535) };
		std::ifstream ticket(requiredEnv("OPENBFME_P16_TICKET"), std::ios::binary);
		REQUIRE(ticket.good());
		lobby.ticket.assign(std::istreambuf_iterator<char>(ticket), {});
		REQUIRE_FALSE(ticket.bad());
		REQUIRE(lobby.ticket.size() == (rejoin ? RelayProtocol::kTicketBytesV2_16 : RelayProtocol::kTicketBytes16));
		REQUIRE(lobby.ticket[0] == (rejoin ? 5 : 4));
		RelayProtocol::Ticket fields;
		REQUIRE_MESSAGE(RelayProtocol::readTicketFields(lobby.ticket.data(), lobby.ticket.size(), fields, &error), error);
		REQUIRE(fields.slot == slot);
		REQUIRE(fields.slotMask == 0xffff);
		lobby.onlineGameId = fields.gameId;
		lobby.rejoinGeneration = rejoin ? (int)fields.generation : 0;
		if (const char *mac = std::getenv("OPENBFME_P16_MACKEY"); mac && *mac)
		{
			lobby.macKey = fileBytes(mac);
			REQUIRE(lobby.macKey.size() == RelayAuth::kSessionKeyBytes);
		}
	}
	else
	{
		REQUIRE(vanishAt == -1);
		REQUIRE_FALSE(rejoin);
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(lobby.game, world->world->setupPlayerTemplates(), world->settings,
		{ p16test::cacheEntry() }, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	start.localSlot = slot;
	ArchiveW3DFileSource source(*world->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*world->world, *world->mount->fs, assets, world->options);
	LiveGame::Options gameOptions;
	gameOptions.start = &start;
	REQUIRE_MESSAGE(game.load(gameOptions, &error), error);
	REQUIRE(game.frame() == 1);
	NetGameSession::Options options;
	options.replayPath = rejoin ? std::string() : prefix + ".replay"; // a rejoined client records no replay (the relay's log is the record)
	options.profile = lobby.profile;
	options.adaptiveRunAhead = false;
	options.hashEveryFrame = true; // ONLINE16-FIX: a rejoined incarnation records no replay; its frames are hashed anyway
	options.desyncDirectory = output.string();
	REQUIRE_MESSAGE(NetworkSettings::load(*world->mount->fs, options.network, &error), error);
	if (disconnectMs > 0)
	{
		options.network.disconnectTime = (std::uint32_t)disconnectMs;
	}
	if (timeoutMs > 0)
	{
		options.network.playerTimeoutTime = (std::uint32_t)timeoutMs;
	}
	NetGameSession session(socket, lobby, options);
	std::uint64_t rejoinMs = 0;
	// the relay datagrams this incarnation refused before its rejoin ticket bound: with the crashed process's UDP port reused, the relay still
	// forwards to the old incarnation (sealed with its keys) until the new HELLO rebinds the slot; those must drop, nothing after the bind may
	unsigned long long droppedBeforeBind = 0;
	if (rejoin)
	{
		// ONLINE16-FIX: the slot's next incarnation: bind with the rejoin ticket, catch up from the relay's log, switch to lockstep at its REJOIN
		REQUIRE_MESSAGE(session.beginRejoin(game, &error), error);
		NetGameSession::RejoinState rs = NetGameSession::RejoinState::Binding;
		const auto rejoinStarted = NetMilliseconds();
		NetGameSession::RejoinState shown = NetGameSession::RejoinState::Failed;
		while (rs != NetGameSession::RejoinState::Live && rs != NetGameSession::RejoinState::Failed)
		{
			rs = session.rejoinStep(game, 100);
			if (rs != shown && shown == NetGameSession::RejoinState::Binding)
			{
				droppedBeforeBind = session.transport().stats().relayAuthDropped;
			}
			if (rs != shown)
			{
				MESSAGE("rejoin state " << (int)rs << " at " << (NetMilliseconds() - rejoinStarted) << " ms, logic frame " << game.frame());
				shown = rs;
			}
			// tolerance: wall-clock deadline, not simulation state.
			REQUIRE(NetMilliseconds() - rejoinStarted < 300000);
			if (rs == NetGameSession::RejoinState::Binding || rs == NetGameSession::RejoinState::Waiting)
			{
				NetSleepMilliseconds(2);
			}
		}
		REQUIRE_MESSAGE(rs == NetGameSession::RejoinState::Live, session.rejoinReport().failure);
		REQUIRE(session.rejoinReport().crcsCompared > 0); // tolerance: the catch-up compared the players' CRCs (their count depends on the drop frame)
		rejoinMs = session.rejoinReport().liveMs - session.rejoinReport().startedMs;
	}
	else
	{
		REQUIRE_MESSAGE(session.begin(game, &error), error);
		const auto loadStarted = NetMilliseconds();
		while (!session.pollLoaded())
		{
			if (NetMilliseconds() - loadStarted >= (loadSeconds > 0 ? (std::uint64_t)loadSeconds * 1000u : 180000u))
			{
				const Transport::Stats &ts = session.transport().stats();
				MESSAGE("load deadline: bound " << session.transport().relayBound() << " refusal " << session.transport().relayRefusal() << " auth_dropped "
					<< ts.relayAuthDropped << " bad " << ts.badPackets << " sent " << ts.packetsSent << " received " << ts.packetsReceived << " resends "
					<< ts.commandsResent << " not_bound " << ts.relayNotBound << " hellos " << ts.relayHellos);
				for (const auto &why : session.transport().errors()) MESSAGE("Transport error: ", why);
				for (const auto &why : session.network().errors()) MESSAGE("Network error: ", why);
			}
			// tolerance: wall-clock deadline, not simulation state or an expected hash.
			REQUIRE(NetMilliseconds() - loadStarted < (loadSeconds > 0 ? (std::uint64_t)loadSeconds * 1000u : 180000u));
			NetSleepMilliseconds(2);
		}
	}
	REQUIRE(session.transport().relayBound() == lobby.online);
	std::ofstream hashes(prefix + ".csv");
	REQUIRE(hashes.good());
	hashes << "frame,state_hash\n";
	unsigned completed = 0;
	const unsigned firstFrame = rejoin ? (unsigned)game.frame() + 1 : 2u; // a rejoined incarnation hashes from its first lockstep frame on
	session.driver().setOnCompleted([&](const FrameCompletion &frame) {
		REQUIRE(frame.hashed);
		REQUIRE(frame.frame == completed + firstFrame);
		hashes << frame.frame << ',' << frame.hash << '\n';
		++completed;
	});
	const unsigned stopFrame = (unsigned)ticks + 1;
	session.driver().setStopFrame(stopFrame);
	const auto began = NetMilliseconds();
	auto progress = began;
	unsigned lastScripted = 0;
	unsigned scripted = 0;
	if (vanishAt > 0)
	{
		session.driver().setStopFrame((unsigned)vanishAt);
	}
	while (game.protocolFrame() < stopFrame)
	{
		session.service();
		if (session.quitRequested())
		{
			// the reason before the assertion below fails (a drop scenario's diagnostics)
			MESSAGE("quit at protocol frame " << game.protocolFrame() << ": " << session.quitReason());
			for (const auto &why : session.network().errors()) MESSAGE("Network error: ", why);
			for (const auto &line : session.network().rejoinLog()) MESSAGE("rejoin log: ", line);
		}
		if (vanishAt > 0 && game.protocolFrame() >= (unsigned)vanishAt)
		{
			// ONLINE16-FIX: a crash: no leave, nothing more sent; the frames it ran are its evidence
			game.drainFrames();
			hashes.flush();
			std::ofstream(prefix + ".vanished") << "vanished_at " << game.protocolFrame() << "\nrelay_auth " << (lobby.macKey.empty() ? 0 : 1) << "\n";
			std::fflush(stdout);
			std::_Exit(0);
		}
		REQUIRE_FALSE(session.quitRequested());
		if (paced && NetMilliseconds() < began + (std::uint64_t)completed * 200)
		{
			NetSleepMilliseconds(1);
			continue;
		}
		if (game.frame() != lastScripted)
		{
			lastScripted = game.frame();
			scripted += ScriptedPlayer::issue(game, session.config().slotPlayerIndex[(size_t)slot], slot, game.commands());
		}
		if (game.advance(0.2)) progress = NetMilliseconds();
		else NetSleepMilliseconds(1);
		// tolerance: a stalled peer fails; no timeout drop is accepted as successful progress.
		REQUIRE(NetMilliseconds() - progress < 90000);
		REQUIRE_FALSE(session.network().sawCRCMismatch());
	}
	game.drainFrames();
	const auto elapsed = NetMilliseconds() - began;
	REQUIRE(completed + firstFrame - 1 == stopFrame); // every frame from the first one to the stop frame hashed
	REQUIRE(game.protocolFrame() == stopFrame);
	REQUIRE(scripted != 0); // tolerance: workload must include commands, not only empty FRAMEINFO records.
	for (int i = 0; i < 16; ++i)
	{
		REQUIRE(session.network().isGameSlot(i));
		// ONLINE16-FIX: the dropped slot left the packet-router order at its drop for good (Network::passRouterRole: a rejoined incarnation never
		// routes again), and is connected again: it rejoined
		REQUIRE(session.network().hasDeparted(i) == (i == dropSlot));
		REQUIRE_FALSE(session.network().isDisconnected(i));
	}
	// CRC of F is compared on F + run-ahead; batches stop at ticks, inclusive. A game with a drop compares fewer (the held frames, the rejoiner's
	// catch-up): its count is reported, the frame-by-frame hashes of every process are compared by the script
	if (dropSlot < 0)
	{
		REQUIRE(session.network().crcChecksPassed() == (unsigned long long)((ticks - lobby.runAhead) / lobby.crcInterval));
	}
	hashes.flush();
	REQUIRE(hashes.good());
	std::ofstream report(prefix + ".txt");
	REQUIRE(report.good());
	report << "slot " << slot << "\nticks " << completed << "\nwall_ms " << elapsed
		<< "\ncrc_checks " << session.network().crcChecksPassed() << "\nscripted " << scripted
		<< "\ncommands_relayed " << session.network().commandsRelayed()
		<< "\nrelay_welcomes " << session.transport().stats().relayWelcomes
		<< "\npackets_sent " << session.transport().stats().packetsSent
		<< "\nbytes_sent " << session.transport().stats().bytesSent
		<< "\npackets_received " << session.transport().stats().packetsReceived
		<< "\nbytes_received " << session.transport().stats().bytesReceived
		<< "\nresends " << session.transport().stats().commandsResent
		<< "\nrelay_auth " << (lobby.macKey.empty() ? 0 : 1)
		<< "\nrelay_auth_dropped " << session.transport().stats().relayAuthDropped
		<< "\nrelay_auth_stale " << session.transport().stats().relayAuthStale
		<< "\nrelay_auth_dropped_before_bind " << droppedBeforeBind
		<< "\nrejoin_ms " << rejoinMs
		<< "\nfirst_frame " << firstFrame << '\n';
	report.flush();
	REQUIRE(report.good());
	auto barrier = [&](const char *phase) {
		std::ofstream ready(barrierPrefix + phase);
		REQUIRE(ready.good());
		ready.close();
		const auto waiting = NetMilliseconds();
		for (;;)
		{
			bool all = true;
			for (int i = 0; i < 16; ++i)
				all = all && std::filesystem::exists(output / ("peer-" + std::to_string(i) + phase));
			if (all) break;
			session.service();
			session.network().update(game.protocolFrame(), game.commands());
			// tolerance: bounded end-of-run coordination, outside simulation.
			REQUIRE(NetMilliseconds() - waiting < 30000);
			NetSleepMilliseconds(2);
		}
	};
	barrier(".ready"); // every peer checks membership before any peer announces its departure
	session.finish(game);
	barrier(".left");
	const auto drain = NetMilliseconds();
	while (!session.allAcked() && NetMilliseconds() - drain < 10000)
	{
		session.service();
		session.network().update(game.protocolFrame(), game.commands());
		NetSleepMilliseconds(2);
	}
	REQUIRE(session.allAcked());
	barrier(".acked"); // keep serving acknowledgements until every peer has drained
	for (const auto &why : session.transport().errors()) MESSAGE("Transport error: ", why);
	for (const auto &why : session.network().errors()) MESSAGE("Network error: ", why);
	REQUIRE(session.network().errors().empty());
	REQUIRE(session.transport().errors().empty());
	REQUIRE(session.recordingErrors().empty());
	REQUIRE(session.driver().errors().empty());
	// ONLINE16-FIX: OPENBFME_P16_ALLOW_STOPS=1 (opt-in; default off): a game error that cites a stop registered in docs/STOPS.md ("(S-209)":
	// a headless game has no drawable bones, so a Mordor barricade's exit reports) is written to the report as that stop instead of failing;
	// any other game error still fails
	const bool allowStops = optionalEnv("OPENBFME_P16_ALLOW_STOPS", 0, 1) == 1;
	std::vector<std::string> otherErrors;
	std::ofstream stops(prefix + ".stops");
	for (const auto &why : game.report().errors)
	{
		MESSAGE("Game error: ", why);
		const size_t at = why.find("(S-");
		const bool registered = at != std::string::npos && why.find(')', at) != std::string::npos;
		if (allowStops && registered)
		{
			stops << why << '\n';
		}
		else
		{
			otherErrors.push_back(why);
		}
	}
	REQUIRE(otherErrors.empty());
	game.setFrameDriver(nullptr);
}

// ONLINE16-FIX (Sol #6): the relay's command log of a sixteen-player legacy game, assembled with the validated sixteen-entry slot -> player map and
// re-simulated at full speed from frame 0 (CommandLogCatchUp, what a rejoin and a spectator run): every players' CRC in the log is compared, and every
// CRC frame's hash must equal the one peer 0 hashed live (p16_net_soak.py --replay-log)
TEST_CASE("p16 net soak: the relay log replays with every CRC equal" * doctest::skip())
{
	const std::filesystem::path output = requiredEnv("OPENBFME_P16_OUTPUT");
	const std::string logPath = requiredEnv("OPENBFME_P16_RELAY_LOG");
	const int ticks = integerEnv("OPENBFME_P16_TICKS", 30, 18000);
	std::map<unsigned, std::uint32_t> live;
	{
		std::ifstream csv(output / "peer-0.csv");
		REQUIRE(csv.good());
		std::string line;
		std::getline(csv, line);
		REQUIRE(line == "frame,state_hash");
		while (std::getline(csv, line))
		{
			const size_t comma = line.find(',');
			REQUIRE(comma != std::string::npos);
			live[(unsigned)std::stoul(line.substr(0, comma))] = (std::uint32_t)std::stoul(line.substr(comma + 1));
		}
	}
	REQUIRE(live.size() == (size_t)ticks);
	auto world = starttest::freshWorld();
	REQUIRE_MESSAGE(world, "ROTWK_INSTALL and BFME2_INSTALL are required");
	REQUIRE_MESSAGE(world->world, world->error);
	auto context = world->world->enterContext();
	p16test::mount(*world);
	LobbyStart lobby;
	lobby.game = p16test::message(*world, 16);
	std::string error;
	RelayLogFile log;
	REQUIRE_MESSAGE(RelayLogFile::load(logPath, log, &error), error);
	REQUIRE(log.complete);
	REQUIRE(log.slotMask == 0xffff);
	REQUIRE(log.gaps.empty());
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(lobby.game, world->world->setupPlayerTemplates(), world->settings,
		{ p16test::cacheEntry() }, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	start.localSlot = 0;
	ArchiveW3DFileSource source(*world->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*world->world, *world->mount->fs, assets, world->options);
	LiveGame::Options gameOptions;
	gameOptions.start = &start;
	REQUIRE_MESSAGE(game.load(gameOptions, &error), error);
	NetworkConfig config;
	config.localSlot = 0;
	config.runAhead = 2;
	config.crcInterval = 10;
	const std::vector<std::string> &names = game.startSlotPlayers();
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		const Player *p = (size_t)s < names.size() ? game.players().findPlayerWithName(names[(size_t)s]) : nullptr;
		REQUIRE(p != nullptr);
		config.slotPlayerIndex[(size_t)s] = p->getPlayerIndex();
	}
	auto assembled = std::make_shared<RelayLog::Assembled>();
	REQUIRE_MESSAGE(RelayLog::assemble(log, config, *assembled, &error), error);
	REQUIRE(assembled->crcDisagreements == 0);
	REQUIRE(assembled->problems.empty());
	// every frame the replay completes, hashed as the live peers hashed theirs
	struct Recording : CommandLogCatchUp
	{
		using CommandLogCatchUp::CommandLogCatchUp;
		std::map<unsigned, std::uint32_t> hashes;
		void completed(const FrameCompletion &c) override
		{
			if (c.hashed)
			{
				hashes[c.frame] = c.hash;
			}
			CommandLogCatchUp::completed(c);
		}
		Capture capture() const override
		{
			Capture k = CommandLogCatchUp::capture();
			k.hashEveryFrame = true;
			return k;
		}
	};
	Recording catchUp(config.crcInterval);
	catchUp.setLog(assembled);
	RegisterLogicCRCHandler(game.dispatch());
	game.setFrameDriver(&catchUp);
	while (game.advance(0.2) > 0)
	{
	}
	game.drainFrames();
	game.setFrameDriver(nullptr);
	CHECK(catchUp.crcMismatches() == 0);
	CHECK(catchUp.firstMismatch().empty());
	unsigned compared = 0, differing = 0;
	for (const auto &[frame, hash] : live)
	{
		const auto it = catchUp.hashes.find(frame);
		REQUIRE_MESSAGE(it != catchUp.hashes.end(), "the replay did not reach frame " << frame);
		differing += it->second != hash;
		++compared;
	}
	CHECK(differing == 0);
	CHECK(compared == (unsigned)ticks);
	std::ofstream report(output / "replay.txt");
	report << "replayed_to " << game.frame() << "\ncrcs_compared " << catchUp.crcsCompared() << "\ncrc_mismatches " << catchUp.crcMismatches()
		   << "\nlive_hashes_compared " << compared << "\nlive_hashes_differing " << differing << "\nlog_complete_frame " << assembled->lastCompleteFrame << "\n";
	REQUIRE(report.good());
	REQUIRE(catchUp.crcsCompared() > 0); // tolerance: the count depends on the game's length; a replay comparing none proves nothing
}
