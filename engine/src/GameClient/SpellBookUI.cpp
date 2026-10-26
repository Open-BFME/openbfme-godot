// OpenBFME. GPL-3.0.
//
// The spell book screens. See GameClient/SpellBookUI.h for the target facts. Lane SPELL-2.

#include "GameClient/SpellBookUI.h"

#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "Common/PlayerTemplate.h"
#include "Common/SpecialPower.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>

// RW 0xC50CF0: the purchase proxy the store's tests run against
struct SpellStoreModel::Proxy : public ScienceOwner
{
	const SpellStoreModel &m;
	explicit Proxy(const SpellStoreModel &model) : m(model) {}
	bool hasScience(ScienceType st) const override { return m.proxyHas(st); } // RW 0x822D97
	int getSciencePurchasePoints() const override { return m.points(); }    // RW 0x822843
};

bool SpellStoreModel::proxyHas(ScienceType st) const
{
	if (!m_player)
	{
		return false;
	}
	return m_player->science().hasScience(st) || std::find(m_pending.begin(), m_pending.end(), st) != m_pending.end();
}

int SpellStoreModel::points() const
{
	return m_player ? m_player->science().getSciencePurchasePoints() - m_pendingCost : 0;
}

bool SpellStoreModel::open(const CommandStore &commands, GameLogic &logic, Player &player, std::string *error)
{
	m_logic = &logic;
	m_commands = &commands;
	m_player = nullptr;
	m_buttons.clear();
	m_pending.clear();
	m_pendingButtons.clear();
	m_pendingCost = 0;
	const PlayerTemplate *pt = player.getPlayerTemplate();
	if (!pt)
	{
		if (error)
		{
			*error = "the player has no PlayerTemplate";
		}
		return false;
	}
	// S-923 (inference); lane MODS-3: an upgraded store under edain-unchained.spell-store-upgrade (its 0xED38ED at RW 0x822AD0)
	m_setName = logic.players().templates().purchaseScienceCommandSetFor(player, &commands);
	const CommandSet *set = commands.findCommandSet(m_setName);
	if (!set)
	{
		if (error)
		{
			*error = "the store's command set '" + m_setName + "' is unknown";
		}
		return false;
	}
	m_player = &player;
	for (int i = 0; i < MAX_BUTTONS; ++i) // RW 0x822AE0 .. 0x822B9E
	{
		const CommandButton *b = set->getCommandButton(i);
		if (!b)
		{
			continue;
		}
		Button out;
		out.index = i;
		out.button = b;
		if (!b->m_science.empty() && TheScienceStore)
		{
			out.scienceName = b->m_science.front(); // button + 0xA4: the first science
			out.science = TheScienceStore->getScienceFromInternalName(out.scienceName);
			out.cost = TheScienceStore->getSciencePurchaseCost(out.science, player.science().mode().skirmishOrMultiplayer); // RW 0x5FEC64
		}
		out.image = b->m_buttonImageName.empty() ? std::string() : b->m_buttonImageName.front();
		out.label = b->m_textLabel.empty() ? std::string() : b->m_textLabel.front();
		m_buttons.push_back(out);
	}
	// the connectors: a button's parents are the buttons of the sciences its prerequisite groups name (display only)
	for (Button &b : m_buttons)
	{
		const ScienceInfo *info = TheScienceStore ? TheScienceStore->findScienceInfo(b.science) : nullptr;
		if (!info)
		{
			continue;
		}
		for (const ScienceVec &group : info->m_prereqSciences)
		{
			for (ScienceType st : group)
			{
				for (const Button &other : m_buttons)
				{
					if (other.science == st && std::find(b.parents.begin(), b.parents.end(), other.index) == b.parents.end())
					{
						b.parents.push_back(other.index);
					}
				}
			}
		}
	}
	refresh();
	return true;
}

