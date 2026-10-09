// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-1, retail: the spell book Heal (GoodSpellBook's PlayerHealSpecialPower, data\ini\object\system\system.ini) parsed into its typed module
// data and its effect measured on retail units: a wounded Gondor soldier within the radius gets 60% of its max health back (capped at max), an enemy
// next to it and an ally outside the radius get nothing.

#include "doctest.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"

#include <memory>
#include <string>

namespace
{
struct HealGame
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	starttest::Shared &s;
	explicit HealGame(starttest::Shared &shared)
		: players(shared.world->nameKeys(), shared.world->playerTemplates(), teams)
		, s(shared)
	{
		SkirmishSetup setup;
		const char *factions[2] = { "FactionMen", "FactionMordor" };
		for (int i = 0; i < 2; ++i)
		{
			SkirmishPlayer p;
			p.name = std::string("p") + std::to_string(i);
			p.faction = factions[i];
			p.human = i == 0;
			p.team = i;
			p.startIndex = i + 1;
			setup.players.push_back(p);
		}
		REQUIRE(players.setupSkirmish(setup).empty());
		logic = std::make_unique<GameLogic>(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic->settings(), &err), err);
	}
	Object *place(const char *name, int player, float x, float y)
	{
		const ThingTemplate *t = s.world->things().findTemplate(name);
		REQUIRE_MESSAGE(t, name);
		Object *o = logic->newObject(t, players.findPlayerWithName(std::string("p") + std::to_string(player))->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		Coord3D pos{ x, y, 0.0f };
		o->setPosition(&pos);
		return o;
	}
};

PlayerHealSpecialPower *healModule(Object &book)
{
	for (const auto &m : book.modules())
	{
		if (auto *h = dynamic_cast<PlayerHealSpecialPower *>(m.get()))
		{
			return h;
		}
	}
	return nullptr;
}

void wound(Object &o, float amount)
{
	ActiveBody *b = dynamic_cast<ActiveBody *>(o.getBodyModule());
	REQUIRE(b);
	b->internalChangeHealth(-amount);
}
} // namespace

TEST_CASE("SPELL-1 retail: the spell book Heal restores 60% of max health to the caster's wounded units in its radius")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	HealGame g(*shared);
	Object *book = g.place("MenSpellBook", 0, 0.0f, 0.0f);
	PlayerHealSpecialPower *heal = healModule(*book);
	REQUIRE(heal);
	const PlayerHealSpecialPowerModuleData *d = heal->data();
	CHECK(d->m_healAmount == 0.6f);
	CHECK(d->m_healAsPercent);
	CHECK(d->m_healRadius == 200.0f); // SPELL_HEAL_RADIUS_UNIT_SCAN
	CHECK(d->m_specialPowerTemplateName == "SpellBookHeal");
	REQUIRE(d->m_specialPowerTemplate);
	CHECK(d->m_specialPowerTemplate->getName() == "SpellBookHeal");
	CHECK_FALSE(d->m_availableAtStart);
	CHECK(d->m_healOCL == "OCL_HealSpellHordeReplenishPing");

	Object *near = g.place("GondorFighter", 0, 1000.0f, 1000.0f);
	Object *hurt = g.place("GondorFighter", 0, 1100.0f, 1000.0f);
	Object *far = g.place("GondorFighter", 0, 1500.0f, 1000.0f);
	Object *enemy = g.place("MordorFighter", 1, 1050.0f, 1000.0f);
	const float max = hurt->getBodyModule()->getMaxHealth();
	REQUIRE(max > 0.0f);
	wound(*hurt, max * 0.9f);
	wound(*near, max * 0.1f);
	wound(*far, max * 0.5f);
	const float enemyMax = enemy->getBodyModule()->getMaxHealth();
	wound(*enemy, enemyMax * 0.5f);
	const float hurtBefore = hurt->getBodyModule()->getHealth();
	const float farBefore = far->getBodyModule()->getHealth();
	const float enemyBefore = enemy->getBodyModule()->getHealth();

	const Coord3D at{ 1050.0f, 1000.0f, 0.0f };
	const int healed = heal->healAt(at);
	CHECK(healed == 2); // the two soldiers in range (the enemy is skipped before healing)
	CHECK(hurt->getBodyModule()->getHealth() == doctest::Approx(hurtBefore + max * 0.6f));
	CHECK(near->getBodyModule()->getHealth() == max); // capped at max health
	CHECK(far->getBodyModule()->getHealth() == farBefore);
	CHECK(enemy->getBodyModule()->getHealth() == enemyBefore);
	CHECK(heal->oclRequests() == 1u); // HealOCL (the horde replenish ping): counted, S-529
	CHECK(heal->fxRequests() == 2u);  // HealFX per healed object (client)
	MESSAGE("SPELL-1 heal: GondorFighter max health " << max << ", healed " << healed << " (" << hurtBefore << " -> " << hurt->getBodyModule()->getHealth() << ")");
}
