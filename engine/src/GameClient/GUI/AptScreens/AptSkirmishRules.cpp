// OpenBFME. GPL-3.0.
//
// lane RULES-1: the Rules tab of the lobby movie (MpGameSetup.apt's rule rows): RotWK's AptMpGameRules, the panel at AptMpGameSetup + 0xD0.
//
// TARGET FACTS (rotwk201_game.exe, caveat S-001; every function below is tier A of its BFME2 1.06 twin unless marked, donor open-bfme-2
// GUI/GUICallbacks/Apt/AptMpGameRulesCallbacks.cpp and AptMpGameRulesRefreshWidgets.cpp, byte-matched; the two Populate functions are the decomp's
// recovered (not yet byte-matching) reverse/attempts/0x0057e7a1.cpp and 0x0057eaad.cpp):
//   * the registration RW 0x987106 (tier B of BFME2 0x97F0AA: RotWK appends a tail, no change to the bindings): every "APT:RuleComboBox_<n>" and
//     "APT:RuleCheckBox_<n>" text (n < 10) becomes " ", the rules get the mode's defaults (RW 0x960A74), the widgets are filed again, then the bindings
//     MpGameRules::NumCheckBoxes / NumComboBoxes (RW 0x9865C9 queries 1 / 0), AptMpGameRules::Reset and the screen reference
//     AptMpGameRules::InitGadgets;
//   * InitGadgets RW 0x987092: the index after '_' of "RuleComboBox_<n>" / "RuleCheckBox_<n>" (strstr), any other name is ignored; the widget is filed at
//     atoi(index) (the list grows to it) and HIDDEN (RW 0x986F.. 0x57EF46 twin); populated (+ 0x89) -> the windows are forgotten and filed again
//     (0x57EF18 twin) else the populate test (0x57ED2B twin);
//   * the populate test: both lists hold at least the mode's counts -> PopulateComboBoxes (RW 0x986B1C), PopulateCheckBoxes (RW 0x986810), populated,
//     then the enable pass (0x57E97A twin: every rule window enabled when the game's host test holds and its state is 1);
//   * PopulateComboBoxes: for each combo of the mode (skipping a rule in state 3 and a missing widget) the combo is reset, the rule's widget is the next
//     row's (a rule that already has one is skipped), the row's text "APT:RuleComboBox_<row>" is the rule's name (RW 0x960B14), every choice is added
//     (the label's text formatted with the value, + "VALUE:Default" for the default index; item data = the value), state 1, shown, UpdateRuleGadget;
//     the rows left over are hidden with the text " ". PopulateCheckBoxes is the same for the check rows ("APT:RuleCheckBox_<row>"; GlobalData + 0x9D4
//     & 3, the demo mode (Common/MpGameRules.h: 0 in RotWK 2.01, MpGameRules::kDemoMode), makes rules 0 / 1 state 2 and their boxes disabled);
//   * UpdateRuleGadget RW 0x986624: input gate off, a combo (style 0x8000) selects the entry whose item data is the rule's value and its drop down is
//     disabled unless this game's host; a check box (style 4) is checked when the value is not 0; input gate on;
//   * the message handler RW 0x986776: with the gate on, a message 0x4008 (GBM_SELECTED) or 0x4026 (BFME's combo selected) of a rule widget stores
//     GetGadgetValue (RW 0x9866D9: the selected item data, or IsChecked) and calls ruleChanged(rule, false);
//   * Reset RW 0x98674C: the mode's defaults, ruleChanged(10, true), every widget refreshed;
//   * ruleChanged = the panel's RW 0x8421E0 (tier B of BFME2 0x43FC1F, RotWK adds kind 5): kind 0 -> refresh the slots (+0x2B9), 1 -> slots and
//     players (+0x2BF, +0x2B9), 3 -> MessageBoxOk("RULE:CommandPointFactor", "APT:CommandPointsTooHigh") when the panel's factor (+0x168) > 100 (0x64),
//     5 -> +0x2BD, 7 -> +0x2C1, 10 -> +0x2C1, +0x2BF, +0x2B9; then on the host: the rules are copied into the game (RW 0x784208), the game's vslot 0x38,
//     every slot's ready icon, the owner's vslot 2, and the notice "GUI:RuleChangeWarning" + " " + the rule's name (10 with reset: "GUI:RuleResetWarning");
//   * MpGameSetup::OnTabSelect "RulesTab" refreshes the widgets.
// OpenBFME: one more combo row after RotWK's in mode 0, "Legendary Heroes (OpenBFME rule)": Off / 2x / 3x / 4x (SkirmishGameSetup::setLegendaryHeroes);
// Reset puts it back to Off.
// INFERENCE (S-3600): the movie's generated rows read MpGameRules::NumComboBoxes: the extra row is a generated row like retail's (the movie is not
// changed); the drop-down disable of a non-host (GadgetComboBoxHideDropDown) is the widget's disable here.

