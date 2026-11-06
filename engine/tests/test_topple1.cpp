// OpenBFME. GPL-3.0.
// Lane TOPPLE-1: StructureToppleUpdate (GameLogic/Module/StructureToppleUpdate.h; RW 0x8A92EA beginStructureTopple, RW 0x8A96A0 update, RW 0x8A950A / 0x8A8FA5 the crushing,
// RW 0x8A8F1B xfer). A synthetic tower with Fornost_FallingTower's toppling values (MinToppleDelay 50, bursts 1500 .. 2500, ToppleAccelerationFactor 0.25,
// StructuralIntegrity 0.75, StructuralDecay 1.0) and a test crushing weapon; the retail check runs only with ROTWK_INSTALL and BFME2_INSTALL (otherwise SKIP).
#include "doctest.h"
#include "CombatTestUtil.h"
#include "StructureArena.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/StructureToppleUpdate.h"
#include "GameLogic/StructureStops.h"

#include <cstring>
#include <string>
#include <vector>

using namespace combattest;

namespace
{
const char kTowers[] =
	"Weapon TopplerCrushWeapon\n"
	"  RadiusDamageAffects = ALLIES ENEMIES NEUTRALS\n"
	"  DamageNugget\n"
	"    Damage = 30\n"
	"    Radius = 40.0\n"
	"    DamageType = PIERCE\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"Object FallingTower\n"
	"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 15\n"
	"  GeometryMinorRadius = 15\n"
	"  GeometryHeight = 100\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = StoneArmor\n"
	"  End\n"
	"  Body = StructureBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = StructureToppleUpdate ModuleTag_Topple\n"
	"    MinToppleDelay = 50\n"
	"    MaxToppleDelay = 50\n"
	"    MinToppleBurstDelay = 1500\n"
	"    MaxToppleBurstDelay = 2500\n"
	"    StructuralIntegrity = 0.75\n"
	"    StructuralDecay = 1.0\n"
	"    ToppleAccelerationFactor = 0.25\n"
	"    CrushingWeaponName = TopplerCrushWeapon\n"
	"  End\n"
	"End\n";

const float kPi8 = 3.14159265359f / 8.0f;

DamageInfo lethal(const Object *source)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	d.m_input.m_amount = 100000.0f;
	d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	return d;
}

StructureToppleUpdate *toppleOf(Object *o)
{
	StructureToppleUpdate *t = dynamic_cast<StructureToppleUpdate *>(o->findModule("StructureToppleUpdate"));
	REQUIRE(t != nullptr);
	return t;
}

int runUntil(CombatWorld &w, StructureToppleUpdate *t, StructureToppleStateType state, int limit = 400)
{
	int n = 0;
	while (t->state() != state && n < limit)
	{
		w.logic->runLogicFrame();
		++n;
	}
	REQUIRE_MESSAGE(t->state() == state, "state " << (int)t->state() << " after " << n << " frames");
	return n;
}

std::vector<GameLogicRandom::Call> toppleDraws(const std::vector<GameLogicRandom::Call> &log, size_t from = 0)
{
	std::vector<GameLogicRandom::Call> out;
	for (size_t i = from; i < log.size(); ++i)
	{
		if (log[i].file == "StructureToppleUpdate.cpp")
		{
			out.push_back(log[i]);
		}
	}
	return out;
}
} // namespace

TEST_CASE("topple1: the module data reads RotWK's table 0xC69680 (durations in frames, the retail defaults where a row is absent)")
{
	CombatWorld w(kTowers);
	Object *tower = w.unit("FallingTower", 'B', 300, 300);
	const StructureToppleUpdateModuleData *d = toppleOf(tower)->data();
	CHECK(d->m_minToppleDelay == 1); // 50 ms at 5 logic frames a second -> ceil(0.25)
	CHECK(d->m_maxToppleDelay == 1);
	CHECK(d->m_minToppleBurstDelay == 8); // ceil(7.5)
	CHECK(d->m_maxToppleBurstDelay == 13); // ceil(12.5)
	CHECK(d->m_toppleAccelerationFactor == 0.25f);
	CHECK(d->m_structuralIntegrity == 0.75f);
	CHECK(d->m_structuralDecay == 1.0f);
	CHECK(d->m_crushingWeaponName == "TopplerCrushWeapon");
	CHECK(d->m_damageFXTypes == 0xFFFFFFFFu);
	CHECK(d->m_forceToppleAngle == -9.87654f);
	CHECK(d->m_oclCount[STPHASE_INITIAL] == 1);
	StructureToppleUpdate *t = toppleOf(tower);
	CHECK(t->state() == TOPPLESTATE_STANDING);
	CHECK(t->accumulatedAngle() == 0.001f); // RW 0x8A8BB4
	CHECK(t->nextBurstFrame() == 0xFFFFFFFFu);
	CHECK(t->buildingHeight() == 100.0f);
}

