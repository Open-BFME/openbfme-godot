// OpenBFME unit tests. GPL-3.0.
// Lane ONLINEMENU-1: RelayProbe (GameNetwork/RelayProbe.h), a player's link to the online relay for the staging room's Connections screen, against a
// stand-in relay on localhost that answers as RelayCore answers an unknown sender (NOT_BOUND) and can drop, delay or garble its answers.
#include "doctest.h"

#include "GameNetwork/RelayProbe.h"
#include "GameNetwork/RelayProtocol.h"
#include "GameNetwork/Transport.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace
{
// a relay stand-in: answers each datagram by `policy(index)`: 0 drop, 1 NOT_BOUND, 2 garbage from the relay's address, 3 a NOT_BOUND from another socket
struct FakeRelay
{
	UDP socket, other;
	NetAddress address;
	std::atomic<bool> stop{ false };
	std::atomic<int> received{ 0 };
	std::function<int(int)> policy;
	int delayMs = 0;
	std::thread thread;
	explicit FakeRelay(std::function<int(int)> p, int delay = 0) : policy(std::move(p)), delayMs(delay)
	{
		std::string error;
		REQUIRE_MESSAGE(socket.bind(0x7F000001u, 0, &error), error);
		REQUIRE_MESSAGE(other.bind(0x7F000001u, 0, &error), error);
		address = socket.localAddress();
		address.ip = 0x7F000001u;
		thread = std::thread([this] {
			NetAddress from;
			std::vector<std::uint8_t> d;
			while (!stop)
			{
				if (!socket.receiveFrom(from, d))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				const int i = received++;
				// RelayCore answers an unknown sender's LOG_REQUEST NOT_BOUND (services/online/relay/RelayCore.cpp, logRequest): only those count here
				if (d != RelayProtocol::encodeLogRequest(0))
				{
					continue;
				}
				if (delayMs)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
				}
				const std::vector<std::uint8_t> notBound = RelayProtocol::encodeEmpty(RelayProtocol::NOT_BOUND);
				const std::vector<std::uint8_t> junk = { 'B', 'R', RelayProtocol::kVersion, RelayProtocol::REFUSED, 1 };
				switch (policy(i))
				{
				case 1: socket.sendTo(from, notBound.data(), notBound.size()); break;
				case 2: socket.sendTo(from, junk.data(), junk.size()); break;
				case 3: other.sendTo(from, notBound.data(), notBound.size()); break;
				default: break;
				}
			}
		});
	}
	~FakeRelay()
	{
		stop = true;
		thread.join();
	}
};

RelayProbe::Result measure(const NetAddress &relay, int probes, std::uint32_t timeoutMs)
{
	RelayProbe::Options o;
	o.probes = probes;
	o.timeoutMs = timeoutMs;
	RelayProbe p(o);
	std::string error;
	REQUIRE_MESSAGE(p.start(relay.text(), &error), error);
	const std::uint64_t until = NetMilliseconds() + (std::uint64_t)(probes + 2) * timeoutMs + 2000;
	while (p.running() && NetMilliseconds() < until)
	{
		p.update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE_FALSE(p.running());
	REQUIRE(p.hasResult());
	return p.result();
}
} // namespace

