// OpenBFME. GPL-3.0.
// See GameNetwork/NetGameSession.h.

#include "GameNetwork/NetGameSession.h"

#include "Common/Player.h"
#include "GameNetwork/DesyncDump.h"

std::vector<int> NetGameSession::disconnectedPlayerIndices() const
{
	std::vector<int> players;
	if (!m_network && (!spectating() || !m_assembled || !m_catchUp))
	{
		return players;
	}
	const std::uint16_t playbackDrops = !m_network ? m_assembled->disconnectedSlotsAt(m_catchUp->reached()) : 0; // ONLINE16-FIX: 16 wide
	for (int slot = 0; slot < MAX_SLOTS; ++slot)
	{
		const int player = m_config.slotPlayerIndex[(size_t)slot];
		const bool dropped = m_network ? m_network->isDisconnected(slot) : (playbackDrops & (1u << slot)) != 0;
		if (player >= 0 && m_start.game.game.slots[slot].disconnected(dropped))
		{
			players.push_back(player);
		}
	}
	return players;
}

NetGameSession::NetGameSession(UDP &socket, const LobbyStart &start, const Options &options)
	: m_socket(socket)
	, m_start(start)
	, m_options(options)
{
}

NetGameSession::~NetGameSession()
{
	if (m_game)
	{
		m_game->drainFrames(); // SMOOTH-1: the queued batches run and their completions reach the replay before its end record
		if (m_writer && m_writer->isOpen())
		{
			m_writer->close(m_game->protocolFrame(), m_game->logic().computeStateHash()); // a game that ends without finish() still gets its end record
		}
		m_game->setFrameDriver(nullptr);
	}
}

bool NetGameSession::begin(LiveGame &game, std::string *error)
{
	if (!setUp(game, error))
	{
		return false;
	}
	game.setFrameDriver(m_driver.get());
	m_network->sendLoadComplete();
	return true;
}

bool NetGameSession::setUp(LiveGame &game, std::string *error)
{
	const LiveGame::Report r = game.report();
	m_config.localSlot = m_start.localSlot;
	m_config.runAhead = m_start.runAhead;
	m_config.crcInterval = m_start.crcInterval;
	m_config.settings = m_options.network; // lane MP-2
	m_config.adaptiveRunAhead = m_options.adaptiveRunAhead; // the lobby's run-ahead is the minimum
	m_config.earlyOrders = m_options.earlyOrders;           // lane NETLAG-2
	m_config.holdForRejoin = m_start.online;                // lane ONLINE-3: an online drop holds the player for a rejoin (LAN: retail's hand-over)
	m_config.rejoinWindowMs = m_options.rejoinWindowMs;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		m_config.slotNames[(size_t)s] = m_start.game.game.slots[s].name;
	}
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (!m_start.networkSlot[(size_t)s])
		{
			continue;
		}
		const Player *p = (size_t)s < r.startSlotPlayers.size() ? game.players().findPlayerWithName(r.startSlotPlayers[(size_t)s]) : nullptr;
		if (!p)
		{
			if (error)
			{
				*error = "network slot " + std::to_string(s) + " has no player in the loaded game";
			}
			return false;
		}
		m_config.slotPlayerIndex[(size_t)s] = p->getPlayerIndex();
	}
	Transport::Options transportOptions = m_options.transport;
	if (m_start.online)
	{
		// lane ONLINE-2: the START came from the lobby service: every peer is reached through the relay with this client's ticket
		transportOptions.relay.enabled = true;
		transportOptions.relay.address = m_start.relay;
		transportOptions.relay.ticket = m_start.ticket;
		transportOptions.relay.macKey = m_start.macKey; // lane AUTH-1: empty for an old lobby's START
	}
	m_transport = std::make_unique<Transport>(m_socket, m_start.localSlot, transportOptions);
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (m_start.networkSlot[(size_t)s] && s != m_start.localSlot)
		{
			m_transport->setPeer(s, m_start.addresses[(size_t)s]);
		}
	}
	m_network = std::make_unique<Network>(m_config, *m_transport);
	m_network->setChatLobbyMuted(m_start.chatMutedMask); // lane CHAT-1: the lobby moderators' mute (the START's chatMuted)
	if (!m_options.replayPath.empty())
	{
		m_writer = std::make_unique<ReplayWriter>();
		ReplayHeader h;
		h.game = m_start.game;
		h.recordingSlot = m_start.localSlot;
		h.algorithm = m_options.algorithm;
		h.profile = m_options.profile;
		h.network = m_config;
		if (!m_writer->open(m_options.replayPath, h, error))
		{
			return false;
		}
	}
	m_driver = std::make_unique<LockstepDriver>(m_network.get(), m_writer.get());
	m_driver->setCensus(m_options.census); // lane MP-3: before the driver is installed (its capture is read then)
	m_driver->setHashEveryFrame(m_options.hashEveryFrame); // ONLINE16-FIX: likewise
	RegisterLogicCRCHandler(game.dispatch());
	m_game = &game;
	return true;
}