TEST_CASE("topple1 (a): killed from the east the tower falls west (building minus attacker, RW 0x8A92EA), and ends upright facing west")
{
	CombatWorld w(kTowers);
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("Swordsman", 'A', 400, 300);
	Object *tower = w.unit("FallingTower", 'B', 300, 300);
	StructureToppleUpdate *t = toppleOf(tower);
	DamageInfo d = lethal(a);
	tower->attemptDamage(d);
	REQUIRE(t->state() == TOPPLESTATE_WAITINGFORTOPPLESTART);
	// (300 - 400, 0): angle pi, then a perturbation in [-pi/8, pi/8]
	// tolerance: the direction carries a random perturbation in [-pi/8, pi/8]; cos(pi +- pi/8) <= -0.9238
	CHECK(t->toppleDirectionX() <= -0.92f);
	CHECK(std::fabs(t->toppleDirectionY()) <= std::sin(kPi8) + 1e-4f); // tolerance: |sin| of that perturbation, plus the float rounding
	// the delay burst lies 0.9 * the average radius (15) towards the fall
	// tolerance: the expected value is recomputed in plain float, the runtime rounds every step to PC24
	CHECK(t->delayBurstLocation().x == doctest::Approx(300.0f + 13.5f * t->toppleDirectionX()).epsilon(1e-4));
	runUntil(w, t, TOPPLESTATE_TOPPLING);
	for (int i = 0; i < 12; ++i)
	{
		w.logic->runLogicFrame();
	}
	REQUIRE(t->state() == TOPPLESTATE_TOPPLING);
	CHECK(t->accumulatedAngle() > 0.001f); // tolerance: the fall has started (the angle leaves its 0.001 seed); the exact path is (d)'s
	CHECK(tower->getBasis()[2] < 0.0f); // tolerance: a sign check; the object's Z axis leans to -x: the top falls west
	runUntil(w, t, TOPPLESTATE_DONE);
	CHECK(t->accumulatedAngle() == 3.14159265359f / 2.0f);
	CHECK(tower->getBasis()[8] == 1.0f); // RW 0x8A8C86: the orientation set again, upright
	// tolerance: the orientation is pi plus the random perturbation (|.| >= pi - pi/8)
	CHECK(std::fabs(tower->getOrientation()) > 3.14159265359f - kPi8 - 1e-3f); // turned to the topple direction (about pi)
	const int post = CombatNames::modelCondition("POST_COLLAPSE");
	CHECK(tower->testModelCondition(post));
}

TEST_CASE("topple1 (b): a unit on the fall line takes the crushing weapon, one off the line does not")
{
	CombatWorld w(kTowers);
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("Swordsman", 'A', 400, 300);
	Object *tower = w.unit("FallingTower", 'B', 300, 300);
	Object *onLine = w.unit("Swordsman", 'B', 240, 300);
	Object *offLine = w.unit("Swordsman", 'B', 240, 420);
	const ObjectID onId = onLine->getID();
	const float onBefore = w.health(onLine);
	const float offBefore = w.health(offLine);
	const float aBefore = w.health(a);
	StructureToppleUpdate *t = toppleOf(tower);
	DamageInfo d = lethal(a);
	tower->attemptDamage(d);
	runUntil(w, t, TOPPLESTATE_DONE);
	CHECK(t->lastCrushedLocation() >= 100.0f); // tolerance: the last line lies at or past the height; the lines reached the top's distance (the height)
	Object *on = w.byId(onId);
	// tolerance: how many 25-unit lines reach the unit depends on the fall's frames; any crushing damage proves the hit
	CHECK((on == nullptr || w.health(on) < onBefore));
	CHECK(w.health(offLine) == offBefore);
	CHECK(w.health(a) == aBefore); // the killer to the east is behind the fall
}

