// OpenBFME unit tests, lane SIEGEFALL-1 (merge queue q39): the retail MordorSiegeTower's slow death beside Helm's Deep's wall. GPL-3.0.
//
// The tower's four ClearanceTestingSlowDeathBehavior falls each have a killer window (DieMux RW 0x8D29A9) and a clearance box (RW 0x887191, the box at
// ClearanceGeometryOffset through the tower's transform, RW 0x88701C). Placed on flat ground beside a wall piece, the fall whose box reaches into the wall is
// refused and the roulette (RW 0x861712) repeats with the others. SKIPs loudly without the installs.

#include "Siege1TestUtil.h"

#include "GameLogic/Damage.h"
#include "GameLogic/Module/CombatModules.h"

#include <cmath>
#include <string>
#include <vector>

using namespace siege1test;

namespace
{
const char *const kHelmsDeep = "maps/map wor helms deep/map wor helms deep.map";

struct Fall
{
	std::string tag;
	float minAngle = 0.0f, maxAngle = 0.0f;
	bool clear = false; // canBeginSlowDeath before the death
	Coord3D box{};
};

std::vector<Fall> falls(Object *tower)
{
	std::vector<Fall> out;
	for (const std::unique_ptr<BehaviorModule> &m : tower->modules())
	{
		if (ClearanceTestingSlowDeathBehavior *c = dynamic_cast<ClearanceTestingSlowDeathBehavior *>(m.get()))
		{
			Fall f;
			f.tag = c->getModuleData()->getModuleTagName();
			f.minAngle = c->clearanceData()->m_dieMux.m_minKillerAngle;
			f.maxAngle = c->clearanceData()->m_dieMux.m_maxKillerAngle;
			f.clear = c->canBeginSlowDeath();
			f.box = c->getClearanceTestGeometryCenter();
			out.push_back(f);
		}
	}
	return out;
}

std::vector<std::string> begun(const Object *o)
{
	std::vector<std::string> out;
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		if (const SlowDeathBehavior *sd = dynamic_cast<const SlowDeathBehavior *>(m.get()))
		{
			if (sd->isSlowDeathActivated())
			{
				out.push_back(sd->getModuleData()->getModuleTagName());
			}
		}
	}
	return out;
}
} // namespace

TEST_CASE("siegefall1 retail: a MordorSiegeTower killed beside Helm's Deep's wall never falls into it")
{
	OPENBFME_REQUIRE_START(s);
	FortressGame g(*s, kHelmsDeep, false);
	g.frames(2);
	Object *gate = g.named("Main Gate");
	REQUIRE(gate);
	Player *me = g.ofFaction("FactionMordor");
	Player *men = g.ofFaction("FactionMen");
	REQUIRE(me);
	REQUIRE(men);
	const Waypoint *w = g.game->logic().terrain()->findWaypointByName("Player_2_Start");
	REQUIRE(w);
	Object *wall = nullptr;
	float best = 1e9f;
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o != gate && o->isKindOfName("WALK_ON_TOP_OF_WALL") && !o->isKindOfName("BLOCKING_GATE") && o->getBodyModule())
		{
			const float dd = dist2(*o->getPosition(), *gate->getPosition());
			if (dd < best && dd > 50.0f)
			{
				best = dd;
				wall = o;
			}
		}
	}
	REQUIRE(wall);
	const Coord3D wp = *wall->getPosition();
	float dx = w->location.x - wp.x, dy = w->location.y - wp.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	dx /= len;
	dy /= len;
	MESSAGE("wall " << wall->getTemplate()->getName() << " at " << wp.x << ", " << wp.y << "; outward " << dx << ", " << dy);
	const float wallAngle = std::atan2(-dy, -dx) * 57.2957795f; // the wall seen from the tower
	const float distances[] = { 60.0f, 80.0f, 100.0f, 120.0f, 160.0f };
	const float bearings[] = { 0.0f, 90.0f, 180.0f, 270.0f };
	for (float d : distances)
	{
		for (float b : bearings)
		{
			std::string error;
			const float killerDeg = wallAngle + b;
			const Coord3D at{ wp.x + dx * d, wp.y + dy * d, 0.0f };
			Object *tower = g.game->createObject("MordorSiegeTower", me->getPlayerIndex(), at, std::atan2(dy, dx), &error);
			REQUIRE_MESSAGE(tower, error);
			const float r = killerDeg / 57.2957795f;
			Object *killer = g.game->createObject("GondorFighterHorde", men->getPlayerIndex(), Coord3D{ at.x + 200.0f * std::cos(r), at.y + 200.0f * std::sin(r), 0.0f }, 0.0f, &error);
			REQUIRE_MESSAGE(killer, error);
			g.frames(1);
			const std::vector<Fall> fs = falls(tower);
			std::string line;
			for (const Fall &f : fs)
			{
				line += " " + f.tag + "[" + std::to_string((int)(f.minAngle * 57.2957795f)) + ".." + std::to_string((int)(f.maxAngle * 57.2957795f)) + "]" + (f.clear ? " clear" : " BLOCKED") +
					" box " + std::to_string((int)dist2(f.box, wp));
			}
			DamageInfo dm;
			dm.m_input.m_sourceID = killer->getID();
			dm.m_input.m_amount = 1.0e6f;
			dm.m_input.m_damageType = DAMAGE_UNRESISTABLE;
			tower->attemptDamage(dm);
			g.frames(1);
			const std::vector<std::string> b2 = begun(tower);
			MESSAGE("d " << d << " killer at wall+" << b << ":" << line << " -> " << (b2.empty() ? std::string("none") : b2[0]));
			REQUIRE(b2.size() == 1);
			CAPTURE(d);
			CAPTURE(b);
			// the fall toward the wall (ModuleTag_05_s) is refused at every distance, and no refused fall is ever chosen
			CHECK(fs.size() == 4);
			for (const Fall &f : fs)
			{
				CHECK((f.tag != "ModuleTag_05_s" || !f.clear));
				CHECK((b2[0] != f.tag || f.clear));
			}
			if (d == 60.0f || b == 0.0f)
			{
				CHECK(b2[0] == "ModuleTag_05"); // every box in the wall, or the killer on the wall's side: the plain SlowDeathBehavior
			}
			if (d == 160.0f)
			{
				const char *const expect[4] = { "ModuleTag_05", "ModuleTag_05_e", "ModuleTag_05_n", "ModuleTag_05_w" };
				CHECK(b2[0] == expect[(int)(b / 90.0f)]); // clear of the wall, the fall on the killer's side
			}
			g.game->logic().destroyObject(killer);
			g.game->logic().destroyObject(tower);
			g.frames(2);
		}
	}
}
