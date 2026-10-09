// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// Port of ZH GameEngine/Source/GameClient/GUI/GameWindow.cpp, WinInstanceData.cpp and GameEngineDevice W3DGameWindow.cpp (the border and
// the default background).  See GameClient/GUI/GameWindow.h.

#include "GameClient/GUI/GameWindow.h"

#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/Gadgets.h"

#include <algorithm>

// RotWK game.dat 0x00DA3244 (28 names, NULL-terminated): ZH's WindowStatusNames plus CIRCULAR, AVAIL_FLAG, BLOCK_INPUT
const char *const *WindowStatusNames()
{
	static const char *const names[] = { "ACTIVE", "TOGGLE", "DRAGABLE", "ENABLED", "HIDDEN", "ABOVE", "BELOW", "IMAGE", "TABSTOP", "NOINPUT", "NOFOCUS",
		"DESTROYED", "BORDER", "SMOOTH_TEXT", "ONE_LINE", "NO_FLUSH", "SEE_THRU", "RIGHT_CLICK", "WRAP_CENTERED", "CHECK_LIKE", "HOTKEY_TEXT",
		"USE_OVERLAY_STATES", "NOT_READY", "FLASHING", "ALWAYS_COLOR", "ON_MOUSE_DOWN", "WIN_STATUS_CIRCULAR", "WIN_STATUS_AVAIL_FLAG",
		"WIN_STATUS_BLOCK_INPUT", nullptr };
	return names;
}

// RotWK game.dat 0x00DA3280
const char *const *WindowStyleNames()
{
	static const char *const names[] = { "PUSHBUTTON", "RADIOBUTTON", "CHECKBOX", "VERTSLIDER", "HORZSLIDER", "SCROLLLISTBOX", "ENTRYFIELD", "STATICTEXT",
		"PROGRESSBAR", "USER", "MOUSETRACK", "ANIMATED", "TABSTOP", "TABCONTROL", "TABPANE", "COMBOBOX", nullptr };
	return names;
}

// ZH WinInstanceData.cpp WinInstanceData::init
void WinInstanceData::init()
{
	for (int i = 0; i < MAX_DRAW_DATA; ++i)
	{
		m_enabledDrawData[i] = WinDrawData();
		m_enabledDrawData[i].color = WIN_COLOR_UNDEFINED;
		m_enabledDrawData[i].borderColor = WIN_COLOR_UNDEFINED;
		m_disabledDrawData[i] = m_enabledDrawData[i];
		m_hiliteDrawData[i] = m_enabledDrawData[i];
	}
	m_enabledText.color = m_enabledText.borderColor = WIN_COLOR_UNDEFINED;
	m_disabledText.color = m_disabledText.borderColor = WIN_COLOR_UNDEFINED;
	m_hiliteText.color = m_hiliteText.borderColor = WIN_COLOR_UNDEFINED;
	m_imeCompositeText.color = m_imeCompositeText.borderColor = 0;
	m_id = 0;
	m_state = 0;
	m_style = 0;
	m_status = WIN_STATUS_NONE;
	m_owner = nullptr;
	m_textLabelString.clear();
	m_tooltipString.clear();
	m_tooltipDelay = -1;
	m_decoratedNameString.clear();
	m_headerTemplateName.clear();
	m_imageOffset = ICoord2D();
	m_font = nullptr;
	m_text.clear();
	m_tooltipText.clear();
}