TEST_CASE("topple1 (c): the logic random draws of a topple in retail order (RW 0x8A92EA lines 132 / 164 / 181, RW 0x8A96A0 lines 230 / 279)")
{
	CombatWorld w(kTowers);
	w.combat().setAutoAcquireEnabled(false);
	Object *a = w.unit("Swordsman", 'A', 400, 300);
	Object *tower = w.unit("FallingTower", 'B', 300, 300);
	StructureToppleUpdate *t = toppleOf(tower);
	w.logic->random().enableCallLog(true);
	DamageInfo d = lethal(a);
	tower->attemptDamage(d);
	{
		const std::vector<GameLogicRandom::Call> draws = toppleDraws(w.logic->random().callLog());
		REQUIRE(draws.size() == 3);
		CHECK((!draws[0].real && draws[0].line == 132 && draws[0].lo == 1 && draws[0].hi == 1)); // the topple delay
		CHECK((draws[1].real && draws[1].line == 164 && draws[1].rlo == -kPi8 && draws[1].rhi == kPi8)); // the perturbation of the attacker's direction
		CHECK((!draws[2].real && draws[2].line == 181 && draws[2].lo == 8 && draws[2].hi == 13)); // the first burst
	}
	// every later draw is a burst re-roll on the frame the burst is due: 230 while waiting, 279 while falling
	int bursts = 0;
	for (int f = 0; f < 400 && t->state() != TOPPLESTATE_DONE; ++f)
	{
		const size_t before = w.logic->random().callLog().size();
		const StructureToppleStateType state = t->state();
		const unsigned due = t->nextBurstFrame();
		const unsigned now = w.logic->getFrame() + 1;
		w.logic->runLogicFrame();
		const std::vector<GameLogicRandom::Call> draws = toppleDraws(w.logic->random().callLog(), before);
		if (state != TOPPLESTATE_WAITINGFORDONE && now >= due)
		{
			REQUIRE_MESSAGE(draws.size() == 1, "frame " << now);
			CHECK(draws[0].line == (state == TOPPLESTATE_WAITINGFORTOPPLESTART ? 230 : 279));
			CHECK((draws[0].lo == 8 && draws[0].hi == 13));
			++bursts;
		}
		else
		{
			CHECK_MESSAGE(draws.empty(), "frame " << now);
		}
	}
	CHECK(t->state() == TOPPLESTATE_DONE);
	MESSAGE("topple1: bursts during the topple " << bursts);
}

TEST_CASE("topple1: with no attacker the direction is one random real in [0, 2 pi] (line 151)")
{
	CombatWorld w(kTowers);
	Object *tower = w.unit("FallingTower", 'B', 300, 300);
	w.logic->random().enableCallLog(true);
	DamageInfo d = lethal(nullptr);
	tower->attemptDamage(d);
	const std::vector<GameLogicRandom::Call> draws = toppleDraws(w.logic->random().callLog());
	REQUIRE(draws.size() == 3);
	CHECK((draws[1].real && draws[1].line == 151 && draws[1].rlo == 0.0f && draws[1].rhi == 2.0f * 3.14159265359f));
}

