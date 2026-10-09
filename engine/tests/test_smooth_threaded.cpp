// OpenBFME. SMOOTH-1 tests (S-810): the logic on its worker thread gives exactly the single-thread game.
//
// A retail skirmish map with two players' hordes fighting (melee and archers: projectile launches), player commands, creation / destruction churn and long
// stalls, played twice per render-rate pattern: on the caller's thread (LiveGame::Options::logicThread false, the fallback) and on the worker. Compared:
// the state hash of every completed frame, the logic RNG call log, and on the render side the client events (each applied once, in order: the manager
// reports an out-of-order or repeated event as an error) and the drawables. Inputs are keyed to logic frames, so every render-rate pattern (5, 30, 60,
// 144, 240 Hz, uneven, uncapped real time), worker on / off and interpolation on / off gives the one reference game; long pauses (beyond
// maxFramesPerAdvance) are compared worker against single thread. Then: a published snapshot never changes once taken (immutability), the
// presentation holds the last completed frame when the worker is late, and destroying a game whose worker has frames queued joins cleanly.
#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Player.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/LiveGame.h"
#include "GameClient/LogicSnapshot.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

struct Played
{
	std::vector<std::pair<UnsignedInt, std::uint32_t>> hashes;
	std::vector<GameLogicRandom::Call> rng;
	std::vector<std::string> drawErrors;
	size_t eventsApplied = 0, eventsPending = 0;
	size_t liveDrawables = 0;
	UnsignedInt frame = 0;
	unsigned long long held = 0, damage = 0, launches = 0;
	bool presentedBeyondCompleted = false;
};

GameMessage selectMsg(int player, const std::vector<ObjectID> &ids)
{
	GameMessage m(MSG_CREATE_SELECTED_GROUP, player);
	m.appendBooleanArgument(true);
	for (ObjectID id : ids)
	{
		m.appendObjectIDArgument(id);
	}
	return m;
}

GameMessage moveMsg(int player, const Coord3D &to)
{
	GameMessage m(MSG_DO_MOVETO, player);
	m.appendLocationArgument(to);
	return m;
}

// The inputs are keyed to LOGIC frames (review r2): each is given once the clock has made its frame due (LiveGame::dueFrame), before the next
// advance, so it executes in the next frame in every mode and at every render rate. `deltas` is cycled (empty: real time, uncapped) until
// `frames` logic frames are due.
const UnsignedInt kCommandFrame = 6, kChurnFrame = 30;

Played play(bool threaded, const std::vector<double> &deltas, UnsignedInt frames, bool interpolate)
{
	SharedWorld &s = shared();
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	Played out;
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.mapName = "map mp fall back 4p";
	o.seed = 4711;
	o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
	o.logicThread = threaded;
	o.presentationDelaySeconds = threaded ? 0.03 : 0.0;
	o.hashEveryFrame = true;
	o.logRandomCalls = true;
	std::string err;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	REQUIRE(game.logicThread() == threaded);
	game.setRenderInterpolation(interpolate);
	// the two lobby players by name (index 0 is the neutral player; on this map they are 10 and 11), opponents
	Player *pa = game.players().findPlayerWithName("Player_1");
	Player *pb = game.players().findPlayerWithName("Player_2");
	REQUIRE(pa != nullptr);
	REQUIRE(pb != nullptr);
	REQUIRE(pa->getRelationship(pb) == ENEMIES);
	const int p0 = pa->getPlayerIndex();
	const int p1 = pb->getPlayerIndex();
	const Coord3D centre{ 1400.0f, 1400.0f, 0.0f };
	std::vector<ObjectID> ours, theirs;
	const char *mine[] = { "GondorFighterHorde", "GondorArcherHorde", "GondorFighterHorde" };
	const char *enemy[] = { "MordorFighterHorde", "MordorArcherHorde", "MordorFighterHorde" };
	for (int i = 0; i < 3; ++i)
	{
		Object *a = game.createObject(mine[i], p0, Coord3D{ centre.x - 260.0f, centre.y + 120.0f * (float)i, 0.0f }, 0.0f, &err);
		REQUIRE_MESSAGE(a, err);
		ours.push_back(a->getID());
		Object *b = game.createObject(enemy[i], p1, Coord3D{ centre.x + 260.0f, centre.y + 120.0f * (float)i, 0.0f }, 3.14159f, &err);
		REQUIRE_MESSAGE(b, err);
		theirs.push_back(b->getID());
	}
	bool commanded = false, churned = false;
	size_t step = 0;
	auto last = std::chrono::steady_clock::now();
	while (game.dueFrame() < frames)
	{
		if (!commanded && game.dueFrame() >= kCommandFrame)
		{
			// both sides march into each other (the main thread waits for the worker before it appends: they belong to the next frame)
			commanded = true;
			game.commands().append(selectMsg(p0, ours));
			game.commands().append(moveMsg(p0, Coord3D{ centre.x + 200.0f, centre.y + 120.0f, 0.0f }));
			game.commands().append(selectMsg(p1, theirs));
			game.commands().append(moveMsg(p1, Coord3D{ centre.x - 200.0f, centre.y + 120.0f, 0.0f }));
		}
		if (!churned && game.dueFrame() >= kChurnFrame)
		{
			// churn: a new horde for each side, and one of the enemy's hordes removed
			churned = true;
			REQUIRE(game.createObject("GondorFighterHorde", p0, Coord3D{ centre.x - 300.0f, centre.y - 150.0f, 0.0f }, 0.0f, &err));
			REQUIRE(game.createObject("MordorArcherHorde", p1, Coord3D{ centre.x + 300.0f, centre.y - 150.0f, 0.0f }, 0.0f, &err));
			if (Object *victim = game.logic().findObjectByID(theirs[2]))
			{
				game.logic().destroyObject(victim);
			}
		}
		double dt;
		if (deltas.empty())
		{
			const auto now = std::chrono::steady_clock::now();
			dt = std::min(0.19, std::chrono::duration<double>(now - last).count()); // uncapped: real time between render frames (at most one frame a step)
			last = now;
		}
		else
		{
			dt = deltas[step % deltas.size()];
		}
		++step;
		game.advance(dt);
		const std::shared_ptr<const LogicSnapshot> shown = game.presentedSnapshot();
		REQUIRE(shown.get() != nullptr);
		const std::shared_ptr<const LogicSnapshot> latest = game.latestSnapshot();
		out.presentedBeyondCompleted = out.presentedBeyondCompleted || shown->frame > latest->frame;
	}
	game.waitIdle();
	out.hashes = game.frameHashes();
	out.rng = game.logic().random().callLog();
	out.frame = game.frame();
	out.held = game.heldPresentations();
	out.damage = game.logic().combat().counters().damageApplications;
	out.launches = game.logic().combat().counters().projectilesLaunched;
	// review r2: the render side takes everything published up to the last frame (advance(0) keeps the presentation delay)
	game.refreshClient(0.0, 1.0);
	LiveGame::Report r = game.report();
	out.drawErrors = r.drawables.errors;
	out.eventsApplied = game.drawables().eventsApplied();
	out.eventsPending = game.pendingClientEvents();
	out.liveDrawables = game.drawables().liveCount();
	return out;
}

