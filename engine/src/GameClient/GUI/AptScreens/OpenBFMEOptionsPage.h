// OpenBFME. GPL-3.0.
//
// Lane OPTS-3: the OpenBFME page of the Options screen. NOT A RETAIL SCREEN: RotWK has no such page; it holds this rebuild's own settings (the
// window, the renderer, the camera, the interface extras ...). The owner asked for it "in RotWK's own style" (2026-10-10, preview.4 at 2560 x 1440: the
// former Godot Control screen looked low-resolution next to the menus), so it is made of the retail menu's own pieces only:
//
//   * it is a page of Options.apt (AptOptionsScreen hosts it, GUI/AptScreens/AptSimpleScreens.h): the movie's background, OPTIONS title, subtitle and
//     bottom bar (CANCEL / RESET TO DEFAULTS / ACCEPT) stay, the three settings frames of the basic page are hidden while the page is up;
//   * its controls are RotWK's APT gadgets created from the retail skins, window\apt\combobox.wnd, checkbox.wnd, horzslider.wnd and listbox.wnd (the
//     skins Options.apt's own Detail combo, Show All Health Bars box and volume sliders are made from), through the gadget layer
//     (AptGadgetLayer::createEngineGadget);
//   * its frames and texts are gadget draw commands (GadgetDrawList.h) in Options.apt's own measures: the frame of a settings panel as the movie's
//     FrameLeft / FrameMiddle / FrameRight shapes draw it (two crossed thin rectangles and a rule under the header, colour 0.45 0.50 0.56), headers in
//     Albertus MT 18 white, row labels in Albertus MT 16 in the labels' pale blue (0.75 0.93 1.00), the help line in Albertus MT 14 white (the movie's
//     PortNumHelp text). The values are the draw ops of Options.apt read at run time (AptMenuPlayer.describe_ops, lane OPTS-3): INFERENCE that an
//     added page should look like the movie's pages, not a retail fact.
//
// Everything is drawn by the same path as the movies' gadgets (GodotDevice/GodotAptPlayer.cpp drawGadgets: stage coordinates mapped to the window,
// fonts rasterised at the window's pixel size), so the page is as sharp as the rest of the menus at any window size.
//
// The page holds no settings: the device (godot/scripts/openbfme_options_screen.gd) describes the rows of a tab (OpenBFMEPageSpec), takes the events
// (a value changed, a tab chosen, the bottom bar's buttons) and applies them.

#pragma once

#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/GameWindow.h"

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class AptGadgetLayer;

struct OpenBFMEPageItem
{
	UnicodeString text;
	bool enabled = true; // a choice the machine cannot take (an upscaler the renderer lacks): listed greyed, refused when chosen
};

struct OpenBFMEPageRow
{
	enum class Kind : std::uint8_t
	{
		Section, // a heading inside the panel (label only)
		Choice,  // combobox.wnd: `items`, `value` = the chosen index
		Check,   // checkbox.wnd: `value` 0 / 1
		Slider,  // horzslider.wnd: `minValue` .. `maxValue`, `value` = the position; `valueText` is drawn right of it
		Text     // a read-only value: `valueText`
	};
	Kind kind = Kind::Text;
	std::string key;
	UnicodeString label;
	UnicodeString help;
	std::vector<OpenBFMEPageItem> items;
	int value = 0;
	int minValue = 0;
	int maxValue = 100;
	UnicodeString valueText;
	bool enabled = true;
};

struct OpenBFMEPageSpec
{
	UnicodeString subtitle;   // Options.apt's subtitle while the page is up ("SETTINGS" on the basic page)
	UnicodeString tabsHeader; // the left frame's header
	std::vector<UnicodeString> tabs;
	int tab = 0;
	UnicodeString panelHeader; // the right frame's header (the tab's name)
	std::vector<OpenBFMEPageRow> rows;
	UnicodeString note; // the help line while no row is pointed at
	// a two-column list (listbox.wnd) in place of rows: the hot keys
	std::vector<std::pair<UnicodeString, UnicodeString>> list;
	bool hasList = false;
};

struct OpenBFMEPageEvent
{
	enum class Kind : std::uint8_t
	{
		Value,  // `key` changed to `value` (Choice: index, Check: 0 / 1, Slider: position)
		Tab,    // the tab `value` was chosen
		Cancel, // Options.apt's CANCEL while the page is up
		Reset,  // RESET TO DEFAULTS
		Accept, // ACCEPT
		Entry   // the OPENBFME button of the basic page
	};
	Kind kind = Kind::Value;
	std::string key;
	int value = 0;
};

class OpenBFMEOptionsPage
{
public:
	// The page's place on Options.apt's 1024 x 768 stage: the band the basic page's three frames fill (the movie's FrameLeft .. FrameRight, stage
	// x 97 .. 927, y 182 .. 610), split into the tab frame on the left and the settings frame on the right.
	static constexpr int kTop = 182;
	static constexpr int kBottom = 610;
	static constexpr int kTabsLeft = 97;
	static constexpr int kTabsRight = 287;
	static constexpr int kPanelLeft = 305;
	static constexpr int kPanelRight = 927;
	static constexpr int kHeaderHeight = 30; // the frame's header band (the movie's: 27 .. 30 stage pixels)
	static constexpr int kRowTop = kTop + kHeaderHeight + 8;
	static constexpr int kRowPitch = 34;
	static constexpr int kRowHeight = 26;
	static constexpr int kLabelLeft = kPanelLeft + 22;
	static constexpr int kGadgetLeft = kPanelLeft + 300;
	static constexpr int kGadgetRight = kPanelRight - 22;
	static constexpr int kSliderWidth = 220;
	static constexpr int kCheckSize = 30; // checkbox.wnd draws its box (height - 6) square: 24, the size of Options.apt's Show All Health Bars box
	static constexpr int kHelpTop = kBottom - 70;
	static constexpr int kTabTop = kTop + kHeaderHeight + 12;
	static constexpr int kTabPitch = 38;