TEST_CASE("topple1 (d): a save in the middle of the fall loads into another topple and resumes it (RW 0x8A8F1B)")
{
	CombatWorld w1(kTowers);
	CombatWorld w2(kTowers);
	w1.combat().setAutoAcquireEnabled(false);
	w2.combat().setAutoAcquireEnabled(false);
	Object *a1 = w1.unit("Swordsman", 'A', 400, 300);
	Object *a2 = w2.unit("Swordsman", 'A', 300, 400); // killed from the north: the same draws, another direction
	Object *t1o = w1.unit("FallingTower", 'B', 300, 300);
	Object *t2o = w2.unit("FallingTower", 'B', 300, 300);
	StructureToppleUpdate *t1 = toppleOf(t1o);
	StructureToppleUpdate *t2 = toppleOf(t2o);
	DamageInfo d1 = lethal(a1);
	DamageInfo d2 = lethal(a2);
	t1o->attemptDamage(d1);
	t2o->attemptDamage(d2);
	REQUIRE(w1.logic->getFrame() == w2.logic->getFrame());
	runUntil(w1, t1, TOPPLESTATE_TOPPLING);
	for (int i = 0; i < 15; ++i)
	{
		w1.logic->runLogicFrame();
	}
	while (w2.logic->getFrame() < w1.logic->getFrame())
	{
		w2.logic->runLogicFrame();
	}
	REQUIRE(t1->state() == TOPPLESTATE_TOPPLING);
	REQUIRE(t1->toppleDirectionX() != t2->toppleDirectionX());
	XferSave s;
	t1->xfer(s);
	{
		XferLoad l(s.data());
		t2->xfer(l);
	}
	XferSave again;
	t2->xfer(again);
	CHECK(again.data() == s.data());
	for (int f = 0; f < 400 && t1->state() != TOPPLESTATE_DONE; ++f)
	{
		w1.logic->runLogicFrame();
		w2.logic->runLogicFrame();
		REQUIRE_MESSAGE(t2->state() == t1->state(), "frame " << w1.logic->getFrame());
		CHECK(t2->accumulatedAngle() == t1->accumulatedAngle());
		CHECK(t2->toppleVelocity() == t1->toppleVelocity());
		CHECK(t2->structuralIntegrity() == t1->structuralIntegrity());
		CHECK(t2->lastCrushedLocation() == t1->lastCrushedLocation());
		CHECK(t2->nextBurstFrame() == t1->nextBurstFrame());
		CHECK(t2->toppleFrame() == t1->toppleFrame());
	}
	CHECK(t2->state() == TOPPLESTATE_DONE);
	CHECK(t2->toppleDirectionX() == t1->toppleDirectionX());
	CHECK(t2o->getOrientation() == t1o->getOrientation()); // the done turn uses the loaded direction
}

TEST_CASE("topple1: S-340 no longer says the toppling has no runtime and S-345 names what is not exact")
{
	int s340 = 0, s345 = 0;
	for (const std::string &line : StructureStops::lines())
	{
		if (line.rfind("[S-340]", 0) == 0)
		{
			++s340;
			CHECK(line.find("StructureToppleUpdate has no runtime") == std::string::npos);
		}
		if (line.rfind("[S-345]", 0) == 0)
		{
			++s345;
			CHECK(line.find("RW 0x8A92EA") != std::string::npos);
		}
	}
	CHECK(s340 == 1);
	CHECK(s345 == 1);
}

TEST_CASE("topple1 retail: Fornost_FallingTower killed from the east falls west and crushes its fall line")
{
	if (!hudtest::haveWorld("topple1 retail"))
	{
		return;
	}
	structtest::Arena a(hudtest::shared(), "FactionMen", "FactionMen");
	Object *killer = a.place("GondorFighterHorde", 0, 640.0f, 500.0f);
	Object *tower = a.place("Fornost_FallingTower", 1, 500.0f, 500.0f);
	REQUIRE(killer != nullptr);
	REQUIRE(tower != nullptr);
	StructureToppleUpdate *t = dynamic_cast<StructureToppleUpdate *>(tower->findModule("StructureToppleUpdate"));
	REQUIRE(t != nullptr);
	CHECK(t->data()->m_toppleAccelerationFactor == 0.25f);
	CHECK(t->data()->m_structuralIntegrity == 0.75f);
	CHECK(t->data()->m_structuralDecay == 1.0f);
	CHECK(!t->data()->m_crushingWeaponName.empty());
	DamageInfo d = lethal(killer);
	tower->attemptDamage(d);
	REQUIRE(t->state() == TOPPLESTATE_WAITINGFORTOPPLESTART);
	CHECK(t->toppleDirectionX() <= -0.92f); // tolerance: the random perturbation in [-pi/8, pi/8]
	for (int f = 0; f < 2000 && t->state() != TOPPLESTATE_DONE; ++f)
	{
		a.logic.runLogicFrame();
	}
	CHECK(t->state() == TOPPLESTATE_DONE);
}
