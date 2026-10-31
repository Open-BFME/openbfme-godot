// OpenBFME. GPL-3.0. See RelayProbe.h.
#include "GameNetwork/RelayProbe.h"

#include "GameNetwork/RelayProtocol.h"

#include <algorithm>
#include <chrono>

RelayProbe::RelayProbe() : m_options(Options())
{
}

RelayProbe::RelayProbe(const Options &options) : m_options(options)
{
}

RelayProbe::~RelayProbe()
{
	stop();
}

bool RelayProbe::start(const std::string &relay, std::string *error)
{
	stop();
	NetAddress address;
	if (!NetAddress::parse(relay, address) || address.port == 0)
	{
		if (error)
		{
			*error = "the relay address \"" + relay.substr(0, 64) + "\" is not a.b.c.d:port";
		}
		return false;
	}
	// the first probe's socket, opened here so a machine that cannot open one is told at once
	auto socket = std::make_unique<UDP>();
	if (!socket->bind(0, 0, error))
	{
		return false;
	}
	m_socket = std::move(socket);
	m_relay = address;
	m_relayText = relay;
	m_result = Result();
	m_hasResult = false;
	m_running = true;
	m_stop = false;
	m_done = false;
	m_receiver = std::thread([this] { measure(); });
	return true;
}

void RelayProbe::stop()
{
	m_stop = true;
	if (m_receiver.joinable())
	{
		m_receiver.join();
	}
	m_running = false;
	m_socket.reset();
}

void RelayProbe::update()
{
	if (m_running && m_done)
	{
		m_receiver.join();
		m_result = m_completed;
		m_hasResult = true;
		m_running = false;
		m_socket.reset();
	}
}

void RelayProbe::measure()
{
	Result result;
	std::vector<int> rtts;
	const auto probe = RelayProtocol::encodeLogRequest(0);
	for (int i = 0; i < m_options.probes && !m_stop; ++i)
	{
		++result.sent;
		// NOT_BOUND has no probe ID: each attempt needs a fresh socket to exclude late answers.
		if (i > 0)
		{
			auto socket = std::make_unique<UDP>();
			std::string error;
			if (!socket->bind(0, 0, &error))
			{
				continue;
			}
			m_socket = std::move(socket);
		}
		const auto sentAt = NetMilliseconds();
		if (!m_socket->sendTo(m_relay, probe.data(), probe.size()))
		{
			continue;
		}
		while (!m_stop)
		{
			NetAddress from;
			std::vector<std::uint8_t> d;
			const bool received = m_socket->receiveFrom(from, d);
			const auto elapsed = NetMilliseconds() - sentAt;
			if (elapsed >= m_options.timeoutMs)
			{
				break;
			}
			if (received && from == m_relay && d.size() == RelayProtocol::kHeaderBytes && RelayProtocol::isRelayDatagram(d) &&
				d[3] == RelayProtocol::NOT_BOUND)
			{
				rtts.push_back((int)std::min<std::uint64_t>(elapsed, 10000));
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	result.answered = (int)rtts.size();
	result.lossPct = result.sent ? (result.sent - result.answered) * 100 / result.sent : 100;
	if (!rtts.empty())
	{
		std::sort(rtts.begin(), rtts.end());
		result.rttMs = std::max(1, rtts[rtts.size() / 2]);
	}
	m_completed = result;
	m_done = true;
}
