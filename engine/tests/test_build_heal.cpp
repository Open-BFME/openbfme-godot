// OpenBFME unit tests (lane BUILD-2): GettingBuiltBehavior's HealWeapon (RW 0x857F82..0x857FAA): once per build, when the percent reaches 75, the module fires its HealWeapon
// from the structure's position (TheWeaponStore createAndFireTempWeapon RW 0x6CF530; the port delivers the weapon's damage nuggets, stop S-651). A synthetic healing pulse
// heals a damaged soldier next to a rising tower exactly once.  GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "ProdTestUtil.h"

#include "GameLogic/Module/ConstructionModules.h"

namespace
{
const char kTower[] =
	"Object HealTower\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE\n"
	"  BuildCost = 100\n"
	"  BuildTime = 4.0\n"
	"  Behavior = GettingBuiltBehavior ModuleTag_GB\n"
	"    HealWeapon = BuildPulse\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 200\n"
	"  End\n"
	"End\n";
const char kPulse[] =
	"Weapon BuildPulse\n"
	"  AttackRange = 100\n"
	"  DamageNugget\n"
	"    Damage = 25\n"
	"    Radius = 100.0\n"
	"    DamageType = HEALING\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n";
} // namespace

TEST_CASE("build rate: the HealWeapon fires once when a self-built structure reaches 75 percent and heals around it")
{
	combattest::CombatWorld f(kTower);
	f.logic->productionSettings() = prodtest::defaultSettings();
	const std::string err = f.w.load(kPulse, INI_LOAD_OVERWRITE, "heal.ini");
	REQUIRE_MESSAGE(err.empty(), err);
	Object *soldier = f.unit("Dummy", 'A', 520.0f, 500.0f);
	Object *tower = f.unit("HealTower", 'A', 500.0f, 500.0f);
	REQUIRE((soldier && tower));
	DamageInfo hurt;
	hurt.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	hurt.m_input.m_amount = 60.0f;
	soldier->attemptDamage(hurt);
	f.logic->runLogicFrame();
	REQUIRE(f.health(soldier) == 40.0f);
	// the tower becomes a foundation at 1.0 health (as a plot's, RW 0x85895D) and builds itself: 20 frames, 10 health per frame
	ActiveBody *body = dynamic_cast<ActiveBody *>(tower->getBodyModule());
	body->internalChangeHealth(1.0f - body->getHealth());
	tower->setConstructionPercent(0.0f);
	tower->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(tower->findModule("GettingBuiltBehavior"));
	REQUIRE(gb != nullptr);
	float soldierAtFire = -1.0f;
	for (int i = 0; i < 120 && tower->getConstructionPercent() != -1.0f; ++i)
	{
		const UnsignedInt before = gb->healWeaponShots();
		f.logic->runLogicFrame();
		if (gb->healWeaponShots() != before)
		{
			soldierAtFire = f.health(soldier);
			CHECK(tower->getConstructionPercent() >= 75.0f);
		}
	}
	CHECK(tower->getConstructionPercent() == -1.0f);
	CHECK(gb->healWeaponShots() == 1u);
	CHECK(soldierAtFire == 65.0f); // 40 + 25 (HEALING through the nugget)
	CHECK(f.health(soldier) == 65.0f);
	CHECK(f.logic->report().errors.empty());
}
