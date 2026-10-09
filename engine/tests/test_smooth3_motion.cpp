// OpenBFME. Lane SMOOTH-3 (owner feedback 2026-10-06, "the battalions jerk around", second round): the walk / run cycle against the drawn ground speed.
//
// RW 0x4B67D4 (called from the draw update RW 0x4BF560 at 0x4BFAEA): an animation whose AnimationState names a Distance plays at
// natural duration / (Distance / speed * 200 ms), the speed being the object's current locomotor speed per logic frame (RW 0x68B34C), else its horde's
// (RW 0x693A1A(0)). The port had the routine (W3DScriptedModelDraw::syncSpeedToMovement) but the live drawables gave it no speed: every Distance
// animation played at its natural rate, the feet sliding over the ground. The snapshot now carries the speed (ObjectSnapshot::moveSpeed).
//
// Measured per unit kind (retail data, "map mp fall back 4p", 144 render frames per second through LiveGame::present): for every drawable whose current
// animation names a Distance, the ground it covered over the loops its animation played, divided by Distance: 1 when the feet stay planted.

#include "doctest.h"
#include "HudTestUtil.h"

#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace
{
struct Slide
{
	double ground = 0.0;      ///< world distance covered while a Distance animation played
	double groundLoops = 0.0; ///< the same in loops' worth of Distance
	double loops = 0.0;       ///< animation loops played meanwhile
	int drawables = 0;
	double ratio() const { return loops > 0.0 ? groundLoops / loops : 0.0; }
};

struct Track
{
	Coord3D pos;
	float frame = -1.0f;
	const HAnimClass *anim = nullptr;
};

// moves `templateName` 400 units east and measures its (and its members') Distance animations
Slide measureSlide(hudtest::Rig &rig, const char *templateName, const Coord3D &at)
{
	std::string err;
	const int ours = rig.local->getPlayerIndex();
	Object *o = rig.game->createObject(templateName, ours, at, 0.0f, &err);
	REQUIRE_MESSAGE(o, err);
	std::vector<ObjectID> ids{ o->getID() };
	for (int i = 0; i < 3; ++i)
	{
		rig.game->advance(0.2);
	}
	if (o->getContain() && o->getContain()->getContainedItemsList())
	{
		for (const Object *m : *o->getContain()->getContainedItemsList())
		{
			ids.push_back(m->getID());
		}
	}
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, ours);
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(o->getID());
	rig.game->commands().append(sel);
	GameMessage mv(MSG_DO_MOVETO, ours);
	mv.appendLocationArgument(Coord3D{ at.x + 400.0f, at.y, 0.0f });
	rig.game->commands().append(mv);
	Slide s;
	std::map<ObjectID, Track> last;
	std::map<ObjectID, bool> counted;
	for (int i = 0; i < 144 * 6; ++i)
	{
		rig.game->advance(1.0 / 144.0);
		for (ObjectID id : ids)
		{
			const Drawable *d = rig.game->drawables().findByObject(id);
			if (!d)
			{
				continue;
			}
			for (const DrawEntry &e : d->entries())
			{
				if (!e.draw)
				{
					continue;
				}
				const W3DDrawFrame f = e.draw->frame();
				const AnimationStateInfo *st = e.draw->currentAnimationState();
				const int which = e.draw->currentAnimationIndex();
				if (f.trackCount < 1 || !f.tracks[0].anim || !st || which < 0 || which >= (int)st->animations.size())
				{
					last.erase(id);
					continue;
				}
				const float distance = st->animations[(size_t)which].distance;
				Track now{ *d->getPosition(), f.tracks[0].frame, f.tracks[0].anim };
				auto it = last.find(id);
				if (distance > 0.0f && it != last.end() && it->second.anim == now.anim)
				{
					const double frames = (double)now.anim->Get_Num_Frames();
					double df = (double)now.frame - (double)it->second.frame;
					if (df < 0.0)
					{
						df += frames; // a loop wrapped
					}
					const double step = std::hypot((double)now.pos.x - it->second.pos.x, (double)now.pos.y - it->second.pos.y);
					s.ground += step;
					s.groundLoops += step / (double)distance;
					s.loops += df / frames;
					if (!counted[id])
					{
						counted[id] = true;
						++s.drawables;
					}
				}
				last[id] = now;
				break; // the first model draw of the drawable
			}
		}
	}
	return s;
}
} // namespace

