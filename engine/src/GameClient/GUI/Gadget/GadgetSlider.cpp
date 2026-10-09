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
			child->winSetPosition(clickPos - childSize.x / 2, HORIZONTAL_SLIDER_THUMB_POSITION);
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
						child->winSetPosition((int)((s->position - s->minVal) * s->numTicks), HORIZONTAL_SLIDER_THUMB_POSITION);
					}
					break;
				case KEY_LEFT:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN) && s->position < s->maxVal - 1)
					{
						GameWindow *child = window->winGetChild();
						s->position += 2;
						mgr.winSendSystemMsg(window->winGetOwner(), GSM_SLIDER_TRACK, msgData(window), (WindowMsgData)s->position);
						child->winSetPosition((int)((s->position - s->minVal) * s->numTicks), HORIZONTAL_SLIDER_THUMB_POSITION);
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
				child->winSetPosition(0, HORIZONTAL_SLIDER_THUMB_POSITION);
				s->position = s->minVal;
			}
			else if (childCenter.x >= x + size.x - childSize.x / 2)
			{
				child->winSetPosition((int)((s->maxVal - s->minVal) * s->numTicks) - HORIZONTAL_SLIDER_THUMB_WIDTH / 2, HORIZONTAL_SLIDER_THUMB_POSITION);
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
				child->winSetPosition(childRelativePos.x, HORIZONTAL_SLIDER_THUMB_POSITION);
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
			child->winSetPosition(newPos, HORIZONTAL_SLIDER_THUMB_POSITION);
			break;
		}
		case GSM_SET_MIN_MAX:
		{
			GameWindow *child = window->winGetChild();
			s->minVal = (int)mData1;
			s->maxVal = (int)mData2;
			s->numTicks = (float)(size.x - HORIZONTAL_SLIDER_THUMB_WIDTH) / (float)(s->maxVal - s->minVal);
			s->position = s->minVal;
			child->winSetPosition(0, HORIZONTAL_SLIDER_THUMB_POSITION);
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
			const int height = (int)mData2;
			if (GameWindow *thumb = window->winGetChild())
			{
				thumb->winSetSize(GADGET_SIZE, height);
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

// W3DHorizontalSlider.cpp W3DGadgetHorizontalSliderImageDraw: the box meter
void W3DGadgetHorizontalSliderImageDraw(GameWindow *window, WinInstanceData *instData)
{
	GameWindowManager &mgr = window->manager();
	ICoord2D origin, size, start, end, highlightOffset;
	window->winGetScreenPosition(&origin.x, &origin.y);
	window->winGetSize(&size.x, &size.y);
	const std::string &highlightSquare = window->winGetHiliteImage(0);
	const std::string &blankSquare = window->winGetDisabledImage(1);
	const std::string &fillSquare = window->winGetDisabledImage(0);
	SliderData *s = static_cast<SliderData *>(window->winGetUserData());
	int fillW = 0, fillH = 0;
	if (fillSquare.empty() || !mgr.imageSize(fillSquare, fillW, fillH))
	{
		return; // ZH dereferences the image; an unknown or missing image draws nothing (the unresolved name is reported by the list)
	}
	const float xMulti = (float)mgr.displayWidth() / 800.0f;
	int numBoxes = 0;
	int numSelectedBoxes = 0;
	const int boxWidth = (int)(fillW * xMulti);
	const int boxPadding = 2;
	if (boxWidth <= 0)
	{
		return;
	}
	start.x = origin.x;
	end.x = start.x + boxWidth;
	const float selectedPercent = (float)(s->position - s->minVal) / (float)(s->maxVal - s->minVal);
	const int maxSelectedX = origin.x + (int)(selectedPercent * size.x);
	while (end.x < origin.x + size.x)
	{
		if (start.x <= maxSelectedX && end.x < origin.x + size.x && s->position != s->minVal)
		{
			++numSelectedBoxes;
		}
		start.x = end.x + boxPadding;
		end.x = start.x + boxWidth;
		++numBoxes;
	}
	const int numHighlightBoxes = numBoxes + 1;
	const int distanceCovered = end.x - origin.x - boxWidth;
	highlightOffset.x = -(boxWidth + boxPadding) / 2;
	highlightOffset.y = boxWidth / 3;
	const int blankness = size.x - distanceCovered;
	origin.x += blankness / 2;
	if (BitTest(instData->getState(), WIN_STATE_HILITED) && !highlightSquare.empty())
	{
		ICoord2D backgroundStart, backgroundEnd;
		backgroundStart.y = origin.y + highlightOffset.y;
		backgroundEnd.y = backgroundStart.y + boxWidth + boxPadding;
		for (int i = 0; i < numHighlightBoxes; ++i)
		{
			backgroundStart.x = origin.x + highlightOffset.x + i * (boxWidth + boxPadding);
			backgroundEnd.x = backgroundStart.x + boxWidth + boxPadding;
			mgr.winDrawImage(highlightSquare, backgroundStart.x, backgroundStart.y, backgroundEnd.x, backgroundEnd.y);
		}
	}
	start.y = origin.y;
	end.y = start.y + boxWidth;
	for (int i = 0; i < numSelectedBoxes; ++i)
	{
		start.x = origin.x + i * (boxWidth + boxPadding);
		end.x = start.x + boxWidth;
		mgr.winDrawImage(fillSquare, start.x, start.y, end.x, end.y);
	}
	for (int i = numSelectedBoxes; i < numBoxes; ++i)
	{
		start.x = origin.x + i * (boxWidth + boxPadding);
		end.x = start.x + boxWidth;
		mgr.winDrawImage(blankSquare, start.x, start.y, end.x, end.y);
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
