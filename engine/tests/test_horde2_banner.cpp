// OpenBFME. HORDE-2 tests: the banner carrier (RW 0x8719E4 / 0x870204) and BannerCarrierUpdate's replenishment (RW 0x89ACE4 / 0x89A392) on synthetic data.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Horde2TestUtil.h"

#include "GameLogic/Module/BannerCarrierUpdate.h"

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
	CHECK(banner->getPosition()->x == doctest::Approx(310.0f).epsilon(0.02)); // BannerCarrierPosition X:10 in front of the horde centre
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
