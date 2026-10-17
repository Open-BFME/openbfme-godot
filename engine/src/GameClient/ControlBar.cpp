// OpenBFME. GPL-3.0.
// See ControlBar.h.

#include "GameClient/ControlBar.h"
#include "Common/Upgrade.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "Common/PlayerHeroList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Contain/OpenContainRuntime.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"
#include "Common/Team.h"
#include "GameLogic/WeaponSetToggle.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/GateModules.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/CastleModules.h"
#include "Common/ModelState.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <algorithm>

namespace ModelCondition
{
int indexOf(const std::string &name);
}

namespace
{
const char *kPaletteSize = nullptr;

int hotkeyOf(const std::string &)
{
	return 0; // the `&` accelerator of a label needs the game text: the HUD fills it when a text source is given
}
} // namespace

ButtonState ControlBar::evaluate(const CommandButton &b, Object &obj)
{
	// ZH ControlBar::getCommandAvailability (ControlBarCommand.cpp:1017), the subset whose logic is ported (stop S-290)
	Player *player = obj.getControllingPlayer();
	if (!player)
	{
		return ButtonState::Hidden;
	}
	ProductionUpdateInterface *pu = obj.getProductionUpdate();
	if (pu && pu->firstProduction() && b.hasOption(COMMAND_OPTION_NOT_QUEUEABLE))
	{
		return ButtonState::Restricted;
	}
	// lane FACT-A: RW 0x942733 (RotWK's getCommandAvailability), before its command switch: a MOUNTED_ONLY button is disabled (0) while the object lacks the
	// model condition MOUNTED (object + 0x124 >> 22 & 1: model condition 214), an UNMOUNTED_ONLY one while it has it (Glorfindel's Wind Rider, the mounted heroes)
	static const int mounted = ModelCondition::indexOf("MOUNTED");
	if ((b.hasOption(COMMAND_OPTION_MOUNTED_ONLY) && !obj.testModelCondition(mounted)) || (b.hasOption(COMMAND_OPTION_UNMOUNTED_ONLY) && obj.testModelCondition(mounted)))
	{
		return ButtonState::Restricted;
	}
	// lane FACT-B, RW 0x9428A3 .. 0x942993 (before every command's own rule): a NEED_UPGRADE button (Options bit 0x40) needs its NeededUpgrade list (+0x28):
	// every entry, or any one with NeededUpgradeAny (+0x34). A PLAYER entry asks Player::hasUpgradeComplete (RW 0x6AC2AF), an OBJECT entry Object::hasUpgrade
	// (RW 0x691421). A missing entry restricts the button (RW 0x9438C8: 0), except that a missing OBJECT entry hides a build button whose template is
	// Buildable = No, or Only_By_AI for a human (RW 0x942950 .. 0x942972: 3)
	if (b.hasOption(COMMAND_OPTION_NEED_UPGRADE) && !b.m_neededUpgrade.empty())
	{
		bool restricted = b.m_neededUpgradeAny;
		for (const std::string &name : b.m_neededUpgrade)
		{
			const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr;
			if (!u)
			{
				continue; // RW 0x9428E0: a null entry is skipped
			}
			const bool isPlayer = u->getUpgradeType() == UPGRADE_TYPE_PLAYER;
			const bool have = isPlayer ? player->hasUpgradeComplete(u) : obj.hasUpgrade(u);
			if (b.m_neededUpgradeAny)
			{
				if (have)
				{
					restricted = false;
					break;
				}
				continue;
			}
			if (have)
			{
				continue;
			}
			if (isPlayer)
			{
				return ButtonState::Restricted;
			}
			if (const ThingTemplate *tt = b.getThingTemplate())
			{
				const int buildable = BuildAssistant::buildable(*tt);
				if (buildable == 2 || (buildable == 3 && player->getPlayerType() != PLAYER_COMPUTER))
				{
					return ButtonState::Hidden;
				}
			}
			restricted = true;
		}
		if (restricted)
		{
			return ButtonState::Restricted;
		}
	}
	switch (b.m_command)
	{
		case GUI_COMMAND_UNIT_BUILD:
		case GUI_COMMAND_DOZER_CONSTRUCT:     // BUILD-1: a builder's or a plot's building button (the same availability: command set, price, command points, maximum)
		case GUI_COMMAND_FOUNDATION_CONSTRUCT:
		{
			const ThingTemplate *tt = b.getThingTemplate();
			if (!tt)
			{
				return ButtonState::Hidden;
			}
			const int buildable = BuildAssistant::buildable(*tt);
			if (buildable == 2 || (buildable == 3 && player->getPlayerType() != PLAYER_COMPUTER))
			{
				return ButtonState::Hidden; // Buildable = No, and Only_By_AI for a human
			}
			if (pu && !(b.m_command == GUI_COMMAND_DOZER_CONSTRUCT && BuildAssistant::isFortressBuilder(obj)) && pu->getProductionCount() >= pu->maxQueueEntries())
			{
				return ButtonState::Restricted;
			}
			if (!BuildAssistant::playerCanBuild(*player, tt, m_ctx.logic))
			{
				return ButtonState::Restricted;
			}
			switch (BuildAssistant::canMakeUnit(obj, tt, -1))
			{
				case CANMAKE_OK: return ButtonState::Enabled;
				case CANMAKE_NO_MONEY: return ButtonState::CantAfford;
				default: return ButtonState::Restricted;
			}
		}
		case GUI_COMMAND_PUSH_VISIBLE_COMMAND_RANGE:
		case GUI_COMMAND_POP_VISIBLE_COMMAND_RANGE:
		case GUI_COMMAND_STOP:
		case GUI_COMMAND_GUARD:
		case GUI_COMMAND_GUARD_WITHOUT_PURSUIT:
		case GUI_COMMAND_GUARD_FLYING_UNITS_ONLY:
		case GUI_COMMAND_ATTACK_MOVE:
		case GUI_COMMAND_SELL:
		case GUI_COMMAND_HORDE_TOGGLE_FORMATION:
			return ButtonState::Enabled;
		case GUI_COMMAND_SET_RALLY_POINT:
			return obj.getProductionUpdate() ? ButtonState::Enabled : ButtonState::Restricted;
		case GUI_COMMAND_TOGGLE_STANCE:
			// lane PLAY-4: RW 0x942A77: available when the object has a StancesBehavior (RW 0x861E6E / 0x68BDA5), else restricted
			return StancesBehavior::of(obj) ? ButtonState::Enabled : ButtonState::Restricted;
		case GUI_COMMAND_SET_STANCE:
			return ButtonState::Enabled; // RW 0x942733: type 0x3A is in the default (available) arm
		case GUI_COMMAND_TOGGLE_WEAPONSET:
			// lane HUD-4: RW 0x942FC6: restricted while a non-horde contain of the object holds someone, else available
			return WeaponSetToggle::isRestrictedByContain(obj) ? ButtonState::Restricted : ButtonState::Enabled;
		case GUI_COMMAND_TOGGLE_GATE:
		case GUI_COMMAND_OPEN_GATE:
		case GUI_COMMAND_CLOSE_GATE:
		{
			// lane HUD-5: RW 0x9436E9: restricted (0) under construction, dead, without a gate or while it moves; TOGGLE_GATE active (2), OPEN_GATE when closed,
			// CLOSE_GATE when open
			GateOpenAndCloseBehavior *gate = GateOpenAndCloseBehavior::findGate(obj);
			if (obj.isUnderConstruction() || obj.isEffectivelyDead() || !gate || !gate->isSettled())
			{
				return ButtonState::Restricted;
			}
			if (b.m_command == GUI_COMMAND_TOGGLE_GATE || (b.m_command == GUI_COMMAND_OPEN_GATE && !gate->isOpen()) || (b.m_command == GUI_COMMAND_CLOSE_GATE && gate->isOpen()))
			{
				return ButtonState::Active;
			}
			return ButtonState::Restricted;
		}
		case GUI_COMMAND_EVACUATE:
		{
			// lane UI-1: RW 0x942733 case 0x11: available when the contain holds someone (contain slot 0x114 getContainCount), else restricted
			ContainModuleInterface *c = obj.getContain();
			return c && c->getContainCount() != 0 ? ButtonState::Enabled : ButtonState::Restricted;
		}
		case GUI_COMMAND_EXIT_CONTAINER:
			// lane UI-1: shown disabled; the transport inventory (doTransportInventoryUI, RW 0x94251F) enables the slots of the riders
			return ButtonState::Restricted;
		case GUI_COMMAND_PLAYER_UPGRADE:
		{
			// lane FACT-A: RW 0x942733 case 6 (RW 0x942FF9): no upgrade template (button + 0x24) hides it (3); the player has it or is researching it: 8 (the
			// purchased state, ButtonState::Purchased); it cannot pay (UpgradeCenter::canAffordUpgrade RW 0x66F492) or the object's queue is full (RW 0x942A4F:
			// 20 entries): 6 (cannot afford); else available
			const UpgradeTemplate *u = TheUpgradeCenter && !b.m_upgradeName.empty() ? TheUpgradeCenter->findUpgrade(b.m_upgradeName) : nullptr;
			if (!u)
			{
				return ButtonState::Hidden;
			}
			if (player->hasUpgradeComplete(u) || player->hasUpgradeInProduction(u)) // RW 0x6AC2AF / 0x6AB2E1
			{
				return ButtonState::Purchased; // lane HUD-7: availability 8 (RW 0x9431A6)
			}
			const bool queueFull = pu && pu->getProductionCount() == 20;
			if (!UpgradeCenter::canAffordUpgrade(player, u, &obj) || queueFull)
			{
				return ButtonState::CantAfford;
			}
			return ButtonState::Enabled;
		}
		case GUI_COMMAND_OBJECT_UPGRADE:
		{
			// lane FACT-A: RW 0x942733 case 7 (RW 0x943050): no upgrade template hides it (3); a DEFENSIVE_WALL or CASTLE_KEEP under construction (status 2) or
			// effectively dead (+ 0x458 bit 0) and not UNDERGOING_REPAIR (0x14) hides it (3); no production queue (RW 0x68C327): disabled (0); the object has
			// it (RW 0x691421) or queues it (queue slot 0x18): 8 (Purchased); a full queue (20 entries): 0; an object the upgrade would not affect
			// (Object::affectedByUpgrade RW 0x694914: the RequiredObjectFilter's building, a module the upgrade triggers): 0; then canAffordUpgrade (RW
			// 0x66F492): available (1), else 6 (cannot afford). Not ported: a BRIDGE whose body is RUBBLE (RW 0x94305D .. 0x943074, disabled)
			const UpgradeTemplate *u = TheUpgradeCenter && !b.m_upgradeName.empty() ? TheUpgradeCenter->findUpgrade(b.m_upgradeName) : nullptr;
			if (!u)
			{
				return ButtonState::Hidden;
			}
			static const int underConstruction = ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION"), repair = ObjectTemplateInfoBuilder::objectStatusIndex("UNDERGOING_REPAIR");
			if ((obj.testStatus((unsigned)underConstruction) || obj.isEffectivelyDead()) && (obj.isKindOfName("DEFENSIVE_WALL") || obj.isKindOfName("CASTLE_KEEP"))
				&& !obj.testStatus((unsigned)repair))
			{
				return ButtonState::Hidden;
			}
			if (!pu)
			{
				return ButtonState::Restricted;
			}
			if (obj.hasUpgrade(u) || pu->isUpgradeInQueue(u))
			{
				return ButtonState::Purchased; // lane HUD-7: availability 8 (RW 0x9430C8 / 0x9430DD -> 0x9431A6)
			}
			if (pu->getProductionCount() == 20 || !obj.affectedByUpgrade(u))
			{
				return ButtonState::Restricted;
			}
			return UpgradeCenter::canAffordUpgrade(player, u, &obj) ? ButtonState::Enabled : ButtonState::CantAfford;
		}
		case GUI_COMMAND_CASTLE_UPGRADE:
		{
			// lane FACT-A: RW 0x942733 case 8 (RW 0x943123): the object's queue full (20 entries): 6 (cannot afford); no queue: disabled (0); the upgrade
			// not in the queue (slot 0x18) and the object's castle (CastleMemberBehavior + 0x18) has it: 8 (Purchased); otherwise canAffordUpgrade
			// (RW 0x66F492): available (1), else 6. (An upgrade already queued shows available, as RW 0x94313E has it.) A button without an upgrade template
			// is hidden here (RW passes the null template on)
			const UpgradeTemplate *u = TheUpgradeCenter && !b.m_upgradeName.empty() ? TheUpgradeCenter->findUpgrade(b.m_upgradeName) : nullptr;
			if (!u)
			{
				return ButtonState::Hidden;
			}
			if (pu && pu->getProductionCount() == 20)
			{
				return ButtonState::CantAfford;
			}
			if (!pu)
			{
				return ButtonState::Restricted;
			}
			if (!pu->isUpgradeInQueue(u))
			{
				const CastleMemberBehavior *member = dynamic_cast<const CastleMemberBehavior *>(obj.findModule("CastleMemberBehavior"));
				const Object *castle = member && member->castleId() != INVALID_ID ? m_ctx.logic.findObjectByID(member->castleId()) : nullptr;
				if (castle && castle->hasUpgrade(u))
				{
					return ButtonState::Purchased; // lane HUD-7: availability 8 (RW 0x9431A0 -> 0x9431A6)
				}
			}
			return UpgradeCenter::canAffordUpgrade(player, u, &obj) ? ButtonState::Enabled : ButtonState::CantAfford;
		}
		case GUI_COMMAND_TOGGLE_NO_AUTO_ACQUIRE:
			return ButtonState::Enabled; // lane FACT-A: RW 0x942733 has no case for 0x30: available (1)
		case GUI_COMMAND_FIRE_WEAPON:
		{
			// lane FACT-A: RW 0x942733 case 0x17: an object without an AI (object + 0x260) is disabled; a weapon in the button's slot (RW 0x6C81B0) whose template
			// reloads a clip (+ 0xEC ClipReloadTime max), can fire while charging (+ 0x82) or always shows its readiness (+ 0x155, RW 0x6CA415) is not ready (5)
			// while its status (RW 0x6CD142) is not READY_TO_FIRE or it may fire again this frame or the last (+ 0x18 == frame / frame - 1); otherwise available
			if (!obj.getAIUpdateInterface())
			{
				return ButtonState::Restricted;
			}
			ObjectWeapons *weapons = obj.getWeapons();
			const Weapon *weapon = weapons ? weapons->weaponInSlot(b.m_weaponSlot) : nullptr;
			const WeaponTemplate *wt = weapon ? weapon->getTemplate() : nullptr;
			if (wt && (wt->m_clipReloadMax != 0 || wt->m_canFireWhileCharging || wt->m_forceDisplayPercentReady))
			{
				const std::uint32_t now = m_ctx.logic.getFrame();
				if (weapons->slotStatus(b.m_weaponSlot) != WEAPON_READY_TO_FIRE || weapon->whenWeCanFireAgain() == now || weapon->whenWeCanFireAgain() + 1 == now)
				{
					return ButtonState::NotReady;
				}
			}
			return ButtonState::Enabled;
		}
		case GUI_COMMAND_SPECIAL_POWER:
		{
			// lane HERO-1 (S-850): a special power button of the object (the hero abilities): no module of the power on the object hides it, a paused module (an
			// ability its level has not unlocked) is restricted, a recharging one not ready (its timer), else enabled
			const SpecialPowerTemplate *t = specialPowerOf(b);
			SpecialPowerModuleInterface *sp = t ? SpecialPowerModules::findModule(obj, t) : nullptr;
			if (!sp)
			{
				return ButtonState::Hidden;
			}
			const SpecialPowerModule *m = dynamic_cast<const SpecialPowerModule *>(sp);
			if ((m && m->pauseCount() > 0) || !sp->requirementsMet())
			{
				return ButtonState::Restricted;
			}
			return sp->isReadyForDisplay() ? ButtonState::Enabled : ButtonState::NotReady;
		}
		case GUI_COMMAND_CASTLE_UNPACK:
		case GUI_COMMAND_CASTLE_UNPACK_EXPLICIT_OBJECT:
		{
			// lane BACKLOG2: RW 0x942733 cases 33 (RW 0x942CA9) and 50 (RW 0x942D1C): no castle module (or, for the explicit one, no template) hides (3); so do
			// canUnpack(0) (RW 0x7983E3) and isPlayerAllowedToPackOrUnpack(the controller, 0) (RW 0x79A472); the price (RW 0x79969F, the explicit template's
			// RW 0x797B6D) not covered: cannot afford (6); the readiness (RW 0x797EF9: 1 - timer / UnpackDelayTime) below 1: not ready (5); else active (2)
			const CastleBehavior *castle = dynamic_cast<const CastleBehavior *>(obj.findModule("CastleBehavior"));
			const ThingTemplate *tt = b.m_command == GUI_COMMAND_CASTLE_UNPACK_EXPLICIT_OBJECT ? b.getThingTemplate() : nullptr;
			if (!castle || (b.m_command == GUI_COMMAND_CASTLE_UNPACK_EXPLICIT_OBJECT && !tt) || !castle->canUnpack(false) || !castle->isPlayerAllowedToPackOrUnpack(player))
			{
				return ButtonState::Hidden;
			}
			const bool afford = tt ? castle->canAffordUnpackOf(player, tt) : castle->canAffordUnpack(player);
			if (!afford)
			{
				return ButtonState::CantAfford;
			}
			return castle->unpackReadiness() < 1.0f ? ButtonState::NotReady : ButtonState::Active;
		}
		case GUI_COMMAND_CASTLE_PACK:
		{
			// lane BACKLOG2: RW 0x942733 case 34 (RW 0x942C71): unpacked (RW 0x797B63) and allowed (RW 0x79A472): active (2), else hidden (3)
			const CastleBehavior *castle = dynamic_cast<const CastleBehavior *>(obj.findModule("CastleBehavior"));
			return castle && castle->isUnpacked() && castle->isPlayerAllowedToPackOrUnpack(player) ? ButtonState::Active : ButtonState::Hidden;
		}
		default:
			return ButtonState::Restricted;
	}
}

