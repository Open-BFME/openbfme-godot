// OpenBFME retail tests for lane PHYS-1 round 6: the per-unit melee attack machine (RW 0x744B71, AIAttackMelee.cpp). They run only when ROTWK_INSTALL and BFME2_INSTALL are
// set (otherwise SKIP): pure RotWK 2.01 + BFME2 1.06, the shared retail world of the HUD / combat / structure tests.
#include "doctest.h"
#include "StructureArena.h"

#include "Common/StateHash.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIAttackMelee.h"
#include "GameLogic/AI/AIMove.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/AIUpdate.h"

#include <cstdio>
#include <set>

using namespace structtest;

namespace
{
AIAttackState *attackStateOf(Object &o)
{
	AIUpdateInterface *ai = o.getAIUpdateInterface();
	return ai ? dynamic_cast<AIAttackState *>(ai->stateMachine().currentState()) : nullptr;
}

unsigned subStateOf(Object &o)
{
	AIAttackState *s = attackStateOf(o);
	return s && s->attackMachine() ? s->attackMachine()->currentStateId() : 0u;
}

size_t stopsWith(const std::vector<std::string> &stops, const char *prefix)
{
	size_t n = 0;
	for (const std::string &l : stops)
	{
		n += l.rfind(prefix, 0) == 0 ? 1u : 0u;
	}
	return n;
}
} // namespace

// RW 0x74CED6: kind 2 for a soldier with a MeleeWeapon attacking an object; a HORDE, a ranged weapon or a position target keeps another machine
TEST_CASE("phys1 retail: the attack kind of RW 0x74CED6 picks the melee machine for a melee soldier only")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Arena a(s, "FactionMen", "FactionMordor");
	Object *fighter = a.place("GondorFighter", 0, 400.0f, 500.0f);
	Object *archer = a.place("GondorArcher", 0, 400.0f, 540.0f);
	Object *horde = a.place("GondorFighterHorde", 0, 300.0f, 300.0f);
	Object *orc = a.place("MordorFighter", 1, 700.0f, 500.0f);
	a.logic.runLogicFrame();
	REQUIRE(fighter->getWeapons());
	REQUIRE(archer->getWeapons());
	CHECK(AttackUsesMeleeMachine(*fighter, orc, true));
	CHECK_FALSE(AttackUsesMeleeMachine(*fighter, nullptr, false)); // a position target
	CHECK_FALSE(AttackUsesMeleeMachine(*fighter, orc, false));
	CHECK_FALSE(AttackUsesMeleeMachine(*archer, orc, true));
	CHECK_FALSE(AttackUsesMeleeMachine(*horde, orc, true)); // the HORDE object runs the horde machine (kinds 6 / 7)
}

// a lone Gondor soldier attacks a lone orc 300 away: 0xE1 walks (the melee approach request), 0xE2 closes in, the aim / fire / wait states strike; the stop S-787 is reported
TEST_CASE("phys1 retail: a lone soldier approaches, engages and strikes through 0xE1 / 0xE2 / 0xE6 .. 0xE8")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Arena a(s, "FactionMen", "FactionMordor");
	Object *fighter = a.place("GondorFighter", 0, 400.0f, 500.0f);
	Object *orc = a.place("MordorFighter", 1, 700.0f, 500.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	orc->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	fighter->getAIUpdateInterface()->aiAttackObject(orc, CMD_FROM_PLAYER);
	std::set<unsigned> seen;
	int firstStrike = -1, approachFrames = 0;
	bool approachHadPath = false;
	const float h0 = orc->getBodyModule() ? orc->getBodyModule()->getHealth() : 0.0f;
	for (int f = 0; f < 300 && CombatQueries::isAlive(*orc); ++f)
	{
		a.logic.runLogicFrame();
		const unsigned sub = subStateOf(*fighter);
		seen.insert(sub);
		if (sub == MELEE_APPROACH)
		{
			++approachFrames;
			approachHadPath = approachHadPath || fighter->getAIUpdateInterface()->mover().path() != nullptr;
		}
		AIAttackState *st = attackStateOf(*fighter);
		if (firstStrike < 0 && st && st->shotsFired() > 0)
		{
			firstStrike = f;
		}
	}
	std::printf("  info: lone GondorFighter vs MordorFighter 300 away: states seen %zu, 0xE1 frames %d (path %d), first strike at frame %d, orc health %.0f -> %.0f\n", seen.size(),
		approachFrames, approachHadPath ? 1 : 0, firstStrike, h0, orc->getBodyModule() ? orc->getBodyModule()->getHealth() : 0.0f);
	CHECK(seen.count(MELEE_APPROACH) == 1);
	CHECK(approachHadPath);
	CHECK((seen.count(MELEE_ENGAGE) == 1 || seen.count(MELEE_AIM) == 1));
	CHECK(firstStrike > 0);
	CHECK(stopsWith(a.logic.report().stops, "[S-787] ") == 1);
}

