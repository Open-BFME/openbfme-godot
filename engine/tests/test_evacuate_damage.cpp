// OpenBFME unit tests. GPL-3.0.
// EvacuateDamage (RW 0x8BF12E): the occupants of a building leave it when one weapon hits it; the damage of one type is counted. Synthetic data
// (combattest::CombatWorld), no retail files.

#include "doctest.h"

#include "CombatTestUtil.h"

#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/EvacuateDamage.h"
#include "GameLogic/Object/Object.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
const char kHeadThrower[] =
	"Weapon HeadThrower\n"
	"  AttackRange = 400\n"
	"  WeaponSpeed = 100\n"
	"  DelayBetweenShots = 4000\n"
	"  DamageNugget\n"
	"    Damage = 1\n"
	"    Radius = 0.0\n"
	"    DamageType = SIEGE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n";

std::string thrower()
{
	return "Object Thrower\n"
		   "  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
		   "  Geometry = CYLINDER\n"
		   "  GeometryMajorRadius = 8\n"
		   "  GeometryMinorRadius = 8\n"
		   "  GeometryHeight = 20\n"
		   "  WeaponSet\n"
		   "    Conditions = None\n"
		   "    Weapon = PRIMARY HeadThrower\n"
		   "  End\n"
		   "  Body = ActiveBody ModuleTag_Body\n"
		   "    MaxHealth = 100\n"
		   "  End\n"
		   "End\n";
}

std::string owner(const std::string &name, const std::string &kinds, const std::string &fields, bool contain, bool ai)
{
	return "Object " + name + "\n  KindOf = " + kinds + "\n"
		   "  Geometry = BOX\n"
		   "  GeometryMajorRadius = 20\n"
		   "  GeometryMinorRadius = 20\n"
		   "  GeometryHeight = 60\n"
		   "  ArmorSet\n"
		   "    Conditions = None\n"
		   "    Armor = PlainArmor\n" // SLASH 50%
		   "  End\n"
		   "  Body = ActiveBody ModuleTag_Body\n"
		   "    MaxHealth = 1000\n"
		   "  End\n" +
		(contain ? "  Behavior = GarrisonContain ModuleTag_Contain\n"
				   "    ContainMax = 10\n"
				   "    DamagePercentToUnits = 0%\n"
				   "  End\n"
				 : "") +
		(ai ? "  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n" : "") + "  Behavior = EvacuateDamage ModuleTag_evacuateDamage\n" + fields + "  End\n" +
		"  LocomotorSet\n"
		"    Locomotor = WalkerLoco\n"
		"    Condition = SET_NORMAL\n"
		"    Speed = 55\n"
		"  End\n"
		"End\n";
}

const char kTrack[] =
	"    WeaponThatCausesEvacuation = HeadThrower\n"
	"    DamageTypeToTrack = SLASH\n"
	"    DamageToPanicThreshold = 20%\n"
	"    TrackingTimeSpan = 1000\n"; // 5 frames