Object *ControlBar::castleOfMember(Object &obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		if (const CastleMemberBehavior *member = dynamic_cast<const CastleMemberBehavior *>(m.get())) // RW 0x68BDA5 by the name "CastleMemberBehavior"
		{
			return obj.logic().findObjectByID(member->castleId()); // RW 0x449681
		}
	}
	return nullptr;
}

const SpecialPowerTemplate *ControlBar::specialPowerOf(const CommandButton &b)
{
	return TheSpecialPowerStore && !b.m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(b.m_specialPowerName) : nullptr;
}

// lane HERO-1 (S-850): RW 0x7810BC, the button image of a hero record: Gandalf (GondorGandalf) without SCIENCE_GandalftheWhite shows HIGandalTheGrey; a
// Create-A-Hero (template + 0x11F & 0x40) its own data's image (not ported: the template's); otherwise the template's ButtonImage (RW 0x73CFEF)
std::string ControlBar::heroRecordImage(const HeroRecord &r, const Player &player)
{
	if (r.templateName == "GondorGandalf" && TheScienceStore)
	{
		const ScienceType st = TheScienceStore->getScienceFromInternalName("SCIENCE_GandalftheWhite");
		if (st != SCIENCE_INVALID && !player.hasScience(st))
		{
			return "HIGandalTheGrey";
		}
	}
	const ThingTemplate *tt = m_ctx.logic.things().findTemplate(r.templateName);
	if (tt)
	{
		for (const char *field : { "ButtonImage", "SelectPortrait" })
		{
			if (const FieldValue *v = tt->getFinalOverride()->findField(field))
			{
				if (const std::string *s = std::get_if<std::string>(v))
				{
					if (!s->empty())
					{
						return *s;
					}
				}
			}
		}
	}
	return std::string();
}

