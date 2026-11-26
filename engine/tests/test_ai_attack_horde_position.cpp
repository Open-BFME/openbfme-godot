// OpenBFME. GPL-3.0.
// PORT-165 synthetic regression: a MELEE_HORDE changes to a ranged position weapon.
// The position-attack machine is selected at RW 0x74CFF1..0x74D019.
#include "doctest.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "CombatTestUtil.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/HudInput.h"
#include "GameClient/FXPlayback.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/AI/AIAttack.h"
#include "GameLogic/AI/AIAttackHordePosition.h"
#include "GameLogic/AI/AIHunt.h"
#include "GameLogic/AI/AIGarrisonStates.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/AI/WeaponCommands.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/WeaponSetToggle.h"

using namespace combattest;
namespace {
const char kSynthetic[] =
	"Weapon P165HordeRangefinder\n"
	"  AttackRange = 12\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 1000\n"
	"  LeechRangeWeapon = Yes\n"
	"  HordeAttackNugget\n"
	"  End\n"
	"End\n"
	"Weapon P165HordeBombardRangefinder\n"
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
	"Weapon P165Bow\n"
	"  AttackRange = 300\n"
	"  DelayBetweenShots = 0\n"
	"  ClipSize = 1\n"
	"  AutoReloadsClip = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = Arrow\n"
	"    WarheadTemplateName = BowWarhead\n"
	"  End\n"
	"End\n"
	"Weapon P165BowBombard\n"
	"  AttackRange = 350\n"
	"  WeaponSpeed = 100\n"
	"  MinimumAttackRange = 25\n"
	"  DelayBetweenShots = 800\n"
	"  ClipSize = 1\n"
	"  AutoReloadsClip = Yes\n"
	"  NoVictimNeeded = Yes\n"
	"  BombardType = Yes\n"
	"  ProjectileNugget\n"
	"    ProjectileTemplateName = P165Bomb\n"
	"    WarheadTemplateName = P165BombWarhead\n"
	"  End\n"
	"End\n"
	"Weapon P165BombWarhead\n"
	"  DamageNugget\n"
	"    Damage = 5\n"
	"    Radius = 80\n"
	"    DamageType = FLAME\n"
	"    DeathType = BURNED\n"
	"  End\n"
	"End\n"
	"Object P165Bomb\n"
	"  KindOf = PROJECTILE NO_COLLIDE\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n"
	"  End\n"
	"  Behavior = BezierProjectileBehavior ModuleTag_Projectile\n"
	"    FirstHeight = 30\n"
	"    SecondHeight = 12\n"
	"    FirstPercentIndent = 30%\n"
	"    SecondPercentIndent = 80%\n"
	"    GroundHitFX = FX_P165BombExplosion\n"
	"  End\n"
	"  Geometry = Sphere\n"
	"  GeometryMajorRadius = 1\n"
	"End\n"
	"Object P165Archer\n"
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
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = WEAPONSET_TOGGLE_1\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"    Weapon = TERTIARY P165BowBombard\n"
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
	"Object P165ArcherHorde\n"
	"  KindOf = HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK\n"
	"  CommandSet = P165CommandSet\n"
		"  VisionRange = 250\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 30\n"
	"  GeometryMinorRadius = 45\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY P165HordeRangefinder\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = WEAPONSET_TOGGLE_1\n"
	"    Weapon = PRIMARY P165HordeRangefinder\n"
	"    Weapon = TERTIARY P165HordeBombardRangefinder\n"
	"    AutoChooseSources = TERTIARY FROM_PLAYER FROM_SCRIPT\n"
	"  End\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeAIUpdate ModuleTag_AI\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:P165Archer Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    RankInfo = RankNumber:2 UnitType:P165Archer Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20\n"
	"    InitialPayload = P165Archer 6\n"
	"    RanksToReleaseWhenAttacking = 1 2\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = HordeLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 50\n"
	"  End\n"
	"End\n"
	"Object P165SpecialApproach\n"
	"  KindOf = PROJECTILE SELECTABLE CAN_ATTACK\n"
	"  VisionRange = 250\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY BowWeapon\n"
	"  End\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface AITag\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n"
	"Object P165TurretApproach\n"
	"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
	"  VisionRange = 250\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  ArmorSet\n"
	"    Conditions = None\n"
	"    Armor = PlainArmor\n"
	"  End\n"
	"  WeaponSet\n"
	"    Conditions = None\n"
	"    Weapon = PRIMARY SwordWeapon\n"
	"  End\n"
	"  Body = ActiveBody BodyTag\n"
	"    MaxHealth = 60\n"
	"  End\n"
	"  Behavior = AIUpdateInterface AITag\n"
	"    Turret\n"
	"      TurretTurnRate = 360\n"
	"      ControlledWeaponSlots = PRIMARY\n"
	"      NaturalTurretAngle = 90\n"
	"    End\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 55\n"
	"  End\n"
	"End\n";

struct TestWorld : CombatWorld
{
	TestWorld() : CombatWorld(kSynthetic) { WeaponCommands::registerHandlers(dispatcher); WeaponSetToggle::registerHandlers(dispatcher); }
};

}

