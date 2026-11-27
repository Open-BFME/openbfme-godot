// OpenBFME unit tests. LOCAL-75 (issue fork issue 537, retail P-142 & P-145):
//   * P-145: ActionManager::canDoSpecialPowerAtObject (RW 0x82D925) is called unconditionally from
//     CommandTranslator::evaluateGUISpecialPower (RW 0x6A4260 -> 0x6A437A) and SpellCommands::castWithGroup
//     (RW 0x77097E -> 0x770B67 / 0x770BD3). RotWK checks SpecialPowerTemplate::m_objectFilter (RW 0x762977 /
//     0x7640C1 at 0x82D996..0x82D9D1) even when Flags omits NEEDS_OBJECT_FILTER, so Heldenkiller* (Elladan /
//     Elrohir), WeisheitderGelehrten, and HerzdesFeuers reject non-matching targets on both client and logic.
//   * P-142: Imladris library units (ImladrisLoremaster Tomes Ulmo/Yavanna/Aule — never Manve fork issue 527,
//     ImladrisLichtaltar, BruchtalGelehrter, BruchtalLichtbringerErde/Wasser/Feuer/Luft + hordes + combo hordes).
//
// GPL-3.0.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "HudTestUtil.h"

#include "Common/GameEdition.h"
#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/Upgrade.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/HudInput.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/ActionManager.h"
#include "GameClient/InGameHud.h"
#include "GameClient/InGameUI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SpellCommands.h"
#include "GameLogic/System/ShroudManager.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{