void ControlBar::update()
{
	(void)kPaletteSize;
	Player *local = m_ctx.localPlayer();
	m_money = local && local->getMoney() ? local->getMoney()->countMoney() : 0;
	m_cpUsed = local ? local->commandPoints().getUsage() : 0;
	m_cpLimit = local ? m_cpUsed + local->commandPointsAvailable() : 0;
	Object *obj = m_ctx.logic.findObjectByID(m_ctx.ui.firstSelected());
	if (obj && obj->isDestroyed())
	{
		obj = nullptr;
	}
	const ObjectID id = obj ? obj->getID() : (ObjectID)INVALID_ID;
	if (id != m_source)
	{
		m_source = id;
		m_setName.clear();
		resetRanges();
	}
	m_palantir.clear();
	m_side.clear();
	m_fortressSide.clear();
	m_offBar.clear();
	m_queue.clear();
	m_portrait.clear();
	m_displayName.clear();
	m_underConstruction = false;
	if (obj && isUnderConstructionContext(*obj))
	{
		m_displayName = obj->getTemplate() ? obj->getTemplate()->getName() : std::string();
		m_setName.clear();
		populateUnderConstruction(*obj, HudObjects::isLocallyControlled(m_ctx, *obj));
	}
	else if (obj)
	{
		m_displayName = obj->getTemplate() ? obj->getTemplate()->getName() : std::string();
		const bool mine = HudObjects::isLocallyControlled(m_ctx, *obj);
		m_setName = mine ? obj->getCommandSetName() : std::string();
		const CommandStore *store = m_ctx.commands;
		const CommandSet *set = (store && !m_setName.empty()) ? store->findCommandSet(m_setName) : nullptr;
		ProductionUpdateInterface *pu = mine ? obj->getProductionUpdate() : nullptr;
		if (set)
		{
			const int start = m_rangeStack.back().first, count = m_rangeStack.back().second;
			const int initial = m_rangeStack.size() == 1 ? std::min(set->m_initialVisible, (int)CommandSet::MAX_BUTTONS) : count;
			for (int slot = start; slot < start + (m_rangeStack.size() == 1 ? initial : count) && slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const CommandButton *b = set->getCommandButton(slot);
				if (!b || !b->m_showButton)
				{
					continue;
				}
				ControlBarButton cb;
				cb.slot = slot;
				cb.button = b;
				if (b->m_command == GUI_COMMAND_REVIVE)
				{
					// lane HERO-1 (S-850): RW 0x943D6F's first pass. The jth REVIVE button of the set (counting every REVIVE slot of the set) stands for the jth
					// template of the player template's BuildableRingHeroesMP then BuildableHeroesMP (RW 0x6AB249: + 0x198 then + 0x18C), and shows that template's
					// record, the nth one for the nth button of the same template (RW 0x78131E); no record: no button. A NEED_UPGRADE button (the ring hero and
					// Create-A-Hero slots) hides without its upgrade (HIDE_WHILE_DISABLED). The second pass (leftover records into free slots) is not ported (S-850)
					int j = 0;
					std::map<std::string, int> seen;
					std::string tmpl;
					const PlayerTemplate *pt = local ? local->getPlayerTemplate() : nullptr;
					for (int k = 0; k <= slot; ++k)
					{
						const CommandButton *o = set->getCommandButton(k);
						if (!o || o->m_command != GUI_COMMAND_REVIVE)
						{
							continue;
						}
						tmpl.clear();
						if (pt)
						{
							const size_t ring = pt->m_buildableRingHeroesMP.size();
							if ((size_t)j < ring)
							{
								tmpl = pt->m_buildableRingHeroesMP[(size_t)j];
							}
							else if ((size_t)j - ring < pt->m_buildableHeroesMP.size())
							{
								tmpl = pt->m_buildableHeroesMP[(size_t)j - ring];
							}
						}
						++j;
						if (k < slot && !tmpl.empty())
						{
							++seen[tmpl];
						}
					}
					const ThingTemplate *rt = tmpl.empty() ? nullptr : m_ctx.logic.things().findTemplate(tmpl);
					const int n = (rt && local) ? local->heroes().findIndex(*rt, 0xFFFFFFFFu, seen[tmpl]) : -1;
					if (b->hasOption(COMMAND_OPTION_NEED_UPGRADE) && !b->m_neededUpgrade.empty() && local)
					{
						bool have = false;
						for (const std::string &u : b->m_neededUpgrade)
						{
							const UpgradeTemplate *ut = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(u) : nullptr;
							have = have || (ut && (local->hasUpgradeComplete(ut) || obj->hasUpgrade(ut)));
						}
						if (!have)
						{
							continue;
						}
					}
					if (n < 0 || !local || !reviveButton(cb, n, *local, *obj))
					{
						continue;
					}
					cb.inPalantir = b->m_inPalantir;
					cb.textLabel = b->m_textLabel.empty() ? std::string() : b->m_textLabel.front();
					cb.descriptLabel = b->m_descriptLabel.empty() ? std::string() : b->m_descriptLabel.front();
					place(cb, start);
					continue;
				}
				cb.state = evaluate(*b, *obj);
				// lane HUD-6: RW 0x943B39 .. 0x943B4C: a disabled button (availability 0) with the option HIDE_WHILE_DISABLED (CommandButton + 0x1E & 0x40,
				// option bit 22 of RW 0xDA4C88) is hidden (3), unless the object is another player's while the local player is active (bl, RW 0x943B18):
				// the Mordor fortress's spire fireball before its upgrade
				const bool otherPlayers = obj->getControllingPlayer() != local;
				if (cb.state == ButtonState::Restricted && b->hasOption(0x400000u) && !otherPlayers)
				{
					continue;
				}
				if (cb.state == ButtonState::Hidden)
				{
					continue;
				}
				cb.inPalantir = b->m_inPalantir;
				cb.image = b->m_buttonImageName.empty() ? std::string() : b->m_buttonImageName.front();
				cb.textLabel = b->m_textLabel.empty() ? std::string() : b->m_textLabel.front();
				cb.descriptLabel = b->m_descriptLabel.empty() ? std::string() : b->m_descriptLabel.front();
				if (b->hasOption(COMMAND_OPTION_TOGGLE_IMAGE_ON_WEAPONSET) && b->m_buttonImageName.size() > 1)
				{
					// lane HUD-4 (INFERENCE, S-1672): the second image and labels while the object has one of the button's FlagsUsedForToggle
					const WeaponConditionFlags &f = obj->getWeaponSetFlags();
					bool on = false;
					for (size_t w = 0; w < f.size() && w < b->m_flagsUsedForToggle.size(); ++w)
					{
						on = on || (f[w] & b->m_flagsUsedForToggle[w]) != 0;
					}
					if (on)
					{
						cb.image = b->m_buttonImageName[1];
						cb.textLabel = b->m_textLabel.size() > 1 ? b->m_textLabel[1] : cb.textLabel;
						cb.descriptLabel = b->m_descriptLabel.size() > 1 ? b->m_descriptLabel[1] : cb.descriptLabel;
					}
				}
				if (b->m_command == GUI_COMMAND_TOGGLE_STANCE)
				{
					// lane PLAY-4: the toggle shows the image and labels of the current stance (RW onStancesBehaviorStanceChanged / SetStance: the button's
					// image slot StanceToButtonSlot(stance), BFME2 0x567B10)
					const int k = stanceSlotOf(*b, *obj);
					if (k >= 0)
					{
						cb.image = (size_t)k < b->m_buttonImageName.size() ? b->m_buttonImageName[(size_t)k] : cb.image;
						cb.textLabel = (size_t)k < b->m_textLabel.size() ? b->m_textLabel[(size_t)k] : cb.textLabel;
						cb.descriptLabel = (size_t)k < b->m_descriptLabel.size() ? b->m_descriptLabel[(size_t)k] : cb.descriptLabel;
					}
				}
				cb.hotkey = hotkeyOf(cb.textLabel);
				if (cb.state == ButtonState::NotReady && b->m_command == GUI_COMMAND_SPECIAL_POWER)
				{
					if (SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*obj, specialPowerOf(*b)))
					{
						cb.timer = sp->getPercentReadyForDisplay(); // lane HERO-1: the recharge clock of the ability button
					}
				}
				if (const ThingTemplate *tt = b->m_command == GUI_COMMAND_UNIT_BUILD ? b->getThingTemplate() : nullptr)
				{
					cb.cost = BuildAssistant::calcCostToBuild(*tt, local, obj, -1);
					for (const ProductionEntry *e = pu ? pu->firstProduction() : nullptr; e; e = pu->nextProduction(e))
					{
						if (e->type == PRODUCTION_UNIT && e->objectToProduce && BuildAssistant::isEquivalentTo(e->objectToProduce, tt))
						{
							cb.queued += e->quantityRemaining();
							if (cb.timer < 0.0f)
							{
								cb.timer = e->percentComplete / 100.0f;
							}
						}
					}
				}
				if (b->m_command == GUI_COMMAND_OBJECT_UPGRADE || b->m_command == GUI_COMMAND_PLAYER_UPGRADE || b->m_command == GUI_COMMAND_CASTLE_UPGRADE)
				{
					const UpgradeTemplate *upgrade = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(b->m_upgradeName) : nullptr;
					for (const ProductionEntry *e = pu ? pu->firstProduction() : nullptr; upgrade && e; e = pu->nextProduction(e))
					{
						if (e->type == PRODUCTION_UPGRADE && e->upgradeToResearch == upgrade)
						{
							cb.timer = e->percentComplete / 100.0f;
							break;
						}
					}
				}
				// lane AUTOCAST-1: RW 0x9D2C8C .. 0x9D2D13 (the command button clip's update): the glow shows when the clip's state is not _unused and the
				// object's AutoAbilityBehavior for the button (its power's, else the first) auto casts this button (RW 0x85D741)
				if (const AutoAbilityBehavior *aa = AutoAbilityBehavior::forButton(*obj, *b))
				{
					cb.autoAbility = aa->isAutoButton(*b);
				}
				place(cb, start);
			}
			// RW 0x943D6F: a contain that shows its riders on the control bar (contain slot 0xC8) fills the set's EXIT_CONTAINER buttons
			if (ContainModuleInterface *c = obj->getContain(); c && c->isDisplayedOnControlBar())
			{
				doTransportInventoryUI(*obj);
			}
		}
		else if (obj->getCommandSetName().empty())
		{
			structureInventory(*obj, local);
		}
		for (const ProductionEntry *e = pu ? pu->firstProduction() : nullptr; e; e = pu->nextProduction(e))
		{
			ControlBarQueueEntry q;
			q.templateName = e->objectToProduce ? e->objectToProduce->getName() : (e->upgradeToResearch ? e->upgradeToResearch->getUpgradeName() : std::string());
			q.percent = e->percentComplete;
			q.quantityTotal = e->quantityTotal;
			q.quantityProduced = e->quantityProduced;
			q.cost = e->cost;
			m_queue.push_back(q);
		}
	}
	// Preserve the fortress's normal radial commands; the independent build list occupies the side bar.
	if (obj && HudObjects::isLocallyControlled(m_ctx, *obj))
		if (const auto buildSet = BuildAssistant::fortressBuildSet(*obj))
			for (int slot = 0; slot < CommandSet::MAX_BUTTONS; ++slot)
			{
				const auto b = buildSet->getCommandButton(slot);
				if (!b || !b->m_showButton || b->m_command != GUI_COMMAND_DOZER_CONSTRUCT || !b->getThingTemplate()) continue;
				ControlBarButton cb; cb.slot = CommandSet::MAX_BUTTONS + slot; cb.button = b;
				cb.state = evaluate(*b, *obj); if (cb.state == ButtonState::Hidden) continue;
				cb.position = (int)m_fortressSide.size();
				cb.image = b->m_buttonImageName.empty() ? std::string() : b->m_buttonImageName.front();
				cb.textLabel = b->m_textLabel.empty() ? std::string() : b->m_textLabel.front();
				cb.descriptLabel = b->m_descriptLabel.empty() ? std::string() : b->m_descriptLabel.front();
				cb.cost = BuildAssistant::calcCostToBuild(*b->getThingTemplate(), local, nullptr, -1);
				cb.hotkey = hotkeyOf(cb.textLabel); m_fortressSide.push_back(cb);
			}
	emitStateKey();
}

