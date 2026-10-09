// OpenBFME. GPL-3.0.
// Lane UPGRADE-1: the upgrade module classes, the object / player upgrade flow and the state hash on synthetic data (econtest::Fx: a logic world with
// the live module classes bound and TheUpgradeCenter of the fixture).

#include "doctest.h"

#include "EconTestUtil.h"

#include "Common/Player.h"
#include "Common/Upgrade.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/UpgradeModuleClasses.h"
#include "GameLogic/Object/Object.h"

#include <cstring>

namespace
{
const char *kObjects =
	"Upgrade Upgrade_A\n  Type = OBJECT\nEnd\n"
	"Upgrade Upgrade_B\n  Type = OBJECT\nEnd\n"
	"Upgrade Upgrade_Level2\n  Type = OBJECT\nEnd\n"
	"Upgrade Upgrade_Level3\n  Type = OBJECT\nEnd\n"
	"Upgrade Upgrade_Hat1\n  Type = OBJECT\n  GroupName = Hats\nEnd\n"
	"Upgrade Upgrade_Hat2\n  Type = OBJECT\n  GroupName = Hats\nEnd\n"
	"Upgrade Upgrade_Research\n  Type = PLAYER\n  BuildCost = 100\n  BuildTime = 7.5\nEnd\n"
	"Object Upgradable\n"
	"  KindOf = SELECTABLE STRUCTURE\n"
	"  CommandSet = BaseSet\n"
	"  Behavior = StatusBitsUpgrade ModuleTag_Status\n"
	"    TriggeredBy = Upgrade_A\n"
	"    StatusToSet = UNSELECTABLE\n"
	"    StatusToClear = NO_COLLISIONS\n"
	"  End\n"
	"  Behavior = ModelConditionUpgrade ModuleTag_MC\n"
	"    TriggeredBy = Upgrade_A Upgrade_Research\n"
	"    AddConditionFlags = USER_1\n"
	"    RemoveConditionFlags = USER_2\n"
	"    RemoveConditionFlagsInRange = USER_3 USER_5\n"
	"  End\n"
	"  Behavior = ArmorUpgrade ModuleTag_Armor\n"
	"    TriggeredBy = Upgrade_B\n"
	"    ArmorSetFlag = PLAYER_UPGRADE\n"
	"  End\n"
	"  Behavior = CommandSetUpgrade ModuleTag_Set2\n"
	"    TriggeredBy = Upgrade_Level2\n"
	"    ConflictsWith = Upgrade_Level3\n"
	"    CommandSet = Level2Set\n"
	"  End\n"
	"  Behavior = CommandSetUpgrade ModuleTag_Set3\n"
	"    TriggeredBy = Upgrade_Level3\n"
	"    CommandSet = Level3Set\n"
	"  End\n"
	"  Behavior = SubObjectsUpgrade ModuleTag_Banner\n"
	"    TriggeredBy = Upgrade_B\n"
	"    ConflictsWith = Upgrade_Level3\n"
	"    ShowSubObjects = Banner\n"
	"  End\n"
	"  Behavior = RemoveUpgradeUpgrade ModuleTag_Remove\n"
	"    TriggeredBy = Upgrade_Hat2\n"
	"    UpgradeToRemove = Upgrade_A\n"
	"    UpgradeGroupsToRemove = Hats\n"
	"  End\n"
	"  Behavior = GrantUpgradeCreate ModuleTag_Grant\n"
	"    UpgradeToGrant = Upgrade_Hat1\n"
	"    GiveOnBuildComplete = Yes\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object Plain\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Behavior = GrantUpgradeCreate ModuleTag_Grant\n"
	"    UpgradeToGrant = Upgrade_B\n"
	"    ExemptStatus = UNDER_CONSTRUCTION\n"
	"  End\n"
	"  Behavior = ArmorUpgrade ModuleTag_Armor\n"
	"    TriggeredBy = Upgrade_Research\n"
	"    ArmorSetFlag = PLAYER_UPGRADE_2\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 10\n"
	"  End\n"
	"End\n";

int conditionBit(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::strcmp(TheModelConditionNames[i], name) == 0)
		{
			return i;
		}
	}
	return -1;
}

int armorBit(const char *name)
{
	for (int i = 0; TheArmorSetNames[i]; ++i)
	{
		if (std::strcmp(TheArmorSetNames[i], name) == 0)
		{
			return i;
		}
	}
	return -1;
}

