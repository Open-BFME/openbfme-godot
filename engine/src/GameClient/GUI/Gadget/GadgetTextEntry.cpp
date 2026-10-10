// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// Port of ZH GameClient/GUI/Gadget/GadgetTextEntry.cpp and GameEngineDevice W3DTextEntry.cpp.
//
// Text arrives as GWM_IME_CHAR (ZH's IMEManager translates the keyboard into it); GWM_CHAR carries the editing keys.  Not ported (stop
// S-177): the IME composition (GWM_IME_STRING, the candidate list), the caret blink counter of ZH (the caret blinks on the window
// manager's clock: a presentation detail with no effect on state).

#include "GameClient/GUI/Gadgets.h"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace
{
EntryData *entryOf(GameWindow *w) { return static_cast<EntryData *>(w->winGetUserData()); }

bool winIsDigit(char16_t c) { return c < 0x80 && std::isdigit((int)c); }
bool winIsAlNum(char16_t c) { return c < 0x80 && std::isalnum((int)c); }
bool winIsAscii(char16_t c) { return c < 0x80; }
} // namespace

std::uint32_t GadgetTextEntryValidationFlags(const EntryData &e)
{
	// the EntryData flags as the validator's bits (RW 0x75E4DF: 0x10 ASCII, 0x20 digits, 0x40 letters and digits). INFERENCE (stop S-1409): the
	// other bits (0x01 no space, 0x02 no '%', 0x04 printable ASCII without '\\', 0x08 no file-name characters, 0x80 space allowed) have no setter
	// ported here, so they are never set
	return (e.aSCIIOnly ? 0x10u : 0u) | (e.numericalOnly ? 0x20u : 0u) | (e.alphaNumericalOnly ? 0x40u : 0u);
}

bool GadgetTextEntryValidateCharacter(char16_t character, std::uint32_t flags)
{
	// RW 0x75E4DF (BFME2 decomp GadgetTextEntryValidateCharacter.cpp, tier A byte-matched): the Thai blocks U+0E01..U+0E3A and U+0E3F..U+0E5B are
	// refused whatever the flags, then the flag tests in retail order (the low byte of the flags only)
	const std::uint32_t c = character;
	const std::uint8_t f = (std::uint8_t)flags;
	if ((c >= 0xE01 && c <= 0xE3A) || (c >= 0xE3F && c <= 0xE5B))
	{
		return false;
	}
	if ((f & 0x80) && c == 0x20)
	{
		return true;
	}
	if ((f & 0x01) && c == 0x20)
	{
		return false;
	}
	if ((f & 0x02) && c == 0x25)
	{
		return false;
	}
	if ((f & 0x04) && (c < 0x22 || c > 0x7E || c == 0x5C))
	{
		return false;
	}
	if ((f & 0x08) && (c == 0x2A || c == 0x3F || c == 0x3A || c == 0x5C || c == 0x2F || c == 0x22 || c == 0x3C || c == 0x3E || c == 0x7C))
	{
		return false;
	}
	// iswascii / iswdigit / iswalnum of MSVCRT: INFERENCE (S-1409) for characters above 0x7F, which the port treats as neither digit nor letter
	if ((f & 0x10) && !winIsAscii(character))
	{
		return false;
	}
	if ((f & 0x20) && !winIsDigit(character))
	{
		return false;
	}
	if ((f & 0x40) && !winIsAlNum(character))
	{
		return false;
	}
	return true;
}

bool GadgetTextEntryInsertCharacter(GameWindow *window, char16_t character)
{
	// RW 0x72260B (BFME2 decomp GadgetTextEntryInsertCharacter.cpp, tier A byte-matched): validated, refused when the text already holds maxTextLen
	// characters, else inserted at the cursor, the cursor one further, a '*' appended to the secret text and the entry drawn from its start
	EntryData *e = entryOf(window);
	if (!e || !GadgetTextEntryValidateCharacter(character, GadgetTextEntryValidationFlags(*e)))
	{
		return false;
	}
	if ((int)e->text.size() >= (int)e->maxTextLen)
	{
		return false;
	}
	const std::size_t at = std::min<std::size_t>((std::size_t)std::max<int>(0, e->charPos), e->text.size());
	e->text.insert(e->text.begin() + (std::ptrdiff_t)at, character);
	e->charPos = (short)(at + 1);
	// RotWK's conCharPos follows the cursor here; this port's conCharPos is the IME composition's length (never driven, S-177): left at 0
	e->sText.push_back(u'*');
	e->drawTextFromStart = true;
	return true;
}