// ---- lane ONLINE-3: rejoin ------------------------------------------------------------------------------------------------------------------
bool NetGameSession::beginRejoin(LiveGame &game, std::string *error)
{
	if (!m_start.online || m_start.rejoinGeneration <= 0)
	{
		if (error)
		{
			*error = "a rejoin needs an online START with a rejoin ticket";
		}
		return false;
	}
	if (!m_options.replayPath.empty())
	{
		if (error)
		{
			*error = "a rejoined client records no replay (the relay's log is the game's record)";
		}
		return false;
	}
	if (!setUp(game, error))
	{
		return false;
	}
	m_network->setRejoining(m_start.rejoinGeneration);
	m_fetch = std::make_unique<RelayLogFetch>();
	m_catchUp = std::make_unique<CommandLogCatchUp>(m_config.crcInterval);
	game.setFrameDriver(m_catchUp.get());
	m_rejoin = RejoinReport();
	m_rejoin.startedMs = NetMilliseconds();
	m_rejoinState = RejoinState::Binding;
	return true;
}

void NetGameSession::failRejoin(const std::string &why)
{
	if (m_rejoin.failure.empty())
	{
		m_rejoin.failure = why;
	}
	m_rejoinState = RejoinState::Failed;
}

std::string NetGameSession::assembleLog()
{
	RelayLogFile log;
	std::string error;
	if (!RelayLogFile::parse(m_fetch->bytes(), log, &error, true))
	{
		return "the relay's log: " + error;
	}
	if (log.gameId != m_start.onlineGameId)
	{
		return "the relay's log is of another game";
	}
	m_logComplete = log.complete; // lane ONLINE-3: a spectator ends with the log
	auto a = std::make_shared<RelayLog::Assembled>();
	if (!RelayLog::assemble(log, m_config, *a, &error, true))
	{
		return error;
	}
	if (a->crcDisagreements)
	{
		return "the players' CRCs in the log disagree: the game desynced before the rejoin";
	}
	m_assembled = a;
	m_catchUp->setLog(a);
	m_parsedBytes = m_fetch->bytes().size();
	m_rejoin.logBytes = m_parsedBytes;
	return std::string();
}

void NetGameSession::fillFromLog(UnsignedInt fromFrame)
{
	if (!m_assembled)
	{
		return;
	}
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (s == m_start.localSlot || !m_start.networkSlot[(size_t)s])
		{
			continue;
		}
		const auto &frames = m_assembled->latestFrames[(size_t)s];
		for (auto it = frames.lower_bound(fromFrame); it != frames.end(); ++it)
		{
			const unsigned long long before = m_network->framesFilledFromResend();
			m_network->fillFromLog(s, it->first, it->second.commands, it->second.runAheadChange, it->second.rejoins);
			m_rejoin.framesFilled += m_network->framesFilledFromResend() - before;
		}
	}
}

