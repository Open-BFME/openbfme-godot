// OpenBFME unit tests: PoisonedBehavior (fork issue 762; editions: both RotWK 2.01 and BFME2 1.06).
// Sources:
//   - RW 0x00886165, 0x00886193, 0x008861FA, 0x0088616B, 0x0088628E, 0x00886300,
//     0x00886326, 0x00886378, 0x0088638E, 0x008863B0, 0x00850BE9, 0x00670913
//   - ActiveBody::attemptHealing onHealing: RW 0x008C30C0..0x008C30E9

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/StateHash.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/PoisonedBehavior.h"
#include "GameLogic/Object/Object.h"

#include <utility>
#include <vector>

namespace
{
constexpr UnsignedInt kTintStatusPoisoned = 0x04u; // RW 0x008862E0: TINT_STATUS_POISONED

struct TintCaptureHooks : public ObjectClientHooks
{
	std::vector<std::pair<ObjectID, UnsignedInt>> setEvents;
	std::vector<std::pair<ObjectID, UnsignedInt>> clearEvents;

	void objectCreated(Object &) override {}
	void objectDestroyed(Object &) override {}
	void setDrawableTintStatus(Object &obj, UnsignedInt statusBits) override
	{
		setEvents.push_back({ obj.getID(), statusBits });
	}
	void clearDrawableTintStatus(Object &obj, UnsignedInt statusBits) override
	{
		clearEvents.push_back({ obj.getID(), statusBits });
	}
};

const char *kPoisonIni = R"(
Object Local97PoisonVictim
	Side = Men
	KindOf = SELECTABLE INFANTRY
	Body = ActiveBody ModuleTag_Body
		MaxHealth = 200.0
	End
	Behavior = PoisonedBehavior ModuleTag_Poison
		PoisonDamageInterval = 1000
		PoisonDuration = 3000
	End
End
)";
} // namespace

TEST_CASE("LOCAL-97 PoisonedBehavior: POISON hit ticks damage, sets tint, heals cure poison, and xfer round-trips (RW 0x008863B0, RW 0x0088638E)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		combattest::CombatWorld world;
		world.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		const std::string err = world.w.load(kPoisonIni, INI_LOAD_OVERWRITE, "poisoned_behavior.ini");
		REQUIRE_MESSAGE(err.empty(), err);

		TintCaptureHooks hooks;
		world.logic->setClientHooks(&hooks);

		Object *victim = world.unit("Local97PoisonVictim", 'A', 100.0f, 100.0f);
		REQUIRE(victim != nullptr);
		auto *pb = dynamic_cast<PoisonedBehavior *>(victim->findModule("PoisonedBehavior"));
		REQUIRE(pb != nullptr);
		REQUIRE(pb->moduleData() != nullptr);
		CHECK(pb->moduleData()->m_poisonDamageInterval == 5u);
		CHECK(pb->moduleData()->m_poisonDuration == 15u);
		CHECK(pb->poisonOverallStopFrame() == 0u);
		CHECK(pb->poisonDeathType() == DEATH_POISONED);

		world.frames(2);
		const UnsignedInt t0 = world.logic->getFrame();
		REQUIRE(t0 == 2u);

		{
			DamageInfo slash;
			slash.m_input.m_damageType = DAMAGE_SLASH;
			slash.m_input.m_amount = 10.0f;
			victim->attemptDamage(slash);
			CHECK(pb->poisonOverallStopFrame() == 0u);
			CHECK(hooks.setEvents.empty());
			CHECK(victim->getBodyModule()->getHealth() == 190.0f);
		}

		{
			DamageInfo poison;
			poison.m_input.m_damageType = DAMAGE_POISON;
			poison.m_input.m_deathType = DEATH_POISONED;
			poison.m_input.m_amount = 20.0f;
			victim->attemptDamage(poison);
		}
		CHECK(victim->getBodyModule()->getHealth() == 170.0f);
		CHECK(pb->poisonDamageAmount() == 20.0f);
		CHECK(pb->poisonDamageFrame() == t0 + 5u);
		CHECK(pb->poisonOverallStopFrame() == t0 + 15u);
		REQUIRE(hooks.setEvents.size() == 1u);
		CHECK(hooks.setEvents[0].second == kTintStatusPoisoned);

		world.frames(4);
		CHECK(victim->getBodyModule()->getHealth() == 170.0f);

		world.frames(1);
		CHECK(victim->getBodyModule()->getHealth() == 150.0f);
		CHECK(pb->poisonDamageFrame() == t0 + 10u);

		world.frames(1);
		{
			DamageInfo repoison;
			repoison.m_input.m_damageType = DAMAGE_POISON;
			repoison.m_input.m_deathType = DEATH_POISONED_BETA;
			repoison.m_input.m_amount = 15.0f;
			victim->attemptDamage(repoison);
		}
		CHECK(victim->getBodyModule()->getHealth() == 135.0f);
		CHECK(pb->poisonDamageAmount() == 15.0f);
		CHECK(pb->poisonDamageFrame() == t0 + 10u);
		CHECK(pb->poisonOverallStopFrame() == t0 + 21u);
		CHECK(pb->poisonDeathType() == DEATH_POISONED_BETA);

		{
			XferSave save;
			pb->xfer(save);
			StateHasher hBefore;
			pb->crc(hBefore);

			victim->attemptHealing(10.0f, nullptr);
			CHECK(pb->poisonOverallStopFrame() == 0u);
			CHECK(pb->poisonDamageFrame() == 0u);
			CHECK(pb->poisonDamageAmount() == 0.0f);
			REQUIRE(hooks.clearEvents.size() == 1u);
			CHECK(hooks.clearEvents[0].second == kTintStatusPoisoned);

			XferLoad load(save.data());
			pb->xfer(load);
			StateHasher hAfter;
			pb->crc(hAfter);
			CHECK(hAfter.value() == hBefore.value());
			CHECK(pb->poisonDamageFrame() == t0 + 10u);
			CHECK(pb->poisonOverallStopFrame() == t0 + 21u);
			CHECK(pb->poisonDamageAmount() == 15.0f);
			CHECK(pb->poisonDeathType() == DEATH_POISONED_BETA);
		}

		victim->attemptHealing(5.0f, nullptr);
		const float healthAfterHeal = victim->getBodyModule()->getHealth();
		CHECK(pb->poisonOverallStopFrame() == 0u);
		world.frames(15);
		CHECK(victim->getBodyModule()->getHealth() == healthAfterHeal);

		const std::size_t clearsBefore = hooks.clearEvents.size();
		{
			DamageInfo poison;
			poison.m_input.m_damageType = DAMAGE_POISON;
			poison.m_input.m_deathType = DEATH_POISONED;
			poison.m_input.m_amount = 5.0f;
			victim->attemptDamage(poison);
		}
		world.frames(16);
		CHECK(pb->poisonOverallStopFrame() == 0u);
		CHECK(hooks.clearEvents.size() == clearsBefore + 1u);

		world.logic->setClientHooks(nullptr);
	}
}