TEST_CASE("onlinemenu1: RelayProbe measures a link to the relay: every answer, a lost half, none, and answers that are not the relay's own NOT_BOUND")
{
	SUBCASE("every probe answered: the median round trip, nothing lost")
	{
		FakeRelay relay([](int) { return 1; }, 20);
		const RelayProbe::Result r = measure(relay.address, 5, 500);
		CHECK(r.sent == 5);
		CHECK(r.answered == 5);
		CHECK(r.lossPct == 0);
		CHECK(r.rttMs >= 20); // tolerance: the stand-in waits 20 ms before each answer; the loopback adds scheduling time on a loaded host
		CHECK(r.rttMs < 500); // tolerance: below the probe timeout (an answer after it would be a lost probe)
		CHECK(relay.received == 5);
	}
	SUBCASE("probes 2 and 4 of 5 dropped: 40% lost, the round trip of the three answered")
	{
		FakeRelay relay([](int i) { return i == 1 || i == 3 ? 0 : 1; });
		const RelayProbe::Result r = measure(relay.address, 5, 200);
		CHECK(r.sent == 5);
		CHECK(r.answered == 3);
		CHECK(r.lossPct == 40);
		CHECK(r.rttMs >= 1); // tolerance: a loopback answer takes under a millisecond; the probe reports at least 1
	}
	SUBCASE("no answer at all: a failed link")
	{
		FakeRelay relay([](int) { return 0; });
		const RelayProbe::Result r = measure(relay.address, 3, 150);
		CHECK(r.sent == 3);
		CHECK(r.answered == 0);
		CHECK(r.lossPct == 100);
		CHECK(r.rttMs == -1);
	}
	SUBCASE("a REFUSED from the relay and a NOT_BOUND from another address are no answers")
	{
		FakeRelay relay([](int i) { return i % 2 == 0 ? 2 : 3; });
		const RelayProbe::Result r = measure(relay.address, 4, 150);
		CHECK(r.answered == 0);
		CHECK(r.lossPct == 100);
		CHECK(r.rttMs == -1);
	}
	SUBCASE("a late answer is not counted for the next probe")
	{
		// every answer comes 250 ms after its probe, past the 150 ms timeout: each probe is lost, and the late answers (drained before the next send)
		// never make a later probe look answered
		FakeRelay relay([](int) { return 1; }, 250);
		const RelayProbe::Result r = measure(relay.address, 3, 150);
		CHECK(r.answered == 0);
		CHECK(r.lossPct == 100);
	}
}

TEST_CASE("onlinemenu1: RelayProbe refuses an address it cannot use, by name, and starts nothing")
{
	RelayProbe p;
	std::string error;
	for (const char *bad : { "", "relay.openbfme.com:7350", "1.2.3.4", "1.2.3.4:0", "300.1.1.1:7350", "1.2.3.4:99999" })
	{
		error.clear();
		CHECK_FALSE_MESSAGE(p.start(bad, &error), bad);
		CHECK_MESSAGE(error.find("is not a.b.c.d:port") != std::string::npos, bad << ": " << error);
		CHECK_FALSE(p.running());
	}
}

TEST_CASE("onlinemenu1: RelayProbe timestamps replies without waiting for a frame")
{
	FakeRelay relay([](int) { return 1; }, 20);
	RelayProbe::Options options;
	options.probes = 3;
	options.timeoutMs = 200;
	RelayProbe probe(options);
	std::string error;
	REQUIRE_MESSAGE(probe.start(relay.address.text(), &error), error);
	// No update for longer than the whole batch's timeouts: arrivals, not frames, decide RTT and loss.
	std::this_thread::sleep_for(std::chrono::milliseconds(800));
	probe.update();
	REQUIRE_FALSE(probe.running());
	REQUIRE(probe.hasResult());
	CHECK(probe.result().sent == 3);
	CHECK(probe.result().answered == 3);
	CHECK(probe.result().lossPct == 0);
	CHECK(probe.result().rttMs >= 20); // tolerance: the stand-in delays each response by 20 ms
	CHECK(probe.result().rttMs < 200); // tolerance: RTT must stay below the timeout, not include the 800 ms frame stall
}

TEST_CASE("onlinemenu1: RelayProbe stops and restarts a receiver without leaking old results")
{
	FakeRelay silent([](int) { return 0; });
	FakeRelay answering([](int) { return 1; });
	RelayProbe probe;
	std::string error;
	REQUIRE(probe.start(silent.address.text(), &error));
	probe.stop();
	CHECK_FALSE(probe.running());
	CHECK_FALSE(probe.hasResult());
	REQUIRE(probe.start(answering.address.text(), &error));
	const auto until = NetMilliseconds() + 2000;
	while (probe.running() && NetMilliseconds() < until)
	{
		probe.update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE(probe.hasResult());
	CHECK(probe.result().answered == 5);
	CHECK(probe.result().lossPct == 0);
	REQUIRE(probe.start(silent.address.text(), &error));
	CHECK_FALSE(probe.hasResult());
	// Destruction joins a pending receive rather than leaving a thread using freed state.
}
