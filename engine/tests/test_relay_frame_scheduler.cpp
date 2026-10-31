// OpenBFME unit tests. GPL-3.0.

#include "doctest.h"

#include "GameNetwork/RelayCanonicalInput.h"
#include "GameNetwork/RelayFrameBundle.h"
#include "GameNetwork/RelayFrameClock.h"
#include "GameNetwork/RelayFrameScheduler.h"
#include "GameNetwork/RelayMembership.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{
using RelayFrameBundle::Builder;

RelayCanonicalInput::Message order(std::uint64_t id, std::size_t size = 32)
{
	RelayCanonicalInput::Message message;
	message.kind = RelayCanonicalInput::Kind::Order;
	message.id = id;
	message.bytes.assign(size, 0x42);
	return message;
}

RelayCanonicalInput::Message beat(std::uint64_t id)
{
	RelayCanonicalInput::Message message;
	message.kind = RelayCanonicalInput::Kind::Beat;
	message.id = id;
	return message;
}

// origin 1000, first frame 4: frame 4 covers receipts [1000,1200) and
// closes at 1200; frame 5 covers [1200,1400), closes at 1400.
RelayFrameClock clock4() { return RelayFrameClock(1000, 4); }

RelayMembership::Snapshot membership(std::uint16_t active)
{
	RelayMembership::Snapshot snapshot;
	snapshot.slotMask = snapshot.active = snapshot.loaded = active;
	snapshot.clockValid = true;
	snapshot.originMs = 1000;
	snapshot.firstFrame = 4;
	return snapshot;
}

using Admission = Builder::Admission;
}

TEST_CASE("frame scheduler next-frame: boundary seals, late orders slip one frame")
{
	Builder builder(1, 1, 0x0003, 4);
	RelayFrameScheduler::Scheduler scheduler(RelayFrameScheduler::Policy::NextFrame, 4);
	RelayFrameClock clock = clock4();
	std::string error;

	// An order received inside frame 4's window stamps frame 4.
	CHECK(scheduler.admit(0, order(1), 1100, clock, builder, error) == Admission::Accepted);
	CHECK_FALSE(scheduler.sealDue(1199, clock, membership(0x0003), builder, error));
	CHECK(scheduler.sealDue(1200, clock, membership(0x0003), builder, error));
	CHECK(scheduler.counters().lateOrders == 0);

	// An order received in the next window stamps the next frame.
	CHECK(scheduler.admit(0, order(2), 1250, clock, builder, error) == Admission::Accepted);
	CHECK(scheduler.counters().lateOrders == 0);

	// Commit frame 4 (the caller journals; the test commits directly), then a
	// stale order stamped for the closed frame is late: it runs on frame 5.
	RelayCommit::Stamp stamp;
	stamp.game = 1;
	stamp.epoch = 1;
	stamp.sequence = 1;
	stamp.frame = 4;
	stamp.membershipGeneration = 1;
	RelayCommit::Record record;
	auto m = membership(0x0003);
	CHECK(builder.prepare(stamp, m, record, error));
	CHECK(builder.committed(stamp, error));
	CHECK(scheduler.admit(0, order(3), 1100, clock, builder, error) == Admission::Accepted);
	CHECK(scheduler.counters().lateOrders == 1);
	CHECK_FALSE(scheduler.sealDue(1250, clock, m, builder, error));
	CHECK(scheduler.sealDue(1400, clock, m, builder, error));
}

