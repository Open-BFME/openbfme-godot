// OpenBFME unit tests. GPL-3.0.
// Lane MP-2 (review): what a peer may decide. The execution frame of a command issued on protocol frame L uses the run-ahead the frames before L agreed on,
// so two peers whose logic workers acquired different numbers of batches stamp the CRC of a frame on one execution frame; only the frame's packet router
// changes the run-ahead, within the configured bounds; a FRAMERESEND carries the owner's commands only (a MSG_SELF_DESTRUCT of another player stands in
// the packet router's frame alone); a dropped player's late commands for its drop frame are discarded on every survivor.

#include "doctest.h"

#include "GameNetwork/LockstepDriver.h"
#include "GameNetwork/Network.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace
{
// a transport fed by the test: what is sent is decoded (the CRC's execution frame is kept), what is put is received
struct FedTransport : NetTransport
{
	std::deque<std::pair<int, std::vector<std::uint8_t>>> inbox;
	UnsignedInt crcExecution = 0;
	bool sendTo(int, const std::vector<std::uint8_t> &bytes, std::string *) override
	{
		NetCommandMsg c;
		if (NetPacket::decodeCommand(bytes, c, nullptr) && c.message && c.message->getType() == MSG_LOGIC_CRC)
		{
			crcExecution = c.executionFrame;
		}
		return true;
	}
	bool receive(int &from, std::vector<std::uint8_t> &bytes) override
	{
		if (inbox.empty())
		{
			return false;
		}
		from = inbox.front().first;
		bytes = inbox.front().second;
		inbox.pop_front();
		return true;
	}
	void put(const NetCommandMsg &c) { inbox.push_back({ c.slot, NetPacket::encodeCommand(c, nullptr) }); }
};

NetworkConfig config3(int local)
{
	NetworkConfig c;
	c.localSlot = local;
	for (int i = 0; i < 3; ++i)
	{
		c.slotPlayerIndex[(size_t)i] = i;
	}
	return c;
}

NetCommandMsg frameInfo(int slot, UnsignedInt frame, int count)
{
	NetCommandMsg c;
	c.type = NETCOMMANDTYPE_FRAMEINFO;
	c.slot = (std::uint8_t)slot;
	c.executionFrame = frame;
	c.commandCount = (std::uint16_t)count;
	return c;
}
} // namespace

TEST_CASE("mp2 authority: the CRC of frame 100 gets one execution frame on a peer that completed frame 100 before acquiring batch 100 and on one that "
		  "acquired it first, though batch 100 raises the run-ahead")
{
	FedTransport a, b;
	Network fast(config3(1), a), queued(config3(2), b);
	fast.sendLoadComplete();
	queued.sendLoadComplete();
	for (auto *t : { &a, &b })
	{
		const int local = t == &a ? 1 : 2;
		for (int s = 0; s < 3; ++s)
		{
			if (s != local)
			{
				NetCommandMsg loaded;
				loaded.type = NETCOMMANDTYPE_LOADCOMPLETE;
				loaded.slot = (std::uint8_t)s;
				t->put(loaded);
			}
		}
		for (int s = 0; s < 3; ++s)
		{
			if (s != local)
			{
				t->put(frameInfo(s, 99, 0));
				t->put(frameInfo(s, 100, s == 0 ? 1 : 0)); // the router's RUNAHEAD counts as an entry
			}
		}
		NetCommandMsg ra;
		ra.type = NETCOMMANDTYPE_RUNAHEAD;
		ra.slot = 0;
		ra.executionFrame = 100;
		ra.frame = 3;
		t->put(ra);
	}
	CommandList p, q;
	LockstepDriver fastD(&fast, nullptr), queuedD(&queued, nullptr);
	fastD.pump(99, p);
	queuedD.pump(99, q);
	FrameBatch batch;
	REQUIRE(fastD.acquire(99, batch));
	REQUIRE(queuedD.acquire(99, batch));
	FrameCompletion c;
	c.frame = 100;
	c.batchFrame = 99;
	c.hashed = true;
	c.breakdown = true;
	c.hash = 12345;
	fastD.completed(c);                  // the fast peer completes frame 100 first
	REQUIRE(queuedD.acquire(100, batch)); // the queued peer's worker acquired batch 100 (the run-ahead change) first
	queuedD.completed(c);
	REQUIRE(fastD.acquire(100, batch));
	CHECK(fast.currentRunAhead() == 3);
	CHECK(queued.currentRunAhead() == 3);
	CHECK(a.crcExecution != 0);
	CHECK(a.crcExecution == b.crcExecution);
	CHECK(fast.errors().empty());
	CHECK(queued.errors().empty());
}

