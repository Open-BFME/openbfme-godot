// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// Port of ZH GadgetHorizontalSlider.cpp, GadgetVerticalSlider.cpp and GameEngineDevice W3DHorizontalSlider.cpp / W3DVerticalSlider.cpp.
//
// The horizontal slider of the APT options screen is drawn with W3DGadgetHorizontalSliderImageDraw (the segmented box meter: the
// disabled image left = filled box, disabled image right = empty box, hilite image left = the highlight), as in ZH.  ZH's key handling
// is ported as it is (its horizontal Right arrow lowers the position by 2).

#include "GameClient/GUI/Gadgets.h"

#include <algorithm>

void GadgetSliderGetMinMax(GameWindow *g, int *min, int *max)
{
	SliderData *sData = static_cast<SliderData *>(g->winGetUserData());
	*max = sData->maxVal;
	*min = sData->minVal;
}

void GadgetSliderSetPosition(GameWindow *win, int pos)
{
	win->manager().winSendSystemMsg(win, GSM_SET_SLIDER, (WindowMsgData)pos, 0);
}

int GadgetSliderGetPosition(GameWindow *win)
{
	SliderData *sData = static_cast<SliderData *>(win->winGetUserData());
	return sData->position;
}

WindowMsgHandledType GadgetHorizontalSliderInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	SliderData *s = static_cast<SliderData *>(window->winGetUserData());
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindowManager &mgr = window->manager();
	ICoord2D size, childSize, childCenter;
	window->winGetSize(&size.x, &size.y);
	switch (msg)
	{
		case GWM_MOUSE_ENTERING:
			if (BitTest(instData->getStyle(), GWS_MOUSE_TRACK))
			{
				BitSet(instData->m_state, WIN_STATE_HILITED);
				mgr.winSendSystemMsg(window->winGetOwner(), GBM_MOUSE_ENTERING, msgData(window), 0);
			}
			if (window->winGetChild() && BitTest(window->winGetChild()->winGetStyle(), GWS_PUSH_BUTTON))
			{
				BitSet(window->winGetChild()->winGetInstanceData()->m_state, WIN_STATE_HILITED);
			}
			break;
		case GWM_MOUSE_LEAVING:
			if (BitTest(instData->getStyle(), GWS_MOUSE_TRACK))
			{
				BitClear(instData->m_state, WIN_STATE_HILITED);
				mgr.winSendSystemMsg(window->winGetOwner(), GBM_MOUSE_LEAVING, msgData(window), 0);
			}
			if (window->winGetChild() && BitTest(window->winGetChild()->winGetStyle(), GWS_PUSH_BUTTON))
			{
				BitClear(window->winGetChild()->winGetInstanceData()->m_state, WIN_STATE_HILITED);
			}
			break;
		case GWM_LEFT_DRAG:
			if (BitTest(instData->getStyle(), GWS_MOUSE_TRACK))
			{
				mgr.winSendSystemMsg(window->winGetOwner(), GGM_LEFT_DRAG, msgData(window), mData1);
			}
			break;
		case GWM_LEFT_DOWN:
			break;
		case GWM_LEFT_UP:
		{
			int x, y;
			const int mousex = (int)(mData1 & 0xFFFF);
			GameWindow *child = window->winGetChild();
			window->winGetScreenPosition(&x, &y);
			child->winGetSize(&childSize.x, &childSize.y);
			child->winGetPosition(&childCenter.x, &childCenter.y);
			childCenter.x += childSize.x / 2;
			childCenter.y += childSize.y / 2;
			const int pageClickSize = size.x / 5;
			int clickPos = mousex - x;
			if (clickPos >= childCenter.x)
			{
				clickPos = childCenter.x + pageClickSize;
				if (clickPos > mousex - x)
				{
					clickPos = mousex - x;
				}
			}
			else
			{
				clickPos = childCenter.x - pageClickSize;
				if (clickPos < mousex - x)
				{
					clickPos = mousex - x;
				}
			}
			if (clickPos > x + size.x - childSize.x / 2)
			{
				clickPos = x + size.y - childSize.x / 2; // ZH: size.y here (kept)
			}
			if (clickPos < childSize.x / 2)
			{
				clickPos = childSize.x / 2;
			}
			child->winSetPosition(clickPos - childSize.x / 2, HORIZONTAL_SLIDER_THUMB_Y);
			mgr.winSendSystemMsg(window, GGM_LEFT_DRAG, 0, mData1);
			break;
		}
		case GWM_CHAR:
			switch (mData1)
			{
				case KEY_RIGHT:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN) && s->position > s->minVal + 1)
					{
						GameWindow *child = window->winGetChild();
						s->position -= 2;
						mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_TRACK, msgData(window), (WindowMsgData)s->position);
						child->winSetPosition((int)((s->position - s->minVal) * s->numTicks), HORIZONTAL_SLIDER_THUMB_Y);
					}
					break;
				case KEY_LEFT:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN) && s->position < s->maxVal - 1)
					{
						GameWindow *child = window->winGetChild();
						s->position += 2;
						mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_TRACK, msgData(window), (WindowMsgData)s->position);
						child->winSetPosition((int)((s->position - s->minVal) * s->numTicks), HORIZONTAL_SLIDER_THUMB_Y);
					}
					break;
				case KEY_DOWN:
				case KEY_TAB:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						window->winNextTab();
					}
					break;
				case KEY_UP:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						window->winPrevTab();
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

