// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/OpenBFMEOptionsPage.h (lane OPTS-3; not a retail screen).

#include "GameClient/GUI/AptScreens/OpenBFMEOptionsPage.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"

#include <algorithm>
#include <cstdlib>

namespace
{
// the retail skins the page's gadgets are made from (the APT gadget skins of Window.big, AptGadgetLayer::skinPath)
const char *const kComboSkin = "window/apt/combobox.wnd";
const char *const kCheckSkin = "window/apt/checkbox.wnd";
const char *const kSliderSkin = "window/apt/horzslider.wnd";
const char *const kListSkin = "window/apt/listbox.wnd";
const char *const kFont = "Albertus MT"; // every text of Options.apt's pages (describe_ops: Albertus MT 14 / 16 / 18)
constexpr int kHeaderSize = 18;
constexpr int kLabelSize = 16;
constexpr int kHelpSize = 14;
constexpr int kTabSize = 18;
// a combo's drop-down shows at most this many entries before it scrolls (the lobby's combos: 8 to 10)
constexpr int kComboMaxDisplay = 8;
// the hot key list's two columns (percent of the list's width)
const int kListColumns[2] = { 68, 32 };
} // namespace

OpenBFMEOptionsPage::OpenBFMEOptionsPage(AptGadgetLayer &layer, GameWindow *owner) : m_layer(layer), m_owner(owner)
{
}

OpenBFMEOptionsPage::~OpenBFMEOptionsPage()
{
	destroyGadgets();
}

void OpenBFMEOptionsPage::destroyGadgets()
{
	for (Built &b : m_built)
	{
		if (b.window)
		{
			m_layer.destroyEngineGadget(b.window);
		}
	}
	m_built.clear();
	if (m_list)
	{
		m_layer.destroyEngineGadget(m_list);
		m_list = nullptr;
	}
}

std::array<int, 4> OpenBFMEOptionsPage::gadgetRect(std::size_t index) const
{
	const OpenBFMEPageRow &row = m_spec.rows[index];
	const int y = kRowTop + (int)index * kRowPitch;
	switch (row.kind)
	{
		case OpenBFMEPageRow::Kind::Choice:
			return { kGadgetLeft, y, kGadgetRight, y + kRowHeight };
		case OpenBFMEPageRow::Kind::Check:
			return { kGadgetLeft, y + (kRowHeight - kCheckSize) / 2, kGadgetLeft + kCheckSize, y + (kRowHeight - kCheckSize) / 2 + kCheckSize };
		case OpenBFMEPageRow::Kind::Slider:
			return { kGadgetLeft, y, kGadgetLeft + kSliderWidth, y + kRowHeight };
		default:
			return { kGadgetLeft, y, kGadgetRight, y + kRowHeight };
	}
}

bool OpenBFMEOptionsPage::show(const OpenBFMEPageSpec &spec, std::string *error)
{
	destroyGadgets();
	m_spec = spec;
	m_shown = false;
	m_hoverRow = -1;
	m_built.assign(m_spec.rows.size(), Built{});
	for (std::size_t i = 0; i < m_spec.rows.size(); ++i)
	{
		const OpenBFMEPageRow &row = m_spec.rows[i];
		m_built[i].y = kRowTop + (int)i * kRowPitch;
		const char *skin = nullptr;
		switch (row.kind)
		{
			case OpenBFMEPageRow::Kind::Choice:
				skin = kComboSkin;
				break;
			case OpenBFMEPageRow::Kind::Check:
				skin = kCheckSkin;
				break;
			case OpenBFMEPageRow::Kind::Slider:
				skin = kSliderSkin;
				break;
			default:
				break;
		}
		if (!skin)
		{
			continue;
		}
		const std::array<int, 4> r = gadgetRect(i);
		std::string err;
		GameWindow *w = m_layer.createEngineGadget(skin, r[0], r[1], r[2] - r[0], r[3] - r[1], m_owner, &err);
		if (!w)
		{
			destroyGadgets();
			if (error)
			{
				*error = "the OpenBFME page's row '" + row.key + "': " + err;
			}
			return false;
		}
		m_built[i].window = w;
		if (row.kind == OpenBFMEPageRow::Kind::Choice)
		{
			GadgetComboBoxSetMaxDisplay(w, kComboMaxDisplay);
		}
		else if (row.kind == OpenBFMEPageRow::Kind::Slider)
		{
			// ZH GSM_SET_MIN_MAX takes the tick size from the window's width: sent after the size is set
			w->manager().winSendSystemMsg(w, GSM_SET_MIN_MAX, (WindowMsgData)row.minValue, (WindowMsgData)row.maxValue);
		}
		applyRow(i);
	}
	if (m_spec.hasList)
	{
		std::string err;
		const int y0 = kRowTop;
		const int y1 = kHelpTop - 10;
		m_list = m_layer.createEngineGadget(kListSkin, kLabelLeft, y0, kGadgetRight - kLabelLeft, y1 - y0, m_owner, &err);
		if (!m_list)
		{
			destroyGadgets();
			if (error)
			{
				*error = "the OpenBFME page's list: " + err;
			}
			return false;
		}
		GadgetListBoxSetColumnWidths(m_list, 2, kListColumns);
		GadgetListBoxSetListLength(m_list, std::max<int>(1, (int)m_spec.list.size()));
		for (std::size_t i = 0; i < m_spec.list.size(); ++i)
		{
			GadgetListBoxAddEntryText(m_list, m_spec.list[i].first, kLabelColor, (int)i, 0);
			GadgetListBoxAddEntryText(m_list, m_spec.list[i].second, kHelpColor, (int)i, 1);
		}
		GadgetListBoxSetTopVisibleEntry(m_list, 0);
	}
	m_shown = true;
	return true;
}

