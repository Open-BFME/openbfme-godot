// OpenBFME unit tests (lane MODCOMPAT-MOD1): CommandButtonHuntUpdate (RW 0x898A5D / 0x898EE8 / 0x89903C / 0x898F8F / 0x898FCE / 0x898C17 / 0x898B7C),
// the map script action TEAM_HUNT_WITH_COMMAND_BUTTON (RW 0x7C46A0) and ActionManager::canDoSpecialPowerAtObject (RW 0x82D925). Synthetic data (no
// retail files); the expected values follow the RotWK functions cited at each check. GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CommandButtonHuntUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <string>
#include <vector>

using namespace scripttest;

namespace
{
const char kHunters[] = R"ini(
Object Hunter
 KindOf = INFANTRY SELECTABLE CAN_ATTACK
 CommandSet = HunterSet
 Geometry = CYLINDER
 GeometryMajorRadius = 8
 GeometryHeight = 20
 ArmorSet
  Conditions = None
  Armor = PlainArmor
 End
 WeaponSet
  Conditions = None
  Weapon = PRIMARY SwordWeapon
 End
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 100
 End
 Behavior = AIUpdateInterface ModuleTag_AI
 End
 Behavior = PhysicsBehavior ModuleTag_Physics
 End
 LocomotorSet
  Locomotor = WalkerLoco
  Condition = SET_NORMAL
  Speed = 55
 End
 Behavior = SpecialPowerModule ModuleTag_Power
  SpecialPowerTemplate = HuntKnife
  UpdateModuleStartsAttack = Yes
 End
 Behavior = SpecialAbilityUpdate ModuleTag_Ability
  SpecialPowerTemplate = HuntKnife
  StartAbilityRange = 10
  UnpackTime = 500
  PackTime = 500
 End
 Behavior = CommandButtonHuntUpdate ModuleTag_Hunt
  ScanRange = 400
 End
End
Object HunterNoModule
 KindOf = INFANTRY SELECTABLE CAN_ATTACK
 CommandSet = HunterSet
 Geometry = CYLINDER
 GeometryMajorRadius = 8
 GeometryHeight = 20
 Body = ActiveBody ModuleTag_Body
  MaxHealth = 100
 End
 Behavior = AIUpdateInterface ModuleTag_AI
 End
End
Object HuntDefaults
 KindOf = INFANTRY
 Behavior = CommandButtonHuntUpdate ModuleTag_Hunt
 End
 Behavior = CommandButtonHuntUpdate ModuleTag_Hunt2
  ScanRate = 2000
  ScanRange = 123.5
 End
End
)ini";

const char kPowersAndButtons[] = R"ini(
SpecialPower HuntKnife
 Enum = SPECIAL_KNIFE_ATTACK
 ReloadTime = 0
End
CommandButton Command_HuntKnife
 Command = SPECIAL_POWER
 SpecialPower = HuntKnife
 Options = NEED_TARGET_ENEMY_OBJECT
End
CommandButton Command_HuntKnifeNoTarget
 Command = SPECIAL_POWER
 SpecialPower = HuntKnife
End
CommandButton Command_HuntSword
 Command = FIRE_WEAPON
 WeaponSlot = PRIMARY
End
CommandButton Command_HuntStop
 Command = STOP
End
CommandButton Command_NotInSet
 Command = FIRE_WEAPON
 WeaponSlot = PRIMARY
End
CommandSet HunterSet
 1 = Command_HuntStop
 2 = Command_HuntKnife
 3 = Command_HuntSword
 4 = Command_HuntKnifeNoTarget
End
)ini";

