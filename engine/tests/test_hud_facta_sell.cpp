// OpenBFME retail tests for lane FACT-A: selling the structures of Men, Elves and Dwarves through the control bar's Sell button (MSG_SELL RW 0x77B2C8,
// BuildAssistant::sellObject RW 0x7966BB, BuildAssistant::update RW 0x79468B). SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"
#include "FactATestUtil.h"

#include "Common/Player.h"
#include "Common/ScoreKeeper.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/BuildAssistantSell.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/BehaviorModule.h"

#include <cstdio>
#include <string>
#include <variant>
#include <vector>

using namespace facta;

namespace
{
unsigned refundValue(const ThingTemplate &tt)
{
	const FieldValue *v = tt.getFinalOverride()->findField("RefundValue");
	const long long *i = v ? std::get_if<long long>(v) : nullptr;
	return i ? (unsigned)(*i & 0xFFFF) : 0u;
}

// the refund as the INI states it: RefundValue, or SellPercentage (gamedata.ini: 50%) of what was paid, times the health left; truncated at each step
unsigned expectedRefund(const ThingTemplate &tt, float paid, float health, float maxHealth)
{
	const unsigned rv = refundValue(tt);
	const unsigned base = rv != 0 ? rv : (unsigned)(double)(paid * 0.5f);
	return (unsigned)(double)((float)base * (health / maxHealth));
}

void damage(Object &o, float amount)
{
	DamageInfo d;
	d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	d.m_input.m_deathType = DEATH_NORMAL;
	d.m_input.m_sourceID = INVALID_ID;
	d.m_input.m_amount = amount;
	o.getBodyModule()->attemptDamage(d);
}
} // namespace

TEST_CASE("FACT-A sell: every sellable structure of Men, Elves and Dwarves sells from its Sell button; refund, death and selection as RW 0x7966BB / 0x79468B")
{
	if (!hudtest::haveWorld("facta sell"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	const int structureBit = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
	const int notSellableBit = ObjectTemplateInfoBuilder::kindOfIndex("NOT_SELLABLE");
	for (const std::string &f : factions())
	{
		FactRig r(s, f.c_str());
		REQUIRE(r.logic().settings().sellPercentageLoaded);
		CHECK(r.logic().settings().sellPercentage == doctest::Approx(0.5f));
		size_t sold = 0, notSellable = 0;
		float x = 1300.0f;
		for (const std::string &name : reachableTemplates(r))
		{
			const ThingTemplate *tt = r.logic().things().findTemplate(name);
			const ObjectTemplateInfo info = ObjectTemplateInfoBuilder::build(*tt->getFinalOverride());
			if (!MaskTest(info.kindOf, (unsigned)structureBit))
			{
				continue;
			}
			const CommandButton *sell = setButton(templateCommandSet(*tt), GUI_COMMAND_SELL);
			if (!sell || MaskTest(info.kindOf, (unsigned)notSellableBit))
			{
				++notSellable;
				continue;
			}
			const Coord3D at = r.spot(x, 1700.0f, 300.0f);
			x = x > 3400.0f ? 1300.0f : x + 350.0f;
			Object *o = r.make(name, at);
			const float paid = (float)BuildAssistant::calcCostToBuild(*tt, r.local(), nullptr, -1);
			o->setBuildCostPaid(paid);
			r.frames(3);
			REQUIRE(o->getBodyModule());
			damage(*o, o->getBodyModule()->getMaxHealth() * 0.25f); // a quarter off: the health fraction scales the refund
			r.frames(1);
			const float health = o->getBodyModule()->getHealth(), maxHealth = o->getBodyModule()->getMaxHealth();
			const unsigned expect = expectedRefund(*tt, paid, health, maxHealth);
			const ObjectID id = o->getID();
			CHECK_MESSAGE(r.select(o), name);
			const unsigned before = r.money();
			const unsigned long long lostBefore = r.local()->getScoreKeeper().counters().lostCalls;
			const ControlBarButton *b = r.find(sell->m_name);
			REQUIRE_MESSAGE(b, name);
			CHECK_MESSAGE(b->state == ButtonState::Enabled, name);
			REQUIRE(r.bar().pressButton(b->slot, b->inPalantir));
			r.frames(3);
			INFO(f << " " << name << " paid " << paid << " health " << health << "/" << maxHealth);
			Object *after = r.logic().findObjectByID(id);
			CHECK((after == nullptr || after->isEffectivelyDead()));
			CHECK(r.money() == before + expect);
			// lane KILLSRC-1: one loss, retail's explicit addObjectLost (RW 0x79487A); the kill after it (RW 0x794885 -> 0x698EC3) has no source, so no
			// scoreTheKill counts it again (calls, not structuresLost: a wall segment is no STRUCTURE + SCORE template and counts in neither)
			CHECK(r.local()->getScoreKeeper().counters().lostCalls == lostBefore + 1);
			CHECK_FALSE(r.local()->getAttackedBy(r.local()->getPlayerIndex()));
			CHECK(r.hud->input().ui().selected().empty());
			CHECK(r.local()->selection().empty());
			CHECK(r.unhandled().count(MSG_SELL) == 0);
			std::printf("facta sell %s %s: paid %.0f refund %u (RefundValue %u)\n", f.c_str(), name.c_str(), paid, expect, refundValue(*tt));
			++sold;
			r.frames(10);
		}
		std::printf("facta sell %s: %zu sold, %zu structures without a sell button or NOT_SELLABLE\n", f.c_str(), sold, notSellable);
		CHECK(sold >= 8);
		CHECK(r.logic().sellList().empty());
	}
}

TEST_CASE("FACT-A sell: what retail refuses is not sold (under construction, a unit, a second press), and the list is hashed while it holds an entry")
{
	if (!hudtest::haveWorld("facta sell refusals"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	FactRig r(s, "FactionMen");
	BuildAssistantSell &list = r.logic().sellList();
	const Coord3D at = r.spot(2000.0f, 1600.0f, 300.0f);
	Object *farm = r.make("GondorFarm", at);
	r.frames(2);
	const int underConstruction = ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION");
	farm->setStatus((unsigned)underConstruction, true);
	list.sellObject(farm);
	CHECK(list.empty());
	farm->setStatus((unsigned)underConstruction, false);
	Object *unit = r.make("GondorFighterHorde", r.spot(2600.0f, 1600.0f, 200.0f));
	list.sellObject(unit);
	CHECK(list.empty());
	const std::uint32_t h0 = r.logic().computeStateHash();
	list.sellObject(farm);
	list.sellObject(farm);
	CHECK(list.list().size() == 1);
	CHECK(farm->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("SOLD")));
	CHECK(farm->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE")));
	CHECK(farm->getConstructionPercent() == doctest::Approx(99.9f));
	CHECK(r.logic().computeStateHash() != h0);
	r.frames(2);
	CHECK(list.empty());
	CHECK(unit->isEffectivelyDead() == false);
}
