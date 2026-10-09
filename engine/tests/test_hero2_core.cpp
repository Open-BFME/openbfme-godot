// OpenBFME unit tests (lane HERO-2): the hero abilities HERO-1 left open, in a synthetic world (CombatTestUtil's CombatWorld). Expected values are read from
// the RotWK binary (each check names the address). GPL-3.0.

#include "doctest.h"
#include <fstream>
#include <sstream>
#include "CombatTestUtil.h"

#include "Common/SpecialPower.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/ScriptEngine/LuaHost.h"
#include "GameLogic/ScriptEngine/LuaScriptEvents.h"

#include <set>
#include <string>
#include <vector>

namespace
{
// the special power store of a synthetic world (SpecialPower blocks parse into it)
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
};

const char kScreech[] =
	"SpecialPower SpecialAbilityTestScreech\n"
	"  Enum = SPECIAL_SCREECH\n"
	"  ReloadTime = 30000\n"
	"End\n"
	"Object ScreechHero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule PowerTag\n  SpecialPowerTemplate = SpecialAbilityTestScreech\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = SpecialAbilityUpdate AbilityTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestScreech\n"
	"  UnpackTime = 200\n  PreparationTime = 200\n  PackTime = 200\n  EffectRange = 180\n  IgnoreFacingCheck = Yes\n"
	" End\n"
	"End\n";
} // namespace

// RW 0x85491F .. 0x854977: SPECIAL_SCREECH dispatches the built-in event BeScary (slot 8) to the object through RW 0x7379CB with (the object, EffectRange)
TEST_CASE("hero2: Screech dispatches BeScary (slot 8) with the object and EffectRange through GameLogic::dispatchScriptEvent (RW 0x854968)")
{
	PowerWorld f(kScreech);
	Object *o = f.unit("ScreechHero", 'A', 300, 300);
	REQUIRE(o);
	struct Call
	{
		int slot;
		ObjectID self;
		LuaEventArgs args;
	};
	std::vector<Call> calls;
	f.logic->setScriptEventProc([&calls](int slot, Object &self, const LuaEventArgs &args) { calls.push_back(Call{ slot, self.getID(), args }); });
	auto *u = dynamic_cast<SpecialAbilityUpdate *>(o->findModule("SpecialAbilityUpdate"));
	REQUIRE(u);
	u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	f.frames(20);
	REQUIRE(u->abilitiesTriggered() == 1);
	REQUIRE(calls.size() == 1);
	CHECK(calls[0].slot == (int)LUAEVENT_BeScary);
	CHECK(calls[0].self == o->getID());
	CHECK(calls[0].args.arg[0].kind == LuaEventArg::OBJECT);
	CHECK(calls[0].args.arg[0].objectId == (int)o->getID());
	CHECK(calls[0].args.arg[1].kind == LuaEventArg::REAL);
	CHECK(calls[0].args.arg[1].real == 180.0f);
	CHECK(calls[0].args.arg[2].kind == LuaEventArg::NONE);
	CHECK(u->unported() == 0);
	f.logic->setScriptEventProc(GameLogic::ScriptEventProc());
}

// S-1220: with no ScriptEventProc installed the dispatch is counted and reported, never dropped silently
TEST_CASE("hero2: a script event with no script engine installed is counted and reported as [S-1220]")
{
	PowerWorld f(kScreech);
	Object *o = f.unit("ScreechHero", 'A', 300, 300);
	REQUIRE(o);
	auto *u = dynamic_cast<SpecialAbilityUpdate *>(o->findModule("SpecialAbilityUpdate"));
	REQUIRE(u);
	u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	f.frames(20);
	REQUIRE(u->abilitiesTriggered() == 1);
	CHECK(f.logic->scriptEventsWithoutDispatch() == 1);
	bool reported = false;
	for (const std::string &s : f.logic->report().stops)
	{
		reported = reported || s.rfind("[S-1220] script events: no ScriptEventProc (LiveScripting) was installed; 1 engine dispatches", 0) == 0;
	}
	CHECK(reported);
}

namespace
{
// a client recorder: the drawable replacements the logic asks for
struct LookRecorder : ObjectClientHooks
{
	std::vector<std::string> looks;
	void objectCreated(Object &obj) override { obj.friend_bindToClient(this); }
	void objectDestroyed(Object &) override {}
	void replaceDrawable(Object &, const ThingTemplate *tmpl, bool, std::uint32_t) override { looks.push_back(tmpl ? tmpl->getName() : "null"); }
};

const char kDisguise[] =
	"FXList FX_TestDisguiseExit\nEnd\n"
	"ModifierList TestShieldMaiden\n  Category = LEADERSHIP\n  Duration = 5000\n  Modifier = ARMOR 150%\nEnd\n"
	"SpecialPower SpecialAbilityTestDisguise\n  Enum = SPECIAL_DISGUISE\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityTestSmite\n  Enum = SPECIAL_SMITE_CANCELDISGUISE\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityTestShieldMaiden\n  Enum = SPECIAL_ATTRIBUTEMOD_CANCELDISGUISE\n  ReloadTime = 1000\nEnd\n"
	"Object TestDisguiseLook\n KindOf = INFANTRY\nEnd\n"
	"Object DisguiseHero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" WeaponSet\n  Conditions = None\n  Weapon = PRIMARY SwordWeapon\n End\n"
	" WeaponSet\n  Conditions = MOUNTED\n  Weapon = PRIMARY BowWeapon\n End\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = AttributeModifierPoolUpdate PoolTag\n End\n"
	" Behavior = SpecialPowerModule DisguisePowerTag\n  SpecialPowerTemplate = SpecialAbilityTestDisguise\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = SpecialDisguiseUpdate DisguiseTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestDisguise\n"
	"  UnpackTime = 1000\n  PreparationTime = 1\n  PersistentPrepTime = 250\n  PackTime = 1000\n  OpacityTarget = .3\n"
	"  DisguiseAsTemplate = TestDisguiseLook\n  DisguisedAsTemplate_EnemyPerspective = TestDisguiseLook\n  DisguiseFX = FX_TestDisguiseExit\n"
	"  ForceMountedWhenDisguising = Yes\n"
	" End\n"
	" Behavior = SpecialPowerModule SmitePowerTag\n  SpecialPowerTemplate = SpecialAbilityTestSmite\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = SpecialAbilityUpdate SmiteTag\n  SpecialPowerTemplate = SpecialAbilityTestSmite\n  UnpackTime = 200\n  PreparationTime = 200\n  PackTime = 200\n"
	"  IgnoreFacingCheck = Yes\n End\n"
	" Behavior = SpecialPowerModule ShieldTag\n  SpecialPowerTemplate = SpecialAbilityTestShieldMaiden\n  AttributeModifier = TestShieldMaiden\n"
	"  AttributeModifierRange = 1\n  AttributeModifierAffectsSelf = Yes\n  AttributeModifierAffects = ANY +INFANTRY\n End\n"
	"End\n";

SpecialAbilityUpdate *abilityOf(Object &o, const char *power)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		SpecialAbilityUpdate *u = dynamic_cast<SpecialAbilityUpdate *>(m.get());
		if (u && u->abilityData()->m_specialPowerTemplate && u->abilityData()->m_specialPowerTemplate->getName() == power)
		{
			return u;
		}
	}
	return nullptr;
}