void OpenBFMEOptionsPage::hide()
{
	destroyGadgets();
	m_shown = false;
	m_hoverRow = -1;
	m_hoverTab = -1;
	m_pressTab = -1;
}

void OpenBFMEOptionsPage::applyRow(std::size_t index)
{
	if (index >= m_built.size())
	{
		return; // hidden: no gadgets
	}
	const OpenBFMEPageRow &row = m_spec.rows[index];
	GameWindow *w = m_built[index].window;
	if (!w)
	{
		return;
	}
	m_syncing = true;
	switch (row.kind)
	{
		case OpenBFMEPageRow::Kind::Choice:
		{
			GadgetComboBoxReset(w);
			for (const OpenBFMEPageItem &item : row.items)
			{
				// a choice the machine cannot take is listed in the greyed label colour (the movie's greyed text)
				GadgetComboBoxAddEntry(w, item.text, item.enabled ? w->winGetEnabledTextColor() : w->winGetDisabledTextColor());
			}
			if (row.value >= 0 && row.value < (int)row.items.size())
			{
				GadgetComboBoxSetSelectedPos(w, row.value);
			}
			break;
		}
		case OpenBFMEPageRow::Kind::Check:
			GadgetCheckBoxSetChecked(w, row.value != 0);
			break;
		case OpenBFMEPageRow::Kind::Slider:
			GadgetSliderSetPosition(w, std::clamp(row.value, row.minValue, row.maxValue));
			break;
		default:
			break;
	}
	w->winEnable(row.enabled);
	m_syncing = false;
}

bool OpenBFMEOptionsPage::setRow(const OpenBFMEPageRow &row)
{
	for (std::size_t i = 0; i < m_spec.rows.size(); ++i)
	{
		OpenBFMEPageRow &r = m_spec.rows[i];
		if (r.key != row.key || r.kind != row.kind)
		{
			continue;
		}
		const bool itemsChanged = r.kind == OpenBFMEPageRow::Kind::Choice &&
			(r.items.size() != row.items.size() || !std::equal(r.items.begin(), r.items.end(), row.items.begin(), [](const OpenBFMEPageItem &a, const OpenBFMEPageItem &b) {
				return a.text == b.text && a.enabled == b.enabled;
			}));
		const bool changed = itemsChanged || r.value != row.value || r.enabled != row.enabled;
		const int minValue = r.minValue, maxValue = r.maxValue;
		r = row;
		r.minValue = minValue; // the slider's range is the one it was built with
		r.maxValue = maxValue;
		if (changed)
		{
			applyRow(i);
		}
		return true;
	}
	return false;
}

