// OpenBFME unit tests: the defection parts lane BACKLOG4 ported for stop S-1222, in a synthetic world (CombatTestUtil's CombatWorld).
//
// TARGET (RotWK game.dat): Object::defect RW 0x699368 (RW 0x693919 for an ability in use, the AI's kept machine RW 0x663128, the slaved interface RW
// 0x8A4ED8 slot 2, the spawn interface RW 0x68C3A3 slots 9 / 12); Object::endDefection RW 0x69ABA7 (RW 0x693919, RW 0x663171); restoreOriginalTeam RW
// 0x69AB22 (the team's name looked up, RW 0x7A7483; the kill RW 0x69AB83); setTeam RW 0x69954A (the neutral team for a player who is not active). DONOR:
// the BFME2 decomp's AIUpdateInterface::BeginDefectedStateMachine / rva002633B6, ObjectRestoreOriginalTeam.cpp, ObjectSetTeam.cpp, ObjectRva00293105.cpp.

#include "doctest.h"
#include "CombatTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "GameClient/FXList.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/AI/AIStateMachine.h"

#include <list>
#include <memory>

#include <string>

namespace
{
struct DefectWorld : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	FXListStore fxLists;
	FXListStore *savedFx = TheFXListStore;
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;
	explicit DefectWorld(const char *objects)
	{
		TheSpecialPowerStore = &powers;
		TheFXListStore = &fxLists;
		TheAttributeModifierStore = &modifiers;
		modifiers.registerBlock(w.fx.env.blocks);
		w.fx.env.blocks.registerBlock("FXList", [](INI *ini) { ParseFXListDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		const std::string err = w.load(objects);
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~DefectWorld()
	{
		TheSpecialPowerStore = savedPowers;
		TheFXListStore = savedFx;
		TheAttributeModifierStore = savedModifiers;
	}
	Player *player(const char *name) { return players.findPlayerWithName(name); }
};

#define B4_UNIT(name, extra)                                                                                                                     \
	"Object " name "\n"                                                                                                                          \
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"                                                                                                 \
	" Geometry = CYLINDER\n GeometryMajorRadius = 8\n GeometryMinorRadius = 8\n GeometryHeight = 20\n"                                           \
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"                                                                                      \
	" Behavior = AIUpdateInterface AiTag\n End\n"                                                                                                \
	" LocomotorSet\n  Locomotor = WalkerLoco\n  Condition = SET_NORMAL\n  Speed = 55\n End\n"                                                    \
	" Behavior = TemporarilyDefectUpdate ModuleTag_TemporarilyDefectUpdate\n  DefectDuration = 60000\n End\n" extra "End\n"

const char kWorld[] =
	"SpecialPower SpecialAbilityTestScreech\n  Enum = SPECIAL_SCREECH\n  ReloadTime = 30000\nEnd\n"
	B4_UNIT("DefectWalker", "")
	B4_UNIT("DefectCaster",
		" Behavior = SpecialPowerModule PowerTag\n  SpecialPowerTemplate = SpecialAbilityTestScreech\n  UpdateModuleStartsAttack = Yes\n End\n"
		" Behavior = SpecialAbilityUpdate AbilityTag\n  SpecialPowerTemplate = SpecialAbilityTestScreech\n  UnpackTime = 2000\n  PreparationTime = 200\n"
		"  PackTime = 200\n  EffectRange = 180\n  IgnoreFacingCheck = Yes\n End\n")
	B4_UNIT("DefectSlave", " Behavior = SlavedUpdate SlaveTag\n  GuardMaxRange = 50\n  MarkUnselectable = Yes\n End\n")
	B4_UNIT("DefectSpawner", " Behavior = SpawnBehavior SpawnTag\n  SpawnNumber = 1\n  SpawnReplaceDelay = 600000\n  SpawnTemplateName = DefectWalker\n End\n")
	B4_UNIT("DefectReclaimer", " Behavior = SpawnBehavior SpawnTag\n  SpawnNumber = 2\n  SpawnReplaceDelay = 600000\n  SpawnTemplateName = DefectOrphan\n  CanReclaimOrphans = Yes\n End\n")
	B4_UNIT("DefectOrphan", " Behavior = SlavedUpdate SlaveTag\n  GuardMaxRange = 50\n End\n")
	B4_UNIT("DefectTransport", " Behavior = TransportContain TransportTag\n  Slots = 4\n End\n")
	"SpecialPower SpecialAbilityTestMode\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 10000\nEnd\n"
	B4_UNIT("DefectModeHero", " Behavior = WeaponModeSpecialPowerUpdate ModeTag\n  SpecialPowerTemplate = SpecialAbilityTestMode\n  Duration = 20000\n End\n");

bool same(const Coord3D &a, const Coord3D &b)
{
	return a.x == b.x && a.y == b.y && a.z == b.z;
}

const Coord3D kGoal{ 605.0f, 105.0f, 0.0f };
} // namespace

TEST_CASE("backlog4 defect: a temporary defection keeps the AI's machine and gives it back: the unit stands under its new owner and goes on with its order afterwards (RW 0x663128, 0x663171)")
{
	// control: the same walk, never defected: where it ends
	Coord3D arrived{};
	{
		DefectWorld c(kWorld);
		Object *walker = c.unit("DefectWalker", 'B', 105, 105);
		c.aiOf(walker)->aiMoveToPosition(kGoal, CMD_FROM_PLAYER);
		c.frames(120);
		REQUIRE(c.aiOf(walker)->isIdle());
		arrived = *walker->getPosition();
		REQUIRE(arrived.x != 105.0f);
	}
	DefectWorld f(kWorld);
	Object *hero = f.unit("DefectWalker", 'A', 105, 405);
	Object *walker = f.unit("DefectWalker", 'B', 105, 105);
	AIUpdateInterface *ai = f.aiOf(walker);
	ai->aiMoveToPosition(kGoal, CMD_FROM_PLAYER);
	f.frames(5);
	REQUIRE(ai->isMoving());
	REQUIRE(ai->currentStateId() == (unsigned)AI_MOVE_TO);
	REQUIRE_FALSE(ai->hasDefectedStateMachine());

	walker->defect(hero, false);
	CHECK(walker->getControllingPlayer() == f.player("Alice"));
	CHECK(ai->hasDefectedStateMachine());
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE); // RW 0x66315E .. 0x66316C: a new machine in its default state
	CHECK(ai->isIdle());
	walker->defect(hero, false); // RW 0x66312B: a machine is kept already
	CHECK(ai->currentStateId() == (unsigned)AI_IDLE);
	f.frames(2);
	const Coord3D held = *walker->getPosition();
	CHECK_FALSE(same(held, arrived));
	f.frames(20);
	CHECK(same(*walker->getPosition(), held)); // it stands while it is the hero's: the walk belongs to the kept machine

	walker->endDefection();
	CHECK(walker->getControllingPlayer() == f.player("Bob"));
	CHECK_FALSE(ai->hasDefectedStateMachine());
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_TO); // the kept machine, in the state it was kept in
	f.frames(120);
	CHECK(ai->isIdle());
	CHECK(same(*walker->getPosition(), arrived)); // it finished the walk it was on before it defected
	bool attackStop = false;
	for (const std::string &s : f.logic->report().stops)
	{
		attackStop = attackStop || s.rfind("[S-1222] a defecting object's attack state", 0) == 0;
	}
	CHECK_FALSE(attackStop); // a plain move state has no slot 0x40 work (RW 0x63F3BF)
}

