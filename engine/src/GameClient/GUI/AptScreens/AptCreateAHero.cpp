// OpenBFME. GPL-3.0.
// The Create-a-Hero builder screen (lane CAH-1). See GameClient/GUI/AptScreens/AptCreateAHero.h for the target facts.

#include "GameClient/GUI/AptScreens/AptCreateAHero.h"
#include "GameClient/GUI/AptMessageBox.h"

#include "GameClient/ControlBarCommands.h"
#include "GameClient/CreateAHeroHeroList.h"
#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/GUI/LoadScreenInfo.h"
#include "GameClient/GUI/Shell/Shell.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameLogic/CreateAHeroSystem.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace
{
const char *const kPrefix = "AptCreateAHero::";

int atoiArg(const std::string &s)
{
	// RW 0xBD0628: MSVCR71 atoi (_atol: white space, a sign, digits, 32-bit wrap-around). Lane CAH-2: not the host CRT's, which clamps at LONG_MAX
	// where long is 32-bit (Windows) and would turn a colour "%u" above 2^31 (every opaque colour, OnPaintColor) into 0x7FFFFFFF
	const char *p = s.c_str();
	while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
	{
		++p;
	}
	bool negative = false;
	if (*p == '-' || *p == '+')
	{
		negative = *p == '-';
		++p;
	}
	std::uint32_t total = 0;
	while (*p >= '0' && *p <= '9')
	{
		total = total * 10u + (std::uint32_t)(*p - '0');
		++p;
	}
	return (int)(negative ? 0u - total : total);
}

std::u16string trimmed(const std::u16string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == u' ' || s[a] == u'\t'))
	{
		++a;
	}
	while (b > a && (s[b - 1] == u' ' || s[b - 1] == u'\t'))
	{
		--b;
	}
	return s.substr(a, b - a);
}

bool equalNoCase(const std::u16string &a, const std::u16string &b)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i)
	{
		char16_t x = a[i], y = b[i];
		if (x >= u'A' && x <= u'Z')
		{
			x = (char16_t)(x - u'A' + u'a');
		}
		if (y >= u'A' && y <= u'Z')
		{
			y = (char16_t)(y - u'A' + u'a');
		}
		if (x != y)
		{
			return false;
		}
	}
	return true;
}

// the icon a power shows in the builder: CreateAHeroUIIconImageName, else the first ButtonImage (INFERENCE, S-1404)
std::string powerIcon(const CommandButton *b)
{
	if (!b)
	{
		return std::string();
	}
	if (!b->m_createAHeroUIIconImageName.empty())
	{
		return b->m_createAHeroUIIconImageName;
	}
	return b->m_buttonImageName.empty() ? std::string() : b->m_buttonImageName.front();
}

const char *stateFrame(int state)
{
	switch (state) // the icon frames of the movie's UpdateSelectPowerIcon (the suffixes RW 0xC8AB68 .. 0xC8AB9C)
	{
		case CahPowers::STATE_AVAILABLE:
			return "_avail";
		case CahPowers::STATE_NEEDS_PREREQ:
			return "_needPrereq";
		case CahPowers::STATE_LEVEL_TOO_LOW:
			return "_levelLow";
		case CahPowers::STATE_SELECTED:
			return "_selected";
		case CahPowers::STATE_PALANTIR_FULL:
		case CahPowers::STATE_BOOK_FULL:
			return "_palantirFull";
		default:
			return "_unused";
	}
}

const char *stateTooltip(int state) // RW 0xDB919C
{
	static const char *const names[] = { "", "TOOLTIP:CAH_IS_AVAILABLE", "TOOLTIP:CAH_NEEDS_PREREQ", "TOOLTIP:CAH_LEVEL_TOO_LOW", "TOOLTIP:CAH_IS_SELECTED",
		"TOOLTIP:CAH_PALANTIR_FULL", "TOOLTIP:CAH_POWER_BOOK_FULL" };
	return state >= 0 && state <= 6 ? names[state] : "";
}
} // namespace

// ---- CahPowers ----------------------------------------------------------------------------------------------------------------------------------------------------

void CahPowers::build(CreateAHeroHero &hero, const CreateAHeroSystem &system)
{
	m_cells.clear();
	m_rows.clear();
	m_chosen.clear();
	m_nextSlot = 0;
	m_noPower = Cell();
	m_templateButtons = 0;
	if (!TheCommandStore)
	{
		return;
	}
	m_noPower.button = TheCommandStore->findCommandButton("Command_SpecialAbilityNoPowerDummy"); // RW 0x9C3786 (the name RW 0xC8AED4)
	// RW 0x61900A: the template's leading buttons
	if (const CommandSet *t = TheCommandStore->findCommandSet(system.commandSetTemplate))
	{
		while (m_templateButtons < CommandSet::MAX_BUTTONS && t->getCommandButton(m_templateButtons))
		{
			++m_templateButtons;
		}
	}
	// RW 0x80BD66: every button whose CreateAHeroUIAllowableUpgrades names the class's upgrade (the retail mask test against the upgrade's bit)
	const std::vector<CreateAHeroClass> &classes = system.classes();
	const std::string classUpgrade = hero.classIndex < classes.size() ? classes[hero.classIndex].upgradeName : std::string();
	std::vector<const CommandButton *> available;
	for (const std::string &name : TheCommandStore->buttonNames()) // INFERENCE (S-1404): the store's name order, not retail's button list order
	{
		const CommandButton *b = TheCommandStore->findCommandButton(name);
		if (!b || classUpgrade.empty())
		{
			continue;
		}
		if (std::find(b->m_createAHeroUIAllowableUpgrades.begin(), b->m_createAHeroUIAllowableUpgrades.end(), classUpgrade) != b->m_createAHeroUIAllowableUpgrades.end())
		{
			available.push_back(b);
		}
	}
	for (const CommandButton *b : available)
	{
		auto c = std::make_unique<Cell>();
		c->button = b;
		m_cells.push_back(std::move(c));
	}
	// rows by the prerequisite chains (RW 0x9C39A4 ..): a cell without a known prerequisite starts a row; one after its prerequisite takes the next column
	std::map<std::string, Cell *> byName;
	for (auto &c : m_cells)
	{
		byName[c->button->m_name] = c.get();
	}
	bool placedAll = false;
	while (!placedAll)
	{
		placedAll = true;
		bool progress = false;
		for (auto &c : m_cells)
		{
			if (c->row >= 0)
			{
				continue;
			}
			const std::string &pre = c->button->m_createAHeroUIPrerequisiteButtonName;
			auto it = pre.empty() || pre == "None" ? byName.end() : byName.find(pre);
			if (it == byName.end())
			{
				c->row = (int)m_rows.size();
				c->column = 0;
				m_rows.push_back({ { nullptr, nullptr, nullptr, nullptr } });
				m_rows.back()[0] = c.get();
				progress = true;
			}
			else if (it->second->row >= 0)
			{
				if (it->second->column + 1 >= NUM_COLUMNS)
				{
					continue; // a fifth tier has no column (RW 0x9C3A08: the row's matrix is 4 wide)
				}
				c->row = it->second->row;
				c->column = it->second->column + 1;
				m_rows[(size_t)c->row][(size_t)c->column] = c.get();
				progress = true;
			}
			else
			{
				placedAll = false;
			}
		}
		if (!progress)
		{
			break; // a prerequisite cycle: the rest stay out of the matrix
		}
	}
	// RW 0x9C3A3B: each row from its last column down: a cell moves right to the tier of its minimum level (the table RW 0xC8ABA4: levels 1..10 ->
	// columns 0 0 1 1 1 1 2 2 2 3), never past the cell after it
	static const int kTier[10] = { 0, 0, 1, 1, 1, 1, 2, 2, 2, 3 };
	for (auto &row : m_rows)
	{
		int last = NUM_COLUMNS - 1;
		for (int c = NUM_COLUMNS - 1; c >= 0; --c)
		{
			Cell *cell = row[(size_t)c];
			if (!cell || c == last + 1)
			{
				continue;
			}
			unsigned level = (unsigned)cell->button->m_createAHeroUIMinimumLevel;
			level = level ? level - 1 : 0xFFFFFFFFu;
			if (level < 10)
			{
				const int target = kTier[level];
				int to = c;
				if (c <= target)
				{
					to = target < last ? target : last;
				}
				row[(size_t)c] = nullptr;
				row[(size_t)to] = cell;
				cell->column = to;
				last = to - 1;
			}
		}
	}
	for (size_t r = 0; r < m_rows.size(); ++r)
	{
		for (Cell *c : m_rows[r])
		{
			if (c)
			{
				c->row = (int)r;
			}
		}
	}
	// the hero's chosen powers again, in order (RW 0x9C3AE0: RW 0x9C293D(cell, false) for every power i < 15 with a known cell)
	CreateAHeroHero copy = hero;
	for (CreateAHeroPower &p : hero.powers)
	{
		p = CreateAHeroPower();
	}
	for (const CreateAHeroPower &p : copy.powers)
	{
		if (p.commandButton.empty())
		{
			break;
		}
		Cell *cell = nullptr;
		if (m_noPower.button && p.commandButton == m_noPower.button->m_name)
		{
			cell = &m_noPower;
		}
		else
		{
			auto it = byName.find(p.commandButton);
			if (it == byName.end() || it->second->row < 0)
			{
				break;
			}
			cell = it->second;
		}
		if (!select(cell, hero))
		{
			break;
		}
	}
	updateStates();
}

