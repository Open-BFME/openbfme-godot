// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// Port of ZH GameClient/GUI/Gadget/GadgetCheckBox.cpp and GameEngineDevice W3DCheckBox.cpp.
//
// The check box of the APT skins (window\apt\checkbox.wnd): draw data slot 0 = the background, slot 1 = the unchecked box, slot 2 = the
// checked box, per state (ZH GadgetCheckBox.h GadgetCheckBoxGet*BoxImage / *BoxColor).

#include "GameClient/GUI/Gadgets.h"

WindowMsgHandledType GadgetCheckBoxInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindowManager &mgr = window->manager();
	switch (msg)
	{
		case GWM_MOUSE_ENTERING:
			if (BitTest(instData->getStyle(), GWS_MOUSE_TRACK))
			{
				BitSet(instData->m_state, WIN_STATE_HILITED);
				mgr.winSendSystemMsg(window->winGetOwner(), GBM_MOUSE_ENTERING, msgData(window), mData1);
			}
			break;
		case GWM_MOUSE_LEAVING:
			if (BitTest(instData->getStyle(), GWS_MOUSE_TRACK))
			{
				BitClear(instData->m_state, WIN_STATE_HILITED);
				mgr.winSendSystemMsg(window->winGetOwner(), GBM_MOUSE_LEAVING, msgData(window), mData1);
			}
			break;
		case GWM_LEFT_DRAG:
			mgr.winSendSystemMsg(window->winGetOwner(), GGM_LEFT_DRAG, msgData(window), mData1);
			break;
		case GWM_LEFT_DOWN:
			break;
		case GWM_LEFT_UP:
			if (!BitTest(instData->getState(), WIN_STATE_HILITED))
			{
				return MSG_IGNORED;
			}
			instData->m_state ^= WIN_STATE_SELECTED;
			mgr.winSendSystemMsg(window->winGetOwner(), GBM_SELECTED, msgData(window), mData1);
			break;
		case GWM_RIGHT_DOWN:
			break;
		case GWM_RIGHT_UP:
			if (BitTest(instData->getState(), WIN_STATE_SELECTED))
			{
				mgr.winSendSystemMsg(instData->getOwner(), GBM_SELECTED_RIGHT, msgData(window), mData1);
				BitClear(instData->m_state, WIN_STATE_SELECTED);
			}
			else
			{
				return MSG_IGNORED;
			}
			break;
		case GWM_CHAR:
			switch (mData1)
			{
				case KEY_ENTER:
				case KEY_SPACE:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						instData->m_state ^= WIN_STATE_SELECTED;
						mgr.winSendSystemMsg(window->winGetOwner(), GBM_SELECTED, msgData(window), 0);
					}
					break;
				case KEY_DOWN:
				case KEY_RIGHT:
				case KEY_TAB:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						mgr.winNextTab(window);
					}
					break;
				case KEY_UP:
				case KEY_LEFT:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						mgr.winPrevTab(window);
					}
					break;
				default:
					return MSG_IGNORED;
			}
			break;
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

