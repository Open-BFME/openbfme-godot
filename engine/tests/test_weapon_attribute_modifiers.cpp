// OpenBFME unit tests. RANGE (7) and RATE_OF_FIRE (0x15) attribute modifiers
// in WeaponRangeScale (RW 0x6CA61F -> 0x6CA641) and Weapon::privateFireWeapon (RW 0x6CEF6D -> 0x6CF042 -> 0x6CA066).
// Synthetic INI only, no retail files required.
// GPL-3.0.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/WeaponState.h"

#include <string>

using namespace combattest;

namespace
{

const char kLocal85Modifiers[] =
	"ModifierList Local85Range25\n"
	"  Category = BUFF\n"
	"  Modifier = RANGE 25%\n"
	"  Duration = 0\n"
	"End\n"
	"ModifierList Local85Range10\n"
	"  Category = LEADERSHIP\n"
	"  Modifier = RANGE 10%\n"
	"  Duration = 0\n"
	"End\n"
	"ModifierList Local85Rof200\n"
	"  Category = BUFF\n"
	"  Modifier = RATE_OF_FIRE 200%\n"
	"  Duration = 0\n"
	"End\n"
	"ModifierList Local85Rof50\n"
	"  Category = LEADERSHIP\n"
	"  Modifier = RATE_OF_FIRE 50%\n"
	"  Duration = 0\n"
	"End\n";

const char kLocal85Objects[] =
	"Object Local85Archer\n"
	"  KindOf = INFANTRY ARCHER SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 300\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 5\n"
	"  GeometryMinorRadius = 5\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY BowWeapon\n"
	"  End\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HumanLocomotor\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 40\n"
	"  End\n"
	"End\n"
	"Object Local85Horde\n"
	"  KindOf = INFANTRY ARCHER SELECTABLE CAN_ATTACK HORDE\n"
	"  VisionRange = 300\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY HordeRangefinder\n"
	"  End\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Horde\n"
	"    ObjectStatusOfContained =\n"
	"    InitialPayload = Local85Archer 2\n"
	"    Slots = 2\n"
	"    PassengerFilter = NONE +INFANTRY\n"
	"    RankInfo = RankNumber:1 UnitType:Local85Archer Position:X:0 Y:0 Position:X:0 Y:15\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HumanLocomotor\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 40\n"
	"  End\n"
	"End\n";

struct Local85World : CombatWorld
{
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;

	Local85World()
	{
		TheAttributeModifierStore = &modifiers;
		modifiers.registerBlock(w.fx.env.blocks);
		const std::string err = w.load(std::string(kLocal85Modifiers) + "\n" + kLocal85Objects);
		REQUIRE_MESSAGE(err.empty(), err);
		frames(1);
	}
	~Local85World() { TheAttributeModifierStore = savedModifiers; }
	void step(int n) { frames(n); }
};

} // namespace

TEST_CASE("weapon attribute modifiers: RANGE scales weapon attack range by (1 + sum) (RW 0x6CA61F / RW 0x6CA641)")
{
	Local85World world;
	Object *archer = world.unit("Local85Archer", 'A', 100.0f, 100.0f);
	Object *target = world.unit("Local85Archer", 'B', 330.0f, 100.0f);
	REQUIRE(archer != nullptr);
	REQUIRE(target != nullptr);
	REQUIRE(archer->getWeapons() != nullptr);

	// BowWeapon: AttackRange = 200, kUndersize = 2.5 => base 197.5 (RW 0x6C9F5B).
	const float baseRange = archer->getWeapons()->currentAttackRange();
	CHECK(baseRange == 197.5f);
	CHECK(archer->getWeapons()->currentAttackRangeNoTarget() == 197.5f);
	CHECK_FALSE(archer->getWeapons()->isWithinAttackRange(*target));

	// RW 0x6CA641 -> 0x68C818(7, &sum, nullptr): +25% => scale = 1.0 + 0.25 = 1.25.
	REQUIRE(archer->addAttributeModifier("Local85Range25", -1));
	CHECK(archer->getWeapons()->currentAttackRange() == 246.875f);
	// RW 0x6CA935 -> 0x6CA701 applies WeaponRangeScale twice.
	CHECK(archer->getWeapons()->currentAttackRangeNoTarget() == 308.59375f);
	CHECK(archer->getWeapons()->isWithinAttackRange(*target));

	// Additive sum across categories: +25% + 10% => scale = 1.35.
	REQUIRE(archer->addAttributeModifier("Local85Range10", -1));
	CHECK(archer->getWeapons()->currentAttackRange() == SimMath::mulf32(SimMath::addf32(SimMath::addf32(0.25f, 0.10f), 1.0f), 197.5f));

	// Removal restores baseline range.
	archer->removeAttributeModifier("Local85Range10");
	archer->removeAttributeModifier("Local85Range25");
	CHECK(archer->getWeapons()->currentAttackRange() == 197.5f);
	CHECK_FALSE(archer->getWeapons()->isWithinAttackRange(*target));
}

