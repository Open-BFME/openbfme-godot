// OpenBFME. PLAY-2: force attack on the player's own structure with the retail data (the Devlog #4 report: archers ordered onto their own farm do not
// shoot; a trebuchet hits the farm and the farm takes no damage). Needs the retail install (SKIPs loudly without it).
//
// What retail does, from the binary and the data:
//   * the forced order is accepted for an own object (Object::getAbleToAttackSpecificObject RW 0x68D6A6, forced: no relationship test);
//   * a weapon can be chosen against it when one of its nuggets would deliver (RW 0x6C8A4E -> RW 0x6CDBF3 -> RW 0x6CB779: the nugget's shouldDeliver
//     with the FIRING weapon's template): GondorArcherBow / GondorTrebuchetRock carry no RadiusDamageAffects (the default ALLIES ENEMIES NEUTRALS),
//     so the arrows and the rocks are fired;
//   * the damage: RW 0x90D77C (DamageNugget::shouldDeliver's base, no exemption for the aimed-at victim, unlike ZH's dealDamageInternal) masks the
//     relationship (own player = ALLIES -> 2) with the WARHEAD's RadiusDamageAffects: GondorArcherBowWarhead is "ENEMIES NEUTRALS NOT_SIMILAR ;ALLIES"
//     (allies commented out in the data), so an arrow does nothing to an own structure; GondorTrebuchetRockWarhead affects ALLIES but its
//     DamageNugget carries "DamageScalar = 0% ALL ALLIES" (an own object matches ALLIES, ObjectFilter RW relationship row), so the rock does 0.
// So retail: both units fire, the farm takes no damage.

#include "HudTestUtil.h"

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AICommands.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Map/TerrainLogic.h"

using namespace hudtest;

namespace
{
unsigned long long shotsOf(Object *o)
{
	unsigned long long n = o->getWeapons() ? o->getWeapons()->stats().shotsFired : 0;
	if (o->getContain() && o->getContain()->getContainedItemsList())
	{
		for (Object *m : *o->getContain()->getContainedItemsList())
		{
			n += m->getWeapons() ? m->getWeapons()->stats().shotsFired : 0;
		}
	}
	return n;
}

void forceAttack(Rig &r, Object *attacker, Object *victim)
{
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, r.index());
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(attacker->getID());
	r.game->commands().append(sel);
	GameMessage m(MSG_DO_FORCE_ATTACK_OBJECT, r.index());
	m.appendObjectIDArgument(victim->getID());
	m.appendLocationArgument(*victim->getPosition());
	r.game->commands().append(m);
}
} // namespace

TEST_CASE("play2 force attack retail: archers and a trebuchet ordered onto their own farm choose no weapon (their warheads spare allies) and do not fire")
{
	if (!haveWorld("play2 force attack retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	for (const char *unit : { "GondorArcherHorde", "GondorTrebuchet" })
	{
		INFO(unit);
		Rig r(s);
		float mx = 0, my = 0;
		REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
		const Coord3D c0 = r.freeSpot(mx * 0.5f, my * 0.5f, 300.0f);
		Object *farm = r.make("GondorFarm", c0.x + 180.0f, c0.y);
		Object *attacker = r.make(unit, c0.x - 60.0f, c0.y);
		r.frame(5);
		const float before = farm->getBodyModule()->getHealth();
		forceAttack(r, attacker, farm);
		r.frame(1);
		CHECK(r.game->aiCommands().stats().forceAttacks == 1);
		CHECK(r.game->aiCommands().stats().attackOrdersAccepted == 1);
		r.frame(150); // 30 seconds
		const unsigned long long shots = shotsOf(attacker);
		MESSAGE(std::string(unit) << ": " << shots << " shots, farm health " << before << " -> " << farm->getBodyModule()->getHealth());
		// INTEG-2: RotWK's weapon choice skips a weapon that cannot damage the victim (RW 0x6C8BF2, lane DECOMP-1) and the attack state fails without one
		// (RW 0x74CF4F); the retail bows' and rocks' warheads leave ALLIES out, so neither unit fires
		CHECK(shots == 0);
		CHECK(farm->getBodyModule()->getHealth() == before);
	}
}

TEST_CASE("play2 force attack retail: Ctrl+click on the ground - an archer horde and a trebuchet fire at the spot until another order")
{
	if (!haveWorld("play2 force attack retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	for (const char *unit : { "GondorArcherHorde", "GondorTrebuchet" })
	{
		INFO(unit);
		Rig r(s);
		float mx = 0, my = 0;
		REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
		const Coord3D c0 = r.freeSpot(mx * 0.5f, my * 0.5f, 300.0f);
		Object *attacker = r.make(unit, c0.x - 60.0f, c0.y);
		r.frame(5);
		GameMessage sel(MSG_CREATE_SELECTED_GROUP, r.index());
		sel.appendBooleanArgument(true);
		sel.appendObjectIDArgument(attacker->getID());
		r.game->commands().append(sel);
		GameMessage m(MSG_DO_FORCE_ATTACK_GROUND, r.index());
		m.appendLocationArgument(Coord3D{ c0.x + 180.0f, c0.y, r.logic().getGroundHeight(c0.x + 180.0f, c0.y) });
		r.game->commands().append(m);
		r.frame(1);
		CHECK(r.game->aiCommands().stats().forceAttackGrounds == 1);
		REQUIRE(attacker->getAIUpdateInterface());
		CHECK(attacker->getAIUpdateInterface()->currentStateId() == (unsigned)AI_ATTACK_POSITION);
		r.frame(150);
		const unsigned long long shots = shotsOf(attacker);
		MESSAGE(std::string(unit) << ": " << shots << " shots at the ground");
		CHECK(shots > 0);
		CHECK(attacker->getAIUpdateInterface()->currentStateId() == (unsigned)AI_ATTACK_POSITION); // no shot limit: it keeps firing
	}
}