CahPowers::Cell *CahPowers::cell(int row, int column)
{
	if (row < 0 || (size_t)row >= m_rows.size() || column < 0 || column >= NUM_COLUMNS)
	{
		return nullptr;
	}
	return m_rows[(size_t)row][(size_t)column];
}

bool CahPowers::select(Cell *cell, CreateAHeroHero &hero)
{
	if (!cell)
	{
		cell = &m_noPower;
	}
	if (cell->selected || (int)m_chosen.size() >= MAX_POWERS)
	{
		return false;
	}
	const bool noPower = cell == &m_noPower;
	const Cell *previous = nullptr; // RW 0x9C1F4A: a chosen cell of the same row
	if (!noPower)
	{
		for (const Cell *c : m_chosen)
		{
			if (c != &m_noPower && c->row == cell->row)
			{
				previous = c;
			}
		}
	}
	if (!previous)
	{
		if (noPower)
		{
			cell->palantirSlot = NO_POWER_SLOT;
		}
		else
		{
			if (m_nextSlot >= PALANTIR_BUTTONS - m_templateButtons)
			{
				return false; // the palantir is full
			}
			cell->palantirSlot = m_nextSlot++;
		}
	}
	else
	{
		cell->palantirSlot = previous->palantirSlot;
	}
	const int index = (int)m_chosen.size();
	m_chosen.push_back(cell);
	cell->powerIndex = index;
	// RW 0x809B98(power list, index, slot + template buttons): the record's power i is the button, unlocked at rank index, on that command set slot
	CreateAHeroPower &p = hero.powers[(size_t)index];
	p.commandButton = cell->button ? cell->button->m_name : std::string();
	p.expLevel = (std::uint32_t)index;
	p.buttonIndex = (std::uint32_t)(cell->palantirSlot + m_templateButtons);
	hero.flags |= 0x20;
	cell->selected = !noPower;
	updateStates();
	return true;
}

void CahPowers::truncate(int count, CreateAHeroHero &hero)
{
	if (count < 0)
	{
		count = 0;
	}
	while ((int)m_chosen.size() > count)
	{
		Cell *c = m_chosen.back();
		m_chosen.pop_back();
		hero.powers[m_chosen.size()] = CreateAHeroPower();
		c->selected = false;
		c->powerIndex = -1;
		bool rowStillChosen = false;
		for (const Cell *o : m_chosen)
		{
			if (c != &m_noPower && o != &m_noPower && o->row == c->row)
			{
				rowStillChosen = true;
			}
		}
		if (c != &m_noPower && !rowStillChosen && c->palantirSlot == m_nextSlot - 1)
		{
			--m_nextSlot;
		}
	}
	hero.flags |= 0x20;
	updateStates();
}

void CahPowers::updateStates()
{
	// INFERENCE (S-1404): the state rules (RW 0x9C2xxx were not read in full): selected; the book full (10 powers); the rank this choice is for
	// (chosen + 1) below CreateAHeroUIMinimumLevel; a prerequisite not chosen; a new row with the palantir full; else available
	const int rank = (int)m_chosen.size() + 1;
	for (auto &c : m_cells)
	{
		if (c->row < 0)
		{
			c->state = STATE_NONE;
			continue;
		}
		if (c->selected)
		{
			c->state = STATE_SELECTED;
			continue;
		}
		if ((int)m_chosen.size() >= MAX_POWERS)
		{
			c->state = STATE_BOOK_FULL;
			continue;
		}
		if (c->button->m_createAHeroUIMinimumLevel > rank)
		{
			c->state = STATE_LEVEL_TOO_LOW;
			continue;
		}
		const Cell *before = c->column > 0 ? nullptr : nullptr;
		for (int k = c->column - 1; k >= 0; --k)
		{
			if (m_rows[(size_t)c->row][(size_t)k])
			{
				before = m_rows[(size_t)c->row][(size_t)k];
				break;
			}
		}
		if (before && !before->selected)
		{
			c->state = STATE_NEEDS_PREREQ;
			continue;
		}
		bool rowChosen = false;
		for (const Cell *o : m_chosen)
		{
			if (o != &m_noPower && o->row == c->row)
			{
				rowChosen = true;
			}
		}
		if (!rowChosen && m_nextSlot >= PALANTIR_BUTTONS - m_templateButtons)
		{
			c->state = STATE_PALANTIR_FULL;
			continue;
		}
		c->state = STATE_AVAILABLE;
	}
}

// ---- AptCreateAHero -----------------------------------------------------------------------------------------------------------------------------------------------

const std::vector<std::string> &AptCreateAHero::retailNames()
{
	// RW 0x91A6BF, 0x9C5EA4, 0x9C4AE0, 0x9C4255, 0x9C338B (and the AptMyHero providers RW 0x9C00E7); strings RW 0xC7CC54 .., 0xC8AF4C .., 0xC8B214 ..
	static const std::vector<std::string> names = {
		"AptCreateAHero::OnShowScreen", "AptCreateAHero::PrepareToTakePicture", "AptCreateAHero::OnTakePicture", "CreateAHero::RenderPictureGuard",
		"CreateAHero::DrawMapComponent", "CreateAHeroDemo", "AptCreateAHero::SuppressCAHPromo", "AptCreateAHero::RotateLeft", "AptCreateAHero::RotateRight",
		"AptCreateAHero::ZoomIn", "AptCreateAHero::ZoomOut",
		// Manager
		"AptCreateAHero::Manager::OnDeleteHero", "AptCreateAHero::Manager::OnPlayGame", "AptCreateAHero::Manager::OnSelectAward", "Mission::OnSortIcon",
		"Mission::OnSortName", "Mission::OnSortType", "AptCreateAHero::OnMyPowerRollOver", "AptCreateAHero::OnMyPowerRollOut", "CahManager::InitGadgets",
		// Class
		"CahClass::InitGadgets", "CahClass::NumClassTypes", "CahClass::NumClasses", "AptCreateAHero::Class::SetClassAndType", "AptCreateAHero::Class::OnMapClick",
		"AptCreateAHero::Class::Exit",
		// Appearance
		"AptCreateAHero::Appearance::AutoChangeBttn", "AptCreateAHero::Appearance::OnComplete", "AptCreateAHero::Appearance::NamePrompt",
		"AptCreateAHero::Appearance::OnPaintColor", "AptCreateAHero::Appearance::OnSkinColor", "AptCreateAHero::Appearance::OnHairColor",
		"AptCreateAHero::Appearance::IncreaseAttribute", "AptCreateAHero::Appearance::DecreaseAttribute", "AptCreateAHero::Appearance::NextAppearance",
		"AptCreateAHero::Appearance::PrevAppearance", "CahAppearance::InitGadgets", "CahAppearance::ShowNamePrompt", "CahAppearance::HeroNameSet",
		"CahAppearance::PaintColor", "CahAppearance::SkinColor", "CahAppearance::HairColor",
		// Powers
		"CahPowers::InitGadgets", "AptCreateAHero::OnPowerSelect", "AptCreateAHero::OnMyPowerSelect", "AptCreateAHero::OnNoPowerSelect",
		"AptCreateAHero::PowersIconsUpdate", "AptCreateAHero::OnPowerSelectionComplete", "AptCreateAHero::OnResetBttn", "AptCreateAHero::PalantirToolTip",
		"AptCreateAHero::MatrixToolTip", "AptCreateAHero::MyPowerToolTip", "NumPowerRows", "DisablePowerInstructions", "CurrentPowerIndex",
		"NumCurrentPowers", "CahPowers::Powers::InitGadgets",
		// Bonus
		"CahBonus::InitGadgets"
	};
	return names;
}