// ---- lane ONLINE-3: a spectator ------------------------------------------------------------------------------------------------------------
bool NetGameSession::beginSpectate(LiveGame &game, std::string *error)
{
	if (!m_start.online || m_start.spectatorIndex < 0)
	{
		if (error)
		{
			*error = "spectating needs an online spectator START";
		}
		return false;
	}
	const LiveGame::Report r = game.report();
	m_config = NetworkConfig();
	m_config.localSlot = -1;
	m_config.runAhead = m_start.runAhead;
	m_config.crcInterval = m_start.crcInterval;
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (!m_start.networkSlot[(size_t)s])
		{
			continue;
		}
		const Player *p = (size_t)s < r.startSlotPlayers.size() ? game.players().findPlayerWithName(r.startSlotPlayers[(size_t)s]) : nullptr;
		if (!p)
		{
			if (error)
			{
				*error = "network slot " + std::to_string(s) + " has no player in the loaded game";
			}
			return false;
		}
		m_config.slotPlayerIndex[(size_t)s] = p->getPlayerIndex();
	}
	Transport::Options o = m_options.transport;
	o.relay.enabled = true;
	o.relay.address = m_start.relay;
	o.relay.ticket = m_start.ticket;
	o.relay.macKey = m_start.macKey; // lane AUTH-1: the spectator's own k (relay->spectator only)
	o.relay.spectatorIndex = m_start.spectatorIndex;
	m_transport = std::make_unique<Transport>(m_socket, 0, o); // no peer: it never sends a transport packet
	m_fetch = std::make_unique<RelayLogFetch>();
	m_catchUp = std::make_unique<CommandLogCatchUp>(m_config.crcInterval);
	RegisterLogicCRCHandler(game.dispatch());
	game.setFrameDriver(m_catchUp.get());
	m_game = &game;
	m_spectate = SpectateReport();
	m_spectate.startedMs = NetMilliseconds();
	m_spectateState = SpectateState::Binding;
	return true;
}

bool NetGameSession::detachSpectator(LiveGame &game, std::string *error)
{
	if (!spectating())
	{
		if (error)
		{
			*error = "the network session is not a spectator session";
		}
		return false;
	}
	if (m_game != &game)
	{
		if (error)
		{
			*error = "the spectator session is attached to another game";
		}
		return false;
	}
	if (spectatorFailed())
	{
		if (error)
		{
			*error = m_spectate.failure.empty() ? "the spectator session has failed" : m_spectate.failure;
		}
		return false;
	}
	game.drainFrames();
	if (m_catchUp && m_catchUp->crcMismatches())
	{
		if (error)
		{
			*error = "the spectator's state differs from the players' CRC: " + m_catchUp->firstMismatch();
		}
		return false;
	}
	game.setFrameDriver(nullptr);
	m_game = nullptr;
	return true;
}

bool NetGameSession::attachSpectator(LiveGame &game, std::string *error)
{
	if (!spectating())
	{
		if (error)
		{
			*error = "the network session is not a spectator session";
		}
		return false;
	}
	if (m_game)
	{
		if (error)
		{
			*error = "the spectator session is already attached to a game";
		}
		return false;
	}
	if (spectatorFailed() || !m_transport || !m_fetch)
	{
		if (error)
		{
			*error = m_spectate.failure.empty() ? "the spectator session cannot be reattached" : m_spectate.failure;
		}
		return false;
	}
	const Player *local = game.players().getLocalPlayer();
	if (!local || !local->isObserver())
	{
		if (error)
		{
			*error = "the replacement game is not an observer game";
		}
		return false;
	}
	if (m_config.localSlot != -1)
	{
		if (error)
		{
			*error = "the spectator session has a local network slot";
		}
		return false;
	}
	const LiveGame::Report r = game.report();
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (!m_start.networkSlot[(size_t)s])
		{
			continue;
		}
		const Player *p = (size_t)s < r.startSlotPlayers.size() ? game.players().findPlayerWithName(r.startSlotPlayers[(size_t)s]) : nullptr;
		if (!p || p->getPlayerIndex() != m_config.slotPlayerIndex[(size_t)s])
		{
			if (error)
			{
				*error = "network slot " + std::to_string(s) + " does not match the original spectator game";
			}
			return false;
		}
	}
	m_catchUp = std::make_unique<CommandLogCatchUp>(m_config.crcInterval);
	m_catchUp->setLog(m_assembled);
	RegisterLogicCRCHandler(game.dispatch());
	game.setFrameDriver(m_catchUp.get());
	m_game = &game;
	const unsigned long long requests = m_spectate.logRequests;
	const std::uint64_t logBytes = m_spectate.logBytes;
	const bool complete = m_spectate.logComplete || m_logComplete;
	const UnsignedInt served = m_assembled && m_assembled->anyComplete ? m_assembled->lastCompleteFrame : 0;
	m_spectate = SpectateReport();
	m_spectate.startedMs = NetMilliseconds();
	m_spectate.reached = game.protocolFrame();
	m_spectate.served = served;
	m_spectate.logRequests = requests;
	m_spectate.logBytes = logBytes;
	m_spectate.logComplete = complete;
	m_spectateState = SpectateState::Following;
	m_spectatorAutoCatchup = false;
	return true;
}