WindowMsgHandledType GadgetHorizontalSliderSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	SliderData *s = static_cast<SliderData *>(window->winGetUserData());
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindowManager &mgr = window->manager();
	ICoord2D size, childSize, childCenter, childRelativePos;
	window->winGetSize(&size.x, &size.y);
	switch (msg)
	{
		case GGM_LEFT_DRAG:
		{
			const int mousex = (int)(mData2 & 0xFFFF);
			int x, y;
			GameWindow *child = window->winGetChild();
			window->winGetScreenPosition(&x, &y);
			child->winGetSize(&childSize.x, &childSize.y);
			child->winGetScreenPosition(&childCenter.x, &childCenter.y);
			child->winGetPosition(&childRelativePos.x, &childRelativePos.y);
			childCenter.x += childSize.x / 2;
			childCenter.y += childSize.y / 2;
			if (mousex > x + size.x - HORIZONTAL_SLIDER_THUMB_WIDTH / 2)
			{
				mgr.winSendSystemMsg(window, GSM_SET_SLIDER, (WindowMsgData)s->maxVal, 0);
				mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_TRACK, msgData(window), (WindowMsgData)s->position);
				break;
			}
			else if (mousex < x + HORIZONTAL_SLIDER_THUMB_WIDTH / 2)
			{
				mgr.winSendSystemMsg(window, GSM_SET_SLIDER, (WindowMsgData)s->minVal, 0);
				mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_TRACK, msgData(window), (WindowMsgData)s->position);
				break;
			}
			if (childCenter.x < x + childSize.x / 2)
			{
				child->winSetPosition(0, HORIZONTAL_SLIDER_THUMB_Y);
				s->position = s->minVal;
			}
			else if (childCenter.x >= x + size.x - childSize.x / 2)
			{
				child->winSetPosition((int)((s->maxVal - s->minVal) * s->numTicks) - HORIZONTAL_SLIDER_THUMB_WIDTH / 2, HORIZONTAL_SLIDER_THUMB_Y);
				s->position = s->maxVal;
			}
			else
			{
				const int delta = childCenter.x - x - HORIZONTAL_SLIDER_THUMB_WIDTH / 2;
				s->position = (int)(delta / s->numTicks) + s->minVal;
				if (s->position > s->maxVal)
				{
					s->position = s->maxVal;
				}
				if (s->position < s->minVal)
				{
					s->position = s->minVal;
				}
				child->winSetPosition(childRelativePos.x, HORIZONTAL_SLIDER_THUMB_Y);
			}
			mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_TRACK, msgData(window), (WindowMsgData)s->position);
			break;
		}
		case GSM_SET_SLIDER:
		{
			int newPos = (int)mData1;
			GameWindow *child = window->winGetChild();
			if (newPos < s->minVal || newPos > s->maxVal)
			{
				break;
			}
			s->position = newPos;
			newPos = (int)((newPos - s->minVal) * s->numTicks);
			child->winSetPosition(newPos, HORIZONTAL_SLIDER_THUMB_Y);
			break;
		}
		case GSM_SET_MIN_MAX:
		{
			GameWindow *child = window->winGetChild();
			s->minVal = (int)mData1;
			s->maxVal = (int)mData2;
			s->numTicks = (float)(size.x - HORIZONTAL_SLIDER_THUMB_WIDTH) / (float)(s->maxVal - s->minVal);
			s->position = s->minVal;
			child->winSetPosition(0, HORIZONTAL_SLIDER_THUMB_Y);
			break;
		}
		case GWM_CREATE:
			break;
		case GWM_DESTROY:
			delete static_cast<SliderData *>(window->winGetUserData());
			window->winSetUserData(nullptr);
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
			mgr.winSendSystemMsg(window->winGetOwner(), GGM_FOCUS_CHANGE, mData1, (WindowMsgData)window->winGetWindowId());
			*msgPtr<bool>(mData2) = true;
			break;
		case GGM_RESIZED:
		{
			// lane FB7-1, RotWK RW 0x7237F8 (message 0x4004): the ticks follow the new width and the thumb is 13 wide and the full height
			const int width = (int)mData1, height = (int)mData2;
			s->numTicks = (float)(width - HORIZONTAL_SLIDER_THUMB_WIDTH) / (float)(s->maxVal - s->minVal);
			if (GameWindow *thumb = window->winGetChild())
			{
				thumb->winSetSize(HORIZONTAL_SLIDER_THUMB_WIDTH, height);
			}
			break;
		}
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