void SpellStoreModel::refresh()
{
	if (!m_player || !TheScienceStore)
	{
		return;
	}
	const Proxy proxy(*this);
	const bool mp = m_player->science().mode().skirmishOrMultiplayer;
	for (Button &b : m_buttons)
	{
		if (m_player->science().hasScience(b.science))
		{
			b.state = STATE_PURCHASED;
		}
		else if (std::find(m_pending.begin(), m_pending.end(), b.science) != m_pending.end())
		{
			b.state = STATE_PENDING;
		}
		else if (b.science != SCIENCE_INVALID && !m_player->isScienceDisabled(b.science) && !m_player->isScienceHidden(b.science)
			&& TheScienceStore->playerHasRootPrereqsAndCanPurchase(proxy, b.science, mp)) // RW 0x8230AE: a script's disabled / hidden science is locked
		{
			b.state = STATE_AVAILABLE;
		}
		else
		{
			b.state = STATE_LOCKED;
		}
	}
}

bool SpellStoreModel::click(int index)
{
	if (!m_player || !TheScienceStore || index < 0 || index >= MAX_BUTTONS)
	{
		return false;
	}
	const Button *b = nullptr;
	for (const Button &x : m_buttons)
	{
		if (x.index == index)
		{
			b = &x;
		}
	}
	if (!b || b->science == SCIENCE_INVALID)
	{
		return false;
	}
	const Proxy proxy(*this);
	if (proxy.hasScience(b->science) || m_player->isScienceDisabled(b->science) || m_player->isScienceHidden(b->science)
		|| !TheScienceStore->playerHasRootPrereqsAndCanPurchase(proxy, b->science, m_player->science().mode().skirmishOrMultiplayer))
	{
		refresh();
		return false;
	}
	// RW 0x82355F: appended, the pending cost grows (the click sound RW 0x90BE00 is the HUD's)
	m_pending.push_back(b->science);
	m_pendingButtons.push_back(b->button);
	m_pendingCost += TheScienceStore->getSciencePurchaseCost(b->science, m_player->science().mode().skirmishOrMultiplayer);
	refresh();
	return true;
}

void SpellStoreModel::reset()
{
	m_pending.clear();
	m_pendingButtons.clear();
	m_pendingCost = 0;
	refresh();
}

std::vector<GameMessage> SpellStoreModel::close()
{
	std::vector<GameMessage> out;
	if (m_player)
	{
		const int pi = m_player->getPlayerIndex();
		for (size_t i = 0; i < m_pending.size(); ++i) // RW 0x8232ED -> 0x940435, case 0x19
		{
			GameMessage m(MSG_PURCHASE_SCIENCE, pi);
			m.appendIntegerArgument(pi);
			m.appendIntegerArgument(m_pending[i]);
			out.push_back(m);
			for (const std::string &name : m_pendingButtons[i]->m_commandTrigger)
			{
				const CommandButton *trigger = m_commands->findCommandButton(name);
				const SpecialPowerTemplate *power = trigger && TheSpecialPowerStore
					? TheSpecialPowerStore->findSpecialPowerTemplate(trigger->m_specialPowerName) : nullptr;
				if (!power)
				{
					// retail dereferences the missing button / power (RW 0x940435 case 0x19): reported by name, that cast is not sent (lane DEVPORT-7)
					const std::string e = "spell store: CommandTrigger " + name + " of " + m_pendingButtons[i]->m_name + " names no special power button";
					if (std::find(m_errors.begin(), m_errors.end(), e) == m_errors.end())
					{
						m_errors.push_back(e);
					}
					continue;
				}
				GameMessage cast(MSG_DO_SPELLBOOK_SPECIAL_POWER, pi);
				cast.appendIntegerArgument((int)power->getID());
				cast.appendIntegerArgument((int)trigger->m_options);
				cast.appendObjectIDArgument(INVALID_ID);
				out.push_back(cast);
			}
		}
	}
	m_pending.clear();
	m_pendingButtons.clear();
	m_pendingCost = 0;
	m_player = nullptr;
	m_commands = nullptr;
	m_buttons.clear();
	return out;
}