#include "GameClient/GUI/AptScreens/AptSkirmish.h"

#include "GameClient/GUI/AptMessageBox.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"

#include <cstdlib>
#include <cstring>

namespace
{
std::string toUtf8(const std::u16string &s)
{
	std::string out;
	for (char16_t c16 : s)
	{
		const std::uint32_t c = c16;
		if (c < 0x80)
		{
			out.push_back((char)c);
		}
		else if (c < 0x800)
		{
			out.push_back((char)(0xC0 | (c >> 6)));
			out.push_back((char)(0x80 | (c & 0x3F)));
		}
		else
		{
			out.push_back((char)(0xE0 | (c >> 12)));
			out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (c & 0x3F)));
		}
	}
	return out;
}

void fillRuleCombo(GameWindow *combo, const std::vector<SkirmishComboEntry> &entries)
{
	GadgetComboBoxReset(combo);
	for (const SkirmishComboEntry &e : entries)
	{
		const int at = GadgetComboBoxAddEntry(combo, e.text, e.color);
		GadgetComboBoxSetItemData(combo, at, reinterpret_cast<void *>(static_cast<std::intptr_t>(e.itemData)));
	}
}

int comboValue(GameWindow *combo)
{
	int selected = -1;
	GadgetComboBoxGetSelectedPos(combo, &selected);
	return selected >= 0 ? static_cast<int>(reinterpret_cast<std::intptr_t>(GadgetComboBoxGetItemData(combo, selected))) : -1; // RW 0x9866D9
}

void selectComboValue(GameWindow *combo, int value)
{
	const int n = GadgetComboBoxGetLength(combo);
	for (int i = 0; i < n; ++i)
	{
		if (static_cast<int>(reinterpret_cast<std::intptr_t>(GadgetComboBoxGetItemData(combo, i))) == value)
		{
			GadgetComboBoxSetSelectedPos(combo, i, true); // RW 0x986624: the first entry of that item data; none: the selection stays
			return;
		}
	}
}

bool isCombo(GameWindow *w) { return (w->winGetStyle() & GWS_COMBO_BOX) != 0; }
bool isCheck(GameWindow *w) { return (w->winGetStyle() & GWS_CHECK_BOX) != 0; }
} // namespace

int AptSkirmish::ruleComboCount() const
{
	if (!m_setup)
	{
		return 0;
	}
	const int mode = m_setup->rulesMode();
	return MpGameRules::numComboBoxes(mode) + (mode == RULES_MODE_SKIRMISH ? 1 : 0);
}

void AptSkirmish::initRuleGadget(const std::string &name, GameWindow *window)
{
	// RW 0x987092
	const std::size_t underscore = name.find('_');
	if (!window || underscore == std::string::npos)
	{
		return;
	}
	std::vector<GameWindow *> *widgets = nullptr;
	if (name.find("RuleComboBox_") != std::string::npos)
	{
		widgets = &m_ruleCombos;
	}
	else if (name.find("RuleCheckBox_") != std::string::npos)
	{
		widgets = &m_ruleChecks;
	}
	else
	{
		return;
	}
	const int position = std::atoi(name.c_str() + underscore + 1);
	if (position < 0)
	{
		return;
	}
	if (widgets->size() <= (std::size_t)position)
	{
		widgets->resize((std::size_t)position + 1, nullptr);
	}
	(*widgets)[(std::size_t)position] = window;
	window->winHide(true);
	if (m_rulesPopulated)
	{
		refileRules();
	}
	else
	{
		tryPopulateRules();
	}
}