WindowMsgHandledType GadgetCheckBoxSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	WinInstanceData *instData = window->winGetInstanceData();
	switch (msg)
	{
		case GGM_SET_LABEL:
			window->winSetText(*msgPtr<UnicodeString>(mData1));
			break;
		case GWM_CREATE:
		case GWM_DESTROY:
			break;
		case GWM_INPUT_FOCUS:
			if (mData1 == 0)
			{
				BitClear(instData->m_state, WIN_STATE_HILITED);
			}
			else
			{
				BitSet(instData->m_state, WIN_STATE_HILITED);
			}
			window->manager().winSendSystemMsg(window->winGetOwner(), GGM_FOCUS_CHANGE, mData1, (WindowMsgData)window->winGetWindowId());
			*msgPtr<bool>(mData2) = mData1 != 0;
			break;
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

void GadgetCheckBoxSetText(GameWindow *g, const UnicodeString &text)
{
	if (g == nullptr)
	{
		return;
	}
	UnicodeString copy = text;
	g->manager().winSendSystemMsg(g, GGM_SET_LABEL, msgData(&copy), 0);
}

void GadgetCheckBoxSetChecked(GameWindow *g, bool isChecked)
{
	WinInstanceData *instData = g->winGetInstanceData();
	if (isChecked)
	{
		BitSet(instData->m_state, WIN_STATE_SELECTED);
	}
	else
	{
		BitClear(instData->m_state, WIN_STATE_SELECTED);
	}
	g->manager().winSendSystemMsg(g->winGetOwner(), GBM_SELECTED, msgData(g), 0);
}

void GadgetCheckBoxToggle(GameWindow *g)
{
	WinInstanceData *instData = g->winGetInstanceData();
	if (BitTest(instData->m_state, WIN_STATE_SELECTED))
	{
		BitClear(instData->m_state, WIN_STATE_SELECTED);
	}
	else
	{
		BitSet(instData->m_state, WIN_STATE_SELECTED);
	}
	g->manager().winSendSystemMsg(g->winGetOwner(), GBM_SELECTED, msgData(g), 0);
}

bool GadgetCheckBoxIsChecked(GameWindow *g)
{
	return BitTest(g->winGetInstanceData()->m_state, WIN_STATE_SELECTED);
}

static void drawCheckBoxText(GameWindow *window, WinInstanceData *instData)
{
	if (instData->getTextLength() == 0)
	{
		return;
	}
	ICoord2D origin, size;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	Color textColor, dropColor;
	if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
	{
		textColor = window->winGetDisabledTextColor();
		dropColor = window->winGetDisabledTextBorderColor();
	}
	else if (BitTest(instData->getState(), WIN_STATE_HILITED))
	{
		textColor = window->winGetHiliteTextColor();
		dropColor = window->winGetHiliteTextBorderColor();
	}
	else
	{
		textColor = window->winGetEnabledTextColor();
		dropColor = window->winGetEnabledTextBorderColor();
	}
	GameWindowManager &mgr = window->manager();
	const int height = mgr.winFontHeight(window->winGetFont());
	mgr.winDrawText(instData->getText(), window->winGetFont(), origin.x + size.y, origin.y + (size.y / 2) - (height / 2), textColor, dropColor);
}

void W3DGadgetCheckBoxDraw(GameWindow *window, WinInstanceData *instData)
{
	Color backColor, backBorder, boxColor, boxBorder;
	ICoord2D origin, size, start, end;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	const int checkOffsetFromLeft = size.x / 16;
	const bool selected = BitTest(instData->getState(), WIN_STATE_SELECTED);
	if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
	{
		backColor = window->winGetDisabledColor(0);
		backBorder = window->winGetDisabledBorderColor(0);
		boxColor = window->winGetDisabledColor(selected ? 2 : 1);
		boxBorder = window->winGetDisabledBorderColor(selected ? 2 : 1);
	}
	else if (BitTest(instData->getState(), WIN_STATE_HILITED))
	{
		backColor = window->winGetHiliteColor(0);
		backBorder = window->winGetHiliteBorderColor(0);
		boxColor = window->winGetHiliteColor(selected ? 2 : 1);
		boxBorder = window->winGetHiliteBorderColor(selected ? 2 : 1);
	}
	else
	{
		backColor = window->winGetEnabledColor(0);
		backBorder = window->winGetEnabledBorderColor(0);
		boxColor = window->winGetEnabledColor(selected ? 2 : 1);
		boxBorder = window->winGetEnabledBorderColor(selected ? 2 : 1);
	}
	GameWindowManager &mgr = window->manager();
	start = origin;
	end.x = start.x + size.x;
	end.y = start.y + size.y;
	mgr.winOpenRect(backBorder, WIN_DRAW_LINE_WIDTH, start.x, start.y, end.x, end.y);
	++start.x;
	++start.y;
	--end.x;
	--end.y;
	mgr.winFillRect(backColor, WIN_DRAW_LINE_WIDTH, start.x, start.y, end.x, end.y);
	start.x = origin.x + checkOffsetFromLeft;
	start.y = origin.y + (size.y / 3);
	end.x = start.x + (size.y / 3);
	end.y = start.y + (size.y / 3);
	mgr.winOpenRect(boxBorder, WIN_DRAW_LINE_WIDTH, start.x, start.y, end.x, end.y);
	if (boxColor != WIN_COLOR_UNDEFINED)
	{
		mgr.winDrawLine(boxColor, WIN_DRAW_LINE_WIDTH, start.x, start.y, end.x, end.y);
		mgr.winDrawLine(boxColor, WIN_DRAW_LINE_WIDTH, start.x, end.y, end.x, start.y);
	}
	if (instData->getTextLength())
	{
		drawCheckBoxText(window, instData);
	}
}

void W3DGadgetCheckBoxImageDraw(GameWindow *window, WinInstanceData *instData)
{
	ICoord2D origin, size;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	const bool selected = BitTest(instData->getState(), WIN_STATE_SELECTED);
	const std::string *boxImage;
	if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
	{
		boxImage = &window->winGetDisabledImage(selected ? 2 : 1);
	}
	else if (BitTest(instData->getState(), WIN_STATE_HILITED))
	{
		boxImage = &window->winGetHiliteImage(selected ? 2 : 1);
	}
	else
	{
		boxImage = &window->winGetEnabledImage(selected ? 2 : 1);
	}
	if (!boxImage->empty())
	{
		const int checkOffsetFromLeft = 0;
		const int sx = origin.x + instData->m_imageOffset.x + checkOffsetFromLeft;
		const int sy = origin.y + 3;
		window->manager().winDrawImage(*boxImage, sx, sy, sx + (size.y - 6), sy + (size.y - 6));
	}
	if (instData->getTextLength())
	{
		drawCheckBoxText(window, instData);
	}
}