bool sameRng(const std::vector<GameLogicRandom::Call> &a, const std::vector<GameLogicRandom::Call> &b)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i)
	{
		const auto &x = a[i];
		const auto &y = b[i];
		if (x.real != y.real || x.lo != y.lo || x.hi != y.hi || x.result != y.result || std::memcmp(&x.rresult, &y.rresult, sizeof(float)) != 0 || x.file != y.file ||
			x.line != y.line)
		{
			return false;
		}
	}
	return true;
}

void sameGame(const Played &ref, const Played &run)
{
	CHECK(run.hashes.size() == ref.hashes.size());
	size_t firstDiff = ref.hashes.size();
	for (size_t i = 0; i < ref.hashes.size() && i < run.hashes.size(); ++i)
	{
		if (ref.hashes[i] != run.hashes[i] && firstDiff == ref.hashes.size())
		{
			firstDiff = i;
		}
	}
	CHECK_MESSAGE(firstDiff == ref.hashes.size(), "the first differing frame: " << (firstDiff < ref.hashes.size() ? ref.hashes[firstDiff].first : 0));
	CHECK(sameRng(ref.rng, run.rng));
	CHECK(run.frame == ref.frame);
	CHECK(run.drawErrors.empty()); // includes "client event applied out of order": every event once, in order
	CHECK(run.eventsPending == 0);
	CHECK(run.eventsApplied == ref.eventsApplied);
	CHECK(run.liveDrawables == ref.liveDrawables);
	CHECK_FALSE(run.presentedBeyondCompleted);
}
} // namespace