const UpgradeTemplate *up(const char *name)
{
	const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(name);
	REQUIRE_MESSAGE(u, name);
	return u;
}
} // namespace

TEST_CASE("upgrade modules: StatusBits, ModelCondition (with RemoveConditionFlagsInRange) and Armor upgrades apply and undo on the object")
{
	econtest::Fx f(kObjects);
	Object *o = f.make("Upgradable", f.teamOf("Alice"));
	const unsigned kUnselectable = 3, kNoCollisions = 4;
	o->setStatus(kNoCollisions, true);
	const int user1 = conditionBit("USER_1"), user2 = conditionBit("USER_2"), user3 = conditionBit("USER_3"), user4 = conditionBit("USER_4"), user5 = conditionBit("USER_5");
	REQUIRE(user5 > user3);
	for (int b : { user2, user3, user4, user5 })
	{
		o->setModelConditionState(b, true);
	}
	const std::uint32_t h0 = f.logic->computeStateHash();
	o->giveUpgrade(up("Upgrade_A"));
	CHECK(f.logic->computeStateHash() != h0);
	CHECK(o->testStatus(kUnselectable));
	CHECK_FALSE(o->testStatus(kNoCollisions));
	CHECK(o->testModelCondition(user1));
	for (int b : { user3, user4, user5 })
	{
		CHECK_FALSE(o->testModelCondition(b)); // the range USER_3 .. USER_5 is the remove set
	}
	CHECK(o->testModelCondition(user2)); // a plain flag list replaces the flags before it (RW 0x4B8B37): the range line dropped RemoveConditionFlags' USER_2
	o->removeUpgrade(up("Upgrade_A"));
	CHECK_FALSE(o->testStatus(kUnselectable));
	CHECK(o->testStatus(kNoCollisions));
	CHECK_FALSE(o->testModelCondition(user1));
	CHECK(o->testModelCondition(user4));

	const int pu = armorBit("PLAYER_UPGRADE");
	o->giveUpgrade(up("Upgrade_B"));
	CHECK((o->armorSetFlags() & (1u << pu)) != 0);
	CHECK(o->testModelCondition(ArmorUpgrade::modelConditionOfArmorSetFlag(pu)));
	o->removeUpgrade(up("Upgrade_B"));
	CHECK((o->armorSetFlags() & (1u << pu)) == 0);
}

TEST_CASE("upgrade modules: CommandSetUpgrade follows the levels through postUpgradeCheck (Level2, then Level3 conflicts it away)")
{
	econtest::Fx f(kObjects);
	Object *o = f.make("Upgradable", f.teamOf("Alice"));
	CHECK(o->getCommandSetName() == "BaseSet");
	o->giveUpgrade(up("Upgrade_Level2"));
	CHECK(o->getCommandSetName() == "Level2Set");
	o->giveUpgrade(up("Upgrade_Level3"));
	CHECK(o->getCommandSetName() == "Level3Set");
	auto *sub = dynamic_cast<SubObjectsUpgrade *>(o->findModuleByTag(f.w.keys.nameToKey("ModuleTag_Banner")));
	REQUIRE(sub);
	o->giveUpgrade(up("Upgrade_B"));
	CHECK_FALSE(sub->hasShown()); // RW 0x8B8FD7: the object holds a ConflictsWith upgrade (Level3)
	o->removeUpgrade(up("Upgrade_Level3"));
	// RW quirk (0x691438 -> 0x8D2901 clears the executed flag before 0x8D2688 runs the removal, which tests it): the Level3 set stays until the next update
	CHECK(o->getCommandSetName() == "Level3Set");
	o->updateUpgradeModules();
	CHECK(o->getCommandSetName() == "Level2Set");
}