WindowMsgHandledType GadgetVerticalSliderInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	SliderData *s = static_cast<SliderData *>(window->winGetUserData());
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindowManager &mgr = window->manager();
	switch (msg)
	{
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
				mgr.winSendSystemMsg(window->winGetOwner(), GGM_LEFT_DRAG, msgData(window), mData1);
			}
			break;
		case GWM_LEFT_DOWN:
			break;
		case GWM_LEFT_UP:
		{
			int x, y;
			const int mousey = (int)(mData1 >> 16);
			ICoord2D size, childSize, childCenter;
			GameWindow *child = window->winGetChild();
			window->winGetScreenPosition(&x, &y);
			window->winGetSize(&size.x, &size.y);
			child->winGetSize(&childSize.x, &childSize.y);
			child->winGetPosition(&childCenter.x, &childCenter.y);
			childCenter.x += childSize.x / 2;
			childCenter.y += childSize.y / 2;
			const int pageClickSize = size.y / 5;
			int clickPos = mousey - y;
			if (clickPos >= childCenter.y)
			{
				clickPos = childCenter.y + pageClickSize;
				if (clickPos > mousey - y)
				{
					clickPos = mousey - y;
				}
			}
			else
			{
				clickPos = childCenter.y - pageClickSize;
				if (clickPos < mousey - y)
				{
					clickPos = mousey - y;
				}
			}
			if (clickPos > y + size.y - childSize.y / 2)
			{
				clickPos = y + size.y - childSize.y / 2;
			}
			if (clickPos < childSize.y / 2)
			{
				clickPos = childSize.y / 2;
			}
			child->winSetPosition(0, clickPos - childSize.y / 2);
			mgr.winSendSystemMsg(window, GGM_LEFT_DRAG, 0, mData1);
			break;
		}
		case GWM_CHAR:
			switch (mData1)
			{
				case KEY_UP:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN) && s->position < s->maxVal - 1)
					{
						GameWindow *child = window->winGetChild();
						s->position += 2;
						mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_TRACK, msgData(window), (WindowMsgData)s->position);
						child->winSetPosition(0, (int)((s->maxVal - s->position) * s->numTicks));
					}
					break;
				case KEY_DOWN:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN) && s->position > s->minVal + 1)
					{
						GameWindow *child = window->winGetChild();
						s->position -= 2;
						mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_TRACK, msgData(window), (WindowMsgData)s->position);
						child->winSetPosition(0, (int)((s->maxVal - s->position) * s->numTicks));
					}
					break;
				case KEY_RIGHT:
				case KEY_TAB:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						window->winNextTab();
					}
					break;
				case KEY_LEFT:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						window->winPrevTab();
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