const std::vector<std::string> &AptCreateAHero::stopLines()
{
	static const std::vector<std::string> lines = {
		"[S-1400] Create-a-Hero hero list: the sort comparator of RW 0x61EE28 was not read (system heroes first, then by file name without case); the profile folder is "
		"retail's <application data>\\<gi.dat UserDataLeafName>\\Save\\ (lane CAH-2, S-1407); the unique id's GUID comes from the client's random device; the hero being edited may keep its own name "
		"(RW 0x80CC7E not read); Mission::OnSort* do not re-sort",
		"[S-1401] Create-a-Hero edited hero: AptMyHero's vslot 0x14 (the apply after a change: texts, the movie's cost, the preview object's upgrades) is "
		"summarised; AppearanceRandom draws from the device's client random",
		"[S-1402] Create-a-Hero message boxes (lane CAH-2: NamePrompt's Ok box RW 0x81A375 and the delete question RW 0x81A452(2) are ported): the box's "
		"timeout and its state callbacks are not used",
		"[S-1403] Create-a-Hero awards and statistics: Manager::OnSelectAward, the SavedHeroStats rows, MyHero::AwardState_<i> and MaxAwards (0) are "
		"not ported (the award bling RW 0x8096B0 and the statistics file RW 0x80B339 are HERO-2's S-1226)",
		"[S-1404] Create-a-Hero powers page: the matrix's row order (RW 0x9C38EB's comparator not read: the command store's name order), the cell states "
		"(RW 0x9C2xxx not read in full: selected, book full, level too low, prerequisite, palantir full), the icons (CreateAHeroUIIconImageName, else the "
		"first ButtonImage), the row names, the labels without their hot-key marks, the wizard line and the matrix's first scroll position are inferred",
		"[S-1405] Create-a-Hero screen: the hero portrait (OnTakePicture), OnPlayGame's test map, which page \"CreateAHero\" forwards to, the "
		"SuppressCahPromo preference (kept for the session), the color picker's \"?<picker>Color\" externs (undefined), the 3D view's camera (ViewInfo, "
		"RW 0x9BFE74: the ground point, angle, blend and field of view as retail; the eye height Floor * Zoom and the pitch's use inferred, lane CAH-2) and the name prompt (ShowNamePrompt: \"1\" while the entry is empty; the movie reads it when the page opens, so its text "
		"stays over a name typed later) are inferred or not ported",
		"[S-1408] Create-a-Hero appearance (lane CAH-2 r2: ported): the record's three colours reach the drawable as kind 3 (RW 0x80AF0B -> 0x80959A -> "
		"0x6727B0) and its model's house colour textures are recoloured texel by texel before filtering as RW 0x531C77 (mipmaps from the recoloured level); INFERENCE: the recoloured texel replaces the base where the house texture's alpha "
		"is set (the combine is unrecovered, S-022 / S-119), and the A4R4G4B4 path is not ported. The ColorPicker component (RW 0xB55146 ..) draws "
		"AptColorChooserPallete, reads the texel under the cursor and places the cursor for a colour in the update rather than the draw. The rows' "
		"StatVal fields are retail-exact: no movie places a StatVal, so no bling name is shown"
	};
	return lines;
}

AptCreateAHero::AptCreateAHero(WindowManager &windows, Shell &shell, ShellEnvironment &environment)
	: AptScreen(windows, shell, "CreateAHero.apt", "AptCreateAHero"), m_env(environment)
{
	for (const std::string &line : stopLines())
	{
		windows.note("create-a-hero-stop", line);
	}
	if (!m_env.createAHero || !m_env.createAHero->system)
	{
		windows.note("create-a-hero", "the shell has no Create-a-Hero context (CreateAHeroScreenContext with TheCreateAHeroSystem): the builder shows nothing");
	}
	else
	{
		m_myHero = std::make_unique<AptMyHero>(*m_env.createAHero->system, &windows, m_env.gameText);
		m_myHero->onChanged = [this]() {
			bumpDisplay();
			refreshTexts();
		};
		// RW 0x9C4951: the class page's hero of every (class, subclass)
		const std::vector<CreateAHeroClass> &classes = m_env.createAHero->system->classes();
		for (size_t c = 0; c < classes.size(); ++c)
		{
			for (size_t s = 0; s < classes[c].subClasses.size(); ++s)
			{
				CreateAHeroHero h;
				h.classIndex = (std::uint32_t)c;
				h.subClassIndex = (std::uint32_t)s;
				h.primaryColor = 0xFFFFFFFFu;
				h.secondaryColor = 0xFF707070u;
				h.tertiaryColor = 0xFFFFFFFFu;
				h.flags = CreateAHeroHero::LOAD_FLAGS;
				m_classHeroes.push_back(h);
			}
			setImage("Cah::ClassIcon" + std::to_string(c), classes[c].iconImage); // "Cah::ClassIcon%d" RW 0xC8B43C
		}
		if (!m_env.createAHero->heroes)
		{
			windows.note("create-a-hero", "the Create-a-Hero context has no hero list: the Manager page lists nothing");
		}
	}
	windows.setAptText("APT:CahClassDescription", " ");
	windows.setAptText("APT:CahTypeDescription", " ");
	windows.setAptText("APT:HeroTypeDescription", " ");
	windows.setAptText("APT:HeroPowersDescription", " ");
	registerAll();
	windows.addUpdateListener(this, [this]() { update(); });
}

AptCreateAHero::~AptCreateAHero() = default;

const CreateAHeroHero *AptCreateAHero::displayedHero() const
{
	if (m_showClassHero && m_classHero >= 0 && (size_t)m_classHero < m_classHeroes.size())
	{
		return &m_classHeroes[(size_t)m_classHero];
	}
	if (m_myHero && (m_page == 'A' || m_page == 'P' || m_selectedHero >= 0))
	{
		return &m_myHero->hero();
	}
	return nullptr;
}

void AptCreateAHero::setImage(const std::string &name, const std::string &image)
{
	windows().setAptImage(name, image); // RW 0x6236F6
}

void AptCreateAHero::message(const std::string &titleLabel, const std::string &textLabel)
{
	// RW 0x81A375(0, title, text): an Ok box with the two game texts (lane CAH-2: TheMessageBox, GameClient/GUI/AptMessageBox.h)
	m_lastMessage = titleLabel + ": " + textLabel;
	windows().note("create-a-hero-message", m_lastMessage);
	if (!m_box)
	{
		m_box = std::make_unique<AptMessageBox>(windows(), shell());
	}
	m_box->show(AptMessageBox::TYPE_OK, fetchOrMissing(m_env.gameText, titleLabel), fetchOrMissing(m_env.gameText, textLabel));
}

void AptCreateAHero::invoke(const std::string &function, const std::vector<std::string> &args)
{
	if (level() < 0)
	{
		return;
	}
	std::string error;
	windows().invokeAS(level(), function, args, nullptr, &error); // RW 0x83FFD6 / 0x6D5587 on the screen's level (+0x274)
}

void AptCreateAHero::refreshTexts()
{
	if (!m_myHero || !m_env.createAHero || !m_env.createAHero->system)
	{
		return;
	}
	const CreateAHeroHero &h = m_myHero->hero();
	const CreateAHeroSystem &system = *m_env.createAHero->system;
	windows().setAptText("APT:MyHeroName", h.name.empty() ? std::string(" ") : loadScreenU16ToUtf8(h.name));
	const std::vector<CreateAHeroClass> &classes = system.classes();
	if (h.classIndex < classes.size())
	{
		windows().setAptText("APT:MyHeroClass", loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, classes[h.classIndex].nameTag)));
	}
	if (const CreateAHeroSubClass *sub = system.subClass(h.classIndex, h.subClassIndex))
	{
		windows().setAptText("APT:MyHeroType", loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, sub->nameTag)));
		windows().setAptText("APT:HeroTypeDescription", loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, sub->descriptionTag)));
	}
	const int cost = m_myHero->buildCost();
	windows().setAptText("APT:MyHeroCost", cost >= 0 ? std::to_string(cost) : std::string(" "));
	setImage("Cah::Portrait", h.classIndex < classes.size() ? classes[h.classIndex].iconImage : std::string()); // INFERENCE (S-1405): the portrait file is not ported
}

