// OpenBFME. Lane RENDERWAIT-1: the render (main) thread never waits for a logic frame (LiveGame's client window and wait log).
//
// UBER-1 measured the real client's main thread waiting on the logic worker for most of each frame in a big battle; RENDERWAIT-1's wait log named the
// site (the spell ring's per-frame HUD query, through InGameHud::enterContext -> LiveGame::waitIdle). The fix: the render thread reads and commands the
// live logic only in the client window of its render frame (the worker holds between frames for it); a render frame that finds the worker inside a frame
// skips that work instead of waiting. These tests drive a live game the way the device layer does (GameWorld: beginClientWindow at the frame's start, the
// HUD's work while held, advance) beside a worker whose frames take longer than the 200 ms clock.
#include "doctest.h"
#include "HudTestUtil.h"

#include "GameLogic/GameMessage.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace
{
// two armies in contact and their orders (the same in every rig of a test, before the worker starts)
void stageBattle(hudtest::Rig &rig)
{
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	std::string err;
	std::vector<ObjectID> ours, theirs;
	for (int i = 0; i < 6; ++i)
	{
		Object *a = rig.game->createObject(i < 4 ? "GondorFighterHorde" : "GondorArcherHorde", rig.index(), Coord3D{ 1200.0f + 80.0f * (float)i, 1150.0f, 0 }, 0, &err);
		REQUIRE_MESSAGE(a != nullptr, err);
		ours.push_back(a->getID());
		Object *b = rig.game->createObject("MordorFighterHorde", enemy->getPlayerIndex(), Coord3D{ 1200.0f + 80.0f * (float)i, 1550.0f, 0 }, 3.14f, &err);
		REQUIRE_MESSAGE(b != nullptr, err);
		theirs.push_back(b->getID());
	}
	auto order = [&](int player, const std::vector<ObjectID> &ids, float y) {
		GameMessage select(MSG_CREATE_SELECTED_GROUP, player);
		select.appendBooleanArgument(true);
		for (ObjectID id : ids)
		{
			select.appendObjectIDArgument(id);
		}
		rig.game->commands().append(select);
		GameMessage move(MSG_DO_MOVETO, player);
		move.appendLocationArgument(Coord3D{ 1400, y, 0 });
		rig.game->commands().append(move);
	};
	order(rig.index(), ours, 1500);
	order(enemy->getPlayerIndex(), theirs, 1200);
}
} // namespace

TEST_CASE("renderwait1 retail: 60 Hz render frames beside 250 ms logic frames never wait, get a client window every logic frame, and the logic is unchanged")
{
	if (!hudtest::haveWorld("renderwait1 client window"))
	{
		return;
	}
	constexpr int kFrames = 12;     // logic frames run after the order
	constexpr int kWorkerMs = 250;  // each worker frame: longer than the 200 ms tick, so the worker is always behind and never idle
	// the reference: the same game single-threaded, its state hash after kFrames frames
	std::uint32_t reference = 0;
	UnsignedInt referenceFrame = 0;
	{
		hudtest::Rig ref(hudtest::shared());
		stageBattle(ref);
		const UnsignedInt start = ref.game->frame();
		while (ref.game->frame() < start + kFrames)
		{
			ref.game->advance(0.2);
		}
		referenceFrame = ref.game->frame();
		reference = ref.game->logic().computeStateHash();
	}
	hudtest::Rig rig(hudtest::shared());
	stageBattle(rig);
	const UnsignedInt start = rig.game->frame();
	rig.game->setWorkerFrameDelayMs(kWorkerMs);
	rig.game->setLogicThread(true);
	REQUIRE(rig.game->logicThread());
	const unsigned long long waits0 = rig.game->blockingWaits();
	int heldFrames = 0, renderFrames = 0;
	double worstRenderMs = 0.0;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (rig.game->frame() < start + kFrames && std::chrono::steady_clock::now() < deadline)
	{
		const auto t0 = std::chrono::steady_clock::now();
		// GameWorld's render frame: the window at its start, the HUD's live reads while the logic is held, then advance (queues the clock's frames, ends
		// the window, presents). The clock stops once the last frame of the test is due (the worker catches up; nothing more is queued)
		rig.game->beginClientWindow();
		if (rig.game->logicHeld())
		{
			++heldFrames;
			rig.input->update(); // the HUD's input update reads the live logic (selection, picking): the reads InGameHud::update makes in the window
			CHECK(rig.game->logic().getFrame() == rig.game->frame()); // held: the live logic is the latest published frame, none is running
			CHECK(rig.game->players().getLocalPlayer() == rig.local);
		}
		rig.game->advance(rig.game->dueFrame() < start + kFrames ? 1.0 / 60.0 : 0.0);
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		worstRenderMs = std::max(worstRenderMs, ms);
		++renderFrames;
		std::this_thread::sleep_until(t0 + std::chrono::microseconds(16667));
	}
	const LiveGame::ClientWindowStats w = rig.game->clientWindowStats();
	MESSAGE("render frames " << renderFrames << ", held " << heldFrames << ", worst render frame " << worstRenderMs << " ms; windows opened " << w.opened << ", missed "
			<< w.missed << ", worker parks " << w.parks << " (" << w.parkTimeouts << " timed out), worker held " << w.workerHeldMs << " ms for " << kFrames
			<< " frames of " << kWorkerMs << " ms");
	// the effect: no render frame blocked on the worker, and none took as long as a logic frame
	CHECK(rig.game->blockingWaits() == waits0);
	CHECK(rig.game->logicWaitSites().empty());
	CHECK(worstRenderMs < kWorkerMs / 2.0); // tolerance: wall time on a shared machine; a render frame that waited would take most of a 250 ms logic frame
	// the HUD's live work was not starved by a worker that never went idle: the render thread held it between frames at least once per logic frame
	CHECK(heldFrames >= kFrames - 1); // tolerance: how many render frames fall between two worker frames is wall-clock timing; at least one per logic frame is the rule
	CHECK(w.parks >= 1); // tolerance: the number of holds depends on when render frames fall; a worker that never held would leave the HUD starved
	// the logic's work and order are unchanged: the same state after the same frames as the single-threaded reference
	rig.game->waitIdle();
	REQUIRE(rig.game->frame() == referenceFrame);
	CHECK(rig.game->logic().computeStateHash() == reference);
	rig.game->setLogicThread(false);
}

