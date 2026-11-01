// OpenBFME. GPL-3.0.
// See GameClient/GUI/MenuNavigator.h (lane OPTS-6; not retail).

#include "GameClient/GUI/MenuNavigator.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"

#include <algorithm>
#include <cctype>
#include <cmath>

// Layout and client input only: no simulation state is read or written (tools/sim/sim_policy.json lists the file as client UI).

const char *menuNavKeyName(MenuNavKey key)
{
	switch (key)
	{
		case MenuNavKey::Up: return "up";
		case MenuNavKey::Down: return "down";
		case MenuNavKey::Left: return "left";
		case MenuNavKey::Right: return "right";
		case MenuNavKey::Next: return "next";
		case MenuNavKey::Previous: return "previous";
		case MenuNavKey::Activate: return "activate";
		case MenuNavKey::Back: return "back";
		case MenuNavKey::PageNext: return "page-next";
		case MenuNavKey::PagePrevious: return "page-previous";
	}
	return "?";
}

namespace
{
void collectButtons(AptCharacterInst *c, std::vector<AptButtonInst *> &out)
{
	if (!c)
	{
		return;
	}
	if (AptButtonInst *b = c->asButton())
	{
		out.push_back(b);
	}
	if (AptSpriteInst *s = c->asSprite())
	{
		for (AptCharacterInst *k : s->children())
		{
			collectButtons(k, out);
		}
	}
}

// reading order: rows (centres within half the smaller height of each other count as one row), then left to right; ties by id (no pointer order)
bool readingBefore(const MenuNavTarget &a, const MenuNavTarget &b)
{
	const float tol = 0.5f * std::min(a.y1 - a.y0, b.y1 - b.y0);
	if (std::fabs(a.cy() - b.cy()) > tol)
	{
		return a.cy() < b.cy();
	}
	if (a.cx() != b.cx())
	{
		return a.cx() < b.cx();
	}
	return a.id < b.id;
}

// shown up its chain and not faded out (AptInput's clip walk test, RW 0xAF4DE0: alpha under one half is not shown). The button hit list itself has no
// alpha test (a closed sub-menu's alpha 0 buttons can still be hit): the navigation only goes to what a player can see
bool seen(const AptCharacterInst &inst)
{
	for (const AptCharacterInst *c = &inst; c; c = c->parent())
	{
		if (!c->visible || c->color.mul[3] < 0.5f)
		{
			return false;
		}
	}
	return true;
}

// the target's box meets the stage (a control laid out above or beside it, hidden by its layout, is not one a player can see or press)
bool onStage(const MenuNavTarget &t, float stageW, float stageH)
{
	return t.x1 > 0.0f && t.y1 > 0.0f && t.x0 < stageW && t.y0 < stageH;
}

std::string lower(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

std::string leafOf(const std::string &path)
{
	const std::size_t dot = path.find_last_of('.');
	return dot == std::string::npos ? path : path.substr(dot + 1);
}

int sectionOf(const MenuNavTarget &target)
{
	const std::string leaf = leafOf(target.id);
	if (leaf == "Chat" || leaf == "ChatPlayers" || leaf == "ChatEntry") return 2;
	if (target.id.find(".MpMapAndSettings.") != std::string::npos ||
		target.id.find(".MpMapSettings.") != std::string::npos ||
		target.id.find(".TabButtons.") != std::string::npos ||
		target.id.find(".TabMap.") != std::string::npos ||
		target.id.find(".MapsTabSelected.") != std::string::npos ||
		target.id.find(".RulesTabSelected.") != std::string::npos ||
		target.id.find(".ChatTabSelected.") != std::string::npos ||
		target.id.find(".TabChat.") != std::string::npos) return 2;
	if (target.id.find(".lobby.") != std::string::npos || target.id.find(".MainButtons.") != std::string::npos || target.id.find(".ProfileNav.") != std::string::npos ||
		target.id.find(".LowerButtons_") != std::string::npos || target.id.find(".MessengerButton.") != std::string::npos) return 0;
	return 1;
}

bool isImageCombo(GameWindow *w)
{
	using Input = WindowMsgHandledType (*)(GameWindow *, std::uint32_t, WindowMsgData, WindowMsgData);
	const auto input = w->winGetInputFunc();
	const auto *fn = input.target<Input>();
	return fn && *fn == GadgetImageComboBoxInput;
}

bool isTextEntry(GameWindow *w)
{
	return w && BitTest(w->winGetStyle(), GWS_ENTRY_FIELD);
}
} // namespace

MenuNavigator::MenuNavigator(WindowManager &windows) : m_windows(windows)
{
}

const std::vector<std::string> &MenuNavigator::backButtonNames()
{
	// lane OPTS-6 review: "mainmenu" is the name of the LAN lobby's and the lobbies' own way back (MainButtons.MainMenu: the label is MAIN MENU), "exit" the
	// online login's (OnlineLogin.Exit: it leaves for the main menu)
	static const std::vector<std::string> names = { "back", "buttonback", "backbutton", "cancel", "buttoncancel", "close", "buttonclose", "returntogame", "resume", "mainmenu", "exit" };
	return names;
}

void MenuNavigator::setMenu(int level, const void *screen)
{
	if (level != m_level || screen != m_screen)
	{
		m_level = level;
		m_screen = screen;
		m_focusId.clear();
		m_resultPage.clear();
		m_controllerResults = false;
		m_frameShown = false;
		m_controllerSections = false;
		m_section = 1;
	}
}

bool MenuNavigator::supportsControllerSections() const
{
	// All three lobby screens host MpGameSetup. Detect its visible controls,
	// so browsing games, signing in, and profile pages keep normal navigation.
	std::vector<MenuNavTarget> list;
	collect(list, nullptr, false);
	bool players = false;
	for (const auto &target : list)
	{
		players |= target.id.find(".PlayerList.") != std::string::npos;
	}
	return players;
}

void MenuNavigator::setControllerSections(bool enabled)
{
	if (m_controllerSections == enabled) return;
	m_controllerSections = enabled;
	clearFocus();
}

void MenuNavigator::setControllerResults(bool enabled)
{
	m_controllerResults = enabled;
	if (!enabled) { m_resultPage.clear(); return; }
	const auto list = targets();
	// Prefer the preview's Skip, then More, then the statistics' Continue.
	// Only transfer focus on a page change; the player can still browse tabs.
	for (const char *name : { "Skip", "More", "Continue" })
	{
		const std::string path = std::string(".MainButtons.") + name;
		for (const auto &target : list)
		{
			if (target.id.find(path) == std::string::npos) continue;
			if (m_resultPage != name)
			{
				m_resultPage = name;
				focus(target);
			}
			return;
		}
	}
}

void MenuNavigator::switchControllerSection(int delta)
{
	if (!m_controllerSections || m_windows.componentInputBlocked()) return;
	if (auto *layer = m_windows.gadgetLayer()) layer->gadgets().winSetLoneWindow(nullptr);
	m_section = (m_section + (delta < 0 ? 2 : 1)) % 3;
	clearFocus();
	const auto list = targets();
	if (!list.empty()) focus(list.front());
}

void MenuNavigator::clearFocus()
{
	m_focusId.clear();
	m_frameShown = false;
}

void MenuNavigator::collect(std::vector<MenuNavTarget> &out, std::vector<MenuNavTarget> *catchers, bool section) const
{
	out.clear();
	if (catchers)
	{
		catchers->clear();
	}
	if (m_level < 0)
	{
		return;
	}
	Apt &apt = m_windows.apt();
	// the level's Apt buttons the pointer could press at their centre now
	std::vector<AptButtonInst *> buttons;
	collectButtons(apt.level(m_level), buttons);
	const float stageW = 1024.0f, stageH = 768.0f; // the menus' stage (AptStageMapping)
	for (AptButtonInst *b : buttons)
	{
		// Use the authored hit area, not the current state's artwork bounds.
		// Hover animations/dropdowns can replace that artwork after focus moves.
		float box[4];
		if (!b->hitBounds(box)) continue;
		MenuNavTarget t;
		t.kind = MenuNavTarget::Kind::Button;
		t.x0 = box[0]; t.y0 = box[1];
		t.x1 = box[2]; t.y1 = box[3];
		if (t.x1 - t.x0 >= 0.9f * stageW && t.y1 - t.y0 >= 0.9f * stageH)
		{
			if (catchers && b->enabled)
			{
				t.id = b->targetPath();
				catchers->push_back(std::move(t)); // a click-away area, not a control (see the header)
			}
			continue;
		}
		if (!seen(*b) || apt.input().hitTestButtons(t.cx(), t.cy()) != b || !onStage(t, stageW, stageH))
		{
			continue;
		}
		t.id = b->targetPath();
		out.push_back(std::move(t));
	}
	// the level's gadgets
	if (AptGadgetLayer *layer = m_windows.gadgetLayer())
	{
		GameWindowManager &gwm = layer->gadgets();
		for (const WindowManager::ComponentRecord &r : m_windows.components())
		{
			GameWindow *w = r.window.get();
			if (r.level != m_level || !w || !r.instance || !gwm.winIsAlive(w) || gwm.isHidden(w) || !gwm.isEnabled(w))
			{
				continue;
			}
			// lane OPTS-6 review: a gadget of a clip that is hidden or faded (the LAN lobby's create-game components, laid out above the stage) is not a target
			if (!seen(*r.instance))
			{
				continue;
			}
			const std::uint32_t style = w->winGetStyle();
			if (!isImageCombo(w) && !(style & (GWS_PUSH_BUTTON | GWS_CHECK_BOX | GWS_HORZ_SLIDER | GWS_SCROLL_LISTBOX | GWS_ENTRY_FIELD | GWS_COMBO_BOX)))
			{
				continue;
			}
			int x = 0, y = 0, cw = 0, ch = 0;
			w->winGetScreenPosition(&x, &y);
			w->winGetSize(&cw, &ch);
			if (cw <= 0 || ch <= 0)
			{
				continue;
			}
			MenuNavTarget t;
			t.kind = MenuNavTarget::Kind::Gadget;
			t.id = r.instancePath;
			t.window = w;
			t.x0 = (float)x;
			t.y0 = (float)y;
			t.x1 = (float)(x + cw);
			t.y1 = (float)(y + ch);
			if (!onStage(t, stageW, stageH))
			{
				continue; // laid out outside the stage: nothing a player can reach
			}
			out.push_back(std::move(t));
		}
	}
	if (MenuNavigationSource *source = m_windows.navigationSource())
	{
		source->navTargets(out);
	}
	// Player rows laid out below the bottom toolbar are not visible controls.
	// Do not let LOCAL16's extra rows extend the controller region behind it.
	if (section && m_controllerSections && m_section == 1)
	{
		float toolbarTop = stageH;
		for (const auto &t : out)
			if (sectionOf(t) == 0 && t.id.find(".MessengerButton.") == std::string::npos && t.id.find(".ProfileNav.") == std::string::npos)
				toolbarTop = std::min(toolbarTop, t.y0);
		out.erase(std::remove_if(out.begin(), out.end(), [toolbarTop](const MenuNavTarget &t) {
			return sectionOf(t) == 1 && t.y1 >= toolbarTop;
		}), out.end());
	}
	if (section && m_controllerSections && m_section == 0 &&
		std::any_of(out.begin(), out.end(), [](const MenuNavTarget &t) { return t.id.find(".ProfileNav.ChangeProfile.") != std::string::npos; }))
	{
		// DisableComponents blocks the native gadgets, but they still exist
		// geometrically behind the movie dropdown. They are not its choices.
		out.erase(std::remove_if(out.begin(), out.end(), [](const MenuNavTarget &t) { return t.id.find(".ProfileNav.") == std::string::npos; }), out.end());
	}
	const bool popup = std::any_of(out.begin(), out.end(), [](const MenuNavTarget &t) { return t.id.find(".ProfilePopup.") != std::string::npos; });
	if (section && m_controllerSections && !popup && !m_windows.componentInputBlocked())
		out.erase(std::remove_if(out.begin(), out.end(), [this](const MenuNavTarget &t) { return sectionOf(t) != m_section; }), out.end());
	std::stable_sort(out.begin(), out.end(), readingBefore);
}

std::vector<MenuNavTarget> MenuNavigator::targets() const
{
	std::vector<MenuNavTarget> out;
	collect(out);
	return out;
}

std::vector<MenuNavTarget> MenuNavigator::catchers() const
{
	std::vector<MenuNavTarget> list, out;
	collect(list, &out);
	return out;
}

const MenuNavTarget *MenuNavigator::find(const std::vector<MenuNavTarget> &list, const std::string &id) const
{
	for (const MenuNavTarget &t : list)
	{
		if (t.id == id)
		{
			return &t;
		}
	}
	return nullptr;
}

bool MenuNavigator::focusBox(float out[4]) const
{
	if (m_focusId.empty())
	{
		return false;
	}
	std::vector<MenuNavTarget> list;
	collect(list);
	const MenuNavTarget *t = find(list, m_focusId);
	if (!t)
	{
		return false;
	}
	out[0] = t->x0;
	out[1] = t->y0;
	out[2] = t->x1;
	out[3] = t->y1;
	return true;
}

void MenuNavigator::appendFocusFrame(GadgetDrawList &out) const
{
	if (!m_frameShown && !m_controllerSections) return;
	// Authored movie buttons already show their Over artwork or rollover
	// animation through focus()'s pointer move. Do not cover it with a box.
	const auto list = targets();
	if (m_controllerSections && !list.empty() && !m_windows.componentInputBlocked())
	{
		float x0 = 1024, y0 = 768, x1 = 0, y1 = 0;
		for (const auto &t : list) { x0 = std::min(x0, t.x0); y0 = std::min(y0, t.y0); x1 = std::max(x1, t.x1); y1 = std::max(y1, t.y1); }
		// Quiet corner accents in the native menu's slate/pale-blue palette.
		// Leave the authored panel borders and headings visible.
		const int left = (int)x0 - 5, top = (int)y0 - 5;
		const int right = (int)x1 + 5, bottom = (int)y1 + 5;
		for (int corner = 0; corner < 4; ++corner)
		{
			const int x = (corner & 1) ? right : left;
			const int y = (corner & 2) ? bottom : top;
			const int dx = (corner & 1) ? -14 : 14;
			const int dy = (corner & 2) ? -14 : 14;
			for (int edge = 0; edge < 2; ++edge)
			{
				GadgetDrawCommand accent;
				accent.kind = GadgetDrawCommand::Kind::Line;
				accent.window = "ControllerSection";
				accent.x0 = x; accent.y0 = y;
				accent.x1 = x + (edge ? 0 : dx);
				accent.y1 = y + (edge ? dy : 0);
				accent.color = edge ? 0xFF738090u : 0xFFBFEDFFu;
				accent.lineWidth = 1;
				out.commands.push_back(accent);
			}
		}
	}
	if (const auto *target = find(list, m_focusId); target && target->kind == MenuNavTarget::Kind::Button)
	{
		const auto *button = m_windows.apt().input().hitTestButtons(target->cx(), target->cy());
		if (button && button->info())
		{
			for (const auto &record : button->info()->records)
				if (bool(record.stateMask & 1) != bool(record.stateMask & 2)) return;
			for (const auto &action : button->info()->actions)
				if (action.transitionMask & 3) return; // rollover / rollout actions
		}
	}
	if (const auto *target = find(list, m_focusId); target && target->window &&
		(target->window->winGetStyle() & (GWS_PUSH_BUTTON | GWS_CHECK_BOX)))
	{
		GameWindow *w = target->window;
		for (int i = 0; i < MAX_DRAW_DATA; ++i)
		{
			if (!w->winGetHiliteImage(i).empty() && w->winGetHiliteImage(i) != w->winGetEnabledImage(i)) return;
			if ((w->winGetHiliteColor(i) >> 24) && w->winGetHiliteColor(i) != w->winGetEnabledColor(i)) return;
		}
		if ((w->winGetHiliteTextColor() >> 24) && w->winGetHiliteTextColor() != w->winGetEnabledTextColor()) return;
	}
	float box[4];
	if (!m_frameShown || !focusBox(box))
	{
		return;
	}
	for (int inset = 2; inset <= 3; ++inset) // two stage pixels wide
	{
		GadgetDrawCommand c;
		c.kind = GadgetDrawCommand::Kind::OpenRect;
		c.window = "MenuFocus";
		c.x0 = (int)std::floor(box[0]) - inset;
		c.y0 = (int)std::floor(box[1]) - inset;
		c.x1 = (int)std::ceil(box[2]) + inset;
		c.y1 = (int)std::ceil(box[3]) + inset;
		c.color = kFrameColor;
		out.commands.push_back(std::move(c));
	}
}

void MenuNavigator::focus(const MenuNavTarget &t)
{
	m_focusId = t.id;
	m_focusTarget = t;
	m_frameShown = true;
	m_log.push_back("focus " + t.id);
	// the pointer's hover: the button's Over state, the gadget's highlight, the page's help line (WindowManager::postMouseMove: the mouse's path)
	m_windows.postMouseMove(t.cx(), t.cy());
	if (AptGadgetLayer *layer = m_windows.gadgetLayer())
	{
		GameWindowManager &gwm = layer->gadgets();
		GameWindow *w = t.window;
		if (w && BitTest(w->winGetStyle(), GWS_COMBO_BOX))
		{
			// lane ONLINEUX-1: an editable combo box (the online login's nickname: AptOnlineShell sets it editable) takes the typed characters in its edit
			// box, as a click on it does (GadgetComboBox: the edit box is the combo's entry field); a drop-down-only combo keeps its own keys (dropDownKey)
			const ComboBoxData *c = static_cast<const ComboBoxData *>(w->winGetUserData());
			w = c && c->isEditable && c->editBox ? c->editBox : nullptr;
		}
		if (w && (w->winGetStyle() & (GWS_SCROLL_LISTBOX | GWS_ENTRY_FIELD)))
		{
			gwm.winSetFocus(w); // a list box takes Up / Down / Enter itself, a text entry the typed characters
		}
		else if (GameWindow *had = gwm.winGetFocus(); had && (had->winGetStyle() & (GWS_SCROLL_LISTBOX | GWS_ENTRY_FIELD)))
		{
			gwm.winSetFocus(nullptr); // leaving them: the keys are the navigation's again
		}
	}
}

void MenuNavigator::press(const MenuNavTarget &t)
{
	m_log.push_back("press " + t.id);
	m_windows.postMouseMove(t.cx(), t.cy());
	m_windows.postMouseButton(true);
	m_windows.postMouseButton(false);
}

bool MenuNavigator::textEntryFocused() const
{
	AptGadgetLayer *layer = m_windows.gadgetLayer();
	return layer && isTextEntry(layer->gadgets().winGetFocus());
}

int MenuNavigator::nearest(const std::vector<MenuNavTarget> &list, int from, MenuNavKey dir) const
{
	const MenuNavTarget &a = list[(std::size_t)from];
	int best = -1;
	float bestScore = 0;
	for (std::size_t i = 0; i < list.size(); ++i)
	{
		if ((int)i == from)
		{
			continue;
		}
		const MenuNavTarget &b = list[i];
		// Horizontal navigation stays in the current row. Adjacent player
		// rows can touch/overlap, so edge distance alone admits diagonals.
		if (!(m_controllerSections && m_section == 0) && (dir == MenuNavKey::Left || dir == MenuNavKey::Right) &&
			std::fabs(a.cy() - b.cy()) > 0.5f * std::min(a.y1 - a.y0, b.y1 - b.y0)) continue;
		float along = 0, across = 0; // the distance the way the key goes (positive) and beside it
		switch (dir)
		{
			case MenuNavKey::Up: along = a.y0 - b.y1; across = std::max({ 0.0f, b.x0 - a.x1, a.x0 - b.x1 }); break;
			case MenuNavKey::Down: along = b.y0 - a.y1; across = std::max({ 0.0f, b.x0 - a.x1, a.x0 - b.x1 }); break;
			case MenuNavKey::Left: along = a.x0 - b.x1; across = std::max({ 0.0f, b.y0 - a.y1, a.y0 - b.y1 }); break;
			case MenuNavKey::Right: along = b.x0 - a.x1; across = std::max({ 0.0f, b.y0 - a.y1, a.y0 - b.y1 }); break;
			default: return -1;
		}
		// boxes may overlap a little (a row's gadget and its neighbours): the centres must also lie that way
		const float centre = dir == MenuNavKey::Up ? a.cy() - b.cy() : dir == MenuNavKey::Down ? b.cy() - a.cy() : dir == MenuNavKey::Left ? a.cx() - b.cx() : b.cx() - a.cx();
		if (centre <= 0.5f)
		{
			continue;
		}
		const float score = m_controllerSections && m_section == 0 && (dir == MenuNavKey::Left || dir == MenuNavKey::Right)
			? centre : std::max(along, 0.0f) + 4.0f * across;
		if (best < 0 || score < bestScore)
		{
			best = (int)i;
			bestScore = score;
		}
	}
	return best;
}

void MenuNavigator::back(const std::vector<MenuNavTarget> &list)
{
	const MenuNavTarget *only = nullptr;
	int count = 0;
	for (const MenuNavTarget &t : list)
	{
		if (t.kind != MenuNavTarget::Kind::Button)
		{
			continue;
		}
		// the button's own name or its parent clip's (Skirmish.apt's "ButtonBack.TheButton" style)
		std::string path = lower(t.id);
		const std::string leaf = leafOf(path);
		std::string parent;
		if (path.size() > leaf.size() + 1)
		{
			parent = leafOf(path.substr(0, path.size() - leaf.size() - 1));
		}
		for (const std::string &n : backButtonNames())
		{
			if (leaf == n || parent == n)
			{
				only = &t;
				++count;
				break;
			}
		}
	}
	if (count == 1)
	{
		m_log.push_back("back " + only->id);
		press(*only);
	}
	else
	{
		m_log.push_back(count == 0 ? "back none" : "back ambiguous");
	}
}

bool MenuNavigator::leave(const std::vector<MenuNavTarget> &catchers, const std::vector<MenuNavTarget> &list)
{
	Apt &apt = m_windows.apt();
	for (const MenuNavTarget &c : catchers)
	{
		// a point of the area no target covers and the input gives to the area: its corners and edge middles, 6 stage pixels in
		const float xs[3] = { c.x0 + 6, c.cx(), c.x1 - 6 }, ys[3] = { c.y0 + 6, c.cy(), c.y1 - 6 };
		for (float y : ys)
		{
			for (float x : xs)
			{
				bool covered = false;
				for (const MenuNavTarget &t : list)
				{
					covered = covered || (x >= t.x0 - 4 && x <= t.x1 + 4 && y >= t.y0 - 4 && y <= t.y1 + 4);
				}
				AptButtonInst *hit = apt.input().hitTestButtons(x, y);
				if (!covered && hit && hit->targetPath() == c.id)
				{
					m_log.push_back("leave " + c.id);
					m_windows.postMouseMove(x, y);
					return true;
				}
			}
		}
	}
	return false;
}

bool MenuNavigator::dropDownKey(MenuNavKey key, GameWindow *combo)
{
	GameWindowManager &gwm = m_windows.gadgetLayer()->gadgets();
	const bool image = isImageCombo(combo);
	GameWindow *list = image ? GadgetImageComboBoxGetListBox(combo) : GadgetComboBoxGetListBox(combo);
	if (!list) return false;
	auto *data = static_cast<ListboxData *>(list->winGetUserData());
	if (!data) return false;
	switch (key)
	{
		case MenuNavKey::Up:
		case MenuNavKey::Down:
		{
			// Preview through the list's native hover highlight; selection and
			// GCM_SELECTED stay untouched until the second A press.
			const int count = image ? GadgetImageComboBoxGetLength(combo) : GadgetComboBoxGetLength(combo);
			int index = data->hoverActive ? data->hoverPos : data->selectPos;
			if (count > 0)
			{
				index = std::clamp(index + (key == MenuNavKey::Up ? -1 : 1), 0, count - 1);
				data->trackHover = data->hoverActive = true;
				data->hoverPos = index;
				if (index < GadgetListBoxGetTopVisibleEntry(list)) GadgetListBoxSetTopVisibleEntry(list, index);
				if (index > GadgetListBoxGetBottomVisibleEntry(list)) GadgetListBoxSetBottomVisibleEntry(list, index);
			}
			return true;
		}
		case MenuNavKey::Activate:
		{
			const int index = data->hoverActive ? data->hoverPos : data->selectPos;
			if (index >= 0)
			{
				data->selectPos = index;
				gwm.winSendSystemMsg(combo, GLM_SELECTED, reinterpret_cast<WindowMsgData>(list), (WindowMsgData)index);
			}
			gwm.winSetLoneWindow(nullptr);
			return true;
		}
		case MenuNavKey::Back:
			gwm.winSetLoneWindow(nullptr); // cancel the preview
			return true;
		default:
			return true;
	}

}

bool MenuNavigator::gadgetKey(MenuNavKey key, const MenuNavTarget &t)
{
	GameWindow *w = t.window;
	GameWindowManager &gwm = m_windows.gadgetLayer()->gadgets();
	const std::uint32_t style = w->winGetStyle();
	if (style & GWS_HORZ_SLIDER)
	{
		if (key == MenuNavKey::Activate)
		{
			return true; // a press at its centre would move it there (GWM_LEFT_UP's track click): nothing
		}
		if (key != MenuNavKey::Left && key != MenuNavKey::Right)
		{
			return false;
		}
		int lo = 0, hi = 0;
		GadgetSliderGetMinMax(w, &lo, &hi);
		const int step = std::max(1, (hi - lo) / kSliderSteps);
		const int pos = GadgetSliderGetPosition(w);
		const int next = std::clamp(pos + (key == MenuNavKey::Left ? -step : step), lo, hi);
		if (next != pos)
		{
			// a drag's messages (GadgetHorizontalSliderSystem GGM_LEFT_DRAG: GSM_SLIDER_TRACK to the owner), then the end of a track click (RW 0x7234EC: 0x4010)
			GadgetSliderSetPosition(w, next);
			gwm.winSendSystemMsg(w->winGetOwner(), GSM_SLIDER_TRACK, reinterpret_cast<WindowMsgData>(w), (WindowMsgData)GadgetSliderGetPosition(w));
			gwm.winSendSystemMsg(w->winGetOwner(), GSM_SLIDER_CLICKED, reinterpret_cast<WindowMsgData>(w), 0);
			m_log.push_back("slider " + t.id + " " + std::to_string(GadgetSliderGetPosition(w)));
		}
		return true;
	}
	if ((style & GWS_COMBO_BOX) || isImageCombo(w))
	{
		if (key != MenuNavKey::Activate) return false; // closed choices are navigation targets
		gwm.winSendInputMsg(w, GWM_LEFT_UP, 0, 0);
		return true;
	}
	if (style & GWS_SCROLL_LISTBOX)
	{
		// ZH GadgetListBoxInput's GWM_CHAR: Up / Down move the selection (GLM_SELECTED to the owner), Enter is a double click (GLM_DOUBLE_CLICKED)
		const int dik = key == MenuNavKey::Up ? KEY_UP : key == MenuNavKey::Down ? KEY_DOWN : key == MenuNavKey::Activate ? KEY_ENTER : 0;
		if (!dik)
		{
			return false;
		}
		if (key == MenuNavKey::Up || key == MenuNavKey::Down)
		{
			int sel = -1;
			GadgetListBoxGetSelected(w, &sel);
			const int count = GadgetListBoxGetNumEntries(w);
			if ((key == MenuNavKey::Up && sel <= 0) || (key == MenuNavKey::Down && sel >= count - 1))
			{
				return false; // at the list's end the arrow leaves it
			}
		}
		gwm.winSendInputMsg(w, GWM_CHAR, (WindowMsgData)dik, (WindowMsgData)KEY_STATE_DOWN);
		m_log.push_back("list " + t.id + " " + menuNavKeyName(key));
		return true;
	}
	return false;
}

bool MenuNavigator::key(MenuNavKey key)
{
	if (m_level < 0)
	{
		return false;
	}
	if (!m_noted)
	{
		m_noted = true;
		m_windows.note("menu-navigation", "[S-4981] keyboard / controller navigation of the menus (OpenBFME, not retail): the click-away areas (a button over 90 % "
			"of the stage), the hidden buttons (alpha under one half) and the back buttons by name (backButtonNames) are inferences");
	}
	AptGadgetLayer *layer = m_windows.gadgetLayer();
	if (m_windows.componentInputBlocked())
	{
		// a message box of the movie has the input (DisableComponents()): its own movie buttons are on another level. A nav clip over this level's gadgets
		// blocks them the same way while its sub-menu is open (Skirmish.apt's PROFILE), and then the click-away area of this level is the way out: the
		// keys are the navigation's (they close the sub-menu), or a keyboard user focusing such a nav button would be locked out of the screen (lane OPTS-6 review)
		std::vector<MenuNavTarget> list, catchers;
		collect(list, &catchers);
		if (catchers.empty())
		{
			return false;
		}
	}
	if (layer)
	{
		if (const GameWindow *drop = layer->openDropDown())
		{
			if (key == MenuNavKey::PageNext || key == MenuNavKey::PagePrevious)
			{
				layer->gadgets().winSetLoneWindow(nullptr); // shoulder changes cancel an unconfirmed choice
			}
			else return dropDownKey(key, const_cast<GameWindow *>(drop));
		}
	}
	// a text entry with the keyboard focus edits with these itself
	if (textEntryFocused() && (key == MenuNavKey::Left || key == MenuNavKey::Right || key == MenuNavKey::Activate))
	{
		return false;
	}
	std::vector<MenuNavTarget> list, catchers;
	collect(list, &catchers, key != MenuNavKey::PageNext && key != MenuNavKey::PagePrevious);
	const MenuNavTarget *current = find(list, m_focusId);
	if (m_controllerResults && (key == MenuNavKey::Back || (key == MenuNavKey::Activate && m_resultPage == "Skip")))
	{
		const std::string path = key == MenuNavKey::Back ? ".MainButtons.Back" : ".MainButtons.Skip";
		for (const auto &target : list)
		{
			if (target.id.find(path) != std::string::npos)
			{
				focus(target);
				press(target);
				return true;
			}
		}
	}

	// PROFILE is a movie-driven rollover menu, not a native combo box. Its
	// heading remains a separate toggle while its entries animate in. Transfer
	// focus into those entries; clicking the heading again closes the menu.
	if (m_controllerSections && m_section == 0 &&
		m_focusId.find(".ProfileNav.bttn") != std::string::npos &&
		m_focusId.find(".ProfileNav.") != std::string::npos &&
		(key == MenuNavKey::Activate || key == MenuNavKey::Up || key == MenuNavKey::Down))
	{
		const MenuNavTarget *entry = nullptr;
		for (const auto &target : list)
			if (target.id.find(".ProfileNav.") != std::string::npos &&
				(target.id.find(".ChangeProfile.") != std::string::npos || target.id.find(".NewProfile.") != std::string::npos || target.id.find(".DeleteProfile.") != std::string::npos))
				if (!entry || target.cy() > entry->cy()) entry = &target;
		if (entry) { focus(*entry); return true; }
	}

	MenuNavigationSource *source = m_windows.navigationSource();
	if (source && source->navKey(key, m_frameShown ? current : nullptr))
	{
		m_log.push_back(std::string("source ") + menuNavKeyName(key));
		return true;
	}
	if (key == MenuNavKey::Back)
	{
		if (!leave(catchers, list))
		{
			if (m_controllerSections)
			{
				std::vector<MenuNavTarget> all;
				collect(all, nullptr, false);
				back(all);
			}
			else back(list);
		}
		return true;
	}
	if (key == MenuNavKey::PageNext || key == MenuNavKey::PagePrevious)
	{
		// Active tabs have no button. The remaining buttons identify the current
		// page and whether this movie offers Chat as its third page.
		const char *names[] = { ".TabButtons.MapTab.", ".TabButtons.RulesTab.", ".TabButtons.ChatTab." };
		const MenuNavTarget *tabs[3] = {};
		int found = 0;
		for (const auto &target : list)
			if (target.kind == MenuNavTarget::Kind::Button)
				for (int i = 0; i < 3; ++i)
					if (target.id.find(names[i]) != std::string::npos && !tabs[i]) { tabs[i] = &target; ++found; }
		const int count = found == 2 || tabs[2] ? 3 : 2;
		for (int active = 0; active < count; ++active)
		{
			if (tabs[active]) continue;
			const int next = (active + (key == MenuNavKey::PageNext ? 1 : count - 1)) % count;
			if (!tabs[next]) continue;
			if (m_controllerSections) m_section = 2;
			focus(*tabs[next]);
			press(*tabs[next]);
			clearFocus();
			return true;
		}
		return false; // a screen without pages
	}
	if (list.empty())
	{
		return false; // nothing to focus (a movie playing, a loading screen): the keys stay the screen's own
	}
	bool phantom = false;
	if (!current && m_frameShown && !m_focusId.empty() && key == MenuNavKey::Activate)
	{
		m_log.push_back("press gone " + m_focusId); // the focused button went (its sub-menu opened over it when pointed at): nothing to press there
		return true;
	}
	if (!current && m_frameShown && !m_focusId.empty())
	{
		// the focused target went (a nav clip closed its buttons when the focus left them): the arrows and Tab go on from where it was
		MenuNavTarget gone = m_focusTarget;
		gone.window = nullptr;
		list.push_back(std::move(gone));
		current = &list.back();
		phantom = true;
	}
	if (!current || !m_frameShown)
	{
		// the first key shows the focus: where it was (the pointer took over since), else the screen's first target
		if (key == MenuNavKey::Activate && !current)
		{
			return false; // nothing focused: Enter / Space stay the screen's own (a movie's key)
		}
		const MenuNavTarget *first = current;
		if (!first && source && !source->navFirst().empty())
		{
			first = find(list, source->navFirst());
		}
		focus(first ? *first : list.front());
		return true;
	}
	const int index = (int)(current - list.data());
	if (!phantom && current->kind == MenuNavTarget::Kind::Gadget && current->window && gadgetKey(key, *current))
	{
		return true;
	}
	switch (key)
	{
		case MenuNavKey::Activate:
			press(*current);
			return true;
		case MenuNavKey::Next:
		case MenuNavKey::Previous:
		{
			// the first target after (before) the focused one's place in reading order, wrapping: a screen whose targets change under the focus (a nav
			// clip opening its buttons when pointed at) is walked on from where the focus was, never back and forth by index
			const MenuNavTarget &from = *current;
			const MenuNavTarget *pick = nullptr;
			if (key == MenuNavKey::Next)
			{
				for (const MenuNavTarget &t : list)
				{
					if (t.id != from.id && readingBefore(from, t))
					{
						pick = &t;
						break;
					}
				}
				if (!pick && leave(catchers, list))
				{
					return true; // past the last: out of the sub-menu first
				}
				pick = pick ? pick : &list.front();
			}
			else
			{
				for (auto it = list.rbegin(); it != list.rend(); ++it)
				{
					if (it->id != from.id && readingBefore(*it, from))
					{
						pick = &*it;
						break;
					}
				}
				if (!pick && leave(catchers, list))
				{
					return true;
				}
				pick = pick ? pick : &list[list.size() - (phantom ? 2 : 1)];
			}
			focus(*pick);
			return true;
		}
		default:
		{
			const int next = nearest(list, index, key);
			if (next >= 0)
			{
				focus(list[(std::size_t)next]);
			}
			else if (m_controllerSections || !leave(catchers, list))
			{
				m_frameShown = true; // the edge: the focus stays
			}
			return true;
		}
	}
}