bool OpenBFMEOptionsPage::message(std::uint32_t msg, WindowMsgData data1, WindowMsgData data2)
{
	GameWindow *from = reinterpret_cast<GameWindow *>(data1);
	if (!from)
	{
		return false;
	}
	if (m_list && from == m_list)
	{
		return true; // the hot key list is read only
	}
	for (std::size_t i = 0; i < m_built.size(); ++i)
	{
		if (m_built[i].window != from)
		{
			continue;
		}
		OpenBFMEPageRow &row = m_spec.rows[i];
		if (m_syncing)
		{
			return true;
		}
		if (msg == GCM_SELECTED && row.kind == OpenBFMEPageRow::Kind::Choice)
		{
			int index = -1;
			GadgetComboBoxGetSelectedPos(from, &index);
			if (index < 0 || index >= (int)row.items.size() || index == row.value)
			{
				return true;
			}
			if (!row.items[index].enabled)
			{
				applyRow(i); // refused: the shown choice goes back
				return true;
			}
			row.value = index;
			emit({ OpenBFMEPageEvent::Kind::Value, row.key, index });
		}
		else if (msg == GBM_SELECTED && row.kind == OpenBFMEPageRow::Kind::Check)
		{
			const int on = GadgetCheckBoxIsChecked(from) ? 1 : 0;
			if (on != row.value)
			{
				row.value = on;
				emit({ OpenBFMEPageEvent::Kind::Value, row.key, on });
			}
		}
		else if ((msg == GSM_SLIDER_TRACK || msg == GSM_SLIDER_DONE) && row.kind == OpenBFMEPageRow::Kind::Slider)
		{
			const int pos = msg == GSM_SLIDER_TRACK ? (int)data2 : GadgetSliderGetPosition(from);
			if (pos != row.value)
			{
				row.value = pos;
				emit({ OpenBFMEPageEvent::Kind::Value, row.key, pos });
			}
		}
		return true;
	}
	return false;
}

int OpenBFMEOptionsPage::rowAt(float x, float y) const
{
	if (x < kPanelLeft || x >= kPanelRight)
	{
		return -1;
	}
	for (std::size_t i = 0; i < m_spec.rows.size(); ++i)
	{
		const int top = kRowTop + (int)i * kRowPitch - (kRowPitch - kRowHeight) / 2;
		if (y >= top && y < top + kRowPitch)
		{
			return (int)i;
		}
	}
	return -1;
}

int OpenBFMEOptionsPage::tabAt(float x, float y) const
{
	if (x < kTabsLeft + 8 || x >= kTabsRight - 8)
	{
		return -1;
	}
	for (std::size_t i = 0; i < m_spec.tabs.size(); ++i)
	{
		const int top = kTabTop + (int)i * kTabPitch;
		if (y >= top && y < top + kTabPitch - 6)
		{
			return (int)i;
		}
	}
	return -1;
}

bool OpenBFMEOptionsPage::mouse(int kind, float x, float y)
{
	if (!m_shown)
	{
		return false;
	}
	const int tab = tabAt(x, y);
	if (kind == 0)
	{
		m_hoverRow = rowAt(x, y);
		m_hoverTab = tab;
		return false;
	}
	if (kind == 1)
	{
		m_pressTab = tab;
		return tab >= 0;
	}
	// the release: a tab is chosen when the press and the release were on it (a button's click)
	const int pressed = m_pressTab;
	m_pressTab = -1;
	if (pressed < 0)
	{
		return false;
	}
	if (pressed == tab && tab != m_spec.tab)
	{
		emit({ OpenBFMEPageEvent::Kind::Tab, std::string(), tab });
	}
	return true;
}

void OpenBFMEOptionsPage::emit(const OpenBFMEPageEvent &e)
{
	// one queue in the order things happened (the owner's, when it gave a sink: the bottom bar's events go there too)
	if (m_sink)
	{
		m_sink(e);
	}
	else
	{
		m_events.push_back(e);
	}
}

std::vector<OpenBFMEPageEvent> OpenBFMEOptionsPage::takeEvents()
{
	std::vector<OpenBFMEPageEvent> out;
	out.swap(m_events);
	return out;
}

UnicodeString OpenBFMEOptionsPage::helpText() const
{
	if (m_hoverRow >= 0 && m_hoverRow < (int)m_spec.rows.size() && !m_spec.rows[m_hoverRow].help.empty())
	{
		return m_spec.rows[m_hoverRow].help;
	}
	return m_spec.note;
}

void OpenBFMEOptionsPage::text(GadgetDrawList &out, const UnicodeString &s, int x, int y, int size, Color color, int wrapWidth, bool centred)
{
	if (s.empty())
	{
		return;
	}
	GadgetDrawCommand c;
	c.kind = GadgetDrawCommand::Kind::Text;
	c.window = "OpenBFMEPage";
	c.x0 = x;
	c.y0 = y;
	c.text = s;
	c.color = color;
	c.font.name = kFont;
	c.font.pointSize = size;
	c.wrapWidth = wrapWidth;
	c.wrapCentered = centred;
	out.commands.push_back(std::move(c));
}

