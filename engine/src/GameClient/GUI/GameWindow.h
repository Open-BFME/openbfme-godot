// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The native window of the SAGE GUI (spec menus-apt.md 3.1 "GameWindow + Gadget*", build step A5): the node of the window tree that the
// APT screens' gadgets (ListBox, ComboBox, TextEntry, CheckBox, HorzSlider and the push buttons / sliders inside them) are made of.
//
// Donor facts (ZH GameEngine/Include/GameClient/GameWindow.h, WinInstanceData.h, Source/GameClient/GUI/GameWindow.cpp): status and style
// bits, the message ids, the instance data (nine draw-data slots per state, text colours, font, tooltip), the tree links (children are
// pushed at the HEAD of the parent's list, hit tests walk the list from the head), position and region maths, enable / hide recursion.
// The BFME1 decompile (GameWindowManagerScript.cpp, 2331 lines) is the same text in the parts ported here.
// Differences by design: callbacks are std::function; a window knows its GameWindowManager (no TheWindowManager global); message data are
// uintptr_t (ZH packs pointers into 32-bit UnsignedInt); text is UTF-16 std::u16string (ZH UnicodeString); an image is a name (the
// MappedImageCollection answers its size, the renderer draws it); a font is a GameFont (name, size, bold) with the height the metrics
// provider reports (stop S-176).

#pragma once

#include "Common/INIDataTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class GameWindow;
class GameWindowManager;
struct GameFont;

typedef std::uint32_t Color;
typedef std::uintptr_t WindowMsgData;
typedef std::u16string UnicodeString;
typedef std::string AsciiString;

struct IRegion2D
{
	ICoord2D lo, hi;
};

enum
{
	WIN_COLOR_UNDEFINED = 0x00FFFFFF // ZH GameClient/Color.h GAME_COLOR_UNDEFINED
};

enum WindowMsgHandledType
{
	MSG_IGNORED,
	MSG_HANDLED
};

enum
{
	WIN_MAX_WINDOWS = 576,
	CURSOR_MOVE_TOL_SQ = 4,
	TOOLTIP_DELAY = 10,
	WIN_TOOLTIP_LEN = 64
};

inline std::uint32_t SHORTTOLONG(int a, int b) { return (std::uint32_t)((std::uint16_t)a | ((std::uint32_t)(std::uint16_t)b << 16)); }
inline int LOLONGTOSHORT(std::uint32_t a) { return (int)(a & 0x0000FFFFu); }
inline int HILONGTOSHORT(std::uint32_t b) { return (int)((b & 0xFFFF0000u) >> 16); }

enum GameWindowMessage
{
	GWM_NONE = 0,
	GWM_CREATE,
	GWM_DESTROY,
	GWM_ACTIVATE,
	GWM_ENABLE,
	GWM_LEFT_DOWN,
	GWM_LEFT_UP,
	GWM_LEFT_DOUBLE_CLICK,
	GWM_LEFT_DRAG,
	GWM_MIDDLE_DOWN,
	GWM_MIDDLE_UP,
	GWM_MIDDLE_DOUBLE_CLICK,
	GWM_MIDDLE_DRAG,
	GWM_RIGHT_DOWN,
	GWM_RIGHT_UP,
	GWM_RIGHT_DOUBLE_CLICK,
	GWM_RIGHT_DRAG,
	GWM_MOUSE_ENTERING,
	GWM_MOUSE_LEAVING,
	GWM_WHEEL_UP,
	GWM_WHEEL_DOWN,
	GWM_CHAR,
	GWM_SCRIPT_CREATE,
	GWM_INPUT_FOCUS,
	GWM_MOUSE_POS,
	GWM_IME_CHAR,
	GWM_IME_STRING
};

enum WinInputReturnCode
{
	WIN_INPUT_NOT_USED = 0,
	WIN_INPUT_USED
};