TEST_CASE("smooth3 retail: the walk / run cycle of a Distance animation keeps pace with the ground (RW 0x4B67D4: no foot sliding)")
{
	if (!hudtest::haveWorld("smooth3 foot sliding"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared());
	struct Kind
	{
		const char *name;
		Coord3D at;
	};
	// cavalry and infantry with Distance animations in the retail data (gondorcavalry.ini, arnorfighter.ini), a troll and a mumak
	const Kind kinds[] = { { "GondorKnightHorde", { 1150, 950, 0 } }, { "ArnorFighterHorde", { 1150, 1100, 0 } }, { "MordorMountainTroll", { 1150, 1250, 0 } },
		{ "MordorMumakil", { 1150, 1450, 0 } } };
	int withDistance = 0;
	for (const Kind &k : kinds)
	{
		const Slide s = measureSlide(rig, k.name, k.at);
		MESSAGE("SMOOTH3 slide " << std::string(k.name) << " drawables_with_Distance=" << s.drawables << " ground=" << s.ground << " loops=" << s.loops << " ratio=" << s.ratio());
		if (s.drawables > 0 && s.loops > 1.0)
		{
			++withDistance;
			INFO(k.name);
			// the feet keep pace: about one loop per Distance of ground (before SMOOTH-3 the animations ran at their natural rate: GondorKnightHorde 1.33,
			// ArnorFighterHorde 1.08 Distances of ground per loop). Below 1 where a member covers less ground than its locomotor speed (retail's speed source:
			// a member closing on its slot, the snapshot's speed one logic frame behind the drawn pose): 0.91 and 0.96 measured
			CHECK(s.ratio() > 0.85);
			CHECK(s.ratio() < 1.03);
		}
	}
	CHECK(withDistance >= 2);
}

// RW 0x5E98D6 (Locomotor::locomotorMoveTowardsAngle): a member's angle goal (the hub's near arm turning it to the formation's facing) turns at the locomotor's rate,
// 2 pi / TurnTime frames (RW 0x5E372D); the port set the heading at once (a member spun up to 180 degrees in one logic frame on reaching its slot).
TEST_CASE("smooth3 retail: horde members turn to their slot's facing at the locomotor's turn rate, never at once")
{
	if (!hudtest::haveWorld("smooth3 member turns"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared());
	std::string err;
	const int ours = rig.local->getPlayerIndex();
	Object *h = rig.game->createObject("GondorKnightHorde", ours, Coord3D{ 1200.0f, 1100.0f, 0.0f }, 0.0f, &err);
	REQUIRE_MESSAGE(h, err);
	for (int i = 0; i < 3; ++i)
	{
		rig.game->advance(0.2);
	}
	std::vector<ObjectID> members;
	for (const Object *m : *h->getContain()->getContainedItemsList())
	{
		members.push_back(m->getID());
	}
	REQUIRE(members.size() >= 5);
	// a march, then a turn of the whole horde (the members re-slot and turn to the new facing)
	const Coord3D orders[] = { { 1450.0f, 1150.0f, 0.0f }, { 1450.0f, 1450.0f, 0.0f } };
	std::map<ObjectID, float> last;
	double worstOverRate = 0.0, worstAngleGoal = 0.0;
	int angleTurns = 0, angleGoalTurns = 0;
	for (const Coord3D &to : orders)
	{
		GameMessage sel(MSG_CREATE_SELECTED_GROUP, ours);
		sel.appendBooleanArgument(true);
		sel.appendObjectIDArgument(h->getID());
		rig.game->commands().append(sel);
		GameMessage mv(MSG_DO_MOVETO, ours);
		mv.appendLocationArgument(to);
		rig.game->commands().append(mv);
		for (int f = 0; f < 60; ++f)
		{
			rig.game->advance(0.2);
			for (ObjectID id : members)
			{
				const Object *m = rig.game->logic().findObjectByID(id);
				if (!m || !m->getAIUpdateInterface() || !m->getAIUpdateInterface()->curLocomotor())
				{
					continue;
				}
				const float a = m->getOrientation();
				auto it = last.find(id);
				if (it != last.end())
				{
					double d = std::fabs((double)a - (double)it->second);
					if (d > 3.14159265358979)
					{
						d = 2.0 * 3.14159265358979 - d;
					}
					const LocomotorTemplate &t = m->getAIUpdateInterface()->curLocomotor()->getTemplate();
					const double rate = 2.0 * 3.14159265358979 / (double)(t.m_turnTime > 0 ? t.m_turnTime : 1);
					angleTurns += d > 1e-4 ? 1 : 0;
					if (m->getAIUpdateInterface()->mover().goalType() == AIGOAL_ANGLE)
					{
						angleGoalTurns += d > 1e-4 ? 1 : 0;
						worstAngleGoal = std::max(worstAngleGoal, d / rate);
					}
					worstOverRate = std::max(worstOverRate, d / rate);
				}
				last[id] = a;
			}
		}
	}
	MESSAGE("SMOOTH3 member turns: " << angleTurns << " turning member-frames (" << angleGoalTurns << " under the angle goal), the largest turn of one frame is "
									 << worstOverRate << " x the turn rate (" << worstAngleGoal << " under the angle goal)");
	CHECK(angleTurns > 20);
	CHECK(angleGoalTurns >= 1); // the goal is cleared in the frame its turn ends: the frames seen under it are the ones still turning
	CHECK(worstAngleGoal <= 1.0001);
	// at most two mover calls a frame: RW doLocomotor's goal type 4 undoes an invalid straight step by position only (RW 0x70C201) and moves along the new path
	// with a second RW 0x5E8865 call; no member turns faster than that (with the heading set at once a member turned up to pi in one frame)
	CHECK(worstOverRate <= 2.0001);
}
