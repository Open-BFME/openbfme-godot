// OpenBFME. FreeLifeBody unit tests (RW 0x8C5AE6, 0x8C5B86; editions: both RotWK 2.01 and BFME2 1.06).
#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/FreeLifeBody.h"

#include <string>

using namespace combattest;

namespace
{
std::string unitWith(const char *name, const std::string &body)
{
	return std::string("Object ") + name + "\n"
		"  KindOf = INFANTRY SELECTABLE SCORE\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 8\n"
		"  GeometryMinorRadius = 8\n"
		"  GeometryHeight = 20\n"
		"  ArmorSet\n"
		"    Conditions = None\n"
		"    Armor = NoArmor\n"
		"  End\n" + body +
		"  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n"
		"  Behavior = DestroyDie ModuleTag_Destroy\n  End\n"
		"End\n";
}

std::string freeLifeBody(const char *extra)
{
	return std::string("  Body = FreeLifeBody ModuleTag_Body\n    MaxHealth = 1000\n    FreeLifeHealthPercent = 50%\n    FreeLifeTime = 10000\n") + extra + "  End\n";
}

const std::string kFreeLifeObjects =
	unitWith("Hero", freeLifeBody("")) +
	unitWith("HeroInvincible", freeLifeBody("    FreeLifeInvincible = Yes\n    FreeLifeAnimAndDuration = AnimState:RESURRECTED AnimTime:2000\n")) +
	unitWith("HeroLocked", freeLifeBody("    FreeLifePrerequisiteUpgrade = Upgrade_NobodyHasThis\n"));

void hit(Object *victim, float amount)
{
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_SLASH;
	info.m_input.m_deathType = DEATH_NORMAL;
	info.m_input.m_amount = amount;
	victim->attemptDamage(info);
}
} // namespace

TEST_CASE("modgap1 FreeLifeBody: a lethal hit restores FreeLifeHealthPercent of the maximum; another within FreeLifeTime kills (RW 0x8C5B86)")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CombatWorld w(kFreeLifeObjects.c_str());
		w.logic->setEdition(edition);
		CAPTURE(gameEditionName(edition));
		Object *hero = w.unit("Hero", 'A', 400, 400);
		REQUIRE(hero != nullptr);
		FreeLifeBody *body = dynamic_cast<FreeLifeBody *>(hero->getBodyModule());
		REQUIRE(body != nullptr);
		CHECK_FALSE(body->used());
		hit(hero, 5000.0f);
		CHECK(body->used());
		CHECK_FALSE(hero->isEffectivelyDead());
		CHECK(hero->getBodyModule()->getHealth() == 500.0f);

		XferSave save;
		body->xfer(save);
		XferLoad load(save.data());
		body->xfer(load);
		CHECK(body->used());

		w.frames(3);
		hit(hero, 5000.0f); // 3 frames later, FreeLifeTime is 50
		CHECK(hero->isEffectivelyDead());
	}
}

TEST_CASE("modgap1 FreeLifeBody: the second free life comes once FreeLifeTime has passed (RW 0x8C5C07 .. 0x8C5C5C)")
{
	CombatWorld w(kFreeLifeObjects.c_str());
	Object *hero = w.unit("Hero", 'A', 400, 400);
	hit(hero, 5000.0f);
	w.frames(51); // FreeLifeTime 10000 ms = 50 frames: strictly more
	hit(hero, 5000.0f);
	CHECK_FALSE(hero->isEffectivelyDead());
	CHECK(hero->getBodyModule()->getHealth() == 500.0f);
}

TEST_CASE("modgap1 FreeLifeBody: without the prerequisite upgrade the hit kills (RW 0x8C5BCD)")
{
	CombatWorld w(kFreeLifeObjects.c_str());
	Object *hero = w.unit("HeroLocked", 'A', 400, 400);
	hit(hero, 5000.0f);
	CHECK(hero->isEffectivelyDead());
}

TEST_CASE("modgap1 FreeLifeBody: FreeLifeInvincible ignores hits while the animation's condition holds (RW 0x8C5AE6)")
{
	CombatWorld w(kFreeLifeObjects.c_str());
	Object *hero = w.unit("HeroInvincible", 'A', 400, 400);
	hit(hero, 5000.0f);
	REQUIRE(hero->getBodyModule()->getHealth() == 500.0f);
	hit(hero, 100.0f); // the condition RESURRECTED lasts AnimTime 2000 ms
	CHECK(hero->getBodyModule()->getHealth() == 500.0f);
	w.frames(12);
	hit(hero, 100.0f);
	CHECK(hero->getBodyModule()->getHealth() == 400.0f);
}