enum
{
	WIN_STATUS_NONE = 0x00000000,
	WIN_STATUS_ACTIVE = 0x00000001,
	WIN_STATUS_TOGGLE = 0x00000002,
	WIN_STATUS_DRAGABLE = 0x00000004,
	WIN_STATUS_ENABLED = 0x00000008,
	WIN_STATUS_HIDDEN = 0x00000010,
	WIN_STATUS_ABOVE = 0x00000020,
	WIN_STATUS_BELOW = 0x00000040,
	WIN_STATUS_IMAGE = 0x00000080,
	WIN_STATUS_TAB_STOP = 0x00000100,
	WIN_STATUS_NO_INPUT = 0x00000200,
	WIN_STATUS_NO_FOCUS = 0x00000400,
	WIN_STATUS_DESTROYED = 0x00000800,
	WIN_STATUS_BORDER = 0x00001000,
	WIN_STATUS_SMOOTH_TEXT = 0x00002000,
	WIN_STATUS_ONE_LINE = 0x00004000,
	WIN_STATUS_NO_FLUSH = 0x00008000,
	WIN_STATUS_SEE_THRU = 0x00010000,
	WIN_STATUS_RIGHT_CLICK = 0x00020000,
	WIN_STATUS_WRAP_CENTERED = 0x00040000,
	WIN_STATUS_CHECK_LIKE = 0x00080000,
	WIN_STATUS_HOTKEY_TEXT = 0x00100000,
	WIN_STATUS_USE_OVERLAY_STATES = 0x00200000,
	WIN_STATUS_NOT_READY = 0x00400000,
	WIN_STATUS_FLASHING = 0x00800000,
	WIN_STATUS_ALWAYS_COLOR = 0x01000000,
	WIN_STATUS_ON_MOUSE_DOWN = 0x02000000,
	// RotWK 2.01 game.dat WindowStatusNames table (0x00DA3244, 28 names): ZH's 26, then these three.  ZH's WIN_STATUS_SHORTCUT_BUTTON (0x04000000)
	// has no name in the target table; its bit is the target's CIRCULAR.
	WIN_STATUS_CIRCULAR = 0x04000000,
	WIN_STATUS_AVAIL_FLAG = 0x08000000,
	WIN_STATUS_BLOCK_INPUT = 0x10000000
};

// The names a WND file's STATUS line uses, bit i = (1 << i): RotWK game.dat 0x00DA3244 (ZH GameWindowManagerScript.cpp WindowStatusNames + 3)
const char *const *WindowStatusNames();
// RotWK game.dat 0x00DA3280 (= ZH WindowStyleNames): GWS_* bit i
const char *const *WindowStyleNames();

enum
{
	WIN_STATE_HILITED = 0x00000002,
	WIN_STATE_SELECTED = 0x00000004
};

enum
{
	MAX_WINDOW_NAME_LEN = 64,
	MAX_DRAW_DATA = 9,
	MAX_TEXT_LABEL = 128
};

enum
{
	WIN_ERR_OK = 0,
	WIN_ERR_GENERAL_FAILURE = -1,
	WIN_ERR_INVALID_WINDOW = -2,
	WIN_ERR_INVALID_PARAMETER = -3,
	WIN_ERR_MOUSE_CAPTURED = -4,
	WIN_ERR_KEYBOARD_CAPTURED = -5,
	WIN_ERR_OUT_OF_WINDOWS = -6
};

// One slot of the per-state draw data (ZH WinDrawData; the Image is the mapped image's name, empty = "NoImage").
struct WinDrawData
{
	std::string image;
	Color color = 0;
	Color borderColor = 0;
};

struct TextDrawData
{
	Color color = 0;
	Color borderColor = 0;
};

// A font request resolved by the window manager's font library.  `height` is what the metrics provider says (stop S-176).
struct GameFont
{
	std::string name;
	int pointSize = 0;
	bool bold = false;
	int height = 0;
};

// ZH WinInstanceData.h
class WinInstanceData
{
public:
	WinInstanceData() { init(); }
	void init();

	void setTooltipText(const UnicodeString &tip) { m_tooltipText = tip; }
	void setText(const UnicodeString &text) { m_text = text; }
	const UnicodeString &getTooltipText() const { return m_tooltipText; }
	const UnicodeString &getText() const { return m_text; }
	int getTextLength() const { return (int)m_text.size(); }
	int getTooltipTextLength() const { return (int)m_tooltipText.size(); }
	std::uint32_t getStyle() const { return m_style; }
	std::uint32_t getStatus() const { return m_status; }
	std::uint32_t getState() const { return (std::uint32_t)m_state; }
	GameWindow *getOwner() const { return m_owner; }
	GameFont *getFont() const { return m_font; }

	int m_id = 0;
	int m_state = 0;
	std::uint32_t m_style = 0;
	std::uint32_t m_status = 0;
	GameWindow *m_owner = nullptr;
	WinDrawData m_enabledDrawData[MAX_DRAW_DATA];
	WinDrawData m_disabledDrawData[MAX_DRAW_DATA];
	WinDrawData m_hiliteDrawData[MAX_DRAW_DATA];
	TextDrawData m_enabledText;
	TextDrawData m_disabledText;
	TextDrawData m_hiliteText;
	TextDrawData m_imeCompositeText;
	ICoord2D m_imageOffset;
	GameFont *m_font = nullptr;
	AsciiString m_textLabelString;
	AsciiString m_decoratedNameString;
	AsciiString m_tooltipString;
	AsciiString m_headerTemplateName;
	int m_tooltipDelay = -1;

private:
	UnicodeString m_text;
	UnicodeString m_tooltipText;
};