UnsignedInt NetGameSession::spectatorFinalFrame() const
{
	return m_assembled && m_assembled->anyComplete ? std::max<UnsignedInt>(1, m_assembled->lastCompleteFrame + 1) : 1;
}

NetGameSession::SpectateState NetGameSession::spectateStep(LiveGame &game, std::uint32_t budgetMs)
{
	if (m_spectateState == SpectateState::Ended || m_spectateState == SpectateState::Failed || !m_catchUp || !m_transport)
	{
		return m_spectateState;
	}
	auto fail = [&](const std::string &why) {
		if (m_spectate.failure.empty())
		{
			m_spectate.failure = why;
		}
		m_spectateState = SpectateState::Failed;
		return m_spectateState;
	};
	m_transport->service();
	m_transport->foreignDatagrams().clear();
	const std::uint64_t now = NetMilliseconds();
	if (m_transport->relayRefusal() != 0)
	{
		return fail(std::string("the relay refused the spectator ticket: ") + RelayProtocol::refuseReasonName(m_transport->relayRefusal()));
	}
	if (!m_transport->relayBound())
	{
		return now - m_spectate.startedMs > 60000 ? fail("the relay did not welcome the spectator ticket in 60 s") : m_spectateState;
	}
	m_spectateState = SpectateState::Following;
	m_fetch->step(*m_transport, now);
	m_spectate.logRequests = m_fetch->requests();
	if (m_transport->relayLogRefused())
	{
		return fail("the relay keeps no log of this game: it cannot be watched");
	}
	if (m_fetch->bytes().size() > m_parsedBytes)
	{
		const std::string why = assembleLog();
		if (!why.empty())
		{
			return fail(why);
		}
	}
	if (!m_assembled)
	{
		return m_spectateState;
	}
	if (m_spectateStop != 0xFFFFFFFFu)
	{
		m_catchUp->setTarget(m_spectateStop - 1);
	}
	const std::uint64_t t0 = NetMilliseconds();
	while (m_spectatorAutoCatchup && NetMilliseconds() - t0 < budgetMs)
	{
		if (game.advance(0.2) == 0)
		{
			break;
		}
	}
	m_spectate.reached = m_catchUp->reached();
	m_spectate.served = m_assembled->anyComplete ? m_assembled->lastCompleteFrame : 0;
	m_spectate.logBytes = m_parsedBytes;
	m_spectate.crcsCompared = m_catchUp->crcsCompared();
	m_spectate.crcMismatches = m_catchUp->crcMismatches();
	m_spectate.firstMismatch = m_catchUp->firstMismatch();
	m_spectate.firstMismatchFrame = m_catchUp->firstMismatchFrame();
	m_spectate.logComplete = m_logComplete;
	if (m_catchUp->crcMismatches())
	{
		return fail("the spectator's state differs from the players' CRC: " + m_catchUp->firstMismatch());
	}
	const bool atEdge = m_assembled->anyComplete && m_catchUp->reached() > m_assembled->lastCompleteFrame && m_fetch->caughtUp();
	if (atEdge && m_spectate.caughtUpMs == 0)
	{
		m_spectate.caughtUpMs = NetMilliseconds();
	}
	if ((atEdge && m_logComplete) || (m_spectateStop != 0xFFFFFFFFu && m_catchUp->reached() >= m_spectateStop))
	{
		m_spectateState = SpectateState::Ended;
	}
	return m_spectateState;
}

