// OpenBFME. GPL-3.0.
//
// PORT-115 (issue fork issue 299, PROBLEMS.md P-60): the archers' Barrage — Command_ArcherBombard
// (commandbutton.ini:1077: Command = FIRE_WEAPON, WeaponSlot = TERTIARY, Options = NEED_TARGET_POS), the ground click of
// a horde of archers. The chain in RotWK 2.01 (PORT115_RW_ANALYSIS.md):
//
//   MSG_DO_WEAPON_AT_LOCATION 1038 { slot, location, maxShots } — RW 0x77A3CB:
//     AIGroup::setWeaponLockForGroup(slot, LOCKED_TEMPORARILY)  RW 0x76FAB7
//       -> per member Object::setWeaponLock(slot, type)         RW 0x69121A
//            -> the contain (+ 0x258) slot 0x168                RW 0x8656D6, the callback RW 0x8656A7
//                 -> Object::setWeaponLock on every passenger   (the horde's members take the TERTIARY lock)
//            -> SWITCHED_WEAPONS (0x51) = (type == 2 && slot != 0), then the own weapon set RW 0x6C97F9
//     nothing locked -> the handler ends (RW 0x77A41B)
//     AIGroup::groupAttackPosition(&loc, maxShots, CMD_FROM_PLAYER) RW 0x77229A
//       -> a free horde's contain slot 0xB4 (RW 0x86C713) is false, so only the horde object's own AI is ordered:
//          aiAttackPosition RW 0x6961F1 -> aiDoCommand (HordeAIUpdate RW 0x89E169) -> privateAttackPosition RW 0x66DE02
//          -> state 9, the shot limit on the current weapon (+ 0x34)
//     the horde's locked TERTIARY bombard rangefinder (NoVictimNeeded, BombardType, an empty HordeAttackNugget) fires at
//     the ground -> the nugget's position form RW 0x911AC9 -> the horde interface's slot 0 RW 0x875550 -> every member of
//     a released rank gets aiAttackPosition(pos, 0x7FFFFFFF, CMD_FROM_AI) and shoots its own locked TERTIARY bow.
//   The release half: AIGroup::releaseWeaponLockForGroup RW 0x76FB64 -> Object::releaseWeaponLock RW 0x68DF11 (a destroyed
//   object returns at once; the contain's slot 0x16C releases the passengers; then the own weapon set RW 0x6C98E6).
//
// The retail data these tests are built after (no game file is needed): MordorArcherHorde (mordorhordes.ini:392) with
// PRIMARY MordorArcherMissileHordeRangefinderEdain_New / TERTIARY MordorArcherMissileHordeRangefinderBombard_New
// (weapon.inc:33862 / 33880), AutoChooseSources = TERTIARY FROM_PLAYER FROM_SCRIPT and RanksToReleaseWhenAttacking = 1 2 3 4;
// the member MordorArcher (mordorarcher.ini:644) with PRIMARY MordorArcherBowEdain (no NoVictimNeeded) / TERTIARY
// MordorArcherBowBombardEdain (NoVictimNeeded, BombardType), AutoChooseSources = TERTIARY FROM_PLAYER FROM_SCRIPT ("Not
// from AI.") — the members' bombard bow is not auto-choosable for the CMD_FROM_AI order the horde gives them, so the
// button's lock is what keeps them on it (RW 0x6C8A4E: a locked weapon stays).

#include "doctest.h"
#include "CombatTestUtil.h"

#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/WeaponCommands.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

using namespace combattest;

