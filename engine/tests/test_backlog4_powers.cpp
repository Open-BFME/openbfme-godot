// OpenBFME unit tests: the special power runtime parts lane BACKLOG4 ported for stop S-529, in a synthetic world (CombatTestUtil's CombatWorld).
//
// TARGET (RotWK game.dat): SpecialPowerModule::aboutToDoSpecialPower RW 0x89713F tells TheScriptEngine (RW 0x759986: the list at + 0x1A368 of the
// player), which PLAYER_TRIGGERED_SPECIAL_POWER / _FROM_NAMED read and consume (RW 0x7E4E08 -> 0x758CBF); startPowerRecharge RW 0x896E31 scales the reload
// by the object's RECHARGE_TIME attribute modifiers (RW 0x896EA0: RW 0x68C82D(0xC, &out, 0, 1)); initiateIntentToDoSpecialPower RW 0x897E87 idles the AI
// with IdleWhenStartingPower (RW 0x897F97) and plays InitiateFX when an update module drives the power (RW 0x897FFA). DONOR: the BFME2 decomp's
// SpecialPowerModuleTrigger.cpp, SpecialPowerModuleIsReady.cpp, SpecialPowerModuleRva0049466D.cpp, ScriptEngineRva003571B8.cpp.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/SpellCommands.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <string>
#include <vector>

using namespace scripttest;

namespace
{
struct PowerWorld : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	FXListStore fxLists;
	FXListStore *savedFx = TheFXListStore;
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;
	explicit PowerWorld(const char *objects)
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
	~PowerWorld()
	{
		TheSpecialPowerStore = savedPowers;
		TheFXListStore = savedFx;
		TheAttributeModifierStore = savedModifiers;
	}
	int indexOf(const char *player) { return players.findPlayerWithName(player)->getPlayerIndex(); }
};

#define B4_HERO(name, powerLines, extra)                                                                                                         \
	"Object " name "\n"                                                                                                                          \
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"                                                                                            \
	" Geometry = CYLINDER\n GeometryMajorRadius = 8\n GeometryMinorRadius = 8\n GeometryHeight = 20\n"                                           \
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"                                                                                      \
	" Behavior = AIUpdateInterface AiTag\n End\n"                                                                                                \
	" LocomotorSet\n  Locomotor = WalkerLoco\n  Condition = SET_NORMAL\n  Speed = 55\n End\n"                                                    \
	" Behavior = AttributeModifierPoolUpdate PoolTag\n End\n"                                                                                    \
	" Behavior = SpecialPowerModule PowerTag\n  SpecialPowerTemplate = SpecialPowerTestCry\n" powerLines " End\n" extra "End\n"

const char kWorld[] =
	"ModifierList TestQuickRecharge\n  Category = LEADERSHIP\n  Duration = 600000\n  Modifier = RECHARGE_TIME 50%\nEnd\n"
	"ModifierList TestArmorOnly\n  Category = LEADERSHIP\n  Duration = 600000\n  Modifier = ARMOR 150%\nEnd\n"
	"SpecialPower SpecialPowerTestCry\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 10000\nEnd\n"
	"SpecialPower SpecialPowerTestOther\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 10000\nEnd\n"
	B4_HERO("CryHero", "", "")
	B4_HERO("MarkHero", "  SetModelCondition = ModelConditionState:USER_1\n  SetModelConditionTime = 1.0\n", "")
	B4_HERO("IdleHero", "  IdleWhenStartingPower = Yes\n  InitiateFX = FX_TestInitiate\n", "")
	B4_HERO("PlainHero", "  InitiateFX = FX_TestInitiate\n", "")
	B4_HERO("DrivenHero", "  InitiateFX = FX_TestInitiate\n  UpdateModuleStartsAttack = Yes\n",
		" Behavior = SpecialAbilityUpdate AbilityTag\n  SpecialPowerTemplate = SpecialPowerTestCry\n  UnpackTime = 200\n  PreparationTime = 200\n"
		"  PackTime = 200\n  IgnoreFacingCheck = Yes\n End\n");

SpecialPowerModule *powerOf(Object *o)
{
	SpecialPowerModule *m = dynamic_cast<SpecialPowerModule *>(o->findModule("SpecialPowerModule"));
	REQUIRE(m != nullptr);
	return m;
}