TEST_CASE("mp2 authority: a RUNAHEAD from a peer that is not the packet router, or outside the configured bounds, is an error and changes nothing")
{
	SUBCASE("not the router")
	{
		FedTransport t;
		NetworkConfig cfg = config3(0);
		cfg.slotPlayerIndex[2] = -1;
		Network n(cfg, t);
		t.put(frameInfo(1, 2, 1));
		NetCommandMsg c;
		c.slot = 1;
		c.type = NETCOMMANDTYPE_RUNAHEAD;
		c.executionFrame = 2;
		c.frame = 5; // in bounds: the authority refuses it
		t.put(c);
		CommandList empty, out;
		n.update(2, empty);
		REQUIRE(n.isFrameReady(2));
		n.relayCommands(2, out);
		CHECK(n.currentRunAhead() == 2);
		REQUIRE(n.errors().size() == 1);
		CHECK(n.errors()[0].find("not the packet router") != std::string::npos);
	}
	SUBCASE("out of bounds, from the router")
	{
		FedTransport t;
		NetworkConfig cfg = config3(1);
		cfg.slotPlayerIndex[2] = -1;
		Network n(cfg, t);
		t.put(frameInfo(0, 2, 1));
		NetCommandMsg c;
		c.slot = 0;
		c.type = NETCOMMANDTYPE_RUNAHEAD;
		c.executionFrame = 2;
		c.frame = 1000; // the configured maximum is 25
		t.put(c);
		CommandList empty, out;
		n.update(2, empty);
		REQUIRE(n.isFrameReady(2)); // the count still holds
		n.relayCommands(2, out);
		CHECK(n.currentRunAhead() == 2);
		REQUIRE(!n.errors().empty());
		CHECK(n.errors()[0].find("outside") != std::string::npos);
	}
}

TEST_CASE("mp2 authority: a FRAMERESEND that puts another player's MSG_SELF_DESTRUCT in a slot that is not the packet router is refused; resent commands "
		  "belong to their owner's player")
{
	FedTransport u;
	Network r(config3(0), u);
	u.put(frameInfo(1, 2, 0));
	NetCommandMsg c;
	c.slot = 1;
	c.type = NETCOMMANDTYPE_FRAMERESEND;
	c.targetSlot = 2;
	c.frame = 2;
	c.messages.push_back(std::make_shared<GameMessage>(MSG_SELF_DESTRUCT, 0));
	c.messages.back()->appendBooleanArgument(true);
	u.put(c);
	CommandList empty, out;
	r.update(2, empty);
	CHECK(!r.isFrameReady(2)); // slot 2's frame is still missing
	REQUIRE(r.errors().size() == 1);
	CHECK(r.errors()[0].find("not the packet router") != std::string::npos);
	// a resent command of slot 2 claiming player 0 is slot 2's player's
	NetCommandMsg ok;
	ok.slot = 1;
	ok.type = NETCOMMANDTYPE_FRAMERESEND;
	ok.targetSlot = 2;
	ok.frame = 2;
	ok.messages.push_back(std::make_shared<GameMessage>(MSG_CREATE_SELECTED_GROUP, 0));
	ok.messages.back()->appendBooleanArgument(true);
	u.put(ok);
	r.update(2, empty);
	REQUIRE(r.isFrameReady(2));
	r.relayCommands(2, out);
	REQUIRE(out.messages().size() == 1);
	CHECK(out.messages()[0].getPlayerIndex() == 2);
}

