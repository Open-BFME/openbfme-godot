// OpenBFME retail tests for lane FACT-A: every hero and unit ability (a SPECIAL_POWER / FIRE_WEAPON / TOGGLE_* button of the object's command set) of Men,
// Elves and Dwarves used as a player uses it: the hero is raised to its top level (ExperienceTracker::gainLevels, RW 0x79D7E5) so every ability its level
// gates is open, the button is pressed on the control bar, a button that needs a target gets a click on an enemy horde, an own unit or the ground, and the
// power must then run: its button goes NOT READY (the recharge of the special power module) or the object's state shows the ability. A press the control bar
// cannot execute, a message the logic leaves unhandled, or a button that stays enabled and unused after the press is a problem. Then the effect: nobody fights
// on his own (hold fire), the hero and an allied horde carry a wound, and the frames after the press must show what the power does in retail (its module,
// weapon or OCL read from the data, expectedEffects): damage to the enemy horde, a heal, a buff or debuff (attribute modifier lists), fear, a summon, levels
// for the ally, a refreshed power, a capture, the object's own state.
// SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"
#include "FactATestUtil.h"
#include "FactAEffects.h"

#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "Common/ModelState.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/WeaponState.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/SpellCommands.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/System/EmotionSystem.h"
#include "GameLogic/Damage.h"
#include "Common/PlayerList.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace facta;