void AptCreateAHero::registerAll()
{
	// ---- the screen (RW 0x91A6BF) ----
	registerCommand("AptCreateAHero::OnShowScreen", [this](const std::string &arg) { showPage(arg.empty() ? 0 : arg[0]); });
	registerCommand("AptCreateAHero::PrepareToTakePicture", [this](const std::string &) { m_pictureFrames = 0; });
	registerCommand("AptCreateAHero::OnTakePicture", [this](const std::string &) {
		windows().note("unported-command", "AptCreateAHero::OnTakePicture: the hero portrait is not taken [S-1405]");
		invoke("OnGameTookPicture", {});
	});
	registerCommand("AptCreateAHero::RotateLeft", [this](const std::string &a) { m_rotateLeft = !a.empty() && a[0] == 't'; });
	registerCommand("AptCreateAHero::RotateRight", [this](const std::string &a) { m_rotateRight = !a.empty() && a[0] == 't'; });
	registerCommand("AptCreateAHero::ZoomIn", [this](const std::string &a) { m_zoomIn = !a.empty() && a[0] == 't'; });
	registerCommand("AptCreateAHero::ZoomOut", [this](const std::string &a) { m_zoomOut = !a.empty() && a[0] == 't'; });
	registerComponent("CreateAHero::RenderPictureGuard", [this](AptComponentRequest &) -> std::shared_ptr<GameWindow> {
		++m_pictureFrames;
		return nullptr;
	});
	// RW 0x91A3A9: the 3D view is drawn by the device into the clip's rectangle (GodotDevice: the preview); no window is kept
	registerComponent("CreateAHero::DrawMapComponent", [](AptComponentRequest &) -> std::shared_ptr<GameWindow> { return nullptr; });
	registerProvider("CreateAHeroDemo", [](const std::string &, std::string &value, bool setting) {
		if (!setting)
		{
			value = "1"; // GlobalData + 0x9D1 (a demo build) is false
		}
		return true;
	});
	registerProvider("AptCreateAHero::SuppressCAHPromo", [this](const std::string &, std::string &value, bool setting) {
		CreateAHeroScreenContext *ctx = m_env.createAHero;
		if (setting)
		{
			if (ctx)
			{
				ctx->suppressPromo = value == "true" || value == "1";
			}
			return true;
		}
		value = ctx && ctx->suppressPromo ? "1" : "0";
		return true;
	});
	// the movie's OnInitialized (CreateAHero.apt's root calls GameCode('OnInitialized')): INFERENCE (S-1405): the screens' generic handler, an empty body
	// (RW 0x9F3A3C: `ret 4`, as AptLanLobby::OnInitialized); the name is not in the binary's AptCreateAHero string table
	registerCommand("AptCreateAHero::OnInitialized", [](const std::string &) {});
	// GadgetColorPicker.swf's InitColor asks "?" + <picker> + "Color" with the default "extern" and, when it answers the default, writes <picker> + "Color":
	// RotWK registers neither (they are no names of the binary): the read is undefined and the write is dropped, as an extern nobody provides (INFERENCE,
	// S-1405: the retail window manager's answer to an unknown extern was not read; the shell reports unknown names, so these are named here)
	// lane CAH-2: <picker> + "Color" / "Cursor" are the ColorPicker component's own externs (GUI/AptColorPicker.h, RW 0xB5570B); the "?" test name
	// stays unanswered as retail (no such name in the binary)
	for (const char *picker : { "HairColor", "SkinColor", "PaintColor" })
	{
		registerProvider(std::string("?") + picker + "Color", [](const std::string &, std::string &, bool) { return false; });
	}
	registerCommand("CreateAHero", [this](const std::string &a) {
		windows().note("unported-command", "CreateAHero(" + a + "): the current page's vslot 7 is not read [S-1405]");
	});

	// ---- the MyHero providers (RW 0x9C00E7) ----
	for (const std::string &name : AptMyHero::providerNames())
	{
		registerProvider(name, [this](const std::string &n, std::string &value, bool setting) {
			if (setting || !m_myHero)
			{
				return false;
			}
			return m_myHero->provide(n, value);
		});
		if (name != "MyHero::IsSystemHero")
		{
			windows().markNumericProvider(name);
		}
	}
	windows().markNumericProvider("MyHero::IsSystemHero"); // the movie tests it with Not: "0" must be false (as AptTimeLine's flags, S-1063)

	// ---- Manager (RW 0x9C5EA4) ----
	registerCommand("AptCreateAHero::Manager::OnDeleteHero", [this](const std::string &) { deleteSelectedHero(); });
	registerCommand("AptCreateAHero::Manager::OnPlayGame", [this](const std::string &) {
		windows().note("unported-command", "AptCreateAHero::Manager::OnPlayGame: the test map is not started [S-1405]");
	});
	registerCommand("AptCreateAHero::Manager::OnSelectAward", [this](const std::string &a) {
		windows().note("unported-command", "AptCreateAHero::Manager::OnSelectAward(" + a + "): the awards are not ported [S-1403]");
	});
	for (const char *n : { "Mission::OnSortIcon", "Mission::OnSortName", "Mission::OnSortType" })
	{
		const std::string name = n;
		registerCommand(name, [this, name](const std::string &) { windows().note("unported-command", name + ": the hero list is not re-sorted [S-1400]"); });
	}
	registerCommand("AptCreateAHero::OnMyPowerRollOver", [this](const std::string &a) {
		// RW 0x9C4E1A: the power's description into APT:HeroPowersDescription
		const int i = atoiArg(a) - 1;
		const CreateAHeroHero *h = displayedHero();
		std::u16string text = u" ";
		if (h && i >= 0 && i < CreateAHeroHero::POWER_COUNT && TheCommandStore)
		{
			if (const CommandButton *b = TheCommandStore->findCommandButton(h->powers[(size_t)i].commandButton))
			{
				if (!b->m_descriptLabel.empty())
				{
					text = fetchOrMissing(m_env.gameText, b->m_descriptLabel.front());
				}
			}
		}
		windows().setAptText("APT:HeroPowersDescription", loadScreenU16ToUtf8(text));
	});
	registerCommand("AptCreateAHero::OnMyPowerRollOut", [this](const std::string &) { windows().setAptText("APT:HeroPowersDescription", " "); });
	registerScreenRef("CahManager::InitGadgets", [this](const std::string &name, GameWindow *w) {
		if (!w)
		{
			return;
		}
		if (name == "SavedHeroList") // RW 0x9C5C78
		{
			const int widths[3] = { 9, 46, 45 };
			GadgetListBoxSetColumnWidths(w, 3, widths);
			m_heroList = w;
			m_listDirty = true;
		}
		else if (name == "SavedHeroStats")
		{
			const int widths[2] = { 80, 20 };
			GadgetListBoxSetColumnWidths(w, 2, widths);
			m_heroStats = w;
		}
	});

	// ---- Class (RW 0x9C4AE0) ----
	registerScreenRef("CahClass::InitGadgets", [](const std::string &, GameWindow *) {});
	registerProvider("CahClass::NumClasses", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			return false;
		}
		value = std::to_string(m_env.createAHero && m_env.createAHero->system ? m_env.createAHero->system->classes().size() : 0);
		return true;
	});
	registerProvider("CahClass::NumClassTypes", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			return false;
		}
		size_t n = 0;
		if (m_env.createAHero && m_env.createAHero->system && m_classHero >= 0 && (size_t)m_classHero < m_classHeroes.size())
		{
			const std::uint32_t c = m_classHeroes[(size_t)m_classHero].classIndex;
			const std::vector<CreateAHeroClass> &classes = m_env.createAHero->system->classes();
			n = c < classes.size() ? classes[c].subClasses.size() : 0;
		}
		value = std::to_string(n);
		return true;
	});
	windows().markNumericProvider("CahClass::NumClasses");
	windows().markNumericProvider("CahClass::NumClassTypes");
	registerCommand("AptCreateAHero::Class::SetClassAndType", [this](const std::string &a) {
		int cls = 0, type = 0;
		if (std::sscanf(a.c_str(), "%d %d", &cls, &type) == 2) // "%d %d" RW 0xBF99D8
		{
			setClassAndType(cls, type);
		}
	});
	registerCommand("AptCreateAHero::Class::OnMapClick", [this](const std::string &) {});
	registerCommand("AptCreateAHero::Class::Exit", [this](const std::string &) {
		m_exitRequested = true;
		ShellRequest r;
		r.action = ShellAction::CreateAHeroExit;
		if (m_env.services)
		{
			m_env.services->request(r);
		}
		windows().requestShellPop();
	});

	// ---- Appearance (RW 0x9C4255) ----
	registerCommand("AptCreateAHero::Appearance::AutoChangeBttn", [this](const std::string &a) {
		if (!m_myHero)
		{
			return;
		}
		if (a == "AppearanceDefault")
		{
			m_myHero->setDefaults(AptMyHero::KIND_APPEARANCE);
			m_myHero->setDefaultColors();
		}
		else if (a == "AppearanceRandom")
		{
			CreateAHeroScreenContext *ctx = m_env.createAHero;
			m_myHero->randomize(AptMyHero::KIND_APPEARANCE, ctx && ctx->random ? ctx->random : std::function<int(int, int)>([](int lo, int) { return lo; }));
		}
		else if (a == "AttribReset")
		{
			m_myHero->resetToMinimum(AptMyHero::KIND_ATTRIBUTE);
		}
		else if (a == "AttribRecommend")
		{
			m_myHero->setDefaults(AptMyHero::KIND_ATTRIBUTE);
		}
		invoke("UpdateHeroBaseAttributes", {});
	});
	registerCommand("AptCreateAHero::Appearance::OnComplete", [this](const std::string &) { completeAppearance(); });
	registerCommand("AptCreateAHero::Appearance::NamePrompt", [this](const std::string &) {
		// RW 0x9C3DC7
		if (trimmed(entryName()).empty())
		{
			message("APT:EnterNameErrorTitle", "APT:EnterNameError");
		}
		else
		{
			message("APT:EnterNameErrorTitle", "LAN:ErrorDuplicateName");
		}
	});
	registerCommand("AptCreateAHero::Appearance::OnPaintColor", [this](const std::string &a) {
		if (m_myHero)
		{
			m_myHero->setTertiaryColor((std::uint32_t)atoiArg(a));
			bumpDisplay();
		}
	});
	registerCommand("AptCreateAHero::Appearance::OnSkinColor", [this](const std::string &a) {
		if (m_myHero)
		{
			m_myHero->setSecondaryColor((std::uint32_t)atoiArg(a));
			bumpDisplay();
		}
	});
	registerCommand("AptCreateAHero::Appearance::OnHairColor", [this](const std::string &a) {
		if (m_myHero)
		{
			m_myHero->setPrimaryColor((std::uint32_t)atoiArg(a));
			bumpDisplay();
		}
	});
	registerCommand("AptCreateAHero::Appearance::IncreaseAttribute", [this](const std::string &a) {
		if (m_myHero)
		{
			m_myHero->step(AptMyHero::KIND_ATTRIBUTE, atoiArg(a), 1);
		}
	});
	registerCommand("AptCreateAHero::Appearance::DecreaseAttribute", [this](const std::string &a) {
		if (m_myHero)
		{
			m_myHero->step(AptMyHero::KIND_ATTRIBUTE, atoiArg(a), -1);
		}
	});
	registerCommand("AptCreateAHero::Appearance::NextAppearance", [this](const std::string &a) {
		if (m_myHero)
		{
			m_myHero->step(AptMyHero::KIND_APPEARANCE, atoiArg(a), 1);
		}
	});
	registerCommand("AptCreateAHero::Appearance::PrevAppearance", [this](const std::string &a) {
		if (m_myHero)
		{
			m_myHero->step(AptMyHero::KIND_APPEARANCE, atoiArg(a), -1);
		}
	});
	registerScreenRef("CahAppearance::InitGadgets", [this](const std::string &name, GameWindow *w) {
		if (w && name == "CreateAHero::HeroName") // RW 0x9C4025 -> 0x9C3F2A
		{
			m_nameEntry = w;
			if (EntryData *entry = static_cast<EntryData *>(w->winGetUserData()))
			{
				entry->maxTextLen = 0x16; // RW 0x81606D(entry, 0x16)
			}
			GadgetTextEntrySetText(w, m_myHero ? m_myHero->hero().name : std::u16string());
		}
	});
	registerProvider("CahAppearance::ShowNamePrompt", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			return false;
		}
		value = entryName().empty() ? "1" : "0";
		return true;
	});
	registerProvider("CahAppearance::HeroNameSet", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			return false;
		}
		const std::u16string name = trimmed(entryName());
		value = !name.empty() && nameIsFree(name) ? "1" : "0";
		return true;
	});
	windows().markNumericProvider("CahAppearance::ShowNamePrompt");
	windows().markNumericProvider("CahAppearance::HeroNameSet");
	for (int k = 0; k < 3; ++k)
	{
		static const char *const names[] = { "CahAppearance::HairColor", "CahAppearance::SkinColor", "CahAppearance::PaintColor" };
		registerProvider(names[k], [this, k](const std::string &, std::string &value, bool setting) {
			if (setting || !m_myHero)
			{
				return false;
			}
			const CreateAHeroHero &h = m_myHero->hero();
			const std::uint32_t c = k == 0 ? h.primaryColor : k == 1 ? h.secondaryColor : h.tertiaryColor;
			value = std::to_string(c); // "%u" RW 0xBD41E8
			return true;
		});
	}

	// ---- Powers (RW 0x9C338B) ----
	registerScreenRef("CahPowers::InitGadgets", [](const std::string &, GameWindow *) {});
	registerScreenRef("CahPowers::Powers::InitGadgets", [](const std::string &, GameWindow *) {});
	registerScreenRef("CahBonus::InitGadgets", [](const std::string &, GameWindow *) {});
	registerCommand("AptCreateAHero::OnPowerSelect", [this](const std::string &a) {
		int row = 0, col = 0;
		if (std::sscanf(a.c_str(), "%d,%d", &row, &col) == 2) // "%d,%d" RW 0xC8AD10, 1-based
		{
			choosePower(row - 1, col - 1);
		}
	});
	registerCommand("AptCreateAHero::OnMyPowerSelect", [this](const std::string &a) {
		if (atoiArg(a) == m_powers.chosenCount()) // RW 0x9C17F4: only the last choice can be taken back
		{
			m_keepPowers = atoiArg(a) - 1;
		}
	});
	registerCommand("AptCreateAHero::OnNoPowerSelect", [this](const std::string &) { m_pendingPower = &m_powers.noPower(); });
	registerCommand("AptCreateAHero::PowersIconsUpdate", [this](const std::string &) { m_powersDirty = true; });
	registerCommand("AptCreateAHero::OnPowerSelectionComplete", [this](const std::string &) { completePowers(); });
	registerCommand("AptCreateAHero::OnResetBttn", [this](const std::string &) { m_keepPowers = 0; });
	for (const char *n : { "AptCreateAHero::PalantirToolTip", "AptCreateAHero::MatrixToolTip", "AptCreateAHero::MyPowerToolTip" })
	{
		const std::string name = n;
		registerCommand(name, [this, name](const std::string &a) {
			// RW 0x9C1CD1 / 0x9C2484 / 0x9C1DD9: the tooltip of a cell (the state's TOOLTIP label for the matrix)
			int row = 0, col = 0;
			std::string label;
			if (name == "AptCreateAHero::MatrixToolTip" && std::sscanf(a.c_str(), "%d,%d", &row, &col) == 2)
			{
				if (CahPowers::Cell *c = m_powers.cell(row - 1, col - 1))
				{
					label = stateTooltip(c->state);
				}
			}
			windows().setAptText("APT:HeroPowersDescription", label.empty() ? std::string(" ") : loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, label)));
		});
	}
	auto powersProvider = [this](int which) {
		return [this, which](const std::string &, std::string &value, bool setting) {
			if (setting)
			{
				if (which == 2) // CurrentPowerIndex = "<row> <col>" (the movie's GetCurrentPowerIndex)
				{
					m_lastMessage.clear();
					int row = 0, col = 0;
					m_pendingQueryRow = std::sscanf(value.c_str(), "%d %d", &row, &col) == 2 ? row : -1;
					m_pendingQueryCol = col;
				}
				return true;
			}
			switch (which) // RW 0x9C1A55
			{
				case 0:
					value = std::to_string(m_powers.rowCount());
					break;
				case 1:
					value = m_powers.chosenCount() >= CahPowers::MAX_POWERS ? "1" : "0";
					break;
				case 2:
				{
					CahPowers::Cell *c = m_powers.cell(m_pendingQueryRow - 1, m_pendingQueryCol - 1); // the movie's 1-based row and cell clip names
					value = std::to_string(c && c->selected ? c->powerIndex + 1 : 0);
					break;
				}
				default:
					value = std::to_string(m_powers.chosenCount());
					break;
			}
			return true;
		};
	};
	registerProvider("NumPowerRows", powersProvider(0));
	registerProvider("DisablePowerInstructions", powersProvider(1));
	registerProvider("CurrentPowerIndex", powersProvider(2));
	registerProvider("NumCurrentPowers", powersProvider(3));
	for (const char *n : { "NumPowerRows", "DisablePowerInstructions", "CurrentPowerIndex", "NumCurrentPowers" })
	{
		windows().markNumericProvider(n);
	}
}