TEST_CASE("mp2 authority: a command of a player dropped at frame 10 that only one survivor received is discarded on both")
{
	FedTransport a, b;
	Network n0(config3(0), a), n1(config3(1), b);
	CommandList empty;
	a.put(frameInfo(1, 10, 0));
	b.put(frameInfo(0, 10, 0));
	NetCommandMsg c;
	c.slot = 2;
	c.type = NETCOMMANDTYPE_GAMECOMMAND;
	c.executionFrame = 10;
	c.message = std::make_shared<GameMessage>(MSG_CREATE_SELECTED_GROUP, 2);
	c.message->appendBooleanArgument(true);
	b.put(c); // only slot 1 got it
	n0.update(10, empty);
	n1.update(10, empty);
	n0.disconnectPlayer(2, 10);
	n1.disconnectPlayer(2, 10);
	CommandList x, y;
	REQUIRE(n0.isFrameReady(10));
	REQUIRE(n1.isFrameReady(10));
	n0.relayCommands(10, x);
	n1.relayCommands(10, y);
	CHECK(x.messages().size() == 0);
	CHECK(y.messages().size() == 0);
	CHECK(n0.errors().empty());
	CHECK(n1.errors().empty());
}

TEST_CASE("mp2 authority: a DISCONNECTPLAYER from a peer that is not the packet router, delivered to one survivor only, changes no membership; a "
		  "drop for a frame already run is refused too")
{
	FedTransport a, b;
	NetworkConfig c0 = config3(0), c2 = config3(2);
	c0.slotPlayerIndex[3] = 3;
	c2.slotPlayerIndex[3] = 3;
	c0.crcInterval = 0;
	c2.crcInterval = 0;
	Network n0(c0, a), n2(c2, b);
	n0.sendLoadComplete();
	n2.sendLoadComplete();
	for (int s = 0; s < 4; ++s)
	{
		NetCommandMsg l;
		l.slot = (std::uint8_t)s;
		l.type = NETCOMMANDTYPE_LOADCOMPLETE;
		if (s != 0)
		{
			a.put(l);
		}
		if (s != 2)
		{
			b.put(l);
		}
	}
	for (int s = 0; s < 4; ++s)
	{
		if (s != 0)
		{
			a.put(frameInfo(s, 10, s == 3 ? 1 : 0));
		}
		if (s != 2)
		{
			b.put(frameInfo(s, 10, s == 3 ? 1 : 0));
		}
	}
	NetCommandMsg cmd;
	cmd.type = NETCOMMANDTYPE_GAMECOMMAND;
	cmd.slot = 3;
	cmd.executionFrame = 10;
	cmd.message = std::make_shared<GameMessage>(MSG_CREATE_SELECTED_GROUP, 3);
	cmd.message->appendBooleanArgument(true);
	a.put(cmd);
	b.put(cmd);
	NetCommandMsg drop;
	drop.type = NETCOMMANDTYPE_DISCONNECTPLAYER;
	drop.slot = 1; // not the router (slot 0)
	drop.targetSlot = 3;
	drop.frame = 10;
	b.put(drop);
	CommandList e, x, y;
	n0.update(10, e);
	n2.update(10, e);
	REQUIRE(n0.isFrameReady(10));
	REQUIRE(n2.isFrameReady(10));
	CHECK(!n0.isDisconnected(3));
	CHECK(!n2.isDisconnected(3));
	n0.relayCommands(10, x);
	n2.relayCommands(10, y);
	CHECK(x.messages().size() == 1);
	CHECK(y.messages().size() == 1);
	REQUIRE(n2.errors().size() == 1);
	CHECK(n2.errors()[0].find("may not decide") != std::string::npos);
	// the router's drop for frame 10, which slot 2 already ran: refused
	NetCommandMsg late = drop;
	late.slot = 0;
	b.put(late);
	n2.update(11, e);
	CHECK(!n2.isDisconnected(3));
	REQUIRE(n2.errors().size() == 2);
	CHECK(n2.errors()[1].find("already ran") != std::string::npos);
	// the router's drop for a frame not run yet is applied
	NetCommandMsg ok = drop;
	ok.slot = 0;
	ok.frame = 12;
	b.put(ok);
	n2.update(11, e);
	CHECK(n2.isDisconnected(3));
}