// lane HERO-1 (S-850): a REVIVE button for record `n` (false: no record, the button is hidden). The availability is the logic's (BuildAssistant::canMakeUnit
// with the build index, RW 0x793ECB; RW 0x942733's UI rules beyond it are S-290's); the cost is the record's (RW 0x780614), the timer its production progress
// (RW 0x780C9F) while it is queued
bool ControlBar::reviveButton(ControlBarButton &cb, int n, Player &player, Object &producer)
{
	PlayerHeroList &list = player.heroes();
	const HeroRecord *r = list.at(n);
	if (!r)
	{
		return false;
	}
	cb.reviveIndex = n;
	cb.image = heroRecordImage(*r, player);
	cb.cost = list.costAt(m_ctx.logic, player, n, &producer);
	if (r->startFrame >= 0)
	{
		cb.queued = 1;
		cb.timer = (float)list.progressAt(m_ctx.logic, player, n, &producer);
		cb.state = ButtonState::NotReady;
		return true;
	}
	switch (BuildAssistant::canMakeUnit(producer, nullptr, n))
	{
		case CANMAKE_OK: cb.state = ButtonState::Enabled; break;
		case CANMAKE_NO_MONEY: cb.state = ButtonState::CantAfford; break;
		default: cb.state = ButtonState::Restricted; break;
	}
	return true;
}