const char kLocal75SynthData[] =
	"SpecialPower L75HeroKillerPower\n"
	"  Enum = SPECIAL_AT_VISIBLE_OBJECT\n"
	"  ReloadTime = 1000\n"
	"  ObjectFilter = ANY +HERO ENEMIES\n"
	"End\n"
	"SpecialPower L75TwinFilterPower\n"
	"  Enum = SPECIAL_AT_VISIBLE_OBJECT\n"
	"  ReloadTime = 1000\n"
	"  ObjectFilter = NONE +HERO -L75ExcludedBoss ENEMIES\n"
	"End\n"
	"SpecialPower L75GroundedPower\n"
	"  Enum = SPECIAL_AT_VISIBLE_GROUNDED_OBJECT\n"
	"  ReloadTime = 1000\n"
	"  ObjectFilter = ANY +STRUCTURE ENEMIES\n"
	"End\n"
	"SpecialPower L75AllTargetPower\n"
	"  Enum = SPECIAL_AT_VISIBLE_OBJECT\n"
	"  ReloadTime = 1000\n"
	"  ObjectFilter = ALL ENEMIES\n"
	"End\n"
	"Weapon L75KillerWeapon\n"
	"  RadiusDamageAffects = ENEMIES\n"
	"  AttackRange = 300.0\n"
	"  MeleeWeapon = Yes\n"
	"  DelayBetweenShots = 200\n"
	"  PreAttackDelay = 0\n"
	"  PreAttackType = PER_SHOT\n"
	"  FiringDuration = 100\n"
	"  DamageNugget\n"
	"    Damage = 100\n"
	"    Radius = 5.0\n"
	"    DelayTime = 0\n"
	"    DamageType = HERO\n"
	"    DeathType = NORMAL\n"
	"  End\n"
	"End\n"
	"CommandButton Command_L75HeroKiller\n"
	"  Command = SPECIAL_POWER\n"
	"  SpecialPower = L75HeroKillerPower\n"
	"  Options = NEED_TARGET_ENEMY_OBJECT\n"
	"  CursorName = EvilAbilityObj\n"
	"  InvalidCursorName = GenericInvalid\n"
	"End\n"
	"CommandButton Command_L75TwinFilter\n"
	"  Command = SPECIAL_POWER\n"
	"  SpecialPower = L75TwinFilterPower\n"
	"  Options = NEED_TARGET_ENEMY_OBJECT\n"
	"  CursorName = EvilAbilityObj\n"
	"  InvalidCursorName = GenericInvalid\n"
	"End\n"
	"CommandButton Command_L75Grounded\n"
	"  Command = SPECIAL_POWER\n"
	"  SpecialPower = L75GroundedPower\n"
	"  Options = NEED_TARGET_ENEMY_OBJECT\n"
	"  CursorName = Bombard\n"
	"  InvalidCursorName = GenericInvalid\n"
	"End\n"
	"CommandButton Command_L75NoShrub\n"
	"  Command = SPECIAL_POWER\n"
	"  SpecialPower = L75AllTargetPower\n"
	"  Options = NEED_TARGET_ENEMY_OBJECT\n"
	"  CursorName = EvilAbilityObj\n"
	"  InvalidCursorName = GenericInvalid\n"
	"End\n"
	"CommandButton Command_L75ShrubOk\n"
	"  Command = SPECIAL_POWER\n"
	"  SpecialPower = L75AllTargetPower\n"
	"  Options = NEED_TARGET_ENEMY_OBJECT ALLOW_SHRUBBERY_TARGET\n"
	"  CursorName = EvilAbilityObj\n"
	"  InvalidCursorName = GenericInvalid\n"
	"End\n"
	"CommandButton Command_L75RockOk\n"
	"  Command = SPECIAL_POWER\n"
	"  SpecialPower = L75AllTargetPower\n"
	"  Options = NEED_TARGET_ENEMY_OBJECT ALLOW_ROCK_TARGET\n"
	"  CursorName = EvilAbilityObj\n"
	"  InvalidCursorName = GenericInvalid\n"
	"End\n"
	"CommandSet L75CasterSet\n"
	"  1 = Command_L75HeroKiller\n"
	"  2 = Command_L75TwinFilter\n"
	"  3 = Command_L75Grounded\n"
	"  4 = Command_L75NoShrub\n"
	"  5 = Command_L75ShrubOk\n"
	"  6 = Command_L75RockOk\n"
	"End\n"
	"Object L75Caster\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  CommandSet = L75CasterSet\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 6\n"
	"  GeometryHeight = 16\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"  LocomotorSet\n"
	"    Locomotor = WalkerLoco\n"
	"    Condition = SET_NORMAL\n"
	"    Speed = 60\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_HK\n"
	"    SpecialPowerTemplate = L75HeroKillerPower\n"
	"    StartsPaused = No\n"
	"    UpdateModuleStartsAttack = Yes\n"
	"  End\n"
	"  Behavior = WeaponFireSpecialAbilityUpdate ModuleTag_HKUpd\n"
	"    SpecialPowerTemplate = L75HeroKillerPower\n"
	"    SpecialWeapon = L75KillerWeapon\n"
	"    StartAbilityRange = 300.0\n"
	"    UnpackTime = 0\n"
	"    PreparationTime = 0\n"
	"    PackTime = 0\n"
	"    WhichSpecialWeapon = 1\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_TF\n"
	"    SpecialPowerTemplate = L75TwinFilterPower\n"
	"    StartsPaused = No\n"
	"    UpdateModuleStartsAttack = Yes\n"
	"  End\n"
	"  Behavior = WeaponFireSpecialAbilityUpdate ModuleTag_TFUpd\n"
	"    SpecialPowerTemplate = L75TwinFilterPower\n"
	"    SpecialWeapon = L75KillerWeapon\n"
	"    StartAbilityRange = 300.0\n"
	"    UnpackTime = 0\n"
	"    PreparationTime = 0\n"
	"    PackTime = 0\n"
	"    WhichSpecialWeapon = 2\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_GR\n"
	"    SpecialPowerTemplate = L75GroundedPower\n"
	"    StartsPaused = No\n"
	"    UpdateModuleStartsAttack = Yes\n"
	"  End\n"
	"  Behavior = WeaponFireSpecialAbilityUpdate ModuleTag_GRUpd\n"
	"    SpecialPowerTemplate = L75GroundedPower\n"
	"    SpecialWeapon = L75KillerWeapon\n"
	"    StartAbilityRange = 300.0\n"
	"    UnpackTime = 0\n"
	"    PreparationTime = 0\n"
	"    PackTime = 0\n"
	"    WhichSpecialWeapon = 3\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_AT\n"
	"    SpecialPowerTemplate = L75AllTargetPower\n"
	"    StartsPaused = No\n"
	"  End\n"
	"End\n"
	"Object L75Soldier\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 6\n"
	"  GeometryHeight = 16\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object L75EnemyHero\n"
	"  KindOf = INFANTRY HERO SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 6\n"
	"  GeometryHeight = 16\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object L75ExcludedBoss\n"
	"  KindOf = MONSTER HERO SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object L75Structure\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 10\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object L75Shrubbery\n"
	"  KindOf = SHRUBBERY IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 6\n"
	"  GeometryHeight = 16\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n"
	"Object L75Rock\n"
	"  KindOf = ROCK IMMOBILE\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 6\n"
	"  GeometryHeight = 16\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"End\n";