struct HuntWorld
{
	combattest::CombatWorld w;
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	CommandStore store;
	CommandStore *savedStore = TheCommandStore;
	HuntWorld()
	{
		TheSpecialPowerStore = &powers;
		TheCommandStore = &store;
		store.setThingFactory(&w.w.things);
		w.w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		w.w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		std::string err = w.w.load(kPowersAndButtons, INI_LOAD_OVERWRITE, "hunt.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		err = w.w.load(kHunters, INI_LOAD_OVERWRITE, "hunters.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		w.logic->partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
		ScriptEngine::SideScripts side;
		side.sideName = "";
		static ScriptList empty;
		side.lists.push_back(&empty);
		w.logic->scriptEngine().newGame({ side }, nullptr, nullptr, false);
		w.combat().setAutoAcquireEnabled(false);
	}
	~HuntWorld()
	{
		TheSpecialPowerStore = savedPowers;
		TheCommandStore = savedStore;
	}
	ScriptEngine &engine() { return w.logic->scriptEngine(); }
	std::string teamA() { return w.teamA()->getControllingPlayer()->getPlayerName() + "/" + w.teamA()->getName(); }
	void hunt(const std::string &team, const std::string &button) { ScriptActions::execute(engine(), act("TEAM_HUNT_WITH_COMMAND_BUTTON", { sp(3, team), sp(42, button) })); }
	unsigned long long noteCount(const std::string &prefix) const
	{
		unsigned long long n = 0;
		for (const auto &kv : w.logic->scriptEngine().stats().notes)
		{
			n += kv.first.rfind(prefix, 0) == 0 ? kv.second : 0;
		}
		return n;
	}
};

template <class T>
T *moduleOf(Object *o)
{
	for (const auto &m : o->modules())
	{
		if (T *x = dynamic_cast<T *>(m.get()))
		{
			return x;
		}
	}
	return nullptr;
}

const CommandButtonHuntUpdateModuleData *huntData(const ThingTemplate *t, size_t which)
{
	size_t seen = 0;
	for (const ThingTemplate::Nugget &n : t->getFinalOverride()->behaviorModules().nuggets())
	{
		if (const auto *d = dynamic_cast<const CommandButtonHuntUpdateModuleData *>(n.data.get()))
		{
			if (seen++ == which)
			{
				return d;
			}
		}
	}
	return nullptr;
}
} // namespace

TEST_CASE("cbhunt: the data is RW 0x898A2D / table RW 0xC653F8 (ScanRate 5 frames, ScanRange 9999); the module sleeps from creation (RW 0x898A5D)")
{
	HuntWorld f;
	const CommandButtonHuntUpdateModuleData *d0 = huntData(f.w.w.get("HuntDefaults"), 0);
	const CommandButtonHuntUpdateModuleData *d1 = huntData(f.w.w.get("HuntDefaults"), 1);
	REQUIRE(d0);
	REQUIRE(d1);
	CHECK(d0->m_scanFrames == 5u); // LOGICFRAMES_PER_SECOND ([0xD9F608])
	CHECK(d0->m_scanRange == 9999.0f);
	CHECK(d1->m_scanFrames == 10u); // 2000 ms at 5 frames per second (RW 0x73A429)
	CHECK(d1->m_scanRange == 123.5f);
	Object *h = f.w.unit("Hunter", 'A', 200.0f, 200.0f);
	CommandButtonHuntUpdate *hunt = moduleOf<CommandButtonHuntUpdate>(h);
	REQUIRE(hunt);
	CHECK_FALSE(hunt->isUnported());
	CHECK(hunt->commandButtonName().empty());
	CHECK(hunt->commandButton() == nullptr);
	f.w.frames(3);
	CHECK(hunt->casts() == 0);
	CHECK(hunt->weaponFrames() == 0);
}

