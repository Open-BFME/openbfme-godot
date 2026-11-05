// OpenBFME unit tests. GPL-3.0.
// Lane MPDROP-3 (found by WOTR2's stalled-client LAN test, stop S-1341): a player whose game hangs is dropped by the others, but the drop's
// DISCONNECTPLAYER is the last thing the decider sends it: disconnectPlayer retires the link at once (Transport::retire drops the unacknowledged records)
// and later datagrams from the dropped player count as an unknown sender's. A hung game's socket can lose that one datagram (its receive buffer fills
// while nothing reads it), so the player never learns it was dropped: it drops the others in turn and plays on alone (a split game). Three real Network +
// Transport peers on loopback with a driven clock; while slot 2 hangs, every datagram sent to it is lost (its socket is drained and discarded: the worst
// case of a full buffer). Once it runs again it must learn at once that it was dropped, run no frame from the drop's frame on, and drop nobody: after a
// hang that ends soon after the drop (the decider still resends the drop) and after one far longer than NetworkPlayerTimeoutTime (the decider answers the
// dropped player's traffic with its drop).

#include "doctest.h"

#include "GameNetwork/Network.h"
#include "GameNetwork/Transport.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr std::uint32_t kDisconnectMs = 1000; // NetworkDisconnectTime of these games (retail: 15000)
constexpr std::uint32_t kTimeoutMs = 6000;    // NetworkPlayerTimeoutTime (retail: 60000)

struct HangRun
{
	bool dropped = false;            ///< the survivors dropped slot 2
	UnsignedInt dropFrame = 0;       ///< the frame they dropped it at
	std::uint64_t droppedAtMs = 0;   ///< the clock when both had dropped it
	bool quit = false;               ///< slot 2's network asked it to leave after the hang
	std::string quitReason;
	std::uint64_t quitAfterMs = 0;   ///< clock ms from the end of the hang to its quit
	UnsignedInt hungFrame = 0;       ///< the frames slot 2 had run when it hung
	UnsignedInt lastFrame = 0;       ///< the frames it had run when it quit (or at the end): its next frame
	bool forked = false;             ///< slot 2 dropped a survivor (a game of its own)
	size_t backlogRecords = 0, backlogBytes = 0; ///< the decider's backlog for slot 2 right after the drop (Transport::droppedBacklog)
	std::string log;
};