TEST_CASE("renderwait1 retail: the wait log names a blocking wait's site; a full wait in the client window lifts it instead of deadlocking")
{
	if (!hudtest::haveWorld("renderwait1 wait log"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared());
	rig.game->setWorkerFrameDelayMs(150);
	rig.game->setLogicThread(true);
	REQUIRE(rig.game->logicThread());
	REQUIRE(rig.game->logicWaitSites().empty());
	// outside a window, a live read while the worker runs a frame blocks: logged under this file and line
	rig.game->advance(0.2);
	REQUIRE_FALSE(rig.game->logicIdle());
	const int line = __LINE__ + 1;
	const UnsignedInt frame = rig.game->logic().getFrame();
	const std::vector<LiveGame::WaitSite> sites = rig.game->logicWaitSites();
	REQUIRE(sites.size() == 1);
	CHECK(sites[0].site == "test_renderwait1.cpp:" + std::to_string(line));
	CHECK(sites[0].count == 1);
	CHECK(sites[0].worstMs > 50.0); // tolerance: wall time; the wait covers most of the worker's 150 ms frame
	CHECK(frame == rig.game->frame());
	rig.game->resetLogicWaitSites();
	CHECK(rig.game->logicWaitSites().empty());
	// a window asked while the worker runs a frame stays closed, and the worker holds for the next one after that frame
	rig.game->advance(0.2);
	// (until the worker has taken the queued frame, a window still opens before it: it is ended again and the worker starts the frame; the gate's loaded
	// slot showed that race)
	bool missed = false;
	const auto started = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!missed && std::chrono::steady_clock::now() < started)
	{
		if (rig.game->beginClientWindow())
		{
			rig.game->endClientWindow();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		else
		{
			missed = true;
		}
	}
	REQUIRE(missed);
	CHECK_FALSE(rig.game->logicHeld());
	rig.game->advance(0.2); // a third frame queued (no window to end)
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!rig.game->beginClientWindow() && std::chrono::steady_clock::now() < until)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	REQUIRE(rig.game->logicHeld());
	CHECK_FALSE(rig.game->logicIdle()); // a frame is queued: the worker holds for the window
	const UnsignedInt held = rig.game->frame();
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	CHECK(rig.game->frame() == held); // nothing ran while the window was open
	// inside the window, the protocol owner's full wait (drainFrames) lifts the window, the queued frame runs, and the window is taken again
	const auto t0 = std::chrono::steady_clock::now();
	rig.game->drainFrames();
	CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5)); // tolerance: wall time; a deadlock would never return (one 150 ms frame runs)
	CHECK(rig.game->frame() == held + 1);
	CHECK(rig.game->logicIdle());
	CHECK(rig.game->logicHeld());
	rig.game->endClientWindow();
	rig.game->setLogicThread(false);
}