TEST_CASE("weapon attribute modifiers: RATE_OF_FIRE scales delay between shots, not clip reload (RW 0x6CEF6D / RW 0x6CF042 / RW 0x6CA066)")
{
	Local85World world;
	Object *archer = world.unit("Local85Archer", 'A', 100.0f, 100.0f);
	Object *target = world.unit("Local85Archer", 'B', 200.0f, 100.0f);
	REQUIRE(archer != nullptr);
	REQUIRE(target != nullptr);
	ObjectWeapons *weapons = archer->getWeapons();
	REQUIRE(weapons != nullptr);
	Weapon *bow = weapons->currentWeapon();
	REQUIRE(bow != nullptr);

	// BowWeapon: DelayBetweenShots = 800 ms = 4 frames, ClipSize = 2, ClipReloadTime = 2000 ms = 10 frames (10 - 10%3 = 9 frames at RW 0x6CA0F9).
	// Shot 1 without modifier: delay = floor(4 / 1.0) = 4 frames.
	weapons->reloadAllAmmo(true);
	const std::uint32_t frame0 = world.logic->getFrame();
	CHECK_FALSE(weapons->fireCurrentWeapon(target, nullptr));
	CHECK(bow->storedStatus() == WEAPON_BETWEEN_FIRING_SHOTS);
	CHECK(bow->whenWeCanFireAgain() == frame0 + 4u);

	// Advance past delay, reload full clip.
	world.step(10);
	weapons->reloadAllAmmo(true);

	// Apply RATE_OF_FIRE 200% (factor = 2.0): RW 0x6CF042 -> 0x68C82D(0x15, &factor, nullptr, true),
	// RW 0x6CA066: floor(4 / (1.0 * 2.0)) = 2 frames.
	REQUIRE(archer->addAttributeModifier("Local85Rof200", -1));
	const std::uint32_t frame1 = world.logic->getFrame();
	CHECK_FALSE(weapons->fireCurrentWeapon(target, nullptr));
	CHECK(bow->storedStatus() == WEAPON_BETWEEN_FIRING_SHOTS);
	CHECK(bow->whenWeCanFireAgain() == frame1 + 2u);

	// Shot 2 empties the 2-shot clip -> reloadAmmo (RW 0x6CEE4C -> 0x6CA0C0 getClipReloadTime):
	// RW 0x6CA0C0 takes only WeaponBonus*, NOT the RATE_OF_FIRE attribute modifier, so reload stays 9 frames.
	world.step(2);
	const std::uint32_t frame2 = world.logic->getFrame();
	CHECK(weapons->fireCurrentWeapon(target, nullptr));
	CHECK(bow->storedStatus() == WEAPON_RELOADING_CLIP);
	CHECK(bow->whenWeCanFireAgain() == frame2 + 9u);

	// Multiplicative product across categories: 200% * 50% = 100% => delay = 4 frames again.
	world.step(10);
	weapons->reloadAllAmmo(true);
	REQUIRE(archer->addAttributeModifier("Local85Rof50", -1));
	const std::uint32_t frame3 = world.logic->getFrame();
	CHECK_FALSE(weapons->fireCurrentWeapon(target, nullptr));
	CHECK(bow->storedStatus() == WEAPON_BETWEEN_FIRING_SHOTS);
	CHECK(bow->whenWeCanFireAgain() == frame3 + 4u);
}

TEST_CASE("weapon attribute modifiers: horde member reads its own pool after horde buff propagation (RW 0x68F1A8 / RW 0x6CA641)")
{
	Local85World world;
	Object *horde = world.unit("Local85Horde", 'A', 100.0f, 100.0f);
	REQUIRE(horde != nullptr);
	ContainModuleInterface *contain = horde->getContain();
	REQUIRE(contain != nullptr);
	REQUIRE(contain->getHordeContainInterface() != nullptr);
	world.step(1);

	const ContainModuleInterface::ContainedItemsList *items = contain->getContainedItemsList();
	REQUIRE(items != nullptr);
	REQUIRE(!items->empty());
	Object *member = items->front();
	REQUIRE(member != nullptr);
	REQUIRE(member->getWeapons() != nullptr);
	CHECK(member->getWeapons()->currentAttackRange() == 197.5f);

	// Adding modifier to the horde propagates to each member's own pool (RW 0x68F1A8 -> HordeContain RW 0x870E50),
	// and WeaponRangeScale (RW 0x6CA641) reads the shooting member's own pool.
	REQUIRE(horde->addAttributeModifier("Local85Range25", -1));
	CHECK(member->getWeapons()->currentAttackRange() == 246.875f);
}