void OpenBFMEOptionsPage::frame(GadgetDrawList &out, int x0, int y0, int x1, int y1, const UnicodeString &header)
{
	// Options.apt's settings frame: a rectangle inset 6 pixels left and right, one inset 6 pixels top and bottom crossing it, and a rule under the
	// header across the wider one (the movie's FrameMiddle at 2560 x 1440, lane OPTS-3)
	auto rect = [&out](int a, int b, int c, int d) {
		GadgetDrawCommand cmd;
		cmd.kind = GadgetDrawCommand::Kind::OpenRect;
		cmd.window = "OpenBFMEPage";
		cmd.x0 = a;
		cmd.y0 = b;
		cmd.x1 = c;
		cmd.y1 = d;
		cmd.color = kFrameColor;
		out.commands.push_back(std::move(cmd));
	};
	rect(x0 + 6, y0, x1 - 6, y1);
	rect(x0, y0 + 6, x1, y1 - 6);
	GadgetDrawCommand rule;
	rule.kind = GadgetDrawCommand::Kind::Line;
	rule.window = "OpenBFMEPage";
	rule.x0 = x0;
	rule.x1 = x1;
	rule.y0 = rule.y1 = y0 + kHeaderHeight + 4;
	rule.color = kFrameColor;
	out.commands.push_back(std::move(rule));
	text(out, header, x0, y0 + 8, kHeaderSize, kHeaderColor, x1 - x0, true);
}

void OpenBFMEOptionsPage::decorate(GadgetDrawList &out) const
{
	if (!m_shown)
	{
		return;
	}
	frame(out, kTabsLeft, kTop, kTabsRight, kBottom, m_spec.tabsHeader);
	frame(out, kPanelLeft, kTop, kPanelRight, kBottom, m_spec.panelHeader);
	// the tabs: the chosen one on the list box's selection image (listbox.wnd's highlight, AptListBoxHilite*), the pointed one brighter
	for (std::size_t i = 0; i < m_spec.tabs.size(); ++i)
	{
		const int top = kTabTop + (int)i * kTabPitch;
		const bool chosen = (int)i == m_spec.tab;
		if (chosen)
		{
			GadgetDrawCommand hi;
			hi.kind = GadgetDrawCommand::Kind::Image;
			hi.window = "OpenBFMEPage";
			hi.image = "AptListBoxHiliteSelectedItem"; // listbox.wnd's selected row
			hi.x0 = kTabsLeft + 12;
			hi.x1 = kTabsRight - 12;
			hi.y0 = top;
			hi.y1 = top + kTabPitch - 6;
			hi.color = 0xFFFFFFFFu;
			out.commands.push_back(std::move(hi));
		}
		const Color c = chosen || (int)i == m_hoverTab ? kTabHotColor : kLabelColor;
		text(out, m_spec.tabs[i], kTabsLeft, top + 6, kTabSize, c, kTabsRight - kTabsLeft, true);
	}
	// the rows: label left, gadget right (sections span the panel)
	for (std::size_t i = 0; i < m_spec.rows.size(); ++i)
	{
		const OpenBFMEPageRow &row = m_spec.rows[i];
		const int y = kRowTop + (int)i * kRowPitch;
		if (row.kind == OpenBFMEPageRow::Kind::Section)
		{
			text(out, row.label, kPanelLeft, y + 4, kHeaderSize, kHeaderColor, kPanelRight - kPanelLeft, true);
			continue;
		}
		const Color label = row.enabled ? kLabelColor : kDisabledLabelColor;
		text(out, row.label, kLabelLeft, y + 3, kLabelSize, label);
		if ((row.kind == OpenBFMEPageRow::Kind::Choice || row.kind == OpenBFMEPageRow::Kind::Slider) && i < m_built.size() && m_built[i].window)
		{
			// the box under the gadget, as Options.apt's placeholders draw it (the Detail combo, the volume sliders: a dark well, a 1 pixel outline)
			const std::array<int, 4> r = gadgetRect(i);
			GadgetDrawCommand well;
			well.kind = GadgetDrawCommand::Kind::FillRect;
			well.window = "OpenBFMEPage";
			well.x0 = r[0];
			well.y0 = r[1];
			well.x1 = r[2];
			well.y1 = r[3];
			well.color = kWellColor;
			out.commands.push_back(well);
			well.kind = GadgetDrawCommand::Kind::OpenRect;
			well.color = kFrameColor;
			out.commands.push_back(std::move(well));
		}
		if (row.kind == OpenBFMEPageRow::Kind::Slider)
		{
			text(out, row.valueText, kGadgetLeft + kSliderWidth + 14, y + 3, kLabelSize, row.enabled ? kHelpColor : kDisabledLabelColor);
		}
		else if (row.kind == OpenBFMEPageRow::Kind::Text)
		{
			text(out, row.valueText, kGadgetLeft, y + 3, kLabelSize, row.enabled ? kHelpColor : kDisabledLabelColor);
		}
	}
	// the help line: the pointed row's, else the tab's note
	GadgetDrawCommand rule;
	rule.kind = GadgetDrawCommand::Kind::Line;
	rule.window = "OpenBFMEPage";
	rule.x0 = kPanelLeft + 16;
	rule.x1 = kPanelRight - 16;
	rule.y0 = rule.y1 = kHelpTop - 4;
	rule.color = kFrameColor;
	out.commands.push_back(std::move(rule));
	text(out, helpText(), kLabelLeft, kHelpTop + 2, kHelpSize, kHelpColor, kGadgetRight - kLabelLeft, true);
}

