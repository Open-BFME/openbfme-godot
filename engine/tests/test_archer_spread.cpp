// OpenBFME. ARCHER-1 tests: a ranged horde's HordeAttackNugget spreads the members' fire over the target horde (HordeContain::attackTargetNow RW 0x875221 and the
// member pick RW 0x86FA87: the nearest member after a logic random factor GameLogicRandomValueReal(0.66, 1.33) on every distance). Synthetic data, no retail files.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/SimMath.h"

#include <cmath>
#include <set>

using namespace combattest;

namespace
{
// a horde of six Dummies (no weapon, 100 health each): the target
const char kDummyHorde[] =
	"Object DummyHorde\n"
	"  KindOf = HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE\n"
	"  VisionRange = 200\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:Dummy Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    RankInfo = RankNumber:2 UnitType:Dummy Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20\n"
	"    InitialPayload = Dummy 6\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";

Object *goalOf(Object *m)
{
	AIUpdateInterface *ai = m->getAIUpdateInterface();
	const AIStateMachine *sm = ai ? ai->stateMachineOrNull() : nullptr;
	return ai && ai->isAttacking() && sm ? sm->goalObject() : nullptr;
}

bool isMemberOf(CombatWorld &w, Object *horde, Object *o)
{
	for (Object *m : w.membersOf(horde))
	{
		if (m == o)
		{
			return true;
		}
	}
	return false;
}

struct Arena : CombatWorld
{
	Object *archers = nullptr, *targets = nullptr;
	Arena()
		: CombatWorld(kDummyHorde)
	{
		combat().setAutoAcquireEnabled(false);
		archers = unit("ArcherHorde", 'A', 300, 300);
		targets = unit("DummyHorde", 'B', 430, 300);
		frames(12); // the archers' clips load
	}
	// the horde attacks and the logic runs until its first HordeAttackNugget fire gave orders
	int attackUntilFirstVolley()
	{
		REQUIRE(archers->getAIUpdateInterface()->aiAttackObject(targets, CMD_FROM_PLAYER));
		return runUntil([&] { return hordeOf(archers)->attackStats().orders > 0; }, 200);
	}
};

// retail's pick (RW 0x86FA87) for a member list, from the logged draws: the smallest product of the draw and the 3D distance stored as a float, strictly below the
// running best that starts at 99999
size_t expectedPick(const std::vector<Object *> &members, const Coord3D &p, const std::vector<float> &draws)
{
	size_t best = members.size();
	float bestD = 99999.0f;
	for (size_t i = 0; i < members.size(); ++i)
	{
		const Coord3D *q = members[i]->getPosition();
		const float d = (float)SimMath::length3d(SimMath::subf32(p.x, q->x), SimMath::subf32(p.y, q->y), SimMath::subf32(p.z, q->z));
		const float v = SimMath::mulf32(draws[i], d);
		if (v < bestD)
		{
			best = i;
			bestD = v;
		}
	}
	return best;
}
} // namespace

TEST_CASE("archer1: the member pick RW 0x86FA87 draws one logic random 0.66 .. 1.33 per member in reach (HordeContain.cpp 0x1544), in contain order, and takes the smallest product")
{
	Arena w;
	HordeContain *hc = w.hordeOf(w.targets);
	const std::vector<Object *> members = w.membersOf(w.targets);
	REQUIRE(members.size() == 6);
	GameLogicRandom &rng = w.logic->random();
	rng.enableCallLog(true);
	const Coord3D from{ 350.0f, 310.0f, 0.0f };
	for (int round = 0; round < 50; ++round)
	{
		rng.clearCallLog();
		Object *pick = hc->pickMemberNear(false, from, 500.0f, nullptr);
		const std::vector<GameLogicRandom::Call> &log = rng.callLog();
		REQUIRE(log.size() == members.size());
		std::vector<float> draws;
		for (const GameLogicRandom::Call &c : log)
		{
			CHECK(c.real);
			CHECK(c.rlo == 0.66f);
			CHECK(c.rhi == 1.33f);
			CHECK(c.file == "HordeContain.cpp");
			CHECK(c.line == 0x1544);
			draws.push_back(c.rresult);
		}
		const size_t want = expectedPick(members, from, draws);
		REQUIRE(want < members.size());
		CHECK(pick == members[want]);
	}
	// members out of reach draw nothing; with no range (0: a melee weapon) there is no draw and the plain nearest wins
	rng.clearCallLog();
	CHECK(hc->pickMemberNear(false, from, 1.0f, nullptr) == nullptr);
	CHECK(rng.callLog().empty());
	rng.clearCallLog();
	Object *nearest = hc->pickMemberNear(false, from, 0.0f, nullptr);
	CHECK(rng.callLog().empty());
	std::vector<float> ones(members.size(), 1.0f);
	CHECK(nearest == members[expectedPick(members, from, ones)]);
	CHECK(hc->firstMember() == members.front()); // slot 0x110
	rng.enableCallLog(false);
}