std::string ControlBar::templateButtonImage(const ThingTemplate *tt)
{
	// RW 0x73CFEF: the template's ButtonImage (template + 0x78); RW 0x73D0BA first takes a Create-A-Hero's own image (template + 0x11F & 0x40: not ported, S-850)
	if (tt)
	{
		if (const FieldValue *v = tt->getFinalOverride()->findField("ButtonImage"))
		{
			if (const std::string *s = std::get_if<std::string>(v))
			{
				return *s;
			}
		}
	}
	return std::string();
}

// lane UI-1: ControlBar::doTransportInventoryUI (RW 0x94251F; ZH ControlBarCommand.cpp doTransportInventoryUI / populateInvDataCallback).  TARGET FACTS: nothing
// happens when the contain is an OpenContain whose Enabled is off (slot 4 asOpenContain, module + 0xDE).  Every EXIT_CONTAINER button of the set is shown
// disabled with its own image (the first and last such slot are remembered; ZH's hiding of the slots beyond getContainMax / getExtraSlotsInUse is gone: RotWK
// calls slots 0x70 / 0xCC and drops the answers).  Then the riders, in list order (contain slot 0x110 with flag 1: the rider list, forward), through RW 0x942395:
// each rider whose turn starts at or before the last EXIT_CONTAINER slot takes TransportSlotCount (RW 0x69029B) consecutive slots with its ButtonImage
// (RW 0x73D0BA), the first enabled and the others disabled, the slot -> rider table (RW 0xDEB888) filled for the press (RW 0x940FEF: MSG_EXIT).
// Not reproduced: a rider whose slots run past the last EXIT_CONTAINER slot overwrites the following buttons in retail (the check is per rider, RW 0x942395);
// here its slots stop at the last one; the hide when the object's field + 0x1C8 has bit 5 (unidentified, S-1260); the window status bits (draw only).
void ControlBar::doTransportInventoryUI(Object &obj)
{
	ContainModuleInterface *contain = obj.getContain();
	const OpenContain *open = dynamic_cast<const OpenContain *>(contain);
	if (!contain || (open && !open->isEnabled()))
	{
		return;
	}
	std::vector<ControlBarButton *> exits;
	for (auto *v : { &m_palantir, &m_side, &m_offBar })
	{
		for (ControlBarButton &b : *v)
		{
			if (b.button && b.button->m_command == GUI_COMMAND_EXIT_CONTAINER)
			{
				b.state = ButtonState::Restricted;
				exits.push_back(&b);
			}
		}
	}
	if (exits.empty())
	{
		return;
	}
	std::sort(exits.begin(), exits.end(), [](const ControlBarButton *a, const ControlBarButton *b) { return a->slot < b->slot; });
	const int first = exits.front()->slot, last = exits.back()->slot;
	int current = first;
	const ContainModuleInterface::ContainedItemsList *riders = contain->getContainedItemsList();
	for (Object *rider : riders ? *riders : ContainModuleInterface::ContainedItemsList())
	{
		if (!rider || current > last)
		{
			continue;
		}
		const std::string image = templateButtonImage(rider->getTemplate());
		const int n = TransportContain::transportSlotCount(*rider);
		for (int k = 0; k < n && current <= last; ++k, ++current)
		{
			for (ControlBarButton *b : exits)
			{
				if (b->slot == current)
				{
					b->rider = rider->getID();
					b->image = image;
					b->state = k == 0 ? ButtonState::Enabled : ButtonState::Restricted;
				}
			}
		}
	}
}