unsigned long long initiateFx(PowerWorld &f)
{
	const auto &per = f.logic->fxEvents().perSite();
	const auto it = per.find("SpecialPower InitiateFX");
	return it == per.end() ? 0ull : it->second;
}
} // namespace

TEST_CASE("backlog4 powers: a triggered power is told to the script engine and PLAYER_TRIGGERED_SPECIAL_POWER reads and consumes it (RW 0x89713F, 0x759986, 0x7E4E08, 0x758CBF)")
{
	PowerWorld f(kWorld);
	Object *caster = f.unit("CryHero", 'A', 300, 300);
	Object *other = f.unit("CryHero", 'A', 400, 300);
	caster->setName("Caster");
	other->setName("Bystander");
	ScriptEngine &engine = f.logic->scriptEngine();
	const int alice = f.indexOf("Alice"), bob = f.indexOf("Bob");
	const ScriptCondition any = cond("PLAYER_TRIGGERED_SPECIAL_POWER", { sp(11, "Alice"), sp(31, "SpecialPowerTestCry") });
	const ScriptCondition anyBob = cond("PLAYER_TRIGGERED_SPECIAL_POWER", { sp(11, "Bob"), sp(31, "SpecialPowerTestCry") });
	const ScriptCondition anyOtherPower = cond("PLAYER_TRIGGERED_SPECIAL_POWER", { sp(11, "Alice"), sp(31, "SpecialPowerTestOther") });
	const ScriptCondition fromCaster = cond("PLAYER_TRIGGERED_SPECIAL_POWER_FROM_NAMED", { sp(11, "Alice"), sp(31, "SpecialPowerTestCry"), sp(14, "Caster") });
	const ScriptCondition fromBystander = cond("PLAYER_TRIGGERED_SPECIAL_POWER_FROM_NAMED", { sp(11, "Alice"), sp(31, "SpecialPowerTestCry"), sp(14, "Bystander") });
	const ScriptCondition fromNobody = cond("PLAYER_TRIGGERED_SPECIAL_POWER_FROM_NAMED", { sp(11, "Alice"), sp(31, "SpecialPowerTestCry"), sp(14, "NoSuchUnit") });

	// nothing was cast: every form is false and no list has an entry
	CHECK_FALSE(ScriptConditions::evaluate(engine, any));
	CHECK_FALSE(ScriptConditions::evaluate(engine, fromCaster));
	CHECK(engine.triggeredSpecialPowers(alice).empty());

	SpecialPowerModule *power = powerOf(caster);
	power->doSpecialPower(0);
	REQUIRE(power->triggers() == 1);
	REQUIRE(engine.triggeredSpecialPowers(alice).size() == 1);
	CHECK(engine.triggeredSpecialPowers(alice)[0].nameCrc == 0x48AA3AD4u); // CRC_String("SpecialPowerTestCry") (RW 0x7ED86B), zlib.crc32 of the name
	CHECK(engine.triggeredSpecialPowers(alice)[0].source == caster->getID());
	CHECK(engine.triggeredSpecialPowers(bob).empty());
	CHECK(powerOf(other)->triggers() == 0);

	// the forms that do not match leave the entry where it is
	CHECK_FALSE(ScriptConditions::evaluate(engine, anyBob));
	CHECK_FALSE(ScriptConditions::evaluate(engine, anyOtherPower));
	CHECK_FALSE(ScriptConditions::evaluate(engine, fromBystander));
	CHECK_FALSE(ScriptConditions::evaluate(engine, fromNobody)); // RW 0x7E4E23: the named unit does not exist
	CHECK(engine.triggeredSpecialPowers(alice).size() == 1);
	// the matching form is true once: RW 0x758D18 removes the entry
	CHECK(ScriptConditions::evaluate(engine, any));
	CHECK(engine.triggeredSpecialPowers(alice).empty());
	CHECK_FALSE(ScriptConditions::evaluate(engine, any));

	// two casts by two units: the named form takes its own unit's entry and leaves the other
	f.frames(60); // past the recharge
	power->doSpecialPower(0);
	powerOf(other)->doSpecialPower(0);
	REQUIRE(engine.triggeredSpecialPowers(alice).size() == 2);
	CHECK(ScriptConditions::evaluate(engine, fromBystander));
	REQUIRE(engine.triggeredSpecialPowers(alice).size() == 1);
	CHECK(engine.triggeredSpecialPowers(alice)[0].source == caster->getID());
	CHECK_FALSE(ScriptConditions::evaluate(engine, fromBystander));
	CHECK(ScriptConditions::evaluate(engine, fromCaster));
	CHECK(engine.triggeredSpecialPowers(alice).empty());
}