WindowMsgHandledType GadgetTextEntryInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	EntryData *e = entryOf(window);
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindowManager &mgr = window->manager();
	switch (msg)
	{
		case GWM_IME_CHAR:
		{
			const char16_t ch = (char16_t)mData1;
			if (ch == u'\r')
			{
				mgr.winSendSystemMsg(window->winGetOwner(), GEM_EDIT_DONE, msgData(window), 0);
				return MSG_HANDLED;
			}
			if (ch)
			{
				// lane CAH-2 r2: RotWK's insert (RW 0x72260B) and validator (RW 0x75E4DF) instead of ZH's "charPos < maxTextLen - 1", which kept an
				// entry one character short of its limit (the Create-a-Hero name: 21 of RW 0x9C3F2A's 22)
				if (GadgetTextEntryInsertCharacter(window, ch))
				{
					mgr.winSendSystemMsg(window->winGetOwner(), GEM_UPDATE_TEXT, msgData(window), 0);
				}
			}
			break;
		}
		case GWM_CHAR:
			if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN) && BitTest((std::uint32_t)mData2, KEY_STATE_ALT | KEY_STATE_CONTROL))
			{
				return MSG_IGNORED;
			}
			switch (mData1)
			{
				case KEY_ESC:
				case KEY_PGUP:
				case KEY_PGDN:
				case KEY_HOME:
				case KEY_END:
				case KEY_F1:
				case KEY_F2:
				case KEY_F3:
				case KEY_F4:
				case KEY_F5:
				case KEY_F6:
				case KEY_F7:
				case KEY_F8:
				case KEY_F9:
				case KEY_F10:
				case KEY_F11:
				case KEY_F12:
				case KEY_CAPS:
				case KEY_DEL:
					return MSG_IGNORED;
				case KEY_DOWN:
				case KEY_RIGHT:
				case KEY_TAB:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						GameWindow *parent = window->winGetParent();
						if (parent && !BitTest(parent->winGetStyle(), GWS_COMBO_BOX))
						{
							parent = nullptr;
						}
						mgr.winNextTab(parent ? parent : window);
					}
					break;
				case KEY_UP:
				case KEY_LEFT:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						GameWindow *parent = window->winGetParent();
						if (parent && !BitTest(parent->winGetStyle(), GWS_COMBO_BOX))
						{
							parent = nullptr;
						}
						mgr.winPrevTab(parent ? parent : window);
					}
					break;
				case KEY_BACKSPACE:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						if (e->conCharPos == 0 && e->charPos > 0)
						{
							e->text.pop_back();
							if (!e->sText.empty())
							{
								e->sText.pop_back();
							}
							e->charPos--;
							mgr.winSendSystemMsg(window->winGetOwner(), GEM_UPDATE_TEXT, msgData(window), 0);
						}
					}
					break;
				default:
					break; // ZH: the switch has no default; the message is reported handled below
			}
			break;
		case GWM_LEFT_DOWN:
			BitSet(instData->m_state, WIN_STATE_HILITED);
			mgr.winSetFocus(window);
			break;
		case GWM_MOUSE_ENTERING:
			if (BitTest(instData->getStyle(), GWS_MOUSE_TRACK))
			{
				BitSet(instData->m_state, WIN_STATE_HILITED);
				mgr.winSendSystemMsg(window->winGetOwner(), GBM_MOUSE_ENTERING, msgData(window), 0);
			}
			break;
		case GWM_MOUSE_LEAVING:
			if (BitTest(instData->getStyle(), GWS_MOUSE_TRACK))
			{
				BitClear(instData->m_state, WIN_STATE_HILITED);
				mgr.winSendSystemMsg(window->winGetOwner(), GBM_MOUSE_LEAVING, msgData(window), 0);
			}
			break;
		case GWM_LEFT_DRAG:
			if (BitTest(instData->getStyle(), GWS_MOUSE_TRACK))
			{
				mgr.winSendSystemMsg(window->winGetOwner(), GGM_LEFT_DRAG, msgData(window), 0);
			}
			break;
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