void AptSkirmish::refileRules()
{
	m_rulesPopulated = false;
	m_ruleWindows.fill(nullptr);
	m_legendaryCombo = nullptr;
	tryPopulateRules();
}

bool AptSkirmish::tryPopulateRules()
{
	if (m_rulesPopulated)
	{
		return true;
	}
	if (!m_setup)
	{
		return false;
	}
	const int mode = m_setup->rulesMode();
	if ((int)m_ruleCombos.size() < ruleComboCount() || (int)m_ruleChecks.size() < MpGameRules::numCheckBoxes(mode))
	{
		return false;
	}
	// the states: 1 for every rule a row may show; ClanGame (rule 1) is state 3 outside online play (RW 0x441ECE's BFME2 twin: the panel's
	// rva0057E9ED(1, 3) unless the setup's flag 0x40)
	m_ruleStates.fill(0);
	if ((setupFlags() & 0x40) == 0) // lane ONLINE-5: the flag is the screen's (online 0x200001F0 has it: the online staging room shows Clan Game)
	{
		m_ruleStates[RULE_CLAN_GAME] = 3;
	}
	populateRuleCombos();
	populateRuleChecks();
	m_rulesPopulated = true;
	enableRuleGadgets();
	if (!m_rulesStopReported)
	{
		m_rulesStopReported = true;
		windows().note("rules-unverified", kRulesStop);
	}
	return true;
}

const char *const AptSkirmish::kRulesStop =
	"[S-3600] rule rows: a non-host's drop-down disable (GadgetComboBoxHideDropDown) is the widget's disable, the skirmish owner's rule notice (slot 17) "
	"is not shown, the Legendary Heroes row is one more generated row (lane UI-6: the last lobby's rules are kept with its game, S-3602 closed)";

void AptSkirmish::populateRuleCombos()
{
	// RW 0x986B1C
	const int mode = m_setup->rulesMode();
	const int count = MpGameRules::numComboBoxes(mode);
	const MpRuleCombo *const *combos = MpGameRules::comboBoxes(mode);
	m_ruleInput = false;
	int row = 0;
	for (int i = 0; i < count; ++i)
	{
		const MpRuleCombo *cur = combos[i];
		if (!cur || m_ruleStates[(std::size_t)cur->id] == 3)
		{
			continue;
		}
		if ((std::size_t)i >= m_ruleCombos.size())
		{
			break;
		}
		GameWindow *window = m_ruleCombos[(std::size_t)row];
		if (!window)
		{
			continue;
		}
		GadgetComboBoxReset(window);
		if (m_ruleWindows[(std::size_t)cur->id])
		{
			continue;
		}
		m_ruleWindows[(std::size_t)cur->id] = window;
		windows().setAptText("APT:RuleComboBox_" + std::to_string(row++), toUtf8(m_setup->ruleName(cur->id)));
		fillRuleCombo(window, m_setup->ruleComboEntries(*cur));
		m_ruleStates[(std::size_t)cur->id] = 1;
		window->winHide(false);
		updateRuleGadget(cur->id);
	}
	// OpenBFME: the Legendary Heroes row follows RotWK's rows (mode 0 only)
	if (mode == RULES_MODE_SKIRMISH && (std::size_t)row < m_ruleCombos.size() && m_ruleCombos[(std::size_t)row])
	{
		GameWindow *window = m_ruleCombos[(std::size_t)row];
		m_legendaryCombo = window;
		windows().setAptText("APT:RuleComboBox_" + std::to_string(row++), toUtf8(LegendaryHeroes::label()));
		fillRuleCombo(window, m_setup->legendaryHeroesEntries());
		window->winHide(false);
		updateLegendaryGadget();
	}
	const int rows = ruleComboCount();
	while (row < rows && (std::size_t)row < m_ruleCombos.size())
	{
		GameWindow *window = m_ruleCombos[(std::size_t)row];
		if (!window)
		{
			break;
		}
		window->winHide(true);
		windows().setAptText("APT:RuleComboBox_" + std::to_string(row++), " ");
	}
	m_ruleInput = true;
}

