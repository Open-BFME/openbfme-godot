// OpenBFME unit tests. GPL-3.0.
// Lane PHYS-1 (review r8): the range gate of RW 0x6CC915 fireWeaponTemplate (0x6CCA2C .. 0x6CCAA1) on the synthetic combat world (CombatTestUtil.h, no retail data).
#include "doctest.h"
#include "CombatTestUtil.h"

#include <string>

using namespace combattest;

namespace
{
// LeechSwordsman: the Swordsman with SwordWeapon made a LeechRangeWeapon
std::string leechObjects()
{
	const std::string all = kCombatObjects;
	const size_t at = all.find("Object Swordsman\n");
	REQUIRE(at != std::string::npos);
	const size_t end = all.find("\nEnd\n", at);
	REQUIRE(end != std::string::npos);
	std::string obj = all.substr(at, end + 5 - at);
	obj.replace(0, std::string("Object Swordsman").size(), "Object LeechSwordsman");
	const size_t w = obj.find("Weapon = PRIMARY SwordWeapon");
	REQUIRE(w != std::string::npos);
	obj.replace(w, std::string("Weapon = PRIMARY SwordWeapon").size(), "Weapon = PRIMARY LeechSword");
	const std::string weapon = "Weapon LeechSword\n"
							   "  AttackRange = 11.5\n"
							   "  MeleeWeapon = Yes\n"
							   "  LeechRangeWeapon = Yes\n"
							   "  DelayBetweenShots = 1000\n"
							   "  PreAttackDelay = 600\n"
							   "  PreAttackType = PER_SHOT\n"
							   "  FiringDuration = 200\n"
							   "  DamageNugget\n"
							   "    Damage = 10\n"
							   "    Radius = 0.0\n"
							   "    DelayTime = 0\n"
							   "    DamageType = SLASH\n"
							   "    DeathType = NORMAL\n"
							   "  End\n"
							   "End\n";
	return weapon + obj;
}

struct Shot
{
	float before = 0.0f, after = 0.0f;
	bool spent = false;
	unsigned long long dropped = 0;
	std::uint32_t whenAgain = 0, frame = 0;
};

// Sol's probe: `attacker` stands 12 from a Swordsman and acquires it on its own; during the sword's wind-up (PRE_ATTACK) the victim walks `gap` away; the AI's fire state
// strikes when the wind-up ends (RW 0x74C4A7: no reach re-test), and the delivery's gate decides
Shot escape(const char *attacker, float gap)
{
	const std::string extra = leechObjects();
	CombatWorld w(extra.c_str());
	Object *a = w.unit(attacker, 'A', 300, 300);
	Object *v = w.unit("Swordsman", 'B', 312, 300);
	ObjectWeapons *ow = a->getWeapons();
	int guard = 0;
	while (ow->currentStatus() != WEAPON_PRE_ATTACK && guard++ < 10)
	{
		w.frames(1);
	}
	REQUIRE(ow->currentStatus() == WEAPON_PRE_ATTACK); // the swing started in reach
	v->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	Coord3D away = *v->getPosition();
	away.x += gap;
	v->setPosition(&away);
	Shot s;
	s.before = v->getBodyModule()->getHealth();
	const std::uint32_t last = ow->currentWeapon()->lastFireFrame();
	for (int f = 0; f < 10 && ow->currentWeapon()->lastFireFrame() == last; ++f)
	{
		w.frames(1);
	}
	s.frame = ow->currentWeapon()->lastFireFrame();
	s.spent = s.frame != last; // privateFireWeapon's tail ran: the shot is consumed
	s.whenAgain = ow->currentWeapon()->whenWeCanFireAgain();
	s.dropped = ow->stats().shotsOutOfRange;
	w.frames(2);
	s.after = v->getBodyModule()->getHealth();
	return s;
}
} // namespace

TEST_CASE("phys1 fire gate: a non-leech sword spends its shot on a victim that escaped 500 away and deals nothing (Sol's r8 probe)")
{
	const Shot s = escape("Swordsman", 500.0f);
	CHECK(s.spent);                      // privateFireWeapon's tail ran: the shot is consumed
	CHECK(s.whenAgain == s.frame + 5u);  // DelayBetweenShots 1000 ms: the cooldown starts as for a hit
	CHECK(s.dropped == 1);
	CHECK(s.after == s.before);          // 100 -> 100 (without the gate: 100 -> 95)
}

TEST_CASE("phys1 fire gate: in reach the same sword hits, and a LeechRangeWeapon still lands on the escaped victim")
{
	const Shot hit = escape("Swordsman", 0.0f);
	CHECK(hit.dropped == 0);
	CHECK(hit.after == hit.before - 5.0f); // 10 SLASH against PlainArmor's 50%
	const Shot leech = escape("LeechSwordsman", 500.0f);
	CHECK(leech.spent);
	CHECK(leech.dropped == 0); // the leech deadline (Weapon + 0x50, frame + FiringDuration) lies past the frame
	CHECK(leech.after == leech.before - 5.0f);
}