TEST_CASE("backlog4 defect: a permanent defection leaves the AI's order as it is (RW 0x6993A9: no kept machine, no idle)")
{
	DefectWorld f(kWorld);
	Object *hero = f.unit("DefectWalker", 'A', 105, 405);
	Object *walker = f.unit("DefectWalker", 'B', 105, 105);
	AIUpdateInterface *ai = f.aiOf(walker);
	ai->aiMoveToPosition(kGoal, CMD_FROM_PLAYER);
	f.frames(5);
	REQUIRE(ai->currentStateId() == (unsigned)AI_MOVE_TO);
	walker->defect(hero, true);
	CHECK(walker->getControllingPlayer() == f.player("Alice"));
	CHECK_FALSE(ai->hasDefectedStateMachine());
	CHECK(ai->currentStateId() == (unsigned)AI_MOVE_TO);
	CHECK(ai->lastCommandSource() == CMD_FROM_PLAYER); // HERO-2's inference idled it from the AI
}

TEST_CASE("backlog4 defect: the kept machine is in the state hash and an object deleted while defected leaves nothing behind")
{
	DefectWorld a(kWorld), b(kWorld);
	Object *heroA = a.unit("DefectWalker", 'A', 105, 405);
	Object *walkerA = a.unit("DefectWalker", 'B', 105, 105);
	b.unit("DefectWalker", 'A', 105, 405);
	Object *walkerB = b.unit("DefectWalker", 'B', 105, 105);
	REQUIRE(a.logic->computeStateHash() == b.logic->computeStateHash());
	a.aiOf(walkerA)->beginDefectedStateMachine();
	CHECK(a.logic->computeStateHash() != b.logic->computeStateHash());
	a.aiOf(walkerA)->endDefectedStateMachine();
	CHECK_FALSE(a.aiOf(walkerA)->hasDefectedStateMachine());
	(void)walkerB;
	walkerA->defect(heroA, false);
	const ObjectID id = walkerA->getID();
	a.logic->destroyObject(walkerA);
	a.frames(2);
	CHECK(a.logic->findObjectByID(id) == nullptr);
}

