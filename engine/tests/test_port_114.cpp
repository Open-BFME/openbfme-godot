// OpenBFME unit tests (lane PORT-114, issue fork issue 298): GameData's global WeaponBonus set and the object side that applies it.
//
// RotWK 2.01 facts this lane is built on (addresses the owner's machine re-reads with `tools/mod/rw.py ... dis`):
//   * the GameData row `WeaponBonus` = RW 0xC004D0 (0xBFF580 + 16 * 245, the donor table's own index, gamedata-rows.md), parser
//     RW 0x6CB559 = WeaponBonusSet::parseWeaponBonusSetPtr, the member GlobalData + 0xAD0 (arena-triage/batch-23.md row 5);
//   * one entry per line through RW 0x6CA45B (`<CONDITION> <FIELD> <percent>`, the same routine the Weapon block's own row RW 0x6CB5AB calls);
//   * the only reader: Weapon::computeBonus RW 0x6CA7AC loads `mov ecx,[eax+0xAD0]` with eax = [0xDE4364] at RW 0x6CA7CF, tests the pointer and adds the
//     set with RW 0x6CA4B5 (the set's own loop RW 0x6CA434: bonus[k] += set[cond][k] - 1.0 per satisfied condition).
// STOP S-320 named the whole global set as missing. This lane ports the ROW and the pointer: the set now exists where RW keeps it, and an object's weapon
// adds it for the object's weapon bonus condition mask (RW Object + 0x39C, ported since WEAPON-1).
//
// What the port does NOT do yet, and why these tests do not claim it: WHO sets the condition bits of a horde's members and of the battle plans. In this port
// the only writers of Object + 0x39C are GarrisonContain (GARRISONED, bit 0, RW 0x87BF01), SlavedUpdate (DRONE_SPOTTING, RW 0x8E...), the WeaponBonusUpgrade
// shell (PLAYER_UPGRADE), the FiringTracker (CONTINUOUS_FIRE_MEAN / FAST, RW 0x8E3174) and the veterancy path; nothing sets HORDE (bit 1), NATIONALISM (4),
// ENTHUSIASTIC (8), SUBLIMINAL (15) or BATTLEPLAN_* (12..14), and `TheWeaponBonusUpgrade` has no runtime here. The image is not reachable from this sandbox
// (Google Drive is blocked, no game files), so the setter chain of those bits was NOT read in RW and is not invented here: the tests drive the mask the game
// itself would set. Stop S-7287 holds that half.
// GPL-3.0.

#include "doctest.h"

#include "CombatTestUtil.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <string>

using namespace combattest;

namespace
{
// A GameData block the loader accepts: the five rows the port requires, then whatever the test adds.
std::string gameDataWith(const std::string &extra)
{
	return "GameData\n"
		"  ForceModelsToFollowTimeOfDay = Yes\n"
		"  ForceModelsToFollowWeather = No\n"
		"  UnitDamagedThreshold = 0.5\n"
		"  UnitReallyDamagedThreshold = 0.25\n"
		"  DefaultStartingCash = 700\n" +
		extra +
		"End\n";
}

GameLogicSettings scanOrFail(const std::string &text)
{
	GameLogicSettings g;
	std::string err;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanGameData(text, g, &err), err);
	return g;
}

// the set a GameData text builds, put into a live world's settings (the rest of the fixture's GameData values stay)
void installSet(GameLogic &logic, const std::string &extra)
{
	GameLogicSettings g = scanOrFail(gameDataWith(extra));
	logic.settings().globalWeaponBonusSet = g.globalWeaponBonusSet;
	logic.settings().weaponBonusLoaded = g.weaponBonusLoaded;
}

const int kHorde = INI::scanIndexList("HORDE", TheWeaponBonusConditionNames);
const int kGarrisoned = INI::scanIndexList("GARRISONED", TheWeaponBonusConditionNames);
const int kBattlePlan = INI::scanIndexList("BATTLEPLAN_BOMBARDMENT", TheWeaponBonusConditionNames);
// RW 0x6CA45B parses `<percent>` through scanPercentToReal (RW 0x42EEFA: value * 0.01f, NumericState::pc24Mul), so the expected float is that same
// product and not a rounded literal; the sum of two conditions is the set's own formula (RW 0x6CA434: bonus += set[cond] - 1.0 per satisfied bit).
const float kHorde150 = 150.0f * 0.01f;
const float kRate125 = 125.0f * 0.01f;
const float kRate120 = 120.0f * 0.01f;
const float kHordeAndNationalism = 1.0f + (kHorde150 - 1.0f) + (kRate125 - 1.0f);


