// OpenBFME unit tests. GPL-3.0.
// Lane NETLAG-2 (the owner, 2026-10-20: "input lag from the relay server, it's really bad"): the round trip the adaptive run-ahead plans for. On the
// owner's live relay games of 2026-10-18 one late acknowledgement (the other client busy for a moment) raised the command delay from 1 frame to 6 (1.2 s
// from a click to its effect) for 25 seconds: MP-2 planned for the smoothed round trip plus four times its variation, which one sample of seconds blows
// up. Transport::plannedRoundTripMs takes the 80th percentile of the last 32 samples instead. Real Transports on loopback with a driven clock: every
// round trip below is exact.

#include "doctest.h"
#include "OnlineTestWait.h"

#include "GameNetwork/NetImpairment.h"
#include "GameNetwork/Network.h"
#include "GameNetwork/Transport.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace
{
bool serviceUntil(Transport &transport, const std::function<bool()> &ready)
{
	return onlinetest::waitUntil([&] {
		transport.service();
		return ready();
	});
}

struct Link
{
	UDP sa, sb;
	std::uint64_t now = 1000;
	std::unique_ptr<Transport> ta, tb;
	unsigned long long delivered = 0;
	Link()
	{
		std::string error;
		REQUIRE_MESSAGE(sa.bind(0x7F000001u, 0, &error), error);
		REQUIRE_MESSAGE(sb.bind(0x7F000001u, 0, &error), error);
		Transport::Options o;
		o.resendMs = 10000; // no resend while a test waits (Karn's rule: a resent record gives no sample)
		ta = std::make_unique<Transport>(sa, 0, o);
		tb = std::make_unique<Transport>(sb, 1, o);
		ta->setClock([this] { return now; });
		tb->setClock([this] { return now; });
		ta->setPeer(1, sb.localAddress());
		tb->setPeer(0, sa.localAddress());
	}
	// one record from a to b, acknowledged `rttMs` later (b serviced only then, a not at all meanwhile: no resend, so the record gives a sample)
	void sample(std::uint64_t rttMs)
	{
		std::string error;
		REQUIRE(ta->sendTo(1, { (std::uint8_t)(delivered & 0xFF) }, &error));
		ta->service();
		REQUIRE(serviceUntil(*tb, [&] { return tb->stats().commandsDelivered == delivered + 1; }));
		++delivered;
		now += rttMs;
		tb->service(); // the acknowledgement
		REQUIRE(serviceUntil(*ta, [&] { return ta->allAcked(1); }));
		int from = -1;
		std::vector<std::uint8_t> bytes;
		while (tb->receive(from, bytes))
		{
		}
		now += 10;
	}
};
} // namespace

TEST_CASE("netlag2 planned round trip: none before 16 samples; then the 80th percentile of the last 32, so three late acknowledgements among them do not "
		  "move it while the smoothed round trip plus four variations jumps by seconds")
{
	Link l;
	for (int i = 0; i < 15; ++i)
	{
		l.sample(100);
	}
	CHECK(l.ta->plannedRoundTripMs(1) == -1); // tolerance: none; 15 samples are below the 16 needed
	l.sample(100);
	CHECK(l.ta->plannedRoundTripMs(1) == 100);
	for (int i = 0; i < 13; ++i)
	{
		l.sample(100);
	}
	// the other client is busy for 3 s, three times (a hitch, a load): its acknowledgements come late
	l.sample(3000);
	l.sample(100);
	l.sample(2500);
	l.sample(3000);
	CHECK(l.ta->plannedRoundTripMs(1) == 100);
	const int smoothedPlusFour = l.ta->roundTripMs(1) + 4 * l.ta->roundTripVarMs(1);
	CHECK(smoothedPlusFour == 6278); // what MP-2 planned for: run-ahead 17 (the driven clock makes every sample exact)
	MESSAGE("after the late acknowledgements: planned " << l.ta->plannedRoundTripMs(1) << " ms, smoothed + 4 variations " << smoothedPlusFour << " ms");
	CHECK(l.ta->errors().empty());
	CHECK(l.tb->errors().empty());
}

TEST_CASE("netlag2 planned round trip: a link that really became slower moves it once the slower samples are more than a fifth of the window")
{
	Link l;
	for (int i = 0; i < 32; ++i)
	{
		l.sample(80);
	}
	CHECK(l.ta->plannedRoundTripMs(1) == 80);
	for (int i = 0; i < 6; ++i)
	{
		l.sample(300);
	}
	CHECK(l.ta->plannedRoundTripMs(1) == 80); // 6 of 32: the 26th sorted sample is still a fast one
	l.sample(300);
	CHECK(l.ta->plannedRoundTripMs(1) == 300); // 7 of 32
	for (int i = 0; i < 40; ++i)
	{
		l.sample(60);
	}
	CHECK(l.ta->plannedRoundTripMs(1) == 60); // the window holds only the last 32
}