typedef std::function<void(GameWindow *, WinInstanceData *)> GameWinDrawFunc;
typedef std::function<void(GameWindow *, WinInstanceData *, std::uint32_t)> GameWinTooltipFunc;
typedef std::function<WindowMsgHandledType(GameWindow *, std::uint32_t, WindowMsgData, WindowMsgData)> GameWinInputFunc;
typedef std::function<WindowMsgHandledType(GameWindow *, std::uint32_t, WindowMsgData, WindowMsgData)> GameWinSystemFunc;

WindowMsgHandledType GameWinDefaultSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
WindowMsgHandledType GameWinDefaultInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
void GameWinDefaultTooltip(GameWindow *window, WinInstanceData *instData, std::uint32_t mouse);
// W3DGameWinDefaultDraw (W3DGameWindow.cpp): the background of a plain window, as draw commands.
void GameWinDefaultDraw(GameWindow *window, WinInstanceData *instData);

template <class T>
inline WindowMsgData msgData(T *p) { return reinterpret_cast<WindowMsgData>(p); }
template <class T>
inline T *msgPtr(WindowMsgData d) { return reinterpret_cast<T *>(d); }

// ZH GameWindow.h class GameWindow (W3DGameWindow merged in: the border and default draw are device-free draw commands).
class GameWindow
{
	friend class GameWindowManager;

public:
	explicit GameWindow(GameWindowManager &manager);
	~GameWindow();

	GameWindow(const GameWindow &) = delete;
	GameWindow &operator=(const GameWindow &) = delete;

	GameWindowManager &manager() const { return m_manager; }

	// W3DGameWindow::winDrawBorder: the border pieces of the instance's style (BorderCornerUL ... mapped images) as draw commands.
	void winDrawBorder();

	int winSetWindowId(int id);
	int winGetWindowId();
	int winSetSize(int width, int height);
	int winGetSize(int *width, int *height);
	int winActivate();
	int winBringToTop();
	int winEnable(bool enable);
	bool winGetEnabled();
	int winHide(bool hide);
	bool winIsHidden();
	std::uint32_t winSetStatus(std::uint32_t status);
	std::uint32_t winClearStatus(std::uint32_t status);
	std::uint32_t winGetStatus();
	std::uint32_t winGetStyle();
	int winNextTab();
	int winPrevTab();
	int winSetPosition(int x, int y);
	int winGetPosition(int *x, int *y);
	int winGetScreenPosition(int *x, int *y);
	int winGetRegion(IRegion2D *region);
	int winSetCursorPosition(int x, int y);
	int winGetCursorPosition(int *x, int *y);

	int winSetEnabledImage(int index, const std::string &image);
	int winSetEnabledColor(int index, Color color);
	int winSetEnabledBorderColor(int index, Color color);
	const std::string &winGetEnabledImage(int index) { return m_instData.m_enabledDrawData[index].image; }
	Color winGetEnabledColor(int index) { return m_instData.m_enabledDrawData[index].color; }
	Color winGetEnabledBorderColor(int index) { return m_instData.m_enabledDrawData[index].borderColor; }
	int winSetDisabledImage(int index, const std::string &image);
	int winSetDisabledColor(int index, Color color);
	int winSetDisabledBorderColor(int index, Color color);
	const std::string &winGetDisabledImage(int index) { return m_instData.m_disabledDrawData[index].image; }
	Color winGetDisabledColor(int index) { return m_instData.m_disabledDrawData[index].color; }
	Color winGetDisabledBorderColor(int index) { return m_instData.m_disabledDrawData[index].borderColor; }
	int winSetHiliteImage(int index, const std::string &image);
	int winSetHiliteColor(int index, Color color);
	int winSetHiliteBorderColor(int index, Color color);
	const std::string &winGetHiliteImage(int index) { return m_instData.m_hiliteDrawData[index].image; }
	Color winGetHiliteColor(int index) { return m_instData.m_hiliteDrawData[index].color; }
	Color winGetHiliteBorderColor(int index) { return m_instData.m_hiliteDrawData[index].borderColor; }