std::u16string AptCreateAHero::entryName() const
{
	if (m_nameEntry)
	{
		if (AptGadgetLayer *layer = const_cast<AptCreateAHero *>(this)->windows().gadgetLayer())
		{
			const std::vector<GameWindow *> live = layer->gadgets().allWindows();
			if (std::find(live.begin(), live.end(), m_nameEntry) != live.end())
			{
				return GadgetTextEntryGetText(m_nameEntry);
			}
		}
	}
	return m_myHero ? m_myHero->hero().name : std::u16string();
}

bool AptCreateAHero::nameIsFree(const std::u16string &name) const
{
	const CreateAHeroHeroList *list = m_env.createAHero ? m_env.createAHero->heroes : nullptr;
	if (!list)
	{
		return true;
	}
	const std::string self = m_myHero ? m_myHero->hero().uniqueID : std::string();
	for (const CreateAHeroListEntry &e : list->entries())
	{
		if (!self.empty() && e.hero.uniqueID == self)
		{
			continue; // INFERENCE (S-1400): the hero being edited keeps its own name (RW 0x80CC7E was not read)
		}
		if (equalNoCase(trimmed(e.hero.name), name))
		{
			return false;
		}
	}
	return true;
}

void AptCreateAHero::showPage(char page)
{
	if (page != 'A' && page != 'B' && page != 'C' && page != 'M' && page != 'P')
	{
		return; // BFME2 0x5139B0: another letter keeps the page
	}
	if (page == m_page)
	{
		return;
	}
	m_previousPage = m_page;
	m_page = page;
	// the hidden page (vslot 4) and the shown one (vslot 3)
	if (m_previousPage == 'M')
	{
		windows().setAptText("APT:HeroPowersDescription", " ");
	}
	switch (page)
	{
		case 'M': // RW 0x9C5C40
			m_showClassHero = false;
			m_editingExisting = false;
			m_listDirty = true;
			break;
		case 'C': // the class page shows its pair heroes; SetClassAndType(0, 0) comes from the movie
			m_showClassHero = true;
			m_editingExisting = false;
			refreshClassPage();
			break;
		case 'A': // RW 0x9C0E03 (+0x150: a new hero when coming from the class page)
			if (m_myHero)
			{
				if (m_previousPage == 'C' && m_classHero >= 0 && (size_t)m_classHero < m_classHeroes.size())
				{
					m_myHero->edit(m_classHeroes[(size_t)m_classHero], true);
					m_editingExisting = false;
				}
				else if (m_previousPage == 'M')
				{
					m_editingExisting = true;
				}
				m_showClassHero = false;
				if (m_nameEntry)
				{
					GadgetTextEntrySetText(m_nameEntry, m_myHero->hero().name);
				}
			}
			break;
		case 'P': // RW 0x9C3B51
			m_powersScrollUpdates = 60;
			if (m_myHero && m_env.createAHero && m_env.createAHero->system)
			{
				if (m_previousPage == 'M')
				{
					m_editingExisting = true;
				}
				m_showClassHero = false;
				m_powers.build(m_myHero->hero(), *m_env.createAHero->system);
				m_powersDirty = true;
			}
			break;
		default:
			break;
	}
	bumpDisplay();
	refreshTexts();
}

