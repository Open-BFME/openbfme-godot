// OpenBFME. GPL-3.0.
//
// AptSpellStore. See GameClient/GUI/AptScreens/AptSpellStore.h for the target facts. Lane SPELL-2.

#include "GameClient/GUI/AptScreens/AptSpellStore.h"

#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "Common/PlayerTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/LoadScreenInfo.h"

#include <cstdlib>
#include <cstring>

namespace
{
// RW 0x822903
int spellIndex(const std::string &arg)
{
	if (std::strncmp(arg.c_str(), "Spell", 5) != 0)
	{
		return -1;
	}
	return std::atoi(arg.c_str() + 5) - 1;
}
} // namespace

const char *AptSpellStore::stateName(int state)
{
	static const char *const kNames[] = { "_unused", "_disabled", "_disabled_level", "_already_purchased", "_purchased", "_active" }; // RW 0xC50BF8
	return state >= 0 && state <= 5 ? kNames[state] : "_unused";
}

AptSpellStore::AptSpellStore(WindowManager &windows, Shell &shell) : AptScreen(windows, shell, "SpellStore.apt", "AptSpellStore")
{
	for (int &s : m_state)
	{
		s = -1;
	}
	registerCommand("AptSpellStore::OnInitialized", [this](const std::string &) { // RW 0x82285B
		m_initialized = true;
		m_layout = -1;
		m_points = -1;
		m_help = -1;
		m_helpShown = false;
		m_helpSent = false;
		for (int &s : m_state)
		{
			s = -1;
		}
	});
	registerCommand("AptSpellStore::OnClosed", [this](const std::string &) { m_closing = true; }); // RW 0x822A80
	registerCommand("AptSpellStore::OnBttnClose", [this](const std::string &) { // RW 0x82289A: the movie's Close plays its fade, OnClosed follows
		if (!m_closing)
		{
			std::string error;
			this->windows().invokeASAt(this->level(), "", "Close", {}, nullptr, &error);
			m_closing = true;
		}
	});
	registerCommand("AptSpellStore::OnBttnReset", [this](const std::string &) { reset(); }); // RW 0x823484
	registerCommand("AptSpellStore::OnBttnSpell", [this](const std::string &arg) { click(spellIndex(arg)); }); // RW 0x8235A5
	registerCommand("AptSpellStore::OnRollOverBttnSpell", [this](const std::string &arg) { rollOver(spellIndex(arg)); }); // RW 0x82292E
	registerCommand("AptSpellStore::OnRollOutBttnSpell", [this](const std::string &arg) { rollOut(spellIndex(arg)); });   // RW 0x822954
	// RW 0x822974: the extern InputEnabled answers true outside a paused campaign (the game kind 6 / shell + 0x16 test: a skirmish answers true)
	registerProvider("AptSpellStore::InputEnabled", [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "1";
		}
		return true;
	});
}

AptSpellStore::~AptSpellStore() = default;

bool AptSpellStore::bind(const CommandStore &commands, GameLogic &logic, Player &player, const GameTextSource *gameText, std::string *error)
{
	m_logic = &logic;
	m_player = &player;
	m_text = gameText;
	if (!m_model.open(commands, logic, player, error))
	{
		return false;
	}
	// RW 0x822B4B .. 0x822BA9: the cost records of the 20 buttons (the images are read from the model by the HUD's RenderImage clips)
	for (const SpellStoreModel::Button &b : m_model.buttons())
	{
		windows().setAptText("APT:Spell" + std::to_string(b.index + 1) + "Cost", std::to_string(b.cost));
	}
	return true;
}

int AptSpellStore::computeLayout() const
{
	if (!m_player)
	{
		return 0;
	}
	if (m_player->science().mode().skirmishOrMultiplayer)
	{
		return 2; // RW 0x822DDE
	}
	const PlayerTemplate *pt = m_player->getPlayerTemplate();
	if (pt && pt->m_evil)
	{
		return pt->getName() == "FactionAngmar" ? 3 : 1; // RW 0x822E1A (a name compare in the binary)
	}
	return 0;
}

void AptSpellStore::rollOver(int index)
{
	if (index >= 0 && index < SpellStoreModel::MAX_BUTTONS)
	{
		m_help = index;
		m_helpSent = false; // RW 0x822949: + 0x359 = 0
	}
}