	// Colours (ARGB) and fonts of Options.apt's draw ops (see the header comment)
	static constexpr Color kFrameColor = 0xFF738090u;
	static constexpr Color kWellColor = 0xC0030403u; // the dark inside of the box Options.apt draws under a combo box or a slider (its placeholder's shape)
	static constexpr Color kHeaderColor = 0xFFFFFFFFu;
	static constexpr Color kLabelColor = 0xFFBFEDFFu;
	static constexpr Color kHelpColor = 0xFFFFFFFFu;
	static constexpr Color kDisabledLabelColor = 0x59BFEDFFu; // the movie's greyed text: the colour at alpha 0.35 (REFRESH NAT, OPTS-2's greyed rows)
	static constexpr Color kTabColor = 0xFF80ABBAu;           // the bottom bar's button text (0.50 0.67 0.73)
	static constexpr Color kTabHotColor = 0xFFFFFFFFu;

	OpenBFMEOptionsPage(AptGadgetLayer &layer, GameWindow *owner);
	~OpenBFMEOptionsPage();
	OpenBFMEOptionsPage(const OpenBFMEOptionsPage &) = delete;
	OpenBFMEOptionsPage &operator=(const OpenBFMEOptionsPage &) = delete;

	// (Re)builds the page from `spec`: the gadgets of the previous spec go. False and `*error` when a skin cannot be made (nothing is shown then).
	bool show(const OpenBFMEPageSpec &spec, std::string *error);
	void hide();
	// the gadget layer went first (the screen's teardown after the layer's): the page forgets its gadgets without destroying them
	void abandon()
	{
		m_built.clear();
		m_list = nullptr;
		m_shown = false;
	}
	bool shown() const { return m_shown; }
	const OpenBFMEPageSpec &spec() const { return m_spec; }

	// Updates a row in place (value, enabled, items, valueText, help) without rebuilding the page or sending messages. False: no such row.
	bool setRow(const OpenBFMEPageRow &row);

	// A gadget message the owner window received; true when it came from one of the page's gadgets (an event may be queued).
	bool message(std::uint32_t msg, WindowMsgData data1, WindowMsgData data2);
	// The mouse in stage pixels: kind 0 move, 1 left button down, 2 up. Moves set the pointed row (the help line); a press on a tab is the page's
	// (returns true: the gadgets and the movie must not see it).
	bool mouse(int kind, float x, float y);

	// The page's frames and texts, drawn before (under) the gadgets.
	void decorate(GadgetDrawList &out) const;

	void push(const OpenBFMEPageEvent &e) { emit(e); }
	// Where the page's events go (AptOptionsScreen: its one queue, so a change and a bottom bar click keep their order); empty: takeEvents()
	void setSink(std::function<void(const OpenBFMEPageEvent &)> sink) { m_sink = std::move(sink); }
	std::vector<OpenBFMEPageEvent> takeEvents();

	// Tests and automation: the stage rectangle (x0 y0 x1 y1) of a row's gadget ("<key>") or a tab ("tab:<index>"); the gadget of a row; a choice
	// made through the gadget as a click makes it (the gadget's selection, then its message to the owner).
	bool rect(const std::string &name, std::array<int, 4> &out) const;
	GameWindow *gadget(const std::string &key) const;
	bool choose(const std::string &key, int value, std::string *error);
	int hoveredRow() const { return m_hoverRow; }
	// the help line drawn now
	UnicodeString helpText() const;

private:
	struct Built
	{
		GameWindow *window = nullptr;
		int y = 0; // the row's top (stage)
	};
	void destroyGadgets();
	void applyRow(std::size_t index);
	int rowAt(float x, float y) const;
	int tabAt(float x, float y) const;
	std::array<int, 4> gadgetRect(std::size_t index) const;
	static void frame(GadgetDrawList &out, int x0, int y0, int x1, int y1, const UnicodeString &header);
	static void text(GadgetDrawList &out, const UnicodeString &s, int x, int y, int size, Color color, int wrapWidth = 0, bool centred = false);

	AptGadgetLayer &m_layer;
	GameWindow *m_owner = nullptr;
	OpenBFMEPageSpec m_spec;
	std::vector<Built> m_built; // per row of m_spec.rows
	GameWindow *m_list = nullptr;
	void emit(const OpenBFMEPageEvent &e);
	std::vector<OpenBFMEPageEvent> m_events;
	std::function<void(const OpenBFMEPageEvent &)> m_sink;
	bool m_shown = false;
	bool m_syncing = false; // set while the page itself moves a gadget (no events)
	int m_hoverRow = -1;
	int m_hoverTab = -1;
	int m_pressTab = -1;
};
