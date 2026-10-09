// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// Port of ZH GameClient/GUI/Gadget/GadgetPushButton.cpp (input, system) and GameEngineDevice W3DPushButton.cpp (draw).
//
// The push button exists here as the part of the other gadgets (list box arrows, combo drop-down button, slider thumbs).  Not ported
// (stop S-177): the click audio (AudioEventRTS "GUIClick": ShellServices::playSound is the APT path), the clock / overlay / video
// buffer draws and the three-slice image button (W3DGadgetPushButtonImageDrawThree: a button with a middle image reports a note and is
// drawn with its single image).

#include "GameClient/GUI/Gadgets.h"

WindowMsgHandledType GadgetPushButtonInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindowManager &mgr = window->manager();
	auto onMouseDown = [window]() {
		bool onDown = BitTest(window->winGetStatus(), WIN_STATUS_ON_MOUSE_DOWN);
		if (BitTest(window->winGetStatus(), WIN_STATUS_CHECK_LIKE))
		{
			onDown = true;
		}
		return onDown;
	};
	switch (msg)
	{
		case GWM_MOUSE_ENTERING:
		{
			if (BitTest(instData->getStyle(), GWS_MOUSE_TRACK))
			{
				BitSet(instData->m_state, WIN_STATE_HILITED);
				mgr.winSendSystemMsg(instData->getOwner(), GBM_MOUSE_ENTERING, msgData(window), mData1);
			}
			if (window->winGetParent() && BitTest(window->winGetParent()->winGetStyle(), GWS_HORZ_SLIDER))
			{
				BitSet(window->winGetParent()->winGetInstanceData()->m_state, WIN_STATE_HILITED);
			}
			break;
		}
		case GWM_MOUSE_LEAVING:
		{
			if (BitTest(instData->getStyle(), GWS_MOUSE_TRACK))
			{
				BitClear(instData->m_state, WIN_STATE_HILITED);
				mgr.winSendSystemMsg(instData->getOwner(), GBM_MOUSE_LEAVING, msgData(window), mData1);
			}
			if (!BitTest(window->winGetStatus(), WIN_STATUS_CHECK_LIKE))
			{
				if (BitTest(instData->getState(), WIN_STATE_SELECTED))
				{
					BitClear(instData->m_state, WIN_STATE_SELECTED);
				}
			}
			if (window->winGetParent() && BitTest(window->winGetParent()->winGetStyle(), GWS_HORZ_SLIDER))
			{
				BitClear(window->winGetParent()->winGetInstanceData()->m_state, WIN_STATE_HILITED);
			}
			break;
		}
		case GWM_LEFT_DRAG:
			mgr.winSendSystemMsg(instData->getOwner(), GGM_LEFT_DRAG, msgData(window), mData1);
			break;
		case GWM_LEFT_DOWN:
		{
			if (BitTest(window->winGetStatus(), WIN_STATUS_CHECK_LIKE))
			{
				if (BitTest(instData->m_state, WIN_STATE_SELECTED))
				{
					BitClear(instData->m_state, WIN_STATE_SELECTED);
				}
				else
				{
					BitSet(instData->m_state, WIN_STATE_SELECTED);
				}
			}
			else
			{
				BitSet(instData->m_state, WIN_STATE_SELECTED);
			}
			if (onMouseDown())
			{
				mgr.winSendSystemMsg(instData->getOwner(), GBM_SELECTED, msgData(window), mData1);
			}
			break;
		}
		case GWM_LEFT_UP:
		{
			if (BitTest(instData->getState(), WIN_STATE_SELECTED) && !BitTest(window->winGetStatus(), WIN_STATUS_CHECK_LIKE))
			{
				if (!onMouseDown())
				{
					mgr.winSendSystemMsg(instData->getOwner(), GBM_SELECTED, msgData(window), mData1);
				}
				BitClear(instData->m_state, WIN_STATE_SELECTED);
			}
			else
			{
				return MSG_IGNORED;
			}
			break;
		}
		case GWM_RIGHT_DOWN:
		{
			if (BitTest(instData->getStatus(), WIN_STATUS_RIGHT_CLICK))
			{
				if (BitTest(window->winGetStatus(), WIN_STATUS_CHECK_LIKE))
				{
					if (BitTest(instData->m_state, WIN_STATE_SELECTED))
					{
						BitClear(instData->m_state, WIN_STATE_SELECTED);
					}
					else
					{
						BitSet(instData->m_state, WIN_STATE_SELECTED);
					}
					mgr.winSendSystemMsg(instData->getOwner(), GBM_SELECTED_RIGHT, msgData(window), mData1);
				}
				else
				{
					BitSet(instData->m_state, WIN_STATE_SELECTED);
				}
			}
			else
			{
				return MSG_IGNORED;
			}
			break;
		}
		case GWM_RIGHT_UP:
		{
			if (BitTest(instData->getStatus(), WIN_STATUS_RIGHT_CLICK))
			{
				if (BitTest(instData->getState(), WIN_STATE_SELECTED) && !BitTest(window->winGetStatus(), WIN_STATUS_CHECK_LIKE))
				{
					mgr.winSendSystemMsg(instData->getOwner(), GBM_SELECTED_RIGHT, msgData(window), mData1);
					BitClear(instData->m_state, WIN_STATE_SELECTED);
				}
				else
				{
					return MSG_IGNORED;
				}
			}
			else
			{
				return MSG_IGNORED;
			}
			break;
		}
		case GWM_CHAR:
		{
			switch (mData1)
			{
				case KEY_ENTER:
				case KEY_SPACE:
				{
					if (BitTest((std::uint32_t)mData2, KEY_STATE_UP))
					{
						if (BitTest(instData->getState(), WIN_STATE_SELECTED) && !BitTest(window->winGetStatus(), WIN_STATUS_CHECK_LIKE))
						{
							mgr.winSendSystemMsg(instData->getOwner(), GBM_SELECTED, msgData(window), 0);
							BitClear(instData->m_state, WIN_STATE_SELECTED);
						}
					}
					else
					{
						if (BitTest(window->winGetStatus(), WIN_STATUS_CHECK_LIKE))
						{
							if (BitTest(instData->m_state, WIN_STATE_SELECTED))
							{
								BitClear(instData->m_state, WIN_STATE_SELECTED);
							}
							else
							{
								BitSet(instData->m_state, WIN_STATE_SELECTED);
							}
							mgr.winSendSystemMsg(instData->getOwner(), GBM_SELECTED, msgData(window), mData1);
						}
						else
						{
							BitSet(instData->m_state, WIN_STATE_SELECTED);
						}
					}
					break;
				}
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
		}
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

WindowMsgHandledType GadgetPushButtonSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	WinInstanceData *instData = window->winGetInstanceData();
	switch (msg)
	{
		case GGM_SET_LABEL:
			window->winSetText(*msgPtr<UnicodeString>(mData1));
			break;
		case GWM_CREATE:
			break;
		case GWM_DESTROY:
			window->winSetUserData(nullptr); // ZH deletes its PushButtonData; none is created here
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
			window->manager().winSendSystemMsg(instData->getOwner(), GGM_FOCUS_CHANGE, mData1, (WindowMsgData)window->winGetWindowId());
			*msgPtr<bool>(mData2) = mData1 != 0;
			break;
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

void GadgetButtonSetText(GameWindow *g, const UnicodeString &text)
{
	if (g == nullptr)
	{
		return;
	}
	UnicodeString copy = text;
	g->manager().winSendSystemMsg(g, GGM_SET_LABEL, msgData(&copy), 0);
}

// W3DPushButton.cpp drawButtonText
static void drawButtonText(GameWindow *window, WinInstanceData *instData)
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
	GameFont *font = window->winGetFont();
	const int width = mgr.winTextWidth(font, instData->getText());
	const int height = mgr.winWrappedHeight(font, instData->getText(), size.x);
	ICoord2D textPos;
	textPos.x = origin.x + (size.x / 2) - (width / 2);
	textPos.y = origin.y + (size.y / 2) - (height / 2);
	mgr.winDrawText(instData->getText(), font, textPos.x, textPos.y, textColor, dropColor, size.x, BitTest(instData->getStatus(), WIN_STATUS_WRAP_CENTERED));
}

// W3DPushButton.cpp W3DGadgetPushButtonDraw (colours)
void W3DGadgetPushButtonDraw(GameWindow *window, WinInstanceData *instData)
{
	Color color, border;
	ICoord2D origin, size;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	const bool selected = BitTest(instData->getState(), WIN_STATE_SELECTED);
	if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
	{
		color = window->winGetDisabledColor(selected ? 1 : 0);
		border = window->winGetDisabledBorderColor(selected ? 1 : 0);
	}
	else if (BitTest(instData->getState(), WIN_STATE_HILITED))
	{
		color = window->winGetHiliteColor(selected ? 1 : 0);
		border = window->winGetHiliteBorderColor(selected ? 1 : 0);
	}
	else
	{
		color = window->winGetEnabledColor(selected ? 1 : 0);
		border = window->winGetEnabledBorderColor(selected ? 1 : 0);
	}
	GameWindowManager &mgr = window->manager();
	int sx = origin.x, sy = origin.y, ex = sx + size.x, ey = sy + size.y;
	if (border != WIN_COLOR_UNDEFINED)
	{
		mgr.winOpenRect(border, WIN_DRAW_LINE_WIDTH, sx, sy, ex, ey);
	}
	if (color != WIN_COLOR_UNDEFINED)
	{
		mgr.winFillRect(color, WIN_DRAW_LINE_WIDTH, sx + 1, sy + 1, ex - 1, ey - 1);
	}
	if (instData->getTextLength())
	{
		drawButtonText(window, instData);
	}
}

// W3DPushButton.cpp W3DGadgetPushButtonImageDraw -> ...DrawOne (the single image form)
void W3DGadgetPushButtonImageDraw(GameWindow *window, WinInstanceData *instData)
{
	GameWindowManager &mgr = window->manager();
	if (!window->winGetEnabledImage(5).empty())
	{
		// GadgetButtonGetMiddleEnabledImage is enabled image 5 (ZH GadgetPushButton.h): the three-slice draw is not ported
		mgr.note("push button with a middle image: W3DGadgetPushButtonImageDrawThree is not ported (S-177), drawn as one image");
	}
	const std::string *image = &window->winGetEnabledImage(0);
	if (!BitTest(window->winGetStatus(), WIN_STATUS_USE_OVERLAY_STATES))
	{
		const bool selected = BitTest(instData->getState(), WIN_STATE_SELECTED);
		if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
		{
			image = &window->winGetDisabledImage(selected ? 1 : 0);
		}
		else if (BitTest(instData->getState(), WIN_STATE_HILITED))
		{
			image = &window->winGetHiliteImage(selected ? 1 : 0);
		}
		else if (selected)
		{
			image = &window->winGetHiliteImage(1); // ZH: the enabled+selected state shows the hilite-selected image
		}
	}
	if (!image->empty())
	{
		ICoord2D start, size;
		window->winGetScreenPosition(&start.x, &start.y);
		window->winGetSize(&size.x, &size.y);
		start.x += instData->m_imageOffset.x;
		start.y += instData->m_imageOffset.y;
		mgr.winDrawImage(*image, start.x, start.y, start.x + size.x, start.y + size.y);
	}
	if (instData->getTextLength())
	{
		drawButtonText(window, instData);
	}
}