WindowMsgHandledType GadgetVerticalSliderSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	SliderData *s = static_cast<SliderData *>(window->winGetUserData());
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindowManager &mgr = window->manager();
	switch (msg)
	{
		case GBM_SELECTED:
			mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_DONE, msgData(window), (WindowMsgData)s->position);
			break;
		case GGM_LEFT_DRAG:
		{
			const int mousey = (int)(mData2 >> 16);
			int x, y, delta;
			ICoord2D size, childSize, childCenter;
			GameWindow *child = window->winGetChild();
			window->winGetScreenPosition(&x, &y);
			window->winGetSize(&size.x, &size.y);
			child->winGetSize(&childSize.x, &childSize.y);
			child->winGetScreenPosition(&childCenter.x, &childCenter.y);
			childCenter.x += childSize.x / 2;
			childCenter.y += childSize.y / 2;
			if (mousey > y + size.y)
			{
				mgr.winSendSystemMsg(window, GSM_SET_SLIDER, (WindowMsgData)s->minVal, 0);
				mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_TRACK, msgData(window), (WindowMsgData)s->position);
				break;
			}
			else if (mousey < y)
			{
				mgr.winSendSystemMsg(window, GSM_SET_SLIDER, (WindowMsgData)s->maxVal, 0);
				mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_TRACK, msgData(window), (WindowMsgData)s->position);
				break;
			}
			if (childCenter.y <= y + childSize.y / 2)
			{
				child->winSetPosition(0, 0);
				s->position = s->maxVal;
			}
			else if (childCenter.y >= y + size.y - childSize.y / 2)
			{
				child->winSetPosition(0, size.y - childSize.y);
				s->position = s->minVal;
			}
			else
			{
				delta = childCenter.y - y - childSize.y / 2;
				s->position = (int)(delta / s->numTicks);
				if (s->position > s->maxVal)
				{
					s->position = s->maxVal;
				}
				s->position = s->maxVal - s->position;
			}
			mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_TRACK, msgData(window), (WindowMsgData)s->position);
			break;
		}
		case GSM_SET_SLIDER:
		{
			int newPos = (int)mData1;
			GameWindow *child = window->winGetChild();
			if (newPos < s->minVal || newPos > s->maxVal)
			{
				break;
			}
			s->position = newPos;
			newPos = (int)((s->maxVal - newPos) * s->numTicks);
			child->winSetPosition(0, newPos);
			break;
		}
		case GSM_SET_MIN_MAX:
		{
			ICoord2D size;
			GameWindow *child = window->winGetChild();
			window->winGetSize(&size.x, &size.y);
			s->minVal = (int)mData1;
			s->maxVal = (int)mData2;
			s->numTicks = (float)(size.y - GADGET_SIZE) / (float)(s->maxVal - s->minVal);
			s->position = s->minVal;
			child->winSetPosition(0, (int)((s->maxVal - s->minVal) * s->numTicks));
			break;
		}
		case GWM_CREATE:
			break;
		case GWM_DESTROY:
			delete static_cast<SliderData *>(window->winGetUserData());
			window->winSetUserData(nullptr);
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
			mgr.winSendSystemMsg(window->winGetOwner(), GGM_FOCUS_CHANGE, mData1, (WindowMsgData)window->winGetWindowId());
			*msgPtr<bool>(mData2) = true;
			break;
		case GGM_RESIZED:
		{
			const int width = (int)mData1;
			if (GameWindow *thumb = window->winGetChild())
			{
				thumb->winSetSize(width, GADGET_SIZE);
			}
			break;
		}
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

// ---- draw -----------------------------------------------------------------------------------------------------------------------------------------

static void drawSliderBackground(GameWindow *window, WinInstanceData *instData)
{
	ICoord2D origin, size;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	Color backBorder, backColor;
	if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
	{
		backBorder = window->winGetDisabledBorderColor(0);
		backColor = window->winGetDisabledColor(0);
	}
	else if (BitTest(instData->getState(), WIN_STATE_HILITED))
	{
		backBorder = window->winGetHiliteBorderColor(0);
		backColor = window->winGetHiliteColor(0);
	}
	else
	{
		backBorder = window->winGetEnabledBorderColor(0);
		backColor = window->winGetEnabledColor(0);
	}
	GameWindowManager &mgr = window->manager();
	if (backBorder != WIN_COLOR_UNDEFINED)
	{
		mgr.winOpenRect(backBorder, WIN_DRAW_LINE_WIDTH, origin.x, origin.y, origin.x + size.x, origin.y + size.y);
	}
	if (backColor != WIN_COLOR_UNDEFINED)
	{
		mgr.winFillRect(backColor, WIN_DRAW_LINE_WIDTH, origin.x + 1, origin.y + 1, origin.x + 1 + size.x - 2, origin.y + 1 + size.y - 2);
	}
}