namespace
{
// the mod's pairing, reduced: the horde's two rangefinders (the bombard one may fire with no victim and carries an EMPTY
// HordeAttackNugget — no LockWeaponSlot, so the members' slot comes from the button alone) and the members' two bows
const char kBarrageArchers[] =
	"Weapon P115HordeRangefinder\n"
	"  AttackRange = 150\n"
	"  DelayBetweenShots = 1000\n"
	"  LeechRangeWeapon = Yes\n"
	"  HordeAttackNugget\n"
	"  End\n"
	"End\n"
	"Weapon P115HordeBombardRangefinder\n"
	"  AttackRange = 250\n"
	"  MinimumAttackRange = 50\n"
	"  DelayBetweenShots = 1000\n"
	"  LeechRangeWeapon = Yes\n"
	"  AcceptableAimDelta = 45\n"
	"  NoVictimNeeded = Yes\n"
	"  BombardType = Yes\n"
	"  ScatterRadius = 40\n"
	"  HordeAttackNugget\n"
	"  End\n"
	"End\n"
	"Weapon P115Bow\n"
	"  AttackRange = 300\n"
	"  DelayBetweenShots = 0\n"
	"  ClipSize = 1\n"
	"  AutoReloadsClip = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon P115BowBombard\n"
	"  AttackRange = 350\n"
	"  MinimumAttackRange = 25\n"
	"  DelayBetweenShots = 800\n"
	"  ClipSize = 1\n"
	"  AutoReloadsClip = Yes\n"
	"  NoVictimNeeded = Yes\n"
	"  BombardType = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Object P115Archer\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 250\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY P115Bow\n"
	"    Weapon = TERTIARY P115BowBombard\n"
	"    AutoChooseSources = PRIMARY FROM_PLAYER FROM_SCRIPT FROM_AI\n"
	"    AutoChooseSources = TERTIARY FROM_PLAYER FROM_SCRIPT\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	// the mod's archer hordes carry UseCrushAttack = No (mordorhordes.ini:448 "Archer hordes cannot crush or Bombard will not work"):
	// RW 0x74F21A / RW 0x74F318 make the horde machine's squish state succeed on it (template +0x60D), the default yes needs a goal object
	"Object P115ArcherHorde\n"
	"  KindOf = HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  UseCrushAttack = No\n"
	"  VisionRange = 250\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY P115HordeRangefinder\n"
	"    Weapon = TERTIARY P115HordeBombardRangefinder\n"
	"    AutoChooseSources = TERTIARY FROM_PLAYER FROM_SCRIPT\n"
	"  End\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:P115Archer Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    RankInfo = RankNumber:2 UnitType:P115Archer Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20\n"
	"    InitialPayload = P115Archer 6\n"
	"    RanksToReleaseWhenAttacking = 1 2\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n";

const int kTertiary = 2; // WeaponSlot = TERTIARY (commandbutton.ini:1079)
const int kNoShotLimit = 0x7FFFFFFF;

// the Barrage click of a selected group: MSG_DO_WEAPON_AT_LOCATION { int slot, location, int maxShots } (RW 0x77A3CB)
void barrageClick(CombatWorld &w, int player, const Coord3D &spot, int maxShots = kNoShotLimit)
{
	GameMessage m(MSG_DO_WEAPON_AT_LOCATION, player);
	m.appendIntegerArgument(kTertiary);
	m.appendLocationArgument(spot);
	m.appendIntegerArgument(maxShots);
	w.send(m);
}

// The rig's dispatcher carries the AI commands (MoveTestUtil.h:187); the weapon commands' half is the game's (LiveGame.cpp:504, lane FACT-A,
// RW 0x77B00A .. 0x77A304 .. 0x77A3CB), so the test registers it exactly as the game does before sending MSG_DO_WEAPON_AT_LOCATION.
struct BarrageWorld : CombatWorld
{
	BarrageWorld() : CombatWorld(kBarrageArchers) { WeaponCommands::registerHandlers(dispatcher); }
};

unsigned long long memberShots(const std::vector<Object *> &archers)
{
	unsigned long long shots = 0;
	for (Object *a : archers)
	{
		shots += a->getWeapons()->stats().shotsFired;
	}
	return shots;
}
} // namespace