struct Local75SynthWorld : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	CommandStore commandsStore;
	CommandStore *savedCommands = TheCommandStore;
	SpellCommands spellCommands;

	Local75SynthWorld()
	{
		TheSpecialPowerStore = &powers;
		TheCommandStore = &commandsStore;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandButton", [](INI *ini) { CommandStore::parseCommandButtonDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("CommandSet", [](INI *ini) { CommandStore::parseCommandSetDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		spellCommands.registerHandlers(dispatcher);
		combat().setAutoAcquireEnabled(false);
		const std::string err = w.load(kLocal75SynthData, INI_LOAD_OVERWRITE, "local75.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}

	Object *makeAt(const char *tpl, Team *team, const Coord3D &pos)
	{
		Object *o = make(tpl, team);
		if (o) { Coord3D p = pos; o->setPosition(&p); }
		return o;
	}
	void step(int n = 1) { for (int i = 0; i < n; ++i) { logic->runLogicFrame(); } }
	~Local75SynthWorld()
	{
		TheCommandStore = savedCommands;
		TheSpecialPowerStore = savedPowers;
	}
};

struct Local75Hud
{
	hudtest::Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;

	explicit Local75Hud(hudtest::SharedWorld &s, const char *faction = "FactionImladris")
		: rig(s, "map mp fall back 8p", faction)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string err;
		REQUIRE_MESSAGE(hud->boot(&err), err);
		rig.game->shroud().revealMapForPlayerPermanently(rig.index());
	}
	~Local75Hud() { hud.reset(); }
	void update() { hud->update(0.034); }
	void step(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			update();
			rig.game->advance(0.2);
		}
	}
	ControlBarButton select(Object &o, const char *buttonName)
	{
		hud->input().ui().deselectAll(false);
		hud->input().ui().selectObject(o.getID());
		GameMessage grp(MSG_CREATE_SELECTED_GROUP, rig.index());
		grp.appendBooleanArgument(true);
		grp.appendObjectIDArgument(o.getID());
		rig.game->dispatch().dispatch(grp);
		update();
		for (const auto *list : { &hud->controlBar().palantirButtons(), &hud->controlBar().sideButtons(), &hud->controlBar().offBarButtons() })
		{
			for (const auto &b : *list)
			{
				if (b.button && b.button->m_name == buttonName)
				{
					return b;
				}
			}
		}
		return {};
	}
	ICoord2D aim(const Coord3D &where)
	{
		rig.lookAt(where);
		const ICoord2D px = rig.screenOf(where);
		hud->mouseMove(px.x - 1, px.y);
		update();
		hud->mouseMove(px.x, px.y);
		update();
		return px;
	}
	void leftClick(const ICoord2D &p)
	{
		hud->mouseButton(HudInput::Button::Left, true, p.x, p.y, 0, rig.timeMs += 500);
		update();
		hud->mouseButton(HudInput::Button::Left, false, p.x, p.y, 0, rig.timeMs += 40);
		update();
	}
};

