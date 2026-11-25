// OpenBFME: retail 2.01 InactiveBody armor-set flags and damage answers. Synthetic INI, no game files.
// Every expectation is taken from the RW instructions quoted beside it; no game or mod install is needed.
#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"

using namespace combattest;

namespace
{
const char kInactive[] =
	"Object BridgePiece\n"
	"  KindOf = STRUCTURE IMMOBILE\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryMinorRadius = 10\n"
	"  GeometryHeight = 10\n"
	"  Body = InactiveBody ModuleTag_Body\n"
	"  End\n"
	"  Behavior = ArmorUpgrade ModuleTag_Armor\n"
	"    ArmorSetFlag = PLAYER_UPGRADE\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Die\n"
	"  End\n"
	"End\n"
	"Object Guard\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = ArmorUpgrade ModuleTag_Armor\n"
	"    ArmorSetFlag = PLAYER_UPGRADE\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Die\n"
	"  End\n"
	"End\n";

DamageInfo hit(const Object *source, float amount, int type)
{
	DamageInfo d;
	d.m_input.m_sourceID = source ? source->getID() : 0;
	d.m_input.m_amount = amount;
	d.m_input.m_damageType = type;
	return d;
}
} // namespace

TEST_CASE("PORT-302: an armor upgrade cannot give an InactiveBody an armor-set condition")
{
	CombatWorld w(kInactive);
	Object *bridge = w.unit("BridgePiece", 'B', 300, 300);
	Object *guard = w.unit("Guard", 'A', 200, 300);
	REQUIRE(bridge != nullptr);
	REQUIRE(guard != nullptr);
	const InactiveBody *body = dynamic_cast<const InactiveBody *>(bridge->getBodyModule());
	REQUIRE(body != nullptr);
	CHECK(body->ignoresArmorSetFlagChanges());
	const int bit = CombatNames::armorSetBit("PLAYER_UPGRADE");
	const ArmorSetFlags flag = static_cast<ArmorSetFlags>(1u << bit);
	CHECK(bridge->armorSetFlags() == 0);

	// RW 0x8B761E ArmorUpgrade::apply calls body slot 0x34 (RW 0x8B76A5); InactiveBody's slot is the `ret 4` stub
	// RW 0x9F3A3C and its slot 0x3C (RW 0x8C1AC3) reads back zero flags. The port stores the bits on Object,
	// so the write must be refused there for this body (the test fails without that change).
	ArmorUpgrade *bridgeUpgrade = dynamic_cast<ArmorUpgrade *>(bridge->findModule("ArmorUpgrade"));
	REQUIRE(bridgeUpgrade != nullptr);
	bridgeUpgrade->giveSelfUpgrade();
	CHECK(bridge->armorSetFlags() == 0);
	bridge->setArmorSetFlag(bit, true);
	CHECK(bridge->armorSetFlags() == 0);

	// The neighbour keeps retail's behaviour: a body that implements the slots stores the condition.
	ArmorUpgrade *guardUpgrade = dynamic_cast<ArmorUpgrade *>(guard->findModule("ArmorUpgrade"));
	REQUIRE(guardUpgrade != nullptr);
	guardUpgrade->giveSelfUpgrade();
	CHECK(guard->armorSetFlags() == flag);
	guard->setArmorSetFlag(bit, false);
	CHECK(guard->armorSetFlags() == 0);
}

TEST_CASE("PORT-302: an InactiveBody ignores weapons and healing and dies once to UNRESISTABLE damage")
{
	CombatWorld w(kInactive);
	Object *bridge = w.unit("BridgePiece", 'B', 300, 300);
	Object *swordsman = w.unit("Guard", 'A', 200, 300);
	REQUIRE(bridge != nullptr);
	REQUIRE(swordsman != nullptr);
	BodyModuleInterface *body = bridge->getBodyModule();
	REQUIRE(body != nullptr);
	// RW 0x8C1A43: the constructor marks the object dead; RW 0x70D41A: the body's health and max health are zero.
	CHECK(bridge->isEffectivelyDead());
	CHECK(body->getHealth() == 0.0f);
	CHECK(body->getMaxHealth() == 0.0f);
	CHECK(body->getDamageState() == BODY_PRISTINE);

	// RW 0x8C18FE: only type 8 has a nonzero estimate, RW 0x8C1A92/0x8C1AA2: other hits have noEffect set.
	DamageInfo slash = hit(swordsman, 500.0f, DAMAGE_SLASH);
	CHECK(body->estimateDamage(slash.m_input) == 0.0f);
	body->attemptDamage(slash);
	CHECK(slash.m_output.m_actualDamageDealt == 0.0f);
	CHECK(slash.m_output.m_actualDamageClipped == 0.0f);
	CHECK(slash.m_output.m_noEffect);

	// RW 0x8C191D / 0x8C1935: healing writes the same zero answer, not a health increase.
	DamageInfo heal = hit(swordsman, 10.0f, DAMAGE_HEALING);
	body->attemptHealing(heal);
	CHECK(heal.m_output.m_actualDamageDealt == 0.0f);
	CHECK(heal.m_output.m_noEffect);
	CHECK(body->getHealth() == 0.0f);

	// RW 0x8C1A75 / 0x8C1AA8: UNRESISTABLE clears noEffect and runs onDie once (RW 0x8C1ABB sets dieCalled).
	DamageInfo kill = hit(swordsman, 5.0f, DAMAGE_UNRESISTABLE);
	CHECK(body->estimateDamage(kill.m_input) == 5.0f);
	body->attemptDamage(kill);
	CHECK_FALSE(kill.m_output.m_noEffect);
	CHECK(w.combat().counters().kills == 1);
	DamageInfo again = hit(swordsman, 5.0f, DAMAGE_UNRESISTABLE);
	body->attemptDamage(again);
	CHECK_FALSE(again.m_output.m_noEffect);
	CHECK(w.combat().counters().kills == 1);
}