// Sol's review r1 #1: input issued in a client window while older frames are still queued. Before the window every accessor drained the worker first, so
// the input ran in the first frame queued after it; the window must give the same frame (it ran in the next queued one: the hashes differed from frame 2).
TEST_CASE("renderwait1 retail: input issued in a window while frames are queued runs in the first frame queued after it, as when the accessors drained the worker")
{
	if (!hudtest::haveWorld("renderwait1 backlog input"))
	{
		return;
	}
	struct Staged
	{
		std::vector<ObjectID> ours;
		Coord3D first{ 0, 0, 0 };
	};
	auto stage = [](hudtest::Rig &rig) {
		Staged st;
		std::string err;
		for (int i = 0; i < 4; ++i)
		{
			Object *a = rig.game->createObject("GondorFighterHorde", rig.index(), Coord3D{ 1200.0f + 80.0f * (float)i, 1150.0f, 0 }, 0, &err);
			REQUIRE_MESSAGE(a != nullptr, err);
			st.ours.push_back(a->getID());
		}
		st.first = *rig.game->logic().findObjectByID(st.ours.front())->getPosition();
		return st;
	};
	auto issue = [](hudtest::Rig &rig, const Staged &st) {
		GameMessage select(MSG_CREATE_SELECTED_GROUP, rig.index());
		select.appendBooleanArgument(true);
		for (ObjectID id : st.ours)
		{
			select.appendObjectIDArgument(id);
		}
		rig.game->commands().append(select);
		GameMessage move(MSG_DO_MOVETO, rig.index());
		move.appendLocationArgument(Coord3D{ 1500, 1500, 0 });
		rig.game->commands().append(move);
	};
	auto positionOf = [](hudtest::Rig &rig, ObjectID id) { return *rig.game->logic().findObjectByID(id)->getPosition(); };
	// the reference, single-threaded: three frames, the input, then five more (the input runs in the fourth)
	std::uint32_t refAt3 = 0, refAt4 = 0, refAt8 = 0;
	Coord3D refPosAt3{ 0, 0, 0 }, refPosAt8{ 0, 0, 0 };
	{
		hudtest::Rig ref(hudtest::shared());
		const Staged st = stage(ref);
		for (int i = 0; i < 3; ++i)
		{
			ref.game->advance(0.2);
		}
		refAt3 = ref.game->logic().computeStateHash();
		refPosAt3 = positionOf(ref, st.ours.front());
		issue(ref, st);
		ref.game->advance(0.2);
		refAt4 = ref.game->logic().computeStateHash();
		for (int i = 0; i < 4; ++i)
		{
			ref.game->advance(0.2);
		}
		refAt8 = ref.game->logic().computeStateHash();
		refPosAt8 = positionOf(ref, st.ours.front());
		// the order has an effect to tell the frames apart by: the horde left its place
		REQUIRE((refPosAt8.x != st.first.x || refPosAt8.y != st.first.y));
		REQUIRE(refAt3 != refAt4);
	}
	hudtest::Rig rig(hudtest::shared());
	const Staged st = stage(rig);
	const UnsignedInt start = rig.game->frame();
	rig.game->setWorkerFrameDelayMs(150);
	rig.game->setLogicThread(true);
	REQUIRE(rig.game->logicThread());
	rig.game->advance(0.6); // three frames queued
	REQUIRE(rig.game->dueFrame() == start + 3);
	// a client window while frames are still queued: asked until the worker holds between two frames
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	bool open = false;
	while (!open && std::chrono::steady_clock::now() < until)
	{
		open = rig.game->beginClientWindow();
		if (open && rig.game->logicIdle())
		{
			break; // (every frame already ran: no backlog to test against; fails below)
		}
		if (!open)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}
	REQUIRE(open);
	REQUIRE_FALSE(rig.game->logicIdle()); // frames are queued behind the window: the case under test
	const UnsignedInt issuedAt = rig.game->frame();
	CHECK(issuedAt < start + 3); // tolerance: which of the three queued frames the window falls behind is wall-clock timing; any before the last is the case
	issue(rig, st);
	rig.game->endClientWindow();
	// the queued frames run without the input
	rig.game->waitIdle();
	REQUIRE(rig.game->frame() == start + 3);
	CHECK(rig.game->logic().computeStateHash() == refAt3);
	CHECK(positionOf(rig, st.ours.front()).x == refPosAt3.x);
	CHECK(positionOf(rig, st.ours.front()).y == refPosAt3.y);
	// the first frame queued after it executes it
	rig.game->advance(0.2);
	rig.game->waitIdle();
	REQUIRE(rig.game->frame() == start + 4);
	CHECK(rig.game->logic().computeStateHash() == refAt4);
	rig.game->advance(0.8);
	rig.game->waitIdle();
	REQUIRE(rig.game->frame() == start + 8);
	CHECK(rig.game->logic().computeStateHash() == refAt8);
	CHECK(positionOf(rig, st.ours.front()).x == refPosAt8.x);
	CHECK(positionOf(rig, st.ours.front()).y == refPosAt8.y);
	rig.game->setLogicThread(false);
}

// Sol's review r1 #4: a window never outlives its render frame: one that no advance ended is ended at the next render frame's start, and that frame gets none
TEST_CASE("renderwait1 retail: a window no advance ended is ended at the next render frame's start, which opens none")
{
	if (!hudtest::haveWorld("renderwait1 unended window"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared());
	rig.game->setWorkerFrameDelayMs(20);
	rig.game->setLogicThread(true);
	REQUIRE(rig.game->beginClientWindow()); // the worker is idle
	const UnsignedInt before = rig.game->frame();
	REQUIRE_FALSE(rig.game->beginClientWindow()); // the next render frame: the old window ends, none opens
	CHECK(rig.game->clientWindowStats().unended == 1);
	rig.game->advance(0.2); // (no window to end) the frame is queued and runs
	rig.game->waitIdle();
	CHECK(rig.game->frame() == before + 1);
	const LiveGame::ClientWindowStats w = rig.game->clientWindowStats();
	CHECK(w.maxWindowMs == w.windowMs); // one window so far: the longest is the total
	rig.game->setLogicThread(false);
}
