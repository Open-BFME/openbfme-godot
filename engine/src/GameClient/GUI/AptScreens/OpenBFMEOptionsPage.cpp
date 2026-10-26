// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptScreens/OpenBFMEOptionsPage.h (lane OPTS-3; not a retail screen).

#include "GameClient/GUI/AptScreens/OpenBFMEOptionsPage.h"

#include "GameClient/GUI/AptGadgetLayer.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameTextSource.h"

#include <algorithm>
#include <cstdlib>

namespace
{
// the retail skins the page's gadgets are made from (the APT gadget skins of Window.big, AptGadgetLayer::skinPath)
const char *const kComboSkin = "window/apt/combobox.wnd";
const char *const kCheckSkin = "window/apt/checkbox.wnd";
const char *const kSliderSkin = "window/apt/horzslider.wnd";
const char *const kListSkin = "window/apt/listbox.wnd";
const char *const kEntrySkin = "window/apt/textentry.wnd"; // lane OPTS-5: the skin of Options.apt's Port Number entry (OnlinePortNum)
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

void OpenBFMEOptionsPage::layout()
{
	// lane OPTS-5: a tab with a row in column 1 is laid out in two columns, each half of the panel; the rows of a column follow one another
	bool twoColumns = false;
	for (const OpenBFMEPageRow &row : m_spec.rows)
	{
		twoColumns = twoColumns || row.column == 1;
	}
	m_built.assign(m_spec.rows.size(), Built{});
	int next[2] = { 0, 0 };
	for (std::size_t i = 0; i < m_spec.rows.size(); ++i)
	{
		const int column = twoColumns && m_spec.rows[i].column == 1 ? 1 : 0;
		Built &b = m_built[i];
		b.y = kRowTop + next[column]++ * kRowPitch;
		b.narrow = twoColumns;
		b.left = !twoColumns || column == 0 ? kPanelLeft : kPanelMiddle;
		b.right = !twoColumns || column == 1 ? kPanelRight : kPanelMiddle;
	}
}

int OpenBFMEOptionsPage::labelLeft(std::size_t index) const
{
	const Built &b = m_built[index];
	return b.narrow ? b.left + kColumnLabelInset : kLabelLeft;
}

int OpenBFMEOptionsPage::gadgetLeft(std::size_t index) const
{
	const Built &b = m_built[index];
	return b.narrow ? b.left + kColumnGadgetInset : kGadgetLeft;
}

std::array<int, 4> OpenBFMEOptionsPage::gadgetRect(std::size_t index) const
{
	const OpenBFMEPageRow &row = m_spec.rows[index];
	const Built &b = m_built[index];
	const int y = b.y;
	const int x0 = gadgetLeft(index);
	const int x1 = b.narrow ? b.right - kColumnRightInset : kGadgetRight;
	switch (row.kind)
	{
		case OpenBFMEPageRow::Kind::Choice:
			return { x0, y, x1, y + kRowHeight };
		case OpenBFMEPageRow::Kind::Check:
			// lane OPTS-6: as wide as the box it draws (W3DGadgetCheckBoxImageDraw: a (height - 6) square from the left edge, 3 down), so a press right of
			// the box is no click; above and below the box the window keeps its 3 stage pixels (the skin's inset, kCheckBoxMargin)
			return { x0, y + (kRowHeight - kCheckSize) / 2, x0 + kCheckSize - 2 * kCheckBoxMargin, y + (kRowHeight - kCheckSize) / 2 + kCheckSize };
		case OpenBFMEPageRow::Kind::Slider:
			// a narrow column keeps 50 pixels right of the slider for its value
			return { x0, y, b.narrow ? std::min(x0 + kSliderWidth, x1 - 50) : x0 + kSliderWidth, y + kRowHeight };
		case OpenBFMEPageRow::Kind::Entry:
			// lane ONLINEMENU-1: an entry for more than a code or a number (the friends page's chat line) runs to the gadgets' right edge
			return { x0, y, row.maxValue > kShortEntryLength ? x1 : std::min(x0 + kEntryWidth, x1), y + kRowHeight };
		case OpenBFMEPageRow::Kind::Button:
			return { x0, y, std::min(x0 + kButtonWidth, x1), y + kRowHeight }; // lane CHEAT-1
		default:
			return { x0, y, x1, y + kRowHeight };
	}
}

bool OpenBFMEOptionsPage::labelBox(const std::string &key, std::array<int, 4> &out) const
{
	if (!m_shown)
	{
		return false;
	}
	for (std::size_t i = 0; i < m_spec.rows.size() && i < m_built.size(); ++i)
	{
		if (m_spec.rows[i].key == key)
		{
			// the label runs from its start to 8 pixels before the gadget
			out = { labelLeft(i), m_built[i].y, gadgetLeft(i) - 8, m_built[i].y + kRowHeight };
			return true;
		}
	}
	return false;
}

bool OpenBFMEOptionsPage::show(const OpenBFMEPageSpec &spec, std::string *error)
{
	destroyGadgets();
	m_spec = spec;
	m_shown = false;
	m_hoverRow = -1;
	layout();
	for (std::size_t i = 0; i < m_spec.rows.size(); ++i)
	{
		const OpenBFMEPageRow &row = m_spec.rows[i];
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
			case OpenBFMEPageRow::Kind::Entry:
				skin = kEntrySkin;
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
			// lane OPTS-6: combobox.wnd's drop-down button is 30 high, the row's box 26: it hung 4 stage pixels under the box, where a press found no
			// window. The button takes the box's height, so the combo box draws exactly the window that takes its clicks
			if (GameWindow *button = GadgetComboBoxGetDropDownButton(w))
			{
				int bw = 0, bh = 0;
				button->winGetSize(&bw, &bh);
				button->winSetSize(bw, r[3] - r[1]);
			}
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
		// lane ONLINEMENU-1: below the page's first rows when the spec says so (the friends page: rows, then its list)
		const int y0 = m_spec.listFromRow < 0 ? kRowTop : kRowTop + m_spec.listFromRow * kRowPitch;
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
		// lane ONLINEMENU-1: a list below rows is as tall as its lines and one more (at least three), not the rest of the panel in empty space
		const ListboxData *data = m_spec.listFromRow < 0 ? nullptr : static_cast<const ListboxData *>(m_list->winGetUserData());
		const int pitch = m_list->manager().winFontHeight(m_list->winGetFont()) + 1;
		if (data && pitch > 1)
		{
			const int height = std::max(data->totalHeight + pitch, 3 * pitch) + 2 * TOTAL_OUTLINE_HEIGHT;
			if (height < y1 - y0)
			{
				m_list->winSetSize(kGadgetRight - kLabelLeft, height);
			}
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
	m_hoverButton = -1;
	m_pressButton = -1;
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
		case OpenBFMEPageRow::Kind::Entry:
			if (EntryData *entry = static_cast<EntryData *>(w->winGetUserData()))
			{
				entry->maxTextLen = (short)row.maxValue; // RW 0x81606D(entry, 5) for the port
			}
			if (GadgetTextEntryGetText(w) != row.valueText)
			{
				GadgetTextEntrySetText(w, row.valueText);
			}
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
		const bool changed = itemsChanged || r.value != row.value || r.enabled != row.enabled || (r.kind == OpenBFMEPageRow::Kind::Entry && r.valueText != row.valueText);
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
		// the hot key list is read only; lane ONLINEMENU-1: a list that activates tells its row chosen and double-clicked (ZH GadgetListBox sends the
		// row in data2 with GLM_DOUBLE_CLICKED; the selection is read for GLM_SELECTED)
		if (m_spec.listActivates && !m_syncing && (msg == GLM_SELECTED || msg == GLM_DOUBLE_CLICKED))
		{
			int row = -1;
			GadgetListBoxGetSelected(m_list, &row);
			if (msg == GLM_DOUBLE_CLICKED && (int)data2 >= 0 && (int)data2 < (int)m_spec.list.size())
			{
				row = (int)data2;
			}
			if (row >= 0 && row < (int)m_spec.list.size())
			{
				emit({ msg == GLM_SELECTED ? OpenBFMEPageEvent::Kind::ListSelect : OpenBFMEPageEvent::Kind::ListActivate, std::string("list"), row });
			}
		}
		return true;
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
		else if (msg == GSM_SLIDER_CLICKED && row.kind == OpenBFMEPageRow::Kind::Slider)
		{
			emit({ OpenBFMEPageEvent::Kind::Release, row.key, row.value }); // lane OPTS-5: the owner's 0x4010 (the volume sample stops, RW 0x91F90D)
		}
		else if ((msg == GEM_UPDATE_TEXT || msg == GEM_EDIT_DONE) && row.kind == OpenBFMEPageRow::Kind::Entry)
		{
			// lane OPTS-5: the entry's text; the value is its number as _wtoi reads it (RW 0x9202F2), 0 when empty
			const UnicodeString text = GadgetTextEntryGetText(from);
			if (text != row.valueText)
			{
				row.valueText = text;
				int number = 0;
				for (char16_t c : text)
				{
					if (c < u'0' || c > u'9' || number > 1000000)
					{
						break;
					}
					number = number * 10 + (c - u'0');
				}
				row.value = number;
				OpenBFMEPageEvent e{ OpenBFMEPageEvent::Kind::Value, row.key, number };
				e.text = text;
				emit(e);
			}
			if (msg == GEM_EDIT_DONE)
			{
				OpenBFMEPageEvent e{ OpenBFMEPageEvent::Kind::Submit, row.key, row.value };
				e.text = text;
				emit(e); // lane ONLINEMENU-1: Enter sends the text (the friends page's "add friend by code")
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
	for (std::size_t i = 0; i < m_spec.rows.size() && i < m_built.size(); ++i)
	{
		const Built &b = m_built[i];
		const int top = b.y - (kRowPitch - kRowHeight) / 2;
		if (x >= b.left && x < b.right && y >= top && y < top + kRowPitch)
		{
			return (int)i;
		}
	}
	return -1;
}

int OpenBFMEOptionsPage::tabAt(float x, float y) const
{
	if (x < kTabsLeft + 12 || x >= kTabsRight - 12) // lane OPTS-6: the drawn bar (decorate: the selection image kTabsLeft + 12 .. kTabsRight - 12); OPTS-3 took 4 more each side
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

int OpenBFMEOptionsPage::buttonAt(float x, float y) const
{
	for (std::size_t i = 0; i < m_spec.rows.size() && i < m_built.size(); ++i)
	{
		if (m_spec.rows[i].kind != OpenBFMEPageRow::Kind::Button || !m_spec.rows[i].enabled)
		{
			continue;
		}
		const std::array<int, 4> r = gadgetRect(i);
		if (x >= r[0] && x < r[2] && y >= r[1] && y < r[3])
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
	const int button = buttonAt(x, y); // lane CHEAT-1
	if (kind == 0)
	{
		m_hoverRow = rowAt(x, y);
		m_hoverTab = tab;
		m_hoverButton = button;
		return false;
	}
	if (kind == 1)
	{
		m_pressTab = tab;
		m_pressButton = button;
		return tab >= 0 || button >= 0;
	}
	// lane CHEAT-1: a Button row is clicked when the press and the release were on it
	if (m_pressButton >= 0)
	{
		const int pressedButton = m_pressButton;
		m_pressButton = -1;
		if (pressedButton == button && button < (int)m_spec.rows.size())
		{
			emit({ OpenBFMEPageEvent::Kind::Press, m_spec.rows[(std::size_t)button].key, 0 });
		}
		return true;
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
	if (m_spec.backdrop)
	{
		// lane ONLINEMENU-1: over an online page the movie's own panels are under the page's band: a dark fill (the menus' background tone) first
		GadgetDrawCommand c;
		c.kind = GadgetDrawCommand::Kind::FillRect;
		c.window = "OpenBFMEPage";
		c.x0 = kTabsLeft - 8;
		c.y0 = kTop - 8;
		c.x1 = kPanelRight + 8;
		c.y1 = kBottom + 8;
		c.color = 0xFF050607u; // opaque: at 0xF0 the online page's text showed through (the friends video)
		out.commands.push_back(c);
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
	// the rows: label left, gadget right (sections span their column)
	for (std::size_t i = 0; i < m_spec.rows.size() && i < m_built.size(); ++i)
	{
		const OpenBFMEPageRow &row = m_spec.rows[i];
		const Built &b = m_built[i];
		const int y = b.y;
		if (row.kind == OpenBFMEPageRow::Kind::Section)
		{
			text(out, row.label, b.left, y + 4, kHeaderSize, kHeaderColor, b.right - b.left, true);
			continue;
		}
		const Color label = row.enabled ? kLabelColor : kDisabledLabelColor;
		text(out, row.label, labelLeft(i), y + 3, kLabelSize, label);
		const std::array<int, 4> r = gadgetRect(i);
		if ((row.kind == OpenBFMEPageRow::Kind::Choice || row.kind == OpenBFMEPageRow::Kind::Slider || row.kind == OpenBFMEPageRow::Kind::Entry) && b.window)
		{
			// the box under the gadget, as Options.apt's placeholders draw it (the Detail combo, the volume sliders: a dark well, a 1 pixel outline)
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
		if (row.kind == OpenBFMEPageRow::Kind::Button)
		{
			// lane CHEAT-1: the FRIENDS button's look (AptOnlineShell::decorateAll): a dark box, the page's frame, the bottom bar's lettering, brighter when pointed
			GadgetDrawCommand box;
			box.kind = GadgetDrawCommand::Kind::FillRect;
			box.window = "OpenBFMEPage";
			box.x0 = r[0];
			box.y0 = r[1];
			box.x1 = r[2];
			box.y1 = r[3];
			box.color = (int)i == m_pressButton ? 0xF0182028u : 0xE0050607u;
			out.commands.push_back(box);
			box.kind = GadgetDrawCommand::Kind::OpenRect;
			box.color = kFrameColor;
			out.commands.push_back(std::move(box));
			const Color c = !row.enabled ? kDisabledLabelColor : (int)i == m_hoverButton ? kTabHotColor : kTabColor;
			text(out, row.valueText, r[0], y + 4, kLabelSize, c, r[2] - r[0], true);
		}
		if (row.kind == OpenBFMEPageRow::Kind::Slider)
		{
			text(out, row.valueText, r[2] + 14, y + 3, kLabelSize, row.enabled ? kHelpColor : kDisabledLabelColor);
		}
		else if (row.kind == OpenBFMEPageRow::Kind::Text)
		{
			text(out, row.valueText, gadgetLeft(i), y + 3, kLabelSize, row.enabled ? kHelpColor : kDisabledLabelColor);
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
		if (m_spec.rows[i].key == name && (m_built[i].window || m_spec.rows[i].kind == OpenBFMEPageRow::Kind::Button))
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
			case OpenBFMEPageRow::Kind::Entry:
				// typed text: the entry's text, then its update message to the owner (GadgetTextEntry's GEM_UPDATE_TEXT)
				GadgetTextEntrySetText(w, value > 0 ? asciiToU16(std::to_string(value)) : UnicodeString());
				w->manager().winSendSystemMsg(w->winGetOwner(), GEM_UPDATE_TEXT, reinterpret_cast<WindowMsgData>(w), 0);
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