TEST_CASE("backlog4 powers: the script engine's triggered list is in the state hash, is cleared by the reset and refuses a player index outside its 20 lists by name")
{
	PowerWorld a(kWorld), b(kWorld);
	a.unit("CryHero", 'A', 300, 300);
	b.unit("CryHero", 'A', 300, 300);
	// the engine's own hash (GameLogic adds it once map scripts are loaded: a game without scripts has no reader of the list and keeps its hash)
	auto engineHash = [](const GameLogic &l) {
		StateHasher h;
		l.scriptEngine().crc(h);
		return h.value();
	};
	REQUIRE(engineHash(*a.logic) == engineHash(*b.logic));
	ScriptEngine &engine = a.logic->scriptEngine();
	engine.notifyOfTriggeredSpecialPower(0, "SpecialPowerTestCry", 7);
	const std::uint32_t one = engineHash(*a.logic);
	CHECK(one != engineHash(*b.logic));
	b.logic->scriptEngine().notifyOfTriggeredSpecialPower(0, "SpecialPowerTestCry", 8); // another source
	CHECK(one != engineHash(*b.logic));
	b.logic->scriptEngine().isSpecialPowerTriggered(0, "SpecialPowerTestCry", true, 8);
	b.logic->scriptEngine().notifyOfTriggeredSpecialPower(1, "SpecialPowerTestCry", 7); // another player
	CHECK(one != engineHash(*b.logic));
	// the name is case sensitive (CRC_String, not CRC_Stringi) and the source 0 matches any
	CHECK_FALSE(engine.isSpecialPowerTriggered(0, "specialpowertestcry", false, 0));
	CHECK_FALSE(engine.isSpecialPowerTriggered(0, "SpecialPowerTestCry", false, 9));
	CHECK(engine.isSpecialPowerTriggered(0, "SpecialPowerTestCry", false, 7));
	CHECK(engine.isSpecialPowerTriggered(0, "SpecialPowerTestCry", false, 0));
	CHECK(engine.triggeredSpecialPowers(0).size() == 1); // not removed without the flag
	// the 20 lists: RW 0x758CC9 .. 0x758CCE refuses the reader's index; the writer's is refused here and reported
	CHECK_FALSE(engine.isSpecialPowerTriggered(-1, "SpecialPowerTestCry", true, 0));
	CHECK_FALSE(engine.isSpecialPowerTriggered(20, "SpecialPowerTestCry", true, 0));
	const std::string kNote = "[S-529] a special power triggered by a player index outside the script engine's 20 lists (RW 0x759986 does not test it) is not recorded";
	CHECK(engine.stats().notes.count(kNote) == 0);
	engine.notifyOfTriggeredSpecialPower(20, "SpecialPowerTestCry", 7);
	engine.notifyOfTriggeredSpecialPower(-1, "SpecialPowerTestCry", 7);
	REQUIRE(engine.stats().notes.count(kNote) == 1);
	CHECK(engine.stats().notes.at(kNote) == 2);
	for (int i = 0; i < ScriptEngine::kSpecialPowerListPlayers; ++i)
	{
		CAPTURE(i);
		CHECK(engine.triggeredSpecialPowers(i).size() == (i == 0 ? 1u : 0u));
	}
	engine.notifyOfTriggeredSpecialPower(19, "SpecialPowerTestCry", 7); // the last list
	CHECK(engine.triggeredSpecialPowers(19).size() == 1);
	engine.reset();
	CHECK(engine.triggeredSpecialPowers(0).empty());
	CHECK(engine.triggeredSpecialPowers(19).empty());
}