// an ordinary horde member requests no path of its own in the melee machine: ordered at an enemy far beyond MeleeApproachTolerance + MeleeApproachDist, 0xE1 fails (RW 0x74EF4B: HORDE_MEMBER
// with obj + 0x27C) and the attack ends without a path of the member's own
TEST_CASE("phys1 retail: a horde member's melee machine fails beyond the approach gate instead of pathing")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Arena a(s, "FactionMen", "FactionMordor");
	Object *horde = a.place("GondorFighterHorde", 0, 400.0f, 500.0f);
	Object *orc = a.place("MordorFighter", 1, 800.0f, 500.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	REQUIRE(horde->getContain());
	REQUIRE(a.members(horde) > 0);
	orc->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	Object *member = horde->getContain()->getContainedItemsList()->front();
	REQUIRE(member->getContainedBy() == horde);
	AIUpdateInterface *ai = member->getAIUpdateInterface();
	ai->mover().destroyPath();
	ai->aiAttackObject(orc, CMD_FROM_PLAYER);
	a.logic.runLogicFrame();
	CHECK(attackStateOf(*member) == nullptr);   // the machine failed on entry: the attack state is gone
	CHECK(ai->mover().path() == nullptr);       // and no path of the member's own was asked for
	CHECK_FALSE(ai->mover().meleeApproachPending());
}

// review r6: the attack state hashes the retained melee states too: E5's retry timestamp (inactive while another state runs) changes the hash, the current state unchanged
TEST_CASE("phys1 retail: the attack state's hash covers the retained melee states (E5's retry timestamp)")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	SharedWorld &s = shared();
	Arena a(s, "FactionMen", "FactionMordor");
	Object *fighter = a.place("GondorFighter", 0, 400.0f, 500.0f);
	Object *orc = a.place("MordorFighter", 1, 700.0f, 500.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	orc->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	fighter->getAIUpdateInterface()->aiAttackObject(orc, CMD_FROM_PLAYER);
	for (int f = 0; f < 3; ++f)
	{
		a.logic.runLogicFrame();
	}
	AIAttackState *st = attackStateOf(*fighter);
	REQUIRE(st != nullptr);
	REQUIRE(st->usesMeleeMachine());
	AIStateMachine &m = *st->attackMachine();
	REQUIRE(m.currentStateId() != (unsigned)MELEE_REACQUIRE);
	auto hashOf = [&]() {
		StateHasher h;
		st->crc(h);
		return h.value();
	};
	const std::uint32_t before = hashOf();
	const unsigned cur = m.currentStateId();
	// run E5's entry out of turn: its retry timestamp is set, the machine's current state is untouched
	AIState *e5 = m.findState(MELEE_REACQUIRE);
	REQUIRE(e5 != nullptr);
	e5->onEnter();
	CHECK(m.currentStateId() == cur);
	CHECK(hashOf() != before);
}

namespace
{
struct EscapeShot
{
	bool leech = false;
	float before = 0.0f, after = 0.0f;
	unsigned long long outOfRange = 0;
	bool spent = false;
};

// `attacker` fires its current weapon once at `victim`, which has walked 500 away (Sol's r8 probe), then the delayed damage gets 30 frames
EscapeShot escapeShot(const char *factionA, const char *attackerName, const char *factionB, const char *victimName, float startGap)
{
	SharedWorld &s = shared();
	Arena a(s, factionA, factionB);
	Object *attacker = a.place(attackerName, 0, 400.0f, 500.0f);
	Object *victim = a.place(victimName, 1, 400.0f + startGap, 500.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	attacker->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	victim->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	ObjectWeapons *ow = attacker->getWeapons();
	REQUIRE(ow != nullptr);
	REQUIRE(ow->chooseBestWeaponForTarget(victim, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER));
	EscapeShot out;
	out.leech = ow->currentWeapon()->getTemplate()->m_leechRangeWeapon;
	REQUIRE(ow->isWithinAttackRange(*victim)); // in reach where the swing would have started
	const Coord3D away{ 900.0f + startGap, 500.0f, victim->getPosition()->z };
	victim->setPosition(&away);
	out.before = victim->getBodyModule()->getHealth();
	const unsigned frame = a.logic.getFrame();
	ow->fireCurrentWeapon(victim, nullptr);
	out.spent = ow->currentWeapon()->lastFireFrame() == frame; // privateFireWeapon's tail ran: the shot is consumed
	out.outOfRange = ow->stats().shotsOutOfRange;
	for (int f = 0; f < 30; ++f)
	{
		a.logic.runLogicFrame();
	}
	out.after = victim->getBodyModule()->getHealth();
	return out;
}
} // namespace

// review r8: RW 0x6CC915's range gate (0x6CCA2C .. 0x6CCAA1). The fire state no longer re-tests the reach (RW 0x74C4A7), so the delivery decides; AngmarSword is a
// LeechRangeWeapon: its leech deadline (Weapon + 0x50) lies past the frame and the swing lands on an axe thrower that went 500 away (the non-leech case, where the shot
// is spent and nothing lands, is test_phys1_fire_gate.cpp: retail's soldiers' swords are leech weapons)
TEST_CASE("phys1 retail: the delivery range gate lets the Angmar Dunedain's leech sword land on a victim that escaped")
{
	if (!hudtest::haveWorld("phys1 retail"))
	{
		return;
	}
	const EscapeShot leech = escapeShot("FactionAngmar", "AngmarDarkDunedain", "FactionDwarves", "DwarvenAxeThrower", 12.0f);
	std::printf("  info: AngmarDarkDunedain at an axe thrower that went 500 away: leech %d, health %.0f -> %.0f, spent %d, dropped %llu\n", leech.leech ? 1 : 0, leech.before,
		leech.after, leech.spent ? 1 : 0, leech.outOfRange);
	CHECK(leech.leech);
	CHECK(leech.spent);
	CHECK(leech.outOfRange == 0);
	CHECK(leech.after < leech.before);
}