TEST_CASE("cbhunt: TEAM_HUNT_WITH_COMMAND_BUTTON (RW 0x7C46A0) with a SPECIAL_POWER button casts it at the best target of the scan (RW 0x898C17) at once and "
		  "again once the ability is done; another source's command ends the hunt (RW 0x89903C)")
{
	HuntWorld f;
	Object *h = f.w.unit("Hunter", 'A', 200.0f, 200.0f);
	Object *near = f.w.unit("Swordsman", 'B', 300.0f, 200.0f);
	Object *far = f.w.unit("Swordsman", 'B', 380.0f, 200.0f);
	Object *outside = f.w.unit("Swordsman", 'B', 700.0f, 200.0f); // beyond ScanRange 400
	Object *friendly = f.w.unit("Swordsman", 'A', 220.0f, 200.0f);
	(void)outside;
	(void)friendly;
	f.w.frames(2);
	CommandButtonHuntUpdate *hunt = moduleOf<CommandButtonHuntUpdate>(h);
	SpecialAbilityUpdate *ability = moduleOf<SpecialAbilityUpdate>(h);
	REQUIRE(hunt);
	REQUIRE(ability);
	// the scan alone: every enemy in range has the default priority 1 and mod 1 (AttackPriorityDistanceModifier 0: dist / 0 is inf, raised to 1.0),
	// so the first hit of the near-to-far walk wins and a later equal one does not replace it
	hunt->setCommandButton("Command_HuntStop"); // a STOP button: found, so the AI idles and update() runs once (nothing for STOP)
	REQUIRE(hunt->commandButton() != nullptr);
	CHECK(hunt->scanClosestTarget() == nullptr); // STOP has no power: RW 0x898C40
	f.hunt(f.teamA(), "Command_HuntKnife");
	REQUIRE(hunt->commandButton() != nullptr);
	CHECK(hunt->commandButtonName() == "Command_HuntKnife");
	CHECK(hunt->casts() == 1); // setCommandButton's update() (RW 0x898EE8) scanned and cast at once
	CHECK(ability->isActive());
	CHECK(f.engine().stats().unportedActions.count("TEAM_HUNT_WITH_COMMAND_BUTTON") == 0);
	// the friendly Swordsman is not a candidate: the HunterNoModule check below covers the module-less member note
	CHECK(f.noteCount("Error - Team hunt with command button - unit type 'Swordsman' is not valid for ability Command_HuntKnife") == 1);
	// while the ability is active the hunt only waits (RW 0x898FCE: the interface's vslot 2)
	f.w.frames(4);
	CHECK(hunt->casts() == 1);
	// the scan's pick is the near enemy
	ability->finishAbility(false, false);
	CHECK_FALSE(ability->isActive());
	CHECK(hunt->scanClosestTarget() == near);
	near->kill(DAMAGE_UNRESISTABLE, 0);
	f.w.frames(1);
	CHECK(hunt->scanClosestTarget() == far); // a dead object is not a candidate (RW 0xC10E20)
	// a command from a script ends the hunt at the next update (AI vslot 0x23C is not CMD_FROM_AI)
	h->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ 250.0f, 300.0f, 0.0f }, CMD_FROM_SCRIPT);
	f.w.frames(12);
	CHECK(hunt->commandButton() == nullptr);
	CHECK(hunt->commandButtonName().empty());
}

TEST_CASE("cbhunt: a FIRE_WEAPON button hunts with the weapon locked every frame (RW 0x898F8F); the action's refusals are debug lines (RW 0x7C46A0)")
{
	HuntWorld f;
	Object *h = f.w.unit("Hunter", 'A', 200.0f, 200.0f);
	Object *bare = f.w.unit("HunterNoModule", 'A', 230.0f, 200.0f);
	(void)bare;
	f.w.unit("Swordsman", 'B', 600.0f, 600.0f);
	f.w.frames(2);
	CommandButtonHuntUpdate *hunt = moduleOf<CommandButtonHuntUpdate>(h);
	REQUIRE(hunt);
	f.hunt(f.teamA(), "Command_HuntSword");
	REQUIRE(hunt->commandButton() != nullptr);
	CHECK(hunt->weaponFrames() == 1); // setCommandButton's update()
	CHECK(h->getAIUpdateInterface()->lastCommandSource() == CMD_FROM_AI);
	CHECK(h->getWeapons()->isCurWeaponLocked()); // RW 0x69121A(PRIMARY, LOCKED_TEMPORARILY)
	CHECK(f.noteCount("Error - Team hunt with command button - unit type 'HunterNoModule' requires CommandButtonHuntUpdate in .ini definition to hunt with Command_HuntSword") == 1);
	f.w.frames(3);
	CHECK(hunt->weaponFrames() == 4); // returns 1: every frame
	// the refusals: a STOP button, a SPECIAL_POWER button without a target option, a button no member's command set holds, an unknown button or team
	f.hunt(f.teamA(), "Command_HuntStop");
	f.hunt(f.teamA(), "Command_HuntKnifeNoTarget");
	CHECK(f.noteCount("ERROR-Team hunt with command button - cannot hunt with ability Command_HuntStop") == 1);
	CHECK(f.noteCount("ERROR-Team hunt with command button - cannot hunt with ability Command_HuntKnifeNoTarget") == 1);
	f.hunt(f.teamA(), "Command_NotInSet");
	CHECK(f.noteCount("Error - Team hunt with command button - unit type 'Hunter' is not valid for ability Command_NotInSet") == 1);
	f.hunt(f.teamA(), "Command_Unknown");
	f.hunt("NoSuchPlayer/NoSuchTeam", "Command_HuntSword");
	CHECK(hunt->commandButtonName() == "Command_HuntSword"); // none of them reached the module
	CHECK(f.engine().stats().unportedActions.empty());
}

