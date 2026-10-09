// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// Port of ZH GameEngine/Source/GameClient/GUI/GameWindowManager.cpp (the parts listed in GameWindowManager.h).

#include "GameClient/GUI/GameWindowManager.h"

#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/Gadgets.h"

#include <algorithm>

int DefaultFontMetrics::wrappedHeight(const GameFont &font, const UnicodeString &text, int wrapWidth)
{
	const int line = fontHeight(font);
	if (wrapWidth <= 0 || text.empty())
	{
		return line;
	}
	const int total = textWidth(font, text);
	const int lines = std::max(1, (total + wrapWidth - 1) / wrapWidth);
	return lines * line;
}

GameWindowManager::GameWindowManager() = default;

GameWindowManager::~GameWindowManager()
{
	// the windows are plain objects: nothing sends GWM_DESTROY here (the gadget user data are released by winDestroyAll first)
	winDestroyAll();
	processDestroyList();
}

void GameWindowManager::note(const std::string &text)
{
	if (std::find(m_notes.begin(), m_notes.end(), text) == m_notes.end())
	{
		m_notes.push_back(text);
	}
}

std::vector<GameWindow *> GameWindowManager::allWindows() const
{
	std::vector<GameWindow *> out;
	for (const auto &w : m_owned)
	{
		if (!BitTest(w->m_status, WIN_STATUS_DESTROYED))
		{
			out.push_back(w.get());
		}
	}
	return out;
}

// ---- fonts -------------------------------------------------------------------------------------------------------------------------------------

GameFont *GameWindowManager::winFindFont(const std::string &name, int pointSize, bool bold)
{
	auto key = std::make_tuple(name, pointSize, bold);
	auto it = m_fonts.find(key);
	if (it == m_fonts.end())
	{
		auto font = std::make_unique<GameFont>();
		font->name = name;
		font->pointSize = pointSize;
		font->bold = bold;
		font->height = m_metrics->fontHeight(*font);
		it = m_fonts.emplace(key, std::move(font)).first;
	}
	return it->second.get();
}

int GameWindowManager::winFontHeight(GameFont *font)
{
	return font ? font->height : 0;
}

int GameWindowManager::winTextWidth(GameFont *font, const UnicodeString &text)
{
	return font ? m_metrics->textWidth(*font, text) : 0;
}

int GameWindowManager::winWrappedHeight(GameFont *font, const UnicodeString &text, int wrapWidth)
{
	return font ? m_metrics->wrappedHeight(*font, text, wrapWidth) : 0;
}

UnicodeString GameWindowManager::winTextLabelToText(const std::string &label)
{
	// ZH GameWindowManager::winTextLabelToText translates the label as it is (the string manager was never written there); here a text
	// resolver (the owner's game text) answers first, and a label it does not know is recorded and shown as the label.
	if (label.empty())
	{
		return UnicodeString();
	}
	UnicodeString out;
	if (m_textResolver && m_textResolver(label, out))
	{
		return out;
	}
	if (m_textResolver)
	{
		m_unresolvedLabels.push_back(label);
	}
	for (char c : label)
	{
		out.push_back((char16_t)(unsigned char)c);
	}
	return out;
}

// ---- tree --------------------------------------------------------------------------------------------------------------------------------------

GameWindow *GameWindowManager::winCreate(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, GameWinSystemFunc system, WinInstanceData *instData)
{
	if ((int)m_owned.size() >= WIN_MAX_WINDOWS)
	{
		note("winCreate: the window pool is full (WIN_MAX_WINDOWS)");
		return nullptr;
	}
	m_owned.push_back(std::make_unique<GameWindow>(*this));
	GameWindow *window = m_owned.back().get();
	if (parent)
	{
		addWindowToParent(window, parent);
	}
	else
	{
		linkWindow(window);
	}
	window->m_status = (int)status;
	window->m_size.x = width;
	window->m_size.y = height;
	window->m_region.lo.x = x;
	window->m_region.lo.y = y;
	window->m_region.hi.x = x + width;
	window->m_region.hi.y = y + height;
	window->normalizeWindowRegion();
	window->winSetSystemFunc(system);
	winSendSystemMsg(window, GWM_CREATE, 0, 0);
	if (instData)
	{
		window->winSetInstanceData(instData);
	}
	// ZH: the global language data's default window font, else Times New Roman 14 (the script's FONT / HEADERTEMPLATE replaces it)
	window->winSetFont(winFindFont("Times New Roman", 14, false));
	return window;
}

int GameWindowManager::winDestroy(GameWindow *window)
{
	if (window == nullptr)
	{
		return WIN_ERR_INVALID_WINDOW;
	}
	if (BitTest(window->m_status, WIN_STATUS_DESTROYED))
	{
		return WIN_ERR_OK;
	}
	BitSet(window->m_status, WIN_STATUS_DESTROYED);
	if (m_mouseCaptor == window)
	{
		winRelease(window);
	}
	if (m_keyboardFocus == window)
	{
		winSetFocus(nullptr);
	}
	if (!m_modal.empty() && window == m_modal.back())
	{
		winUnsetModal(m_modal.back());
	}
	if (m_currMouseRgn == window)
	{
		m_currMouseRgn = nullptr;
	}
	if (m_grabWindow == window)
	{
		m_grabWindow = nullptr;
	}
	if (m_loneWindow == window)
	{
		m_loneWindow = nullptr;
	}
	GameWindow *next;
	for (GameWindow *child = window->m_child; child; child = next)
	{
		next = child->m_next;
		winDestroy(child);
	}
	// the system function releases the gadget's own data (GWM_DESTROY is not blocked for destroyed windows)
	winSendSystemMsg(window, GWM_DESTROY, 0, 0);
	if (window->m_parent == nullptr)
	{
		unlinkWindow(window);
	}
	else
	{
		unlinkChildWindow(window);
	}
	window->m_prev = nullptr;
	window->m_next = nullptr;
	m_destroyList.push_back(window);
	return WIN_ERR_OK;
}

int GameWindowManager::winDestroyAll()
{
	GameWindow *next;
	for (GameWindow *win = m_windowList; win; win = next)
	{
		next = win->m_next;
		winDestroy(win);
	}
	m_windowList = m_windowTail = nullptr;
	return WIN_ERR_OK;
}

void GameWindowManager::processDestroyList()
{
	for (GameWindow *w : m_destroyList)
	{
		auto it = std::find_if(m_owned.begin(), m_owned.end(), [w](const std::unique_ptr<GameWindow> &p) { return p.get() == w; });
		if (it != m_owned.end())
		{
			m_owned.erase(it);
		}
	}
	m_destroyList.clear();
}