TEST_CASE("PORT-115: the Barrage click on a horde locks TERTIARY on the horde and, through its contain (RW 0x76FAB7 -> Object::setWeaponLock RW 0x69121A -> contain slot 0x168 RW 0x8656D6 / the callback RW 0x8656A7), on every member")
{
	BarrageWorld w;
	w.combat().setAutoAcquireEnabled(false);
	// 450 from the spot: outside the bombard rangefinder's AttackRange 250, so the horde is still on its way when the lock is looked at.
	// The lock is not permanent: the horde machine's HORDE_WAIT exit releases the temporary lock (RW 0x742D42 -> RW 0x68DF11(1)), so this
	// is the frame where RW still holds it. (RW 0x68DF11 also walks the contain, slot 0x16C; the port's wait exit, AIAttack.cpp:1069,
	// lane WEAPON-1, releases only the horde's own set - the rest of S-2480, not this task's break.)
	Object *horde = w.unit("P115ArcherHorde", 'A', 300, 600);
	w.frames(12);
	HordeContain *hc = w.hordeOf(horde);
	REQUIRE(hc != nullptr);
	const std::vector<Object *> archers = w.membersOf(horde);
	REQUIRE(archers.size() == 6);
	static const int kSwitched = CombatNames::status("SWITCHED_WEAPONS");
	for (Object *a : archers)
	{
		REQUIRE(a->getWeapons() != nullptr);
		CHECK(a->getWeapons()->curSlot() == 0);
		CHECK_FALSE(a->getWeapons()->isCurWeaponLocked());
	}
	const int player = w.playerIndex("Alice");
	w.select(player, { horde });
	w.frames(1);
	const unsigned long long before = WeaponCommands::stats().atLocation;
	const unsigned long long groupsBefore = WeaponCommands::stats().lockedGroups;
	const Coord3D spot{ 300.0f, 150.0f, w.logic->getGroundHeight(300.0f, 150.0f) };
	barrageClick(w, player, spot);
	w.frames(1);

	CHECK(horde->getWeapons()->stats().shotsFired == 0);              // the horde has not fired yet: this is the locked approach
	CHECK(WeaponCommands::stats().atLocation == before + 1);          // RW 0x77A3CB ran
	CHECK(WeaponCommands::stats().lockedGroups == groupsBefore + 1);  // RW 0x77A414: some member locked, so the order goes on
	CHECK(horde->getWeapons()->curSlot() == kTertiary);               // the horde's own set (RW 0x6C97F9)
	CHECK(horde->getWeapons()->isCurWeaponLocked());
	CHECK_FALSE(horde->testStatus((unsigned)kSwitched));              // a temporary lock clears SWITCHED_WEAPONS (RW 0x691238)
	for (Object *a : archers)
	{
		// the contain pass-through: without it the members' sets stay free and their bombard bow is only a choice
		CHECK(a->getWeapons()->isCurWeaponLocked());
		CHECK(a->getWeapons()->curSlot() == kTertiary);
		CHECK_FALSE(a->testStatus((unsigned)kSwitched));
	}
	// RW 0x77229A: a free horde's contain slot 0xB4 (RW 0x86C713) is false, so only the horde object is ordered — state 9
	CHECK(horde->getAIUpdateInterface()->currentStateId() == (unsigned)AI_ATTACK_POSITION);
	CHECK(horde->getAIUpdateInterface()->lastCommandSource() == CMD_FROM_PLAYER);
}

TEST_CASE("PORT-115: the Barrage volley — the horde's locked TERTIARY bombard rangefinder fires at the ground and its empty HordeAttackNugget (RW 0x911AC9 -> the horde interface's slot 0, RW 0x875550) sends every released member to shoot the spot with its locked TERTIARY bow")
{
	BarrageWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *horde = w.unit("P115ArcherHorde", 'A', 300, 300);
	w.frames(12);
	HordeContain *hc = w.hordeOf(horde);
	const std::vector<Object *> archers = w.membersOf(horde);
	REQUIRE(archers.size() == 6);
	const int player = w.playerIndex("Alice");
	w.select(player, { horde });
	w.frames(1);
	const Coord3D spot{ 300.0f, 150.0f, w.logic->getGroundHeight(300.0f, 150.0f) };
	barrageClick(w, player, spot);
	w.frames(1);
	// the horde is in range at once (the bombard rangefinder reaches 250, its minimum is 50): the volley starts
	const int n = w.runUntil([&] { return hc->attackStats().positionOrders > 0; }, 200);
	CHECK(n < 200); // tolerance: the frame the horde's first shot lands (its machine walks squish -> approach -> fire); the volley is checked exactly below
	CHECK(hc->attackStats().fires == 0);            // no victim: the nugget's position form, not attackTargetNow (slot 4)
	CHECK(hc->attackStats().positionOrders == 6);   // ranks 1 and 2 are released: every archer (RW 0x875550)
	for (Object *a : archers)
	{
		AIUpdateInterface *ai = a->getAIUpdateInterface();
		CHECK(ai->currentStateId() == (unsigned)AI_ATTACK_POSITION);
		CHECK(ai->lastCommandSource() == CMD_FROM_AI);             // the horde's order, not the player's
		CHECK(a->getWeapons()->curSlot() == kTertiary);            // the bombard bow, and it stays (RW 0x6C8A4E: a lock stays)
		CHECK(a->getWeapons()->isCurWeaponLocked());
		CHECK(a->getWeapons()->currentWeapon()->maxShotCount() == kNoShotLimit); // RW 0x875550's own limit
	}
	w.frames(30);
	// every released archer shot at least once: the exact count depends on the members' odd/even frame split (RW 0x7479B7)
	// and their DelayBetweenShots 800 ms, and the horde's own rangefinder keeps firing while the order stands
	CHECK(memberShots(archers) >= archers.size()); // tolerance: at least one arrow per released archer - the Barrage is not silent
	CHECK(horde->getWeapons()->stats().shotsFired >= 1); // tolerance: the horde's bombard rangefinder fired the position form
}