// lane UI-1: evaluateContextUI (RW 0x71EBDA): an object without a command set whose contain is garrisonable (slot 0x10) switches to context 2 when it is locally
// controlled or NEUTRAL to the local player (RW 0x68B749 / 0x6ADBEB); context 2 (RW 0x71D8BE) is RW 0x94518D's inventory: the first getContainMax (slot 0x70)
// buttons are the command button Command_StructureExit (a hard-coded name, RW 0xC22FD8) with the CONTAINER's ButtonImage (RW 0x73CFEF of the container's
// template), disabled; the buttons beyond are hidden; button 11 is Command_Evacuate (RW 0xC8041C), enabled when the contain holds someone (slot 0x114); each
// rider in list order (RW 0x9450F0, one button per rider) gets its ButtonImage, the slot -> rider table and is enabled.  The contested variant (context 3,
// Command_Evacuate_Contested, slots 0x128 / 0x12C) needs MSG_EVACUATE_CONTESTERS (S-1103): not ported.  The buttons take this model's slots 0 .. 32; which
// of them show on the Palantir follows the buttons' InPalantir (S-289).
bool ControlBar::structureInventory(Object &obj, Player *local)
{
	ContainModuleInterface *contain = obj.getContain();
	if (!contain || !contain->isGarrisonable() || !m_ctx.commands || !local)
	{
		return false;
	}
	const bool mine = HudObjects::isLocallyControlled(m_ctx, obj);
	if (!mine && local->getRelationship(obj.getTeam()) != NEUTRAL)
	{
		return false;
	}
	const CommandButton *exitButton = m_ctx.commands->findCommandButton("Command_StructureExit");
	const CommandButton *evacuate = m_ctx.commands->findCommandButton("Command_Evacuate");
	const OpenContain *open = dynamic_cast<const OpenContain *>(contain);
	const int max = std::min(open ? open->getContainMax() : 0, (int)CommandSet::MAX_BUTTONS);
	const std::string containerImage = templateButtonImage(obj.getTemplate());
	std::vector<ControlBarButton> buttons;
	for (int i = 0; i < max && exitButton; ++i)
	{
		ControlBarButton cb;
		cb.slot = i;
		cb.button = exitButton;
		cb.state = ButtonState::Restricted;
		cb.inPalantir = exitButton->m_inPalantir;
		cb.image = !containerImage.empty() ? containerImage : (exitButton->m_buttonImageName.empty() ? std::string() : exitButton->m_buttonImageName.front());
		cb.textLabel = exitButton->m_textLabel.empty() ? std::string() : exitButton->m_textLabel.front();
		cb.descriptLabel = exitButton->m_descriptLabel.empty() ? std::string() : exitButton->m_descriptLabel.front();
		buttons.push_back(cb);
	}
	int index = 0;
	const ContainModuleInterface::ContainedItemsList *riders = contain->getContainedItemsList();
	for (Object *rider : riders ? *riders : ContainModuleInterface::ContainedItemsList())
	{
		if (rider && index < (int)buttons.size())
		{
			buttons[(std::size_t)index].rider = rider->getID();
			buttons[(std::size_t)index].image = templateButtonImage(rider->getTemplate());
			buttons[(std::size_t)index].state = ButtonState::Enabled;
		}
		++index;
	}
	if (evacuate && max <= 11)
	{
		ControlBarButton cb;
		cb.slot = 11; // RW 0x94518D: the window at control bar + 0x108 (0xDC + 11 * 4)
		cb.button = evacuate;
		cb.state = contain->getContainCount() != 0 ? ButtonState::Enabled : ButtonState::Restricted;
		cb.inPalantir = evacuate->m_inPalantir;
		cb.image = evacuate->m_buttonImageName.empty() ? std::string() : evacuate->m_buttonImageName.front();
		cb.textLabel = evacuate->m_textLabel.empty() ? std::string() : evacuate->m_textLabel.front();
		cb.descriptLabel = evacuate->m_descriptLabel.empty() ? std::string() : evacuate->m_descriptLabel.front();
		buttons.push_back(cb);
	}
	else if (max > 11 && contain->getContainCount() != 0)
	{
		buttons[11].state = ButtonState::Enabled; // RW 0x94518D: the loop gave window 11 Command_StructureExit; the evacuate's enable still lands on it
	}
	for (const ControlBarButton &cb : buttons)
	{
		place(cb, 0); // context 2 has no range: window i is slot i
	}
	return true;
}