void AptCreateAHero::refreshClassPage()
{
	if (!m_env.createAHero || !m_env.createAHero->system || m_classHero < 0 || (size_t)m_classHero >= m_classHeroes.size())
	{
		return;
	}
	const CreateAHeroHero &h = m_classHeroes[(size_t)m_classHero];
	const std::vector<CreateAHeroClass> &classes = m_env.createAHero->system->classes();
	if (h.classIndex >= classes.size())
	{
		return;
	}
	const CreateAHeroClass &c = classes[h.classIndex];
	for (size_t s = 0; s < c.subClasses.size(); ++s)
	{
		setImage("Cah::TypeIcon" + std::to_string(s), c.subClasses[s].buttonImage); // "Cah::TypeIcon%d" RW 0xC8B42C, RW 0x61AF37
	}
	windows().setAptText("APT:CahClassDescription", loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, c.descriptionTag)));
	if (h.subClassIndex < c.subClasses.size())
	{
		windows().setAptText("APT:CahTypeDescription", loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, c.subClasses[h.subClassIndex].descriptionTag)));
	}
}

void AptCreateAHero::setClassAndType(int cls, int type)
{
	// RW 0x9C47D3: RW 0x80CD33 finds the class page's hero of the pair
	for (size_t i = 0; i < m_classHeroes.size(); ++i)
	{
		if ((int)m_classHeroes[i].classIndex == cls && (int)m_classHeroes[i].subClassIndex == type)
		{
			m_classHero = (int)i;
			m_showClassHero = true;
			if (m_myHero) // the class page's view of the pair: the pair's hero with its default appearance (what the next page starts from)
			{
				m_myHero->edit(m_classHeroes[i], true);
			}
			refreshClassPage();
			bumpDisplay();
			refreshTexts();
			return;
		}
	}
	windows().note("create-a-hero", "Class::SetClassAndType: no class " + std::to_string(cls) + " type " + std::to_string(type));
}

void AptCreateAHero::selectHero(int index)
{
	// RW 0x9C4F66: the row's hero is the one shown and edited
	const CreateAHeroHeroList *list = m_env.createAHero ? m_env.createAHero->heroes : nullptr;
	const CreateAHeroListEntry *e = list ? list->at(index) : nullptr;
	if (!e || !m_myHero)
	{
		m_selectedHero = -1;
		invoke("EnableCustomizeButtons", { "false" });
		return;
	}
	m_selectedHero = index;
	m_myHero->edit(e->hero, false);
	m_showClassHero = false;
	invoke("EnableCustomizeButtons", { e->system ? "false" : "true" });
	bumpDisplay();
	refreshTexts();
	invoke("UpdateHeroBuildCost", { std::to_string(m_myHero->buildCost()) });
}