NetGameSession::RejoinState NetGameSession::rejoinStep(LiveGame &game, std::uint32_t budgetMs)
{
	if (m_rejoinState == RejoinState::Live || m_rejoinState == RejoinState::Failed || !m_catchUp)
	{
		return m_rejoinState;
	}
	service();
	m_network->update(game.protocolFrame(), m_empty); // rejoining: the transport, what arrived, and the REJOINREQUEST once ready
	const std::uint64_t now = NetMilliseconds();
	if (m_transport->relayRefusal() != 0)
	{
		failRejoin(std::string("the relay refused the rejoin ticket: ") + RelayProtocol::refuseReasonName(m_transport->relayRefusal()));
		return m_rejoinState;
	}
	if (!m_transport->relayBound())
	{
		if (now - m_rejoin.startedMs > 60000)
		{
			failRejoin("the relay did not welcome the rejoin ticket in 60 s");
		}
		return m_rejoinState;
	}
	if (m_rejoinState == RejoinState::Binding)
	{
		m_rejoinState = RejoinState::CatchingUp;
	}
	m_fetch->step(*m_transport, now);
	m_rejoin.logRequests = m_fetch->requests();
	if (m_transport->relayLogRefused())
	{
		failRejoin("the relay keeps no log of this game: it cannot be rejoined");
		return m_rejoinState;
	}
	if (m_fetch->bytes().size() > m_parsedBytes)
	{
		const std::string why = assembleLog();
		if (!why.empty())
		{
			failRejoin(why);
			return m_rejoinState;
		}
	}
	if (!m_assembled)
	{
		return m_rejoinState;
	}
	const int local = m_start.localSlot;
	for (const Network::MembershipEvent &e : m_assembled->membership)
	{
		if (e.kind == Network::MembershipEvent::DESTROY && e.slot == local)
		{
			failRejoin("the rejoin window ended: the player's army was handed over (frame " + std::to_string(e.frame) + ")");
			return m_rejoinState;
		}
	}
	// the REJOIN of this incarnation (the log's latest of this slot: this client bound it) names the first lockstep frame
	const bool ours = m_assembled->incarnations[(size_t)local] >= 2;
	if (!ours && m_fetch->caughtUp())
	{
		failRejoin("cannot rejoin: the relay log has no REJOINED record for this player");
		return m_rejoinState;
	}
	const UnsignedInt join = ours ? m_assembled->joinFrame[(size_t)local] : 0xFFFFFFFFu;
	m_catchUp->setTarget(join == 0xFFFFFFFFu ? 0xFFFFFFFFu : join - 1);
	// the catch-up: full speed, no rendering, at most budgetMs per step (the host keeps its window alive)
	const std::uint64_t t0 = NetMilliseconds();
	while (NetMilliseconds() - t0 < budgetMs)
	{
		if (game.advance(0.2) == 0)
		{
			break;
		}
	}
	m_rejoin.framesCaughtUp = m_catchUp->reached();
	m_rejoin.crcsCompared = m_catchUp->crcsCompared();
	m_rejoin.crcMismatches = m_catchUp->crcMismatches();
	m_rejoin.firstMismatch = m_catchUp->firstMismatch();
	if (m_catchUp->crcMismatches())
	{
		failRejoin("the catch-up state differs from the players' CRC: " + m_catchUp->firstMismatch());
		return m_rejoinState;
	}
	if (m_rejoinState == RejoinState::CatchingUp && ours && m_fetch->caughtUp() && game.frame() + 10 >= m_assembled->lastCompleteFrame)
	{
		m_network->setRejoinReady(); // near the live edge: ask the others to take this slot back
		m_rejoinState = RejoinState::Waiting;
		m_rejoin.liveMs = now; // reused below: when the wait began
	}
	if (m_rejoinState == RejoinState::Waiting && join == 0xFFFFFFFFu && now - m_rejoin.liveMs > 60000)
	{
		failRejoin("no REJOIN from the packet router within 60 s of the request");
		return m_rejoinState;
	}
	if (join == 0xFFFFFFFFu || game.frame() < join)
	{
		return m_rejoinState;
	}
	// the switch: the state after the REJOIN's frame ran is this client's; it is a member from `join` on
	game.drainFrames();
	Network::LiveStart ls;
	ls.frame = join;
	ls.runAhead = m_assembled->runAheadAfter(join - 1, m_start.runAhead);
	ls.membership = m_assembled->membership;
	for (const auto &kv : m_catchUp->crcs())
	{
		// the CRC of frame C executes on C + the run-ahead the frames before C left (Network::executionFrame); those from `join` on are this client's to send
		const UnsignedInt c = kv.first;
		if (c == 0 || c > join)
		{
			continue;
		}
		const UnsignedInt exec = c + (UnsignedInt)m_assembled->runAheadAfter(c - 1, m_start.runAhead);
		if (exec < join)
		{
			continue;
		}
		Network::LiveStart::Crc crc = kv.second;
		crc.executionFrame = exec;
		crc.half.slot = local;
		ls.crcs.push_back(crc);
	}
	m_rejoin.crcsSentAfter = ls.crcs.size();
	m_network->beginLive(ls);
	fillFromLog(join);
	game.setFrameDriver(m_driver.get());
	m_rejoin.liveFrame = join;
	m_rejoin.liveMs = NetMilliseconds();
	m_rejoinState = RejoinState::Live;
	m_lastTail = m_rejoin.liveMs;
	return m_rejoinState;
}

