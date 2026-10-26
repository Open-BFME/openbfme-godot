// OpenBFME. HORDE-2 tests: the banner carrier (RW 0x8719E4 / 0x870204) and BannerCarrierUpdate's replenishment (RW 0x89ACE4 / 0x89A392) on synthetic data.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Horde2TestUtil.h"

#include "GameLogic/Module/BannerCarrierUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/HeroModules.h"

#include <functional>

using namespace combattest;
using namespace horde2test;

namespace
{

HordeContain *hc(Object *h)
{
	return dynamic_cast<HordeContain *>(h->getContain());
}
} // namespace

TEST_CASE("horde2 banner: no carrier at rank 0; at rank 1 the carrier joins, stands at its position and refills a dead member when out of combat")
{
	CombatWorld w(kBannerObjects);
	w.combat().setAutoAcquireEnabled(false);
	Object *h = w.unit("BannerHorde", 'A', 300, 300);
	w.frames(10);
	REQUIRE(hc(h) != nullptr);
	REQUIRE(h->getExperienceTracker() != nullptr);
	REQUIRE(h->getExperienceTracker()->getRank() == 0); // no ExperienceLevel data in this fixture
	CHECK(hc(h)->bannerCarrier() == 0); // BannerCarrierMinLevel 0 is not below rank 0
	CHECK(hc(h)->getContainCount() == 3);
	ExperienceTrackerTestAccess::setRank(*h->getExperienceTracker(), 1);
	w.frames(2);
	REQUIRE(hc(h)->bannerCarrier() != 0);
	Object *banner = w.byId(hc(h)->bannerCarrier());
	REQUIRE(banner != nullptr);
	CHECK(banner->getContainedBy() == h);
	CHECK(hc(h)->getContainCount() == 4);
	CHECK(hc(h)->bannerCountdown() == 19u); // MeleeFreeBannerReSpawnTime 4000 ms = 20 frames, one update ran since
	w.frames(30);
	// BannerCarrierPosition X:10, in the slot table centred on its mean (RW 0x86C4EA: three swordsmen at X 50 and the carrier at 10, 40): 30 behind the horde
	CHECK(banner->getPosition()->x == doctest::Approx(h->getPosition()->x - 30.0f).epsilon(0.02)); // tolerance: the carrier walks onto its slot (2 %, as before)
	// a member dies; the carrier brings the horde back to three swordsmen after IdleSpawnRate once nobody fought for MeleeFreeUnitSpawnTime
	Object *victim = nullptr;
	for (Object *m : w.membersOf(h))
	{
		if (m != banner)
		{
			victim = m;
			break;
		}
	}
	REQUIRE(victim != nullptr);
	victim->kill(0);
	w.frames(40);
	BannerCarrierUpdate *b = dynamic_cast<BannerCarrierUpdate *>(banner->findModule("BannerCarrierUpdate"));
	REQUIRE(b != nullptr);
	CHECK(b->membersSpawned() == 1);
	CHECK(hc(h)->getContainCount() == 4);
	CHECK(hc(h)->freeSlotIndices().empty());
	REQUIRE(b->data().m_morphConditions.size() == 1);
	CHECK(b->data().m_morphConditions[0] == "UnitType:Swordsman ModelState:\"USER_2\"");
}

TEST_CASE("horde2 banner: nearby replenishment waits for its pulse, skips construction and fills one or all in distance order")
{
	for (bool all : { false, true })
	{
		CAPTURE(all);
		const std::string objects = std::string(kBannerObjects) +
			"Object Well\n"
			" KindOf = STRUCTURE\n"
			" Body = ActiveBody Body\n MaxHealth = 100\n End\n"
			" Behavior = BannerCarrierUpdate Refill\n"
			" IdleSpawnRate = 1000\n ReplenishNearbyHorde = Yes\n ScanHordeDistance = 300\n"
			" ReplenishAllNearbyHordes = " + (all ? "Yes\n" : "No\n") + " End\nEnd\n";
		CombatWorld w(objects.c_str());
		w.combat().setAutoAcquireEnabled(false);
		Object *blocked = w.unit("BannerHorde", 'A', 320, 300);
		Object *near = w.unit("BannerHorde", 'A', 350, 300);
		Object *far = w.unit("BannerHorde", 'A', 400, 300);
		w.frames(2);
		for (Object *h : { blocked, near, far })
		{
			REQUIRE(hc(h)->getContainCount() == 3);
			REQUIRE(hc(h)->bannerCarrier() == 0);
			DamageInfo damage;
			damage.m_input.m_damageType = DAMAGE_UNRESISTABLE;
			damage.m_input.m_amount = 10000.0f;
			w.membersOf(h).front()->attemptDamage(damage);
		}
		w.frames(2);
		blocked->setStatus(2, true); // RW 0x89AC1D excludes UNDER_CONSTRUCTION before checking capacity.
		Object *well = w.unit("Well", 'A', 300, 300);
		auto *refill = dynamic_cast<BannerCarrierUpdate *>(well->findModule("BannerCarrierUpdate"));
		REQUIRE(refill);
		w.frames(4);
		CHECK(refill->membersSpawned() == 0);
		w.frames(1);
		CHECK(hc(blocked)->getContainCount() == 2);
		CHECK(hc(near)->getContainCount() == 3);
		CHECK(hc(far)->getContainCount() == (all ? 3 : 2));
		CHECK(refill->membersSpawned() == (all ? 2 : 1));
		w.frames(10);
		CHECK(hc(near)->getContainCount() == 3);
		CHECK(hc(far)->getContainCount() == 3);
		CHECK(refill->membersSpawned() == 2);
	}
}