void AptSkirmish::populateRuleChecks()
{
	// RW 0x986810
	const int mode = m_setup->rulesMode();
	const int count = MpGameRules::numCheckBoxes(mode);
	const MpRuleCheck *const *checks = MpGameRules::checkBoxes(mode);
	m_ruleInput = false;
	int row = 0;
	for (int i = 0; i < count; ++i)
	{
		const MpRuleCheck *cur = checks[i];
		if (!cur || m_ruleStates[(std::size_t)cur->id] == 3)
		{
			continue;
		}
		if ((std::size_t)row >= m_ruleChecks.size())
		{
			break;
		}
		GameWindow *window = m_ruleChecks[(std::size_t)row];
		if (!window)
		{
			continue;
		}
		if (m_ruleWindows[(std::size_t)cur->id])
		{
			break; // the retail body returns here (the attempt's `return`)
		}
		m_ruleWindows[(std::size_t)cur->id] = window;
		windows().setAptText("APT:RuleCheckBox_" + std::to_string(row++), toUtf8(m_setup->ruleName(cur->id)));
		updateRuleGadget(cur->id);
		m_ruleStates[(std::size_t)cur->id] = 1;
		window->winHide(false);
		if (MpGameRules::demoRestrictsRules(MpGameRules::kDemoMode) && cur->id >= 0 && cur->id <= 1)
		{
			m_ruleStates[(std::size_t)cur->id] = 2; // RW 0x986912: the demo mode
			window->winEnable(false);
		}
	}
	while (row < count && (std::size_t)row < m_ruleChecks.size())
	{
		GameWindow *window = m_ruleChecks[(std::size_t)row];
		if (!window)
		{
			break;
		}
		window->winHide(true);
		windows().setAptText("APT:RuleCheckBox_" + std::to_string(row++), " ");
	}
	m_ruleInput = true;
}

void AptSkirmish::enableRuleGadgets()
{
	// the 0x57E97A twin: a window is enabled when the game exists, its host test holds and the rule's state is 1
	const bool host = m_setup && amIHost();
	m_ruleInput = false;
	for (std::size_t rule = 0; rule < m_ruleWindows.size(); ++rule)
	{
		if (GameWindow *w = m_ruleWindows[rule])
		{
			w->winEnable(host && m_ruleStates[rule] == 1);
		}
	}
	if (m_legendaryCombo)
	{
		m_legendaryCombo->winEnable(host);
	}
	m_ruleInput = true;
}

void AptSkirmish::updateRuleGadget(int rule)
{
	// RW 0x986624
	GameWindow *window = ruleWindow(rule);
	if (!window || !m_setup)
	{
		return;
	}
	const int value = m_setup->info().rules[(std::size_t)rule];
	m_ruleInput = false;
	if (isCombo(window))
	{
		selectComboValue(window, value);
		window->winEnable(amIHost() && m_ruleStates[(std::size_t)rule] == 1); // GadgetComboBoxHideDropDown(!host) [S-3600]
	}
	else if (isCheck(window))
	{
		GadgetCheckBoxSetChecked(window, value != 0);
	}
	m_ruleInput = true;
}

void AptSkirmish::updateLegendaryGadget()
{
	if (!m_legendaryCombo || !m_setup)
	{
		return;
	}
	m_ruleInput = false;
	selectComboValue(m_legendaryCombo, m_setup->info().legendaryHeroes);
	m_legendaryCombo->winEnable(amIHost());
	m_ruleInput = true;
}

void AptSkirmish::refreshRuleGadgets()
{
	// AptMpGameRules::rva0057E6C1: every rule's UpdateRuleGadget
	for (int rule = 0; rule < NUM_GAME_RULES; ++rule)
	{
		updateRuleGadget(rule);
	}
	updateLegendaryGadget();
}