// ---- InGameSpellBookModel --------------------------------------------------------------------------------------------------------------------
bool InGameSpellBookModel::refresh(const CommandStore &commands, GameLogic &logic, Player &player)
{
	m_buttons.clear();
	Object *book = SpecialPowerModules::findSpellBookObject(logic, player); // a display read: the id is not cached
	m_book = book ? book->getID() : (ObjectID)INVALID_ID;
	if (!book)
	{
		m_targeting = -1;
		return false;
	}
	const CommandSet *set = commands.findCommandSet(book->getCommandSetName());
	if (!set)
	{
		m_targeting = -1;
		return false;
	}
	for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		if (!b || b->m_specialPowerName.empty() || !TheSpecialPowerStore)
		{
			continue;
		}
		Button out;
		out.index = i;
		out.button = b;
		out.power = TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName);
		out.image = b->m_buttonImageName.empty() ? std::string() : b->m_buttonImageName.front();
		out.needsPosition = (b->m_options & COMMAND_OPTION_NEED_TARGET_POS) != 0;
		out.radiusCursor = b->m_radiusCursor;
		if (out.power)
		{
			out.radius = out.power->getRadiusCursorRadius();
			out.owned = out.power->getRequiredSciences().empty();
			for (ScienceType st : out.power->getRequiredSciences())
			{
				out.owned = out.owned || player.science().hasScience(st);
			}
			if (SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*book, out.power))
			{
				out.ready = sp->isReadyForDisplay();
				out.percent = sp->getPercentReadyForDisplay();
				out.usable = out.owned && SpecialPowerModules::canUseSpecialPower(*book, out.power);
			}
		}
		m_buttons.push_back(out);
	}
	if (m_targeting >= 0 && !targetButton())
	{
		m_targeting = -1;
	}
	return true;
}

const InGameSpellBookModel::Button *InGameSpellBookModel::targetButton() const
{
	for (const Button &b : m_buttons)
	{
		if (b.index == m_targeting)
		{
			return &b;
		}
	}
	return nullptr;
}

bool InGameSpellBookModel::press(int index, int playerIndex, std::vector<GameMessage> &out)
{
	for (const Button &b : m_buttons)
	{
		if (b.index != index)
		{
			continue;
		}
		if (!b.power || !b.owned || !b.usable || !b.ready)
		{
			return false;
		}
		if (b.needsPosition)
		{
			m_targeting = index;
			return true;
		}
		GameMessage m(MSG_DO_SPECIAL_POWER, playerIndex); // { power id, options, source (the book) } (SpellCommands kind 0)
		m.appendIntegerArgument((int)b.power->getID());
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(m_book);
		out.push_back(m);
		return true;
	}
	return false;
}

InGameSpellBookModel::Target InGameSpellBookModel::targetVerdict(GameLogic &logic, const Coord3D &where) const
{
	// lane SPELL-4: CommandTranslator's SPELL_BOOK case (RW 0x81EA88): the local player's spell book (RW 0x6AD0D9) and RW 0x6A4260(button, no object,
	// the location, 0, options, book) -> RW 0x82DFB7(book, location, 0, power, 0, options, 1): with the last argument 1 the book must have the power
	// (RW 0x68DF46) and its module must be 100% ready (vslot 8 >= 1.0), then the location tests of ActionManager
	const Button *b = targetButton();
	Object *book = logic.findObjectByID(m_book);
	if (!b || !b->power || !b->button || !book || !b->ready)
	{
		return Target::INVALID;
	}
	bool unported = false;
	if (ActionManager::canDoSpecialPowerAtLocation(*book, where, b->power, b->button->m_options, &unported))
	{
		return Target::VALID;
	}
	return unported ? Target::UNPORTED : Target::INVALID;
}

bool InGameSpellBookModel::clickWorld(const Coord3D &where, int playerIndex, std::vector<GameMessage> &out)
{
	const Button *b = targetButton();
	if (!b || !b->power)
	{
		m_targeting = -1;
		return false;
	}
	GameMessage m(MSG_DO_SPECIAL_POWER_AT_LOCATION, playerIndex);
	m.appendIntegerArgument((int)b->power->getID());
	m.appendLocationArgument(where);
	m.appendObjectIDArgument(INVALID_ID);
	m.appendIntegerArgument(0);
	m.appendObjectIDArgument(m_book);
	out.push_back(m);
	m_targeting = -1;
	return true;
}