TEST_CASE("backlog4 defect: the original team is kept by name and looked up; a return to a defeated player goes to the neutral team, and to a defeated neutral player it dies (RW 0x69959D, 0x7A7483, 0x699554, 0x69AB83)")
{
	DefectWorld f(kWorld);
	Object *hero = f.unit("DefectWalker", 'A', 105, 405);
	Object *back = f.unit("DefectWalker", 'B', 105, 105);
	Object *orphan = f.unit("DefectWalker", 'B', 205, 105);
	Object *doomed = f.unit("DefectWalker", 'B', 305, 105);
	Player *alice = f.player("Alice"), *bob = f.player("Bob"), *neutral = f.players.getNeutralPlayer();
	REQUIRE(neutral != bob);
	CHECK(back->getOriginalTeamName() == "Bob/" + bob->getDefaultTeam()->getPrototype()->getName());
	CHECK(back->getOriginalTeam() == bob->getDefaultTeam());

	// the plain case: back to its own team, alive
	back->defect(hero, false);
	CHECK(back->getControllingPlayer() == alice);
	CHECK(back->getOriginalTeam() == bob->getDefaultTeam()); // RW 0x699513 does not record the temporary team
	back->endDefection();
	CHECK(back->getTeam() == bob->getDefaultTeam());
	CHECK_FALSE(back->isEffectivelyDead());

	// its player was defeated meanwhile: RW 0x699554 gives it to the neutral player's default team; the neutral player is not defeated, so it lives
	orphan->defect(hero, false);
	doomed->defect(hero, false);
	bob->setDefeated(true);
	orphan->endDefection();
	CHECK(orphan->getTeam() == neutral->getDefaultTeam());
	CHECK(orphan->getControllingPlayer() == neutral);
	CHECK_FALSE(orphan->isEffectivelyDead());
	CHECK(orphan->getOriginalTeamName() == neutral->getPlayerName() + "/" + neutral->getDefaultTeam()->getPrototype()->getName()); // setTeam recorded the new team

	// the player it ends with is defeated: RW 0x69AB83 kills it (UNRESISTABLE, NORMAL)
	neutral->setDefeated(true);
	doomed->endDefection();
	CHECK(doomed->getControllingPlayer() == neutral);
	CHECK(doomed->isEffectivelyDead());
	CHECK_FALSE(orphan->isEffectivelyDead()); // the kill is the returning object's alone
	neutral->setDefeated(false);
	bob->setDefeated(false);

	// setTeam itself (RW 0x699554): a defeated player's team is refused, an active player's team is taken
	Object *given = f.unit("DefectWalker", 'A', 405, 405);
	bob->setDefeated(true);
	given->setTeam(bob->getDefaultTeam());
	CHECK(given->getTeam() == neutral->getDefaultTeam());
	bob->setDefeated(false);
	given->setTeam(bob->getDefaultTeam());
	CHECK(given->getTeam() == bob->getDefaultTeam());
}