TEST_CASE("mp2 authority: the router's leave reached one survivor only; a successor's drop that claims the router gone is held on the other survivor (its "
		  "frame gated) until the leave arrives, then applied there too: identical membership and commands; without the claim both refuse it")
{
	auto run = [](std::uint8_t claim, bool &dropped2, bool &dropped4, size_t &cmds2, size_t &cmds4, size_t &errors2, size_t &errors4, bool &gatedBeforeLeave) {
		FedTransport a, b;
		NetworkConfig c2 = config3(2), c4 = config3(4);
		for (int s = 0; s < 5; ++s)
		{
			c2.slotPlayerIndex[(size_t)s] = s;
			c4.slotPlayerIndex[(size_t)s] = s;
		}
		c2.crcInterval = 0;
		c4.crcInterval = 0;
		Network n2(c2, a), n4(c4, b);
		n2.sendLoadComplete();
		n4.sendLoadComplete();
		for (int s = 0; s < 5; ++s)
		{
			NetCommandMsg l;
			l.slot = (std::uint8_t)s;
			l.type = NETCOMMANDTYPE_LOADCOMPLETE;
			if (s != 2)
			{
				a.put(l);
				a.put(frameInfo(s, 10, s == 3 ? 1 : 0));
			}
			if (s != 4)
			{
				b.put(l);
				b.put(frameInfo(s, 10, s == 3 ? 1 : 0));
			}
		}
		NetCommandMsg cmd;
		cmd.type = NETCOMMANDTYPE_GAMECOMMAND;
		cmd.slot = 3;
		cmd.executionFrame = 10;
		cmd.message = std::make_shared<GameMessage>(MSG_CREATE_SELECTED_GROUP, 3);
		cmd.message->appendBooleanArgument(true);
		a.put(cmd);
		b.put(cmd);
		NetCommandMsg leave;
		leave.type = NETCOMMANDTYPE_PLAYERLEAVE;
		leave.slot = 0;
		leave.executionFrame = 12; // the router leaves after frame 11
		a.put(leave);               // only slot 2 has it
		CommandList e, x, y;
		n2.update(10, e);
		n4.update(10, e);
		NetCommandMsg drop;
		drop.type = NETCOMMANDTYPE_DISCONNECTPLAYER;
		drop.slot = 1;
		drop.targetSlot = 3;
		drop.frame = 10;
		drop.releasedMask = claim;
		a.put(drop);
		b.put(drop);
		n2.update(10, e);
		n4.update(10, e);
		gatedBeforeLeave = !n4.allowedToContinue(10);
		b.put(leave); // the leave reaches slot 4 late
		n4.update(10, e);
		REQUIRE(n2.isFrameReady(10));
		REQUIRE(n4.isFrameReady(10));
		REQUIRE(n2.allowedToContinue(10));
		REQUIRE(n4.allowedToContinue(10));
		n2.relayCommands(10, x);
		n4.relayCommands(10, y);
		dropped2 = n2.isDisconnected(3);
		dropped4 = n4.isDisconnected(3);
		cmds2 = x.messages().size();
		cmds4 = y.messages().size();
		errors2 = n2.errors().size();
		errors4 = n4.errors().size();
	};
	bool d2 = false, d4 = false, gated = false;
	size_t x = 0, y = 0, e2 = 0, e4 = 0;
	SUBCASE("the drop claims the router gone")
	{
		run(1u << 0, d2, d4, x, y, e2, e4, gated);
		CHECK(gated); // held on slot 4 until the leave arrived
		CHECK(d2);
		CHECK(d4);
		CHECK(x == 0);
		CHECK(y == 0);
		CHECK(e2 == 0);
		CHECK(e4 == 0);
	}
	SUBCASE("the drop claims nothing: the router decides, both refuse")
	{
		run(0, d2, d4, x, y, e2, e4, gated);
		CHECK(!d2);
		CHECK(!d4);
		CHECK(x == 1);
		CHECK(y == 1);
		CHECK(e2 == 1);
		CHECK(e4 == 1);
	}
}
