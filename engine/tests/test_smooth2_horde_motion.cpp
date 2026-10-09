// OpenBFME. Lane SMOOTH-2 (owner feedback 2026-10-06: "the infantry + horse battalions jerked around a lot and seemed to 'lag'"): the horde motion probe.
//
// Retail data, "map mp fall back 4p": a GondorFighterHorde, a cavalry horde (RohanRohirrimHorde) and a MordorFighterHorde march, turn 90 degrees, charge
// into each other, fight and re-form. Every logic frame the completed frame's snapshot (GameClient/LogicSnapshot, what the drawables are posed from) is
// recorded for every horde and member; the drawn path is then the render side's pose (RenderInterpolation::retailPose) sampled at 144 render frames per
// second on the 5 Hz clock, exactly as LiveGame::present poses the drawables when the worker is on time.
//
// Measured (per member, the logic side and the drawn side):
//   * logic reversals: consecutive logic steps pointing against each other (dot < 0, both steps longer than 0.5);
//   * drawn reversals: consecutive render-frame steps pointing against each other while the member moves (both steps longer than 0.02, i.e. a speed of 3 / s);
//   * the largest drawn step of one render frame over the member's largest logic step / (render frames per logic frame) (1 = evenly spread);
//   * the drawn speed's jump at a logic frame boundary: the render step just after a boundary over the one just before it.
// With SMOOTH2_DUMP=<file> the records are written as CSV for tools/smooth/horde_motion.py (the before / after numbers of the lane report).

#include "doctest.h"
#include "HudTestUtil.h"

#include "GameClient/LogicSnapshot.h"
#include "GameClient/RenderInterpolation.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Object/Object.h"
#include "Common/Thing/ThingTemplate.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct HordeRun
{
	std::vector<std::shared_ptr<const LogicSnapshot>> snaps;
	std::map<ObjectID, ObjectID> hordeOf;   ///< member -> its horde (the last frame it was a member)
	std::map<ObjectID, std::string> label;  ///< horde -> template name
	std::vector<std::pair<UnsignedInt, std::string>> phases;
};

struct Metrics
{
	int members = 0;
	int logicReversals = 0;
	int drawnReversals = 0;
	int drawnSamples = 0;
	double worstSpread = 0.0;   ///< the largest drawn step / (largest logic step / render frames per logic frame)
	double worstBoundaryJump = 0.0;
	double meanBoundaryJump = 0.0;
	int boundarySamples = 0;
	double worstTurnPerFrame = 0.0; ///< radians in one render frame
};

double len2(double x, double y)
{
	return std::sqrt(x * x + y * y);
}

void recordMembers(LiveGame &game, HordeRun &run, const std::vector<ObjectID> &hordes)
{
	for (ObjectID h : hordes)
	{
		Object *o = game.logic().findObjectByID(h);
		if (!o || !o->getContain())
		{
			continue;
		}
		run.label[h] = o->getTemplate()->getName();
		if (const auto *list = o->getContain()->getContainedItemsList())
		{
			for (const Object *m : *list)
			{
				run.hordeOf[m->getID()] = h;
			}
		}
	}
}