void AptCreateAHero::fillHeroList()
{
	// RW 0x9C5A66: every hero's name (column 1) and class label (column 2); the edited hero stays selected
	m_listDirty = false;
	if (!m_heroList)
	{
		return;
	}
	AptGadgetLayer *layer = windows().gadgetLayer();
	if (layer)
	{
		const std::vector<GameWindow *> live = layer->gadgets().allWindows();
		if (std::find(live.begin(), live.end(), m_heroList) == live.end())
		{
			m_heroList = nullptr;
			return;
		}
	}
	GadgetListBoxReset(m_heroList);
	const CreateAHeroHeroList *list = m_env.createAHero ? m_env.createAHero->heroes : nullptr;
	if (!list || !m_env.createAHero->system)
	{
		return;
	}
	const std::vector<CreateAHeroClass> &classes = m_env.createAHero->system->classes();
	int select = -1;
	for (size_t i = 0; i < list->size(); ++i)
	{
		const CreateAHeroHero &h = list->entries()[i].hero;
		const int row = GadgetListBoxAddEntryText(m_heroList, h.name, 0xFFFFFFFFu, -1, 1);
		std::u16string cls = h.classIndex < classes.size() ? fetchOrMissing(m_env.gameText, classes[h.classIndex].nameTag) : std::u16string();
		GadgetListBoxAddEntryText(m_heroList, cls, 0xFFFFFFFFu, row, 2);
		GadgetListBoxSetItemData(m_heroList, (void *)(std::intptr_t)i, row, 0);
		if (m_myHero && !m_myHero->hero().uniqueID.empty() && h.uniqueID == m_myHero->hero().uniqueID)
		{
			select = row;
		}
	}
	if (select < 0 && list->size() > 0)
	{
		select = 0;
	}
	if (select >= 0)
	{
		GadgetListBoxSetSelected(m_heroList, select);
		selectHero(select);
	}
	else
	{
		selectHero(-1);
	}
}

WindowMsgHandledType AptCreateAHero::gadgetMessage(GameWindow *from, std::uint32_t msg, WindowMsgData data1, WindowMsgData)
{
	if (msg == GLM_SELECTED && from == m_heroList) // RW 0x9C5114: 0x4014 from the hero list
	{
		const int row = (int)data1;
		selectHero(row);
		return MSG_HANDLED;
	}
	return MSG_IGNORED;
}

bool AptCreateAHero::completeAppearance()
{
	// RW 0x9C4054: from the Manager the hero is named and saved (RW 0x9C3EC9, then the Manager's save RW 0x9C4EE3); from the class page it goes on to
	// the powers (the movie shows Powers) and is named now (RW 0x9C3EC9 runs in both paths through the movie's OnComplete before ShowScreen)
	if (!m_myHero)
	{
		return false;
	}
	m_myHero->setName(trimmed(entryName())); // RW 0x80A352
	bumpDisplay();
	refreshTexts();
	if (m_editingExisting)
	{
		std::string error;
		CreateAHeroHeroList *list = m_env.createAHero ? m_env.createAHero->heroes : nullptr;
		if (!list || !list->save(m_myHero->hero(), &error, &m_selectedHero))
		{
			windows().note("create-a-hero", "saving the hero failed: " + (list ? error : std::string("no hero list")));
			return false;
		}
		m_listDirty = true;
	}
	return true;
}

bool AptCreateAHero::completePowers()
{
	// RW 0x9C181F: a new hero joins the list (RW 0x80CF9C) and is saved; an edited one is saved (RW 0x9C4EE3)
	if (!m_myHero)
	{
		return false;
	}
	if (m_myHero->hero().uniqueID.empty())
	{
		m_myHero->setName(trimmed(entryName()));
	}
	CreateAHeroHeroList *list = m_env.createAHero ? m_env.createAHero->heroes : nullptr;
	std::string error;
	if (!list || !list->save(m_myHero->hero(), &error, &m_selectedHero))
	{
		windows().note("create-a-hero", "saving the hero failed: " + (list ? error : std::string("no hero list")));
		return false;
	}
	m_listDirty = true;
	return true;
}

bool AptCreateAHero::deleteSelectedHero()
{
	// RW 0x9C5D6F: the selected hero, unless it is a system hero (and the flag RW 0xDE3D8C is clear), is kept (+0x10) and the question
	// GUI:AreYouSureDelete under GUI:DeleteFile is asked in a Yes / No box (RW 0x81A452(2)); its Yes (RW 0x9C5D16) deletes it
	CreateAHeroHeroList *list = m_env.createAHero ? m_env.createAHero->heroes : nullptr;
	const CreateAHeroListEntry *e = list ? list->at(m_selectedHero) : nullptr;
	if (!e || e->system)
	{
		return false;
	}
	m_lastMessage = "GUI:DeleteFile: GUI:AreYouSureDelete";
	if (!m_box)
	{
		m_box = std::make_unique<AptMessageBox>(windows(), shell());
	}
	m_deleteAnswer = -1;
	m_box->show(AptMessageBox::TYPE_YES_NO, fetchOrMissing(m_env.gameText, "GUI:DeleteFile"), fetchOrMissing(m_env.gameText, "GUI:AreYouSureDelete"),
		[this](int button) { m_deleteAnswer = button; });
	if (m_box->level() < 0)
	{
		windows().note("create-a-hero", "the delete question cannot be shown (no GuiFX.apt): the hero is kept");
		return false;
	}
	return true;
}

bool AptCreateAHero::removeSelectedHero()
{
	// RW 0x9C5D16: the kept hero is removed (RW 0x80CDA2: its file too) and the list refilled (RW 0x9C5A66)
	CreateAHeroHeroList *list = m_env.createAHero ? m_env.createAHero->heroes : nullptr;
	const CreateAHeroListEntry *e = list ? list->at(m_selectedHero) : nullptr;
	if (!e || e->system)
	{
		return false;
	}
	std::string error;
	if (!list->remove(m_selectedHero, &error))
	{
		windows().note("create-a-hero", "deleting the hero failed: " + error);
		return false;
	}
	m_selectedHero = -1;
	if (m_myHero)
	{
		m_myHero->hero().uniqueID.clear();
	}
	m_listDirty = true;
	return true;
}

bool AptCreateAHero::choosePower(int row, int column)
{
	// RW 0x9C238A: a cell that is not available shows its error (APT:HeroPowersError, the movie's ShowPowerErrorMessage); an available one is chosen
	CahPowers::Cell *c = m_powers.cell(row, column);
	if (!c)
	{
		return false;
	}
	if (c->state != CahPowers::STATE_AVAILABLE)
	{
		if (c->state > CahPowers::STATE_AVAILABLE && c->state <= CahPowers::STATE_BOOK_FULL && c->state != CahPowers::STATE_SELECTED)
		{
			windows().setAptText("APT:HeroPowersError", loadScreenU16ToUtf8(fetchOrMissing(m_env.gameText, stateTooltip(c->state))));
			invoke("ShowPowerErrorMessage", {});
		}
		return false;
	}
	m_pendingPower = c;
	return true;
}

bool AptCreateAHero::typeName(const std::u16string &name)
{
	if (!m_nameEntry)
	{
		return false;
	}
	if (AptGadgetLayer *layer = windows().gadgetLayer())
	{
		const std::vector<GameWindow *> live = layer->gadgets().allWindows();
		if (std::find(live.begin(), live.end(), m_nameEntry) == live.end())
		{
			m_nameEntry = nullptr;
			return false;
		}
	}
	// lane CAH-2 r2: typed character by character through the entry's own insert (RW 0x72260B: the validator RW 0x75E4DF and the 22 character limit
	// of RW 0x9C3F2A), as a player's keys reach it, never set past them; false when a character was refused
	GadgetTextEntrySetText(m_nameEntry, std::u16string());
	bool all = true;
	for (char16_t c : name)
	{
		all = GadgetTextEntryInsertCharacter(m_nameEntry, c) && all;
	}
	return all;
}

std::string AptCreateAHero::firstAvailablePower()
{
	for (int r = 0; r < m_powers.rowCount(); ++r)
	{
		for (int c = 0; c < CahPowers::NUM_COLUMNS; ++c)
		{
			const CahPowers::Cell *cell = m_powers.cell(r, c);
			if (cell && cell->state == CahPowers::STATE_AVAILABLE)
			{
				return std::to_string(r + 1) + "," + std::to_string(c + 1);
			}
		}
	}
	return std::string();
}

void AptCreateAHero::chooseNoPower()
{
	m_pendingPower = &m_powers.noPower();
}