// The two objects the duel drives: an infantryman whose sword is the fixture's SwordWeapon (a 1000 ms delay) and a passive victim. A Weapon block of this
// file is read through the same store the fixtures use (WeaponStore), so the file needs neither test_combat_weapons.cpp's own fixture text nor retail data.
const char kPort114Weapons[] =
	"Weapon Port114Sword\n"
	"  AttackRange = 11.5\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 1000\n"
	"  PreAttackDelay = 600\n"
	"  PreAttackType = PER_SHOT\n"
	"  FiringDuration = 200\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 0.0\n"
	"    DelayTime = 0\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	// the Weapon block's own WeaponBonus row (RW 0x6CB5AB): the per-template path the global set must not disturb
	"Weapon Port114GarrisonSword\n"
	"  AttackRange = 11.5\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 1000\n"
	"  WeaponBonus = GARRISONED RATE_OF_FIRE 200%\n"
	"  DamageNugget\n"
	"    Damage = 10\n"
	"    Radius = 0.0\n"
	"    DelayTime = 0\n"
	"    DamageType = SLASH\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n";

const char kPort114Objects[] =
	"Object Port114Swordsman\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY Port114Sword\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Port114GarrisonSwordsman\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY Port114GarrisonSword\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object Port114Dummy\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"End\n";