bool OpenBFMEOptionsPage::rect(const std::string &name, std::array<int, 4> &out) const
{
	if (!m_shown)
	{
		return false;
	}
	if (name.rfind("tab:", 0) == 0)
	{
		const int i = std::atoi(name.c_str() + 4);
		if (i < 0 || i >= (int)m_spec.tabs.size())
		{
			return false;
		}
		const int top = kTabTop + i * kTabPitch;
		out = { kTabsLeft + 12, top, kTabsRight - 12, top + kTabPitch - 6 };
		return true;
	}
	if (name == "list" && m_list)
	{
		int x, y, w, h;
		m_list->winGetPosition(&x, &y);
		m_list->winGetSize(&w, &h);
		out = { x, y, x + w, y + h };
		return true;
	}
	for (std::size_t i = 0; i < m_built.size(); ++i)
	{
		if (m_spec.rows[i].key == name && m_built[i].window)
		{
			out = gadgetRect(i);
			return true;
		}
	}
	return false;
}

GameWindow *OpenBFMEOptionsPage::gadget(const std::string &key) const
{
	if (key == "list")
	{
		return m_list;
	}
	for (std::size_t i = 0; i < m_built.size(); ++i)
	{
		if (m_spec.rows[i].key == key)
		{
			return m_built[i].window;
		}
	}
	return nullptr;
}

bool OpenBFMEOptionsPage::choose(const std::string &key, int value, std::string *error)
{
	for (std::size_t i = 0; i < m_built.size(); ++i)
	{
		const OpenBFMEPageRow &row = m_spec.rows[i];
		GameWindow *w = m_built[i].window;
		if (row.key != key || !w)
		{
			continue;
		}
		if (!w->winGetEnabled())
		{
			if (error)
			{
				*error = "the row '" + key + "' is disabled";
			}
			return false;
		}
		switch (row.kind)
		{
			case OpenBFMEPageRow::Kind::Choice:
				if (value < 0 || value >= (int)row.items.size())
				{
					if (error)
					{
						*error = "the row '" + key + "' has no choice " + std::to_string(value);
					}
					return false;
				}
				// the list box's selection sets the combo's and tells the owner (GadgetComboBox GLM_SELECTED: GCM_SET_SELECTION, then GCM_SELECTED)
				m_syncing = true;
				GadgetComboBoxSetSelectedPos(w, value);
				m_syncing = false;
				w->manager().winSendSystemMsg(w->winGetOwner(), GCM_SELECTED, reinterpret_cast<WindowMsgData>(w), 0);
				return true;
			case OpenBFMEPageRow::Kind::Check:
				if ((value != 0) != GadgetCheckBoxIsChecked(w))
				{
					GadgetCheckBoxToggle(w); // the click's toggle: GBM_SELECTED to the owner
				}
				return true;
			case OpenBFMEPageRow::Kind::Slider:
				m_syncing = true;
				GadgetSliderSetPosition(w, std::clamp(value, row.minValue, row.maxValue));
				m_syncing = false;
				w->manager().winSendSystemMsg(w->winGetOwner(), GSM_SLIDER_TRACK, reinterpret_cast<WindowMsgData>(w), (WindowMsgData)GadgetSliderGetPosition(w));
				return true;
			default:
				break;
		}
	}
	if (error)
	{
		*error = "no row '" + key + "' with a gadget";
	}
	return false;
}