// the drawn poses of one object at `perFrame` render frames per logic frame, from the first to the last snapshot
using PoseFn = RenderInterpolation::Pose (*)(const ObjectSnapshot &, UnsignedInt, double);
Metrics measure(const HordeRun &run, int perFrame, ObjectID only, PoseFn pose)
{
	Metrics m;
	std::map<ObjectID, std::vector<RenderInterpolation::Pose>> drawn;
	std::map<ObjectID, std::vector<Coord3D>> logic;
	// lane MOVE-2 r2: from the first order on (the "march" phase): the Gondor horde is created with some slots on the cliff north-east of (1200, 1100) (pathfinder
	// cells x >= 121, y >= 111 are CLIFF), and RotWK's slot destination test RW 0x6F0889 sends those members to the horde's cell, which they reach by the
	// doLocomotor fallback RW 0x669AF1 (put on the goal) at frame 2: a creation pop, not motion of the manoeuvres this probe measures
	const UnsignedInt firstFrame = run.phases.empty() ? 0u : run.phases.front().first;
	for (size_t k = 0; k < run.snaps.size(); ++k)
	{
		const LogicSnapshot &s = *run.snaps[k];
		if (s.frame < firstFrame)
		{
			continue;
		}
		for (const auto &hm : run.hordeOf)
		{
			if (only != INVALID_ID && run.hordeOf.at(hm.first) != only)
			{
				continue;
			}
			const ObjectSnapshot *rec = s.find(hm.first);
			if (!rec)
			{
				continue;
			}
			logic[hm.first].push_back(rec->position);
			for (int i = 0; i < perFrame; ++i)
			{
				drawn[hm.first].push_back(pose(*rec, s.frame, (double)i / (double)perFrame));
			}
		}
	}
	double jumpSum = 0.0;
	for (const auto &d : drawn)
	{
		++m.members;
		const std::vector<Coord3D> &lp = logic[d.first];
		double maxLogic = 0.0;
		for (size_t k = 2; k < lp.size(); ++k)
		{
			const double ax = lp[k - 1].x - lp[k - 2].x, ay = lp[k - 1].y - lp[k - 2].y;
			const double bx = lp[k].x - lp[k - 1].x, by = lp[k].y - lp[k - 1].y;
			maxLogic = std::max(maxLogic, len2(bx, by));
			if (len2(ax, ay) > 0.5 && len2(bx, by) > 0.5 && ax * bx + ay * by < 0.0)
			{
				++m.logicReversals;
			}
		}
		const std::vector<RenderInterpolation::Pose> &p = d.second;
		double maxDrawn = 0.0;
		for (size_t i = 2; i < p.size(); ++i)
		{
			const double ax = p[i - 1].position.x - p[i - 2].position.x, ay = p[i - 1].position.y - p[i - 2].position.y;
			const double bx = p[i].position.x - p[i - 1].position.x, by = p[i].position.y - p[i - 1].position.y;
			maxDrawn = std::max(maxDrawn, len2(bx, by));
			if (len2(ax, ay) > 0.02 && len2(bx, by) > 0.02)
			{
				++m.drawnSamples;
				if (ax * bx + ay * by < 0.0)
				{
					++m.drawnReversals;
				}
				if (i % (size_t)perFrame == 0)
				{
					const double j = len2(bx, by) / len2(ax, ay);
					const double r = std::max(j, 1.0 / j);
					m.worstBoundaryJump = std::max(m.worstBoundaryJump, r);
					jumpSum += r;
					++m.boundarySamples;
				}
			}
			const float da = RenderInterpolation::lerpAngle(p[i - 1].angle, p[i].angle, 1.0f) - p[i - 1].angle;
			m.worstTurnPerFrame = std::max(m.worstTurnPerFrame, (double)std::fabs(da));
		}
		if (maxLogic > 0.5)
		{
			m.worstSpread = std::max(m.worstSpread, maxDrawn / (maxLogic / (double)perFrame));
		}
	}
	m.meanBoundaryJump = m.boundarySamples ? jumpSum / m.boundarySamples : 0.0;
	return m;
}

void dump(const HordeRun &run, const char *path)
{
	FILE *f = std::fopen(path, "w");
	REQUIRE(f != nullptr);
	std::fprintf(f, "frame,horde,label,id,x,y,z,angle,rx,ry,rangle,px,py,nx,ny,hasNext,lastMoved,hasRecorded\n");
	for (const auto &sp : run.snaps)
	{
		std::vector<std::pair<ObjectID, ObjectID>> rows; // (id, horde)
		for (const auto &h : run.label)
		{
			rows.push_back({ h.first, h.first });
		}
		for (const auto &hm : run.hordeOf)
		{
			rows.push_back(hm);
		}
		for (const auto &row : rows)
		{
			const ObjectSnapshot *r = sp->find(row.first);
			if (!r)
			{
				continue;
			}
			std::fprintf(f, "%u,%u,%s,%u,%.4f,%.4f,%.4f,%.5f,%.4f,%.4f,%.5f,%.4f,%.4f,%.4f,%.4f,%d,%u,%d\n", sp->frame, row.second, run.label.at(row.second).c_str(),
				row.first, r->position.x, r->position.y, r->position.z, r->angle, r->recordedPos.x, r->recordedPos.y, r->recordedAngle, r->previousPos.x,
				r->previousPos.y, r->nextPos.x, r->nextPos.y, r->hasNext ? 1 : 0, r->lastMovedFrame, r->hasRecorded ? 1 : 0);
		}
	}
	for (const auto &ph : run.phases)
	{
		std::fprintf(f, "#phase,%u,%s\n", ph.first, ph.second.c_str());
	}
	std::fclose(f);
}