SpecialPowerModule *powerOf(Object &o, const char *power)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(m.get());
		if (sp && sp->getSpecialPowerTemplate() && sp->getSpecialPowerTemplate()->getName() == power)
		{
			return sp;
		}
	}
	return nullptr;
}

bool idleNow(Object &o)
{
	return o.getAIUpdateInterface() && o.getAIUpdateInterface()->isIdle();
}
} // namespace

// RW 0x8B4760 / 0x8B45BF: the disguise sets DISGUISED (model condition 300) and, with ForceMountedWhenDisguising, the mount (MOUNTED, weapon set MOUNTED); a
// viewer that is not the local owner sees DisguiseAsTemplate's drawable; the packing frames fade the opacity (RW 0x8B4455); a second use ends it with the own
// look back
TEST_CASE("hero2: SpecialDisguiseUpdate disguises (DISGUISED, the forced mount, DisguiseAsTemplate's look, the fade) and a second use ends it")
{
	PowerWorld f(kDisguise);
	LookRecorder rec;
	f.logic->setClientHooks(&rec);
	Object *o = f.unit("DisguiseHero", 'A', 300, 300);
	REQUIRE(o);
	auto *sd = SpecialDisguiseUpdate::of(*o);
	REQUIRE(sd);
	REQUIRE(idleNow(*o));
	Player *viewer = f.teamB()->getControllingPlayer();
	REQUIRE(viewer);
	f.logic->players().setLocalPlayer(viewer); // the client watching is the enemy's
	sd->initiateIntentToDoSpecialPower(sd->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	std::vector<float> fade;
	for (int i = 0; i < 30 && sd->isActive(); ++i)
	{
		f.frames(1);
		fade.push_back(sd->opacity());
	}
	CHECK(o->testModelCondition(300));
	CHECK(o->testModelCondition(CombatNames::modelCondition("MOUNTED")));
	CHECK(o->getWeapons()->weaponInSlot(PRIMARY_WEAPON)->getTemplate()->getName() == "BowWeapon");
	REQUIRE(rec.looks.size() == 1);
	CHECK(rec.looks[0] == "TestDisguiseLook"); // RW 0x8B47CE: the local player is not the owner
	CHECK(sd->disguises() == 1);
	// UnpackTime 1000 ms = 5 frames: the opacity runs 1.0 -> 0.3 over the frames left 4 .. 1 ((0.3 - 1) * (1 - left / 5) + 1), and back over the pack
	bool sawUnpack = false, sawPack = false;
	for (float v : fade)
	{
		sawUnpack = sawUnpack || (v > 0.85f && v < 0.87f);  // left 4: 0.86
		sawPack = sawPack || (v > 0.43f && v < 0.45f);      // left 4 of the pack: (1 - 0.3) * 0.2 + 0.3 = 0.44
	}
	CHECK(sawUnpack);
	CHECK(sawPack);
	CHECK(sd->opacity() == 1.0f); // RW 0x8B4521: back to 1.0 when the ability ends
	f.frames(5);
	REQUIRE(idleNow(*o));
	sd->initiateIntentToDoSpecialPower(sd->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	f.frames(30);
	CHECK_FALSE(o->testModelCondition(300));
	REQUIRE(rec.looks.size() == 2);
	CHECK(rec.looks[1] == "DisguiseHero");
	// the owner's own client keeps the own look while disguised
	f.logic->players().setLocalPlayer(o->getControllingPlayer());
	f.frames(5);
	REQUIRE(idleNow(*o));
	sd->initiateIntentToDoSpecialPower(sd->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	f.frames(30);
	CHECK(o->testModelCondition(300));
	REQUIRE(rec.looks.size() == 3);
	CHECK(rec.looks[2] == "DisguiseHero");
	CHECK(sd->unported() == 0);
	f.logic->setClientHooks(nullptr);
}

// RW 0x853884 .. 0x8538DD: SPECIAL_SMITE_CANCELDISGUISE's unpack ends a disguise (RW 0x8B4702(0): the own look back and DisguiseFX); RW 0x8977E5 ..
// 0x897840: SPECIAL_ATTRIBUTEMOD_CANCELDISGUISE's attribute modifier power does the same
TEST_CASE("hero2: Smite (SPECIAL_SMITE_CANCELDISGUISE) and Shield Maiden (SPECIAL_ATTRIBUTEMOD_CANCELDISGUISE) end the disguise with DisguiseFX")
{
	PowerWorld f(kDisguise);
	LookRecorder rec;
	f.logic->setClientHooks(&rec);
	Object *o = f.unit("DisguiseHero", 'A', 300, 300);
	REQUIRE(o);
	auto *sd = SpecialDisguiseUpdate::of(*o);
	REQUIRE(sd);
	auto disguise = [&]() {
		REQUIRE(idleNow(*o));
		sd->initiateIntentToDoSpecialPower(sd->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
		f.frames(30);
		REQUIRE(o->testModelCondition(300));
	};
	auto fxCount = [&]() {
		const auto &per = f.logic->fxEvents().perSite();
		auto it = per.find("SpecialDisguiseUpdate");
		return it == per.end() ? 0ull : it->second;
	};
	disguise();
	SpecialAbilityUpdate *smite = abilityOf(*o, "SpecialAbilityTestSmite");
	REQUIRE(smite);
	const size_t looks0 = rec.looks.size();
	smite->initiateIntentToDoSpecialPower(smite->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	f.frames(2); // the unpack starts
	CHECK_FALSE(o->testModelCondition(300));
	REQUIRE(rec.looks.size() == looks0 + 1);
	CHECK(rec.looks.back() == "DisguiseHero");
	CHECK(fxCount() == 1);
	f.frames(30);
	disguise();
	SpecialPowerModule *shield = powerOf(*o, "SpecialAbilityTestShieldMaiden");
	REQUIRE(shield);
	shield->doSpecialPower(0);
	f.frames(1);
	CHECK_FALSE(o->testModelCondition(300));
	CHECK(rec.looks.back() == "DisguiseHero");
	CHECK(fxCount() == 2);
	f.logic->setClientHooks(nullptr);
}

namespace
{
const char kDominate[] =
	"ModifierList TestDominateBuff\n  Category = LEADERSHIP\n  Duration = 5000\n  Modifier = ARMOR 150%\nEnd\n"
	"SpecialPower SpecialAbilityTestDominate\n  Enum = SPECIAL_DOMINATE_ENEMY\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityTestConvert\n  Enum = SPECIAL_DOMINATE_ENEMY\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityTestActivate\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityTestActivatee\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 0\nEnd\n"
	"SpecialPower SpecialAbilityTestSleeper\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 0\nEnd\n"
	"Object DefectMan\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = TemporarilyDefectUpdate ModuleTag_TemporarilyDefectUpdate\n  DefectDuration = 2000\n End\n"
	"End\n"
	"Object DominateHero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule PowerTag\n  SpecialPowerTemplate = SpecialAbilityTestDominate\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = DominateEnemySpecialPower DominateTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestDominate\n  StartAbilityRange = 200.0\n  UnpackTime = 200\n  PreparationTime = 1\n  IgnoreFacingCheck = Yes\n"
	"  AttributeModifierAffects = ALL\n  DominateRadius = 60\n"
	" End\n"
	" Behavior = SpecialPowerModule ConvertPowerTag\n  SpecialPowerTemplate = SpecialAbilityTestConvert\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = DominateEnemySpecialPower ConvertTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestConvert\n  StartAbilityRange = 200.0\n  UnpackTime = 200\n  PreparationTime = 1\n  IgnoreFacingCheck = Yes\n"
	"  AttributeModifierAffects = ALL\n  DominateRadius = 80\n  PermanentlyConvert = Yes\n"
	" End\n"
	"End\n"
	"Object ActivateHero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = AttributeModifierPoolUpdate PoolTag\n End\n"
	" Behavior = SpecialPowerModule PowerTag\n  SpecialPowerTemplate = SpecialAbilityTestActivate\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = ActivateModuleSpecialPower ModuleTag_Mover\n"
	"  SpecialPowerTemplate = SpecialAbilityTestActivate\n  UnpackTime = 200\n  PreparationTime = 1\n  PackTime = 200\n  IgnoreFacingCheck = Yes\n"
	"  TriggerSpecialPower = ModuleTag_Buff OBJECTPOS\n"
	"  TriggerSpecialPower = ModuleTag_Sleeper\n"
	"  TriggerSpecialPower = ModuleTag_NoSuchModule TARGETPOS\n"
	" End\n"
	" Behavior = SpecialPowerModule ModuleTag_Buff\n  SpecialPowerTemplate = SpecialAbilityTestActivatee\n  AttributeModifier = TestDominateBuff\n"
	"  AttributeModifierRange = 50\n  AttributeModifierAffectsSelf = Yes\n  AttributeModifierAffects = ANY +INFANTRY\n End\n"
	" Behavior = SpecialAbilityUpdate ModuleTag_Sleeper\n  SpecialPowerTemplate = SpecialAbilityTestSleeper\n End\n"
	"End\n";
} // namespace

// RW 0x8D103D / 0x8D0F6C / 0x699368 / 0x8D0BF3 / 0x8D0CE0 / 0x69ABA7: a direct Dominate makes the target the hero's (TEMPORARILY_DEFECTED, the hero's team)
// for DefectDuration (2000 ms = 10 frames) through its TemporarilyDefectUpdate; then it goes back to its own team
TEST_CASE("hero2: DominateEnemySpecialPower defects a target for DefectDuration (TemporarilyDefectUpdate) and it comes back")
{
	PowerWorld f(kDominate);
	Object *hero = f.unit("DominateHero", 'A', 300, 300);
	Object *victim = f.unit("DefectMan", 'B', 340, 300);
	REQUIRE(hero);
	REQUIRE(victim);
	Player *a = hero->getControllingPlayer();
	Player *b = victim->getControllingPlayer();
	Team *bTeam = victim->getTeam();
	REQUIRE(a != b);
	auto *de = dynamic_cast<DominateEnemySpecialPower *>(abilityOf(*hero, "SpecialAbilityTestDominate"));
	REQUIRE(de);
	de->initiateIntentToDoSpecialPower(de->abilityData()->m_specialPowerTemplate, victim, nullptr, 0, 0);
	unsigned triggerFrame = 0;
	for (int i = 0; i < 30 && de->dominated() == 0; ++i)
	{
		f.frames(1);
		triggerFrame = f.logic->getFrame();
	}
	REQUIRE(de->dominated() == 1);
	const int kDefected = ObjectTemplateInfoBuilder::objectStatusIndex("TEMPORARILY_DEFECTED");
	CHECK(victim->getControllingPlayer() == a);
	CHECK(victim->testStatus((unsigned)kDefected));
	CHECK(victim->getOriginalTeam() == bTeam); // RW 0x699513 does not record the temporary team
	TemporarilyDefectUpdate *td = TemporarilyDefectUpdate::of(*victim);
	REQUIRE(td);
	CHECK(td->dominator() == hero->getID());
	CHECK(td->endFrame() >= triggerFrame);
	CHECK(td->endFrame() <= triggerFrame + 10); // DefectDuration 2000 ms = 10 frames from the trigger frame
	f.frames(25); // the update wakes every 10 frames (RW 0x8D0CE0) past the end
	CHECK(victim->getControllingPlayer() == b);
	CHECK(victim->getTeam() == bTeam);
	CHECK_FALSE(victim->testStatus((unsigned)kDefected));
	CHECK(td->endFrame() == 0);
	CHECK(td->dominator() == INVALID_ID);
	CHECK(de->unported() == 0);
}

// the area Dominate with PermanentlyConvert: a horde within DominateRadius of the location goes over for good, its members with it (HordeContain's vslot
// 0x108, RW 0x86ED25); the members themselves are not picked by the scan (RW 0xC1D6A0: not HORDE_MEMBER)
TEST_CASE("hero2: an area Dominate with PermanentlyConvert converts a horde and its members (RW 0x8D10E7 / 0x86ED25)")
{
	PowerWorld f(kDominate);
	Object *hero = f.unit("DominateHero", 'A', 300, 300);
	Object *horde = f.unit("SwordHorde", 'B', 360, 300);
	REQUIRE(hero);
	REQUIRE(horde);
	f.frames(5);
	Player *a = hero->getControllingPlayer();
	ContainModuleInterface *c = horde->getContain();
	REQUIRE(c);
	REQUIRE(c->getContainCount() == 6);
	auto *de = dynamic_cast<DominateEnemySpecialPower *>(abilityOf(*hero, "SpecialAbilityTestConvert"));
	REQUIRE(de);
	const Coord3D at = *horde->getPosition();
	de->initiateIntentToDoSpecialPower(de->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
	f.frames(20);
	CHECK(de->dominated() == 1); // the horde alone (its members are HORDE_MEMBER)
	CHECK(horde->getControllingPlayer() == a);
	const int kDefected = ObjectTemplateInfoBuilder::objectStatusIndex("TEMPORARILY_DEFECTED");
	CHECK_FALSE(horde->testStatus((unsigned)kDefected));
	for (Object *m : *c->getContainedItemsList())
	{
		CHECK(m->getControllingPlayer() == a);
	}
	CHECK(horde->getOriginalTeam() != hero->getTeam()); // RW 0x698E6F: a permanent conversion does not record the team either
}

// RW 0x8D222A / 0x8D238D / 0x8D235D / 0x8D2370: the listed update module sleeps from creation, wakes at the trigger and sleeps again at the end; the listed
// special power module runs at the object's position (OBJECTPOS) when the ability triggers; an unknown tag is no module
TEST_CASE("hero2: ActivateModuleSpecialPower wakes / sleeps its update modules and fires its power modules at the trigger")
{
	PowerWorld f(kDominate);
	Object *hero = f.unit("ActivateHero", 'A', 300, 300);
	REQUIRE(hero);
	auto *am = dynamic_cast<ActivateModuleSpecialPower *>(abilityOf(*hero, "SpecialAbilityTestActivate"));
	REQUIRE(am);
	SpecialAbilityUpdate *sleeper = abilityOf(*hero, "SpecialAbilityTestSleeper");
	REQUIRE(sleeper);
	SpecialPowerModule *buff = powerOf(*hero, "SpecialAbilityTestActivatee");
	REQUIRE(buff);
	CHECK(sleeper->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER); // onObjectCreated (RW 0x8D238D)
	const unsigned applied0 = buff->applied();
	am->initiateIntentToDoSpecialPower(am->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	bool woke = false;
	for (int i = 0; i < 30 && am->abilitiesTriggered() == 0; ++i)
	{
		f.frames(1);
	}
	REQUIRE(am->abilitiesTriggered() == 1);
	woke = sleeper->friend_getNextCallFrame() == f.logic->getFrame() + 1 || sleeper->friend_getNextCallFrame() == f.logic->getFrame();
	CHECK(woke);
	CHECK(am->activations() == 1);         // the Buff module (the sleeper is an update: woken, not fired)
	CHECK(buff->applied() > applied0);     // the attribute modifier power ran at the hero's position (it affects itself)
	f.frames(30);
	CHECK_FALSE(am->isActive());
	CHECK(sleeper->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER); // finishAbility (RW 0x8D2370)
	CHECK(f.logic->report().errors.empty());
}

namespace
{
const char kVolley[] =
	"Weapon VolleyWeapon\n  AttackRange = 400\n  DelayBetweenShots = 100\n  DamageNugget\n    Damage = 10\n    Radius = 0.0\n    DelayTime = 0\n"
	"    DamageType = SLASH\n    DeathType = NORMAL\n  End\nEnd\n"
	"ModifierList TestSpeechBonus\n  Category = LEADERSHIP\n  Duration = 5000\n  Modifier = ARMOR 150%\nEnd\n"
	"SpecialPower SpecialAbilityTestVolley\n  Enum = SPECIAL_ARROW_STORM\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityTestCurse\n  Enum = SPECIAL_CURSE_ENEMY\n  ReloadTime = 1000\n  RadiusCursorRadius = 100\nEnd\n"
	"SpecialPower SpecialAbilityTestSpeech\n  Enum = SPECIAL_ROUSING_SPEECH\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityTestSwoop\n  Enum = SPECIAL_LEVEL_ATTACK\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityTestVictimMode\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 10000\nEnd\n"
	"SpecialPower SpecialAbilityTestVictimPower\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 10000\nEnd\n"
	"Object VolleyHero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" WeaponSet\n  Conditions = None\n  Weapon = PRIMARY SwordWeapon\n End\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = AttributeModifierPoolUpdate PoolTag\n End\n"
	" Behavior = SpecialPowerModule VolleyPowerTag\n  SpecialPowerTemplate = SpecialAbilityTestVolley\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = ArrowStormUpdate VolleyTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestVolley\n  StartAbilityRange = 300\n  UnpackTime = 200\n  PreparationTime = 200\n  PersistentPrepTime = 200\n"
	"  PackTime = 200\n  IgnoreFacingCheck = Yes\n  WeaponTemplate = VolleyWeapon\n  TargetRadius = 60\n  ShotsPerTarget = 1\n  ShotsPerBurst = 2\n  MaxShots = 5\n"
	" End\n"
	" Behavior = SpecialPowerModule CursePowerTag\n  SpecialPowerTemplate = SpecialAbilityTestCurse\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = CurseSpecialPower CurseTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestCurse\n  StartAbilityRange = 300\n  UnpackTime = 200\n  PreparationTime = 1\n  IgnoreFacingCheck = Yes\n"
	"  CursePercentage = 100%\n"
	" End\n"
	" Behavior = SpecialPowerModule SpeechPowerTag\n  SpecialPowerTemplate = SpecialAbilityTestSpeech\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = RousingSpeechUpdate SpeechTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestSpeech\n  StartAbilityRange = 1.0\n  UpdateInterval = 200\n  BonusRadius = 150\n  SpeechDuration = 2000\n"
	"  CreateWave = Yes\n  WaveWidth = 50\n  ModifierName = TestSpeechBonus\n"
	" End\n"
	" Behavior = SpecialPowerModule SwoopPowerTag\n  SpecialPowerTemplate = SpecialAbilityTestSwoop\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = FellBeastSwoopPower SwoopTag\n  SpecialPowerTemplate = SpecialAbilityTestSwoop\n  UnpackTime = 1\n End\n"
	"End\n"
	"Object VictimHero\n"
	" KindOf = INFANTRY SELECTABLE HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 1000\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule VictimPowerTag\n  SpecialPowerTemplate = SpecialAbilityTestVictimPower\n End\n"
	" Behavior = WeaponModeSpecialPowerUpdate VictimModeTag\n  SpecialPowerTemplate = SpecialAbilityTestVictimMode\n  Duration = 2000\n End\n"
	"End\n"
	"Object SpeechFollower\n"
	" KindOf = INFANTRY SELECTABLE\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = AttributeModifierPoolUpdate PoolTag\n End\n"
	"End\n";
} // namespace

// RW 0x894396 / 0x894151 / 0x894100 / 0x893EC8: the volley queues the enemies within TargetRadius of the location and refills the queue to MaxShots by
// cycling; every trigger fires ShotsPerBurst temporary weapons at the queue's next targets until MaxShots shots
TEST_CASE("hero2: ArrowStormUpdate queues the enemies at the location, fires ShotsPerBurst per trigger and stops at MaxShots")
{
	PowerWorld f(kVolley);
	Object *hero = f.unit("VolleyHero", 'A', 300, 300);
	Object *e1 = f.unit("VictimHero", 'B', 380, 300);
	Object *e2 = f.unit("VictimHero", 'B', 390, 310);
	REQUIRE(hero);
	REQUIRE(e1);
	REQUIRE(e2);
	auto *as = dynamic_cast<ArrowStormUpdate *>(abilityOf(*hero, "SpecialAbilityTestVolley"));
	REQUIRE(as);
	const float h1 = e1->getBodyModule()->getHealth(), h2 = e2->getBodyModule()->getHealth();
	const Coord3D at = { 385.0f, 305.0f, 0.0f };
	as->initiateIntentToDoSpecialPower(as->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
	bool queued = false;
	for (int i = 0; i < 80 && (as->isActive() || as->shots() == 0); ++i)
	{
		f.frames(1);
		queued = queued || as->targetsQueued() == 5; // RW 0x8942A3: refilled to MaxShots
	}
	CHECK(queued);
	CHECK(as->shots() == 5);              // three triggers of ShotsPerBurst 2, the last one stops at MaxShots
	f.frames(10);
	CHECK(e1->getBodyModule()->getHealth() < h1); // 10 SLASH a shot
	CHECK(e2->getBodyModule()->getHealth() < h2);
	CHECK(as->unported() == 0);
}

// RW 0x8D134C / 0x8D12E3: Curse restarts every special power of each enemy HERO within the template's RadiusCursorRadius at CursePercentage (100%: the full
// reload) through their + 0xC slot 0xAC (RW 0x6519AF -> startPowerRecharge)
TEST_CASE("hero2: CurseSpecialPower restarts the recharge of the enemy heroes' powers in the radius")
{
	PowerWorld f(kVolley);
	Object *hero = f.unit("VolleyHero", 'A', 300, 300);
	Object *near = f.unit("VictimHero", 'B', 360, 300);
	Object *far = f.unit("VictimHero", 'B', 600, 300);
	REQUIRE(hero);
	REQUIRE(near);
	REQUIRE(far);
	f.frames(60); // the victims' powers are ready (ReloadTime 10 s = 50 frames)
	SpecialPowerModule *pn = powerOf(*near, "SpecialAbilityTestVictimPower");
	SpecialPowerModule *pf = powerOf(*far, "SpecialAbilityTestVictimPower");
	REQUIRE(pn);
	REQUIRE(pf);
	REQUIRE(pn->isReady());
	REQUIRE(pf->isReady());
	auto *cs = dynamic_cast<CurseSpecialPower *>(abilityOf(*hero, "SpecialAbilityTestCurse"));
	REQUIRE(cs);
	const Coord3D at = *near->getPosition();
	cs->initiateIntentToDoSpecialPower(cs->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
	f.frames(20);
	CHECK(cs->cursed() == 1);
	CHECK_FALSE(pn->isReady());
	CHECK(pf->isReady());
	// RW 0x68BDD0 -> RW 0x898263: the cursed hero's WeaponModeSpecialPowerUpdate (an update + special power base) restarts its recharge too
	auto modeOf = [](Object &o) {
		for (const std::unique_ptr<BehaviorModule> &m : o.modules())
		{
			if (auto *w = dynamic_cast<WeaponModeSpecialPowerUpdate *>(m.get()))
			{
				return w;
			}
		}
		return static_cast<WeaponModeSpecialPowerUpdate *>(nullptr);
	};
	REQUIRE(modeOf(*near));
	REQUIRE(modeOf(*far));
	CHECK_FALSE(modeOf(*near)->isReady());
	CHECK(modeOf(*far)->isReady());
}

// RW 0x8B068D / 0x8B0485 / 0x8B0A0D / 0x8B07EA: the speech's waves (WaveWidth 50: rings (50, 100] then (100, 150]) give ModifierName to the allies in each
// ring; a follower gets model condition 201 and the leader's id while it is in the current ring
TEST_CASE("hero2: RousingSpeechUpdate inspires the allies of each wave ring with ModifierName")
{
	PowerWorld f(kVolley);
	Object *hero = f.unit("VolleyHero", 'A', 300, 300);
	Object *inFirst = f.unit("SpeechFollower", 'A', 330, 300);  // 30: inside the skipped first ring (0, 50]
	Object *ring1 = f.unit("SpeechFollower", 'A', 380, 300);    // 80: the first trigger's ring
	Object *ring2 = f.unit("SpeechFollower", 'A', 430, 300);    // 130: the second
	Object *enemy = f.unit("SpeechFollower", 'B', 380, 310);
	REQUIRE(hero);
	REQUIRE(inFirst);
	REQUIRE(ring1);
	REQUIRE(ring2);
	REQUIRE(enemy);
	auto *rs = dynamic_cast<RousingSpeechUpdate *>(abilityOf(*hero, "SpecialAbilityTestSpeech"));
	REQUIRE(rs);
	rs->initiateIntentToDoSpecialPower(rs->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
	bool leaderSeen = false;
	for (int i = 0; i < 20; ++i)
	{
		f.frames(1);
		leaderSeen = leaderSeen || ring1->getSpeechLeader() == hero->getID();
	}
	CHECK(leaderSeen);
	CHECK(rs->followersInspired() == 2); // ring1, then ring2
	auto pool = [](Object *o) { return dynamic_cast<AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate")); };
	REQUIRE(pool(ring1));
	CHECK(pool(ring1)->hasList("TestSpeechBonus"));
	CHECK(pool(ring2)->hasList("TestSpeechBonus"));
	CHECK_FALSE(pool(inFirst)->hasList("TestSpeechBonus"));
	CHECK_FALSE(pool(enemy)->hasList("TestSpeechBonus"));
	f.frames(20);
	CHECK_FALSE(hero->testModelCondition(207)); // the speech condition clears when the speech ends
	CHECK_FALSE(ring2->testModelCondition(201));
}

// RW 0x8CB2CE / 0x8CB28B: the swoop triggers on its first update with the AI command 0x40 (GiantBirdAIUpdate's swoop: not ported, counted by the AI) and
// ends once the AI is idle
TEST_CASE("hero2: FellBeastSwoopPower gives the level attack AI command (counted: GiantBirdAIUpdate's swoop is not ported) and ends when the AI idles")
{
	PowerWorld f(kVolley);
	Object *hero = f.unit("VolleyHero", 'A', 300, 300);
	REQUIRE(hero);
	auto *sw = dynamic_cast<FellBeastSwoopPower *>(abilityOf(*hero, "SpecialAbilityTestSwoop"));
	REQUIRE(sw);
	const Coord3D at = { 500.0f, 300.0f, 0.0f };
	sw->initiateIntentToDoSpecialPower(sw->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
	f.frames(5);
	CHECK(sw->swoopCommands() == 1);
	const auto &unported = hero->getAIUpdateInterface()->unportedCommands();
	CHECK(unported.count("level attack position (AI command 0x40: GiantBirdAIUpdate vslot 0x94)") == 1);
	CHECK(sw->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
}

namespace
{
const char kTeleport[] =
	"SpecialPower SpecialAbilityTestTunnel\n  Enum = SPECIAL_BALROG_WINGS\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityTestRefresh\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityTestAllyPower\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 60000\nEnd\n"
	"Object TunnelHero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule TunnelPowerTag\n  SpecialPowerTemplate = SpecialAbilityTestTunnel\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = TeleportSpecialAbilityUpdate TunnelTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestTunnel\n  UnpackTime = 400\n  PackTime = 400\n  ApproachRequiresLOS = No\n  BusyForDuration = 1800\n  MaxDistance = 1000\n"
	" End\n"
	" Behavior = SpecialPowerTimerRefreshSpecialPower RefreshTag\n  SpecialPowerTemplate = SpecialAbilityTestRefresh\n  AttributeModifierRange = 150\n"
	"  AttributeModifierAffects = ALL\n  AttributeModifierAffectsSelf = No\n End\n"
	"End\n"
	"Object RefreshAlly\n"
	" KindOf = INFANTRY SELECTABLE HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule AllyPowerTag\n  SpecialPowerTemplate = SpecialAbilityTestAllyPower\n End\n"
	"End\n";
} // namespace

// RW 0x89653A / 0x8964A0: the tunnel holds the AI for BusyForDuration from the unpack, then places the hero at the location turned towards it
TEST_CASE("hero2: TeleportSpecialAbilityUpdate moves the hero to the location and holds its AI for BusyForDuration")
{
	PowerWorld f(kTeleport);
	Object *hero = f.unit("TunnelHero", 'A', 300, 300);
	REQUIRE(hero);
	auto *tp = dynamic_cast<TeleportSpecialAbilityUpdate *>(abilityOf(*hero, "SpecialAbilityTestTunnel"));
	REQUIRE(tp);
	const Coord3D at = { 600.0f, 300.0f, 0.0f };
	tp->initiateIntentToDoSpecialPower(tp->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
	bool held = false;
	for (int i = 0; i < 40 && tp->teleports() == 0; ++i)
	{
		f.frames(1);
		held = held || hero->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("IGNORE_AI_COMMAND"));
	}
	REQUIRE(tp->teleports() == 1);
	CHECK(held);
	CHECK(hero->getPosition()->x == 600.0f);
	CHECK(hero->getPosition()->y == 300.0f);
	CHECK(tp->unported() == 0);
}

// RW 0x8C7B26 / 0x68BDF9 / 0x896F99: the refresh power makes the special powers of the objects it reaches ready at once
TEST_CASE("hero2: SpecialPowerTimerRefreshSpecialPower makes its targets' powers ready")
{
	PowerWorld f(kTeleport);
	Object *hero = f.unit("TunnelHero", 'A', 300, 300);
	Object *ally = f.unit("RefreshAlly", 'A', 350, 300);
	REQUIRE(hero);
	REQUIRE(ally);
	SpecialPowerModule *allyPower = powerOf(*ally, "SpecialAbilityTestAllyPower");
	REQUIRE(allyPower);
	allyPower->startPowerRecharge(1.0f);
	REQUIRE_FALSE(allyPower->isReady());
	auto *refresh = dynamic_cast<SpecialPowerTimerRefreshSpecialPower *>(powerOf(*hero, "SpecialAbilityTestRefresh"));
	REQUIRE(refresh);
	refresh->doSpecialPower(0);
	f.frames(1);
	CHECK(refresh->refreshed() == 1);
	CHECK(allyPower->isReady());
}

namespace
{
const char kLanding[] =
	"SpecialPower SpecialAbilityTestLand\n  Enum = SPECIAL_TOGGLE_MOUNTED\n  ReloadTime = 1000\nEnd\n"
	"SpecialPower SpecialAbilityTestShared\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 30000\nEnd\n"
	"Object FlyingHero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 500\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule LandPowerTag\n  SpecialPowerTemplate = SpecialAbilityTestLand\n  UpdateModuleStartsAttack = Yes\n End\n"
	" Behavior = ToggleMountedSpecialAbilityUpdate LandTag\n"
	"  SpecialPowerTemplate = SpecialAbilityTestLand\n  StartAbilityRange = 50.0\n  MountedTemplate = LandedHero\n"
	"  SynchronizeTimerOnSpecialPower = SpecialAbilityTestShared\n  UnpackTime = 0\n  PreparationTime = 1\n  PackTime = 0\n  IgnoreFacingCheck = Yes\n"
	" End\n"
	" Behavior = SpecialPowerModule SharedTag\n  SpecialPowerTemplate = SpecialAbilityTestShared\n End\n"
	"End\n"
	"Object LandedHero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 500\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = SpecialPowerModule SharedTag\n  SpecialPowerTemplate = SpecialAbilityTestShared\n End\n"
	"End\n";
} // namespace

// RW 0x8B140D / 0x8B12BF / 0x8B1E9A: a landing makes MountedTemplate at the hero's place for its player, with its name, health and team, and the
// synchronised power's timer; the hero itself is destroyed after its packing
TEST_CASE("hero2: ToggleMountedSpecialAbilityUpdate's MountedTemplate replaces the hero (name, health, team, the synchronised timer)")
{
	PowerWorld f(kLanding);
	Object *hero = f.unit("FlyingHero", 'A', 300, 300);
	REQUIRE(hero);
	hero->setName("TheNazgul");
	DamageInfo hurt;
	hurt.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	hurt.m_input.m_amount = 120.0f;
	hero->attemptDamage(hurt);
	const float health = hero->getBodyModule()->getHealth();
	REQUIRE(health < 500.0f);
	SpecialPowerModule *shared = powerOf(*hero, "SpecialAbilityTestShared");
	REQUIRE(shared);
	shared->startPowerRecharge(1.0f);
	const unsigned readyAt = shared->getReadyFrame();
	auto *tm = dynamic_cast<ToggleMountedSpecialAbilityUpdate *>(abilityOf(*hero, "SpecialAbilityTestLand"));
	REQUIRE(tm);
	const ObjectID heroId = hero->getID();
	Team *team = hero->getTeam();
	const Coord3D at = { 310.0f, 300.0f, 0.0f };
	tm->initiateIntentToDoSpecialPower(tm->abilityData()->m_specialPowerTemplate, nullptr, &at, 0, 0);
	for (int i = 0; i < 20 && f.logic->findObjectByID(heroId); ++i)
	{
		f.frames(1); // the trigger, the packing and the removal can share a frame: the module goes with the hero
	}
	CHECK(f.logic->findObjectByID(heroId) == nullptr);
	ObjectID repId = INVALID_ID;
	for (Object *o = f.logic->getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getProducerID() == heroId) // Construction::buildObjectNow: the builder is the producer
		{
			repId = o->getID();
		}
	}
	REQUIRE(repId != INVALID_ID);
	Object *rep = f.logic->findObjectByID(repId);
	REQUIRE(rep);
	CHECK(rep->getTemplate()->getName() == "LandedHero");
	CHECK(rep->getName() == "TheNazgul");
	CHECK(rep->getTeam() == team);
	CHECK(rep->getBodyModule()->getHealth() == health);
	SpecialPowerModule *repShared = powerOf(*rep, "SpecialAbilityTestShared");
	REQUIRE(repShared);
	CHECK(repShared->getReadyFrame() == readyAt);
}

// lane HERO-2: RW 0x894467 / 0xC05230: ModelConditionSpecialAbilityUpdate's own five rows (WhichSpecialPower 1 and EmotionPulseRadius 50 by default); a
// row of SummonReplacementSpecialAbilityUpdate's table (RW 0xC05AF8: OpacityTarget ...) is not one of its fields
TEST_CASE("hero2: ModelConditionSpecialAbilityUpdate's data is RW 0xC05230 (WhichSpecialPower 1, EmotionPulseRadius 50)")
{
	PowerWorld f(kScreech);
	REQUIRE(f.w.load("SpecialPower SpecialAbilityTestHorn\n  Enum = SPECIAL_GENERAL_TARGETLESS\n  ReloadTime = 1000\nEnd\n"
					 "Object HornHero\n KindOf = INFANTRY SELECTABLE HERO\n Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
					 " Behavior = AIUpdateInterface AiTag\n End\n"
					 " Behavior = ModelConditionSpecialAbilityUpdate HornTag\n  SpecialPowerTemplate = SpecialAbilityTestHorn\n End\nEnd\n")
				.empty());
	Object *o = f.unit("HornHero", 'A', 300, 300);
	REQUIRE(o);
	SpecialAbilityUpdate *u = abilityOf(*o, "SpecialAbilityTestHorn");
	REQUIRE(u);
	const auto *d = dynamic_cast<const ModelConditionSpecialAbilityUpdateModuleData *>(u->abilityData());
	REQUIRE(d);
	CHECK(d->m_whichSpecialPower == 1);
	CHECK(d->m_emotionPulseRadius == 50.0f);
	const std::string err = f.w.load("SpecialPower SpecialAbilityTestHorn2\n  Enum = SPECIAL_GENERAL_TARGETLESS\nEnd\n"
									 "Object HornHero2\n Behavior = ModelConditionSpecialAbilityUpdate HornTag\n  SpecialPowerTemplate = SpecialAbilityTestHorn2\n"
									 "  OpacityTarget = 0.5\n End\nEnd\n");
	CHECK_FALSE(err.empty());
}

// lane HERO-2: RW 0x663E32 (the AI update's first call): before frame 2 only the snapshot; afterwards a change of the flags reaches the proc with the old
// snapshot, which then takes the flags
TEST_CASE("hero2: GameLogic::scriptModelConditionEvents hands a model condition change to the proc once (RW 0x663E32)")
{
	PowerWorld f(kScreech);
	Object *o = f.unit("ScreechHero", 'A', 300, 300);
	REQUIRE(o);
	int calls = 0;
	bool hadBit = true;
	f.logic->setModelConditionEventProc([&](Object &, const std::array<std::uint32_t, 19> &before) {
		++calls;
		hadBit = ((before[211 >> 5] >> (211 & 31)) & 1u) != 0;
	});
	f.frames(3);
	const int calls0 = calls;
	o->setModelConditionState(211, true); // SPECIAL_POWER_1
	f.frames(2);
	CHECK(calls == calls0 + 1);
	CHECK_FALSE(hadBit); // the proc saw the snapshot before the change
	f.logic->setModelConditionEventProc(GameLogic::ModelConditionEventProc());
}

// the lane's stops are reported and registered
TEST_CASE("hero2 stops: S-1221 .. are reported and have a row in docs/STOPS.md")
{
	const std::vector<std::string> lines = SpecialAbilityModules::hero2StopLines();
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE(in.good());
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string docs = ss.str();
	REQUIRE(lines.size() == 7);
	std::set<std::string> ids;
	for (const std::string &l : lines)
	{
		REQUIRE(l.size() > 8);
		REQUIRE(l[0] == '[');
		ids.insert(l.substr(1, l.find(']') - 1));
	}
	for (int i : { 1221, 1222, 1223, 1224, 1225, 1227, 1228 }) // S-1226 (Create-a-Hero) is GameLogic's: test_create_a_hero.cpp
	{
		const std::string id = "S-" + std::to_string(i);
		CHECK_MESSAGE(ids.count(id) == 1, id << " is not reported");
		CHECK_MESSAGE(docs.find("| " + id + " |") != std::string::npos, id << " has no row in docs/STOPS.md");
	}
	CHECK(docs.find("| S-1220 |") != std::string::npos);
}

namespace
{
const char kShareExperience[] =
	"Object SharingHero\n"
	" KindOf = INFANTRY SELECTABLE CAN_ATTACK HERO\n"
	" Body = ActiveBody BodyTag\n  MaxHealth = 100\n End\n"
	" Behavior = AIUpdateInterface AiTag\n End\n"
	" Behavior = ShareExperienceBehavior ShareTag\n  Radius = 100.0\n  DropOff = 1.0\n  Percentage = 0.5\n  ObjectFilter = ANY +HERO\n End\n"
	"End\n";
} // namespace

// RW 0x8832F0 / 0x88325B: an allied HERO within Radius gets the shared gain * Percentage * (1 - distance / Radius); the value is not restored between candidates
TEST_CASE("hero2: ShareExperienceBehavior shares a gain with allied heroes in range, scaled by Percentage and the drop off (RW 0x8832F0)")
{
	PowerWorld f(kShareExperience);
	Object *giver = f.unit("SharingHero", 'A', 1000, 1000);
	Object *near1 = f.unit("SharingHero", 'A', 1050, 1000); // 50 away: factor 0.5
	Object *near2 = f.unit("SharingHero", 'A', 1000, 1050);
	Object *far = f.unit("SharingHero", 'A', 1200, 1000);   // out of range
	Object *enemy = f.unit("SharingHero", 'B', 1000, 950);  // not an ally
	REQUIRE(giver);
	REQUIRE(near1);
	REQUIRE(near2);
	ShareExperienceBehavior *s = ShareExperienceBehavior::of(*giver);
	REQUIRE(s);
	auto xp = [](Object *o) { return o->getExperienceTracker()->getExperience(); };
	const float a0 = xp(near1), b0 = xp(near2), f0 = xp(far), e0 = xp(enemy), g0 = xp(giver);
	s->share(100.0f);
	std::multiset<float> gains = { xp(near1) - a0, xp(near2) - b0 };
	// the first candidate: 100 * 0.5 * 0.5 = 25; the second shares the first's share: 25 * 0.5 * 0.5 = 6.25
	CHECK(gains == std::multiset<float>{ 6.25f, 25.0f });
	CHECK(xp(far) == f0);
	CHECK(xp(enemy) == e0);
	CHECK(xp(giver) == g0); // not itself (RW 0xC1D660)
	CHECK(s->shares() == 2);
}
