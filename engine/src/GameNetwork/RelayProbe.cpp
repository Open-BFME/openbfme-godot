// OpenBFME. GPL-3.0. See RelayProbe.h.
#include "GameNetwork/RelayProbe.h"

#include "GameNetwork/RelayProtocol.h"

#include <algorithm>

RelayProbe::RelayProbe() : m_options(Options())
{
}

RelayProbe::RelayProbe(const Options &options) : m_options(options)
{
}

bool RelayProbe::start(const std::string &relay, std::uint64_t nowMs, std::string *error)
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
	m_rtts.clear();
	m_result = Result();
	m_running = true;
	m_waiting = false;
	sendProbe(nowMs);
	return true;
}

void RelayProbe::stop()
{
	m_running = false;
	m_waiting = false;
	m_socket.reset();
}

void RelayProbe::sendProbe(std::uint64_t nowMs)
{
	// every probe after the first goes from a socket of its own (a fresh port): NOT_BOUND carries nothing to tell probes apart, so a late answer to a
	// probe that already timed out must not reach the socket waiting for the next one
	if (m_result.sent > 0)
	{
		auto socket = std::make_unique<UDP>();
		std::string error;
		if (!socket->bind(0, 0, &error))
		{
			++m_result.sent; // a probe that could not even be sent is lost
			m_sentAt = nowMs - m_options.timeoutMs;
			m_waiting = true;
			return;
		}
		m_socket = std::move(socket);
	}
	const std::vector<std::uint8_t> probe = RelayProtocol::encodeLogRequest(0);
	++m_result.sent;
	m_sentAt = nowMs;
	m_waiting = true;
	if (!m_socket->sendTo(m_relay, probe.data(), probe.size()))
	{
		m_sentAt = nowMs - m_options.timeoutMs; // a send that failed is a lost probe (it times out now)
	}
}

void RelayProbe::update(std::uint64_t nowMs)
{
	if (!m_running || !m_socket)
	{
		return;
	}
	NetAddress from;
	std::vector<std::uint8_t> d;
	while (m_waiting && m_socket->receiveFrom(from, d))
	{
		// only the relay's own NOT_BOUND answers a probe (anything else, or from anyone else, is ignored)
		if (from == m_relay && d.size() == RelayProtocol::kHeaderBytes && RelayProtocol::isRelayDatagram(d) && d[3] == RelayProtocol::NOT_BOUND)
		{
			m_rtts.push_back((int)std::min<std::uint64_t>(nowMs - m_sentAt, 10000));
			m_waiting = false;
		}
	}
	if (m_waiting && nowMs - m_sentAt >= m_options.timeoutMs)
	{
		m_waiting = false; // lost
	}
	if (!m_waiting)
	{
		if (m_result.sent >= m_options.probes)
		{
			finish();
		}
		else
		{
			sendProbe(nowMs);
		}
	}
}

void RelayProbe::finish()
{
	m_result.answered = (int)m_rtts.size();
	m_result.lossPct = m_result.sent ? (m_result.sent - m_result.answered) * 100 / m_result.sent : 100;
	if (!m_rtts.empty())
	{
		std::vector<int> sorted = m_rtts;
		std::sort(sorted.begin(), sorted.end());
		m_result.rttMs = std::max(1, sorted[sorted.size() / 2]);
	}
	m_hasResult = true;
	stop();
}