void NetGameSession::service()
{
	if (!m_transport)
	{
		return;
	}
	writeDesyncDumps();
	m_transport->service();
	if (m_rejoinState == RejoinState::Live && m_fetch && m_driver && m_driver->waitingMs() > 250)
	{
		// lane ONLINE-3: a rejoined client waiting for a frame in its first minute: frame data the others sent before they took it back is in the log
		const std::uint64_t now = NetMilliseconds();
		if (now - m_rejoin.liveMs < 60000 && now - m_lastTail >= 250)
		{
			m_lastTail = now;
			m_fetch->step(*m_transport, now);
			if (m_fetch->bytes().size() > m_parsedBytes)
			{
				const std::string why = assembleLog();
				if (why.empty())
				{
					fillFromLog(0); // Network::fillFromLog takes only frames this client has not run and has not got whole
				}
				else if (m_rejoin.tailErrors.size() < 20)
				{
					m_rejoin.tailErrors.push_back(why);
				}
			}
		}
	}
	auto &foreign = m_transport->foreignDatagrams();
	for (const auto &d : foreign)
	{
		LobbyStart s;
		if (LANLobby::isLobbyDatagram(d.second) && LANLobby::decodeStart(d.second, s, nullptr))
		{
			const std::vector<std::uint8_t> ack = LANLobby::encodeStartAck(m_start.localSlot);
			m_socket.sendTo(d.first, ack.data(), ack.size());
		}
	}
	foreign.clear();
}

bool NetGameSession::pollLoaded()
{
	service();
	m_network->update(0, m_empty);
	return m_network->allLoaded();
}

void NetGameSession::finish(LiveGame &game)
{
	// SMOOTH-1 (review r4): the protocol owner drains the worker first (complete batches only: never a wait for the network), so the leave, the
	// protocol frame and the replay's end record agree with the logic
	game.drainFrames();
	if (m_network && !rejoinUnjoined())
	{
		// lane END-2: the commands the local player gave last (the quit menu's MSG_SELF_DESTRUCT(true) of an Exit, RW 0x625E36, which retail sends in the
		// same message stream as the MSG_CLEAR_GAME_DATA that leaves) go out before the leave: the other peers execute them on the frame before it
		if (!game.commands().messages().empty())
		{
			m_network->update(game.protocolFrame(), game.commands());
			game.commands().reset();
		}
		m_network->leave(game.protocolFrame());
		m_network->update(game.protocolFrame(), m_empty);
	}
	if (m_writer && m_writer->isOpen())
	{
		m_writer->close(game.protocolFrame(), game.logic().computeStateHash());
	}
}

bool NetGameSession::allAcked() const
{
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (m_start.networkSlot[(size_t)s] && s != m_start.localSlot && !m_transport->allAcked(s))
		{
			return false;
		}
	}
	return true;
}

void NetGameSession::writeDesyncDumps()
{
	// lane MP-2 (RW 0x6290C7): one file per desync, written when the local half is in and again when another half arrives
	if (!m_network)
	{
		return;
	}
	const std::vector<DesyncReport> &reports = m_network->desyncs();
	for (size_t i = 0; i < reports.size(); ++i)
	{
		if (i >= m_dumpedHalves.size())
		{
			m_dumpedHalves.push_back(0);
			m_desyncDumps.push_back(DesyncDump::fileName(m_options.desyncDirectory, reports[i].checkedOnFrame, m_options.exeName,
				m_options.playerName.empty() ? "slot" + std::to_string(m_start.localSlot) : m_options.playerName));
		}
		if (m_dumpedHalves[i] == reports[i].halves.size() + 1)
		{
			continue; // written with these halves (the stored count is halves + 1: 0 = not written yet)
		}
		std::string error;
		if (!DesyncDump::write(m_desyncDumps[i], reports[i], m_start.localSlot, m_options.replayPath, &error))
		{
			m_desyncDumpErrors.push_back(error);
		}
		m_dumpedHalves[i] = reports[i].halves.size() + 1;
	}
}