WindowMsgHandledType GadgetTextEntrySystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	EntryData *e = entryOf(window);
	WinInstanceData *instData = window->winGetInstanceData();
	switch (msg)
	{
		case GEM_GET_TEXT:
			*msgPtr<UnicodeString>(mData2) = e->text;
			break;
		case GEM_SET_TEXT:
		{
			const UnicodeString &ustr = *msgPtr<const UnicodeString>(mData1);
			e->text = ustr;
			e->charPos = (short)ustr.size();
			e->constructText.clear();
			e->conCharPos = 0;
			e->sText.assign(ustr.size(), u'*');
			break;
		}
		case GWM_CREATE:
			break;
		case GWM_DESTROY:
			delete e;
			window->winSetUserData(nullptr);
			break;
		case GWM_INPUT_FOCUS:
			if (mData1 == 0)
			{
				BitClear(instData->m_state, WIN_STATE_SELECTED);
				BitClear(instData->m_state, WIN_STATE_HILITED);
				e->constructText.clear();
				e->conCharPos = 0;
			}
			else
			{
				BitSet(instData->m_state, WIN_STATE_SELECTED);
				BitSet(instData->m_state, WIN_STATE_HILITED);
			}
			window->manager().winSendSystemMsg(window->winGetOwner(), GGM_FOCUS_CHANGE, mData1, (WindowMsgData)window->winGetWindowId());
			*msgPtr<bool>(mData2) = true;
			break;
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

UnicodeString GadgetTextEntryGetText(GameWindow *textentry)
{
	if (textentry == nullptr || !BitTest(textentry->winGetStyle(), GWS_ENTRY_FIELD))
	{
		return UnicodeString();
	}
	UnicodeString result;
	textentry->manager().winSendSystemMsg(textentry, GEM_GET_TEXT, 0, msgData(&result));
	return result;
}

void GadgetTextEntrySetText(GameWindow *g, const UnicodeString &text)
{
	if (g == nullptr)
	{
		return;
	}
	UnicodeString copy = text;
	g->manager().winSendSystemMsg(g, GEM_SET_TEXT, msgData(&copy), 0);
}

static Color darkenColor(Color color, int percent)
{
	// ZH GameDarkenColor
	if (percent >= 90 || percent <= 0)
	{
		return color;
	}
	int r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
	const int a = (color >> 24) & 0xFF;
	r -= (r * percent / 100);
	g -= (g * percent / 100);
	b -= (b * percent / 100);
	return GameMakeColor(r, g, b, a);
}

void GadgetTextEntrySetTextColor(GameWindow *g, Color color)
{
	const Color back = g->winGetEnabledTextBorderColor();
	g->winSetEnabledTextColors(color, back);
	g->winSetDisabledTextColors(darkenColor(color, 25), back);
}

// ---- draw (W3DTextEntry.cpp) -------------------------------------------------------------------------------------------------------------------

static void drawTextEntryText(GameWindow *window, WinInstanceData *instData, Color textColor, Color textDropColor, int x, int y, int width, int fontHeight)
{
	EntryData *e = entryOf(window);
	GameWindowManager &mgr = window->manager();
	(void)instData;
	if (textColor == WIN_COLOR_UNDEFINED)
	{
		return;
	}
	const UnicodeString &text = e->secretText ? e->sText : e->text;
	GameFont *font = window->winGetFont();
	const int textWidth = mgr.winTextWidth(font, text);
	ICoord2D origin, size;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	IRegion2D clipRegion;
	int cursorPos;
	if (!e->drawTextFromStart)
	{
		clipRegion.lo.x = x;
		clipRegion.hi.x = x + width;
		clipRegion.lo.y = y;
		clipRegion.hi.y = y + fontHeight;
		mgr.setClipRegion(clipRegion);
		x += 2;
		if (textWidth < width)
		{
			mgr.winDrawText(text, font, x, y, textColor, textDropColor);
			cursorPos = textWidth + x;
		}
		else
		{
			const int div = textWidth / (width / 2) - 1;
			mgr.winDrawText(text, font, x - (div * (width / 2)), y, textColor, textDropColor);
			cursorPos = textWidth - (div * (width / 2)) + x;
		}
	}
	else
	{
		clipRegion.lo.x = origin.x;
		clipRegion.hi.x = origin.x + size.x;
		clipRegion.lo.y = origin.y;
		clipRegion.hi.y = origin.y + size.y;
		mgr.setClipRegion(clipRegion);
		x += 5;
		mgr.winDrawText(text, font, x, y, textColor, textDropColor);
		cursorPos = textWidth + x;
	}
	mgr.enableClipping(false);
	GameWindow *parent = window->winGetParent();
	if (parent && !BitTest(parent->winGetStyle(), GWS_COMBO_BOX))
	{
		parent = nullptr;
	}
	if ((window == mgr.winGetFocus() || (parent && parent == mgr.winGetFocus())) && ((mgr.timeMs() / 266) & 1))
	{
		mgr.winFillRect(textColor, WIN_DRAW_LINE_WIDTH, cursorPos, origin.y + 3, cursorPos + 2, origin.y + size.y - 3);
	}
	window->winSetCursorPosition(cursorPos + 2 - origin.x, 0);
}

static void drawTextEntryBody(GameWindow *window, WinInstanceData *instData, Color textColor, Color textBorder)
{
	GameWindowManager &mgr = window->manager();
	ICoord2D origin, size;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	const int fontHeight = mgr.winFontHeight(instData->getFont());
	const int startOffset = 5;
	const int width = size.x - (2 * startOffset);
	const int startX = origin.x + startOffset;
	int startY;
	if (BitTest(window->winGetStatus(), WIN_STATUS_ONE_LINE))
	{
		startY = size.y / 2 - fontHeight / 2; // ZH: no origin.y added (kept)
	}
	else
	{
		startY = origin.y + startOffset;
	}
	drawTextEntryText(window, instData, textColor, textBorder, startX, startY, width, fontHeight);
}

void W3DGadgetTextEntryDraw(GameWindow *window, WinInstanceData *instData)
{
	EntryData *e = entryOf(window);
	e->receivedUnichar = false;
	GameWindowManager &mgr = window->manager();
	ICoord2D origin, size;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	Color backBorder, backColor, textColor, textBorder;
	if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
	{
		textColor = window->winGetDisabledTextColor();
		textBorder = window->winGetDisabledTextBorderColor();
		backColor = window->winGetDisabledColor(0);
		backBorder = window->winGetDisabledBorderColor(0);
	}
	else if (BitTest(instData->getState(), WIN_STATE_HILITED))
	{
		textColor = window->winGetHiliteTextColor();
		textBorder = window->winGetHiliteTextBorderColor();
		backColor = window->winGetHiliteColor(0);
		backBorder = window->winGetHiliteBorderColor(0);
	}
	else
	{
		textColor = window->winGetEnabledTextColor();
		textBorder = window->winGetEnabledTextBorderColor();
		backColor = window->winGetEnabledColor(0);
		backBorder = window->winGetEnabledBorderColor(0);
	}
	if (backBorder != WIN_COLOR_UNDEFINED)
	{
		mgr.winOpenRect(backBorder, WIN_DRAW_LINE_WIDTH, origin.x, origin.y, origin.x + size.x, origin.y + size.y);
	}
	if (backColor != WIN_COLOR_UNDEFINED)
	{
		mgr.winFillRect(backColor, WIN_DRAW_LINE_WIDTH, origin.x + 1, origin.y + 1, origin.x + 1 + size.x - 2, origin.y + 1 + size.y - 2);
	}
	drawTextEntryBody(window, instData, textColor, textBorder);
}

void W3DGadgetTextEntryImageDraw(GameWindow *window, WinInstanceData *instData)
{
	EntryData *e = entryOf(window);
	e->receivedUnichar = false;
	GameWindowManager &mgr = window->manager();
	ICoord2D origin, size, start, end;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	const int xOffset = instData->m_imageOffset.x;
	const int yOffset = instData->m_imageOffset.y;
	Color textColor, textBorder;
	const std::string *leftImage, *rightImage, *centerImage, *smallCenterImage;
	if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
	{
		textColor = window->winGetDisabledTextColor();
		textBorder = window->winGetDisabledTextBorderColor();
		leftImage = &window->winGetDisabledImage(0);
		rightImage = &window->winGetDisabledImage(1);
		centerImage = &window->winGetDisabledImage(2);
		smallCenterImage = &window->winGetDisabledImage(3);
	}
	else if (BitTest(instData->getState(), WIN_STATE_HILITED))
	{
		textColor = window->winGetHiliteTextColor();
		textBorder = window->winGetHiliteTextBorderColor();
		leftImage = &window->winGetHiliteImage(0);
		rightImage = &window->winGetHiliteImage(1);
		centerImage = &window->winGetHiliteImage(2);
		smallCenterImage = &window->winGetHiliteImage(3);
	}
	else
	{
		textColor = window->winGetEnabledTextColor();
		textBorder = window->winGetEnabledTextBorderColor();
		leftImage = &window->winGetEnabledImage(0);
		rightImage = &window->winGetEnabledImage(1);
		centerImage = &window->winGetEnabledImage(2);
		smallCenterImage = &window->winGetEnabledImage(3);
	}
	int lw, lh, rw, rh, cw, ch, sw, sh;
	const bool haveImages = !leftImage->empty() && !rightImage->empty() && !centerImage->empty() && !smallCenterImage->empty() && mgr.imageSize(*leftImage, lw, lh) && mgr.imageSize(*rightImage, rw, rh) && mgr.imageSize(*centerImage, cw, ch) && mgr.imageSize(*smallCenterImage, sw, sh);
	if (haveImages && cw > 0 && sw > 0)
	{
		ICoord2D leftEnd, rightStart;
		leftEnd.x = origin.x + lw + xOffset;
		leftEnd.y = origin.y + size.y + yOffset;
		rightStart.x = origin.x + size.x - rw + xOffset;
		rightStart.y = origin.y + yOffset;
		int centerWidth = rightStart.x - leftEnd.x;
		int pieces = centerWidth / cw;
		start.x = leftEnd.x;
		start.y = origin.y + yOffset;
		end.y = start.y + size.y;
		for (int i = 0; i < pieces; ++i)
		{
			end.x = start.x + cw;
			mgr.winDrawImage(*centerImage, start.x, start.y, end.x, end.y);
			start.x += cw;
		}
		centerWidth = rightStart.x - start.x;
		pieces = centerWidth / sw + 1;
		end.y = start.y + size.y;
		for (int i = 0; i < pieces; ++i)
		{
			end.x = start.x + sw;
			mgr.winDrawImage(*smallCenterImage, start.x, start.y, end.x, end.y);
			start.x += sw;
		}
		start.x = origin.x + xOffset;
		start.y = origin.y + yOffset;
		end = leftEnd;
		mgr.winDrawImage(*leftImage, start.x, start.y, end.x, end.y);
		start = rightStart;
		end.x = start.x + rw;
		end.y = start.y + size.y;
		mgr.winDrawImage(*rightImage, start.x, start.y, end.x, end.y);
	}
	drawTextEntryBody(window, instData, textColor, textBorder);
}