void W3DGadgetHorizontalSliderDraw(GameWindow *window, WinInstanceData *instData) { drawSliderBackground(window, instData); }
void W3DGadgetVerticalSliderDraw(GameWindow *window, WinInstanceData *instData) { drawSliderBackground(window, instData); }

// W3DGadgetHorizontalSliderImageDraw: the box meter. Lane FB7-1: RotWK's body (RW 0x4A1130, the function lexicon entry RW 0xD98E78), which is BFME2's
// (Open-BFME-2 W3DHorizontalSlider.cpp 0x000A1747) and not ZH's. TARGET FACTS: unless status bit 0x08000000 is set the multipliers are the display's
// width / 800 and height / 600 (RW 0xBDED74 = 1/800, RW 0xBDED70 = 1/600); a box is ftol(fill image width * xMulti * 0.6) wide (x87, 0.6 a double at
// RW 0xBDED68) and (int)(window height * yMulti) tall, boxes 1 pixel apart; a box is selected while its left edge is at or before origin +
// (int)((position - min) / (max - min) * width) and the position is not the minimum; the row is centred by half the blank width; a hilited slider
// draws numBoxes + 1 squares (bw + 1) wide and tall, (int)(box height * 0.8) below the top (0.8 a double at RW 0xBDED60), shifted left by
// (bw + 1) / 2; then the selected boxes with disabled image 0, the rest with disabled image 1 (HorzSlider.wnd: AptHSliderOnBar / NoImage).
// The port's gadget coordinates are stage pixels, so `displayWidth / 800` is the multiplier retail has at a 1024 x 768 screen (INFERENCE, S-1911).
void W3DGadgetHorizontalSliderImageDraw(GameWindow *window, WinInstanceData *instData)
{
	GameWindowManager &mgr = window->manager();
	ICoord2D origin, size;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	const std::string &highlightSquare = window->winGetHiliteImage(0);
	const std::string &blankSquare = window->winGetDisabledImage(1);
	const std::string &fillSquare = window->winGetDisabledImage(0);
	SliderData *s = static_cast<SliderData *>(window->winGetUserData());
	int fillW = 0, fillH = 0;
	if (fillSquare.empty() || !mgr.imageSize(fillSquare, fillW, fillH))
	{
		return; // retail dereferences the image; an unknown or missing image draws nothing (the unresolved name is reported by the list)
	}
	float xMulti = 1.0f, yMulti = 1.0f;
	if (!BitTest(window->winGetStatus(), 0x08000000u))
	{
		xMulti = (float)mgr.displayWidth() * (1.0f / 800.0f);
		yMulti = (float)mgr.displayHeight() * (1.0f / 600.0f);
	}
	const int boxWidth = (int)((double)fillW * (double)xMulti * 0.6);
	const int boxHeight = (int)((float)size.y * yMulti);
	if (boxWidth <= 0)
	{
		return;
	}
	const int maxSelectedX = origin.x + (int)((float)(s->position - s->minVal) / (float)(s->maxVal - s->minVal) * (float)size.x);
	int numBoxes = 0, numSelectedBoxes = 0;
	int start = origin.x;
	int end = start + boxWidth;
	while (end < origin.x + size.x)
	{
		if (start <= maxSelectedX && end < origin.x + size.x && s->position != s->minVal)
		{
			++numSelectedBoxes;
		}
		start = end + 1;
		end = start + boxWidth;
		++numBoxes;
	}
	const int step = boxWidth + 1;
	const int highlightX = step / -2;
	const int highlightY = (int)((double)boxHeight * 0.8);
	origin.x += (boxWidth - end + size.x + origin.x) / 2;
	if (BitTest(instData->getState(), WIN_STATE_HILITED) && !highlightSquare.empty())
	{
		const int y0 = origin.y + highlightY;
		for (int i = 0; i < numBoxes + 1; ++i)
		{
			const int x0 = origin.x + highlightX + i * step;
			mgr.winDrawImage(highlightSquare, x0, y0, x0 + step, y0 + boxWidth + 1);
		}
	}
	for (int i = 0; i < numSelectedBoxes; ++i)
	{
		const int x0 = origin.x + i * step;
		mgr.winDrawImage(fillSquare, x0, origin.y, x0 + boxWidth, origin.y + boxHeight);
	}
	if (!blankSquare.empty())
	{
		for (int i = numSelectedBoxes; i < numBoxes; ++i)
		{
			const int x0 = origin.x + i * step;
			mgr.winDrawImage(blankSquare, x0, origin.y, x0 + boxWidth, origin.y + boxHeight);
		}
	}
}