namespace
{
struct Outcome
{
	std::vector<std::string> problems;
	size_t pressed = 0, fired = 0, objects = 0, stopped = 0;
	std::map<std::string, std::string> effects; // tag -> the effect letters (Effects)
};

// what each ability must do (Effects letters, all of them), by its button: the retail data of the power (its module, its weapon, its OCL) read for each.
// "" is a fire order on the ground (the slot's shot is checked by the press loop) or a known stop
const std::map<std::string, std::string> &expectedEffects()
{
	static const std::map<std::string, std::string> e = {
		// fire orders on the ground (the slot's weapon fires: checked in the press loop)
		{ "Command_ArcherBombard", "" }, { "Command_Bombard", "" },
		// toggles and riding: the object's state (weapon set flag, MOUNTED, NO_AUTO_ACQUIRE, stealth, disguise)
		{ "Command_ToggleTrebAutoAcquire", "T" }, { "Command_ToggleElvenWarriorWeapon", "T" }, { "Command_ToggleFaramirWeapon", "T" }, { "Command_ToggleHaldirWeapon", "T" },
		{ "Command_ToggleRohirrimWeapon", "T" }, { "Command_ToggleTreebeardRockThrow", "T" }, { "Command_ToggleMounted", "T" }, { "Command_TheodenToggleMounted", "T" },
		{ "Command_MountHorseArwen", "T" }, { "Command_MountHorseEomer", "T" }, { "Command_MountHorseEowyn", "T" }, { "Command_MountHorseGlorfindel", "T" },
		{ "Command_GandalfShadowfax", "T" }, { "Command_Disguise", "T" }, { "Command_SpecialAbilityMoveUnseen", "T" }, { "Command_SpecialAbilityElfCloak", "T" },
		{ "Command_SpecialAbilityElfCloakThranduil", "T" },
		// heals (the wounded hero or ally gains health)
		{ "Command_SpecialAbilityAthelas", "H" }, { "Command_ArwenElvenGrace", "H" }, { "Command_SpecialAbilityElrondElvenGrace", "H" },
		// damage to the enemy horde
		{ "Command_BlackArrows", "D" }, { "Command_SpecialAbilityDaleBeastSlayerArrow", "D" }, { "Command_SpecialAbilityDaleSlamArrow", "D" },
		{ "Command_SpecialAbilityDeadEye", "D" }, { "Command_SpecialAbilityThornofVengeance", "D" }, { "Command_SpecialAbilityWoundArrow", "D" },
		{ "Command_LegolasHawkStrike", "D" }, { "Command_HaldirGoldenArrow", "D" }, { "Command_SpecialAbilityArrowStorm", "D" }, { "Command_RohanEomerSpear", "D" },
		{ "Command_GimliAxeThrow", "D" }, { "Command_ZealotAxeThrow", "D" }, { "Command_EowynSmite", "D" }, { "Command_SpecialAbilityWordOfPower", "DK" },
		{ "Command_SpecialAbilityWizardBlast", "D" }, { "Command_GondorGandalfIstariLight", "D" }, { "Command_GondorGandalfLightningSword", "D" },
		{ "Command_SpecialAbilityDwarvenGloinSlam", "D" }, { "Command_SpecialAbilityDwarvenGloinShakeFoundation", "D" }, { "Command_SpecialAbilityGloinSmash", "D" },
		{ "Command_ChargeofGlory", "D" }, { "Command_DwarvenGuardianChargeAttack", "D" }, { "Command_SpecialAbilityTheodenGloriousCharge", "BD" },
		{ "Command_SpecialAbilityGlorfindelWindRider", "BD" }, { "Command_RohanGimliLeap", "D" }, { "Command_MenLongShotAbility", "D" },
		{ "Command_FireWeaponDwarvenFortressOilCasks", "D" }, { "Command_FireWeaponMenFortressBoilingOil", "D" }, { "Command_FireWeaponMightyCatapult", "D" },
		{ "Command_SpecialAbilityOilBarrels", "D" }, { "Command_SpecialAbilityDwarvenIronHillsCatapultOilBarrels", "D" },
		// buffs on the hero or its horde (an attribute modifier list)
		{ "Command_SpecialAbilityBladeMaster", "B" }, { "Command_EowynShieldMaiden", "B" }, { "Command_LegolasKnifeFightingMode", "B" }, { "Command_SpecialAbilitySlayer", "B" },
		{ "Command_SpecialAbilityZerkergang", "B" }, { "Command_SpecialAbilityGlorfindelBladeOfPurity", "B" }, { "Command_SpecialAbilityNoldorWeaponSong", "B" },
		// the enemy: fear, a debuff
		{ "Command_SpecialAbilityElendil", "F" }, { "Command_SpecialAbilityHornOfGondor", "F" }, { "Command_SpecialAbilityTerribleFury", "F" },
		{ "Command_GlorfindelStarlight", "F" }, { "Command_SpecialAbilityDainMightyRage", "X" },
		// summons and created objects (Oathbreakers, the Royal Guard, the tornadoes, the revealers, the flood wave)
		{ "Command_SpawnOathbreakers", "S" }, { "Command_SpecialAbilityDainSummonRoyalGuard", "S" }, { "Command_SpecialPowerSpawnTornado", "S" },
		{ "Command_SpecialPowerSpawnWhirlwind", "S" }, { "Command_SpecialAbilityElrondFarsight", "S" }, { "Command_SpecialAbilityIvoryTowerVision", "S" },
		{ "Command_ArwenFlood", "S" },
		// levels for the ally (LevelGrantSpecialPower)
		{ "Command_SpecialAbilityCaptainOfGondor", "L" }, { "Command_SpecialAbilityCaptainOfGondorBoromir", "L" }, { "Command_SpecialAbilityKingsFavor", "L" },
		{ "Command_SpecialAbilityTrainArchers", "L" },
		// a timer refresh (SpecialPowerTimerRefreshSpecialPower)
		{ "Command_SpecialAbilityElrondRestoration", "R" },
		// the deploy toggle (lane SIEGE-1: DeployStyleAIUpdate, S-4945): no effect letter; the press is checked by the UNPACKING / DEPLOYED condition below
		{ "Command_SpecialAbilityDwarvenDemolisherDeploy", "" },
	};
	return e;
}

bool isAbility(const CommandButton &b)
{
	switch (b.m_command)
	{
		case GUI_COMMAND_SPECIAL_POWER:
		case GUI_COMMAND_FIRE_WEAPON:
		case GUI_COMMAND_TOGGLE_WEAPONSET:
		case GUI_COMMAND_TOGGLE_NO_AUTO_ACQUIRE:
			return true;
		default:
			return false;
	}
}

std::string stateName(int s)
{
	static const char *const n[] = { "hidden", "enabled", "restricted", "cantafford", "notready", "active" };
	return s >= 0 && s < 6 ? n[s] : "?";
}

void runObject(FactRig &r, const std::string &faction, const std::string &name, Outcome &out)
{
	std::map<std::string, std::string> &effects = out.effects;
	const ThingTemplate *tt = r.logic().things().findTemplate(name);
	const CommandSet *set = TheCommandStore->findCommandSet(templateCommandSet(*tt));
	std::vector<const CommandButton *> abilities;
	for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
	{
		const CommandButton *b = set->getCommandButton(slot);
		// a NONPRESSABLE button (option bit 28, RW 0xDA4C88) shows a passive ability (the leadership auras): not pressed by a player. A CONTEXTMODE_COMMAND
		// button is pressed and then targeted like the others (Arrow Storm, Train Archers, the Royal Guard); the porters' fire fighting wants a burning
		// building
		const bool fireFighting = b && b->m_specialPowerName == "SpecialAbilityPorterExtinguishFire";
		if (b && isAbility(*b) && !(b->m_showButton && (b->m_options & 0x10000000u) == 0 && !fireFighting))
		{
			effects[faction + " " + name + " " + b->m_name + " (" + commandName(b->m_command) + ")"] = !b->m_showButton ? "not shown (ShowButton No)"
				: fireFighting ? "context command (a click on a burning building: the fire fighting test)"
							   : "passive (NONPRESSABLE)";
		}
		if (b && isAbility(*b) && b->m_showButton && (b->m_options & 0x10000000u) == 0 && !fireFighting)
		{
			abilities.push_back(b);
		}
	}
	if (abilities.empty())
	{
		return;
	}
	++out.objects;
	for (const CommandButton *b : abilities)
	{
		// a fresh object per ability: a power that moves, transforms or hides the hero must not spoil the next one
		const Coord3D at = r.spot(2000.0f, 1300.0f, 350.0f);
		Object *o = r.make(name, at);
		r.frames(20); // a horde's members take their places
		if (ExperienceTracker *xp = o->getExperienceTracker())
		{
			xp->gainLevels(20, false);
		}
		r.frames(3);
		const std::string tag = faction + " " + name + " " + b->m_name + " (" + commandName(b->m_command) + ")";
		if (!r.select(o))
		{
			effects[tag] = "not selected";
			out.problems.push_back(tag + ": a click does not select the object");
			r.logic().destroyObject(o);
			r.frames(2);
			continue;
		}
		Object *enemy = r.make("MordorFighterHorde", Coord3D{ at.x + 170.0f, at.y + 40.0f, 0.0f }, r.enemy());
		// Train Archers levels an archer horde (its filter): the ally is one
		const bool trainsArchers = b->m_specialPowerName == "SpecialAbilityTrainArchers";
		Object *ally = r.make(trainsArchers ? "GondorArcherHorde" : "GondorFighterHorde", Coord3D{ at.x - 260.0f, at.y - 60.0f, 0.0f });
		// a capture (SPECIAL_INFANTRY_CAPTURE_BUILDING) targets a neutral capture flag with its linked building beside it (RW 0x854573, as lane HERO-2's test)
		Object *flag = nullptr, *outpost = nullptr;
		if (b->m_specialPowerName == "SpecialAbilityCaptureBuilding")
		{
			Player *neutral = r.rig.game->players().getNeutralPlayer();
			outpost = r.make("Outpost", Coord3D{ at.x + 300.0f, at.y - 160.0f, 0.0f }, neutral);
			flag = r.make("CaptureFlag", Coord3D{ at.x + 180.0f, at.y - 160.0f, 0.0f }, neutral);
		}
		// the effect is the ability's alone: nobody fights on his own, and the object and the ally carry a wound a heal can close
		for (Object *x : { o, enemy, ally })
		{
			holdFire(*x);
		}
		wound(*o);
		wound(*ally);
		r.frames(2);
		for (Object *x : { o, enemy, ally })
		{
			holdFire(*x); // lane PLAY-4 r3: the stances' guard began with their first update (FactAEffects.h)
		}
		// the enemy must be in the player's sight before an attack order on it counts (VIS-1: a fogged target refuses a player's order, RW 0x82C167)
		auto anyFogged = [&]() {
			bool fogged = false;
			if (enemy->getContain() && enemy->getContain()->getContainedItemsList())
			{
				for (Object *m : *enemy->getContain()->getContainedItemsList())
				{
					fogged = fogged || ShroudManager::isShroudedForAction(*o, *m);
				}
			}
			return fogged;
		};
		for (int f = 0; f < 150 && anyFogged(); ++f)
		{
			r.frames(1);
		}
		const ControlBarButton *cb = r.find(b->m_name);
		// a power its building's upgrade unlocks (the citadel's Boiling Oil, Ivory Tower, Mighty Catapult: UnpauseSpecialPowerUpgrade) gets that upgrade
		// first (its purchase is the production test's), as a player who bought it
		if ((!cb || cb->state == ButtonState::Restricted) && b->m_command == GUI_COMMAND_SPECIAL_POWER)
		{
			for (const auto &mod : o->modules())
			{
				const auto *ud = dynamic_cast<const UnpauseSpecialPowerUpgradeModuleData *>(mod->getModuleData());
				if (ud && ud->m_specialPowerTemplateName == b->m_specialPowerName)
				{
					for (const std::string &u : ud->m_triggeredBy)
					{
						o->giveUpgrade(u);
					}
				}
			}
			// a NEED_UPGRADE button (RW 0x9428CB) gets its NeededUpgrade list: the Rangers' Long Shot after the Archery Range's Long Shot purchase
			for (const std::string &n : b->hasOption(COMMAND_OPTION_NEED_UPGRADE) ? b->m_neededUpgrade : std::vector<std::string>())
			{
				const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(n) : nullptr;
				if (u && u->getUpgradeType() == UPGRADE_TYPE_PLAYER)
				{
					r.local()->addCompletedUpgrade(n);
				}
				else if (u)
				{
					o->giveUpgrade(n);
				}
			}
			r.frames(2);
			cb = r.find(b->m_name);
		}
		bool onBar = false;
		for (const auto *list : { &r.bar().palantirButtons(), &r.bar().sideButtons() })
		{
			for (const ControlBarButton &x : *list)
			{
				onBar = onBar || (cb && x.button == cb->button);
			}
		}
		if (cb && !onBar)
		{
			effects[tag] = "off the bar";
			// off the bar in retail too (HUD-4, S-289): reached by a hotkey or the context command, not a button click
			for (Object *x : { o, enemy, ally, flag, outpost })
			{
				r.logic().destroyObject(x);
			}
			r.frames(2);
			continue;
		}
		const SpecialPowerTemplate *spt = TheSpecialPowerStore && !b->m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName) : nullptr;
		SpecialPowerModuleInterface *spm = spt ? SpecialPowerModules::findModule(*o, spt) : nullptr;
		const Module *spModule = dynamic_cast<const Module *>(spm);
		const std::string moduleName = spModule ? spModule->getModuleClassName() : std::string("-");
		const unsigned long long castsBefore = r.rig.game->spellCommands().casts(), refusedBefore = r.rig.game->spellCommands().refused();
		const auto unhandledBefore = r.unhandled();
		const auto unportedBefore = r.bar().unportedPresses();
		// a MOUNTED_ONLY power (Theoden's Glorious Charge, Glorfindel's Wind Rider) is used on horseback: the hero mounts first with its own button
		static const int mountedBit = ModelCondition::indexOf("MOUNTED");
		if (cb && cb->state == ButtonState::Restricted && b->hasOption(COMMAND_OPTION_MOUNTED_ONLY) && !o->testModelCondition(mountedBit))
		{
			for (int slot = 0; set && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *m = set->getCommandButton(slot);
				if (m && m->m_specialPowerName == "SpecialAbilityToggleMounted" && r.press(m->m_name))
				{
					for (int f = 0; f < 90 && !o->testModelCondition(mountedBit); ++f)
					{
						r.frames(1);
					}
					break;
				}
			}
			r.frames(5);
			cb = r.find(b->m_name);
			// the mount may leave the power recharging: up to its reload time (frames)
			const SpecialPowerTemplate *mt = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName) : nullptr;
			const int reload = mt ? (int)mt->getReloadTime() + 10 : 0;
			for (int f = 0; f < reload && cb && cb->state == ButtonState::NotReady; ++f)
			{
				r.frames(1);
				cb = r.find(b->m_name);
			}
		}
		if (!cb)
		{
			out.problems.push_back(tag + ": not on the bar");
		}
		else if (cb->state == ButtonState::Restricted && (b->hasOption(COMMAND_OPTION_MOUNTED_ONLY) || b->hasOption(COMMAND_OPTION_UNMOUNTED_ONLY)))
		{
			// RW 0x942733: a MOUNTED_ONLY / UNMOUNTED_ONLY button is disabled in the other riding state; the hero is made on foot
			static const int mounted = ModelCondition::indexOf("MOUNTED");
			CHECK_MESSAGE(o->testModelCondition(mounted) == b->hasOption(COMMAND_OPTION_UNMOUNTED_ONLY), tag);
			effects[tag] = "riding state";
			++out.stopped;
		}
		else if (cb->state != ButtonState::Enabled && cb->state != ButtonState::Active)
		{
			out.problems.push_back(tag + ": " + stateName((int)cb->state) + " at the top level");
		}
		else
		{
			const ButtonState was = cb->state;
			const std::uint32_t weaponFlagsBefore = o->getWeaponSetFlags()[0];
			static const int noAuto = ObjectTemplateInfoBuilder::objectStatusIndex("NO_AUTO_ACQUIRE");
			const bool noAutoBefore = o->testStatus((unsigned)noAuto);
			const float enemyHealthBefore = groupHealth(*enemy);
			// a level grant (LevelGrantSpecialPower of this power) is aimed at the ally
			bool grantsLevels = false;
			for (const auto &m : o->modules())
			{
				const auto *ld = dynamic_cast<const LevelGrantSpecialPowerModuleData *>(m->getModuleData());
				grantsLevels = grantsLevels || (ld && ld->m_specialPowerTemplateName == b->m_specialPowerName);
			}
			Watch watch;
			watch.begin(r.logic(), *o, *ally, *enemy, outpost);
			// a timer refresh (Elrond's Restoration: SpecialPowerTimerRefreshSpecialPower) wants an allied hero whose power recharges at the click
			Object *helper = nullptr;
			if (moduleName == "SpecialPowerTimerRefreshSpecialPower")
			{
				helper = r.make("ElvenLegolas", Coord3D{ at.x + 150.0f, at.y - 60.0f, 0.0f });
				r.frames(1);
				for (const auto &m : helper->modules())
				{
					if (SpecialPowerModuleInterface *sp = dynamic_cast<SpecialPowerModuleInterface *>(m.get()))
					{
						sp->setReadyFrame(r.logic().getFrame() + 3000);
						if (sp->getPercentReady() < 1.0f)
						{
							watch.recharging.push_back(sp);
						}
					}
				}
				watch.helper = helper->getID();
			}
			Effects fx;
			const bool accepted = r.bar().pressButton(cb->slot, cb->inPalantir);
			++out.pressed;
			r.frames(1);
			if (r.hud->input().ui().getGUICommand() == b)
			{
				if (flag)
				{
					r.clickWorld(FactRig::clickPoint(*flag));
				}
				else if (b->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT))
				{
					r.clickWorld(FactRig::clickPoint(*enemy));
				}
				else if (b->hasOption(COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT))
				{
					r.clickWorld(FactRig::clickPoint(*ally));
				}
				else if (b->m_command == GUI_COMMAND_FIRE_WEAPON)
				{
					r.clickWorld(Coord3D{ at.x + 320.0f, at.y, 0.0f }); // beyond a siege weapon's minimum range, within its reach
				}
				else if (helper)
				{
					r.clickWorld(*helper->getPosition());
				}
				else if (grantsLevels)
				{
					// a level grant (Captain of Gondor, King's Favor, Train Archers) works within its RadiusEffect of the click: the ally's ground
					Coord3D ground = FactRig::clickPoint(*ally);
					ground.z = r.logic().getGroundHeight(ground.x, ground.y);
					r.clickWorld(ground);
				}
				else
				{
					// Area powers target the battalion's ground, not an individual soldier's occupied approach cell.
					Coord3D ground = *enemy->getPosition();
					ground.z = r.logic().getGroundHeight(ground.x, ground.y);
					// lane SUMMONLOC-1: the hero power click issues nothing where the location test refuses (RW 0x81E9C5 -> RW 0x6A4260 -> RW 0x82DFB7);
					// a LIMIT_DISTANCE power (Dain's Royal Guard 150, Aragorn's Oathbreakers 200) is clicked on the way to the enemy, as a player would
					const SpecialPowerTemplate *pw = b->m_command == GUI_COMMAND_SPECIAL_POWER && TheSpecialPowerStore
						? TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName) : nullptr;
					if (pw && !ActionManager::canDoSpecialPowerAtLocation(*o, ground, pw, b->m_options))
					{
						const Coord3D *c = o->getPosition();
						for (float k : { 0.6f, 0.4f })
						{
							Coord3D p{ c->x + (ground.x - c->x) * k, c->y + (ground.y - c->y) * k, 0.0f };
							p.z = r.logic().getGroundHeight(p.x, p.y);
							if (ActionManager::canDoSpecialPowerAtLocation(*o, p, pw, b->m_options))
							{
								ground = p;
								break;
							}
						}
					}
					r.clickWorld(ground);
				}
			}
			// a power may start its recharge only when its update module acts (UpdateModuleStartsAttack: the hero walks up, winds up): up to 150 frames
			bool recharging = false, firedSlot = false;
			const Weapon *slotWeapon = o->getWeapons() ? o->getWeapons()->weaponInSlot(b->m_weaponSlot) : nullptr;
			const std::uint32_t lastFireBefore = slotWeapon ? slotWeapon->lastFireFrame() : 0u;
			for (int f = 0; f < 150 && !recharging; ++f)
			{
				r.frames(1);
				watch.sample(r.logic(), r.local(), fx);
				if (b->m_command == GUI_COMMAND_FIRE_WEAPON && b->hasOption(COMMAND_OPTION_NEED_TARGET_POS) && r.logic().findObjectByID(o->getID()))
				{
					const Weapon *w = o->getWeapons() ? o->getWeapons()->weaponInSlot(b->m_weaponSlot) : nullptr;
					firedSlot = firedSlot || (w && w->lastFireFrame() != lastFireBefore);
					recharging = firedSlot;
					continue;
				}
				recharging = spm && r.logic().findObjectByID(o->getID()) && spm->getPercentReady() < 1.0f;
				if (b->m_command == GUI_COMMAND_FIRE_WEAPON && b->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT))
				{
					recharging = r.logic().findObjectByID(enemy->getID()) == nullptr || groupHealth(*enemy) < enemyHealthBefore; // the shot hit the target
					continue;
				}
				if (b->m_command != GUI_COMMAND_SPECIAL_POWER)
				{
					break;
				}
			}
			// the effect may follow the recharge (a summon walks in, a capture takes its time, a projectile flies): 60 more frames, 90 for a capture
			for (int f = 0; f < (flag ? 400 : 60); ++f)
			{
				r.frames(1);
				watch.sample(r.logic(), r.local(), fx);
				if (flag && fx.has('C'))
				{
					break;
				}
			}
			effects[tag] = fx.str();
			const auto want = expectedEffects().find(b->m_name);
			if (want == expectedEffects().end())
			{
				out.problems.push_back(tag + ": no expected effect is listed for the button (effects seen: " + fx.str() + ")");
			}
			else
			{
				for (char c : want->second)
				{
					if (!fx.has(c))
					{
						out.problems.push_back(tag + ": the effect " + std::string(1, c) + " is missing (seen: " + fx.str() + ")");
					}
				}
			}
			if (!accepted || r.bar().unportedPresses() != unportedBefore)
			{
				out.problems.push_back(tag + ": the control bar does not execute the press");
			}
			else if (r.unhandled() != unhandledBefore)
			{
				out.problems.push_back(tag + ": the logic leaves the message unhandled");
			}
			else
			{
				const ControlBarButton *now = r.logic().findObjectByID(o->getID()) ? r.find(b->m_name) : nullptr;
				bool changed = !now || now->state != was || recharging;
				// a power without a reload (SPECIAL_GENERAL_TARGETLESS toggles: the Demolisher's deploy) never shows a recharge: the logic casting it is the effect
				if (!changed && spt && spt->getReloadTime() == 0 && r.rig.game->spellCommands().casts() > castsBefore)
				{
					changed = true;
				}
				if (b->m_command == GUI_COMMAND_TOGGLE_WEAPONSET && r.logic().findObjectByID(o->getID()))
				{
					changed = o->getWeaponSetFlags()[0] != weaponFlagsBefore; // the toggle flips the object's weapon set flag (RW 0x77B529)
				}
				if (b->m_command == GUI_COMMAND_TOGGLE_NO_AUTO_ACQUIRE && r.logic().findObjectByID(o->getID()))
				{
					changed = o->testStatus((unsigned)noAuto) != noAutoBefore; // the hold-fire toggle (RW 0x77B6C4)
				}
				if (b->m_command == GUI_COMMAND_FIRE_WEAPON)
				{
					changed = recharging;
					if (b->hasOption(COMMAND_OPTION_NEED_TARGET_POS) && !b->hasOption(COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT))
					{
						// the attack on the clicked ground (AI_ATTACK_POSITION): the locked weapon of the button's slot fires. Without a weapon in that slot in the
						// current weapon set (a Noldor horde with its swords out) the lock fails and nothing happens (RW 0x77A327: no member locked), as in retail
						changed = firedSlot || !slotWeapon;
						if (!slotWeapon)
						{
							++out.stopped;
						}
					}
				}
				// lane SIEGE-1 (S-1984 / S-4945): the deploy toggle (ToggleDeploySpecialAbilityUpdate) asks the DeployStyleAIUpdate to deploy: the
				// Demolisher unpacks (UNPACKING) and stands DEPLOYED
				if (b->m_name == "Command_SpecialAbilityDwarvenDemolisherDeploy" && r.logic().findObjectByID(o->getID()))
				{
					static const int kUnpacking = CombatNames::modelCondition("UNPACKING"), kDeployed = CombatNames::modelCondition("DEPLOYED");
					changed = changed || o->testModelCondition(kUnpacking) || o->testModelCondition(kDeployed);
				}
				if (changed)
				{
					++out.fired;
				}
				else
				{
					out.problems.push_back(tag + ": pressed, the button stays " + stateName((int)was) + " (the power did not run; module " + moduleName + ", casts +"
						+ std::to_string(r.rig.game->spellCommands().casts() - castsBefore) + ", refused +" + std::to_string(r.rig.game->spellCommands().refused() - refusedBefore)
						+ ", percent ready " + std::to_string(spm && r.logic().findObjectByID(o->getID()) ? spm->getPercentReady() : -1.0f) + ")");
				}
			}
		}
		const ObjectID oid = o->getID(), eid = enemy->getID(), aid = ally->getID(), fid = flag ? flag->getID() : INVALID_ID, pid = outpost ? outpost->getID() : INVALID_ID;
		for (ObjectID id : { oid, eid, aid, fid, pid })
		{
			if (Object *x = id != INVALID_ID ? r.logic().findObjectByID(id) : nullptr)
			{
				r.logic().destroyObject(x);
			}
		}
		// what the ability left behind (a summon, a spawned helper, a captured flag's replacement): the next ability starts in an empty field
		for (Object *x = r.logic().getFirstObject(); x; x = x->getNextObject())
		{
			if (x->getID() > oid && !x->getContainedBy())
			{
				r.logic().destroyObject(x);
			}
		}
		r.hud->input().ui().setGUICommand(nullptr);
		r.frames(3);
	}
}
} // namespace