TEST_CASE("smooth1 retail: the logic worker gives the single-thread game frame for frame (hashes, RNG, events) at every render rate")
{
	if (!hudtest::haveWorld("smooth1 threaded"))
	{
		return;
	}
	// the reference: single thread, 5 Hz render; 60 logic frames (12 s) of a two-against-two fight with commands and churn at logic frames 6 and 30
	const UnsignedInt frames = 60;
	const Played ref = play(false, { 0.2 }, frames, true);
	REQUIRE(ref.hashes.size() > 50);
	CHECK(ref.rng.size() > 0);
	CHECK(ref.damage > 0);   // the two players really fight (review r2: the players by name, not index 0)
	CHECK(ref.launches > 0); // and the archers launch projectiles
	CHECK(ref.drawErrors.empty());
	CHECK(ref.eventsPending == 0);
	struct Pattern
	{
		const char *label;
		std::vector<double> deltas;
	};
	const std::vector<Pattern> patterns = {
		{ "5 Hz", { 0.2 } }, { "30 Hz", { 1.0 / 30.0 } }, { "60 Hz", { 1.0 / 60.0 } }, { "144 Hz", { 1.0 / 144.0 } }, { "240 Hz", { 1.0 / 240.0 } },
		{ "uneven", { 0.004, 0.031, 0.0007, 0.016, 0.09, 0.012, 0.0, 0.044 } }, { "uncapped (real time)", {} },
	};
	for (const Pattern &pt : patterns)
	{
		for (int threaded = 0; threaded < 2; ++threaded)
		{
			for (int interp = 0; interp < 2; ++interp)
			{
				if (pt.deltas.empty() && interp == 0)
				{
					continue; // real time is slow (12 s a run): one presentation mode
				}
				CAPTURE(pt.label);
				CAPTURE(threaded);
				CAPTURE(interp);
				const Played run = play(threaded != 0, pt.deltas, frames, interp != 0);
				sameGame(ref, run); // the inputs are keyed to logic frames: every rate, mode and presentation gives the reference game
				std::printf("  info: %s worker=%d interp=%d: %zu frames, %zu RNG draws, %zu events, %zu drawables, damage %llu, launches %llu, presentations held %llu\n",
					pt.label, threaded, interp, run.hashes.size(), run.rng.size(), run.eventsApplied, run.liveDrawables, run.damage, run.launches, run.held);
			}
		}
	}
	// long render pauses (3 s in one step, beyond maxFramesPerAdvance 10: the rest of the time is dropped, never a frame): the frames the clock makes due
	// differ from the reference's (inputs then land on later due frames), so the single-thread and the worker runs of this pattern are compared with each other
	const std::vector<double> stalls = { 1.0 / 144.0, 1.0 / 144.0, 3.0, 1.0 / 144.0, 0.5 };
	const Played s0 = play(false, stalls, frames, true);
	const Played s1 = play(true, stalls, frames, true);
	sameGame(s0, s1);
}

TEST_CASE("smooth1 retail: a published snapshot is immutable, the worker can be switched off and on between frames, and a game with queued frames shuts down")
{
	if (!hudtest::haveWorld("smooth1 snapshot"))
	{
		return;
	}
	SharedWorld &s = shared();
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	std::shared_ptr<const LogicSnapshot> kept;
	std::vector<ObjectSnapshot> copy;
	{
		LiveGame game(*s.world, *s.mount->fs, assets, s.options);
		LiveGame::Options o;
		o.mapName = "map mp fall back 4p";
		o.seed = 5;
		o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
		o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
		o.logicThread = true;
		std::string err;
		REQUIRE_MESSAGE(game.load(o, &err), err);
		std::string e2;
		Player *p1 = game.players().findPlayerWithName("Player_1");
		REQUIRE(p1 != nullptr);
		REQUIRE(game.createObject("GondorFighterHorde", p1->getPlayerIndex(), Coord3D{ 1300.0f, 1400.0f, 0.0f }, 0.0f, &e2));
		for (int i = 0; i < 20; ++i)
		{
			game.advance(0.2);
		}
		game.waitIdle();
		kept = game.latestSnapshot();
		REQUIRE(kept.get() != nullptr);
		copy = kept->objects;
		const UnsignedInt keptFrame = kept->frame;
		for (int i = 0; i < 20; ++i)
		{
			game.advance(0.2);
		}
		game.waitIdle();
		CHECK(game.latestSnapshot()->frame == keptFrame + 20);
		// the kept snapshot did not change while 20 more frames were computed and published
		REQUIRE(kept->objects.size() == copy.size());
		bool same = true;
		for (size_t i = 0; i < copy.size(); ++i)
		{
			const ObjectSnapshot &a = kept->objects[i], &b = copy[i];
			same = same && a.id == b.id && a.tmpl == b.tmpl && std::memcmp(a.basis, b.basis, sizeof(a.basis)) == 0 && std::memcmp(&a.position, &b.position, sizeof(Coord3D)) == 0 &&
				std::memcmp(&a.recordedPos, &b.recordedPos, sizeof(Coord3D)) == 0 && std::memcmp(&a.nextPos, &b.nextPos, sizeof(Coord3D)) == 0 && a.lastMovedFrame == b.lastMovedFrame &&
				a.constructionPercent == b.constructionPercent;
		}
		CHECK(same);
		CHECK(kept->frame == keptFrame);
		// switch to the main thread and back between frames: nothing is dropped
		game.setLogicThread(false);
		CHECK_FALSE(game.logicThread());
		game.advance(0.2);
		CHECK(game.frame() == keptFrame + 21);
		game.setLogicThread(true);
		CHECK(game.logicThread());
		game.advance(0.2);
		game.waitIdle();
		CHECK(game.frame() == keptFrame + 22);
		// frames queued, none waited for: the destructor stops and joins the worker
		game.advance(1.0);
	}
	// the snapshot outlives its game (the reader owns it)
	CHECK(kept->objects.size() == copy.size());
}