TEST_CASE("upgrade modules: RemoveUpgradeUpgrade takes named upgrades and groups away; GrantUpgradeCreate grants on build complete / on create")
{
	econtest::Fx f(kObjects);
	Object *o = f.make("Upgradable", f.teamOf("Alice"));
	CHECK_FALSE(o->hasUpgrade(up("Upgrade_Hat1")));
	o->friend_onBuildComplete(); // GiveOnBuildComplete
	CHECK(o->hasUpgrade(up("Upgrade_Hat1")));
	o->friend_onBuildComplete(); // once
	o->giveUpgrade(up("Upgrade_A"));
	CHECK(o->hasUpgrade(up("Upgrade_A")));
	o->giveUpgrade(up("Upgrade_Hat2")); // triggers the RemoveUpgradeUpgrade: Upgrade_A and the other Hats go, its own trigger stays
	CHECK_FALSE(o->hasUpgrade(up("Upgrade_A")));
	CHECK_FALSE(o->hasUpgrade(up("Upgrade_Hat1")));
	CHECK(o->hasUpgrade(up("Upgrade_Hat2")));

	// ExemptStatus UNDER_CONSTRUCTION: granted at creation by an object that is not under construction
	Object *p = f.make("Plain", f.teamOf("Alice"));
	CHECK(p->hasUpgrade(up("Upgrade_B")));
}

TEST_CASE("upgrade modules: a player upgrade reaches every object of the player and the ones made later; records, masks and the hash")
{
	econtest::Fx f(kObjects);
	Player *alice = f.players.findPlayerWithName("Alice");
	Object *a = f.make("Plain", f.teamOf("Alice"));
	Object *b = f.make("Plain", f.teamOf("Bob"));
	const int pu2 = armorBit("PLAYER_UPGRADE_2");
	const UpgradeTemplate *research = up("Upgrade_Research");
	CHECK(research->calcCostToBuild(alice, nullptr) == 100);
	CHECK(research->calcTimeToBuild(alice) == 37); // cvttss2si(5.0f * 7.5f)
	const std::uint32_t h0 = f.logic->computeStateHash();
	alice->addUpgrade(research, Player::UPGRADE_STATUS_IN_PRODUCTION);
	CHECK(f.logic->computeStateHash() != h0);
	CHECK(alice->hasUpgradeInProduction(research));
	CHECK((a->armorSetFlags() & (1u << pu2)) == 0);
	alice->addUpgrade(research, Player::UPGRADE_STATUS_COMPLETE);
	CHECK_FALSE(alice->hasUpgradeInProduction(research));
	CHECK(alice->hasUpgradeComplete(research));
	CHECK(alice->upgradeStatus(research) == Player::UPGRADE_STATUS_COMPLETE);
	CHECK(alice->upgradeEvaEventsNotPlayed() == 1);
	CHECK((a->armorSetFlags() & (1u << pu2)) != 0);
	CHECK((b->armorSetFlags() & (1u << pu2)) == 0); // Bob's object
	Object *later = f.make("Plain", f.teamOf("Alice"));
	CHECK((later->armorSetFlags() & (1u << pu2)) != 0); // initObject (RW 0x693D38)
	alice->removeUpgrade(research);
	CHECK_FALSE(alice->hasUpgradeComplete(research));
	CHECK((a->armorSetFlags() & (1u << pu2)) == 0);
	CHECK(alice->upgradeStatus(research) == Player::UPGRADE_STATUS_INVALID);
}

TEST_CASE("upgrade modules: two identical runs give the same state hash; the S-480 .. S-486 lines are in the logic report")
{
	std::uint32_t hashes[2] = {};
	for (int run = 0; run < 2; ++run)
	{
		econtest::Fx f(kObjects);
		Object *o = f.make("Upgradable", f.teamOf("Alice"));
		f.make("Plain", f.teamOf("Alice"));
		o->giveUpgrade(up("Upgrade_Level2"));
		o->giveUpgrade(up("Upgrade_A"));
		f.players.findPlayerWithName("Alice")->addUpgrade(up("Upgrade_Research"), Player::UPGRADE_STATUS_COMPLETE);
		for (int i = 0; i < 5; ++i)
		{
			f.logic->runLogicFrame();
		}
		hashes[run] = f.logic->computeStateHash();
		if (run == 1)
		{
			const GameLogic::Report r = f.logic->report();
			for (const char *id : { "[S-480]", "[S-481]", "[S-482]", "[S-483]", "[S-484]", "[S-485]", "[S-486]" })
			{
				bool found = false;
				for (const std::string &s : r.stops)
				{
					found = found || s.rfind(id, 0) == 0;
				}
				CHECK_MESSAGE(found, id);
			}
		}
	}
	CHECK(hashes[0] == hashes[1]);
}