// W3DVerticalSlider.cpp W3DGadgetVerticalSliderImageDraw
void W3DGadgetVerticalSliderImageDraw(GameWindow *window, WinInstanceData *instData)
{
	GameWindowManager &mgr = window->manager();
	ICoord2D origin, size, start, end;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	const int xOffset = instData->m_imageOffset.x;
	const int yOffset = instData->m_imageOffset.y;
	const std::string *topImage, *bottomImage, *centerImage, *smallCenterImage;
	if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
	{
		topImage = &window->winGetDisabledImage(0);
		bottomImage = &window->winGetDisabledImage(1);
		centerImage = &window->winGetDisabledImage(2);
		smallCenterImage = &window->winGetDisabledImage(3);
	}
	else if (BitTest(instData->getState(), WIN_STATE_HILITED))
	{
		topImage = &window->winGetHiliteImage(0);
		bottomImage = &window->winGetHiliteImage(1);
		centerImage = &window->winGetHiliteImage(2);
		smallCenterImage = &window->winGetHiliteImage(3);
	}
	else
	{
		topImage = &window->winGetEnabledImage(0);
		bottomImage = &window->winGetEnabledImage(1);
		centerImage = &window->winGetEnabledImage(2);
		smallCenterImage = &window->winGetEnabledImage(3);
	}
	if (topImage->empty() || bottomImage->empty() || centerImage->empty() || smallCenterImage->empty())
	{
		return;
	}
	int tw, th, bw, bh, cw, ch, sw, sh;
	if (!mgr.imageSize(*topImage, tw, th) || !mgr.imageSize(*bottomImage, bw, bh) || !mgr.imageSize(*centerImage, cw, ch) || !mgr.imageSize(*smallCenterImage, sw, sh))
	{
		return;
	}
	if (th + bh >= size.y)
	{
		start.x = origin.x + xOffset;
		start.y = origin.y + yOffset;
		end.x = origin.x + xOffset + tw;
		end.y = origin.y + size.y / 2;
		mgr.winDrawImage(*topImage, start.x, start.y, end.x, end.y);
		start.y = origin.y + size.y / 2;
		end.x = origin.x + xOffset + bw;
		end.y = origin.y + yOffset + size.y;
		mgr.winDrawImage(*bottomImage, start.x, start.y, end.x, end.y);
	}
	else
	{
		ICoord2D topEnd, bottomStart;
		topEnd.x = origin.x + tw + xOffset;
		topEnd.y = origin.y + th + yOffset;
		bottomStart.x = origin.x + xOffset;
		bottomStart.y = origin.y + size.y - bh + yOffset;
		int centerHeight = bottomStart.y - topEnd.y;
		int pieces = ch > 0 ? centerHeight / ch : 0;
		start.x = origin.x + xOffset;
		start.y = topEnd.y;
		end.x = start.x + cw;
		end.y = start.y + ch;
		for (int i = 0; i < pieces; ++i)
		{
			mgr.winDrawImage(*centerImage, start.x, start.y, end.x, end.y);
			start.y += ch;
			end.y += ch;
		}
		centerHeight = bottomStart.y - start.y;
		pieces = sh > 0 ? centerHeight / sh + 1 : 0;
		end.y = start.y + sh;
		for (int i = 0; i < pieces; ++i)
		{
			mgr.winDrawImage(*smallCenterImage, start.x, start.y, end.x, end.y);
			start.y += sh;
			end.y += sh;
		}
		start.x = origin.x + xOffset;
		start.y = origin.y + yOffset;
		end = topEnd;
		mgr.winDrawImage(*topImage, start.x, start.y, end.x, end.y);
		start = bottomStart;
		end.x = start.x + bw;
		end.y = start.y + bh;
		mgr.winDrawImage(*bottomImage, start.x, start.y, end.x, end.y);
	}
}