WindowMsgHandledType GameWinDefaultSystem(GameWindow *, std::uint32_t, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType GameWinDefaultInput(GameWindow *, std::uint32_t, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
void GameWinDefaultTooltip(GameWindow *, WinInstanceData *, std::uint32_t) {}

// W3DGameWindow.cpp W3DGameWinDefaultDraw
void GameWinDefaultDraw(GameWindow *window, WinInstanceData *instData)
{
	const float borderWidth = 1.0f;
	ICoord2D origin, size;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	GameWindowManager &mgr = window->manager();
	if (BitTest(window->winGetStatus(), WIN_STATUS_IMAGE))
	{
		const std::string *image;
		if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
		{
			image = &window->winGetDisabledImage(0);
		}
		else if (BitTest(instData->getState(), WIN_STATE_HILITED))
		{
			image = &window->winGetHiliteImage(0);
		}
		else
		{
			image = &window->winGetEnabledImage(0);
		}
		if (!image->empty())
		{
			const int sx = origin.x + instData->m_imageOffset.x;
			const int sy = origin.y + instData->m_imageOffset.y;
			mgr.winDrawImage(*image, sx, sy, sx + size.x, sy + size.y);
		}
	}
	else
	{
		Color color, borderColor;
		if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
		{
			color = window->winGetDisabledColor(0);
			borderColor = window->winGetDisabledBorderColor(0);
		}
		else if (BitTest(instData->getState(), WIN_STATE_HILITED))
		{
			color = window->winGetHiliteColor(0);
			borderColor = window->winGetHiliteBorderColor(0);
		}
		else
		{
			color = window->winGetEnabledColor(0);
			borderColor = window->winGetEnabledBorderColor(0);
		}
		if (borderColor != WIN_COLOR_UNDEFINED)
		{
			mgr.winOpenRect(borderColor, borderWidth, origin.x, origin.y, origin.x + size.x, origin.y + size.y);
		}
		if (color != WIN_COLOR_UNDEFINED)
		{
			mgr.winFillRect(color, borderWidth, origin.x + (int)borderWidth, origin.y + (int)borderWidth, origin.x + size.x - (int)borderWidth, origin.y + size.y - (int)borderWidth);
		}
	}
}

GameWindow::GameWindow(GameWindowManager &manager) : m_manager(manager)
{
	m_size = ICoord2D();
	m_region = IRegion2D();
	m_system = GameWinDefaultSystem;
	m_input = GameWinDefaultInput;
	m_draw = GameWinDefaultDraw;
}

GameWindow::~GameWindow() = default;

void GameWindow::normalizeWindowRegion()
{
	if (m_region.lo.x > m_region.hi.x)
	{
		std::swap(m_region.lo.x, m_region.hi.x);
	}
	if (m_region.lo.y > m_region.hi.y)
	{
		std::swap(m_region.lo.y, m_region.hi.y);
	}
}

int GameWindow::winBringToTop()
{
	GameWindow *parent = winGetParent();
	if (parent)
	{
		m_manager.unlinkChildWindow(this);
		m_manager.addWindowToParent(this, parent);
	}
	else
	{
		GameWindow *current = m_manager.winGetWindowList();
		for (; current != this; current = current->m_next)
		{
			if (current == nullptr)
			{
				return WIN_ERR_INVALID_PARAMETER;
			}
		}
		m_manager.unlinkWindow(this);
		m_manager.linkWindow(this);
	}
	return WIN_ERR_OK;
}

int GameWindow::winActivate()
{
	const int rc = winBringToTop();
	if (rc != WIN_ERR_OK)
	{
		return rc;
	}
	BitSet(m_status, WIN_STATUS_ACTIVE);
	winHide(false);
	return WIN_ERR_OK;
}

int GameWindow::winSetPosition(int x, int y)
{
	m_region.lo.x = x;
	m_region.lo.y = y;
	m_region.hi.x = x + m_size.x;
	m_region.hi.y = y + m_size.y;
	normalizeWindowRegion();
	return WIN_ERR_OK;
}

int GameWindow::winGetPosition(int *x, int *y)
{
	if (x == nullptr || y == nullptr)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	*x = m_region.lo.x;
	*y = m_region.lo.y;
	return WIN_ERR_OK;
}

int GameWindow::winSetCursorPosition(int x, int y)
{
	m_cursorX = x;
	m_cursorY = y;
	return WIN_ERR_OK;
}

int GameWindow::winGetCursorPosition(int *x, int *y)
{
	if (x)
	{
		*x = m_cursorX;
	}
	if (y)
	{
		*y = m_cursorY;
	}
	return WIN_ERR_OK;
}

int GameWindow::winGetScreenPosition(int *x, int *y)
{
	GameWindow *parent = m_parent;
	*x = m_region.lo.x;
	*y = m_region.lo.y;
	while (parent)
	{
		*x += parent->m_region.lo.x;
		*y += parent->m_region.lo.y;
		parent = parent->m_parent;
	}
	return WIN_ERR_OK;
}

int GameWindow::winGetRegion(IRegion2D *region)
{
	if (region)
	{
		*region = m_region;
	}
	return WIN_ERR_OK;
}

bool GameWindow::winPointInWindow(int x, int y)
{
	int winX, winY, width, height;
	winGetScreenPosition(&winX, &winY);
	winGetSize(&width, &height);
	return x >= winX && x <= winX + width && y >= winY && y <= winY + height;
}

int GameWindow::winSetSize(int width, int height)
{
	m_size.x = width;
	m_size.y = height;
	m_region.hi.x = m_region.lo.x + width;
	m_region.hi.y = m_region.lo.y + height;
	m_manager.winSendSystemMsg(this, GGM_RESIZED, (WindowMsgData)width, (WindowMsgData)height);
	return WIN_ERR_OK;
}

int GameWindow::winGetSize(int *width, int *height)
{
	if (width == nullptr || height == nullptr)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	*width = m_size.x;
	*height = m_size.y;
	return WIN_ERR_OK;
}

int GameWindow::winEnable(bool enable)
{
	if (enable)
	{
		BitSet(m_status, WIN_STATUS_ENABLED);
	}
	else
	{
		BitClear(m_status, WIN_STATUS_ENABLED);
	}
	for (GameWindow *child = m_child; child; child = child->m_next)
	{
		child->winEnable(enable);
	}
	return WIN_ERR_OK;
}

bool GameWindow::winGetEnabled() { return BitTest(m_status, WIN_STATUS_ENABLED); }

int GameWindow::winHide(bool hide)
{
	if (hide)
	{
		BitSet(m_status, WIN_STATUS_HIDDEN);
		m_manager.windowHiding(this);
	}
	else
	{
		BitClear(m_status, WIN_STATUS_HIDDEN);
	}
	return WIN_ERR_OK;
}

bool GameWindow::winIsHidden() { return BitTest(m_status, WIN_STATUS_HIDDEN); }

std::uint32_t GameWindow::winSetStatus(std::uint32_t status)
{
	const std::uint32_t old = (std::uint32_t)m_status;
	BitSet(m_status, status);
	return old;
}

std::uint32_t GameWindow::winClearStatus(std::uint32_t status)
{
	const std::uint32_t old = (std::uint32_t)m_status;
	BitClear(m_status, status);
	return old;
}

std::uint32_t GameWindow::winGetStatus() { return (std::uint32_t)m_status; }
std::uint32_t GameWindow::winGetStyle() { return m_instData.getStyle(); }

int GameWindow::winNextTab()
{
	m_manager.winNextTab(this);
	return WIN_ERR_OK;
}

int GameWindow::winPrevTab()
{
	m_manager.winPrevTab(this);
	return WIN_ERR_OK;
}

void GameWindow::winSetHiliteState(bool state)
{
	if (state)
	{
		BitSet(m_instData.m_state, WIN_STATE_HILITED);
	}
	else
	{
		BitClear(m_instData.m_state, WIN_STATE_HILITED);
	}
}

void GameWindow::winSetDrawOffset(int x, int y)
{
	m_instData.m_imageOffset.x = x;
	m_instData.m_imageOffset.y = y;
}

void GameWindow::winGetDrawOffset(int *x, int *y)
{
	*x = m_instData.m_imageOffset.x;
	*y = m_instData.m_imageOffset.y;
}

int GameWindow::winSetText(const UnicodeString &newText)
{
	m_instData.setText(newText);
	return WIN_ERR_OK;
}

UnicodeString GameWindow::winGetText() { return m_instData.getText(); }
int GameWindow::winGetTextLength() { return m_instData.getTextLength(); }
GameFont *GameWindow::winGetFont() { return m_instData.getFont(); }

void GameWindow::winSetFont(GameFont *font)
{
	// ZH GameWindow::winSetFont dispatches the font to the gadget's children and display strings; the display strings are plain text
	// here, the list / combo children are re-fonted below
	m_instData.m_font = font;
	if (BitTest(m_instData.getStyle(), GWS_SCROLL_LISTBOX))
	{
		GadgetListBoxSetFont(this, font);
	}
	else if (BitTest(m_instData.getStyle(), GWS_COMBO_BOX))
	{
		GadgetComboBoxSetFont(this, font);
	}
}

void GameWindow::winSetEnabledTextColors(Color color, Color borderColor)
{
	m_instData.m_enabledText.color = color;
	m_instData.m_enabledText.borderColor = borderColor;
}

void GameWindow::winSetDisabledTextColors(Color color, Color borderColor)
{
	m_instData.m_disabledText.color = color;
	m_instData.m_disabledText.borderColor = borderColor;
}

void GameWindow::winSetHiliteTextColors(Color color, Color borderColor)
{
	m_instData.m_hiliteText.color = color;
	m_instData.m_hiliteText.borderColor = borderColor;
}

void GameWindow::winSetIMECompositeTextColors(Color color, Color borderColor)
{
	m_instData.m_imeCompositeText.color = color;
	m_instData.m_imeCompositeText.borderColor = borderColor;
}

Color GameWindow::winGetEnabledTextColor() { return m_instData.m_enabledText.color; }
Color GameWindow::winGetEnabledTextBorderColor() { return m_instData.m_enabledText.borderColor; }
Color GameWindow::winGetDisabledTextColor() { return m_instData.m_disabledText.color; }
Color GameWindow::winGetDisabledTextBorderColor() { return m_instData.m_disabledText.borderColor; }
Color GameWindow::winGetIMECompositeTextColor() { return m_instData.m_imeCompositeText.color; }
Color GameWindow::winGetIMECompositeBorderColor() { return m_instData.m_imeCompositeText.borderColor; }
Color GameWindow::winGetHiliteTextColor() { return m_instData.m_hiliteText.color; }
Color GameWindow::winGetHiliteTextBorderColor() { return m_instData.m_hiliteText.borderColor; }

// ZH GameWindow::winSetInstanceData: a copy that keeps this window's own display strings (here: the text is copied)
int GameWindow::winSetInstanceData(WinInstanceData *data)
{
	m_instData = *data;
	return WIN_ERR_OK;
}

WinInstanceData *GameWindow::winGetInstanceData() { return &m_instData; }
void *GameWindow::winGetUserData() { return m_userData; }
void GameWindow::winSetUserData(void *data) { m_userData = data; }
void GameWindow::winSetTooltip(const UnicodeString &tip) { m_instData.setTooltipText(tip); }

int GameWindow::winSetWindowId(int id)
{
	m_instData.m_id = id;
	return WIN_ERR_OK;
}

int GameWindow::winGetWindowId() { return m_instData.m_id; }

int GameWindow::winSetParent(GameWindow *parent)
{
	if (m_parent == nullptr)
	{
		m_manager.unlinkWindow(this);
	}
	else
	{
		m_manager.unlinkChildWindow(this);
	}
	if (parent == nullptr)
	{
		m_manager.linkWindow(this);
		m_parent = nullptr;
	}
	else
	{
		m_manager.addWindowToParent(this, parent);
	}
	return WIN_ERR_OK;
}

GameWindow *GameWindow::winGetParent() { return m_parent; }

bool GameWindow::winIsChild(GameWindow *child)
{
	while (child)
	{
		if (this == child->m_parent)
		{
			return true;
		}
		child = child->m_parent;
	}
	return false;
}

GameWindow *GameWindow::winGetChild() { return m_child; }

int GameWindow::winSetOwner(GameWindow *owner)
{
	// ZH GameWindow::winSetOwner: a null owner means the window owns itself
	m_instData.m_owner = owner ? owner : this;
	return WIN_ERR_OK;
}

GameWindow *GameWindow::winGetOwner() { return m_instData.getOwner(); }

int GameWindow::winSetSystemFunc(GameWinSystemFunc system)
{
	m_system = system ? system : GameWinSystemFunc(GameWinDefaultSystem);
	return WIN_ERR_OK;
}

int GameWindow::winSetInputFunc(GameWinInputFunc input)
{
	m_input = input ? input : GameWinInputFunc(GameWinDefaultInput);
	return WIN_ERR_OK;
}

int GameWindow::winSetDrawFunc(GameWinDrawFunc draw)
{
	m_draw = draw ? draw : GameWinDrawFunc(GameWinDefaultDraw);
	return WIN_ERR_OK;
}

int GameWindow::winSetTooltipFunc(GameWinTooltipFunc tooltip)
{
	m_tooltip = tooltip;
	return WIN_ERR_OK;
}

int GameWindow::winSetCallbacks(GameWinInputFunc input, GameWinDrawFunc draw, GameWinTooltipFunc tooltip)
{
	winSetInputFunc(input);
	winSetDrawFunc(draw);
	winSetTooltipFunc(tooltip);
	return WIN_ERR_OK;
}

int GameWindow::winDrawWindow()
{
	if (!BitTest(m_status, WIN_STATUS_HIDDEN) && m_draw)
	{
		m_draw(this, &m_instData);
	}
	return WIN_ERR_OK;
}

GameWindow *GameWindow::winPointInChild(int x, int y, bool ignoreEnableCheck, bool)
{
	for (GameWindow *child = m_child; child; child = child->m_next)
	{
		ICoord2D origin = child->m_region.lo;
		for (GameWindow *parent = child->winGetParent(); parent; parent = parent->m_parent)
		{
			origin.x += parent->m_region.lo.x;
			origin.y += parent->m_region.lo.y;
		}
		if (x >= origin.x && x <= origin.x + child->m_size.x && y >= origin.y && y <= origin.y + child->m_size.y)
		{
			const bool enabled = ignoreEnableCheck || BitTest(child->m_status, WIN_STATUS_ENABLED);
			const bool hidden = BitTest(child->m_status, WIN_STATUS_HIDDEN);
			if (!hidden && enabled)
			{
				return child->winPointInChild(x, y, ignoreEnableCheck);
			}
			// ZH plays "GUIClickDisabled" for a disabled child when asked: audio is a ShellServices concern, not ported (S-177)
		}
	}
	return this;
}

GameWindow *GameWindow::winPointInAnyChild(int x, int y, bool ignoreHidden, bool ignoreEnableCheck)
{
	for (GameWindow *child = m_child; child; child = child->m_next)
	{
		ICoord2D origin = child->m_region.lo;
		for (GameWindow *parent = child->m_parent; parent; parent = parent->m_parent)
		{
			origin.x += parent->m_region.lo.x;
			origin.y += parent->m_region.lo.y;
		}
		if (x >= origin.x && x <= origin.x + child->m_size.x && y >= origin.y && y <= origin.y + child->m_size.y)
		{
			if (!(ignoreHidden && BitTest(child->m_status, WIN_STATUS_HIDDEN)))
			{
				return child->winPointInChild(x, y, ignoreEnableCheck);
			}
		}
	}
	return this;
}

int GameWindow::winSetEnabledImage(int index, const std::string &image)
{
	if (index < 0 || index >= MAX_DRAW_DATA)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	m_instData.m_enabledDrawData[index].image = image;
	return WIN_ERR_OK;
}

int GameWindow::winSetEnabledColor(int index, Color color)
{
	if (index < 0 || index >= MAX_DRAW_DATA)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	m_instData.m_enabledDrawData[index].color = color;
	return WIN_ERR_OK;
}

int GameWindow::winSetEnabledBorderColor(int index, Color color)
{
	if (index < 0 || index >= MAX_DRAW_DATA)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	m_instData.m_enabledDrawData[index].borderColor = color;
	return WIN_ERR_OK;
}

int GameWindow::winSetDisabledImage(int index, const std::string &image)
{
	if (index < 0 || index >= MAX_DRAW_DATA)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	m_instData.m_disabledDrawData[index].image = image;
	return WIN_ERR_OK;
}

int GameWindow::winSetDisabledColor(int index, Color color)
{
	if (index < 0 || index >= MAX_DRAW_DATA)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	m_instData.m_disabledDrawData[index].color = color;
	return WIN_ERR_OK;
}

int GameWindow::winSetDisabledBorderColor(int index, Color color)
{
	if (index < 0 || index >= MAX_DRAW_DATA)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	m_instData.m_disabledDrawData[index].borderColor = color;
	return WIN_ERR_OK;
}

int GameWindow::winSetHiliteImage(int index, const std::string &image)
{
	if (index < 0 || index >= MAX_DRAW_DATA)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	m_instData.m_hiliteDrawData[index].image = image;
	return WIN_ERR_OK;
}

int GameWindow::winSetHiliteColor(int index, Color color)
{
	if (index < 0 || index >= MAX_DRAW_DATA)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	m_instData.m_hiliteDrawData[index].color = color;
	return WIN_ERR_OK;
}

int GameWindow::winSetHiliteBorderColor(int index, Color color)
{
	if (index < 0 || index >= MAX_DRAW_DATA)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	m_instData.m_hiliteDrawData[index].borderColor = color;
	return WIN_ERR_OK;
}

bool GameWindow::isEnabled()
{
	GameWindow *win = this;
	if (!BitTest(win->m_status, WIN_STATUS_ENABLED))
	{
		return false;
	}
	while (win->m_parent)
	{
		win = win->m_parent;
		if (!BitTest(win->m_status, WIN_STATUS_ENABLED))
		{
			return false;
		}
	}
	return true;
}

// W3DGameWindow::winDrawBorder with blitBorderRect (W3DGameWindow.cpp:59-250): the border pieces are mapped images
// BorderCornerUL ... BorderBottomShort (the retail menus' WND borders; no APT gadget of the skirmish path sets WIN_STATUS_BORDER on a window
// that is drawn with them, the combo box strips BORDER from its children, but the WND file's own STATUS may carry it: the pieces are
// emitted so the renderer shows what retail shows)
void GameWindow::winDrawBorder()
{
	enum
	{
		BORDER_CORNER_SIZE = 15,
		BORDER_LINE_SIZE = 20
	};
	int originalX, originalY;
	winGetScreenPosition(&originalX, &originalY);
	auto blit = [&](int bx, int by, int width, int height) {
		static const char *const pieces[] = { "BorderCornerUL", "BorderCornerUR", "BorderCornerLL", "BorderCornerLR", "BorderLeft", "BorderLeftShort", "BorderRight",
			"BorderRightShort", "BorderTop", "BorderTopShort", "BorderBottom", "BorderBottomShort" };
		enum { UL, UR, LL, LR, LEFT, LEFT_SHORT, RIGHT, RIGHT_SHORT, TOP, TOP_SHORT, BOTTOM, BOTTOM_SHORT };
		const int Offset = 15, OffsetLower = 5, size = 20, halfSize = 10;
		const int originalBX = bx, originalBY = by;
		const int maxX = bx + width, maxY = by + height;
		int x, y, x2, y2;
		y = originalBY - Offset;
		y2 = maxY - OffsetLower;
		x2 = maxX - (OffsetLower + BORDER_LINE_SIZE);
		for (x = originalBX + OffsetLower; x <= x2; x += BORDER_LINE_SIZE)
		{
			m_manager.winDrawImage(pieces[TOP], x, y, x + size, y + size);
			m_manager.winDrawImage(pieces[BOTTOM], x, y2, x + size, y2 + size);
		}
		x2 = maxX - 5;
		if ((x2 - x) >= (BORDER_LINE_SIZE / 2))
		{
			m_manager.winDrawImage(pieces[TOP_SHORT], x, y, x + halfSize, y + size);
			m_manager.winDrawImage(pieces[BOTTOM_SHORT], x, y2, x + halfSize, y2 + size);
			x += (BORDER_LINE_SIZE / 2);
		}
		if (x < x2)
		{
			x -= ((BORDER_LINE_SIZE / 2) - (((x2 - x) + 1) & ~1));
			m_manager.winDrawImage(pieces[TOP_SHORT], x, y, x + halfSize, y + size);
			m_manager.winDrawImage(pieces[BOTTOM_SHORT], x, y2, x + halfSize, y2 + size);
		}
		x = originalBX - Offset;
		x2 = maxX - OffsetLower;
		y2 = maxY - (OffsetLower + BORDER_LINE_SIZE);
		for (y = originalBY + OffsetLower; y <= y2; y += BORDER_LINE_SIZE)
		{
			m_manager.winDrawImage(pieces[LEFT], x, y, x + size, y + size);
			m_manager.winDrawImage(pieces[RIGHT], x2, y, x2 + size, y + size);
		}
		y2 = maxY - OffsetLower;
		if ((y2 - y) >= (BORDER_LINE_SIZE / 2))
		{
			m_manager.winDrawImage(pieces[LEFT_SHORT], x, y, x + size, y + halfSize);
			m_manager.winDrawImage(pieces[RIGHT_SHORT], x2, y, x2 + size, y + halfSize);
			y += (BORDER_LINE_SIZE / 2);
		}
		if (y < y2)
		{
			y -= ((BORDER_LINE_SIZE / 2) - (((y2 - y) + 1) & ~1));
			m_manager.winDrawImage(pieces[LEFT_SHORT], x, y, x + size, y + halfSize);
			m_manager.winDrawImage(pieces[RIGHT_SHORT], x2, y, x2 + size, y + halfSize);
		}
		x = originalBX - BORDER_CORNER_SIZE;
		y = originalBY - BORDER_CORNER_SIZE;
		m_manager.winDrawImage(pieces[UL], x, y, x + size, y + size);
		x = maxX - 5;
		y = originalBY - BORDER_CORNER_SIZE;
		m_manager.winDrawImage(pieces[UR], x, y, x + size, y + size);
		x = originalBX - BORDER_CORNER_SIZE;
		y = maxY - 5;
		m_manager.winDrawImage(pieces[LL], x, y, x + size, y + size);
		x = maxX - 5;
		y = maxY - 5;
		m_manager.winDrawImage(pieces[LR], x, y, x + size, y + size);
	};
	for (int i = 0; i < 32; ++i)
	{
		const std::uint32_t bits = 1u << i;
		if (!(m_instData.getStyle() & bits))
		{
			continue;
		}
		switch (m_instData.getStyle() & bits)
		{
			case GWS_CHECK_BOX:
			case GWS_VERT_SLIDER:
			case GWS_HORZ_SLIDER:
				return;
			case GWS_ENTRY_FIELD:
			{
				int width = m_size.x, x = originalX;
				if (m_instData.getTextLength())
				{
					const int textWidth = m_manager.winTextWidth(m_instData.getFont(), m_instData.getText());
					width -= textWidth + 6;
					x += textWidth + 6;
				}
				blit(x, originalY, width, m_size.y);
				return;
			}
			case GWS_SCROLL_LISTBOX:
			{
				ListboxData *list = static_cast<ListboxData *>(m_userData);
				int sliderAdjustment = 0;
				int labelAdjustment = 0;
				if (list && list->scrollBar && list->slider && list->slider->winGetChild())
				{
					int sw, sh;
					list->slider->winGetChild()->winGetSize(&sw, &sh);
					sliderAdjustment = sh;
				}
				if (m_instData.getTextLength())
				{
					labelAdjustment = 4;
				}
				blit(originalX - 3, originalY - (3 + labelAdjustment), m_size.x + 3 - sliderAdjustment, m_size.y + 6);
				return;
			}
			case GWS_RADIO_BUTTON:
			case GWS_STATIC_TEXT:
			case GWS_PROGRESS_BAR:
			case GWS_PUSH_BUTTON:
			case GWS_USER_WINDOW:
			case GWS_TAB_CONTROL:
				blit(originalX, originalY, m_size.x, m_size.y);
				return;
			default:
				break;
		}
	}
}