// a world of this file's own objects, with the weapon store the fixtures install
struct World
{
	CombatWorld w;
	explicit World()
		: w(kPort114Weapons)
	{
		w.combat().setAutoAcquireEnabled(false); // driven by hand
		// the objects of this file, after the weapon store holds their weapons (a WeaponSet row resolves at parse time)
		const std::string err = w.w.load(kPort114Objects, INI_LOAD_OVERWRITE, "port114.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	Object *unit(const char *name, char side, float x, float y) { return w.unit(name, side, x, y); }
};

// one duel: the attacker's shops, the object side of RW 0x6CA7CF
struct Duel
{
	World world;
	CombatWorld &w;

	Object *a;
	Object *b;
	ObjectWeapons *ow;
	explicit Duel(const char *attacker = "Port114Swordsman", const char *victim = "Port114Dummy", float gap = 12.0f)
		: world()
		, w(world.w)
	{
		a = w.unit(attacker, 'A', 300, 300);
		b = w.unit(victim, 'B', 300 + gap, 300);
		w.frames(2);
		ow = a->getWeapons();
		REQUIRE(ow != nullptr);
	}
	// the bonus the object's current weapon would fire with (RW 0x6CA7AC)
	WeaponBonus currentBonus()
	{
		WeaponBonus bonus;
		REQUIRE(ow->currentWeaponBonus(bonus));
		return bonus;
	}
	// one shot's delay in frames: wait until the weapon is ready (a previous shot's timer), pre-attack, fire, then read whenWeCanFireAgain - now.
	// The sword is DelayBetweenShots 1000 ms = 5 frames at the port's 5 logic frames per second (docs/PLAN's ms -> frame rule)
	unsigned shotDelay()
	{
		int ready = 0;
		while (ow->currentStatus() != WEAPON_READY_TO_FIRE && ready < 40)
		{
			w.frames(1);
			++ready;
		}
		REQUIRE(ow->currentStatus() == WEAPON_READY_TO_FIRE);
		ow->preFireCurrentWeapon(b, nullptr);
		REQUIRE(ow->currentStatus() == WEAPON_PRE_ATTACK);
		int waited = 0;
		while (ow->currentStatus() == WEAPON_PRE_ATTACK && waited < 40)
		{
			w.frames(1);
			++waited;
		}
		REQUIRE(ow->currentStatus() == WEAPON_READY_TO_FIRE);
		ow->fireCurrentWeapon(b, nullptr);
		const unsigned again = ow->currentWeapon()->whenWeCanFireAgain();
		return again - w.logic->getFrame();
	}
};
} // namespace

TEST_CASE("PORT-114 GameData WeaponBonus: the row builds the global set (RW 0xC004D0 / 0x6CB559, GlobalData +0xAD0), every entry starts at 1.0")
{
	// no line: the pointer stays null exactly as before this lane (RW tests it at RW 0x6CA7CF)
	GameLogicSettings plain = scanOrFail(gameDataWith(""));
	CHECK(plain.globalWeaponBonusSet.get() == nullptr);
	CHECK_FALSE(plain.weaponBonusLoaded);

	// retail 1.9.8.2 gamedata.ini lines 445-465 (the mod's own values; the file itself is not in Git)
	GameLogicSettings g = scanOrFail(gameDataWith(
		"  WeaponBonus = HORDE RATE_OF_FIRE 150%\n"
		"  WeaponBonus = NATIONALISM RATE_OF_FIRE 125%\n"
		"  WeaponBonus = ENTHUSIASTIC RATE_OF_FIRE 125%\n"
		"  WeaponBonus = SUBLIMINAL RATE_OF_FIRE 125%\n"
		"  WeaponBonus = GARRISONED RANGE 100%\n"
		"  WeaponBonus = GARRISONED DAMAGE 100%\n"
		"  WeaponBonus = BATTLEPLAN_BOMBARDMENT DAMAGE 120%\n"
		"  WeaponBonus = BATTLEPLAN_SEARCHANDDESTROY RANGE 120%\n"
		"  WeaponBonus = BATTLEPLAN_HOLDTHELINE RATE_OF_FIRE 100%\n"));
	REQUIRE(g.globalWeaponBonusSet.get() != nullptr);
	CHECK(g.weaponBonusLoaded);
	// the percent form of RW 0x6CA45B (scanPercentToReal: value * 0.01), six fields per condition
	CHECK(g.globalWeaponBonusSet->get(kHorde, WEAPONBONUS_RATE_OF_FIRE) == kHorde150);
	CHECK(g.globalWeaponBonusSet->get(kHorde, WEAPONBONUS_DAMAGE) == 1.0f);
	CHECK(g.globalWeaponBonusSet->get(INI::scanIndexList("NATIONALISM", TheWeaponBonusConditionNames), WEAPONBONUS_RATE_OF_FIRE) == kRate125);
	CHECK(g.globalWeaponBonusSet->get(INI::scanIndexList("ENTHUSIASTIC", TheWeaponBonusConditionNames), WEAPONBONUS_RATE_OF_FIRE) == kRate125);
	CHECK(g.globalWeaponBonusSet->get(INI::scanIndexList("SUBLIMINAL", TheWeaponBonusConditionNames), WEAPONBONUS_RATE_OF_FIRE) == kRate125);
	CHECK(g.globalWeaponBonusSet->get(kGarrisoned, WEAPONBONUS_RANGE) == 1.0f);
	CHECK(g.globalWeaponBonusSet->get(kGarrisoned, WEAPONBONUS_DAMAGE) == 1.0f);
	CHECK(g.globalWeaponBonusSet->get(kBattlePlan, WEAPONBONUS_DAMAGE) == kRate120);
	CHECK(g.globalWeaponBonusSet->get(INI::scanIndexList("BATTLEPLAN_SEARCHANDDESTROY", TheWeaponBonusConditionNames), WEAPONBONUS_RANGE) == kRate120);
	CHECK(g.globalWeaponBonusSet->get(INI::scanIndexList("BATTLEPLAN_HOLDTHELINE", TheWeaponBonusConditionNames), WEAPONBONUS_RATE_OF_FIRE) == 1.0f);
	// a condition the data never names keeps RW's constructor value (all 1.0)
	CHECK(g.globalWeaponBonusSet->get(INI::scanIndexList("VETERAN", TheWeaponBonusConditionNames), WEAPONBONUS_RATE_OF_FIRE) == 1.0f);
	// an unknown condition or field is an INI error, not a silent entry (RW 0x6CA45B -> scanIndexList)
	GameLogicSettings bad;
	std::string err;
	CHECK_FALSE(GameLogicSettingsLoader::scanGameData(gameDataWith("  WeaponBonus = HORD RATE_OF_FIRE 150%\n"), bad, &err));
	// the error names the field (the block keyword the row parser was reached from) or the bad token
	CHECK(err.find("WeaponBonus") != std::string::npos);
}

TEST_CASE("PORT-114 a horde member fires 1.5x faster (RW 0x6CA7CF + RW 0x6CA4B5): 5-frame delay becomes 3, without the HORDE condition it stays 5")
{
	// baseline: no global set, no condition -> the weapon's own 1000 ms
	{
		Duel d;
		CHECK(d.currentBonus().m_field[WEAPONBONUS_RATE_OF_FIRE] == 1.0f);
		CHECK(d.shotDelay() == 5);
	}
	// retail HORDE RATE_OF_FIRE 150% in the world's settings
	{
		Duel d;
		installSet(*d.w.logic, "  WeaponBonus = HORDE RATE_OF_FIRE 150%\n");
		// without the condition the set adds nothing (bit 1 is clear in Object + 0x39C)
		CHECK(d.currentBonus().m_field[WEAPONBONUS_RATE_OF_FIRE] == 1.0f);
		CHECK(d.shotDelay() == 5);
		// the condition the member of a horde carries; the set then adds 1.5 - 1 = 0.5 (RW 0x6CA434)
		d.a->setWeaponBonusCondition(kHorde, true);
		CHECK(d.currentBonus().m_field[WEAPONBONUS_RATE_OF_FIRE] == kHorde150);
		// floor(5 / 1.5) = 3 frames (RW 0x6CA066: fild, fdiv by the rate, floor)
		CHECK(d.shotDelay() == 3);
	}
	// the two conditions the mod stacks add up: NATIONALISM 125% on top of HORDE 150% = 2x, floor(5 / 2) = 2
	{
		Duel d;
		installSet(*d.w.logic,
			"  WeaponBonus = HORDE RATE_OF_FIRE 150%\n"
			"  WeaponBonus = NATIONALISM RATE_OF_FIRE 125%\n");
		d.a->setWeaponBonusCondition(kHorde, true);
		d.a->setWeaponBonusCondition(INI::scanIndexList("NATIONALISM", TheWeaponBonusConditionNames), true);
		CHECK(d.currentBonus().m_field[WEAPONBONUS_RATE_OF_FIRE] == kHordeAndNationalism);
		CHECK(d.shotDelay() == 2);
	}
	// a set that only names another condition leaves the horde unchanged
	{
		Duel d;
		installSet(*d.w.logic, "  WeaponBonus = GARRISONED RANGE 100%\n");
		d.a->setWeaponBonusCondition(kHorde, true);
		CHECK(d.currentBonus().m_field[WEAPONBONUS_RATE_OF_FIRE] == 1.0f);
		CHECK(d.shotDelay() == 5);
	}
}

TEST_CASE("PORT-114 the neighbouring behaviour holds: without the row the set is null, GARRISONED still parses in the Weapon block, and the settings hash moves only when data names the row")
{
	// no GameData row: globalWeaponBonusSet() answers null (RW's own null test at RW 0x6CA7CF) and the per-weapon path is untouched
	{
		Duel d;
		CHECK(d.w.logic->settings().globalWeaponBonusSet.get() == nullptr);
		CHECK(d.ow->weaponInSlot(PRIMARY_WEAPON) != nullptr);
		CHECK(d.ow->weaponInSlot(PRIMARY_WEAPON)->getTemplate()->getName() == "Port114Sword");
		CHECK(d.shotDelay() == 5);
	}
	// the Weapon block's own WeaponBonus lines (RW 0x6CB5AB) still parse into the template's set and apply with the same mask:
	// Port114GarrisonSword carries `WeaponBonus = GARRISONED RATE_OF_FIRE 200%` in this file's own Weapon text
	{
		World world;
		Object *u = world.unit("Port114GarrisonSwordsman", 'A', 300, 300);
		world.w.frames(2);
		ObjectWeapons *ow = u->getWeapons();
		REQUIRE(ow != nullptr);
		WeaponBonus before;
		REQUIRE(ow->currentWeaponBonus(before));
		CHECK(before.m_field[WEAPONBONUS_RATE_OF_FIRE] == 1.0f);
		u->setWeaponBonusCondition(kGarrisoned, true);
		WeaponBonus after;
		REQUIRE(ow->currentWeaponBonus(after));
		CHECK(after.m_field[WEAPONBONUS_RATE_OF_FIRE] == 1.0f + (200.0f * 0.01f - 1.0f));
	}
	// the settings hash: a game whose GameData has no WeaponBonus row keeps the hash it had before this lane, a mod's row moves it
	{
		auto hashOf = [](const GameLogicSettings &s) {
			StateHasher h;
			s.crc(h);
			return h.value();
		};
		const GameLogicSettings plain = scanOrFail(gameDataWith(""));
		const GameLogicSettings plainAgain = scanOrFail(gameDataWith(""));
		CHECK(hashOf(plain) == hashOf(plainAgain));
		const GameLogicSettings withRow = scanOrFail(gameDataWith("  WeaponBonus = HORDE RATE_OF_FIRE 150%\n"));
		CHECK(hashOf(withRow) != hashOf(plain));
		const GameLogicSettings otherRow = scanOrFail(gameDataWith("  WeaponBonus = HORDE RATE_OF_FIRE 160%\n"));
		CHECK(hashOf(otherRow) != hashOf(withRow));
	}
}