void sendDirectAtObject(GameLogicDispatch &dispatch, int playerIndex, const SpecialPowerTemplate *sp, Object *target, unsigned options)
{
	REQUIRE(sp != nullptr);
	REQUIRE(target != nullptr);
	GameMessage msg(MSG_DO_SPECIAL_POWER_AT_OBJECT, playerIndex);
	msg.appendIntegerArgument((int)sp->getID());
	msg.appendObjectIDArgument(target->getID());
	msg.appendIntegerArgument((int)options);
	msg.appendObjectIDArgument(INVALID_ID);
	msg.appendLocationArgument(*target->getPosition());
	dispatch.dispatch(msg);
}

bool hasAttrList(const Object &o, const char *name)
{
	const auto *pool = dynamic_cast<const AttributeModifierPool *>(o.attributeModifierPoolModule());
	return pool && pool->hasList(name);
}

ICoord2D aimAtTarget(Local75Hud &h, const CommandButton &button, Object &target)
{
	for (float dz : { 9.0f, 6.0f, 12.0f, 3.0f, 15.0f })
	{
		const Coord3D where{ target.getPosition()->x, target.getPosition()->y, target.getPosition()->z + dz };
		const ICoord2D px = h.aim(where);
		if (HudObjects::pickForGuiCommand(h.hud->input().context(), px, button) == &target)
		{
			return px;
		}
	}
	return h.aim({ target.getPosition()->x, target.getPosition()->y, target.getPosition()->z + 9.0f });
}

bool pressTargetedExpectRejected(Local75Hud &h, Object &caster, const char *buttonName, Object &target)
{
	const ControlBarButton b = h.select(caster, buttonName);
	REQUIRE_MESSAGE(b.button != nullptr, buttonName);
	REQUIRE_MESSAGE(b.state == ButtonState::Enabled, buttonName);
	REQUIRE(h.hud->controlBar().pressButton(b.slot, b.inPalantir));
	REQUIRE(h.hud->input().ui().getGUICommand() == b.button);
	const unsigned long long castsBefore = h.rig.game->spellCommands().casts();
	const ICoord2D px = aimAtTarget(h, *b.button, target);
	if (HudObjects::pickForGuiCommand(h.hud->input().context(), px, *b.button) != &target)
	{
		h.hud->input().ui().setGUICommand(nullptr);
		CHECK(HudObjects::pickForGuiCommand(h.hud->input().context(), px, *b.button) == &target);
		return false;
	}
	const bool invalidCursor = h.hud->input().ui().cursor() == MouseCursorName::GenericInvalid;
	h.leftClick(px);
	h.step(2);
	const bool stillPending = h.hud->input().ui().getGUICommand() == b.button;
	const bool noCast = h.rig.game->spellCommands().casts() == castsBefore;
	h.hud->input().ui().setGUICommand(nullptr);
	return invalidCursor && stillPending && noCast;
}