// review DEVPORT-7e (Sol #4): a horde healer's HealOnlyIfNotUnderAttack reads RW 0x86EB60 (horde interface slot 0x26C), the newest damage frame of its members
// (body vslot 0x44, unsigned, -1 = never), not the horde object's body. Its loop RW 0x86EB87 .. 0x86EBAB keeps the larger frame, so a member never damaged
// (-1) wins: a horde with one untouched member is never "under attack" (RW 0x8555A2: a last frame at or after now skips the test). The healer heals a
// HealTarget (KindOf CAVALRY) only, so its own members are never healed and the target's health is the effect measured.
TEST_CASE("horde2 AutoHeal: a horde healer's under-attack test reads its members' newest damage frame, an undamaged member reading as never (RW 0x86EB60)")
{
	const std::string objects = std::string(kBannerObjects) +
		"Object HealHorde\n"
		"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
		"  Geometry = BOX\n  GeometryMajorRadius = 30\n  GeometryMinorRadius = 45\n  GeometryHeight = 20\n"
		"  Body = ImmortalBody ModuleTag_Body\n    MaxHealth = 1\n  End\n"
		"  Behavior = HordeAIUpdate ModuleTag_AI\n  End\n"
		"  Behavior = HordeContain ModuleTag_Contain\n"
		"    RankInfo = RankNumber:1 UnitType:Swordsman Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
		"    Slots = 3\n    InitialPayload = Swordsman 3\n  End\n"
		"  Behavior = AutoHealBehavior ModuleTag_Heal\n"
		"    StartsActive = Yes\n    HealingAmount = 10\n    HealingDelay = 1000\n    Radius = 300\n    HealOnlyIfNotUnderAttack = Yes\n    KindOf = CAVALRY\n"
		"  End\n"
		"  LocomotorSet\n    Locomotor = HordeLoco\n    Condition = SET_NORMAL\n    Speed = 50\n  End\n"
		"End\n"
		"Object HealTarget\n"
		"  KindOf = CAVALRY SELECTABLE\n"
		"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 300\n  End\n"
		"End\n";
	CombatWorld w(objects.c_str());
	w.combat().setAutoAcquireEnabled(false);
	Object *h = w.unit("HealHorde", 'A', 300, 300);
	Object *rider = w.unit("HealTarget", 'A', 380, 380);
	w.frames(10);
	REQUIRE(hc(h));
	const std::vector<Object *> members = w.membersOf(h);
	REQUIRE(members.size() == 3);
	AutoHealBehavior *heal = dynamic_cast<AutoHealBehavior *>(h->findModule("AutoHealBehavior"));
	REQUIRE(heal);
	auto hit = [&](Object *o) {
		DamageInfo d;
		d.m_input.m_amount = 10.0f;
		d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		o->attemptDamage(d);
	};
	DamageInfo wound;
	wound.m_input.m_amount = 200.0f;
	wound.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	rider->attemptDamage(wound);
	REQUIRE(w.health(rider) == 100.0f);
	// the next pulse (HealingDelay 1000 ms = 5 frames)
	auto toPulse = [&]() {
		const unsigned before = heal->heals();
		for (int i = 0; i < 6 && heal->heals() == before; ++i)
		{
			w.frames(1);
		}
		REQUIRE(heal->heals() == before + 1);
	};
	// a pulse two frames after `damage`: blocked or not; then 12 frames later the healer has healed again either way
	auto pulseAfter = [&](const std::function<void()> &damage) {
		toPulse();
		w.frames(3);
		damage();
		const unsigned heals0 = heal->heals();
		const float health0 = w.health(rider);
		w.frames(3);
		const bool healed = heal->heals() == heals0 + 1; // one pulse falls in these frames (HealingDelay 5 frames): it healed or it did not
		CHECK(heal->heals() == heals0 + (healed ? 1u : 0u));
		CHECK(w.health(rider) == health0 + (healed ? 10.0f : 0.0f));
		w.frames(12);
		// tolerance: the claim is only that healing resumes once the newest hit is 5 frames old; how many 5-frame pulses fit after it is the pulse phase's
		CHECK(heal->heals() > heals0);
		return healed;
	};
	// one member hit, two never hit: -1 is the newest frame, so the pulse heals
	CHECK(pulseAfter([&] { hit(members[0]); }));
	// every member hit: the newest member frame is two frames old, the pulse heals nobody (the horde object's own body was never hit)
	CHECK_FALSE(pulseAfter([&] { hit(members[1]); hit(members[2]); hit(members[0]); }));
	const ActiveBody *hordeBody = dynamic_cast<ActiveBody *>(h->getBodyModule());
	CHECK((hordeBody == nullptr || hordeBody->lastDamageFrame() == 0xFFFFFFFFu));
	// one member hit again, the others' frames are old: the newest (the larger) decides, so the pulse is blocked
	CHECK_FALSE(pulseAfter([&] { hit(members[2]); }));
}