void AptSpellStore::rollOut(int index)
{
	if (index >= 0 && index < SpellStoreModel::MAX_BUTTONS)
	{
		m_help = -1;
	}
}

bool AptSpellStore::click(int index)
{
	if (m_closing || index < 0 || index >= SpellStoreModel::MAX_BUTTONS)
	{
		return false;
	}
	return m_model.click(index);
}

void AptSpellStore::reset()
{
	if (!m_closing)
	{
		m_model.reset();
	}
}

std::vector<GameMessage> AptSpellStore::takePurchases()
{
	std::vector<GameMessage> out = m_model.close(); // RW 0x8232ED
	m_player = nullptr;
	return out;
}

std::string AptSpellStore::imageForClip(const std::string &clipPath) const
{
	const std::string key = "SpellStore.Buttons.Spell"; // RW 0x822B23: "SpellStore/Buttons/Spell%d"
	const size_t at = clipPath.find(key);
	if (at == std::string::npos || !m_model.isOpen())
	{
		return std::string();
	}
	const int n = std::atoi(clipPath.c_str() + at + key.size()) - 1;
	for (const SpellStoreModel::Button &b : m_model.buttons())
	{
		if (b.index == n)
		{
			return b.image;
		}
	}
	return std::string();
}

void AptSpellStore::update()
{
	if (!m_initialized || !m_player || !m_model.isOpen() || m_closing)
	{
		return;
	}
	const int layout = computeLayout();
	if (layout != m_layout)
	{
		std::string error;
		const char *name = (layout == 2 || layout == 3) ? "_multiplayer" : layout == 0 ? "_campaignGood" : "_campaignEvil";
		windows().invokeASAt(level(), "", "SetLayout", { name }, nullptr, &error); // RW 0x822EB6
		m_layout = layout;
		return;
	}
	m_model.refresh();
	const bool helpOn = m_help >= 0;
	if (helpOn != m_helpShown)
	{
		std::string error;
		windows().invokeASAt(level(), "", "ShowSpellHelpText", { helpOn ? "_on" : "_off" }, nullptr, &error); // RW 0x822F0A
		m_helpShown = helpOn;
	}
	if (helpOn && !m_helpSent)
	{
		for (const SpellStoreModel::Button &b : m_model.buttons())
		{
			if (b.index != m_help || !b.button)
			{
				continue;
			}
			std::u16string label, desc, disabled;
			if (m_text && !b.button->m_textLabel.empty())
			{
				m_text->fetch(b.button->m_textLabel.front(), label);
			}
			if (m_text && !b.button->m_descriptLabel.empty())
			{
				m_text->fetch(b.button->m_descriptLabel.front(), desc);
			}
			if (b.state == SpellStoreModel::STATE_LOCKED && m_text && m_text->fetch("TOOLTIP:ScienceDisabled", disabled)) // RW 0x822FFE .. 0x82307D
			{
				desc += u"\n";
				desc += disabled;
			}
			windows().setAptText("APT:SpellHelpText", loadScreenU16ToUtf8(label));
			windows().setAptText("APT:SpellDescription", loadScreenU16ToUtf8(desc));
		}
		m_helpSent = true;
	}
	const int points = m_model.points();
	if (points != m_points)
	{
		windows().setAptText("APT:SpellStoreSpellPoints", std::to_string(points)); // RW 0x82316B
		m_points = points;
	}
	for (int i = 0; i < SpellStoreModel::MAX_BUTTONS; ++i) // RW 0x8230AF .. 0x823262
	{
		int st = 0;
		for (const SpellStoreModel::Button &b : m_model.buttons())
		{
			if (b.index != i || b.science == SCIENCE_INVALID)
			{
				continue;
			}
			switch (b.state)
			{
				case SpellStoreModel::STATE_PURCHASED:
				case SpellStoreModel::STATE_PENDING:
					st = m_state[i] == 5 ? 4 : 3;
					break;
				case SpellStoreModel::STATE_AVAILABLE:
					st = 5;
					break;
				default:
					st = 1;
					break;
			}
		}
		if (st != m_state[i])
		{
			std::string error;
			windows().invokeASAt(level(), "", "SetSpellButtonState", { std::to_string(i + 1), stateName(st) }, nullptr, &error); // RW 0x8229B5
			m_state[i] = st;
		}
	}
}