TEST_CASE("FACT-A abilities: every hero and unit ability of Men, Elves and Dwarves pressed on the control bar at the top level, its target clicked; the power runs")
{
	if (!hudtest::haveWorld("facta abilities"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	size_t problems = 0, knownIssues = 0;
	for (const std::string &f : factions())
	{
		FactRig r(s, f.c_str());
		r.local()->depositMoney(1000000);
		Outcome out;
		for (const std::string &name : reachableTemplates(r))
		{
			runObject(r, f, name, out);
		}
		std::printf("facta abilities %s: %zu objects, %zu presses, %zu powers ran, %zu on a registered stop or disabled by the riding state, %zu problems\n", f.c_str(), out.objects, out.pressed, out.fired,
			out.stopped, out.problems.size());
		for (const std::string &p : out.problems)
		{
			std::printf("facta ability problem: %s\n", p.c_str());
		}
		for (const auto &kv : out.effects)
		{
			std::printf("facta ability effect: %s -> %s\n", kv.first.c_str(), kv.second.c_str());
		}
		// known, reported: a Noldor horde's position attack (ArcherBombard) leaves AI_ATTACK_POSITION on the frame it enters it (the horde attack machine's
		// position branch, lane PLAY-2's AIAttackState): the bombard does nothing. Pinned so a fix or a new failure shows
		for (const std::string &p : out.problems)
		{
			const bool known = p.find("FactionElves NoldorWarriorHorde Command_ArcherBombard") == 0;
			known ? ++knownIssues : ++problems;
		}
		CHECK(out.fired > 10);
	}
	CHECK(problems == 0);
	CHECK(knownIssues == 1);
}