TEST_CASE("PORT-165: position horde wait tracks active member states (RW 0x747999, 0x742CEA, 0x86D562)")
{
	TestWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *horde = w.unit("P165ArcherHorde", 'A', 300, 300);
	REQUIRE(horde);
	w.frames(12);
	const auto members = w.membersOf(horde);
	REQUIRE(members.size() == 6);
	AIStateMachine hordeMachine(*horde->getAIUpdateInterface(), "P165WaitTest");
	AIHordePositionWaitState wait(hordeMachine);
	CHECK(wait.onEnter() == STATE_CONTINUE);
	CHECK(wait.update() == STATE_FAILURE); // at least one member is not active
	AIStateMachine memberMachine(*members.front()->getAIUpdateInterface(), "P165NonHordeWaitTest");
	AIHordePositionWaitState nonHordeWait(memberMachine);
	CHECK(nonHordeWait.onEnter() == STATE_FAILURE);
	const Coord3D spot{300.0f, 150.0f, w.logic->getGroundHeight(300.0f, 150.0f)};
	for (Object *member : members)
	{
		WeaponSetToggle::setObjectWeaponSetFlag(*member, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
		member->getWeapons()->setWeaponLock(2, LOCKED_TEMPORARILY);
		member->getAIUpdateInterface()->aiAttackPosition(spot, 0x7fffffff, CMD_FROM_AI);
	}
	CHECK(wait.update() == STATE_CONTINUE); // every live member's state reports active
	Object *target = w.unit("Dummy", 'B', 500, 500);
	REQUIRE(target);
	hordeMachine.setGoalObject(target->getID());
	for (Object *member : members) member->getAIUpdateInterface()->setCurrentVictim(target);
	CHECK(wait.update() == STATE_CONTINUE); // RW 0x86D438: all have the same victim
	members.front()->getAIUpdateInterface()->setCurrentVictim(nullptr);
	CHECK(wait.update() == STATE_FAILURE);
	hordeMachine.setGoalObject((ObjectID)INVALID_ID);
	members.front()->getAIUpdateInterface()->aiIdle(CMD_FROM_AI);
	CHECK(wait.update() == STATE_FAILURE); // one inactive member is sufficient
}

namespace {
struct PositionFireNotify : AttackFireNotify
{
	int shots = 0;
	bool slotAllowed = true;
	void notifyFired() override { ++shots; }
	void notifyNewVictimChosen(Object *) override {}
	bool isWeaponSlotOkToFire(int) const override { return slotAllowed; }
	bool isAttackingObject() const override { return false; }
};
}

TEST_CASE("PORT-165: position-fire state tests position range and fires once (RW 0x74C911 / 0x74C9FF)")
{
	TestWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *horde = w.unit("P165ArcherHorde", 'A', 300, 300);
	REQUIRE(horde);
	w.frames(12);
	WeaponSetToggle::setObjectWeaponSetFlag(*horde, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
	horde->getWeapons()->setWeaponLock(2, LOCKED_TEMPORARILY);
	AIStateMachine machine(*horde->getAIUpdateInterface(), "P165PositionFire");
	PositionFireNotify notify;
	AIHordePositionFireState fire(machine, notify);
	machine.setGoalPosition(Coord3D{300.0f, 900.0f, 0.0f});
	CHECK(fire.onEnter() == STATE_FAILURE);
	machine.setGoalPosition(Coord3D{300.0f, 150.0f, 0.0f});
	REQUIRE(fire.onEnter() == STATE_CONTINUE);
	notify.slotAllowed = false;
	CHECK(fire.update() == STATE_FAILURE);
	CHECK(notify.shots == 0);
	notify.slotAllowed = true;
	CHECK(fire.update() == STATE_SUCCESS);
	CHECK(notify.shots == 1);
	CHECK(horde->getWeapons()->stats().shotsFired == 1);
	fire.onExit(EXIT_NORMAL);
	CHECK(fire.onEnter() == STATE_SUCCESS); // weapon is not yet ready
}

TEST_CASE("PORT-165: deferred position-fire state survives save/load (RW 0x74C9C5 / 0x74CA35)")
{
	TestWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *horde = w.unit("P165ArcherHorde", 'A', 300, 300);
	REQUIRE(horde);
	w.frames(12);
	const auto members = w.membersOf(horde);
	REQUIRE(!members.empty());
	Object *member = members.front();
	WeaponSetToggle::setObjectWeaponSetFlag(*member, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
	member->getWeapons()->setWeaponLock(2, LOCKED_TEMPORARILY);
	if ((member->getAIUpdateInterface()->frame() & 1u) == 0) w.frames(1);
	REQUIRE((member->getAIUpdateInterface()->frame() & 1u) == 1u);
	AIStateMachine machine(*member->getAIUpdateInterface(), "P165DeferredFire");
	machine.setGoalPosition(Coord3D{300.0f, 150.0f, 0.0f});
	PositionFireNotify notify;
	AIHordePositionFireState fire(machine, notify);
	REQUIRE(fire.onEnter() == STATE_CONTINUE);
	XferSave save;
	fire.xfer(save);
	AIHordePositionFireState restored(machine, notify);
	XferLoad load(save.data());
	restored.xfer(load);
	CHECK(restored.update() == STATE_CONTINUE); // deferred pre-fire, no shot on this update
	CHECK(notify.shots == 0);
	CHECK(restored.update() == STATE_SUCCESS);
	CHECK(notify.shots == 1);
	restored.onExit(EXIT_NORMAL);
}

TEST_CASE("PORT-165: position approach tests range and queues an attack path (RW 0x74E9DD / 0x741EF3)")
{
	TestWorld w;
	w.combat().setAutoAcquireEnabled(false);
	Object *horde = w.unit("P165ArcherHorde", 'A', 300, 600);
	REQUIRE(horde);
	w.frames(12);
	WeaponSetToggle::setObjectWeaponSetFlag(*horde, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
	horde->getWeapons()->setWeaponLock(2, LOCKED_TEMPORARILY);
	AIStateMachine machine(*horde->getAIUpdateInterface(), "P165Approach");
	machine.setGoalPosition(Coord3D{300.0f, 450.0f, 0.0f});
	AIHordePositionApproachState inRange(machine);
	CHECK(inRange.onEnter() == STATE_SUCCESS);
	machine.setGoalPosition(Coord3D{300.0f, 150.0f, 0.0f});
	AIHordePositionApproachState distant(machine);
	horde->getAIUpdateInterface()->setLastCommandSource(CMD_FROM_PLAYER);
	CHECK(AIAttackSquadState::canChase(*horde));
	CHECK(GarrisonRules::isMobile(*horde));
	CHECK_FALSE(horde->testStatus((unsigned)CombatNames::statuses().hordeMember));
	CHECK_FALSE(horde->getAIUpdateInterface()->isStatusImmobile());
	REQUIRE(distant.onEnter() == STATE_CONTINUE);
	CHECK(horde->getAIUpdateInterface()->mover().attackRequestPending());
	CHECK(horde->getAIUpdateInterface()->mover().isWaitingForPath());
	XferSave save;
	distant.xfer(save);
	AIHordePositionApproachState restored(machine);
	XferLoad load(save.data());
	restored.xfer(load);
	XferSave again;
	restored.xfer(again);
	CHECK(again.data() == save.data());
	restored.onTemporaryStateEnded();
	CHECK(restored.update() == STATE_CONTINUE); // restored move still dispatches its special path hook
	restored.onExit(EXIT_NORMAL);
}

TEST_CASE("PORT-165: a toggled melee horde attacks the clicked position with its ranged weapon")
{
    TestWorld w;
    w.combat().setAutoAcquireEnabled(false);
    Object *horde = w.unit("P165ArcherHorde", 'A', 300, 300);
    REQUIRE(horde);
    w.frames(12);
    const auto members = w.membersOf(horde);
    REQUIRE(members.size() == 6);
    REQUIRE(horde->getWeapons()->weaponInSlot(2) == nullptr);
    for (Object *member : members) REQUIRE(member->getWeapons()->weaponInSlot(2) == nullptr);
    WeaponSetToggle::setObjectWeaponSetFlag(*horde, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
    for (Object *member : members) WeaponSetToggle::setObjectWeaponSetFlag(*member, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
    w.frames(2);
    REQUIRE(horde->getWeapons()->weaponInSlot(2));
    int player = w.playerIndex("Alice");
    w.select(player, { horde });
    w.frames(1);
    Coord3D spot{300.0f, 150.0f, w.logic->getGroundHeight(300.0f, 150.0f)};
    GameMessage click(MSG_DO_WEAPON_AT_LOCATION, player);
    click.appendIntegerArgument(2);
    click.appendLocationArgument(spot);
    click.appendIntegerArgument(0x7fffffff);
    w.send(click);
    w.frames(1);
    // A completed first shot releases the temporary lock when IdleAfterFiringDelay is -1.
    CHECK(horde->getWeapons()->stats().shotsFired == 1);
    CHECK(horde->getWeapons()->curSlot() == 2);
    for (Object *member : members) CHECK(member->getWeapons()->curSlot() == 2);
    CHECK(horde->getAIUpdateInterface()->currentStateId() == (unsigned)AI_ATTACK_POSITION);
    auto *attack = dynamic_cast<AIAttackState *>(horde->getAIUpdateInterface()->stateMachine().currentState());
    REQUIRE(attack);
    REQUIRE(attack->attackMachine());
    CHECK(attack->attackMachine()->currentState()->id() == 0x2BE);
    XferSave save;
    attack->xfer(save);
    AIAttackState restored(horde->getAIUpdateInterface()->stateMachine(), false, false, false);
    XferLoad load(save.data());
    restored.xfer(load);
    REQUIRE(restored.attackMachine());
    CHECK(restored.attackMachine()->currentState()->id() == 0x2BE);
    XferSave again;
    restored.xfer(again);
    CHECK(again.data() == save.data());
    HordeContain *contain = w.hordeOf(horde);
    REQUIRE(contain);
    const int frames = w.runUntil([&] { return contain->attackStats().positionOrders >= members.size(); }, 300);
    CHECK(frames < 300); // tolerance: asynchronous unit updates must complete within ten seconds
    CHECK(contain->attackStats().positionOrders == members.size());
    const int shotFrames = w.runUntil([&] {
        for (Object *member : members) if (member->getWeapons()->stats().shotsFired == 0) return false;
        return true;
    }, 300);
    CHECK(shotFrames < 300); // tolerance: member weapon timing and movement are asynchronous
}

TEST_CASE("PORT-165: out-of-range kind-7 approach and machine snapshot (RW 0x74548D)")
{
    TestWorld w;
    w.combat().setAutoAcquireEnabled(false);
    Object *horde = w.unit("P165ArcherHorde", 'A', 300, 600);
    REQUIRE(horde);
    w.frames(12);
    WeaponSetToggle::setObjectWeaponSetFlag(*horde, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
    for (Object *member : w.membersOf(horde)) WeaponSetToggle::setObjectWeaponSetFlag(*member, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
    REQUIRE(horde->getWeapons()->setWeaponLock(2, LOCKED_TEMPORARILY));
    auto *ai = horde->getAIUpdateInterface();
    const Coord3D spot{300.0f, 150.0f, w.logic->getGroundHeight(300.0f, 150.0f)};
    ai->aiAttackPosition(spot, 0x7fffffff, CMD_FROM_PLAYER);
    auto *attack = dynamic_cast<AIAttackState *>(ai->stateMachine().currentState());
    REQUIRE(attack);
    auto *inner = attack->attackMachine();
    REQUIRE(inner);
    CHECK(inner->currentStateId() == 0x2BC);
    REQUIRE(inner->findState(0x2BC));
    REQUIRE(inner->findState(0x2BD));
    REQUIRE(inner->findState(0x2BE));
    REQUIRE(inner->findState(0x2C0));
    CHECK(inner->findState(0x2BC)->successId() == 0x2BD);
    CHECK(inner->findState(0x2BC)->failureId() == 0x2C0);
    CHECK(inner->findState(0x2BD)->successId() == 0x2BE);
    CHECK(inner->findState(0x2BD)->failureId() == 0x2BC);
    CHECK(inner->findState(0x2BE)->successId() == 0x2BC);
    CHECK(inner->findState(0x2BE)->failureId() == 0x2BC);
    CHECK(inner->findState(0x2C0)->successId() == MACHINE_DONE_FAILURE);
    CHECK(inner->findState(0x2C0)->failureId() == MACHINE_DONE_FAILURE);
    XferSave save;
    attack->xfer(save);
    AIAttackState restored(ai->stateMachine(), false, false, false);
    XferLoad load(save.data());
    restored.xfer(load);
    REQUIRE(restored.attackMachine());
    CHECK(restored.attackMachine()->currentStateId() == 0x2BC);
    XferSave again;
    restored.xfer(again);
    CHECK(again.data() == save.data());
    const int frames = w.runUntil([&] { return w.hordeOf(horde)->attackStats().positionOrders > 0; }, 600);
    CHECK(frames < 600); // tolerance: pathfinding and horde travel time are asynchronous
}

TEST_CASE("PORT-165: horde object attack retains the existing kind-6 machine (RW 0x74D012)")
{
    TestWorld w;
    w.combat().setAutoAcquireEnabled(false);
    Object *horde = w.unit("P165ArcherHorde", 'A', 300, 300);
    Object *target = w.unit("Dummy", 'B', 300, 450);
    REQUIRE(horde);
    REQUIRE(target);
    w.frames(12);
    WeaponSetToggle::setObjectWeaponSetFlag(*horde, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
    REQUIRE(horde->getWeapons()->setWeaponLock(2, LOCKED_TEMPORARILY));
    auto *ai = horde->getAIUpdateInterface();
    ai->aiAttackObject(target, CMD_FROM_PLAYER);
    auto *attack = dynamic_cast<AIAttackState *>(ai->stateMachine().currentState());
    REQUIRE(attack);
    REQUIRE(attack->attackMachine());
    CHECK(attack->attackMachine()->findState(0x2BC) == nullptr);
}

namespace {
struct P165FxSink : FXEventSink
{
    FXPlayback &playback;
    int impacts = 0;
    int played = 0;
    std::vector<Coord3D> positions;
    explicit P165FxSink(FXPlayback &p) : playback(p) {}
    void onFXEvent(const FXEvent &event) override
    {
        if (std::string(event.site) == "GroundHitFX" && event.fxList && *event.fxList == "FX_P165BombExplosion")
        {
            ++impacts;
            positions.push_back(event.position);
            if (playback.playFXList(*event.fxList, event.position, false)) ++played;
        }
    }
};
}

TEST_CASE("PORT-165: command-set toggle sends the horde bomb volley through detonation and sound FX")
{
    TestWorld w;
    w.combat().setAutoAcquireEnabled(false);
    Object *horde = w.unit("P165ArcherHorde", 'A', 300, 300);
    Object *target = w.unit("Dummy", 'B', 300, 150);
    REQUIRE(horde);
    REQUIRE(target);
    w.frames(12);
    const auto members = w.membersOf(horde);
    REQUIRE(members.size() == 6);
    REQUIRE(horde->getWeapons()->weaponInSlot(2) == nullptr);
    for (Object *member : members) REQUIRE(member->getWeapons()->weaponInSlot(2) == nullptr);

    // Synthetic command set and button: ControlBar RW 0x9412C8 posts the toggle,
    // which RW 0x77B529 propagates to the members' WEAPONSET_TOGGLE_1.
    CommandStore commands;
    CommandButton toggle;
    toggle.m_name = "P165Toggle";
    toggle.m_command = GUI_COMMAND_TOGGLE_WEAPONSET;
    toggle.m_inPalantir = true;
    CommandSet *set = commands.newDynamicCommandSet("P165CommandSet");
    set->setDynamicButton(0, &toggle);
    MouseSettings mouse;
    MetaMap meta;
    PinholeView view{{0,-300,400},{300,300,0},1024,768,0.7f};
    HudInput input(*w.logic, w.ai.get(), view, w.commands, mouse, meta);
    input.context().commands = &commands;
    ControlBar bar(input.context());
    w.players.setLocalPlayer(w.players.findPlayerWithName("Alice"));
    input.ui().selectObject(horde->getID());
    GameMessage selection(MSG_CREATE_SELECTED_GROUP, w.playerIndex("Alice"));
    selection.appendBooleanArgument(true);
    selection.appendObjectIDArgument(horde->getID());
    input.submit(selection);
    input.flush();
    w.frames(1);
    bar.update();
    REQUIRE(bar.palantirButtons().size() == 1);
    REQUIRE(bar.palantirButtons()[0].button == &toggle);
    REQUIRE(bar.pressButton(0, true));
    CHECK(input.update() == 1);
    w.frames(1);
    REQUIRE(horde->getWeapons()->weaponInSlot(2));
    for (Object *member : members) REQUIRE(member->getWeapons()->weaponInSlot(2));
    const Coord3D hordeStart = *horde->getPosition();
    std::vector<Coord3D> memberStarts;
    for (Object *member : members) memberStarts.push_back(*member->getPosition());

    Win32BIGFileSystem fs;
    FXPlayback playback(fs, RandomAlgorithm::RotWK_GameDat_LCG);
    REQUIRE(playback.loadIniText("p165fx.ini", "FXList FX_P165BombExplosion\n  Sound\n    Name = P165ExplosionSound\n  End\n  LightPulse\n    Color = R:255 G:100 B:0\n    Radius = 25\n    IncreaseTime = 0\n    DecreaseTime = 500\n  End\nEnd\n").empty());
    P165FxSink sink(playback);
    w.logic->fxEvents().setSink(&sink);
    const int player = w.playerIndex("Alice");
    const Coord3D spot{300.0f, 150.0f, w.logic->getGroundHeight(300.0f, 150.0f)};
    GameMessage click(MSG_DO_WEAPON_AT_LOCATION, player);
    click.appendIntegerArgument(2);
    click.appendLocationArgument(spot);
    click.appendIntegerArgument(0x7fffffff);
    w.send(click);
    const int elapsed = w.runUntil([&] { return w.combat().counters().projectilesDetonated >= members.size(); }, 300);
    CHECK(elapsed < 300); // tolerance: individual projectile flight and weapon timing are asynchronous
    CHECK(w.combat().counters().projectilesLaunched >= members.size()); // tolerance: repeats may launch while other bombs are in flight
    CHECK(w.combat().counters().projectilesDetonated >= members.size()); // tolerance: multiple landings can occur in one update
    CHECK(sink.impacts >= (int)members.size()); // tolerance: multiple landings can occur in one update
    CHECK(sink.played == sink.impacts);
    CHECK(playback.lightPulses().additions() == (unsigned long long)sink.played);
    for (const Coord3D &position : sink.positions)
    {
        const float dx = position.x - spot.x, dy = position.y - spot.y;
        CHECK(dx * dx + dy * dy <= 40.01f * 40.01f); // tolerance: horde ScatterRadius 40 plus floating-point rounding
    }
    CHECK(playback.events().size() == (size_t)sink.played);
    for (const auto &event : playback.events())
    {
        CHECK(event.kind == "sound");
        CHECK(event.detail == "P165ExplosionSound");
    }
    CHECK(w.health(target) < 100.0f); // tolerance: hits depend on projectile scatter and impact order
    CHECK(horde->getPosition()->x == hordeStart.x);
    CHECK(horde->getPosition()->y == hordeStart.y);
    for (size_t i = 0; i < members.size(); ++i)
    {
        CHECK(members[i]->getWeapons()->curSlot() == 2);
        CHECK(members[i]->getWeapons()->stats().shotsFired >= 1); // tolerance: repeated volleys can fire before every bomb lands
        CHECK(members[i]->getPosition()->x == memberStarts[i].x);
        CHECK(members[i]->getPosition()->y == memberStarts[i].y);
    }
    w.logic->fxEvents().setSink(nullptr);
}

TEST_CASE("PORT-165: active position horde survives a full game snapshot")
{
    TestWorld w;
    w.combat().setAutoAcquireEnabled(false);
    Object *horde = w.unit("P165ArcherHorde", 'A', 300, 600);
    REQUIRE(horde);
    const ObjectID id = horde->getID();
    w.frames(12);
    WeaponSetToggle::setObjectWeaponSetFlag(*horde, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
    for (Object *member : w.membersOf(horde)) WeaponSetToggle::setObjectWeaponSetFlag(*member, WeaponSetToggle::WEAPONSET_TOGGLE_1, true);
    REQUIRE(horde->getWeapons()->setWeaponLock(2, LOCKED_TEMPORARILY));
    const Coord3D spot{300.0f, 150.0f, w.logic->getGroundHeight(300.0f, 150.0f)};
    horde->getAIUpdateInterface()->aiAttackPosition(spot, 0x7fffffff, CMD_FROM_PLAYER);
    auto *attack = dynamic_cast<AIAttackState *>(horde->getAIUpdateInterface()->stateMachine().currentState());
    REQUIRE(attack);
    REQUIRE(attack->attackMachine());
    REQUIRE(attack->attackMachine()->currentStateId() == 0x2BC);
    const auto hash = w.logic->computeStateHash();
    std::vector<std::uint8_t> saved;
    std::string error;
    REQUIRE_MESSAGE(w.logic->saveSnapshot(saved, &error), error);
    w.frames(2);
    REQUIRE_MESSAGE(w.logic->loadSnapshot(saved, &error), error);
    horde = w.logic->findObjectByID(id);
    REQUIRE(horde);
    attack = dynamic_cast<AIAttackState *>(horde->getAIUpdateInterface()->stateMachine().currentState());
    REQUIRE(attack);
    REQUIRE(attack->attackMachine());
    CHECK(attack->attackMachine()->currentStateId() == 0x2BC);
    CHECK(w.logic->computeStateHash() == hash);
    const int frames = w.runUntil([&] { return w.hordeOf(horde)->attackStats().positionOrders > 0; }, 600);
    CHECK(frames < 600); // tolerance: the restored horde's pathfinding and travel are asynchronous
}


TEST_CASE("PORT-165: projectile approach records precise-Z stop and does not throw (RW 0x74E9EC)")
{
    TestWorld w;
    w.combat().setAutoAcquireEnabled(false);
    Object *unit = w.unit("P165SpecialApproach", 'A', 300, 300);
    REQUIRE(unit);
    w.frames(2);
    auto *ai = unit->getAIUpdateInterface();
    REQUIRE(ai);
    REQUIRE(ai->curLocomotor());
    AIStateMachine machine(*ai, "P165ProjectileApproach");
    machine.setGoalPosition(Coord3D{300.0f, 200.0f, 0.0f});
    AIHordePositionApproachState state(machine);
    CHECK(state.onEnter() == STATE_SUCCESS);
    const auto report = w.logic->report();
    const std::string line = "[S-7433] position approach: projectile locomotor precise-Z flag is not ported (RW 0x74E9EC / 0x74A51B)";
    REQUIRE(report.stopHits.count(line) == 1);
    CHECK(report.stopHits.at(line) == 1);
    CHECK_NOTHROW(state.onExit(EXIT_NORMAL));
    CHECK(w.logic->report().stopHits.at(line) == 2);
}

TEST_CASE("PORT-165: turret mood approach records S-1189 and does not throw (RW 0x74A4DC)")
{
    TestWorld w;
    w.combat().setAutoAcquireEnabled(false);
    Object *unit = w.unit("P165TurretApproach", 'A', 300, 300);
    Object *enemy = w.unit("Dummy", 'B', 305, 300);
    REQUIRE(unit);
    REQUIRE(enemy);
    w.frames(2);
    auto *ai = unit->getAIUpdateInterface();
    REQUIRE(ai);
    REQUIRE(ai->turretForCurWeapon());
    REQUIRE(ai->aiAttackObject(enemy, CMD_FROM_PLAYER));
    ai->aiIdle(CMD_FROM_PLAYER);
    REQUIRE(ai->orderedMeleeTarget() == enemy->getID());
    ai->setLastCommandSource(CMD_FROM_PLAYER);
    AIStateMachine machine(*ai, "P165TurretApproach");
    machine.setGoalPosition(Coord3D{300.0f, 600.0f, 0.0f});
    AIHordePositionApproachState state(machine);
    REQUIRE(state.onEnter() == STATE_CONTINUE);
    w.combat().setAutoAcquireEnabled(true);
    CHECK_NOTHROW(state.update());
    const auto report = w.logic->report();
    const std::string line = "[S-1189] position approach: turret mood-target update and AI +0x3C7 are not ported (RW 0x74A4DC)";
    REQUIRE(report.stopHits.count(line) == 1);
    CHECK(report.stopHits.at(line) == 1);
    state.onExit(EXIT_NORMAL);
}