void order(LiveGame &game, int player, const std::vector<ObjectID> &ids, int type, const Coord3D &where)
{
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, player);
	sel.appendBooleanArgument(true);
	for (ObjectID id : ids)
	{
		sel.appendObjectIDArgument(id);
	}
	game.commands().append(sel);
	GameMessage mv(type, player);
	mv.appendLocationArgument(where);
	game.commands().append(mv);
}

// the manoeuvres: march east, turn 90 degrees north, charge each other, fight, re-form (both sides walk away)
HordeRun runManoeuvres(hudtest::Rig &rig)
{
	HordeRun run;
	const int ours = rig.local->getPlayerIndex();
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	const int theirs = enemy->getPlayerIndex();
	std::string err;
	std::vector<ObjectID> a, b;
	Object *gondor = rig.game->createObject("GondorFighterHorde", ours, Coord3D{ 1200.0f, 1100.0f, 0 }, 0.0f, &err);
	REQUIRE_MESSAGE(gondor, err);
	Object *riders = rig.game->createObject("RohanRohirrimHorde", ours, Coord3D{ 1200.0f, 1220.0f, 0 }, 0.0f, &err);
	REQUIRE_MESSAGE(riders, err);
	Object *orcs = rig.game->createObject("MordorFighterHorde", theirs, Coord3D{ 1300.0f, 1700.0f, 0 }, 3.14159f, &err);
	REQUIRE_MESSAGE(orcs, err);
	a = { gondor->getID(), riders->getID() };
	b = { orcs->getID() };
	const std::vector<ObjectID> all = { gondor->getID(), riders->getID(), orcs->getID() };
	auto step = [&](int frames) {
		for (int i = 0; i < frames; ++i)
		{
			rig.game->advance(0.2);
			recordMembers(*rig.game, run, all);
			run.snaps.push_back(rig.game->latestSnapshot());
		}
	};
	step(5);
	run.phases.push_back({ run.snaps.back()->frame, "march" });
	order(*rig.game, ours, a, MSG_DO_MOVETO, Coord3D{ 1550.0f, 1160.0f, 0 });
	order(*rig.game, theirs, b, MSG_DO_MOVETO, Coord3D{ 1650.0f, 1700.0f, 0 });
	step(40);
	run.phases.push_back({ run.snaps.back()->frame, "turn" });
	order(*rig.game, ours, a, MSG_DO_MOVETO, Coord3D{ 1550.0f, 1400.0f, 0 });
	order(*rig.game, theirs, b, MSG_DO_MOVETO, Coord3D{ 1650.0f, 1550.0f, 0 });
	step(30);
	run.phases.push_back({ run.snaps.back()->frame, "charge" });
	order(*rig.game, ours, a, MSG_DO_ATTACKMOVETO, Coord3D{ 1650.0f, 1550.0f, 0 });
	order(*rig.game, theirs, b, MSG_DO_ATTACKMOVETO, Coord3D{ 1550.0f, 1400.0f, 0 });
	step(60);
	run.phases.push_back({ run.snaps.back()->frame, "reform" });
	order(*rig.game, ours, a, MSG_DO_MOVETO, Coord3D{ 1350.0f, 1250.0f, 0 });
	order(*rig.game, theirs, b, MSG_DO_MOVETO, Coord3D{ 1800.0f, 1800.0f, 0 });
	step(40);
	return run;
}
} // namespace