GameWindow *GameWindowManager::winGetWindowFromId(GameWindow *window, int id)
{
	if (window == nullptr)
	{
		window = m_windowList;
	}
	for (; window; window = window->m_next)
	{
		if (window->winGetWindowId() == id)
		{
			return window;
		}
		if (window->m_child)
		{
			GameWindow *child = winGetWindowFromId(window->m_child, id);
			if (child)
			{
				return child;
			}
		}
	}
	return nullptr;
}

void GameWindowManager::linkWindow(GameWindow *window)
{
	// the modal windows stay on top: a new window goes behind the last modal one in the list (ZH linkWindow)
	GameWindow *lastModalWindow = nullptr;
	for (GameWindow *tmp = m_windowList; tmp; tmp = tmp->m_next)
	{
		for (GameWindow *m : m_modal)
		{
			if (m == tmp && m != window)
			{
				lastModalWindow = tmp;
			}
		}
	}
	if (!lastModalWindow)
	{
		window->m_prev = nullptr;
		window->m_next = m_windowList;
		if (m_windowList)
		{
			m_windowList->m_prev = window;
		}
		else
		{
			m_windowTail = window;
		}
		m_windowList = window;
	}
	else
	{
		window->m_prev = lastModalWindow;
		window->m_next = lastModalWindow->m_next;
		lastModalWindow->m_next = window;
		if (window->m_next)
		{
			window->m_next->m_prev = window;
		}
	}
}

void GameWindowManager::insertWindowAheadOf(GameWindow *window, GameWindow *aheadOf)
{
	if (window == nullptr)
	{
		return;
	}
	if (aheadOf == nullptr)
	{
		linkWindow(window);
		return;
	}
	GameWindow *aheadOfParent = aheadOf->winGetParent();
	if (aheadOfParent == nullptr)
	{
		window->m_prev = aheadOf->m_prev;
		if (aheadOf->m_prev)
		{
			aheadOf->m_prev->m_next = window;
		}
		else
		{
			m_windowList = window;
		}
		aheadOf->m_prev = window;
		window->m_next = aheadOf;
	}
	else
	{
		window->m_prev = aheadOf->m_prev;
		if (aheadOf->m_prev)
		{
			aheadOf->m_prev->m_next = window;
		}
		else
		{
			aheadOfParent->m_child = window;
		}
		aheadOf->m_prev = window;
		window->m_next = aheadOf;
		window->m_parent = aheadOfParent;
	}
}

void GameWindowManager::unlinkWindow(GameWindow *window)
{
	if (window->m_next)
	{
		window->m_next->m_prev = window->m_prev;
	}
	else
	{
		m_windowTail = window->m_prev;
	}
	if (window->m_prev)
	{
		window->m_prev->m_next = window->m_next;
	}
	else
	{
		m_windowList = window->m_next;
	}
}

void GameWindowManager::unlinkChildWindow(GameWindow *window)
{
	if (window->m_prev)
	{
		window->m_prev->m_next = window->m_next;
		if (window->m_next)
		{
			window->m_next->m_prev = window->m_prev;
		}
	}
	else
	{
		if (window->m_next)
		{
			window->m_parent->m_child = window->m_next;
			window->m_next->m_prev = window->m_prev;
			window->m_next = nullptr;
		}
		else
		{
			window->m_parent->m_child = nullptr;
		}
	}
	window->m_parent = nullptr;
}

