// OpenBFME unit tests, lane TOPPLE-1 (merge queue q39): the falling towers RotWK's War of the Ring Fornost map places (Fornost_FallingTower,
// StructureToppleUpdate RW 0x8A8BB4). Killed from the east, a tower falls west (building minus attacker, RW 0x8A92EA) and its crushing weapon hits a unit
// on the fall line (RW 0x8A950A / 0x8A8FA5); a unit the same distance off the line takes nothing. SKIPs loudly without the installs.

#include "Siege1TestUtil.h"

#include "GameLogic/Damage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/StructureToppleUpdate.h"

#include <cmath>
#include <string>

using namespace siege1test;

namespace
{
const char *const kFornost = "maps/map wor ang fornost/map wor ang fornost.map";
}

TEST_CASE("topple1 retail: a Fornost_FallingTower of map wor ang fornost killed from the east falls west and crushes its fall line")
{
	OPENBFME_REQUIRE_START(s);
	FortressGame g(*s, kFornost, false);
	g.frames(2);
	Player *men = g.ofFaction("FactionMen");
	Player *mordor = g.ofFaction("FactionMordor");
	REQUIRE(men);
	REQUIRE(mordor);
	int towers = 0;
	Object *tower = nullptr;
	for (Object *o = g.game->logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getTemplate()->getName() == "Fornost_FallingTower")
		{
			++towers;
			MESSAGE("tower " << o->getID() << " at " << o->getPosition()->x << ", " << o->getPosition()->y << ", " << o->getPosition()->z << " owner "
							 << (o->getControllingPlayer() ? o->getControllingPlayer()->getPlayerIndex() : -1));
			tower = tower ? tower : o;
		}
	}
	CHECK(towers == 2);
	REQUIRE(tower);
	StructureToppleUpdate *t = dynamic_cast<StructureToppleUpdate *>(tower->findModule("StructureToppleUpdate"));
	REQUIRE(t != nullptr);
	MESSAGE("data: accel " << t->data()->m_toppleAccelerationFactor << " integrity " << t->data()->m_structuralIntegrity << " decay " << t->data()->m_structuralDecay
						   << " weapon " << t->data()->m_crushingWeaponName);
	const Coord3D tp = *tower->getPosition();
	std::string error;
	Object *killer = g.game->createObject("GondorFighterHorde", men->getPlayerIndex(), Coord3D{ tp.x + 300.0f, tp.y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(killer, error);
	Object *onLine = g.game->createObject("MordorMountainTroll", mordor->getPlayerIndex(), Coord3D{ tp.x - 80.0f, tp.y, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(onLine, error);
	Object *offLine = g.game->createObject("MordorMountainTroll", mordor->getPlayerIndex(), Coord3D{ tp.x, tp.y + 400.0f, 0.0f }, 0.0f, &error);
	REQUIRE_MESSAGE(offLine, error);
	g.frames(1);
	const float on0 = healthOf(onLine), off0 = healthOf(offLine);
	const ObjectID onId = onLine->getID(), offId = offLine->getID(), towerId = tower->getID();
	// the map makes both towers indestructible (objectIndestructible, MapObjectLoop.cpp); the test lifts it the way a script's damage would need
	CHECK(tower->getBodyModule()->isIndestructible());
	tower->getBodyModule()->setIndestructible(false);
	DamageInfo d;
	d.m_input.m_sourceID = killer->getID();
	d.m_input.m_amount = 1.0e6f;
	d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	tower->attemptDamage(d);
	g.frames(1);
	REQUIRE(g.game->logic().findObjectByID(towerId) == tower);
	CHECK(tower->isEffectivelyDead());
	g.game->logic().destroyObject(killer);
	CHECK(t->state() == TOPPLESTATE_TOPPLING); // its MinToppleDelay (50 ms) has passed by the end of the kill's step
	CHECK(t->toppleDirectionX() <= -0.92f); // tolerance: the random perturbation in [-pi/8, pi/8] (RW 0x8A92EA)
	MESSAGE("direction " << t->toppleDirectionX() << ", " << t->toppleDirectionY());
	// the map's own defences (AmonSulTower2) shoot the trolls too: count only the damage whose source is the falling tower
	int f = 0, onCrush = -1, offCrush = -1, toppling = -1;
	float onLast = on0, offLast = off0;
	const auto crushed = [&](ObjectID id, float &last) {
		Object *o = g.game->logic().findObjectByID(id);
		const ActiveBody *ab = o ? dynamic_cast<const ActiveBody *>(o->getBodyModule()) : nullptr;
		const float h = o ? healthOf(o) : 0.0f;
		const bool hit = ab && h < last && ab->lastDamager() == towerId;
		last = h;
		return hit;
	};
	for (; f < 1200 && t->state() != TOPPLESTATE_DONE; ++f)
	{
		g.frames(1);
		if (toppling < 0 && t->state() >= TOPPLESTATE_TOPPLING)
		{
			toppling = f;
		}
		if (crushed(onId, onLast) && onCrush < 0)
		{
			onCrush = f;
		}
		if (crushed(offId, offLast) && offCrush < 0)
		{
			offCrush = f;
		}
	}
	MESSAGE("toppling at " << toppling << ", the tower's damage to the unit on the fall line at " << onCrush << ", off the line " << offCrush);
	CHECK(t->state() == TOPPLESTATE_DONE);
	CHECK(toppling == 0);
	CHECK(onCrush == 29); // measured twice (seed 9): the fall line reaches the unit 80 west of the tower
	CHECK(offCrush == -1);
	Object *on = g.game->logic().findObjectByID(onId);
	Object *off = g.game->logic().findObjectByID(offId);
	MESSAGE("done after " << f << " frames; on line " << on0 << " -> " << (on ? healthOf(on) : 0.0f) << ", off line " << off0 << " -> " << (off ? healthOf(off) : 0.0f));
}