TEST_CASE("backlog4 powers: the recharge is scaled by the object's RECHARGE_TIME attribute modifiers (RW 0x896EA0); another modifier type does not scale it")
{
	PowerWorld f(kWorld);
	Object *plain = f.unit("CryHero", 'A', 300, 300);
	Object *quick = f.unit("CryHero", 'A', 400, 300);
	Object *armored = f.unit("CryHero", 'A', 500, 300);
	auto pool = [](Object *o) { return dynamic_cast<AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate")); };
	REQUIRE(pool(quick));
	REQUIRE(pool(quick)->add("TestQuickRecharge", -1));
	REQUIRE(pool(armored)->add("TestArmorOnly", -1));
	f.frames(1);
	const UnsignedInt now = f.logic->getFrame();
	powerOf(plain)->doSpecialPower(0);
	powerOf(quick)->doSpecialPower(0);
	powerOf(armored)->doSpecialPower(0);
	REQUIRE(powerOf(quick)->triggers() == 1);
	// ReloadTime 10000 ms = 50 logic frames; fild 50 ; fmul 1.0 (no discount flag) ; fmul 0.5 ; _ftol2 = 25
	CHECK(powerOf(plain)->getReadyFrame() - now == 50u);
	CHECK(powerOf(quick)->getReadyFrame() - now == 25u);
	CHECK(powerOf(armored)->getReadyFrame() - now == 50u);
	// what the player sees: the quick one is ready again while the plain one still recharges
	f.frames(26);
	CHECK(powerOf(quick)->isReady());
	CHECK_FALSE(powerOf(plain)->isReady());
	CHECK_FALSE(powerOf(armored)->isReady());
}

TEST_CASE("backlog4 powers: IdleWhenStartingPower idles the caster's AI from the AI source (RW 0x897F97); InitiateFX plays only when an update module drives the power (RW 0x897FFA)")
{
	PowerWorld f(kWorld);
	Object *idle = f.unit("IdleHero", 'A', 105, 105);
	Object *plain = f.unit("PlainHero", 'A', 105, 205);
	Object *driven = f.unit("DrivenHero", 'A', 105, 305);
	f.aiOf(idle)->aiMoveToPosition(Coord3D{ 605.0f, 105.0f, 0.0f }, CMD_FROM_PLAYER);
	f.aiOf(plain)->aiMoveToPosition(Coord3D{ 605.0f, 205.0f, 0.0f }, CMD_FROM_PLAYER);
	f.frames(5);
	REQUIRE(f.aiOf(idle)->isMoving());
	REQUIRE(f.aiOf(plain)->isMoving());
	REQUIRE(initiateFx(f) == 0);

	// control: neither the flag nor an update module: the cast leaves the move alone and plays no InitiateFX
	powerOf(plain)->doSpecialPower(0);
	const Coord3D plainAtCast = *plain->getPosition();
	REQUIRE(powerOf(plain)->triggers() == 1);
	CHECK_FALSE(f.aiOf(plain)->isIdle());
	CHECK(f.aiOf(plain)->lastCommandSource() == CMD_FROM_PLAYER);
	CHECK(initiateFx(f) == 0);

	// the flag: idle at once, from the AI; still no InitiateFX (no update module drives this power)
	powerOf(idle)->doSpecialPower(0);
	REQUIRE(powerOf(idle)->triggers() == 1);
	CHECK(f.aiOf(idle)->isIdle());
	CHECK(f.aiOf(idle)->lastCommandSource() == CMD_FROM_AI);
	CHECK(initiateFx(f) == 0);
	const Coord3D stopped = *idle->getPosition();
	f.frames(20);
	CHECK(idle->getPosition()->x == stopped.x); // it stays where the power stopped it
	CHECK(idle->getPosition()->y == stopped.y);
	CHECK(f.aiOf(plain)->isMoving());
	CHECK(plain->getPosition()->x != plainAtCast.x); // the control walked on

	// an update module drives the power: InitiateFX on the caster, once
	powerOf(driven)->doSpecialPower(0);
	CHECK(powerOf(driven)->initiates() == 1);
	CHECK(initiateFx(f) == 1);
}

// ---- review round (Sol r1) -------------------------------------------------------------------------------------------------------------------------------

namespace
{
int modelCondition(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::string(TheModelConditionNames[i]) == name)
		{
			return i;
		}
	}
	return -1;
}