bool GameWindowManager::isEnabled(GameWindow *win)
{
	if (win == nullptr)
	{
		return false;
	}
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

bool GameWindowManager::isHidden(GameWindow *win)
{
	if (win == nullptr)
	{
		return true;
	}
	if (BitTest(win->m_status, WIN_STATUS_HIDDEN))
	{
		return true;
	}
	while (win->m_parent)
	{
		win = win->m_parent;
		if (BitTest(win->m_status, WIN_STATUS_HIDDEN))
		{
			return true;
		}
	}
	return false;
}

void GameWindowManager::addWindowToParent(GameWindow *window, GameWindow *parent)
{
	if (parent)
	{
		window->m_prev = nullptr;
		window->m_next = parent->m_child;
		if (parent->m_child)
		{
			parent->m_child->m_prev = window;
		}
		parent->m_child = window;
		window->m_parent = parent;
	}
}

void GameWindowManager::addWindowToParentAtEnd(GameWindow *window, GameWindow *parent)
{
	if (parent)
	{
		window->m_prev = nullptr;
		window->m_next = nullptr;
		if (parent->m_child)
		{
			GameWindow *last = parent->m_child;
			while (last->m_next != nullptr)
			{
				last = last->m_next;
			}
			last->m_next = window;
			window->m_prev = last;
		}
		else
		{
			parent->m_child = window;
		}
		window->m_parent = parent;
	}
}

void GameWindowManager::windowHiding(GameWindow *window)
{
	if (m_keyboardFocus == window)
	{
		m_keyboardFocus = nullptr;
	}
	if (!m_modal.empty() && m_modal.back() == window)
	{
		winUnsetModal(window);
	}
	if (m_mouseCaptor == window)
	{
		winCapture(nullptr);
	}
	for (GameWindow *child = window->winGetChild(); child; child = child->winGetNext())
	{
		windowHiding(child);
	}
}

void GameWindowManager::hideWindowsInRange(GameWindow *baseWindow, int first, int last, bool hideFlag)
{
	for (int i = first; i <= last; ++i)
	{
		if (GameWindow *window = winGetWindowFromId(baseWindow, i))
		{
			window->winHide(hideFlag);
		}
	}
}

void GameWindowManager::enableWindowsInRange(GameWindow *baseWindow, int first, int last, bool enableFlag)
{
	for (int i = first; i <= last; ++i)
	{
		if (GameWindow *window = winGetWindowFromId(baseWindow, i))
		{
			window->winEnable(enableFlag);
		}
	}
}

// ---- messages -----------------------------------------------------------------------------------------------------------------------------------

WindowMsgHandledType GameWindowManager::winSendSystemMsg(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	if (window == nullptr)
	{
		return MSG_IGNORED;
	}
	if (msg != GWM_DESTROY && BitTest(window->m_status, WIN_STATUS_DESTROYED))
	{
		return MSG_IGNORED;
	}
	return window->m_system ? window->m_system(window, msg, mData1, mData2) : MSG_IGNORED;
}

WindowMsgHandledType GameWindowManager::winSendInputMsg(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	if (window == nullptr)
	{
		return MSG_IGNORED;
	}
	if (msg != GWM_DESTROY && BitTest(window->m_status, WIN_STATUS_DESTROYED))
	{
		return MSG_IGNORED;
	}
	return window->m_input ? window->m_input(window, msg, mData1, mData2) : MSG_IGNORED;
}

// ---- focus, capture, modal --------------------------------------------------------------------------------------------------------------------------

int GameWindowManager::winCapture(GameWindow *window)
{
	if (m_mouseCaptor != nullptr)
	{
		return WIN_ERR_MOUSE_CAPTURED;
	}
	m_mouseCaptor = window;
	return WIN_ERR_OK;
}

int GameWindowManager::winRelease(GameWindow *window)
{
	if (window == m_mouseCaptor)
	{
		m_mouseCaptor = nullptr;
	}
	return WIN_ERR_OK;
}

int GameWindowManager::winSetFocus(GameWindow *window)
{
	bool wantsFocus = false;
	if (window && BitTest(window->winGetStatus(), WIN_STATUS_NO_FOCUS))
	{
		return 0;
	}
	if (m_keyboardFocus && m_keyboardFocus != window)
	{
		bool wf = false;
		winSendSystemMsg(m_keyboardFocus, GWM_INPUT_FOCUS, 0, msgData(&wf));
	}
	m_keyboardFocus = window;
	if (m_keyboardFocus)
	{
		for (;;)
		{
			bool wants = false;
			winSendSystemMsg(window, GWM_INPUT_FOCUS, 1, msgData(&wants));
			wantsFocus = wants;
			if (wantsFocus)
			{
				break;
			}
			window = window->winGetParent();
			if (window == nullptr)
			{
				break;
			}
		}
	}
	if (!wantsFocus)
	{
		m_keyboardFocus = nullptr;
	}
	return WIN_ERR_OK;
}

int GameWindowManager::winSetModal(GameWindow *window)
{
	if (window == nullptr)
	{
		return WIN_ERR_INVALID_WINDOW;
	}
	if (window->m_parent != nullptr)
	{
		return WIN_ERR_INVALID_PARAMETER;
	}
	m_modal.push_back(window);
	return WIN_ERR_OK;
}

int GameWindowManager::winUnsetModal(GameWindow *window)
{
	if (window == nullptr)
	{
		return WIN_ERR_INVALID_WINDOW;
	}
	if (m_modal.empty() || m_modal.back() != window)
	{
		return WIN_ERR_GENERAL_FAILURE;
	}
	m_modal.pop_back();
	return WIN_ERR_OK;
}

void GameWindowManager::winSetLoneWindow(GameWindow *window)
{
	if (m_loneWindow == window)
	{
		return;
	}
	if (m_loneWindow)
	{
		winSendSystemMsg(m_loneWindow, GGM_CLOSE, 0, 0);
	}
	m_loneWindow = window;
}

// ---- input ---------------------------------------------------------------------------------------------------------------------------------------

WinInputReturnCode GameWindowManager::winProcessKey(std::uint8_t key, std::uint8_t state)
{
	WinInputReturnCode returnCode = WIN_INPUT_NOT_USED;
	if (m_keyboardFocus && key != KEY_NONE)
	{
		GameWindow *win = m_keyboardFocus;
		returnCode = WIN_INPUT_USED;
		while (winSendInputMsg(win, GWM_CHAR, key, state) == MSG_IGNORED)
		{
			win = win->winGetParent();
			if (win == nullptr)
			{
				returnCode = WIN_INPUT_NOT_USED;
				break;
			}
		}
	}
	return returnCode;
}

WinInputReturnCode GameWindowManager::winProcessChar(char16_t ch)
{
	// ZH: WindowTranslator -> IMEManager::serviceIMEMessage sends GWM_IME_CHAR to the window with the keyboard focus
	if (!m_keyboardFocus)
	{
		return WIN_INPUT_NOT_USED;
	}
	return winSendInputMsg(m_keyboardFocus, GWM_IME_CHAR, (WindowMsgData)ch, 0) == MSG_HANDLED ? WIN_INPUT_USED : WIN_INPUT_NOT_USED;
}

namespace
{
inline bool inRegion(const GameWindow *window, const IRegion2D &r, int x, int y)
{
	(void)window;
	return x >= r.lo.x && x <= r.hi.x && y >= r.lo.y && y <= r.hi.y;
}
} // namespace

WinInputReturnCode GameWindowManager::winProcessMouseEvent(GameWindowMessage msg, ICoord2D *mousePos, void *data)
{
	WinInputReturnCode returnCode = WIN_INPUT_NOT_USED;
	GameWindow *window = nullptr;
	GameWindow *toolTipWindow = nullptr;
	bool clearGrabWindow = false;
	const std::uint32_t packed = SHORTTOLONG(mousePos->x, mousePos->y);

	if (m_mouseCaptor)
	{
		m_grabWindow = nullptr;
		window = m_mouseCaptor->winPointInChild(mousePos->x, mousePos->y);
		if (m_sendMousePosMessages || msg != GWM_MOUSE_POS)
		{
			GameWindow *win = window;
			if (win)
			{
				while (win != nullptr)
				{
					if (winSendInputMsg(win, msg, packed, 0) == MSG_HANDLED)
					{
						returnCode = WIN_INPUT_USED;
						break;
					}
					if (win == m_mouseCaptor)
					{
						break;
					}
					win = win->winGetParent();
				}
			}
			else if (winSendInputMsg(m_mouseCaptor, msg, packed, 0) == MSG_HANDLED)
			{
				returnCode = WIN_INPUT_USED;
			}
		}
	}
	else if (m_grabWindow)
	{
		switch (msg)
		{
			case GWM_LEFT_UP:
				m_grabWindow->winPointInChild(mousePos->x, mousePos->y, false, true);
				BitClear(m_grabWindow->m_status, WIN_STATUS_ACTIVE);
				if (m_grabWindow->winPointInWindow(mousePos->x, mousePos->y))
				{
					winSendInputMsg(m_grabWindow, GWM_LEFT_UP, packed, 0);
				}
				else if (BitTest(m_grabWindow->m_status, WIN_STATUS_DRAGABLE))
				{
					winSendInputMsg(m_grabWindow, GWM_LEFT_UP, packed, 0);
				}
				clearGrabWindow = true;
				break;
			case GWM_NONE:
			case GWM_LEFT_DRAG:
			{
				if (BitTest(m_grabWindow->m_status, WIN_STATUS_DRAGABLE) && data)
				{
					ICoord2D *mouseDelta = static_cast<ICoord2D *>(data);
					int dx = mouseDelta->x;
					int dy = mouseDelta->y;
					if (m_grabWindow->winGetParent())
					{
						GameWindow *parent = m_grabWindow->winGetParent();
						if (m_grabWindow->m_region.lo.x + dx < 0)
						{
							dx = 0 - m_grabWindow->m_region.lo.x;
						}
						else if (m_grabWindow->m_region.hi.x + dx > parent->m_size.x)
						{
							dx = parent->m_size.x - m_grabWindow->m_region.hi.x;
						}
						if (m_grabWindow->m_region.lo.y + dy < 0)
						{
							dy = 0 - m_grabWindow->m_region.lo.y;
						}
						else if (m_grabWindow->m_region.hi.y + dy > parent->m_size.y)
						{
							dy = parent->m_size.y - m_grabWindow->m_region.hi.y;
						}
					}
					IRegion2D newRegion;
					ICoord2D grabSize;
					m_grabWindow->winGetPosition(&newRegion.lo.x, &newRegion.lo.y);
					m_grabWindow->winGetSize(&grabSize.x, &grabSize.y);
					newRegion.lo.x += dx;
					newRegion.lo.y += dy;
					if (newRegion.lo.x < 0)
					{
						newRegion.lo.x = 0;
					}
					if (newRegion.lo.y < 0)
					{
						newRegion.lo.y = 0;
					}
					newRegion.hi.x = newRegion.lo.x + grabSize.x;
					newRegion.hi.y = newRegion.lo.y + grabSize.y;
					if (newRegion.hi.x > m_displayWidth)
					{
						newRegion.hi.x = m_displayWidth;
					}
					if (newRegion.hi.y > m_displayHeight)
					{
						newRegion.hi.y = m_displayHeight;
					}
					newRegion.lo.x = newRegion.hi.x - grabSize.x;
					newRegion.lo.y = newRegion.hi.y - grabSize.y;
					m_grabWindow->winSetPosition(newRegion.lo.x, newRegion.lo.y);
				}
				winSendInputMsg(m_grabWindow, msg, packed, 0);
				break;
			}
			default:
				break;
		}
		returnCode = WIN_INPUT_USED;
	}
	else
	{
		if (!m_modal.empty() && m_modal.back())
		{
			window = m_modal.back()->winPointInChild(mousePos->x, mousePos->y);
		}
		else
		{
			// three passes (ZH: ABOVE windows, then normal windows, then BELOW windows), each over the list from its head
			auto scan = [&](int pass) -> GameWindow * {
				for (GameWindow *w = m_windowList; w; w = w->m_next)
				{
					bool candidate;
					if (pass == 0)
					{
						candidate = BitTest(w->m_status, WIN_STATUS_ABOVE) && !BitTest(w->m_status, WIN_STATUS_HIDDEN);
					}
					else if (pass == 1)
					{
						candidate = !BitTest(w->m_status, WIN_STATUS_ABOVE | WIN_STATUS_BELOW | WIN_STATUS_HIDDEN);
					}
					else
					{
						candidate = BitTest(w->m_status, WIN_STATUS_BELOW) && !BitTest(w->m_status, WIN_STATUS_HIDDEN);
					}
					if (candidate && inRegion(w, w->m_region, mousePos->x, mousePos->y))
					{
						GameWindow *childWindow = w->winPointInAnyChild(mousePos->x, mousePos->y, true, true);
						if (toolTipWindow == nullptr && (childWindow->m_tooltip || childWindow->m_instData.getTooltipTextLength()))
						{
							toolTipWindow = childWindow;
						}
						if (BitTest(w->m_status, WIN_STATUS_ENABLED))
						{
							return w->winPointInChild(mousePos->x, mousePos->y);
						}
					}
				}
				return nullptr;
			};
			window = scan(0);
			if (window == nullptr)
			{
				window = scan(1);
			}
			if (window == nullptr)
			{
				window = scan(2);
			}
		}
		if (window && BitTest(window->m_status, WIN_STATUS_NO_INPUT))
		{
			if (window->winGetParent() && BitTest(window->winGetParent()->winGetInstanceData()->getStyle(), GWS_COMBO_BOX))
			{
				window = window->winGetParent();
			}
			else
			{
				window = nullptr;
			}
		}
		if (window)
		{
			if (m_sendMousePosMessages || msg != GWM_MOUSE_POS)
			{
				GameWindow *tempWin = window;
				GameWindow *oldLoneWindow = m_loneWindow;
				while (winSendInputMsg(tempWin, msg, packed, 0) == MSG_IGNORED)
				{
					tempWin = tempWin->m_parent;
					if (tempWin == nullptr)
					{
						break;
					}
				}
				if (m_loneWindow && m_loneWindow == oldLoneWindow && (msg == GWM_LEFT_UP || msg == GWM_MIDDLE_UP || msg == GWM_RIGHT_UP || tempWin))
				{
					if (!m_loneWindow->winIsChild(tempWin))
					{
						winSetLoneWindow(nullptr);
					}
				}
				if (tempWin)
				{
					if (msg == GWM_LEFT_DOWN)
					{
						m_grabWindow = tempWin;
					}
					returnCode = WIN_INPUT_USED;
				}
			}
		}
		if (toolTipWindow == nullptr && !isHidden(window))
		{
			toolTipWindow = window;
		}
		// tooltips of native windows: the callback runs, the text path is the APT one (S-177)
		if (toolTipWindow && toolTipWindow->m_tooltip)
		{
			toolTipWindow->m_tooltip(toolTipWindow, &toolTipWindow->m_instData, packed);
		}
	}

	if (m_grabWindow == nullptr && window != m_currMouseRgn)
	{
		if (m_mouseCaptor)
		{
			if (m_mouseCaptor->winIsChild(m_currMouseRgn))
			{
				winSendInputMsg(m_currMouseRgn, GWM_MOUSE_LEAVING, packed, 0);
			}
		}
		else if (m_currMouseRgn)
		{
			winSendInputMsg(m_currMouseRgn, GWM_MOUSE_LEAVING, packed, 0);
		}
		if (window)
		{
			winSendInputMsg(window, GWM_MOUSE_ENTERING, packed, 0);
		}
		m_currMouseRgn = window;
	}
	if (clearGrabWindow)
	{
		m_grabWindow = nullptr;
	}
	return returnCode;
}

GameWindow *GameWindowManager::getWindowUnderCursor(int x, int y, bool ignoreEnabled)
{
	if (m_mouseCaptor)
	{
		return m_mouseCaptor->winPointInChild(x, y, ignoreEnabled);
	}
	if (m_grabWindow)
	{
		return m_grabWindow->winPointInChild(x, y, ignoreEnabled);
	}
	GameWindow *window = nullptr;
	if (!m_modal.empty() && m_modal.back())
	{
		return m_modal.back()->winPointInChild(x, y, ignoreEnabled);
	}
	for (int pass = 0; pass < 3 && window == nullptr; ++pass)
	{
		for (GameWindow *w = m_windowList; w; w = w->m_next)
		{
			bool candidate;
			if (pass == 0)
			{
				candidate = BitTest(w->m_status, WIN_STATUS_ABOVE) && !BitTest(w->m_status, WIN_STATUS_HIDDEN);
			}
			else if (pass == 1)
			{
				candidate = !BitTest(w->m_status, WIN_STATUS_ABOVE | WIN_STATUS_BELOW | WIN_STATUS_HIDDEN);
			}
			else
			{
				candidate = BitTest(w->m_status, WIN_STATUS_BELOW) && !BitTest(w->m_status, WIN_STATUS_HIDDEN);
			}
			if (candidate && inRegion(w, w->m_region, x, y) && (BitTest(w->m_status, WIN_STATUS_ENABLED) || ignoreEnabled))
			{
				window = w->winPointInChild(x, y, ignoreEnabled);
				break;
			}
		}
	}
	if (window)
	{
		if (BitTest(window->m_status, WIN_STATUS_NO_INPUT))
		{
			window = nullptr;
		}
		else if (ignoreEnabled && !BitTest(window->m_status, WIN_STATUS_ENABLED))
		{
			window = nullptr;
		}
	}
	return window;
}

// ---- drawing -------------------------------------------------------------------------------------------------------------------------------------

int GameWindowManager::drawWindow(GameWindow *window)
{
	if (window == nullptr)
	{
		return WIN_ERR_INVALID_WINDOW;
	}
	if (!BitTest(window->m_status, WIN_STATUS_HIDDEN))
	{
		GameWindow *saved = m_drawWindow;
		m_drawWindow = window;
		if (!BitTest(window->m_status, WIN_STATUS_SEE_THRU) && window->m_draw)
		{
			window->m_draw(window, &window->m_instData);
		}
		if (BitTest(window->winGetStyle(), GWS_SCROLL_LISTBOX))
		{
			if (BitTest(window->m_status, WIN_STATUS_BORDER) && !BitTest(window->m_status, WIN_STATUS_SEE_THRU))
			{
				window->winDrawBorder();
			}
		}
		GameWindow *child = window->m_child;
		while (child && child->m_next)
		{
			child = child->m_next;
		}
		for (; child; child = child->m_prev)
		{
			drawWindow(child);
		}
		m_drawWindow = window;
		if (!BitTest(window->winGetStyle(), GWS_SCROLL_LISTBOX))
		{
			if (BitTest(window->m_status, WIN_STATUS_BORDER) && !BitTest(window->m_status, WIN_STATUS_SEE_THRU))
			{
				window->winDrawBorder();
			}
		}
		m_drawWindow = saved;
	}
	return WIN_ERR_OK;
}

void GameWindowManager::winRepaint(GadgetDrawList &out)
{
	m_drawList = &out;
	GameWindow *next;
	for (GameWindow *window = m_windowTail; window; window = next)
	{
		next = window->m_prev;
		if (BitTest(window->m_status, WIN_STATUS_BELOW))
		{
			drawWindow(window);
		}
	}
	for (GameWindow *window = m_windowTail; window; window = next)
	{
		next = window->m_prev;
		if (!BitTest(window->m_status, WIN_STATUS_ABOVE | WIN_STATUS_BELOW))
		{
			drawWindow(window);
		}
	}
	for (GameWindow *window = m_windowTail; window; window = next)
	{
		next = window->m_prev;
		if (BitTest(window->m_status, WIN_STATUS_ABOVE))
		{
			drawWindow(window);
		}
	}
	m_drawList = nullptr;
}

namespace
{
std::string drawerName(GameWindow *w)
{
	if (!w)
	{
		return std::string();
	}
	if (!w->aptInstanceName().empty())
	{
		return w->aptInstanceName();
	}
	return w->name();
}
} // namespace

bool GameWindowManager::imageSize(const std::string &image, int &width, int &height)
{
	if (m_images)
	{
		if (const Image *img = m_images->findImageByName(image))
		{
			width = img->getImageWidth();
			height = img->getImageHeight();
			return true;
		}
	}
	if (m_drawList)
	{
		m_drawList->noteUnresolved(image);
	}
	width = height = 0;
	return false;
}

void GameWindowManager::winDrawImage(const std::string &image, int startX, int startY, int endX, int endY, Color color)
{
	if (!m_drawList || image.empty())
	{
		return;
	}
	if (m_images && !m_images->findImageByName(image))
	{
		m_drawList->noteUnresolved(image);
	}
	GadgetDrawCommand c;
	c.kind = GadgetDrawCommand::Kind::Image;
	c.window = drawerName(m_drawWindow);
	c.x0 = startX;
	c.y0 = startY;
	c.x1 = endX;
	c.y1 = endY;
	c.color = color;
	c.image = image;
	m_drawList->commands.push_back(std::move(c));
}

void GameWindowManager::winFillRect(Color color, float width, int startX, int startY, int endX, int endY)
{
	if (!m_drawList)
	{
		return;
	}
	GadgetDrawCommand c;
	c.kind = GadgetDrawCommand::Kind::FillRect;
	c.window = drawerName(m_drawWindow);
	c.x0 = startX;
	c.y0 = startY;
	c.x1 = endX;
	c.y1 = endY;
	c.color = color;
	c.lineWidth = (int)width;
	m_drawList->commands.push_back(std::move(c));
}

void GameWindowManager::winOpenRect(Color color, float width, int startX, int startY, int endX, int endY)
{
	if (!m_drawList)
	{
		return;
	}
	GadgetDrawCommand c;
	c.kind = GadgetDrawCommand::Kind::OpenRect;
	c.window = drawerName(m_drawWindow);
	c.x0 = startX;
	c.y0 = startY;
	c.x1 = endX;
	c.y1 = endY;
	c.color = color;
	c.lineWidth = (int)width;
	m_drawList->commands.push_back(std::move(c));
}

void GameWindowManager::winDrawLine(Color color, float width, int startX, int startY, int endX, int endY)
{
	if (!m_drawList)
	{
		return;
	}
	GadgetDrawCommand c;
	c.kind = GadgetDrawCommand::Kind::Line;
	c.window = drawerName(m_drawWindow);
	c.x0 = startX;
	c.y0 = startY;
	c.x1 = endX;
	c.y1 = endY;
	c.color = color;
	c.lineWidth = (int)width;
	m_drawList->commands.push_back(std::move(c));
}

void GameWindowManager::winDrawText(const UnicodeString &text, GameFont *font, int x, int y, Color color, Color dropColor, int wrapWidth, bool wrapCentered)
{
	if (!m_drawList || text.empty())
	{
		return;
	}
	GadgetDrawCommand c;
	c.kind = GadgetDrawCommand::Kind::Text;
	c.window = drawerName(m_drawWindow);
	c.x0 = x;
	c.y0 = y;
	c.color = color;
	c.dropColor = dropColor;
	c.text = text;
	if (font)
	{
		c.font = *font;
	}
	c.wrapWidth = wrapWidth;
	c.wrapCentered = wrapCentered;
	m_drawList->commands.push_back(std::move(c));
}

void GameWindowManager::setClipRegion(const IRegion2D &region)
{
	if (!m_drawList)
	{
		return;
	}
	GadgetDrawCommand c;
	c.kind = GadgetDrawCommand::Kind::ClipBegin;
	c.window = drawerName(m_drawWindow);
	c.x0 = region.lo.x;
	c.y0 = region.lo.y;
	c.x1 = region.hi.x;
	c.y1 = region.hi.y;
	m_drawList->commands.push_back(std::move(c));
}

void GameWindowManager::enableClipping(bool on)
{
	if (!m_drawList || on)
	{
		return;
	}
	GadgetDrawCommand c;
	c.kind = GadgetDrawCommand::Kind::ClipEnd;
	c.window = drawerName(m_drawWindow);
	m_drawList->commands.push_back(std::move(c));
}

// ---- gadget creation ----------------------------------------------------------------------------------------------------------------------------

void GameWindowManager::assignDefaultGadgetLook(GameWindow *gadget, GameFont *defaultFont, bool assignVisual)
{
	(void)assignVisual; // [S-177] see the header
	if (gadget == nullptr)
	{
		return;
	}
	if (defaultFont)
	{
		gadget->winSetFont(defaultFont);
	}
	else
	{
		gadget->winSetFont(winFindFont("Times New Roman", 14, false));
	}
}

GameWindow *GameWindowManager::gogoGadgetPushButton(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, GameFont *defaultFont, bool defaultVisual)
{
	if (!BitTest(instData->getStyle(), GWS_PUSH_BUTTON))
	{
		note("gogoGadgetPushButton: the instance data is not of the button style");
		return nullptr;
	}
	GameWindow *button = winCreate(parent, status, x, y, width, height, GadgetPushButtonSystem, instData);
	if (button == nullptr)
	{
		return nullptr;
	}
	button->winSetInputFunc(GadgetPushButtonInput);
	if (BitTest(button->winGetStatus(), WIN_STATUS_IMAGE))
	{
		button->winSetDrawFunc(W3DGadgetPushButtonImageDraw);
	}
	else
	{
		button->winSetDrawFunc(W3DGadgetPushButtonDraw);
	}
	button->winSetOwner(parent);
	button->winSetUserData(nullptr);
	assignDefaultGadgetLook(button, defaultFont, defaultVisual);
	const UnicodeString text = winTextLabelToText(instData->m_textLabelString);
	if (!text.empty())
	{
		GadgetButtonSetText(button, text);
	}
	return button;
}

GameWindow *GameWindowManager::gogoGadgetCheckbox(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, GameFont *defaultFont, bool defaultVisual)
{
	if (!BitTest(instData->getStyle(), GWS_CHECK_BOX))
	{
		note("gogoGadgetCheckbox: the instance data is not of the check box style");
		return nullptr;
	}
	GameWindow *checkbox = winCreate(parent, status, x, y, width, height, GadgetCheckBoxSystem, instData);
	if (checkbox == nullptr)
	{
		return nullptr;
	}
	checkbox->winSetInputFunc(GadgetCheckBoxInput);
	if (BitTest(checkbox->winGetStatus(), WIN_STATUS_IMAGE))
	{
		checkbox->winSetDrawFunc(W3DGadgetCheckBoxImageDraw);
	}
	else
	{
		checkbox->winSetDrawFunc(W3DGadgetCheckBoxDraw);
	}
	checkbox->winSetOwner(parent);
	assignDefaultGadgetLook(checkbox, defaultFont, defaultVisual);
	const UnicodeString text = winTextLabelToText(instData->m_textLabelString);
	if (!text.empty())
	{
		GadgetCheckBoxSetText(checkbox, text);
	}
	return checkbox;
}

GameWindow *GameWindowManager::gogoGadgetListBox(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, ListboxData *listboxDataTemplate, GameFont *defaultFont, bool defaultVisual)
{
	if (!BitTest(instData->getStyle(), GWS_SCROLL_LISTBOX))
	{
		note("gogoGadgetListBox: the instance data is not of the list box style");
		return nullptr;
	}
	GameWindow *listbox = winCreate(parent, status, x, y, width, height, GadgetListBoxSystem, instData);
	if (listbox == nullptr)
	{
		return nullptr;
	}
	ListboxData *listboxData = new ListboxData(*listboxDataTemplate);
	listboxData->upButton = listboxData->downButton = listboxData->slider = nullptr;
	listbox->winSetUserData(listboxData);
	listbox->winSetOwner(parent);
	bool title = instData->getTextLength() != 0;
	if (BitTest(listbox->winGetStatus(), WIN_STATUS_IMAGE))
	{
		listbox->winSetDrawFunc(W3DGadgetListBoxImageDraw);
	}
	else
	{
		listbox->winSetDrawFunc(W3DGadgetListBoxDraw);
	}
	// ZH installs GadgetListBoxMultiInput for multiSelect list boxes: not ported (no APT list box is multi select) [S-177]
	listbox->winSetInputFunc(GadgetListBoxInput);
	if (listboxData->multiSelect)
	{
		note("gogoGadgetListBox: multi select list boxes use the single select input (GadgetListBoxMultiInput is not ported)");
	}
	const int length = listboxData->listLength;
	listboxData->listLength = 0; // ZH "hacky!": SetListLength sees an empty list
	listboxData->listData.clear();
	GadgetListBoxSetListLength(listbox, length);
	listboxData->displayHeight = (short)height;
	if (title)
	{
		listboxData->displayHeight = (short)(listboxData->displayHeight - winFontHeight(instData->getFont()));
	}
	listboxData->displayPos = 0;
	listboxData->selectPos = -1;
	listboxData->doubleClickTime = 0;
	listboxData->insertPos = 0;
	listboxData->endPos = 0;
	listboxData->totalHeight = 0;
	if (listboxData->scrollBar)
	{
		GadgetListboxCreateScrollbar(listbox);
	}
	if (listboxData->columns == 1)
	{
		listboxData->columnWidth.assign(1, width);
		if (listboxData->slider)
		{
			int sw, sh;
			listboxData->slider->winGetSize(&sw, &sh);
			listboxData->columnWidth[0] -= (sw + 2);
		}
	}
	else
	{
		if (listboxData->columnWidthPercentage.empty())
		{
			winDestroy(listbox);
			return nullptr;
		}
		listboxData->columnWidth.assign((std::size_t)listboxData->columns, 0);
		int totalWidth = width;
		if (listboxData->slider)
		{
			int sw, sh;
			listboxData->slider->winGetSize(&sw, &sh);
			totalWidth -= (sw + 2);
		}
		for (int i = 0; i < listboxData->columns; ++i)
		{
			listboxData->columnWidth[(std::size_t)i] = listboxData->columnWidthPercentage[(std::size_t)i] * totalWidth / 100;
		}
	}
	assignDefaultGadgetLook(listbox, defaultFont, defaultVisual);
	return listbox;
}

GameWindow *GameWindowManager::gogoGadgetSlider(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, SliderData *sliderData, GameFont *defaultFont, bool defaultVisual)
{
	GameWindow *slider;
	BitSet(status, WIN_STATUS_TAB_STOP);
	if (BitTest(instData->getStyle(), GWS_HORZ_SLIDER))
	{
		slider = winCreate(parent, status, x, y, width, height, GadgetHorizontalSliderSystem, instData);
		if (slider == nullptr)
		{
			return nullptr;
		}
		slider->winSetInputFunc(GadgetHorizontalSliderInput);
		if (BitTest(slider->winGetStatus(), WIN_STATUS_IMAGE))
		{
			slider->winSetDrawFunc(W3DGadgetHorizontalSliderImageDraw);
		}
		else
		{
			slider->winSetDrawFunc(W3DGadgetHorizontalSliderDraw);
		}
	}
	else if (BitTest(instData->getStyle(), GWS_VERT_SLIDER))
	{
		slider = winCreate(parent, status, x, y, width, height, GadgetVerticalSliderSystem, instData);
		if (slider == nullptr)
		{
			return nullptr;
		}
		slider->winSetInputFunc(GadgetVerticalSliderInput);
		if (BitTest(slider->winGetStatus(), WIN_STATUS_IMAGE) && !(parent && BitTest(parent->winGetStyle(), GWS_SCROLL_LISTBOX)))
		{
			slider->winSetDrawFunc(W3DGadgetVerticalSliderImageDraw);
		}
		else
		{
			slider->winSetDrawFunc(W3DGadgetVerticalSliderDraw);
		}
	}
	else
	{
		note("gogoGadgetSlider: unrecognized slider style");
		return nullptr;
	}
	slider->winSetOwner(parent);

	WinInstanceData buttonInstData;
	std::uint32_t statusFlags = status | WIN_STATUS_ENABLED | WIN_STATUS_DRAGABLE;
	buttonInstData.init();
	BitClear(statusFlags, WIN_STATUS_HIDDEN);
	buttonInstData.m_owner = slider;
	buttonInstData.m_style = GWS_PUSH_BUTTON;
	if (BitTest(instData->getStyle(), GWS_MOUSE_TRACK))
	{
		BitSet(buttonInstData.m_style, GWS_MOUSE_TRACK);
	}
	if (BitTest(instData->getStyle(), GWS_HORZ_SLIDER))
	{
		gogoGadgetPushButton(slider, statusFlags, 0, HORIZONTAL_SLIDER_THUMB_POSITION, HORIZONTAL_SLIDER_THUMB_WIDTH, HORIZONTAL_SLIDER_THUMB_HEIGHT, &buttonInstData, nullptr, true);
	}
	else
	{
		gogoGadgetPushButton(slider, statusFlags, 0, 0, width, width + 1, &buttonInstData, nullptr, true);
	}
	if (sliderData->maxVal == sliderData->minVal)
	{
		sliderData->maxVal = sliderData->minVal + 1;
	}
	if (BitTest(instData->getStyle(), GWS_HORZ_SLIDER))
	{
		sliderData->numTicks = (float)(width - HORIZONTAL_SLIDER_THUMB_WIDTH) / (float)(sliderData->maxVal - sliderData->minVal);
	}
	else
	{
		sliderData->numTicks = (float)(height - GADGET_SIZE) / (float)(sliderData->maxVal - sliderData->minVal);
	}
	slider->winSetUserData(new SliderData(*sliderData));
	assignDefaultGadgetLook(slider, defaultFont, defaultVisual);
	return slider;
}

GameWindow *GameWindowManager::gogoGadgetComboBox(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, ComboBoxData *comboBoxDataTemplate, GameFont *defaultFont, bool defaultVisual)
{
	if (!BitTest(instData->getStyle(), GWS_COMBO_BOX))
	{
		note("gogoGadgetComboBox: the instance data is not of the combo box style");
		return nullptr;
	}
	GameWindow *comboBox = winCreate(parent, status, x, y, width, height, GadgetComboBoxSystem, instData);
	if (comboBox == nullptr)
	{
		return nullptr;
	}
	ComboBoxData *comboBoxData = new ComboBoxData(*comboBoxDataTemplate);
	comboBoxData->dropDownButton = comboBoxData->editBox = comboBoxData->listBox = nullptr;
	comboBox->winSetUserData(comboBoxData);
	comboBox->winSetOwner(parent);
	bool title = instData->getTextLength() != 0;
	if (BitTest(comboBox->winGetStatus(), WIN_STATUS_IMAGE))
	{
		comboBox->winSetDrawFunc(W3DGadgetComboBoxImageDraw);
	}
	else
	{
		comboBox->winSetDrawFunc(W3DGadgetComboBoxDraw);
	}
	comboBox->winSetInputFunc(GadgetComboBoxInput);

	WinInstanceData winInstData;
	const int buttonWidth = 21;
	if (comboBox->winGetTextLength())
	{
		title = true;
	}
	(void)title;
	status &= ~(WIN_STATUS_BORDER | WIN_STATUS_HIDDEN);
	winInstData.init();
	winInstData.m_owner = comboBox;
	winInstData.m_style = GWS_PUSH_BUTTON;
	if (BitTest(comboBox->winGetStyle(), GWS_MOUSE_TRACK))
	{
		BitSet(winInstData.m_style, GWS_MOUSE_TRACK);
	}
	comboBoxData->dropDownButton = gogoGadgetPushButton(comboBox, status | WIN_STATUS_ACTIVE | WIN_STATUS_ENABLED, width - buttonWidth, 0, buttonWidth, height, &winInstData, nullptr, true);
	comboBoxData->dropDownButton->winSetTooltipFunc(comboBox->winGetTooltipFunc());
	comboBoxData->dropDownButton->winSetTooltip(instData->getTooltipText());
	comboBoxData->dropDownButton->setTooltipDelay(comboBox->getTooltipDelay());

	std::uint32_t statusTextEntry;
	winInstData.init();
	winInstData.m_owner = comboBox;
	winInstData.m_style |= GWS_ENTRY_FIELD;
	winInstData.m_textLabelString = "Entry";
	if (BitTest(comboBox->winGetStyle(), GWS_MOUSE_TRACK))
	{
		BitSet(winInstData.m_style, GWS_MOUSE_TRACK);
	}
	EntryData entryData = comboBoxData->entryTemplate;
	if (comboBoxData->isEditable)
	{
		statusTextEntry = status;
	}
	else
	{
		statusTextEntry = status | WIN_STATUS_NO_INPUT;
		entryData.drawTextFromStart = true;
	}
	comboBoxData->editBox = gogoGadgetTextEntry(comboBox, statusTextEntry, 0, 0, width - buttonWidth, height, &winInstData, &entryData, winInstData.m_font, false);
	comboBoxData->editBox->winSetTooltipFunc(comboBox->winGetTooltipFunc());
	comboBoxData->editBox->winSetTooltip(instData->getTooltipText());
	comboBoxData->editBox->setTooltipDelay(comboBox->getTooltipDelay());

	winInstData.init();
	winInstData.m_owner = comboBox;
	if (BitTest(comboBox->winGetStyle(), GWS_MOUSE_TRACK))
	{
		BitSet(winInstData.m_style, GWS_MOUSE_TRACK);
	}
	// ZH: BitSet( winInstData.m_style, WIN_STATUS_HIDDEN ) - a status bit stored into the style word (0x10 = GWS_HORZ_SLIDER); kept as is
	// because the style word of the list box is then GWS_HORZ_SLIDER | GWS_SCROLL_LISTBOX in retail too
	BitSet(winInstData.m_style, WIN_STATUS_HIDDEN);
	winInstData.m_style |= GWS_SCROLL_LISTBOX;
	status &= ~(WIN_STATUS_IMAGE);
	ListboxData listTemplate = comboBoxData->listboxTemplate;
	comboBoxData->listBox = gogoGadgetListBox(comboBox, status | WIN_STATUS_ABOVE | WIN_STATUS_ONE_LINE, 0, height, width, height, &winInstData, &listTemplate, winInstData.m_font, false);
	comboBoxData->listBox->winHide(true);
	comboBoxData->listBox->winSetTooltipFunc(comboBox->winGetTooltipFunc());
	comboBoxData->listBox->winSetTooltip(instData->getTooltipText());
	comboBoxData->listBox->setTooltipDelay(comboBox->getTooltipDelay());
	GadgetListBoxSetAudioFeedback(comboBoxData->listBox, true);
	GadgetComboBoxSetIsEditable(comboBox, comboBoxData->isEditable);
	GadgetComboBoxSetMaxChars(comboBox, comboBoxData->maxChars);
	GadgetComboBoxSetMaxDisplay(comboBox, comboBoxData->maxDisplay);

	Color color = comboBox->winGetEnabledTextColor();
	Color border = comboBox->winGetEnabledTextBorderColor();
	comboBoxData->listBox->winSetEnabledTextColors(color, border);
	comboBoxData->editBox->winSetEnabledTextColors(color, border);
	color = comboBox->winGetDisabledTextColor();
	border = comboBox->winGetDisabledTextBorderColor();
	comboBoxData->listBox->winSetDisabledTextColors(color, border);
	comboBoxData->editBox->winSetDisabledTextColors(color, border);
	color = comboBox->winGetHiliteTextColor();
	border = comboBox->winGetHiliteTextBorderColor();
	comboBoxData->listBox->winSetHiliteTextColors(color, border);
	comboBoxData->editBox->winSetHiliteTextColors(color, border);
	comboBoxData->dontHide = false;
	assignDefaultGadgetLook(comboBox, defaultFont, defaultVisual);
	return comboBox;
}

GameWindow *GameWindowManager::gogoGadgetTextEntry(GameWindow *parent, std::uint32_t status, int x, int y, int width, int height, WinInstanceData *instData, EntryData *entryData, GameFont *defaultFont, bool defaultVisual)
{
	if (!BitTest(instData->getStyle(), GWS_ENTRY_FIELD))
	{
		note("gogoGadgetTextEntry: the style is not of the entry field style");
		return nullptr;
	}
	GameWindow *entry = winCreate(parent, status, x, y, width, height, GadgetTextEntrySystem, instData);
	if (entry == nullptr)
	{
		return nullptr;
	}
	entry->winSetOwner(parent);
	entry->winSetInputFunc(GadgetTextEntryInput);
	if (BitTest(entry->winGetStatus(), WIN_STATUS_IMAGE))
	{
		entry->winSetDrawFunc(W3DGadgetTextEntryImageDraw);
	}
	else
	{
		entry->winSetDrawFunc(W3DGadgetTextEntryDraw);
	}
	entryData->charPos = (short)entryData->text.size();
	entryData->conCharPos = 0;
	entryData->receivedUnichar = false;
	if (entryData->maxTextLen >= ENTRY_TEXT_LEN)
	{
		entryData->maxTextLen = ENTRY_TEXT_LEN;
	}
	EntryData *data = new EntryData(*entryData);
	data->constructText.clear();
	data->constructList = nullptr; // [S-177] the IME candidate list (Korean / Japanese builds only) is not created
	entry->winSetUserData(data);
	assignDefaultGadgetLook(entry, defaultFont, defaultVisual);
	const UnicodeString text = winTextLabelToText(instData->m_textLabelString);
	if (!text.empty())
	{
		GadgetTextEntrySetText(entry, text);
	}
	return entry;
}