bool AptSkirmish::onRuleGadget(GameWindow *gadget)
{
	// RW 0x986776
	if (!gadget || !m_setup)
	{
		return false;
	}
	if (gadget == m_legendaryCombo)
	{
		if (m_ruleInput && !m_refreshing)
		{
			if (!amIHost() || !m_setup->setLegendaryHeroes(comboValue(gadget)))
			{
				updateLegendaryGadget(); // a client's or an unknown choice shows the model again
				return true;
			}
			ruleChanged(-1, false);
		}
		return true;
	}
	int rule = -1;
	for (std::size_t i = 0; i < m_ruleWindows.size(); ++i)
	{
		if (m_ruleWindows[i] == gadget)
		{
			rule = (int)i;
			break;
		}
	}
	if (rule < 0)
	{
		return false;
	}
	if (!m_ruleInput || m_refreshing)
	{
		return true; // the gate is off: the message is the panel's own update (retail: handled, nothing stored)
	}
	if (!amIHost())
	{
		updateRuleGadget(rule);
		return true;
	}
	const int value = isCombo(gadget) ? comboValue(gadget) : (isCheck(gadget) ? (GadgetCheckBoxIsChecked(gadget) ? 1 : 0) : -1); // RW 0x9866D9
	m_setup->setRule(rule, value);
	ruleChanged(rule, false);
	return true;
}

void AptSkirmish::ruleChanged(int kind, bool reset)
{
	// RW 0x8421E0
	if (!m_setup)
	{
		return;
	}
	if (kind == RULE_COMMAND_POINT_FACTOR && m_setup->info().rules[RULE_COMMAND_POINT_FACTOR] > 100)
	{
		showRuleWarning(fetchOrMissing(m_env.gameText, "RULE:CommandPointFactor"), fetchOrMissing(m_env.gameText, "APT:CommandPointsTooHigh"));
	}
	if (!amIHost())
	{
		return;
	}
	// the slots follow (kind 0: the hero combos, RW 0x842A85; 10: everything) and the LAN lobby sends the options on
	refreshAll();
	std::u16string notice;
	if (kind == 10)
	{
		if (reset)
		{
			notice = fetchOrMissing(m_env.gameText, "GUI:RuleResetWarning");
		}
	}
	else
	{
		notice = fetchOrMissing(m_env.gameText, "GUI:RuleChangeWarning") + u" " + (kind < 0 ? LegendaryHeroes::label() : m_setup->ruleName(kind));
	}
	if (!notice.empty())
	{
		ruleNotice(notice);
	}
	lobbyChanged();
}

void AptSkirmish::resetRules()
{
	// RW 0x98674C
	if (!m_setup || !amIHost())
	{
		return;
	}
	m_setup->resetRules();
	ruleChanged(10, true);
	refreshRuleGadgets();
}

void AptSkirmish::showRuleWarning(const std::u16string &title, const std::u16string &text)
{
	m_ruleWarnings.emplace_back(title, text);
	if (!m_ruleBox)
	{
		m_ruleBox = std::make_unique<AptMessageBox>(windows(), shell());
	}
	m_ruleBox->show(AptMessageBox::TYPE_OK, title, text);
}

void AptSkirmish::pruneRuleGadgets(const std::set<const GameWindow *> &alive)
{
	bool lost = false;
	auto check = [&](GameWindow *&w) {
		if (w && !alive.count(w))
		{
			w = nullptr;
			lost = true;
		}
	};
	for (GameWindow *&w : m_ruleCombos)
	{
		check(w);
	}
	for (GameWindow *&w : m_ruleChecks)
	{
		check(w);
	}
	for (GameWindow *&w : m_ruleWindows)
	{
		check(w);
	}
	check(m_legendaryCombo);
	if (lost)
	{
		// the movie took the rows away (a tab or a frame change): the rules are filed again when it places them anew
		m_rulesPopulated = false;
		m_ruleWindows.fill(nullptr);
		m_legendaryCombo = nullptr;
	}
}