bool pressTargetedExpectAccepted(Local75Hud &h, Object &caster, const char *buttonName, Object &target)
{
	const ControlBarButton b = h.select(caster, buttonName);
	REQUIRE_MESSAGE(b.button != nullptr, buttonName);
	REQUIRE_MESSAGE(b.state == ButtonState::Enabled, buttonName);
	REQUIRE(h.hud->controlBar().pressButton(b.slot, b.inPalantir));
	REQUIRE(h.hud->input().ui().getGUICommand() == b.button);
	const unsigned long long castsBefore = h.rig.game->spellCommands().casts();
	const ICoord2D px = aimAtTarget(h, *b.button, target);
	if (HudObjects::pickForGuiCommand(h.hud->input().context(), px, *b.button) != &target)
	{
		h.hud->input().ui().setGUICommand(nullptr);
		CHECK(HudObjects::pickForGuiCommand(h.hud->input().context(), px, *b.button) == &target);
		return false;
	}
	const bool notInvalidCursor = h.hud->input().ui().cursor() != MouseCursorName::GenericInvalid;
	h.leftClick(px);
	h.step(2);
	const bool cleared = h.hud->input().ui().getGUICommand() == nullptr;
	const bool casted = h.rig.game->spellCommands().casts() > castsBefore;
	return notInvalidCursor && cleared && casted;
}

} // namespace

