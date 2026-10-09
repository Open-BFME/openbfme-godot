// OpenBFME retail tests (lane MODULES-2): LargeGroupBonusUpdate (the Mordor orc hordes' group bonus) and PassiveAreaEffectBehavior (the Gondor statue's
// leadership, the Gondor well's healing) over the real RotWK 2.01 data. SKIP without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"

#include "Mod2TestUtil.h"

#include "Common/StateHash.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AreaScanModules.h"
#include "GameLogic/Module/HitReactionBehavior.h"

#include <string>
#include <vector>

namespace
{
template <class T>
T *moduleOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (T *t = dynamic_cast<T *>(m.get()))
		{
			return t;
		}
	}
	return nullptr;
}

float damageMult(Object *o)
{
	float v = 1.0f;
	o->attributeModifierProduct(ATTRIBUTE_DAMAGE_MULT, nullptr, false, v);
	return v;
}
} // namespace

TEST_CASE("area scans retail: Mordor orc hordes packed within 160 reach Count 100 and take MordorLargeGroupBonus; a horde within RubOffRadius of one gets it by rub-off; deterministic")
{
	if (!hudtest::haveWorld("area scans retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		mod2test::RetailGame g(sh);
		std::vector<Object *> pack;
		for (int i = 0; i < 6; ++i)
		{
			pack.push_back(g.make("MordorFighterHorde", "Mordor", 200.0f + 25.0f * (float)i, 400.0f)); // x 200 .. 325
		}
		Object *outrider = g.make("MordorFighterHorde", "Mordor", 480.0f, 400.0f); // 155 from the last of the pack, far from the first
		Object *alone = g.make("MordorFighterHorde", "Mordor", 800.0f, 850.0f);
		g.run(12); // past the first wake (1 .. UpdateRate) and one more scan (UpdateRate 5 frames)
		LargeGroupBonusUpdate *last = moduleOf<LargeGroupBonusUpdate>(pack.back());
		LargeGroupBonusUpdate *out = moduleOf<LargeGroupBonusUpdate>(outrider);
		LargeGroupBonusUpdate *solo = moduleOf<LargeGroupBonusUpdate>(alone);
		REQUIRE(last);
		REQUIRE(out);
		REQUIRE(solo);
		MESSAGE("members seen: pack end " << last->lastCount() << ", outrider " << out->lastCount() << ", alone " << solo->lastCount() << "; members per horde "
		                                 << g.members(alone).size());
		CHECK(last->lastCount() >= 99u);
		CHECK(last->reached());
		CHECK(last->active());
		CHECK(out->lastCount() < 99u);
		CHECK_FALSE(out->reached());
		CHECK(out->active()); // the rub-off from the reached horde within RubOffRadius
		CHECK_FALSE(solo->active());
		// the ModifierList went to the horde's members (DAMAGE_MULT 150 %)
		REQUIRE_FALSE(g.members(pack.back()).empty());
		CHECK(damageMult(g.members(pack.back()).front()) == doctest::Approx(1.5f));
		CHECK(damageMult(g.members(alone).front()) == doctest::Approx(1.0f));
		hashes[run] = g.logic.computeStateHash();
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("area scans retail: the Gondor statue gives GenericHeroLeadership to allied units within 200 (not to enemies); the Gondor well heals a wounded soldier by MaxHealth * 2% / 5 * 15 every 15 frames")
{
	if (!hudtest::haveWorld("area scans retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	mod2test::RetailGame g(sh);
	Object *statue = g.make("GondorStatue", "Men", 300.0f, 300.0f);
	Object *friendly = g.make("GondorFighterHorde", "Men", 400.0f, 300.0f);
	Object *enemy = g.make("MordorFighterHorde", "Mordor", 300.0f, 420.0f);
	PassiveAreaEffectBehavior *buff = moduleOf<PassiveAreaEffectBehavior>(statue);
	REQUIRE(buff);
	// the first scan (frame 1) runs before the horde spawned its members; the next comes PingDelay (2000 ms = 10 frames) later
	g.run(12);
	CHECK(buff->modifiersApplied() > 0);
	bool friendInList = false, enemyInList = false;
	for (ObjectID id : buff->targets())
	{
		Object *o = g.logic.findObjectByID(id);
		friendInList = friendInList || (o && o->getContainedBy() == friendly);
		enemyInList = enemyInList || (o && (o == enemy || o->getContainedBy() == enemy));
	}
	CHECK(friendInList);
	CHECK_FALSE(enemyInList);

	Object *well = g.make("GondorWell", "Men", 700.0f, 700.0f);
	Object *soldier = g.make("GondorFighter", "Men", 740.0f, 700.0f);
	PassiveAreaEffectBehavior *heal = moduleOf<PassiveAreaEffectBehavior>(well);
	REQUIRE(heal);
	ActiveBody *body = dynamic_cast<ActiveBody *>(soldier->getBodyModule());
	REQUIRE(body);
	const float maxHealth = body->getMaxHealth();
	body->friend_setHealthRaw(maxHealth * 0.5f);
	const float before = body->getHealth();
	g.run(16); // the well's first update (its constructor wakes it next frame) heals within 16 frames
	CHECK(heal->heals() >= 1);
	const float gained = body->getHealth() - before;
	MESSAGE("well: max " << maxHealth << " healed " << gained << " in " << heal->heals() << " pulse(s)");
	CHECK(gained == doctest::Approx(maxHealth * 0.02f / 5.0f * 15.0f * (float)heal->heals()).epsilon(0.001));
	// the NonStackable rule is noted (S-1023)
	bool noted = false;
	for (const std::string &l : g.logic.report().stops)
	{
		noted = noted || l.find("[S-1023] PassiveAreaEffectBehavior: NonStackable") == 0;
	}
	CHECK(noted);
}

TEST_CASE("hit reaction retail: an Arnor captain that loses health while idle shows HIT_REACTION and the level of the loss for its LifeTimer, then clears")
{
	if (!hudtest::haveWorld("hit reaction retail"))
	{
		return;
	}
	hudtest::SharedWorld &sh = hudtest::shared();
	auto scope = sh.world->enterContext();
	mod2test::RetailGame g(sh);
	Object *soldier = g.make("ArnorCaptain", "Men", 500.0f, 500.0f);
	HitReactionBehavior *hr = moduleOf<HitReactionBehavior>(soldier);
	REQUIRE(hr);
	const HitReactionBehaviorModuleData *d = nullptr;
	for (const ThingTemplate::Nugget &n : static_cast<const ThingTemplate *>(soldier->getTemplate())->behaviorModules().nuggets())
	{
		d = d ? d : dynamic_cast<const HitReactionBehaviorModuleData *>(n.data.get());
	}
	REQUIRE(d);
	MESSAGE("ArnorCaptain hit reaction: life " << d->m_lifeTimer[0] << " / " << d->m_lifeTimer[1] << " / " << d->m_lifeTimer[2] << " frames, thresholds "
	                                            << d->m_threshold[0] << " / " << d->m_threshold[1] << " / " << d->m_threshold[2]);
	CHECK(d->m_lifeTimer[2] > 0);
	g.run(3);
	REQUIRE(soldier->getAIUpdateInterface());
	REQUIRE(soldier->getAIUpdateInterface()->isIdle());
	ActiveBody *body = dynamic_cast<ActiveBody *>(soldier->getBodyModule());
	REQUIRE(body);
	const int reaction = [] { for (int i = 0; TheModelConditionNames[i]; ++i) { if (std::string(TheModelConditionNames[i]) == "HIT_REACTION") return i; } return -1; }();
	const int level3 = reaction + 3; // HIT_LEVEL_3
	REQUIRE(reaction >= 0);
	// a loss above Threshold3: level 3
	body->friend_setHealthRaw(body->getHealth() - (d->m_threshold[2] + 1.0f));
	g.run(1);
	CHECK(soldier->testModelCondition(reaction));
	CHECK(soldier->testModelCondition(level3));
	CHECK(hr->countdown() == (int)d->m_lifeTimer[2]);
	g.run((int)d->m_lifeTimer[2] - 1);
	CHECK(soldier->testModelCondition(reaction)); // still shown on the countdown's last frame
	g.run(1);
	CHECK_FALSE(soldier->testModelCondition(reaction));
	CHECK_FALSE(soldier->testModelCondition(level3));
	// S-1024: the static line and the note of the unidentified slot 0x84 interface
	bool line = false, note = false;
	for (const std::string &l : g.logic.report().stops)
	{
		line = line || l.find("[S-1024] HitReactionBehavior: ported") == 0;
		note = note || l.find("[S-1024] HitReactionBehavior: the interface RW 0x68C409") == 0;
	}
	CHECK(line);
	CHECK(note);
}