TEST_CASE("smooth2 retail: hordes and cavalry march, turn, charge, fight and re-form without drawn reversals or jumps")
{
	if (!hudtest::haveWorld("smooth2 horde motion"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared());
	const HordeRun run = runManoeuvres(rig);
	REQUIRE(run.snaps.size() > 100);
	if (const char *path = std::getenv("SMOOTH2_DUMP"))
	{
		dump(run, path);
	}
	for (const auto &h : run.label)
	{
		const Metrics retail = measure(run, 29, h.first, &RenderInterpolation::retailPose); // 144 fps: 28.8 render frames per logic frame
		const Metrics smooth = measure(run, 29, h.first, &RenderInterpolation::smoothPose);
		for (const Metrics *m : { &retail, &smooth })
		{
			MESSAGE("SMOOTH2 " << (m == &retail ? "retail " : "smooth ") << h.second << " members=" << m->members << " logic_reversals=" << m->logicReversals
								<< " drawn_reversals=" << m->drawnReversals << "/" << m->drawnSamples << " worst_spread=" << m->worstSpread << " boundary_jump mean="
								<< m->meanBoundaryJump << " worst=" << m->worstBoundaryJump << " worst_turn_per_frame=" << m->worstTurnPerFrame);
		}
		INFO(h.second);
		REQUIRE(smooth.members > 0);
		REQUIRE(smooth.drawnSamples > 10000);
		// the drawn path turns back only where the logic itself turned back (before SMOOTH-2: 958 .. 1586 drawn reversals per horde from the stale pending position)
		CHECK(smooth.drawnReversals <= smooth.logicReversals);
		// no render frame carries much more than its share of a logic step (before: 7.8 .. 18.7 times the share)
		CHECK(smooth.worstSpread < 1.5);
		// no 5 Hz pulse: the drawn speed just after a logic frame boundary is the speed just before it (retail's own curve: about 2.5 .. 2.9 times on average)
		CHECK(smooth.meanBoundaryJump < 1.1);
		CHECK(smooth.worstBoundaryJump < 1.5);
		// lane EXIT-1: the 5 Hz pulse of retailPose came from the pending position the locomotor's movers leave on a member (2.5 .. 2.9 times before). In RotWK a
		// busy member or one with an explicit goal moves through the member update RW 0x66C748, whose transform write (RW 0x70BA76) records no pending position:
		// retail's own pose of these members has no pulse either (measured 1.12)
		CHECK(retail.meanBoundaryJump < 1.5);
	}
	// S-812 resolved: a member moving without a path has the pre-move position as its pending position (RW 0x5E5B4E after RW 0x62618F's clear), never a point
	// of an earlier frame. Before SMOOTH-2 most moving member records had a pending position more than 2.5 steps from the current one.
	int moving = 0, far = 0;
	for (const auto &sp : run.snaps)
	{
		for (const auto &hm : run.hordeOf)
		{
			const ObjectSnapshot *r = sp->find(hm.first);
			if (!r || !r->hasNext)
			{
				continue;
			}
			const double step = len2(r->position.x - r->recordedPos.x, r->position.y - r->recordedPos.y);
			if (step < 0.5)
			{
				continue;
			}
			++moving;
			far += len2(r->nextPos.x - r->position.x, r->nextPos.y - r->position.y) > 2.5 * step + 1.0 ? 1 : 0;
		}
	}
	// lane EXIT-1: most member steps now come from the member update (no pending position, RW 0x70BA76): 79 moving records with one measured, 1000+ before
	CHECK(moving > 20);
	CHECK(far == 0);
}

TEST_CASE("smooth2: smoothPose: even steps are linear, a stop holds without overshoot, a look-ahead is used, a jump before the segment never runs backwards")
{
	ObjectSnapshot s;
	s.hasRecorded = true;
	s.previousPos = Coord3D{ 0, 0, 0 };
	s.recordedPos = Coord3D{ 10, 0, 0 };
	s.position = Coord3D{ 20, 0, 0 };
	s.lastMovedFrame = 50;
	const UnsignedInt frame = 50;
	// a member without a look-ahead (retail's pending is the pre-move position, P1): extrapolated, linear at an even pace
	s.hasNext = true;
	s.nextPos = s.recordedPos;
	for (double a : { 0.0, 0.25, 0.5, 0.75, 1.0 })
	{
		CHECK(RenderInterpolation::smoothPose(s, frame, a).position.x == doctest::Approx(10.0 + 10.0 * a));
	}
	// retail's curve of the same record decelerates: three quarters of the frame cover more than three quarters of the step
	CHECK(RenderInterpolation::retailPose(s, frame, 0.75).position.x > 18.0f);
	// a stop: P1 == P2 holds at P2 (retail's Catmull-Rom first overshoots forward and comes back)
	ObjectSnapshot stop = s;
	stop.previousPos = Coord3D{ 10, 0, 0 };
	stop.recordedPos = Coord3D{ 20, 0, 0 };
	stop.nextPos = stop.recordedPos;
	for (double a : { 0.1, 0.33, 0.6, 0.9 })
	{
		CHECK(RenderInterpolation::smoothPose(stop, frame, a).position.x == doctest::Approx(20.0));
	}
	CHECK(RenderInterpolation::retailPose(stop, frame, 0.33).position.x > 20.5f);
	// a genuine look-ahead (a path point beyond P2, RW 0x5E5A87): the segment arrives at P2 already heading towards it
	ObjectSnapshot path = s;
	path.nextPos = Coord3D{ 25, 10, 0 };
	const Coord3D endA = RenderInterpolation::smoothPose(path, frame, 0.95).position, endB = RenderInterpolation::smoothPose(path, frame, 1.0).position;
	CHECK(endB.x == doctest::Approx(20.0));
	CHECK(endB.y - endA.y > 0.05f);
	// a teleport before the segment (P0 far behind): the start tangent is limited, the path never runs back or past P2
	ObjectSnapshot jump = s;
	jump.previousPos = Coord3D{ -200, 0, 0 };
	float last = 10.0f;
	for (int i = 1; i <= 20; ++i)
	{
		const float x = RenderInterpolation::smoothPose(jump, frame, i / 20.0).position.x;
		CHECK(x >= last);
		CHECK(x <= 20.0001f);
		last = x;
	}
	// a logic reversal (P0 ahead of P1): the backward start tangent is dropped, the segment runs forward only
	ObjectSnapshot back = s;
	back.previousPos = Coord3D{ 30, 0, 0 };
	last = 10.0f;
	for (int i = 1; i <= 20; ++i)
	{
		const float x = RenderInterpolation::smoothPose(back, frame, i / 20.0).position.x;
		CHECK(x >= last - 1e-4f);
		last = x;
	}
	// stale (the last move two frames old): the current transform, as retail
	ObjectSnapshot stale = s;
	stale.lastMovedFrame = 40;
	CHECK(RenderInterpolation::smoothPose(stale, frame, 0.3).position.x == doctest::Approx(20.0));
}

TEST_CASE("smooth2 retail: a worker slower than the presentation delay never freezes and jumps the presented time; the delay grows until it keeps up")
{
	if (!hudtest::haveWorld("smooth2 presentation pacing"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	ArchiveW3DFileSource source(*sh.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*sh.world, *sh.mount->fs, assets, sh.options);
	LiveGame::Options o;
	o.mapName = "map mp fall back 4p";
	o.seed = 4711;
	o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
	o.logicThread = true;
	o.presentationDelaySeconds = 0.03;
	o.workerFrameDelayMs = 70; // every frame publishes 70 ms after it is due: 40 ms later than the default delay allows
	std::string err;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	game.setLogicThread(true);
	REQUIRE(game.logicThread());
	double lastTime = -1.0, worstStep = 0.0;
	int backwards = 0;
	unsigned long long heldAtTwoSeconds = 0;
	const double dt = 1.0 / 144.0;
	auto next = std::chrono::steady_clock::now();
	for (int i = 0; i < 144 * 4; ++i)
	{
		next += std::chrono::microseconds(6944);
		std::this_thread::sleep_until(next);
		game.advance(dt);
		const std::shared_ptr<const LogicSnapshot> shown = game.presentedSnapshot();
		REQUIRE(shown.get() != nullptr);
		const double t = (double)shown->frame + game.presentedAlpha();
		if (lastTime >= 0.0)
		{
			backwards += t < lastTime - 1e-9 ? 1 : 0;
			if (i > 144) // after the first second (the load's catch-up)
			{
				worstStep = std::max(worstStep, t - lastTime);
			}
		}
		lastTime = t;
		if (i == 144 * 2)
		{
			heldAtTwoSeconds = game.heldPresentations();
		}
	}
	game.waitIdle();
	MESSAGE("SMOOTH2 pacing: held " << game.heldPresentations() << " (" << heldAtTwoSeconds << " in the first two seconds) extra delay "
									<< game.presentationExtraDelaySeconds() * 1000.0 << " ms worst presented step " << worstStep << " frames");
	CHECK(backwards == 0);
	// one render step is 5 / 144 = 0.035 frames; easing allows a quarter more. SMOOTH-1 jumped to the clock (about 0.2 frames here) after every late frame
	CHECK(worstStep < 0.06);
	CHECK(game.presentationExtraDelaySeconds() > 0.03);
	CHECK(game.heldPresentations() - heldAtTwoSeconds < heldAtTwoSeconds); // the holds die out once the delay covers the slow worker
}