TEST_CASE("LOCAL-75 P-145 synthetic: canDoSpecialPowerAtObject ObjectFilter blocks non-heroes at client targeting and SpellCommands (RW 0x6A437A, 0x770B67, 0x82D925)")
{
	Local75SynthWorld w;
	Player *alice = w.playerOf('A');
	Player *bob = w.playerOf('B');
	w.players.setLocalPlayer(alice);

	Object *caster = w.makeAt("L75Caster", alice->getDefaultTeam(), { 400.0f, 400.0f, 0.0f });
	Object *soldier = w.makeAt("L75Soldier", bob->getDefaultTeam(), { 460.0f, 400.0f, 0.0f });
	Object *boss = w.makeAt("L75ExcludedBoss", bob->getDefaultTeam(), { 400.0f, 460.0f, 0.0f });
	Object *hero = w.makeAt("L75EnemyHero", bob->getDefaultTeam(), { 460.0f, 460.0f, 0.0f });
	Object *building = w.makeAt("L75Structure", bob->getDefaultTeam(), { 340.0f, 400.0f, 0.0f });
	REQUIRE(caster);
	REQUIRE(soldier);
	REQUIRE(boss);
	REQUIRE(hero);
	REQUIRE(building);
	Object *shrub = w.makeAt("L75Shrubbery", bob->getDefaultTeam(), { 340.0f, 460.0f, 0.0f });
	Object *rock = w.makeAt("L75Rock", bob->getDefaultTeam(), { 340.0f, 340.0f, 0.0f });
	REQUIRE(shrub);
	REQUIRE(rock);

	alice->selection() = { caster->getID() };
	w.step(2);

	const CommandButton *cbHK = w.commandsStore.findCommandButton("Command_L75HeroKiller");
	const CommandButton *cbTF = w.commandsStore.findCommandButton("Command_L75TwinFilter");
	const CommandButton *cbGR = w.commandsStore.findCommandButton("Command_L75Grounded");
	const CommandButton *cbNoShrub = w.commandsStore.findCommandButton("Command_L75NoShrub");
	const CommandButton *cbShrubOk = w.commandsStore.findCommandButton("Command_L75ShrubOk");
	const CommandButton *cbRockOk = w.commandsStore.findCommandButton("Command_L75RockOk");
	const SpecialPowerTemplate *spHK = w.powers.findSpecialPowerTemplate("L75HeroKillerPower");
	const SpecialPowerTemplate *spTF = w.powers.findSpecialPowerTemplate("L75TwinFilterPower");
	const SpecialPowerTemplate *spGR = w.powers.findSpecialPowerTemplate("L75GroundedPower");
	REQUIRE(cbHK);
	REQUIRE(cbTF);
	REQUIRE(cbGR);
	REQUIRE(cbNoShrub);
	REQUIRE(cbShrubOk);
	REQUIRE(cbRockOk);
	REQUIRE(spHK);
	REQUIRE(spTF);
	REQUIRE(spGR);

	// 1. Direct logic path (SpellCommands::castWithGroup -> RW 0x770B67 / 0x770BD3 -> 0x82D925):
	//    Non-hero target and excluded target must be refused even without NEEDS_OBJECT_FILTER.
	sendDirectAtObject(w.dispatcher, alice->getPlayerIndex(), spHK, soldier, cbHK->m_options);
	CHECK(w.spellCommands.refused() == 1);
	CHECK(w.spellCommands.casts() == 0);
	CHECK(soldier->getBodyModule()->getHealth() == 500.0f);

	sendDirectAtObject(w.dispatcher, alice->getPlayerIndex(), spTF, soldier, cbTF->m_options);
	CHECK(w.spellCommands.refused() == 2);
	sendDirectAtObject(w.dispatcher, alice->getPlayerIndex(), spTF, boss, cbTF->m_options);
	CHECK(w.spellCommands.refused() == 3);
	CHECK(w.spellCommands.casts() == 0);

	// Grounded sibling: airborne target refused, grounded structure accepted.
	const int airborneIdx = ObjectTemplateInfoBuilder::objectStatusIndex("AIRBORNE_TARGET");
	REQUIRE(airborneIdx >= 0); // tolerance: status bit index is assigned dynamically from loaded INI names
	building->setStatus((unsigned)airborneIdx, true);
	sendDirectAtObject(w.dispatcher, alice->getPlayerIndex(), spGR, building, cbGR->m_options);
	CHECK(w.spellCommands.refused() == 4);
	CHECK(w.spellCommands.casts() == 0);
	building->setStatus((unsigned)airborneIdx, false);

	// 2. Client HUD targeting path (CommandTranslator::evaluateGUISpecialPower -> RW 0x6A437A -> 0x82D925):
	PinholeView view({ 400.0f, 0.0f, 500.0f }, { 400.0f, 400.0f, 0.0f }, 1024, 768, 0.7f);
	MouseSettings mouse;
	MetaMap meta;
	HudInput hud(*w.logic, w.ai.get(), view, w.commands, mouse, meta);
	hud.ui().mirrorSelection({ caster->getID() });

	auto tryClick = [&](const CommandButton *cb, Object &target, bool expectAccept) {
		hud.ui().setGUICommand(cb);
		ICoord2D px{};
		REQUIRE(view.worldToScreen(*target.getPosition(), px));
		hud.update();
		hud.mouseMove(px.x, px.y, 0);
		hud.update();
		if (expectAccept)
		{
			CHECK(hud.ui().cursor() != MouseCursorName::GenericInvalid);
		}
		else
		{
			CHECK(hud.ui().cursor() == MouseCursorName::GenericInvalid);
		}
		const size_t msgsBefore = w.commands.messages().size();
		hud.mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, 1000);
		hud.mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, 1040);
		hud.flush();
		if (expectAccept)
		{
			CHECK(w.commands.messages().size() == msgsBefore + 1);
			CHECK(hud.ui().getGUICommand() == nullptr);
		}
		else
		{
			CHECK(w.commands.messages().size() == msgsBefore);
			CHECK(hud.ui().getGUICommand() == cb);
		}
		hud.ui().setGUICommand(nullptr);
	};

	// Invalid targets refused on client HUD cursor + zero messages sent
	tryClick(cbHK, *soldier, false);
	tryClick(cbTF, *soldier, false);
	tryClick(cbTF, *boss, false);
	building->setStatus((unsigned)airborneIdx, true);
	tryClick(cbGR, *building, false);
	building->setStatus((unsigned)airborneIdx, false);
	tryClick(cbGR, *building, true);

	// Valid target accepted on client HUD cursor + emitted message dispatches cleanly in logic
	const size_t msgsBeforeCast = w.commands.messages().size();
	tryClick(cbHK, *hero, true);
	REQUIRE(w.commands.messages().size() == msgsBeforeCast + 1);
	w.step(5);
	CHECK(w.spellCommands.casts() == 2);
	CHECK(hero->getBodyModule()->getHealth() == 400.0f);
}