namespace
{
// lane NETLAG-2 (Sol r1): three real Network + Transport peers on loopback with a driven clock and per-link latency; each peer runs a frame every 200 ms
// of the clock, a frame the lockstep held as soon as it is ready (LiveGame's owed frame); returns the clock time 400 frames took and the router's
// run-ahead at the end
struct TopologyRun
{
	std::uint64_t elapsedMs = 0;
	int routerRunAhead = 0;
	size_t errors = 0;
	bool finished = false;
};

TopologyRun runTopology(bool adaptive, const char *routerLink, const char *otherLink)
{
	struct P
	{
		UDP socket;
		std::unique_ptr<Transport> transport;
		std::unique_ptr<Network> net;
		CommandList pending;
		UnsignedInt frame = 0;
		int owed = 0;
	};
	std::uint64_t now = 1000;
	std::array<P, 3> peers;
	std::string error;
	for (P &p : peers)
	{
		REQUIRE_MESSAGE(p.socket.bind(0x7F000001u, 0, &error), error);
	}
	for (int i = 0; i < 3; ++i)
	{
		P &p = peers[(size_t)i];
		Transport::Options o;
		for (int j = 0; j < 3; ++j)
		{
			if (j != i)
			{
				o.impairment.linkSet[(size_t)j] = true;
				REQUIRE_MESSAGE(NetLinkFaults::parse(i == 0 || j == 0 ? routerLink : otherLink, o.impairment.link[(size_t)j], &error), error);
			}
		}
		p.transport = std::make_unique<Transport>(p.socket, i, o);
		p.transport->setClock([&now] { return now; });
		NetworkConfig c;
		c.localSlot = i;
		c.runAhead = adaptive ? 1 : 2;
		c.adaptiveRunAhead = adaptive;
		c.crcInterval = 0;
		c.disconnectPath = false;
		for (int j = 0; j < 3; ++j)
		{
			c.slotPlayerIndex[(size_t)j] = 3 + j;
			if (j != i)
			{
				p.transport->setPeer(j, peers[(size_t)j].socket.localAddress());
			}
		}
		p.net = std::make_unique<Network>(c, *p.transport);
		p.net->setClock([&now] { return now; });
	}
	for (P &p : peers)
	{
		p.net->sendLoadComplete();
	}
	TopologyRun r;
	for (; now < 301000; ++now)
	{
		bool all = true;
		for (P &p : peers)
		{
			p.net->update(p.frame, p.pending);
			if (p.frame >= 400)
			{
				continue;
			}
			all = false;
			const int due = (now % 200 == 0 ? 1 : 0) + p.owed;
			p.owed = 0;
			for (int k = 0; k < due; ++k)
			{
				if (!p.net->allLoaded() || !p.net->isFrameReady(p.frame))
				{
					p.owed = 1;
					break;
				}
				CommandList out;
				p.net->relayCommands(p.frame, out);
				++p.frame;
				p.net->update(p.frame, p.pending);
			}
		}
		if (all)
		{
			r.finished = true;
			break;
		}
		if (now % 50 == 0)
		{
			NetSleepMilliseconds(0); // let loopback deliver
		}
	}
	r.elapsedMs = now - 1000;
	r.routerRunAhead = peers[0].net->currentRunAhead();
	for (P &p : peers)
	{
		r.errors += p.net->errors().size();
	}
	return r;
}
} // namespace

TEST_CASE("netlag2 run-ahead topology (Sol r1): a router 125 ms from two players who are 250 ms from each other plans for that pair: the adaptive "
		  "run-ahead reaches 2 and the game runs at the fixed 2's pace (it kept 1 and ran 20 % slow when only the router's own links counted)")
{
	const TopologyRun fixed2 = runTopology(false, "latency=125", "latency=250");
	const TopologyRun adaptive = runTopology(true, "latency=125", "latency=250");
	REQUIRE(fixed2.finished);
	REQUIRE(adaptive.finished);
	MESSAGE("fixed run-ahead 2: " << fixed2.elapsedMs << " ms; adaptive: " << adaptive.elapsedMs << " ms, router run-ahead " << adaptive.routerRunAhead);
	CHECK(adaptive.routerRunAhead == 2);
	// tolerance: the adaptive game starts at 1 frame and plans only after 16 round-trip samples (about 3 s): its first frames wait for the far pair
	CHECK(adaptive.elapsedMs <= fixed2.elapsedMs + 2000);
	CHECK(adaptive.errors == 0);
	CHECK(fixed2.errors == 0);
}