TEST_CASE("frame scheduler equal-delay: every required slot's beat closes the frame")
{
	Builder builder(1, 1, 0x0003, 4);
	RelayFrameScheduler::Scheduler scheduler(RelayFrameScheduler::Policy::EqualDelay, 4);
	RelayFrameClock clock = clock4();
	auto m = membership(0x0003);
	std::string error;

	// Orders stamp to the open frame while the window runs.
	CHECK(scheduler.admit(0, order(1), 1100, clock, builder, error) == Admission::Accepted);
	CHECK(scheduler.admit(1, order(1), 1100, clock, builder, error) == Admission::Accepted);

	// One slot's beat is not enough; the frame waits for both required slots.
	CHECK(scheduler.admit(0, beat(1), 1150, clock, builder, error) == Admission::Accepted);
	CHECK(scheduler.awaiting(m, builder) == 0x0002);
	CHECK_FALSE(scheduler.sealDue(1300, clock, m, builder, error));

	// A late second beat still closes frame 4: everyone waited (equal delay).
	CHECK(scheduler.admit(1, beat(1), 1450, clock, builder, error) == Admission::Accepted);
	CHECK(scheduler.awaiting(m, builder) == 0);
	CHECK(scheduler.sealDue(1450, clock, m, builder, error));

	// An order received after the slot's own beat stamps the next window's
	// frame, so it cannot change the frame that is still sealing.
	CHECK(scheduler.admit(0, order(2), 1500, clock, builder, error) == Admission::Accepted);
	RelayCommit::Record record;
	RelayCommit::Stamp stamp;
	stamp.game = 1;
	stamp.epoch = 1;
	stamp.sequence = 1;
	stamp.frame = 4;
	stamp.membershipGeneration = 1;
	CHECK(builder.prepare(stamp, m, record, error));
	RelayFrameBundle::Frame frame;
	CHECK(RelayFrameBundle::decode(record.payload, frame, error));
	CHECK(frame.orders.size() == 2);   // the two pre-beat orders; order 2 stamps frame 5
}

TEST_CASE("frame scheduler equal-delay: held and departed slots cannot stall the beat")
{
	Builder builder(1, 1, 0x0007, 4);
	RelayFrameScheduler::Scheduler scheduler(RelayFrameScheduler::Policy::EqualDelay, 4);
	RelayFrameClock clock = clock4();
	std::string error;

	auto m = membership(0x0007);
	m.held = 0x0004;              // slot 2 is held for rejoin
	CHECK_FALSE(scheduler.sealDue(1100, clock, m, builder, error));
	CHECK(scheduler.admit(0, beat(1), 1100, clock, builder, error) == Admission::Accepted);
	CHECK(scheduler.admit(1, beat(1), 1100, clock, builder, error) == Admission::Accepted);
	CHECK(scheduler.sealDue(1100, clock, m, builder, error));

	// A departed slot never gates either.
	Builder builder2(1, 1, 0x0007, 4);
	RelayFrameScheduler::Scheduler scheduler2(RelayFrameScheduler::Policy::EqualDelay, 4);
	auto m2 = membership(0x0007);
	m2.departed = 0x0004;
	CHECK(scheduler2.admit(0, beat(1), 1100, clock, builder2, error) == Admission::Accepted);
	CHECK(scheduler2.admit(1, beat(1), 1100, clock, builder2, error) == Admission::Accepted);
	CHECK(scheduler2.sealDue(1100, clock, m2, builder2, error));
}

TEST_CASE("frame scheduler: beat ids are a dense per-slot sequence")
{
	Builder builder(1, 1, 0x0003, 4);
	RelayFrameScheduler::Scheduler scheduler(RelayFrameScheduler::Policy::EqualDelay, 4);
	RelayFrameClock clock = clock4();
	std::string error;

	CHECK(scheduler.admit(0, beat(1), 1100, clock, builder, error) == Admission::Accepted);
	CHECK(scheduler.admit(0, beat(3), 1100, clock, builder, error) == Admission::InvalidSequence);
	CHECK(scheduler.admit(0, beat(1), 1100, clock, builder, error) == Admission::InvalidSequence);
	CHECK(scheduler.admit(0, beat(2), 1100, clock, builder, error) == Admission::Accepted);
	// Beats are tracked per slot: slot 1 still expects its first.
	CHECK(scheduler.admit(1, beat(1), 1100, clock, builder, error) == Admission::Accepted);
	CHECK(scheduler.counters().beats == 3);
}

TEST_CASE("frame scheduler: admission results pass through unchanged")
{
	Builder builder(1, 1, 0x0003, 4);
	RelayFrameScheduler::Scheduler scheduler(RelayFrameScheduler::Policy::NextFrame, 4);
	RelayFrameClock clock = clock4();
	std::string error;

	// Order ids stay dense per slot; the builder polices the sequence.
	CHECK(scheduler.admit(0, order(2), 1100, clock, builder, error) == Admission::InvalidSequence);
	CHECK(scheduler.admit(4, order(1), 1100, clock, builder, error) == Admission::InvalidSlot);
	CHECK(scheduler.admit(0, order(1), 1100, clock, builder, error) == Admission::Accepted);

	// A receipt before the committed origin cannot be stamped.
	CHECK(scheduler.admit(1, order(1), 500, clock, builder, error) == Admission::InvalidFrame);
}