bool ControlBar::isUnderConstructionContext(const Object &obj)
{
	// RW 0x71EE29 .. 0x71EE61: a WALL_UPGRADE whose Object query RW 0x68C82D(3, ...) reads 0.0 is in this context too: that query is not identified (S-3801)
	static const int baseBuild = ModelCondition::indexOf("BASE_BUILD");
	if (baseBuild >= 0 && obj.testModelCondition(baseBuild))
	{
		return true;
	}
	const GettingBuiltBehavior *gb = dynamic_cast<const GettingBuiltBehavior *>(obj.findModule("GettingBuiltBehavior"));
	if (!gb)
	{
		return false;
	}
	if (obj.isUnderConstruction())
	{
		return gb->rebuilding() || gb->forceComplete();
	}
	return gb->forceComplete();
}

int ControlBar::stanceSlotOf(const CommandButton &b, Object &obj)
{
	const StancesBehavior *sb = StancesBehavior::of(obj);
	if (!sb)
	{
		return -1;
	}
	for (size_t i = 0; i < b.m_stances.size(); ++i)
	{
		if (b.m_stances[i] == (int)sb->getStance())
		{
			return (int)i;
		}
	}
	return -1;
}

void ControlBar::populateUnderConstruction(Object &obj, bool mine)
{
	m_underConstruction = true;
	static const int noCancel = ObjectTemplateInfoBuilder::kindOfIndex("DONT_USE_CANCEL_BUILD_BUTTON");
	const CommandButton *b = m_ctx.commands ? m_ctx.commands->findCommandButton("Command_CancelConstruction") : nullptr;
	if (!b || (noCancel >= 0 && obj.isKindOf((unsigned)noCancel)))
	{
		return;
	}
	ControlBarButton cb;
	cb.slot = 0;
	cb.button = b;
	cb.state = mine ? ButtonState::Enabled : ButtonState::Restricted; // RW 0x945024 ff: winEnable(locally controlled)
	cb.image = b->m_buttonImageName.empty() ? std::string() : b->m_buttonImageName.front();
	cb.textLabel = b->m_textLabel.empty() ? std::string() : b->m_textLabel.front();
	cb.descriptLabel = b->m_descriptLabel.empty() ? std::string() : b->m_descriptLabel.front();
	cb.hotkey = hotkeyOf(cb.textLabel);
	place(cb, 0); // command window 0
}

void ControlBar::emitStateKey()
{
	// the version moves when the picture of the bar changes
	std::string key = std::to_string(m_source) + "|" + m_setName + (m_underConstruction ? "|uc" : "") + "|" + std::to_string(m_rangeStack.size()) + "|" + std::to_string(m_rangeStack.back().first) + "|" + std::to_string(m_money) + "|"
		+ std::to_string(m_cpUsed) + "/" + std::to_string(m_cpLimit);
	for (const auto *v : { &m_palantir, &m_side, &m_offBar, &m_fortressSide })
	{
		for (const ControlBarButton &b : *v)
		{
			key += std::string(v == &m_palantir ? "a" : (v == &m_side ? "s" : "o")) + std::to_string(b.position) + "|" + std::to_string(b.slot) + ":" + std::to_string((int)b.state) + ":" + std::to_string(b.queued) + ":" + std::to_string((int)(b.timer * 100.0f)) + ":" +
				std::to_string(b.rider) + ":" + b.image + (b.autoAbility ? ":auto" : "");
		}
	}
	if (key != m_lastKey)
	{
		m_lastKey = key;
		++m_version;
	}
}

ControlBar::Placement ControlBar::placementOf(const CommandButton &b, int slot, int rangeStart)
{
	Placement p;
	p.window = slot - rangeStart;                                                      // RW 0x943D6F: window i shows slot rangeStart + i
	p.arc = b.m_inPalantir && p.window >= 0 && p.window < kPalantirWindows;            // RW 0x92FF5C: CommandButton + 0x102
	p.side = b.m_radial;                                                               // RW 0x92F082: CommandButton + 0x101
	return p;
}

void ControlBar::place(ControlBarButton cb, int rangeStart)
{
	const Placement pl = cb.button ? placementOf(*cb.button, cb.slot, rangeStart) : Placement();
	const int window = pl.window;
	const bool arc = pl.arc, side = pl.side;
	if (arc)
	{
		ControlBarButton a = cb;
		a.inPalantir = true;
		a.position = window;
		m_palantir.push_back(a);
	}
	if (side)
	{
		ControlBarButton r = cb;
		r.inPalantir = false;
		r.position = (int)m_side.size();
		m_side.push_back(r);
	}
	if (!arc && !side)
	{
		cb.inPalantir = false;
		cb.position = -1;
		m_offBar.push_back(cb);
	}
}

const ControlBarButton *ControlBar::find(int slot, bool inPalantir) const
{
	if (!inPalantir) for (const auto &b : m_fortressSide) if (b.slot == slot) return &b;
	for (const ControlBarButton &b : inPalantir ? m_palantir : m_side)
	{
		if (b.slot == slot)
		{
			return &b;
		}
	}
	// lane INPUT-1 r2: a button off the bar (a horde's Attack Move / Stop) is pressed by its hotkey (RotWK registers every command window: ControlBar::setControlCommand RW 0x71CF3E -> RW 0x71D139)
	for (const ControlBarButton &b : m_offBar)
	{
		if (b.slot == slot && !inPalantir)
		{
			return &b;
		}
	}
	return nullptr;
}