TEST_CASE("archer1: the pick's distribution is retail's: P(member i) = integral over its factor r of prod_j P(r_j d_j > r d_i), factors uniform on [0.66, 1.33]")
{
	Arena w;
	HordeContain *hc = w.hordeOf(w.targets);
	const std::vector<Object *> members = w.membersOf(w.targets);
	REQUIRE(members.size() == 6);
	const Coord3D from{ 350.0f, 300.0f, 0.0f };
	std::vector<double> dist;
	for (Object *m : members)
	{
		const Coord3D *q = m->getPosition();
		dist.push_back(SimMath::length3d(SimMath::subf32(from.x, q->x), SimMath::subf32(from.y, q->y), SimMath::subf32(from.z, q->z)));
	}
	// the model, by numerical integration (independent of the port's code)
	const double lo = 0.66, hi = 1.33;
	std::vector<double> model(members.size(), 0.0);
	const int steps = 4000;
	for (size_t i = 0; i < members.size(); ++i)
	{
		double sum = 0.0;
		for (int k = 0; k < steps; ++k)
		{
			const double r = lo + (hi - lo) * (k + 0.5) / steps;
			double p = 1.0;
			for (size_t j = 0; j < members.size(); ++j)
			{
				if (j == i)
				{
					continue;
				}
				const double t = r * dist[i] / dist[j]; // member j loses when its factor exceeds t
				p *= t <= lo ? 1.0 : (t >= hi ? 0.0 : (hi - t) / (hi - lo));
			}
			sum += p;
		}
		model[i] = sum / steps;
	}
	const int draws = 6000;
	std::vector<int> count(members.size(), 0);
	for (int n = 0; n < draws; ++n)
	{
		Object *pick = hc->pickMemberNear(false, from, 500.0f, nullptr);
		for (size_t i = 0; i < members.size(); ++i)
		{
			count[i] += pick == members[i] ? 1 : 0;
		}
	}
	int picked = 0;
	for (size_t i = 0; i < members.size(); ++i)
	{
		const double freq = (double)count[i] / draws;
		CHECK_MESSAGE(std::abs(freq - model[i]) < 0.025, "member " << i << " freq " << freq << " model " << model[i]);
		picked += count[i] > 0 ? 1 : 0;
	}
	CHECK(picked >= 4); // the spread: not one man takes every shot
}

TEST_CASE("archer1: an archer horde attacking a horde spreads its first volley over distinct members of the target horde")
{
	Arena w;
	const int frames = w.attackUntilFirstVolley();
	CHECK(frames < 200);
	HordeContain *hc = w.hordeOf(w.archers);
	const std::vector<Object *> archers = w.membersOf(w.archers);
	REQUIRE(archers.size() == 6);
	CHECK(hc->attackStats().orders == 6); // ranks 1 and 2 are released: every archer
	CHECK(hc->attackStats().outOfReach == 0);
	std::set<ObjectID> victims;
	for (Object *a : archers)
	{
		Object *v = goalOf(a);
		REQUIRE(v != nullptr);
		CHECK(isMemberOf(w, w.targets, v));
		victims.insert(v->getID());
	}
	MESSAGE("distinct targets of the first volley: " << victims.size());
	CHECK(victims.size() >= 3); // the nearest-member rule gave 1 or 2
	CHECK(victims.size() == 4); // this world's seed (pinned)
	// a later fire leaves the members already attacking the horde alone (RW 0x86BDD3)
	const unsigned long long orders = hc->attackStats().orders;
	w.frames(6);
	CHECK(hc->attackStats().fires > 1);
	CHECK(hc->attackStats().orders == orders);
}

TEST_CASE("archer1: when a member's target dies the member is free and the next fire gives it a living member of the target horde")
{
	Arena w;
	w.attackUntilFirstVolley();
	const std::vector<Object *> archers = w.membersOf(w.archers);
	Object *archer = archers.front();
	Object *dead = goalOf(archer);
	REQUIRE(dead != nullptr);
	const ObjectID deadId = dead->getID();
	dead->kill(DEATH_NORMAL);
	CHECK(w.hordeOf(w.targets)->getContainCount() == 5); // it left the horde
	const int n = w.runUntil([&] {
		Object *v = goalOf(archer);
		return v && v->getID() != deadId;
	}, 60);
	CHECK(n < 60);
	Object *now = goalOf(archer);
	REQUIRE(now != nullptr);
	CHECK(now->getID() != deadId);
	CHECK(isMemberOf(w, w.targets, now));
	CHECK_FALSE(now->isEffectivelyDead());
	for (Object *a : w.membersOf(w.archers))
	{
		Object *v = goalOf(a);
		CHECK((v == nullptr || v->getID() != deadId));
	}
	// the whole horde falls in the end: the volleys keep spreading over the living
	const ObjectID tid = w.targets->getID();
	w.runUntil([&] { return w.byId(tid) == nullptr; }, 3000);
	CHECK(w.byId(tid) == nullptr);
}

