// OpenBFME. GPL-3.0.
// See ControlBar.h.

#include "GameClient/ControlBar.h"
#include "Common/Upgrade.h"

#include "Common/BuildAssistant.h"
#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/HudObjects.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
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

#include <algorithm>

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
			if (pu && pu->getProductionCount() >= pu->maxQueueEntries())
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
		case GUI_COMMAND_TOGGLE_WEAPONSET:
			// lane HUD-4: RW 0x942FC6: restricted while a non-horde contain of the object holds someone, else available
			return WeaponSetToggle::isRestrictedByContain(obj) ? ButtonState::Restricted : ButtonState::Enabled;
		case GUI_COMMAND_EVACUATE:
		{
			// lane UI-1: RW 0x942733 case 0x11: available when the contain holds someone (contain slot 0x114 getContainCount), else restricted
			ContainModuleInterface *c = obj.getContain();
			return c && c->getContainCount() != 0 ? ButtonState::Enabled : ButtonState::Restricted;
		}
		case GUI_COMMAND_EXIT_CONTAINER:
			// lane UI-1: shown disabled; the transport inventory (doTransportInventoryUI, RW 0x94251F) enables the slots of the riders
			return ButtonState::Restricted;
		case GUI_COMMAND_OBJECT_UPGRADE:
		case GUI_COMMAND_PLAYER_UPGRADE:
		{
			// shown, restricted once the object or player has it (the rest of the UI rules of RW 0x942733 is S-290's); a button whose Upgrade is not an upgrade
			// holds no template in RW (S-200: resolved to NULL), so it is restricted
			const UpgradeTemplate *u = TheUpgradeCenter && !b.m_upgradeName.empty() ? TheUpgradeCenter->findUpgrade(b.m_upgradeName) : nullptr;
			if (!u || obj.hasUpgrade(u) || (player && player->hasUpgradeComplete(u)))
			{
				return ButtonState::Restricted;
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
		default:
			return ButtonState::Restricted;
	}
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
	m_offBar.clear();
	m_queue.clear();
	m_portrait.clear();
	m_displayName.clear();
	if (obj)
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

void ControlBar::emitStateKey()
{
	// the version moves when the picture of the bar changes
	std::string key = std::to_string(m_source) + "|" + m_setName + "|" + std::to_string(m_rangeStack.size()) + "|" + std::to_string(m_rangeStack.back().first) + "|" + std::to_string(m_money) + "|"
		+ std::to_string(m_cpUsed) + "/" + std::to_string(m_cpLimit);
	for (const auto *v : { &m_palantir, &m_side, &m_offBar })
	{
		for (const ControlBarButton &b : *v)
		{
			key += std::string(v == &m_palantir ? "a" : (v == &m_side ? "s" : "o")) + std::to_string(b.position) + "|" + std::to_string(b.slot) + ":" + std::to_string((int)b.state) + ":" + std::to_string(b.queued) + ":" + std::to_string((int)(b.timer * 100.0f)) + ":" +
				std::to_string(b.rider) + ":" + b.image;
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
	for (const ControlBarButton &b : inPalantir ? m_palantir : m_side)
	{
		if (b.slot == slot)
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
		"Stop / the SET_STANCE slots, reached by hotkey); not ported: the TOGGLE_STANCE radial sub-menu (PalantirCommandUI::OnSubMenuLoaded, RW 0x97004A / 0x96FDFD, "
		"its stance presses RW 0x96F53C): the stance button's press is counted unported, the stances are reached by the STANCE_* hotkeys",
		"[S-290] control bar availability: the rules that need other lanes' ports (special power cooldown, science, castle / gate commands, weapon toggles, stances, hero revive, spell book, "
		"script status bits, disabled objects) are not ported: those buttons show as restricted and their presses are counted by ControlBar::unportedPresses()",
		"[S-1260] transport inventory (lane UI-1): the riders on a container's EXIT_CONTAINER buttons (RW 0x94251F), the press as MSG_EXIT (RW 0x940FEF), EVACUATE while "
		"someone is inside (RW 0x942733) and the garrison inventory of context 2 (RW 0x94518D) are ported; not reproduced: the hide on Object + 0x1C8 bit 5 (unidentified), "
		"retail's overrun of a rider's slots past the last EXIT_CONTAINER button, context 3 (contested garrisons, MSG_EVACUATE_CONTESTERS), the radial menu, the "
		"Create-A-Hero rider image",
	};
}