// `hangAfterDropMs`: how long slot 2 stays hung after the survivors dropped it; `survivorsLeaveMs`: the survivors stop that long after the drop (they
// played on and left: nobody answers the dropped player any more); `onlyDeciderLeaves`: then only slot 0, the drop's decider, stops (slot 1 plays on)
// `pauseMs` (Sol r1 of wotr2-r1): no drop expected: slot 2 hangs that long from its hang on (`pauseAll`: every player's game hangs with it)
// `delayToSurvivorMs` (Sol r2 of wotr2-r2): slot 2's last datagrams to slot 1 arrive that much late, so slot 1's timeout bar runs behind the decider's
// and it applies the decider's drop before it votes; `retail`: RotWK's NetworkDisconnectTime 15000 and NetworkPlayerTimeoutTime 60000
HangRun runHang(std::uint64_t hangAfterDropMs, std::uint64_t survivorsLeaveMs = ~0ull, bool onlyDeciderLeaves = false, bool pauseAll = false,
	std::uint64_t pauseMs = 0, std::uint64_t delayToSurvivorMs = 0, bool retail = false)
{
	const std::uint32_t disconnectMs = retail ? 15000 : kDisconnectMs, timeoutMs = retail ? 60000 : kTimeoutMs;
	struct P
	{
		UDP socket;
		std::unique_ptr<Transport> transport;
		std::unique_ptr<Network> net;
		CommandList pending;
		UnsignedInt frame = 0;
		int owed = 0;
		bool hung = false;
		bool stopped = false;
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
		p.transport = std::make_unique<Transport>(p.socket, i, o);
		p.transport->setClock([&now] { return now; });
		NetworkConfig c;
		c.localSlot = i;
		c.runAhead = 2;
		c.crcInterval = 0;
		c.settings.disconnectTime = disconnectMs;
		c.settings.playerTimeoutTime = timeoutMs;
		for (int j = 0; j < 3; ++j)
		{
			c.slotPlayerIndex[(size_t)j] = 10 + j;
			c.slotNames[(size_t)j] = u"Player" + std::u16string(1, (char16_t)(u'0' + j));
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
	HangRun r;
	std::uint64_t resumeAt = 0, resumedAt = 0;
	const std::uint64_t end = 1000 + (retail ? 700000 : 200000);
	std::uint64_t delayedAt = 0;
	std::vector<std::vector<std::uint8_t>> delayed;
	for (; now < end; ++now)
	{
		if (!peers[2].hung && resumedAt == 0 && peers[2].frame >= 30)
		{
			peers[2].hung = true; // the process hangs: nothing runs, nothing is read
			r.hungFrame = peers[2].frame;
			if (delayToSurvivorMs != 0)
			{
				// slot 2's datagrams slot 1 has not read yet are held back (the others' go on to it at once)
				NetSleepMilliseconds(20); // a datagram just sent need not have arrived yet (macOS)
				NetAddress from;
				std::vector<std::uint8_t> bytes;
				std::vector<std::vector<std::uint8_t>> others;
				while (peers[1].socket.receiveFrom(from, bytes))
				{
					(from == peers[2].socket.localAddress() ? delayed : others).push_back(bytes);
				}
				for (const std::vector<std::uint8_t> &b : others)
				{
					peers[0].socket.sendTo(peers[1].socket.localAddress(), b.data(), b.size());
				}
				REQUIRE_FALSE(delayed.empty());
				delayedAt = now + delayToSurvivorMs;
			}
			resumeAt = pauseMs ? now + pauseMs : 0;
		}
		if (delayedAt != 0 && now == delayedAt)
		{
			for (const std::vector<std::uint8_t> &b : delayed)
			{
				peers[2].socket.sendTo(peers[1].socket.localAddress(), b.data(), b.size());
			}
		}
		if (peers[2].hung)
		{
			// every datagram to the hung game is lost (a full receive buffer)
			NetAddress from;
			std::vector<std::uint8_t> bytes;
			while (peers[2].socket.receiveFrom(from, bytes))
			{
			}
			if (!r.dropped && peers[0].net->isDisconnected(2) && peers[1].net->isDisconnected(2))
			{
				r.dropped = true;
				r.dropFrame = peers[0].net->departureFrame(2);
				for (int s = 0; s < 2; ++s)
				{
					size_t b = 0;
					const size_t n = peers[(size_t)s].transport->droppedBacklog(2, &b);
					r.backlogRecords = std::max(r.backlogRecords, n);
					r.backlogBytes = std::max(r.backlogBytes, b);
				}
				r.droppedAtMs = now;
				resumeAt = pauseMs ? resumeAt : now + hangAfterDropMs;
			}
			if (r.dropped && now - r.droppedAtMs >= survivorsLeaveMs)
			{
				peers[0].stopped = true;
				peers[1].stopped = peers[1].stopped || !onlyDeciderLeaves;
			}
			if (resumeAt != 0 && now >= resumeAt)
			{
				peers[2].hung = false;
				resumedAt = now;
			}
		}
		for (int i = 0; i < 3; ++i)
		{
			P &p = peers[(size_t)i];
			if (p.hung || p.stopped || (pauseAll && peers[2].hung))
			{
				continue;
			}
			p.net->update(p.frame, p.pending);
			if (i == 2 && resumedAt != 0)
			{
				r.forked = r.forked || p.net->isDisconnected(0) || p.net->isDisconnected(1);
				if (p.net->quitRequested())
				{
					// the game leaves (StrategicSession / LockstepDriver run no frame once the network asks)
					r.quit = true;
					r.quitReason = p.net->quitReason();
					r.quitAfterMs = now - resumedAt;
					p.stopped = true;
					continue;
				}
			}
			const int due = (now % 200 == 0 ? 1 : 0) + p.owed;
			p.owed = 0;
			for (int k = 0; k < due; ++k)
			{
				if (!p.net->allLoaded() || !p.net->isFrameReady(p.frame) || !p.net->allowedToContinue(p.frame))
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
		if (r.quit && now > resumedAt + 3000)
		{
			break; // the survivors ran on a while
		}
		if (now % 50 == 0)
		{
			NetSleepMilliseconds(0); // let loopback deliver
		}
	}
	r.lastFrame = peers[2].frame;
	for (int s = 0; s < 3; ++s)
	{
		for (const std::string &l : peers[(size_t)s].net->disconnectManager().log())
		{
			r.log += "peer " + std::to_string(s) + ": " + l + "\n";
		}
		for (const std::string &l : peers[(size_t)s].net->errors())
		{
			r.log += "peer " + std::to_string(s) + " error: " + l + "\n";
		}
		r.log += "peer " + std::to_string(s) + " at frame " + std::to_string(peers[(size_t)s].frame) + "\n";
	}
	return r;
}
} // namespace

TEST_CASE("mpdrop3: a hung player whose drop was lost learns it was dropped as soon as it runs again (a hang just past the drop, and one far longer "
		  "than NetworkPlayerTimeoutTime): it runs no frame from the drop's frame on and drops nobody")
{
	for (const std::uint64_t hangAfterDropMs : { (std::uint64_t)2000, (std::uint64_t)(3 * kTimeoutMs + 20000) })
	{
		CAPTURE(hangAfterDropMs);
		const HangRun r = runHang(hangAfterDropMs);
		INFO(r.log);
		REQUIRE(r.dropped);
		MESSAGE("the dropped player's backlog at the drop: " << r.backlogRecords << " records, " << r.backlogBytes << " bytes (the larger of the two survivors')");
		CHECK(r.backlogRecords > 0u); // tolerance: its DISCONNECTPLAYER at least (the count is the hang's traffic, logged above)
		CHECK(r.dropFrame > r.hungFrame); // tolerance: the survivors ran the frames the hung player had sent before they dropped it
		CHECK(r.quit);
		CHECK(r.quitReason.find("dropped by the other players") == 0);
		CHECK(r.quitAfterMs < 3000); // tolerance: at once on its clock (the decider resends or answers within its resend time, about a second)
		CHECK(r.lastFrame <= r.dropFrame); // tolerance: `frame` is the next frame to run: frames before the drop are the survivors' too; none from the drop's on
		CHECK_FALSE(r.forked);
	}
}

// WOTR2 (the combined LAN matrix's 120 s stall, S-1127): the survivors finished the war and left long before the hung player runs again, so nobody
// answers its traffic with its drop (MPDROP-3's answer) and everything sent to it while it hung is lost. It must not drop them and play on alone: after
// a hang longer than NetworkDisconnectTime + NetworkPlayerTimeoutTime it drops nobody until a player is heard again, and with none heard for that span
// it leaves as a dropped player
TEST_CASE("mpdrop3 (WOTR2 stall): a hung player whose drop was lost while the others played on and left leaves as dropped after its own hang: it drops "
		  "nobody and runs no frame from the drop's frame on")
{
	const HangRun r = runHang(3 * kTimeoutMs + 20000, 2000);
	INFO(r.log);
	REQUIRE(r.dropped);
	CHECK(r.quit);
	CHECK(r.quitReason.find("dropped by the other players: none was heard") == 0);
	CHECK(r.quitAfterMs == (std::uint64_t)(kDisconnectMs + kTimeoutMs)); // the span the others wait before they drop a silent player (an update a ms)
	CHECK(r.lastFrame <= r.dropFrame); // tolerance: as above: none from the drop's frame on
	CHECK_FALSE(r.forked);
}

// WOTR2 (Sol r1 of wotr2-r1): three players; the decider of the hung player's drop leaves while the other survivor plays on. That survivor answers the
// returning player with what it kept for it (MPDROP-3), its vote to drop it among that, but not the decider's DISCONNECTPLAYER: before, the returning
// player waited for ever at the drop's frame. Every player it hears voted it out (RotWK's voted-out test, RW 0x8D826F, on itself): it leaves as dropped
TEST_CASE("mpdrop3 (WOTR2, three players): the decider of a hung player's lost drop leaves while another survivor plays on: the returning player, voted "
		  "out by every player it hears, leaves as dropped; it drops nobody and runs no frame from the drop's frame on")
{
	const HangRun r = runHang(3 * kTimeoutMs + 20000, 2000, true);
	INFO(r.log);
	REQUIRE(r.dropped);
	CHECK(r.quit);
	CHECK(r.quitReason.find("dropped by the other players: every player heard") == 0);
	CHECK(r.quitAfterMs == (std::uint64_t)(kDisconnectMs + kTimeoutMs)); // the span the others wait before they drop a silent player (an update a ms)
	CHECK(r.lastFrame <= r.dropFrame); // tolerance: as above: none from the drop's frame on
	CHECK_FALSE(r.forked);
}

// Sol r1 of wotr2-r1: the own-hang rule drops nobody where nobody was dropped: one player suspended a moment, or every player suspended together far
// past NetworkDisconnectTime + NetworkPlayerTimeoutTime (a host that slept with all three games on it): all play on in step
TEST_CASE("mpdrop3 (WOTR2): a short hang of one player and a long hang of every player together drop nobody and quit nobody")
{
	for (const bool all : { false, true })
	{
		CAPTURE(all);
		const HangRun r = runHang(0, ~0ull, false, all, all ? 45000 : 3000);
		INFO(r.log);
		CHECK_FALSE(r.dropped);
		CHECK_FALSE(r.quit);
		CHECK_FALSE(r.forked);
		CHECK(r.lastFrame > r.hungFrame + 100); // tolerance: the game ran on after the hang (about 5 frames a second for the rest of the run)
	}
}

// WOTR2 (Sol r2 of wotr2-r2): as above, but slot 2's last datagrams reach slot 1 a little late (200 ms; with RotWK's timings 1 s), so slot 1's bar
// runs behind the decider's: it applies the decider's drop before its own timeout votes, and keeps no vote for slot 2 either. Before, the returning
// player heard slot 1's kept backlog, cleared its guard and waited for ever at the drop's frame. No frame came from the players it heard, and nobody is
// heard any more once that backlog is acknowledged: it leaves as dropped
TEST_CASE("mpdrop3 (WOTR2, three players, late link): the survivor that plays on applied the drop without voting: the returning player gets no frame "
		  "from it and leaves as dropped (the test timings and RotWK's)")
{
	for (const bool retail : { false, true })
	{
		CAPTURE(retail);
		const HangRun r = runHang(retail ? 180000 : 3 * kTimeoutMs + 20000, 2000, true, false, 0, retail ? 1000 : 200, retail);
		INFO(r.log);
		REQUIRE(r.dropped);
		CHECK(r.quit);
		CHECK(r.quitReason.find("dropped by the other players: no frame came from the players heard") == 0);
		CHECK(r.lastFrame <= r.dropFrame); // tolerance: as above: none from the drop's frame on
		CHECK_FALSE(r.forked);
	}
}

namespace
{
struct DropPair
{
	UDP sa, sb;
	std::uint64_t now = 1000;
	std::unique_ptr<Transport> a; ///< the peer that drops slot 1
	std::unique_ptr<Transport> c; ///< slot 1's transport on its own socket
	DropPair()
	{
		std::string error;
		REQUIRE_MESSAGE(sa.bind(0x7F000001u, 0, &error), error);
		REQUIRE_MESSAGE(sb.bind(0x7F000001u, 0, &error), error);
		a = std::make_unique<Transport>(sa, 0, Transport::Options());
		c = std::make_unique<Transport>(sb, 1, Transport::Options());
		a->setClock([this] { return now; });
		c->setClock([this] { return now; });
		a->setPeer(1, sb.localAddress());
		c->setPeer(0, sa.localAddress());
	}
	// `records` commands of `size` bytes to slot 1, which reads none of them
	void backlog(int records, size_t size)
	{
		std::string error;
		for (int i = 0; i < records; ++i)
		{
			REQUIRE_MESSAGE(a->sendTo(1, std::vector<std::uint8_t>(size, (std::uint8_t)i), &error), error);
		}
		a->service();
		drainB();
	}
	void drainB()
	{
		NetAddress from;
		std::vector<std::uint8_t> bytes;
		while (sb.receiveFrom(from, bytes))
		{
		}
	}
};
} // namespace

// lane MPDROP-3 (the coordinator's bound): a dropped peer's cost is bounded. Its backlog is what it had not acknowledged at the drop, kept up to
// Transport::kDroppedBacklogRecords / kDroppedBacklogBytes (beyond: retired as before, by name); a dropped peer that keeps sending for ever (a buggy or
// hostile game that never acknowledges) gets resend rounds at most once a second of at most kDroppedRoundBytes each, and its packets are not stored
TEST_CASE("mpdrop3 bound: a backlog beyond the cap is not kept (an error by name); a dropped peer that sends for ever and acknowledges nothing costs at "
		  "most one round of 32 KB a second and no memory")
{
	{
		DropPair d;
		d.backlog((int)Transport::kDroppedBacklogRecords + 1, 16);
		d.a->retireDropped(1, kTimeoutMs);
		CHECK(d.a->droppedBacklog(1) == 0u);
		d.a->service(); // the retire's last ack: forgotten
		CHECK_FALSE(d.a->isPeer(1));
		bool named = false;
		for (const std::string &e : d.a->errors())
		{
			named = named || e.find("is beyond the cap") != std::string::npos;
		}
		CHECK(named);
	}
	{
		DropPair d;
		d.backlog(2000, 40); // 2000 records of 52 bytes on the wire: 104000 bytes, within the cap and more than three rounds
		d.a->retireDropped(1, kTimeoutMs);
		size_t bytes = 0;
		CHECK(d.a->droppedBacklog(1, &bytes) == 2000u);
		CHECK(bytes == 2000u * 52u);
		const unsigned long long sentBefore = d.a->stats().bytesSent;
		const std::uint64_t start = d.now;
		for (; d.now < start + 60000; ++d.now)
		{
			d.drainB();    // slot 1 reads nothing (it never acknowledges) ...
			d.c->service(); // ... and keeps sending (its keep-alives, every 100 ms)
			d.a->service();
			if (d.now % 50 == 0)
			{
				NetSleepMilliseconds(0); // let loopback deliver
			}
		}
		const unsigned long long sent = d.a->stats().bytesSent - sentBefore;
		CHECK(d.a->stats().droppedPeerPackets > 500u); // tolerance: it was heard all along (a keep-alive every 100 ms; loopback timing decides the count)
		CHECK(sent <= 61ull * (Transport::kDroppedRoundBytes + Transport::Options().packetBytes)); // tolerance: the bound: one round a second
		CHECK(sent >= 104000ull); // tolerance: the whole backlog went at least once
		CHECK(d.a->droppedBacklog(1) == 2000u); // nothing it sent was kept; nothing it never acknowledged was dropped
	}
}
