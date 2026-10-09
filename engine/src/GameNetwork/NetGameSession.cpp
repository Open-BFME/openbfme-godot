// OpenBFME. GPL-3.0.
// See GameNetwork/NetGameSession.h.

#include "GameNetwork/NetGameSession.h"

#include "Common/Player.h"
#include "GameNetwork/DesyncDump.h"

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
	const LiveGame::Report r = game.report();
	m_config.localSlot = m_start.localSlot;
	m_config.runAhead = m_start.runAhead;
	m_config.crcInterval = m_start.crcInterval;
	m_config.settings = m_options.network; // lane MP-2
	m_config.adaptiveRunAhead = m_options.adaptiveRunAhead; // the lobby's run-ahead is the minimum
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
	m_transport = std::make_unique<Transport>(m_socket, m_start.localSlot, m_options.transport);
	for (int s = 0; s < MAX_SLOTS; ++s)
	{
		if (m_start.networkSlot[(size_t)s] && s != m_start.localSlot)
		{
			m_transport->setPeer(s, m_start.addresses[(size_t)s]);
		}
	}
	m_network = std::make_unique<Network>(m_config, *m_transport);
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
	RegisterLogicCRCHandler(game.dispatch());
	game.setFrameDriver(m_driver.get());
	m_game = &game;
	m_network->sendLoadComplete();
	return true;
}

void NetGameSession::service()
{
	if (!m_transport)
	{
		return;
	}
	writeDesyncDumps();
	m_transport->service();
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
	if (m_network)
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