// MSG_DO_SPECIAL_POWER {id, options, source} through the logic's command handler (RW 0x82DFB7's ported part refuses a power that is not fully ready)
void castByCommand(GameLogicDispatch &d, const Object &source, const SpecialPowerTemplate &t)
{
	GameMessage m(MSG_DO_SPECIAL_POWER, source.getControllingPlayer()->getPlayerIndex());
	m.appendIntegerArgument((int)t.getID());
	m.appendIntegerArgument(0);
	m.appendObjectIDArgument(source.getID());
	d.dispatch(m);
}
} // namespace

TEST_CASE("backlog4 powers r1: with a RECHARGE_TIME 50% modifier the second cast by command takes effect at half the reload; the plain hero's is refused (RW 0x896EA0)")
{
	PowerWorld f(kWorld);
	GameLogicDispatch dispatch(*f.logic);
	SpellCommands spells;
	spells.registerHandlers(dispatch);
	Object *plain = f.unit("MarkHero", 'A', 300, 300);
	Object *quick = f.unit("MarkHero", 'A', 400, 300);
	REQUIRE(dynamic_cast<AttributeModifierPool *>(quick->findModule("AttributeModifierPoolUpdate"))->add("TestQuickRecharge", -1));
	f.frames(1);
	const int kMark = modelCondition("USER_1");
	REQUIRE(kMark >= 0); // tolerance: the index of a name in the binary's model condition table
	const SpecialPowerTemplate *t = powerOf(plain)->getSpecialPowerTemplate();
	REQUIRE(t);
	// the first casts: both take effect (SetModelCondition USER_1 for 1 s = 5 frames, RW 0x897A63)
	castByCommand(dispatch, *plain, *t);
	castByCommand(dispatch, *quick, *t);
	CHECK(spells.casts() == 2u);
	CHECK(plain->testModelCondition(kMark));
	CHECK(quick->testModelCondition(kMark));
	f.frames(26); // past the quick hero's 25-frame recharge, inside the plain one's 50 frames; the marks have expired
	REQUIRE_FALSE(plain->testModelCondition(kMark));
	REQUIRE_FALSE(quick->testModelCondition(kMark));
	castByCommand(dispatch, *plain, *t);
	castByCommand(dispatch, *quick, *t);
	CHECK(spells.casts() == 3u);
	CHECK(spells.refused() == 1u);
	CHECK(quick->testModelCondition(kMark));       // the second cast's effect in the world
	CHECK_FALSE(plain->testModelCondition(kMark)); // refused: still recharging
	CHECK(powerOf(quick)->triggers() == 2u);
	CHECK(powerOf(plain)->triggers() == 1u);
	f.frames(25);
	castByCommand(dispatch, *plain, *t); // the plain hero's reload is over at 50 frames
	CHECK(plain->testModelCondition(kMark));
}

TEST_CASE("backlog4 powers r1: InitiateFX plays on the caster with the target object as its second object (RW 0x897FFA: doFXObj(fx, caster, target))")
{
	PowerWorld f(kWorld);
	Object *driven = f.unit("DrivenHero", 'A', 105, 305);
	Object *target = f.unit("CryHero", 'B', 205, 305);
	f.logic->fxEvents().beginFrame();
	powerOf(driven)->doSpecialPowerAtObject(target, 0);
	auto initiates = [&]() {
		std::vector<FXEvent> out;
		for (const FXEvent &e : f.logic->fxEvents().frameEvents())
		{
			if (std::string(e.site) == "SpecialPower InitiateFX")
			{
				out.push_back(e);
			}
		}
		return out;
	};
	std::vector<FXEvent> e = initiates();
	REQUIRE(e.size() == 1u);
	CHECK(e[0].kind == FXEvent::OBJECT_FX);
	CHECK(*e[0].fxList == "FX_TestInitiate");
	CHECK(e[0].primary == driven->getID());
	CHECK(e[0].secondary == target->getID()); // what a targeted FX (a ray to the target, a filter on it) needs
	// control: a cast without a target object has no second object
	f.frames(80);
	f.logic->fxEvents().beginFrame();
	powerOf(driven)->doSpecialPower(0);
	e = initiates();
	REQUIRE(e.size() == 1u);
	CHECK(e[0].secondary == INVALID_ID);
}