	int winDrawWindow();
	void winSetDrawOffset(int x, int y);
	void winGetDrawOffset(int *x, int *y);
	void winSetHiliteState(bool state);
	void winSetTooltip(const UnicodeString &tip);
	int getTooltipDelay() { return m_instData.m_tooltipDelay; }
	void setTooltipDelay(int delay) { m_instData.m_tooltipDelay = delay; }

	virtual int winSetText(const UnicodeString &newText);
	UnicodeString winGetText();
	int winGetTextLength();
	GameFont *winGetFont();
	virtual void winSetFont(GameFont *font);
	void winSetEnabledTextColors(Color color, Color borderColor);
	void winSetDisabledTextColors(Color color, Color borderColor);
	void winSetIMECompositeTextColors(Color color, Color borderColor);
	void winSetHiliteTextColors(Color color, Color borderColor);
	Color winGetEnabledTextColor();
	Color winGetEnabledTextBorderColor();
	Color winGetDisabledTextColor();
	Color winGetDisabledTextBorderColor();
	Color winGetIMECompositeTextColor();
	Color winGetIMECompositeBorderColor();
	Color winGetHiliteTextColor();
	Color winGetHiliteTextBorderColor();

	int winSetInstanceData(WinInstanceData *data);
	WinInstanceData *winGetInstanceData();
	void *winGetUserData();
	void winSetUserData(void *userData);

	int winSetParent(GameWindow *parent);
	GameWindow *winGetParent();
	bool winIsChild(GameWindow *child);
	GameWindow *winGetChild();
	int winSetOwner(GameWindow *owner);
	GameWindow *winGetOwner();
	void winSetNext(GameWindow *next) { m_next = next; }
	void winSetPrev(GameWindow *prev) { m_prev = prev; }
	GameWindow *winGetNext() { return m_next; }
	GameWindow *winGetPrev() { return m_prev; }

	int winSetSystemFunc(GameWinSystemFunc system);
	int winSetInputFunc(GameWinInputFunc input);
	int winSetDrawFunc(GameWinDrawFunc draw);
	int winSetTooltipFunc(GameWinTooltipFunc tooltip);
	int winSetCallbacks(GameWinInputFunc input, GameWinDrawFunc draw, GameWinTooltipFunc tooltip);

	bool winPointInWindow(int x, int y);
	GameWindow *winPointInChild(int x, int y, bool ignoreEnableCheck = false, bool playDisabledSound = false);
	GameWindow *winPointInAnyChild(int x, int y, bool ignoreHidden, bool ignoreEnableCheck = false);

	GameWinInputFunc winGetInputFunc() { return m_input; }
	GameWinSystemFunc winGetSystemFunc() { return m_system; }
	GameWinDrawFunc winGetDrawFunc() { return m_draw; }
	GameWinTooltipFunc winGetTooltipFunc() { return m_tooltip; }

	// An identity for diagnostics and tests: the decorated name ("ListBox.wnd:") plus the window id.
	const std::string &name() const { return m_instData.m_decoratedNameString; }
	// The instance name the APT screen knows this window by (set by the component factory).
	const std::string &aptInstanceName() const { return m_aptInstanceName; }
	void setAptInstanceName(const std::string &n) { m_aptInstanceName = n; }

protected:
	bool isEnabled();
	void normalizeWindowRegion();

	int m_status = 0;
	ICoord2D m_size;
	IRegion2D m_region;
	int m_cursorX = 0;
	int m_cursorY = 0;
	void *m_userData = nullptr;
	WinInstanceData m_instData;
	void *m_inputData = nullptr;
	GameWinInputFunc m_input;
	GameWinSystemFunc m_system;
	GameWinDrawFunc m_draw;
	GameWinTooltipFunc m_tooltip;
	GameWindow *m_next = nullptr;
	GameWindow *m_prev = nullptr;
	GameWindow *m_parent = nullptr;
	GameWindow *m_child = nullptr;
	GameWindowManager &m_manager;
	std::string m_aptInstanceName;
};

inline bool BitTest(std::uint32_t value, std::uint32_t bits) { return (value & bits) != 0; }
inline void BitSet(std::uint32_t &value, std::uint32_t bits) { value |= bits; }
inline void BitClear(std::uint32_t &value, std::uint32_t bits) { value &= ~bits; }
inline bool BitTest(int value, std::uint32_t bits) { return ((std::uint32_t)value & bits) != 0; }
inline void BitSet(int &value, std::uint32_t bits) { value = (int)((std::uint32_t)value | bits); }
inline void BitClear(int &value, std::uint32_t bits) { value = (int)((std::uint32_t)value & ~bits); }