struct EvacWorld : CombatWorld
{
	EvacWorld()
	{
		combat().setAutoAcquireEnabled(false);
		std::string err = w.load(kHeadThrower, INI_LOAD_OVERWRITE, "evacweapon.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		const std::string building = "STRUCTURE SELECTABLE IMMOBILE";
		err = w.load(thrower() + owner("Tower", building, kTrack, true, true) + owner("NoAITower", building, kTrack, true, false) +
				owner("PlainTower", building, "", true, true) + owner("CaseTower", building, "    WeaponThatCausesEvacuation = headthrower\n", true, true) +
				owner("NoContain", building, kTrack, false, true) + owner("Runner", "INFANTRY SELECTABLE", kTrack, false, true),
			INI_LOAD_OVERWRITE, "evacobjects.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	static EvacuateDamage *moduleOf(Object *o)
	{
		EvacuateDamage *m = dynamic_cast<EvacuateDamage *>(o->findModule("EvacuateDamage"));
		REQUIRE(m);
		return m;
	}
	// a hit of `amount` (before the armor) whose source is `source` (its current weapon decides)
	static void hit(Object *victim, const Object *source, DamageType type, float amount)
	{
		DamageInfo info;
		info.m_input.m_sourceID = source ? source->getID() : INVALID_ID;
		info.m_input.m_damageType = type;
		info.m_input.m_amount = amount;
		victim->attemptDamage(info);
	}
	static bool orderedOut(Object *rider, const Object *container, unsigned state)
	{
		AIUpdateInterface *ai = rider->getAIUpdateInterface();
		REQUIRE(ai);
		return ai->stateMachine().currentStateId() == state && ai->lastCommandSource() == CMD_FROM_AI && ai->stateMachine().goalObjectID() == container->getID();
	}
};
} // namespace

TEST_CASE("EvacuateDamage: the module data's defaults (RW 0x8BEE59) and fields (RW 0xC70A88)")
{
	EvacWorld e;
	const EvacuateDamageModuleData *d = e.moduleOf(e.unit("PlainTower", 'A', 300, 300))->data();
	CHECK(d->m_weaponThatCausesEvacuation.empty());
	CHECK(d->m_damageTypeToTrack == DAMAGE_PIERCE);
	CHECK(d->m_damageToPanicThreshold == 0.1f);
	CHECK(d->m_trackingTimeSpan == 6u);
	const EvacuateDamageModuleData *t = e.moduleOf(e.unit("Tower", 'A', 500, 300))->data();
	CHECK(t->m_weaponThatCausesEvacuation == "HeadThrower");
	CHECK(t->m_damageTypeToTrack == DAMAGE_SLASH);
	CHECK(t->m_damageToPanicThreshold == 0.19999999f); // 20% (RW 0x42EEFA: 20 x 0.01, PC24)
	CHECK(t->m_trackingTimeSpan == 5u);
}

TEST_CASE("EvacuateDamage: a hit of the weapon orders every occupant out with CMD_FROM_AI (RW 0x8BF178 .. 0x8BF1EA), a horde as a horde")
{
	EvacWorld e;
	Object *tower = e.unit("Tower", 'A', 300, 300);
	Object *attacker = e.unit("Thrower", 'B', 500, 300);
	Object *first = e.unit("Archer", 'A', 330, 300);
	Object *second = e.unit("Archer", 'A', 330, 340);
	Object *horde = e.unit("SwordHorde", 'A', 300, 500);
	e.frames(3);
	ContainModuleInterface *c = tower->getContain();
	REQUIRE(c);
	REQUIRE(c->addToContain(first));
	REQUIRE(c->addToContain(second));
	REQUIRE(c->addToContain(horde));
	REQUIRE(first->getContainedBy() == tower);
	REQUIRE(horde->getContainedBy() == tower);

	e.hit(tower, attacker, DAMAGE_SIEGE, 10.0f);
	CHECK(e.orderedOut(first, tower, (unsigned)AI_EXIT));
	CHECK(e.orderedOut(second, tower, (unsigned)AI_EXIT));
	CHECK(e.orderedOut(horde, tower, (unsigned)AI_HORDE_EXIT));
	CHECK(e.moduleOf(tower)->hits().empty());

	e.frames(40);
	CHECK(first->getContainedBy() == nullptr);
	CHECK(second->getContainedBy() == nullptr);
	CHECK(horde->getContainedBy() == nullptr);
	CHECK(c->getContainCount() == 0u);
	CHECK(e.health(tower) == 990.0f);
}

TEST_CASE("EvacuateDamage: the weapon is the source's current one, its name is compared exactly; another weapon, no source or a source without a weapon orders nobody")
{
	EvacWorld e;
	Object *thrower = e.unit("Thrower", 'B', 500, 300);
	Object *archer = e.unit("Archer", 'B', 500, 340);
	Object *unarmed = e.unit("Runner", 'B', 500, 380);
	REQUIRE(thrower->getWeapons()->currentWeapon());
	for (const char *name : { "Tower", "CaseTower", "PlainTower" })
	{
		Object *tower = e.unit(name, 'A', 300, 300);
		Object *rider = e.unit("Archer", 'A', 330, 300);
		REQUIRE(tower->getContain()->addToContain(rider));
		const unsigned before = rider->getAIUpdateInterface()->stateMachine().currentStateId();
		REQUIRE(before != (unsigned)AI_EXIT);
		e.hit(tower, archer, DAMAGE_SIEGE, 10.0f);
		e.hit(tower, unarmed, DAMAGE_SIEGE, 10.0f);
		e.hit(tower, nullptr, DAMAGE_SIEGE, 10.0f);
		CHECK(rider->getAIUpdateInterface()->stateMachine().currentStateId() == before);
		e.hit(tower, thrower, DAMAGE_SIEGE, 10.0f);
		CHECK(e.orderedOut(rider, tower, (unsigned)AI_EXIT) == (std::string(name) == "Tower"));
		tower->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
		e.frames(2);
	}
	// an object without a contain (RW 0x8BF183)
	Object *plain = e.unit("NoContain", 'A', 700, 300);
	e.hit(plain, thrower, DAMAGE_SIEGE, 10.0f);
	CHECK(e.health(plain) == 990.0f);
}

TEST_CASE("EvacuateDamage: the tracked hits (RW 0x8BF09E): one damage type, the amount before the armor, forgotten after TrackingTimeSpan (RW 0x8BEF0A)")
{
	EvacWorld e;
	Object *tower = e.unit("NoAITower", 'A', 300, 300);
	Object *archer = e.unit("Archer", 'B', 500, 300);
	Object *thrower = e.unit("Thrower", 'B', 500, 340);
	EvacuateDamage *m = e.moduleOf(tower);
	e.hit(tower, archer, DAMAGE_PIERCE, 10.0f); // another type
	e.hit(tower, nullptr, DAMAGE_SLASH, 10.0f); // no source (RW 0x8BF14E)
	e.hit(tower, thrower, DAMAGE_SLASH, 10.0f); // the weapon that evacuates does not count (RW 0x8BF176)
	CHECK(m->hits().empty());
	const float health = e.health(tower);
	e.hit(tower, archer, DAMAGE_SLASH, 60.0f);
	CHECK(e.health(tower) == health - 30.0f);
	REQUIRE(m->hits().size() == 1u);
	const int first = (int)e.logic->getFrame();
	CHECK(m->hits().front().amount == 60.0f);
	CHECK(m->hits().front().frame == first);
	// 4 frames later the first still counts, 5 frames later (frame <= now - 5) it goes
	e.frames(4);
	e.hit(tower, archer, DAMAGE_SLASH, 1.0f);
	CHECK(m->hits().size() == 2u);
	e.frames(1);
	e.hit(tower, archer, DAMAGE_SLASH, 2.0f);
	REQUIRE(m->hits().size() == 2u);
	CHECK(m->hits().front().amount == 1.0f);
	CHECK(m->hits().back().amount == 2.0f);
	// the threshold without an AI: the list stays (RW 0x8BF080)
	e.hit(tower, archer, DAMAGE_SLASH, 500.0f);
	CHECK(m->hits().size() == 3u);
}

TEST_CASE("EvacuateDamage: at the threshold the list is cleared and the AI told to flee (RW 0x8BF076): nothing for a building, counted for an object that can move")
{
	EvacWorld e;
	Object *archer = e.unit("Archer", 'B', 500, 300);
	auto flees = [](Object *o) {
		unsigned n = 0;
		for (const auto &kv : o->getAIUpdateInterface()->unportedCommands())
		{
			n += kv.first.find("aiFleeFromObject") == 0 ? kv.second : 0u;
		}
		return n;
	};
	for (const char *name : { "Tower", "Runner" })
	{
		Object *o = e.unit(name, 'A', 300, 300);
		EvacuateDamage *m = e.moduleOf(o);
		// 20% of 1000: 199 is below, 200 reaches it
		e.hit(o, archer, DAMAGE_SLASH, 150.0f);
		e.hit(o, archer, DAMAGE_SLASH, 49.0f);
		CHECK(m->hits().size() == 2u);
		CHECK(flees(o) == 0u);
		e.hit(o, archer, DAMAGE_SLASH, 1.0f);
		CHECK(m->hits().empty());
		CHECK(flees(o) == (std::string(name) == "Runner" ? 1u : 0u));
		o->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL);
		e.frames(2);
	}
}

TEST_CASE("EvacuateDamage: the snapshot and the hash take the tracked hits; an empty list adds nothing to the hash")
{
	EvacWorld e;
	Object *tower = e.unit("NoAITower", 'A', 300, 300);
	Object *archer = e.unit("Archer", 'B', 500, 300);
	EvacuateDamage *m = e.moduleOf(tower);
	auto hash = [&] {
		StateHasher h;
		m->crc(h);
		return h.value();
	};
	const auto empty = hash();
	CHECK(empty == StateHasher().value());
	e.hit(tower, archer, DAMAGE_SLASH, 7.0f);
	e.frames(1);
	e.hit(tower, archer, DAMAGE_SLASH, 8.0f);
	const auto two = hash();
	CHECK(two != empty);
	XferSave saved;
	m->xfer(saved);
	e.frames(10);
	e.hit(tower, archer, DAMAGE_SLASH, 9.0f);
	REQUIRE(m->hits().size() == 1u);
	CHECK(hash() != two);
	XferLoad load(saved.data());
	m->xfer(load);
	REQUIRE(m->hits().size() == 2u);
	CHECK(m->hits().front().amount == 7.0f);
	CHECK(m->hits().back().frame == m->hits().front().frame + 1);
	CHECK(hash() == two);
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad shortLoad(truncated);
	CHECK_THROWS_AS(m->xfer(shortLoad), XferException);
}