TEST_CASE("cbhunt: ActionManager::canDoSpecialPowerAtObject (RW 0x82D925): the generic tests, the yes types, and the unported per-type tests counted as S-7140")
{
	HuntWorld f;
	Object *h = f.w.unit("Hunter", 'A', 200.0f, 200.0f);
	Object *enemy = f.w.unit("Swordsman", 'B', 260.0f, 200.0f);
	f.w.frames(2);
	const SpecialPowerTemplate *knife = f.powers.findSpecialPowerTemplate("HuntKnife");
	REQUIRE(knife);
	CHECK(ActionManager::canDoSpecialPowerAtObject(*h, enemy, CMD_FROM_AI, knife, 0, true)); // 0x47: yes
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtObject(*h, nullptr, CMD_FROM_AI, knife, 0, true));
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtObject(*h, enemy, CMD_FROM_AI, nullptr, 0, true));
	Object *bare = f.w.unit("Swordsman", 'A', 230.0f, 200.0f);
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtObject(*bare, enemy, CMD_FROM_AI, knife, 0, true));  // RW 0x68DF46: no power of the type
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtObject(*bare, enemy, CMD_FROM_AI, knife, 0, false)); // RW 0x68C26D: no module
	// the per-type tests not ported answer no, say so and count
	const unsigned long long before = ActionManager::stats().objectTypeTests;
	const unsigned long long before33 = ActionManager::stats().unportedObjectTypeTests[0x33];
	SpecialPowerTemplate *knifeMutable = const_cast<SpecialPowerTemplate *>(knife);
	const int savedType = knifeMutable->m_type;
	knifeMutable->m_type = 0x33;
	bool unported = false;
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtObject(*h, enemy, CMD_FROM_AI, knife, 0, true, &unported));
	CHECK(unported);
	CHECK(ActionManager::stats().objectTypeTests == before + 1);
	CHECK(ActionManager::stats().unportedObjectTypeTests[0x33] == before33 + 1);
	knifeMutable->m_type = 0x5; // a type without a case: no, not unported
	unported = false;
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtObject(*h, enemy, CMD_FROM_AI, knife, 0, false, &unported));
	CHECK_FALSE(unported);
	knifeMutable->m_type = savedType;
	const std::vector<std::string> lines = ActionManager::stopLines();
	REQUIRE(lines.size() == 5);
	CHECK(lines[4].rfind("[S-7140]", 0) == 0);
}

TEST_CASE("cbhunt: the snapshot (RW 0x898B7C's fields) round-trips and a load finds the button again in the command set")
{
	HuntWorld f;
	Object *h = f.w.unit("Hunter", 'A', 200.0f, 200.0f);
	f.w.frames(2);
	CommandButtonHuntUpdate *hunt = moduleOf<CommandButtonHuntUpdate>(h);
	REQUIRE(hunt);
	f.hunt(f.teamA(), "Command_HuntSword");
	f.w.frames(2);
	REQUIRE(hunt->commandButton() != nullptr);
	const unsigned long long weaponFrames = hunt->weaponFrames();
	XferSave saved;
	hunt->xfer(saved);
	f.w.frames(3);
	h->getAIUpdateInterface()->aiIdle(CMD_FROM_SCRIPT);
	f.w.frames(2);
	REQUIRE(hunt->commandButton() == nullptr); // the script's command ended the hunt
	{
		XferLoad load(saved.data());
		hunt->xfer(load);
	}
	// the module's own state comes back (the scheduler's vector index and phase are the scheduler block's, not the module's: not compared here)
	CHECK(hunt->weaponFrames() == weaponFrames);
	CHECK(hunt->commandButtonName() == "Command_HuntSword");
	REQUIRE(hunt->commandButton() != nullptr);
	CHECK(hunt->commandButton()->m_name == "Command_HuntSword");
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(hunt->xfer(lt), XferException);
}