TEST_CASE("archer1: ClosestMemberOnly orders only the member nearest the victim (RW 0x870C29); a MELEE_HORDE with a MeleeWeapon releases nobody (RW 0x86C6EB)")
{
	Arena w;
	HordeContain *hc = w.hordeOf(w.archers);
	Object *closest = hc->closestMemberTo(*w.targets);
	REQUIRE(closest != nullptr);
	hc->attackTargetNow(w.targets, true);
	CHECK(hc->attackStats().fires == 1);
	CHECK(hc->attackStats().orders == 1);
	for (Object *a : w.membersOf(w.archers))
	{
		CHECK((goalOf(a) != nullptr) == (a == closest));
	}
	Object *swords = w.unit("SwordHorde", 'A', 300, 600);
	w.frames(3);
	w.hordeOf(swords)->attackTargetNow(w.targets, false);
	CHECK(w.hordeOf(swords)->attackStats().fires == 0);
}

TEST_CASE("archer1: two peers running the same archer battle hash alike in every frame; the pick's draws are part of the hash")
{
	std::vector<std::uint32_t> run[2];
	for (int r = 0; r < 2; ++r)
	{
		Arena w;
		REQUIRE(w.archers->getAIUpdateInterface()->aiAttackObject(w.targets, CMD_FROM_PLAYER));
		for (int f = 0; f < 400; ++f)
		{
			w.frames(1);
			run[r].push_back(w.hash());
		}
		CHECK(w.hordeOf(w.archers)->attackStats().orders >= 6);
	}
	REQUIRE(run[0].size() == run[1].size());
	for (size_t i = 0; i < run[0].size(); ++i)
	{
		REQUIRE_MESSAGE(run[0][i] == run[1][i], "frame " << i);
	}
	Arena a, b;
	CHECK(a.hash() == b.hash());
	a.hordeOf(a.targets)->pickMemberNear(false, Coord3D{ 350.0f, 300.0f, 0.0f }, 500.0f, nullptr);
	CHECK(a.hash() != b.hash()); // the logic generator advanced
}

TEST_CASE("archer1: the stop S-2610 is in the combat report")
{
	Arena w;
	bool found = false;
	for (const std::string &line : w.combat().report())
	{
		found = found || line.rfind("[S-2610]", 0) == 0;
	}
	CHECK(found);
}

namespace
{
// lane ARCHER-1 r2: archers with two weapons (PRIMARY BowWeapon, SECONDARY Arc20) in a horde whose rangefinder's HordeAttackNugget locks SECONDARY
const char kDualArchers[] =
	"Weapon LockingRangefinder\n"
	"  AttackRange = 150\n"
	"  DelayBetweenShots = 1000\n"
	"  LeechRangeWeapon = Yes\n"
	"  HordeAttackNugget\n"
	"    LockWeaponSlot = SECONDARY\n"
	"  End\n"
	"End\n"
	"Object DualArcher\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 250\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY BowWeapon\n"
	"    Weapon = SECONDARY Arc20\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object DualArcherHorde\n"
	"  KindOf = HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 250\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY LockingRangefinder\n"
	"  End\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:DualArcher Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    RankInfo = RankNumber:2 UnitType:DualArcher Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20\n"
	"    InitialPayload = DualArcher 6\n"
	"    RanksToReleaseWhenAttacking = 1 2\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("archer1 r2: LockWeaponSlot locks the members' slot before the horde's (RW 0x69121A through contain slot 0x168): multiweapon archers shoot with SECONDARY")
{
	static const std::string extra = std::string(kDummyHorde) + kDualArchers;
	CombatWorld w(extra.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *archers = w.unit("DualArcherHorde", 'A', 300, 300);
	Object *targets = w.unit("DummyHorde", 'B', 430, 300);
	w.frames(12);
	const std::vector<Object *> members = w.membersOf(archers);
	REQUIRE(members.size() == 6);
	for (Object *m : members)
	{
		REQUIRE(m->getWeapons() != nullptr);
		CHECK(m->getWeapons()->curSlot() == 0);
		CHECK_FALSE(m->getWeapons()->isCurWeaponLocked());
	}
	REQUIRE(archers->getAIUpdateInterface()->aiAttackObject(targets, CMD_FROM_PLAYER));
	const int n = w.runUntil([&] { return w.hordeOf(archers)->attackStats().orders > 0; }, 200);
	CHECK(n < 200);
	static const int kSwitched = CombatNames::status("SWITCHED_WEAPONS");
	for (Object *m : members)
	{
		CHECK(m->getWeapons()->isCurWeaponLocked());
		CHECK(m->getWeapons()->curSlot() == 1); // SECONDARY: the member does not choose its bow again
		CHECK_FALSE(m->testStatus((unsigned)kSwitched)); // a temporary lock clears SWITCHED_WEAPONS (RW 0x691238)
		CHECK(goalOf(m) != nullptr);
	}
	// the direct call: a permanent lock of a non-PRIMARY slot sets SWITCHED_WEAPONS on the horde and its members
	CHECK_FALSE(ObjectWeapons::setObjectWeaponLock(*archers, 1, LOCKED_PERMANENTLY)); // the horde has no SECONDARY: its own lock fails
	for (Object *m : members)
	{
		CHECK(m->testStatus((unsigned)kSwitched));
	}
	CHECK(archers->testStatus((unsigned)kSwitched));
}