void AptCreateAHero::refreshPowersPage()
{
	// the matrix icons and frames, the chosen powers, the palantir (RW 0x9C188E / 0x9C2061 / 0x9C28CA). The movie names its clips from 1: the rows of mcRows,
	// the cells of a row, the PowersList entries ("SelectPower_" + row + "_" + cell, "MyPowerIcon_" + entry, "$MyPowerLevel_" + entry); OnPowerSelect sends
	// those 1-based numbers back (RW 0x9C23BC: decremented)
	m_powersDirty = false;
	// (the row count: the movie reads NumPowerRows itself when it loads and calls its SetNumPowerRows; a string argument would compare as text there)
	for (int r = 0; r < m_powers.rowCount(); ++r)
	{
		for (int c = 0; c < CahPowers::NUM_COLUMNS; ++c)
		{
			CahPowers::Cell *cell = m_powers.cell(r, c);
			const std::string rr = std::to_string(r + 1), cc = std::to_string(c + 1);
			setImage("SelectPower_" + rr + "_" + cc, cell ? powerIcon(cell->button) : std::string()); // "%s_%d_%d" RW 0xC8AA0C
			invoke("UpdateSelectPowerIcon", { rr, cc, cell ? stateFrame(cell->state) : "_unused" });
		}
	}
	auto powerName = [this](const CommandButton *b) {
		std::u16string text = u" ";
		if (b && !b->m_textLabel.empty())
		{
			text = fetchOrMissing(m_env.gameText, b->m_textLabel.front());
			// the label's '&' marks the hot key (the control bar's accelerator); INFERENCE (S-1404): the builder's lists show the text without it
			text.erase(std::remove(text.begin(), text.end(), u'&'), text.end());
		}
		return text;
	};
	for (int r = 0; r < m_powers.rowCount(); ++r)
	{
		// "$PowerName_" + row: the row's name, its first power's (INFERENCE, S-1404)
		const CahPowers::Cell *first = nullptr;
		for (int c = 0; c < CahPowers::NUM_COLUMNS && !first; ++c)
		{
			first = m_powers.cell(r, c);
		}
		windows().setAptText("APT:PowerName_" + std::to_string(r + 1), loadScreenU16ToUtf8(powerName(first ? first->button : nullptr)));
	}
	const std::vector<CahPowers::Cell *> &chosen = m_powers.chosen();
	for (int i = 0; i < CahPowers::MAX_POWERS; ++i)
	{
		const CahPowers::Cell *c = (size_t)i < chosen.size() ? chosen[(size_t)i] : nullptr;
		setImage("MyPowerIcon_" + std::to_string(i + 1), c ? powerIcon(c->button) : std::string());
		// "APT:%s_%d" (RW 0xC8AA60): the power chosen for the rank, its TextLabel's text
		windows().setAptText("APT:MyPowerLevel_" + std::to_string(i + 1), loadScreenU16ToUtf8(powerName(c ? c->button : nullptr)));
	}
	// the palantir: the template's buttons, then the chosen rows' latest power by slot
	const CommandSet *t = TheCommandStore && m_env.createAHero && m_env.createAHero->system ? TheCommandStore->findCommandSet(m_env.createAHero->system->commandSetTemplate) : nullptr;
	for (int i = 0; i < CahPowers::PALANTIR_BUTTONS; ++i)
	{
		const CommandButton *b = nullptr;
		if (i < m_powers.templateButtons())
		{
			b = t ? t->getCommandButton(i) : nullptr;
		}
		else
		{
			for (const CahPowers::Cell *c : chosen)
			{
				if (c->selected && c->palantirSlot + m_powers.templateButtons() == i)
				{
					b = c->button;
				}
			}
		}
		setImage("PalantirBttn_" + std::to_string(i + 1), powerIcon(b)); // RW 0x9C1270(button, "PalantirBttn", i, -1)
	}
	setImage("CAHNoPower", powerIcon(m_powers.noPower().button)); // RW 0x9C2061
	invoke("SetPowersSelectedNum", { std::to_string(m_powers.chosenCount()) });
	if (m_myHero)
	{
		invoke("UpdateHeroBuildCost", { std::to_string(m_myHero->buildCost()) });
	}
	// the wizard line (APT:HeroPowersWizard, the movie's OnNewWizardMessage; labels RW 0xC8AD2C .. 0xC8AE00). INFERENCE (S-1404): which label when: the first,
	// the second, the next power by its rank, the palantir full, no available power, the selection complete
	std::string wizard;
	int slotArgument = -1;
	const int n = m_powers.chosenCount();
	bool anyAvailable = false;
	bool palantirFull = false;
	for (int r = 0; r < m_powers.rowCount(); ++r)
	{
		for (int c = 0; c < CahPowers::NUM_COLUMNS; ++c)
		{
			if (const CahPowers::Cell *cell = m_powers.cell(r, c))
			{
				anyAvailable = anyAvailable || cell->state == CahPowers::STATE_AVAILABLE;
				palantirFull = palantirFull || cell->state == CahPowers::STATE_PALANTIR_FULL;
			}
		}
	}
	if (n >= CahPowers::MAX_POWERS)
	{
		wizard = "WIZARD:CahPowersSelectionComplete";
	}
	else if (!anyAvailable)
	{
		wizard = palantirFull ? "WIZARD:CahPowersPalantirFull" : "WIZARD:CahPowersNoAvailablePowers";
	}
	else if (palantirFull)
	{
		wizard = "WIZARD:CahPowersUpgradeOrNewPower";
	}
	else if (n == 0)
	{
		wizard = "WIZARD:CahPowersSelectFirstPower";
	}
	else if (n == 1)
	{
		wizard = "WIZARD:CahPowersSelectSecondPower";
	}
	else
	{
		// "WIZARD:CahPowersSelectNextPower_%d" (RW 0xC8AD50) of the tier the next rank reaches (the texts: _1 levels 1 or 3, _2 levels 1, 3 or 7, _3 any),
		// its text formatted with the slot number
		static const int kTier[10] = { 0, 0, 1, 1, 1, 1, 2, 2, 2, 3 };
		wizard = "WIZARD:CahPowersSelectNextPower_" + std::to_string(std::max(1, kTier[std::min(n, 9)]));
		slotArgument = n + 1;
	}
	std::u16string wizardText = fetchOrMissing(m_env.gameText, wizard);
	const size_t pd = wizardText.find(u"%d");
	if (slotArgument > 0 && pd != std::u16string::npos)
	{
		wizardText.replace(pd, 2, asciiToU16(std::to_string(slotArgument)));
	}
	windows().setAptText("APT:HeroPowersWizard", loadScreenU16ToUtf8(wizardText));
	invoke("OnNewWizardMessage", {});
}

void AptCreateAHero::update()
{
	if (m_box)
	{
		m_box->update();
		if (m_deleteAnswer >= 0)
		{
			const int answer = m_deleteAnswer;
			m_deleteAnswer = -1;
			if (answer == AptMessageBox::BUTTON_YES)
			{
				removeSelectedHero();
			}
		}
	}
	if (m_listDirty && m_page == 'M')
	{
		fillHeroList();
	}
	if (m_page == 'P' && m_myHero)
	{
		// the page's update: a take-back first (+0x28), then the pending choice (+0x20)
		if (m_keepPowers < m_powers.chosenCount())
		{
			m_powers.truncate(m_keepPowers, m_myHero->hero());
			m_powersDirty = true;
			bumpDisplay();
		}
		m_keepPowers = CahPowers::MAX_POWERS;
		if (m_pendingPower)
		{
			CahPowers::Cell *c = m_pendingPower;
			m_pendingPower = nullptr;
			if (m_powers.select(c, m_myHero->hero()))
			{
				m_powersDirty = true;
				bumpDisplay();
				refreshTexts();
				if (c->selected)
				{
					invoke("FlashPalantir", { std::to_string(c->palantirSlot + 1 + m_powers.templateButtons()) }); // RW 0x9C29D7
				}
			}
		}
		if (m_powersDirty)
		{
			refreshPowersPage();
		}
		if (m_powersScrollUpdates > 0 && level() >= 0)
		{
			// INFERENCE (S-1404): the matrix starts at its first row. The movie's rows sit above their mask until the VerticalScrollBar reports a position
			// (OnScrollBarLoaded's OnPosChanged -> OnScrollTo), which this player's scroll bar does not do on its own; the first position is given here
			// during the page's first updates (its timeline places mcRows again while it plays in)
			--m_powersScrollUpdates;
			std::string error;
			windows().invokeASAt(level(), "Main.mcPowersScreen.Screen.mcPowersMenu", "OnScrollTo", { "0" }, nullptr, &error);
		}
	}
}