TEST_CASE("PORT-115: the release half is the same pass-through (RW 0x76FB64 -> Object::releaseWeaponLock RW 0x68DF11 -> the contain's slot 0x16C): a later order that releases the group's temporary locks frees the members again")
{
	BarrageWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *horde = w.unit("P115ArcherHorde", 'A', 300, 300);
	w.frames(12);
	const std::vector<Object *> archers = w.membersOf(horde);
	REQUIRE(archers.size() == 6);
	const int player = w.playerIndex("Alice");
	w.select(player, { horde });
	w.frames(1);
	const Coord3D spot{ 300.0f, 150.0f, w.logic->getGroundHeight(300.0f, 150.0f) };
	barrageClick(w, player, spot);
	w.frames(1);
	for (Object *a : archers)
	{
		REQUIRE(a->getWeapons()->isCurWeaponLocked());
	}
	// RW 0x77AF93 (MSG_DO_FORCE_ATTACK_GROUND) releases the group's temporary locks before the new order (lane PLAY-2)
	GameMessage m(MSG_DO_FORCE_ATTACK_GROUND, player);
	m.appendLocationArgument(Coord3D{ 300.0f, 400.0f, w.logic->getGroundHeight(300.0f, 400.0f) });
	w.send(m);
	w.frames(1);
	CHECK(horde->getWeapons()->isCurWeaponLocked() == false);
	for (Object *a : archers)
	{
		CHECK(a->getWeapons()->isCurWeaponLocked() == false); // RW 0x68DF11 -> the contain's slot 0x16C
	}
}

TEST_CASE("PORT-115: a single archer's Barrage is unchanged — the button locks its own TERTIARY slot and it shoots the spot (RW 0x76FAB7 over a one-object group, no contain)")
{
	BarrageWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *archer = w.unit("P115Archer", 'A', 300, 300);
	w.frames(12);
	REQUIRE(archer->getContain() == nullptr);
	CHECK(archer->getWeapons()->curSlot() == 0);
	const int player = w.playerIndex("Alice");
	w.select(player, { archer });
	w.frames(1);
	const Coord3D spot{ 300.0f, 150.0f, w.logic->getGroundHeight(300.0f, 150.0f) };
	barrageClick(w, player, spot, 3); // MaxShotsToFire of the button (CommandButton + 0xA0)
	w.frames(1);
	CHECK(archer->getWeapons()->curSlot() == kTertiary);
	CHECK(archer->getWeapons()->isCurWeaponLocked());
	AIUpdateInterface *ai = archer->getAIUpdateInterface();
	CHECK(ai->currentStateId() == (unsigned)AI_ATTACK_POSITION);
	CHECK(ai->lastCommandSource() == CMD_FROM_PLAYER);
	CHECK(archer->getWeapons()->currentWeapon()->maxShotCount() == 3); // RW 0x66E04B: the limit goes on the current weapon
	// the end condition is exact: MaxShotsToFire 3 of the button (CommandButton + 0xA0) goes on the weapon (RW 0x66E04B) and stops it there
	const int n = w.runUntil([&] { return archer->getWeapons()->stats().shotsFired == 3; }, 400);
	CHECK(n < 400); // tolerance: the frame the third arrow leaves (DelayBetweenShots 800 ms); the count is the exact check
	CHECK(archer->getWeapons()->stats().shotsFired == 3);
	w.frames(90);
	CHECK(archer->getWeapons()->stats().shotsFired == 3); // and it does not shoot on: the limit is reached, the barrage is over
}