std::vector<std::string> ControlBar::acceptanceStops()
{
	return {
		"[S-289] control bar placement (HUD-4, RW 0x943D6F / 0x92FF5C / 0x92F082): window i shows slot rangeStart + i; the Palantir arc shows windows 0 .. 5 whose "
		"button is InPalantir, the side command bar every Radial button in window order; a button in neither is off the bar as in retail (offBarButtons: Attack-Move / "
		"Stop / the SET_STANCE slots, reached by hotkey); the stance buttons: S-3803",
		"[S-290] control bar availability: the rules that need other lanes' ports (special power cooldown, science, castle / gate commands, weapon toggles, stances, hero revive, spell book, "
		"script status bits, disabled objects) are not ported: those buttons show as restricted and their presses are counted by ControlBar::unportedPresses()",
		"[S-1990] FIRE_WEAPON buttons (lane FACT-B: the archers' and siege engines' Bombard, the explosive mine's Deploy, the Mumakil's Charge): RotWK's "
		"availability (RW 0x942733 case 0x17: the weapon of WeaponSlot and its clip) and press (RW 0x940435 case 0x17: MSG_DO_WEAPON with WeaponSlot and "
		"MaxShotsToFire, a targeted one through the GUI command's issueFireWeaponCommand) are not ported, nor the logic's MSG_DO_WEAPON / _AT_LOCATION / "
		"_AT_OBJECT and the AI's attack-position state (S-325): the buttons show restricted",
		"[S-1991] BLOODTHIRSTY buttons (lane FACT-B: the Uruk and orc hordes' Bloodthirsty Sacrifice, NEED_TARGET_ALLY_OBJECT CONTEXTMODE_COMMAND): no "
		"availability rule (RW 0x942733 has no case 0x29: available), the target click's MSG_SACRIFICE (1056) and BloodthirstyUpdate are not ported: the "
		"buttons show restricted",
		"[S-1992] TOGGLE_NO_AUTO_ACQUIRE buttons (lane FACT-B: the catapult's and ballista's auto-acquire toggle): the press (RW 0x940435 case 0x30: "
		"MSG_TOGGLE_NO_AUTO_ACQUIRE 1115 with the object, then the button image RW 0x75D366) and the logic's flag are not ported: the buttons show restricted",
		"[S-1260] transport inventory (lane UI-1): the riders on a container's EXIT_CONTAINER buttons (RW 0x94251F), the press as MSG_EXIT (RW 0x940FEF), EVACUATE while "
		"someone is inside (RW 0x942733) and the garrison inventory of context 2 (RW 0x94518D) are ported; not reproduced: the hide on Object + 0x1C8 bit 5 (unidentified), "
		"retail's overrun of a rider's slots past the last EXIT_CONTAINER button, context 3 (contested garrisons, MSG_EVACUATE_CONTESTERS), the radial menu, the "
		"Create-A-Hero rider image",
		"[S-3460] ability autocast on the control bar (lane AUTOCAST-1): the right press (RW 0x9D28D4 -> processCommandUI RW 0x940435: MSG_DO_AUTO_ABILITY / "
		"MSG_DO_AUTO_ABILITY_WEAPON) and the glow (RW 0x9D2B49: SetAutoAbilityOverlayState) are ported for the Palantir arc; not ported: the toggle voice "
		"(processCommandUI RW 0x940805: voice 0x7E4 / 0x7E5, the button's Set / UnsetAutoAbilityUnitSound, through RW 0x8DEDBB: S-703), the multi-selection "
		"source (RW 0x94057A: the first selected object without + 0x10D bit 7; here the control bar's source object), the click sounds of the clip "
		"(Gui_PalantirCommandButtonClick / DisabledClick, RW 0x9D2941); INFERENCE: only the command button clips of the Palantir see right presses (retail "
		"posts every right press into the Apt, RW 0x83EE14, and its other handlers test TheAptWindowManager + 0x318 themselves; ours do not); the radial "
		"menu's right press keeps only its build cancel (HUD-6)",
		"[S-3801] under-construction context (lane PLAY-4, evaluateContextUI RW 0x71EBDA -> populateUnderConstruction RW 0x944EFB): a structure whose GettingBuiltBehavior "
		"rebuilds it or has + 0x36 set while UNDER_CONSTRUCTION (+ 0x36 alone when not), or with the model condition BASE_BUILD, shows only Command_CancelConstruction "
		"(not for DONT_USE_CANCEL_BUILD_BUTTON; enabled for its controller; MSG_DOZER_CANCEL_CONSTRUCT, RW 0x9409A1); not ported: the WALL_UPGRADE whose Object query "
		"RW 0x68C82D(3, ...) reads 0.0 (unidentified), the overlay windows (ControlBar + 0x29C / + 0x2A0), the radar override; the construction text and the rally "
		"point are the HUD's (DrawableIconUI)",
		"[S-3803] stance buttons (lane PLAY-4): SET_STANCE and TOGGLE_STANCE send MSG_CHANGE_STANCE with the stance (RW 0x96F53C), TOGGLE_STANCE is available with a "
		"StancesBehavior (RW 0x942A77) and shows the current stance's image and labels (StanceToButtonSlot); INFERENCE: retail opens the toggle's radial sub-menu "
		"(PalantirCommandUI::OnSubMenuLoaded RW 0x97004A / 0x96FDFD, not ported) and sends the picked stance; here a press sends the next stance of the button's list",
		"[S-3951] the right press's production cancel (lane HUD-7): a CANCELABLE UNIT_BUILD button cancels one of its template and a REVIVE button its hero "
		"record by index (processCommandUI RW 0x940B0E / 0x940C57, MSG_CANCEL_UNIT_CREATE); not ported: the Shift form that cancels them all (RW 0x940CB7 .. "
		"0x940CDE: Keyboard::isShift RW 0x63F122 while the template's field + 0x5F0 is 0, unidentified): the flag is always false here",
		"[S-4531] the left press's gate and click sound (lane NETLAG-1, read, not ported): RW 0x9D285E (BFME2 decomp Rva005C3427::Run, tier A) reads the "
		"command button clip's state (RW 0xA00C0E: clip + 0x50, set by RW 0x9D2BEE: 1 _disabled, 2 _extraDisabled, 3 _cantAfford, 4 _static, 5 _notReady, "
		"6 _up, 7 _visuallyEnabled) and for 1, 3 and 5 plays Gui_PalantirCommandButtonDisabledClick and drops the press; any other state goes to "
		"processCommandUI (RW 0x9D2814) with no sound of its own, so an enabled press sounds only through its clip. Here every press reaches "
		"ControlBar::pressButton, which refuses what it cannot do, silently",
	};
}