TEST_CASE("backlog4 defect: an ability in use is aborted when its object defects (RW 0x693919 -> 0x851C92 / 0x8513E4); without the defection it runs to its effect")
{
	const int kUsing = ObjectTemplateInfoBuilder::objectStatusIndex("SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING");
	REQUIRE(kUsing == 0x46);
	// control: the ability, left alone, triggers
	{
		DefectWorld c(kWorld);
		Object *caster = c.unit("DefectCaster", 'B', 300, 300);
		auto *u = dynamic_cast<SpecialAbilityUpdate *>(caster->findModule("SpecialAbilityUpdate"));
		REQUIRE(u);
		u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
		c.frames(3);
		REQUIRE(caster->testStatus((unsigned)kUsing));
		REQUIRE(u->isActive());
		REQUIRE(u->isPowerCurrentlyInUse());
		c.frames(40);
		CHECK(u->abilitiesTriggered() == 1);
	}
	DefectWorld f(kWorld);
	Object *hero = f.unit("DefectWalker", 'A', 105, 405);
	Object *caster = f.unit("DefectCaster", 'B', 300, 300);
	auto *u = dynamic_cast<SpecialAbilityUpdate *>(caster->findModule("SpecialAbilityUpdate"));
	REQUIRE(u);
	u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	f.frames(3);
	REQUIRE(caster->testStatus((unsigned)kUsing));
	REQUIRE(u->isPowerCurrentlyInUse());
	caster->defect(hero, false);
	CHECK_FALSE(caster->testStatus((unsigned)kUsing)); // finishAbility(false, true) cleared it (RW 0x8530BA)
	CHECK_FALSE(u->isPowerCurrentlyInUse());
	f.frames(40);
	CHECK(u->abilitiesTriggered() == 0); // the effect never ran
	bool reported = false;
	for (const std::string &s : f.logic->report().stops)
	{
		reported = reported || s.find("RW 0x693919") != std::string::npos;
	}
	CHECK_FALSE(reported); // the former "[S-1222] Object::defect: RW 0x693919 ... is not ported" is gone
	// SUMMONING_REPLACEMENT (status 0x66) skips the abort (RW 0x693928 .. 0x693933)
	DefectWorld g(kWorld);
	Object *summoned = g.unit("DefectCaster", 'B', 300, 300);
	auto *su = dynamic_cast<SpecialAbilityUpdate *>(summoned->findModule("SpecialAbilityUpdate"));
	su->initiateIntentToDoSpecialPower(su->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	g.frames(3);
	const int kSummoning = ObjectTemplateInfoBuilder::objectStatusIndex("SUMMONING_REPLACEMENT");
	REQUIRE(kSummoning == 0x66);
	summoned->setStatus((unsigned)kSummoning, true);
	summoned->abortAbilitiesInUse();
	CHECK(su->isPowerCurrentlyInUse());
	summoned->setStatus((unsigned)kSummoning, false);
	summoned->abortAbilitiesInUse();
	CHECK_FALSE(su->isPowerCurrentlyInUse());
}

TEST_CASE("backlog4 defect: a defecting slave leaves its master and a defecting spawner stops spawning (RW 0x8A4ED8 slot 2, RW 0x68C3A3 slots 9 / 12)")
{
	DefectWorld f(kWorld);
	Object *hero = f.unit("DefectWalker", 'A', 105, 405);
	Object *master = f.unit("DefectWalker", 'B', 300, 300);
	Object *slave = f.unit("DefectSlave", 'B', 320, 300);
	Object *control = f.unit("DefectSlave", 'B', 340, 300);
	SlavedUpdateInterface *si = SlavedUpdateInterface::of(*slave);
	SlavedUpdateInterface *ci = SlavedUpdateInterface::of(*control);
	REQUIRE(si);
	REQUIRE(ci);
	si->onEnslave(master);
	ci->onEnslave(master);
	const int kUnselectable = ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE");
	REQUIRE(si->getSlaverID() == master->getID());
	REQUIRE(slave->testStatus((unsigned)kUnselectable));
	slave->defect(hero, false);
	CHECK(si->getSlaverID() == INVALID_ID);
	CHECK_FALSE(slave->testStatus((unsigned)kUnselectable));
	CHECK(ci->getSlaverID() == master->getID()); // the slave that did not defect keeps its master
	CHECK(control->testStatus((unsigned)kUnselectable));

	Object *spawner = f.unit("DefectSpawner", 'B', 500, 300);
	Object *spawnerControl = f.unit("DefectSpawner", 'B', 600, 300);
	SpawnBehaviorInterface *sp = SpawnBehaviorInterface::of(*spawner);
	SpawnBehaviorInterface *spc = SpawnBehaviorInterface::of(*spawnerControl);
	REQUIRE(sp);
	REQUIRE(spc);
	REQUIRE(sp->isSpawnerActive());
	spawner->defect(hero, false);
	CHECK_FALSE(sp->isSpawnerActive());
	CHECK(spc->isSpawnerActive());
}

// ---- review round (Sol r1) -------------------------------------------------------------------------------------------------------------------------------

namespace
{
bool reported(GameLogic &logic, const std::string &prefix)
{
	for (const std::string &l : logic.report().stops)
	{
		if (l.rfind(prefix, 0) == 0)
		{
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("backlog4 defect r1: the kept machine is hashed whole (RW AI + 0x38): its saved goal and its state each change the world hash")
{
	auto world = [](int variant) {
		auto f = std::make_unique<DefectWorld>(kWorld);
		Object *o = f->unit("DefectWalker", 'B', 105, 105);
		AIUpdateInterface *ai = f->aiOf(o);
		if (variant == 2)
		{
			ai->aiMoveToPosition(kGoal, CMD_FROM_PLAYER); // another state kept (AI_MOVE_TO): the current, new machines are alike
			f->frames(1);
		}
		AIStateMachine &kept = ai->stateMachine();
		ai->beginDefectedStateMachine();
		if (variant == 1)
		{
			kept.setGoalPosition(Coord3D{ 505.0f, 405.0f, 0.0f }); // Sol's probe: only the kept machine's goal differs
		}
		return f;
	};
	auto base = world(0), goal = world(1);
	CHECK(base->logic->computeStateHash() != goal->logic->computeStateHash());
	REQUIRE(base->aiOf(base->logic->getFirstObject()) != nullptr);
	// two worlds whose kept machines are in different states; their current machines are both the fresh idle one
	auto idleKept = world(0);
	auto movingKept = world(2);
	Object *wi = idleKept->logic->getFirstObject();
	Object *wm = movingKept->logic->getFirstObject();
	CHECK(idleKept->aiOf(wi)->currentStateId() == movingKept->aiOf(wm)->currentStateId());
	CHECK(idleKept->logic->computeStateHash() != movingKept->logic->computeStateHash());
	// the same world twice hashes alike (no pointer or order in the kept machine's hash)
	CHECK(world(1)->logic->computeStateHash() == goal->logic->computeStateHash());
}

TEST_CASE("backlog4 defect r1: a defecting spawner takes its due orphans (RW 0x86327C) and stops: back with its owner it reclaims and spawns nothing more; a control spawner goes on")
{
	DefectWorld f(kWorld);
	Object *hero = f.unit("DefectWalker", 'A', 105, 405);
	const unsigned kUnderConstruction = (unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION");
	Object *spawner = f.unit("DefectReclaimer", 'B', 300, 600);
	Object *control = f.unit("DefectReclaimer", 'B', 800, 600);
	// UNDER_CONSTRUCTION keeps their updates from spawning or reclaiming (RW 0x862456) while their due replacements are listed (two each)
	spawner->setStatus(kUnderConstruction, true);
	control->setStatus(kUnderConstruction, true);
	Object *orphan = f.unit("DefectOrphan", 'B', 320, 600);
	Object *orphan2 = f.unit("DefectOrphan", 'B', 340, 600);
	Object *controlOrphan = f.unit("DefectOrphan", 'B', 820, 600);
	Object *controlOrphan2 = f.unit("DefectOrphan", 'B', 840, 600);
	f.frames(3);
	SpawnBehavior *sb = dynamic_cast<SpawnBehavior *>(spawner->findModule("SpawnBehavior"));
	SpawnBehavior *cb = dynamic_cast<SpawnBehavior *>(control->findModule("SpawnBehavior"));
	REQUIRE(sb);
	REQUIRE(cb);
	REQUIRE(sb->replacementFrames().size() == 2u);
	REQUIRE(sb->spawns().empty());
	REQUIRE(orphan->getProducerID() == INVALID_ID);

	spawner->defect(hero, false);
	// RW 0x6993FD: slot 12 ran with the spawner's own player still in charge: both due replacements took the nearest orphans, enslaved to the spawner
	CHECK(sb->spawns() == std::list<ObjectID>{ orphan->getID(), orphan2->getID() });
	CHECK(orphan->getProducerID() == spawner->getID());
	CHECK(orphan2->getProducerID() == spawner->getID());
	CHECK(SlavedUpdateInterface::of(*orphan)->getSlaverID() == spawner->getID());
	CHECK(orphan->getControllingPlayer() == f.player("Bob")); // the slaves stay their player's (the spawner defects alone)
	CHECK_FALSE(SpawnBehaviorInterface::of(*spawner)->isSpawnerActive());
	CHECK(controlOrphan->getProducerID() == INVALID_ID);

	// both back in play: the control reclaims by its update; the stopped spawner never again (its slave dies: no replacement)
	spawner->endDefection();
	REQUIRE(spawner->getControllingPlayer() == f.player("Bob"));
	spawner->setStatus(kUnderConstruction, false);
	control->setStatus(kUnderConstruction, false);
	Object *lateOrphan = f.unit("DefectOrphan", 'B', 300, 640);
	orphan->kill(DAMAGE_UNRESISTABLE, 0);
	f.frames(30);
	CHECK(cb->spawns() == std::list<ObjectID>{ controlOrphan->getID(), controlOrphan2->getID() });
	CHECK(controlOrphan->getProducerID() == control->getID());
	CHECK(lateOrphan->getProducerID() == INVALID_ID);
	CHECK(sb->spawns().size() == 1u); // the dead slave is gone and not replaced
	CHECK(sb->spawns().front() == orphan2->getID());
}

TEST_CASE("backlog4 defect r1: the original team is found by its name: a later team of the same prototype is the one the object returns to (RW 0x7A7483)")
{
	DefectWorld f(kWorld);
	Object *hero = f.unit("DefectWalker", 'A', 105, 405);
	Player *bob = f.player("Bob");
	TeamPrototype *raid = f.teams.initTeam("teamRaidB4", bob, false);
	REQUIRE(raid);
	Team *first = f.teams.createTeam(raid, true);
	Object *o = f.unit("DefectWalker", 'B', 105, 105);
	o->setTeam(first);
	CHECK(o->getOriginalTeamName() == "Bob/teamRaidB4");
	o->defect(hero, false);
	REQUIRE(o->getControllingPlayer() == f.player("Alice"));
	Team *later = f.teams.createTeam(raid, true); // a script makes another team of the prototype while the object is away
	REQUIRE(later != first);
	o->endDefection();
	CHECK(o->getTeam() == later); // the name's newest team (retail's lookup), not the Team the object left
	CHECK(o->getTeam() != first);
	CHECK(o->getControllingPlayer() == bob);
}

namespace
{
// a world whose second player (Bob) is an observer (PlayerTemplate IsObserver, Player + 0x35A)
const char kObserverTemplates[] = "PlayerTemplate FactionB\n  Side = Beta\n  PlayableSide = Yes\n  IsObserver = Yes\nEnd\n";
struct ObserverWorld : logictest::LogicWorld
{
	ObserverWorld()
		: logictest::LogicWorld(RandomAlgorithm::ZH_CarryChain, kObserverTemplates)
	{
		REQUIRE_MESSAGE(loadError.empty(), loadError);
		LogicModules::registerAll(w.modules);
		const std::string err = w.load("Object ObserverDummy\n  KindOf = SELECTABLE\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
	}
};
} // namespace

TEST_CASE("backlog4 defect r1: setTeam gives an observer's team to the neutral player (RW 0x6AAC52: Player + 0x35A), separately from a defeated one")
{
	ObserverWorld f;
	Player *alice = f.players.findPlayerWithName("Alice");
	Player *bob = f.players.findPlayerWithName("Bob");
	Player *neutral = f.players.getNeutralPlayer();
	REQUIRE(bob->isObserver());
	REQUIRE_FALSE(bob->isDefeated());
	REQUIRE_FALSE(alice->isObserver());
	Object *o = f.make("ObserverDummy", alice->getDefaultTeam());
	o->setTeam(bob->getDefaultTeam());
	CHECK(o->getTeam() == neutral->getDefaultTeam());
	CHECK(o->getControllingPlayer() == neutral);
	o->setTeam(alice->getDefaultTeam()); // control: an active player's team is taken
	CHECK(o->getTeam() == alice->getDefaultTeam());
}

TEST_CASE("backlog4 defect r1: the dynamic stops are reported when met: a kept attack state, an active non-SpecialAbilityUpdate power update, a container's passengers")
{
	DefectWorld f(kWorld);
	Object *hero = f.unit("DefectWalker", 'A', 105, 405);
	// a swordsman attacking: its AIAttackState answers State slot 0x24 (active), whose slot 0x40 (RW 0x8DB749) is not read
	Object *sword = f.unit("Swordsman", 'B', 300, 300);
	Object *victim = f.unit("Swordsman", 'A', 700, 300); // far: the attack state is approaching (its machine is not idle)
	REQUIRE(f.aiOf(sword)->aiAttackObject(victim, CMD_FROM_PLAYER));
	f.frames(1);
	const std::string kAttack = "[S-1222] a defecting object's attack state is kept without its slot 0x40 call";
	REQUIRE_FALSE(reported(*f.logic, kAttack));
	sword->defect(hero, false);
	CHECK(reported(*f.logic, kAttack));
	CHECK(f.aiOf(sword)->hasDefectedStateMachine());

	// an active WeaponModeSpecialPowerUpdate on an object using an ability (status 0x46): RW 0x693919 reads its slots 0x18 / 0x34, the port does not
	Object *mode = f.unit("DefectModeHero", 'B', 500, 300);
	WeaponModeSpecialPowerUpdate *w = nullptr;
	for (const auto &m : mode->modules())
	{
		w = w ? w : dynamic_cast<WeaponModeSpecialPowerUpdate *>(m.get());
	}
	REQUIRE(w);
	w->doSpecialPower(0);
	REQUIRE(w->isActive());
	mode->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING"), true);
	const std::string kMode = "[S-1222] RW 0x693919: an active special power update that is not a SpecialAbilityUpdate is not aborted";
	REQUIRE_FALSE(reported(*f.logic, kMode));
	mode->defect(hero, false);
	CHECK(reported(*f.logic, kMode));

	// a transport with a passenger: contain vslot 0x108 of a class other than HordeContain is not read
	Object *transport = f.unit("DefectTransport", 'B', 700, 300);
	Object *rider = f.unit("DefectWalker", 'B', 700, 320);
	REQUIRE(transport->getContain());
	REQUIRE(transport->getContain()->addToContain(rider));
	const std::string kContain = "[S-1222] a defecting container's contain vslot 0x108 (not HordeContain) is not read";
	REQUIRE_FALSE(reported(*f.logic, kContain));
	transport->defect(hero, false);
	CHECK(reported(*f.logic, kContain));
	CHECK(rider->getControllingPlayer() == f.player("Bob")); // what the stop says: the passenger keeps its side
}
