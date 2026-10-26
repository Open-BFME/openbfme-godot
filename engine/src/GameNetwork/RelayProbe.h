// OpenBFME. GPL-3.0.
//
// RelayProbe (lane ONLINEMENU-1): a player's link to the online relay (services/online/relay), measured before the game starts for the staging room's
// Connections screen. RotWK's screen (Connections.apt, AptConnectionScreen: BFME2 0x005DB6E2, RedrawGrid 0x005DB647, OnRetryConnections 0x005DB36A) drew
// the GameSpy NAT negotiation between every two players; OpenBFME's players reach each other only through the relay (RelayProtocol.h), so each player's
// link to the relay is what there is to measure.
//
// A probe is one LOG_REQUEST from a socket the relay does not know; the relay answers an unknown sender NOT_BOUND (RelayCore: 4 bytes for 8, no
// amplification, no state). The probes go one at a time: the next one leaves only after the previous one's answer or its timeout, each from a socket
// of its own (a fresh port), so a late answer is never matched to the wrong probe. Result: the median round trip of the answered
// probes and the share that got no answer; no answer at all is a failed link (rttMs -1, loss 100).
//
// Not simulation code (sim_policy.json): display data for the menus.
// A receive worker timestamps with NetMilliseconds (monotonic); update only publishes the completed batch to the menu thread.
#pragma once

#include "GameNetwork/Transport.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

class RelayProbe
{
public:
	struct Options
	{
		int probes = 5;              ///< probes per measurement
		std::uint32_t timeoutMs = 1000; ///< a probe with no answer by then is lost
	};
	struct Result
	{
		int rttMs = -1;  ///< median round trip of the answered probes; -1: none answered
		int lossPct = 100;
		int sent = 0, answered = 0;
	};

	RelayProbe();
	explicit RelayProbe(const Options &options);
	~RelayProbe();
	// starts a measurement of `relay` ("a.b.c.d:port"); false + *error when the address does not parse or no socket can be opened (nothing runs then)
	bool start(const std::string &relay, std::string *error);
	void update();
	bool running() const { return m_running; }
	bool hasResult() const { return m_hasResult; }
	const Result &result() const { return m_result; }
	const std::string &relay() const { return m_relayText; }
	void stop();

private:
	void measure();

	Options m_options;
	std::unique_ptr<UDP> m_socket;
	NetAddress m_relay;
	std::string m_relayText;
	bool m_running = false, m_hasResult = false;
	std::thread m_receiver;
	std::atomic<bool> m_stop{ false }, m_done{ false };
	Result m_completed;
	Result m_result;
};
