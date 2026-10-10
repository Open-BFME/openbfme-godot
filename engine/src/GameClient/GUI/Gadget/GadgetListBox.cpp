// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// Port of ZH GameClient/GUI/Gadget/GadgetListBox.cpp (single select) and GameEngineDevice W3DListBox.cpp.
//
// The rows are std::vector<ListEntryRow> (ZH memcpy-shifts an array); the row height / listHeight / displayPos arithmetic is ZH's, so
// scrolling and hit testing give ZH's numbers.  Text heights come from the window manager's font metrics (stop S-176).
// Not ported (stop S-177): GadgetListBoxMultiInput (the shift / control multi selection input: the selection data and the
// GLM_SET_SELECTION / GLM_TOGGLE_MULTI_SELECTION messages are), the type-ahead search by printable key (needs the keyboard layout), the click
// audio feedback.

#include "GameClient/GUI/Gadgets.h"

#include <algorithm>

namespace
{

constexpr std::uint32_t kDoubleClickTime = 500; // ZH GadgetListBox.cpp: `static const UnsignedInt doubleClickTime = 500` (ms)

ListboxData *listOf(GameWindow *window) { return static_cast<ListboxData *>(window->winGetUserData()); }

int getListboxEntryBasedOnCoord(GameWindow *window, int x, int y, int &row, int &column)
{
	WinInstanceData *instData = window->winGetInstanceData();
	ListboxData *list = listOf(window);
	int winx, winy;
	window->winGetScreenPosition(&winx, &winy);
	if (instData->getTextLength())
	{
		winy += window->manager().winFontHeight(instData->getFont()) + 1;
	}
	int pos = -2;
	int i;
	for (i = 0;; ++i)
	{
		if (i > 0 && list->listData[(std::size_t)i - 1].listHeight > (list->displayPos + list->displayHeight))
		{
			pos = -1;
			break;
		}
		if (i == list->endPos)
		{
			pos = -1;
			break;
		}
		if (list->listData[(std::size_t)i].listHeight > (y - winy + list->displayPos))
		{
			break;
		}
	}
	column = -1;
	if (pos == -2)
	{
		pos = i;
		int total = 0;
		for (i = 0; i < list->columns; ++i)
		{
			total += list->columnWidth[(std::size_t)i];
			if (x - winx < total)
			{
				column = i;
				break;
			}
		}
	}
	row = pos;
	return pos;
}

int getListboxTopEntry(ListboxData *list)
{
	for (int entry = 0;; ++entry)
	{
		if (list->listData[(std::size_t)entry].listHeight > list->displayPos)
		{
			return entry;
		}
		if (entry >= list->endPos)
		{
			return 0;
		}
	}
}

int getListboxBottomEntry(ListboxData *list)
{
	for (int entry = list->endPos - 1;; --entry)
	{
		if (entry < 0)
		{
			return 0;
		}
		if (list->listData[(std::size_t)entry].listHeight == list->displayPos + list->displayHeight)
		{
			return entry;
		}
		if (list->listData[(std::size_t)entry].listHeight < list->displayPos + list->displayHeight && entry != list->endPos - 1)
		{
			return entry + 1;
		}
		if (list->listData[(std::size_t)entry].listHeight < list->displayPos + list->displayHeight)
		{
			return entry;
		}
	}
}

void removeSelection(ListboxData *list, int i)
{
	for (int k = i; k < list->listLength - 1; ++k)
	{
		list->selections[(std::size_t)k] = list->selections[(std::size_t)k + 1];
	}
	list->selections[(std::size_t)list->listLength - 1] = -1;
}

void adjustDisplay(GameWindow *window, int adjustment, bool updateSlider)
{
	ListboxData *list = listOf(window);
	int entry = getListboxTopEntry(list) + adjustment;
	if (entry < 0)
	{
		entry = 0;
	}
	else if (entry >= list->endPos)
	{
		entry = list->endPos - 1;
	}
	if (updateSlider)
	{
		if (entry > 0)
		{
			list->displayPos = (short)(list->listData[(std::size_t)entry - 1].listHeight + 1);
		}
		else
		{
			list->displayPos = 0;
		}
	}
	if (list->slider != nullptr)
	{
		SliderData *sData = static_cast<SliderData *>(list->slider->winGetUserData());
		ICoord2D sliderSize, sliderChildSize;
		list->slider->winGetSize(&sliderSize.x, &sliderSize.y);
		sData->maxVal = list->totalHeight - (list->displayHeight - TOTAL_OUTLINE_HEIGHT) + 1;
		if (sData->maxVal < 0)
		{
			sData->maxVal = 0;
		}
		GameWindow *child = list->slider->winGetChild();
		child->winGetSize(&sliderChildSize.x, &sliderChildSize.y);
		sData->numTicks = (float)((sliderSize.y - sliderChildSize.y) / (float)sData->maxVal);
		if (updateSlider)
		{
			window->manager().winSendSystemMsg(list->slider, GSM_SET_SLIDER, (WindowMsgData)(sData->maxVal - list->displayPos), 0);
		}
	}
}

void computeTotalHeight(GameWindow *window)
{
	ListboxData *list = listOf(window);
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindowManager &mgr = window->manager();
	int height = 0;
	for (int i = 0; i < list->endPos; ++i)
	{
		ListEntryRow &row = list->listData[(std::size_t)i];
		if (!row.hasCells)
		{
			continue;
		}
		int tempHeight = 0;
		for (int j = 0; j < list->columns; ++j)
		{
			int cellHeight = 0;
			const ListEntryCell &cell = row.cell[(std::size_t)j];
			if (cell.cellType == LISTBOX_TEXT)
			{
				if (BitTest(window->winGetStatus(), WIN_STATUS_ONE_LINE))
				{
					cellHeight = mgr.winFontHeight(instData->getFont());
				}
				else
				{
					const int wrap = (std::size_t)j < list->columnWidth.size() ? list->columnWidth[(std::size_t)j] - TEXT_WIDTH_OFFSET : 0;
					cellHeight = mgr.winWrappedHeight(instData->getFont(), cell.text, wrap);
				}
			}
			else if (cell.cellType == LISTBOX_IMAGE)
			{
				if (cell.height > 0)
				{
					cellHeight = cell.height + 1;
				}
				else
				{
					cellHeight = mgr.winFontHeight(instData->getFont());
				}
			}
			if (cellHeight > tempHeight)
			{
				tempHeight = cellHeight;
			}
		}
		row.height = tempHeight;
		height += (row.height + 1);
		row.listHeight = height;
	}
	list->totalHeight = height;
	adjustDisplay(window, 0, true);
}

ListEntryRow &prepareRow(ListboxData *list, int row)
{
	ListEntryRow &r = list->listData[(std::size_t)row];
	if (!r.hasCells)
	{
		r.cell.assign((std::size_t)list->columns, ListEntryCell());
		r.hasCells = true;
	}
	return r;
}

int addImageEntry(const std::string &image, Color color, int row, int column, GameWindow *window, bool overwrite, int width, int height)
{
	(void)overwrite;
	ListboxData *list = listOf(window);
	if (column >= list->columns || row >= list->listLength)
	{
		return -1;
	}
	if (row == -1)
	{
		row = list->insertPos;
		list->insertPos++;
		list->endPos++;
	}
	if (column == -1)
	{
		column = 0;
	}
	ListEntryRow &listRow = prepareRow(list, row);
	ListEntryCell &cell = listRow.cell[(std::size_t)column];
	cell.text.clear();
	cell.cellType = LISTBOX_IMAGE;
	cell.image = image;
	cell.color = color;
	cell.height = height;
	cell.width = width;
	computeTotalHeight(window);
	return row;
}

int moveRowsDown(ListboxData *list, int startingRow)
{
	for (int i = list->endPos; i > startingRow; --i)
	{
		list->listData[(std::size_t)i] = std::move(list->listData[(std::size_t)i - 1]);
	}
	list->endPos++;
	list->insertPos = list->endPos;
	list->listData[(std::size_t)startingRow] = ListEntryRow();
	if (list->multiSelect)
	{
		for (int i = 0; list->selections[(std::size_t)i] >= 0; ++i)
		{
			if (startingRow <= list->selections[(std::size_t)i])
			{
				list->selections[(std::size_t)i]++;
			}
		}
	}
	else if (list->selectPos >= startingRow)
	{
		list->selectPos++;
	}
	return 1;
}

int addEntry(const UnicodeString &string, Color color, int row, int column, GameWindow *window, bool overwrite)
{
	ListboxData *list = listOf(window);
	GameWindowManager &mgr = window->manager();
	if (column >= list->columns || row >= list->listLength)
	{
		return -1;
	}
	if (row == -1)
	{
		row = list->insertPos;
		list->insertPos++;
		list->endPos++;
	}
	if (column == -1)
	{
		column = 0;
	}
	const int width = list->columnWidth[(std::size_t)column] - TEXT_WIDTH_OFFSET;
	int rowsAdded = 0;
	ListEntryRow *listRow = &list->listData[(std::size_t)row];
	if (!listRow->hasCells)
	{
		listRow->cell.assign((std::size_t)list->columns, ListEntryCell());
		listRow->hasCells = true;
		rowsAdded = 1;
	}
	else if (!overwrite)
	{
		moveRowsDown(list, row);
		listRow = &list->listData[(std::size_t)row];
		listRow->cell.assign((std::size_t)list->columns, ListEntryCell());
		listRow->hasCells = true;
		rowsAdded = 1;
	}
	ListEntryCell &cell = listRow->cell[(std::size_t)column];
	cell.cellType = LISTBOX_TEXT;
	cell.color = color;
	cell.text = string;
	cell.image.clear();
	if (overwrite)
	{
		const int oldRowHeight = listRow->height;
		int oldTotalHeight = listRow->listHeight;
		if (!oldTotalHeight && row)
		{
			oldTotalHeight = list->listData[(std::size_t)row - 1].listHeight;
		}
		int rowHeight;
		if (BitTest(window->winGetStatus(), WIN_STATUS_ONE_LINE))
		{
			rowHeight = mgr.winFontHeight(window->winGetFont());
		}
		else
		{
			rowHeight = mgr.winWrappedHeight(window->winGetFont(), string, width);
		}
		if (rowHeight > oldRowHeight)
		{
			const int totalHeight = oldTotalHeight + (rowHeight - oldRowHeight);
			listRow->height = rowHeight;
			listRow->listHeight = totalHeight + rowsAdded;
			list->totalHeight += (rowHeight - oldRowHeight) + rowsAdded;
			adjustDisplay(window, 0, true);
		}
	}
	else
	{
		computeTotalHeight(window);
	}
	return row;
}

// The shared body of the left-click selection and the right-click lookup.
int rowAtMouse(GameWindow *window, int mousey)
{
	ListboxData *list = listOf(window);
	WinInstanceData *instData = window->winGetInstanceData();
	int x, y;
	window->winGetScreenPosition(&x, &y);
	if (instData->getTextLength())
	{
		y += window->manager().winFontHeight(instData->getFont()) + 1;
	}
	int i;
	for (i = 0;; ++i)
	{
		if (i > 0 && list->listData[(std::size_t)i - 1].listHeight > (list->displayPos + list->displayHeight))
		{
			return -1;
		}
		if (i == list->endPos)
		{
			return -1;
		}
		if (list->listData[(std::size_t)i].listHeight > (mousey - y + list->displayPos))
		{
			return i;
		}
	}
}

} // namespace

int GadgetListBoxGetEntryBasedOnXY(GameWindow *listbox, int x, int y, int &row, int &column)
{
	return getListboxEntryBasedOnCoord(listbox, x, y, row, column);
}

WindowMsgHandledType GadgetListBoxInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	ListboxData *list = listOf(window);
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindowManager &mgr = window->manager();
	switch (msg)
	{
		case GWM_CHAR:
			switch (mData1)
			{
				case KEY_ENTER:
				case KEY_SPACE:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_UP))
					{
						mgr.winSendSystemMsg(window->winGetOwner(), GLM_DOUBLE_CLICKED, msgData(window), (WindowMsgData)list->selectPos);
					}
					break;
				case KEY_DOWN:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						if (list->selectPos == -1)
						{
							list->selectPos = 0;
							adjustDisplay(window, 0, true);
						}
						else if (list->selectPos < list->endPos - 1)
						{
							list->selectPos++;
							for (;;)
							{
								const int cellBottom = list->listData[(std::size_t)list->selectPos].listHeight;
								const int cellTop = cellBottom - list->listData[(std::size_t)list->selectPos].height;
								const int displayTop = list->displayPos;
								const int displayBottom = list->displayPos + list->displayHeight - 1;
								if (cellTop < displayTop)
								{
									adjustDisplay(window, -1, true);
								}
								else if (cellBottom < displayBottom)
								{
									adjustDisplay(window, 0, true);
									break;
								}
								else
								{
									adjustDisplay(window, 1, true);
								}
							}
						}
						mgr.winSendSystemMsg(window->winGetOwner(), GLM_SELECTED, msgData(window), (WindowMsgData)(std::intptr_t)list->selectPos);
					}
					break;
				case KEY_UP:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						if (list->selectPos == -1)
						{
							list->selectPos = 0;
							adjustDisplay(window, 0, true);
						}
						else if (list->selectPos > 0)
						{
							list->selectPos--;
							for (;;)
							{
								ListEntryRow &r = list->listData[(std::size_t)list->selectPos];
								if (r.listHeight - r.height < list->displayPos)
								{
									list->displayPos = (short)(r.listHeight + 1);
									adjustDisplay(window, -1, true);
								}
								else if (r.listHeight > list->displayPos + list->displayHeight)
								{
									list->displayPos = (short)(r.listHeight - list->displayHeight);
									adjustDisplay(window, 1, true);
								}
								else
								{
									adjustDisplay(window, 0, true);
									break;
								}
							}
						}
						mgr.winSendSystemMsg(window->winGetOwner(), GLM_SELECTED, msgData(window), (WindowMsgData)(std::intptr_t)list->selectPos);
					}
					break;
				case KEY_RIGHT:
				case KEY_TAB:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						mgr.winNextTab(window);
					}
					break;
				case KEY_LEFT:
					if (BitTest((std::uint32_t)mData2, KEY_STATE_DOWN))
					{
						mgr.winPrevTab(window);
					}
					break;
				default:
					return MSG_IGNORED; // the type-ahead search by printable key is not ported [S-177]
			}
			break;
		case GWM_WHEEL_DOWN:
			if (list->endPos <= 0)
			{
				break;
			}
			if (list->listData[(std::size_t)list->endPos - 1].listHeight > list->displayHeight + list->displayPos)
			{
				adjustDisplay(window, 1, true);
			}
			break;
		case GWM_WHEEL_UP:
			if (list->endPos <= 0)
			{
				break;
			}
			adjustDisplay(window, -1, true);
			break;
		case GWM_LEFT_UP:
		{
			mgr.winSetFocus(window);
			const int mousey = (int)(mData1 >> 16);
			const int oldPos = list->selectPos;
			int x, y;
			window->winGetScreenPosition(&x, &y);
			if (instData->getTextLength())
			{
				y += mgr.winFontHeight(instData->getFont()) + 1;
			}
			list->selectPos = -2;
			int i;
			for (i = 0;; ++i)
			{
				if (i > 0 && list->listData[(std::size_t)i - 1].listHeight > (list->displayPos + list->displayHeight))
				{
					list->selectPos = -1;
					break;
				}
				if (i == list->endPos)
				{
					list->selectPos = -1;
					break;
				}
				if (list->listData[(std::size_t)i].listHeight > (mousey - y + list->displayPos))
				{
					break;
				}
			}
			if (list->doubleClickTime + kDoubleClickTime > mgr.timeMs() && (i == oldPos || (oldPos == -1 && (i >= 0 && i < list->endPos))))
			{
				list->doubleClickTime = 0;
				const int temp = oldPos == -1 ? i : oldPos;
				mgr.winSendSystemMsg(window->winGetOwner(), GLM_DOUBLE_CLICKED, msgData(window), (WindowMsgData)(std::intptr_t)temp);
			}
			if (i == oldPos && !list->forceSelect)
			{
				list->selectPos = -1;
			}
			if (list->selectPos == -2 && i < list->endPos)
			{
				list->selectPos = i;
			}
			if (list->selectPos < 0 && list->forceSelect)
			{
				list->selectPos = oldPos;
			}
			list->doubleClickTime = mgr.timeMs();
			mgr.winSendSystemMsg(window->winGetOwner(), GLM_SELECTED, msgData(window), (WindowMsgData)(std::intptr_t)list->selectPos);
			break;
		}
		case GWM_RIGHT_DOWN:
			break;
		case GWM_RIGHT_UP:
		{
			mgr.winSetFocus(window);
			const int mousex = (int)(mData1 & 0xFFFF);
			const int mousey = (int)(mData1 >> 16);
			RightClickStruct rc;
			rc.pos = rowAtMouse(window, mousey);
			rc.mouseX = mousex;
			rc.mouseY = mousey;
			mgr.winSendSystemMsg(window->winGetOwner(), GLM_RIGHT_CLICKED, msgData(window), msgData(&rc));
			break;
		}
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
		case GWM_LEFT_DOWN:
			return MSG_HANDLED;
		case GWM_MOUSE_POS:
		{
			// lane FB7-1: RW 0x727081 case 0x18: a list that follows the pointer (+0x12) takes the row under it (RW 0x7258E1: -1 past the last row)
			// when no other window holds the mouse; the message RotWK then sends on (0x18 through the manager's +0xE8) is not ported [S-1916]
			GameWindow *grab = mgr.winGetGrabWindow();
			if (!list->trackHover || (grab && grab != window && !window->winIsChild(grab)))
			{
				return MSG_IGNORED;
			}
			list->hoverPos = rowAtMouse(window, (int)((mData1 >> 16) & 0xFFFF));
			return MSG_HANDLED;
		}
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

WindowMsgHandledType GadgetListBoxSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	ListboxData *list = listOf(window);
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindowManager &mgr = window->manager();
	switch (msg)
	{
		case GGM_SET_LABEL:
			instData->setText(*msgPtr<UnicodeString>(mData1));
			break;
		case GLM_GET_TEXT:
		{
			ICoord2D *pos = msgPtr<ICoord2D>(mData1);
			TextAndColor *tAndC = msgPtr<TextAndColor>(mData2);
			if (pos->x >= list->columns || pos->y >= list->listLength || !list->listData[(std::size_t)pos->y].hasCells || list->listData[(std::size_t)pos->y].cell[(std::size_t)pos->x].cellType != LISTBOX_TEXT)
			{
				tAndC->string.clear();
				tAndC->color = 0;
			}
			else
			{
				tAndC->string = list->listData[(std::size_t)pos->y].cell[(std::size_t)pos->x].text;
				tAndC->color = list->listData[(std::size_t)pos->y].cell[(std::size_t)pos->x].color;
			}
			break;
		}
		case GBM_SELECTED:
		case GGM_LEFT_DRAG:
		{
			GameWindow *src = msgPtr<GameWindow>(mData1);
			if (src == list->upButton)
			{
				if (list->displayPos > 0)
				{
					adjustDisplay(window, -1, true);
				}
			}
			else if (src == list->downButton)
			{
				if (list->displayPos + list->displayHeight <= list->totalHeight)
				{
					adjustDisplay(window, 1, true);
				}
			}
			break;
		}
		case GLM_DEL_ALL:
		{
			for (int i = 0; i < list->listLength; ++i)
			{
				list->listData[(std::size_t)i] = ListEntryRow();
			}
			if (mData1 != GP_DONT_UPDATE)
			{
				list->displayPos = 0;
			}
			if (list->multiSelect)
			{
				std::fill(list->selections.begin(), list->selections.end(), -1);
			}
			else
			{
				list->selectPos = -1;
			}
			list->insertPos = 0;
			list->endPos = 0;
			list->totalHeight = 0;
			adjustDisplay(window, 0, true);
			break;
		}
		case GLM_DEL_ENTRY:
		{
			const int at = (int)mData1;
			if (list->endPos <= at)
			{
				break;
			}
			for (int i = at; i < list->endPos - 1; ++i)
			{
				list->listData[(std::size_t)i] = std::move(list->listData[(std::size_t)i + 1]);
			}
			list->listData[(std::size_t)list->endPos - 1] = ListEntryRow();
			list->endPos--;
			list->insertPos = list->endPos;
			if (list->multiSelect)
			{
				for (int i = 0; list->selections[(std::size_t)i] >= 0; ++i)
				{
					if (at < list->selections[(std::size_t)i])
					{
						list->selections[(std::size_t)i]--;
					}
					else if (at == list->selections[(std::size_t)i])
					{
						removeSelection(list, i);
						--i;
					}
				}
			}
			else
			{
				if (at < list->selectPos)
				{
					list->selectPos--;
				}
				else if (at == list->selectPos)
				{
					list->selectPos = -1;
				}
			}
			computeTotalHeight(window);
			break;
		}
		case GLM_ADD_ENTRY:
		{
			bool success = true;
			int addedIndex = -1;
			AddMessageStruct *addInfo = msgPtr<AddMessageStruct>(mData1);
			if (addInfo->row >= list->insertPos)
			{
				addInfo->row = -1;
			}
			int row = addInfo->row;
			if (addInfo->row == -1 && list->insertPos == list->listLength)
			{
				row = list->insertPos;
				if (list->autoPurge)
				{
					mgr.winSendSystemMsg(window, GLM_SCROLL_BUFFER, 1, 0);
				}
				else
				{
					success = false;
				}
			}
			else if (addInfo->row != -1 && !addInfo->overwrite && list->insertPos == list->listLength)
			{
				if (list->autoPurge)
				{
					mgr.winSendSystemMsg(window, GLM_SCROLL_BUFFER, 1, 0);
				}
				else
				{
					success = false;
				}
			}
			if (success)
			{
				if (addInfo->type == LISTBOX_TEXT)
				{
					addedIndex = addEntry(*static_cast<const UnicodeString *>(addInfo->data), (Color)mData2, addInfo->row, addInfo->column, window, addInfo->overwrite);
				}
				else if (addInfo->type == LISTBOX_IMAGE)
				{
					addedIndex = addImageEntry(*static_cast<const std::string *>(addInfo->data), (Color)mData2, addInfo->row, addInfo->column, window, addInfo->overwrite, addInfo->width, addInfo->height);
				}
				else
				{
					success = false;
				}
			}
			if (success)
			{
				if (list->autoScroll)
				{
					for (;;)
					{
						if (row == -1)
						{
							if (list->listData[(std::size_t)list->insertPos - 1].listHeight >= (list->displayPos + list->displayHeight))
							{
								adjustDisplay(window, 1, true);
							}
							else
							{
								break;
							}
						}
						else
						{
							if (list->listData[(std::size_t)row].listHeight >= (list->displayPos + list->displayHeight))
							{
								adjustDisplay(window, 1, true);
							}
							else
							{
								break;
							}
						}
					}
				}
				if (list->multiSelect)
				{
					// ZH: `if( (row = list->selections[i]) != 0 ) list->selections[i] = -1;` (an assignment in the condition, kept)
					for (int i = 0; list->selections[(std::size_t)i] >= 0; ++i)
					{
						if ((row = list->selections[(std::size_t)i]) != 0)
						{
							list->selections[(std::size_t)i] = -1;
						}
					}
				}
				else if (row == list->selectPos)
				{
					list->selectPos = -1;
				}
			}
			return (WindowMsgHandledType)addedIndex;
		}
		case GLM_TOGGLE_MULTI_SELECTION:
		{
			const int at = (int)mData1;
			if (at < 0)
			{
				if (list->multiSelect)
				{
					std::fill(list->selections.begin(), list->selections.end(), -1);
				}
				break;
			}
			if (!list->listData[(std::size_t)at].hasCells)
			{
				break;
			}
			if (list->multiSelect)
			{
				int i = 0;
				bool removed = false;
				while (list->selections[(std::size_t)i] >= 0)
				{
					if (list->selections[(std::size_t)i] == at)
					{
						removeSelection(list, i);
						removed = true;
						break;
					}
					++i;
				}
				if (!removed)
				{
					list->selections[(std::size_t)i] = at;
					list->selections[(std::size_t)i + 1] = -1;
				}
			}
			break;
		}
		case GLM_SET_SELECTION:
		{
			const int *selectList = msgPtr<const int>(mData1);
			const int selectCount = (int)mData2;
			if (selectList[0] < 0 || list->listLength <= selectList[0])
			{
				if (list->multiSelect)
				{
					std::fill(list->selections.begin(), list->selections.end(), -1);
				}
				else
				{
					list->selectPos = -1;
				}
				mgr.winSendSystemMsg(window->winGetOwner(), GLM_SELECTED, msgData(window), (WindowMsgData)(std::intptr_t)list->selectPos);
				break;
			}
			if (list->multiSelect)
			{
				int i;
				for (i = 0; i < selectCount && i < list->endPos; ++i)
				{
					if (list->listLength <= selectList[i])
					{
						break;
					}
					if (!list->listData[(std::size_t)selectList[i]].hasCells)
					{
						break;
					}
					list->selections[(std::size_t)i] = selectList[i];
				}
				list->selections[(std::size_t)i] = -1;
			}
			else
			{
				if (!list->listData[(std::size_t)selectList[0]].hasCells)
				{
					break;
				}
				list->selectPos = selectList[0];
				GameWindow *parent = window->winGetParent();
				if (parent && BitTest(parent->winGetStyle(), GWS_COMBO_BOX))
				{
					mgr.winSendSystemMsg(window->winGetOwner(), GLM_SELECTED, msgData(window), (WindowMsgData)(std::intptr_t)list->selectPos);
					break;
				}
				if (list->listData[(std::size_t)list->selectPos].listHeight < list->displayPos)
				{
					mgr.winSendSystemMsg(window, GLM_UPDATE_DISPLAY, (WindowMsgData)list->selectPos, 0);
				}
				else if (list->listData[(std::size_t)list->selectPos].listHeight > (list->displayPos + list->displayHeight))
				{
					if (list->selectPos > 0)
					{
						list->displayPos = (short)(list->listData[(std::size_t)list->selectPos].listHeight - list->displayHeight);
					}
					else
					{
						list->displayPos = 0;
					}
					adjustDisplay(window, 0, true);
				}
			}
			mgr.winSendSystemMsg(window->winGetOwner(), GLM_SELECTED, msgData(window), (WindowMsgData)(std::intptr_t)list->selectPos);
			break;
		}
		case GLM_SCROLL_BUFFER:
		{
			const int n = (int)mData1;
			if (list->endPos < n)
			{
				break;
			}
			for (int i = 0; i < list->endPos - n; ++i)
			{
				list->listData[(std::size_t)i] = std::move(list->listData[(std::size_t)i + (std::size_t)n]);
			}
			for (int i = list->endPos - n; i < list->endPos; ++i)
			{
				list->listData[(std::size_t)i] = ListEntryRow();
			}
			list->endPos = (short)(list->endPos - n);
			list->insertPos = list->endPos;
			if (list->multiSelect)
			{
				for (int i = 0; list->selections[(std::size_t)i] >= 0; ++i)
				{
					if (n >= list->selections[(std::size_t)i])
					{
						list->selections[(std::size_t)i] -= n; // ZH: kept (a selection inside the purged rows goes negative and ends the list)
					}
					else
					{
						removeSelection(list, i);
						--i;
					}
				}
			}
			else if (list->selectPos > 0)
			{
				list->selectPos -= n;
			}
			if (list->displayPos > 0)
			{
				adjustDisplay(window, -1 * n, true);
			}
			computeTotalHeight(window);
			break;
		}
		case GLM_GET_SELECTION:
			if (list->multiSelect)
			{
				// ZH hands out the selections array pointer through the int*; here the caller's int array receives the selection list
				int *out = msgPtr<int>(mData2);
				for (std::size_t i = 0; i < list->selections.size(); ++i)
				{
					out[i] = list->selections[i];
				}
			}
			else
			{
				*msgPtr<int>(mData2) = list->selectPos;
			}
			break;
		case GLM_SET_UP_BUTTON:
			list->upButton = msgPtr<GameWindow>(mData1);
			break;
		case GLM_SET_DOWN_BUTTON:
			list->downButton = msgPtr<GameWindow>(mData1);
			break;
		case GLM_SET_SLIDER:
			list->slider = msgPtr<GameWindow>(mData1);
			break;
		case GWM_CREATE:
			break;
		case GGM_RESIZED:
		{
			if (list == nullptr)
			{
				break;
			}
			const int width = (int)mData1;
			const int height = (int)mData2;
			ICoord2D downSize = { 0, 0 }, upSize = { 0, 0 }, sliderSize = { 0, 0 };
			if (list->downButton)
			{
				list->downButton->winGetSize(&downSize.x, &downSize.y);
			}
			if (list->upButton)
			{
				list->upButton->winGetSize(&upSize.x, &upSize.y);
			}
			if (list->slider)
			{
				list->slider->winGetSize(&sliderSize.x, &sliderSize.y);
			}
			if (list->upButton)
			{
				list->upButton->winSetPosition(width - upSize.x - 2, 2);
			}
			if (list->downButton)
			{
				list->downButton->winSetPosition(width - downSize.x - 2, height - downSize.y - 2);
			}
			if (list->slider)
			{
				list->slider->winSetSize(sliderSize.x, height - (2 * upSize.y) - 6);
				list->slider->winSetPosition(width - sliderSize.x - 2, upSize.y + 3);
			}
			list->displayHeight = (short)height;
			if (instData->getTextLength())
			{
				list->displayHeight = (short)(list->displayHeight - mgr.winFontHeight(instData->getFont()));
			}
			if (list->columns == 1)
			{
				if (list->columnWidth.empty())
				{
					list->columnWidth.assign(1, 0);
				}
				list->columnWidth[0] = width;
				if (list->slider)
				{
					int sw, sh;
					list->slider->winGetSize(&sw, &sh);
					list->columnWidth[0] -= sw;
				}
			}
			else
			{
				if (list->columnWidthPercentage.empty() || list->columnWidth.empty())
				{
					break;
				}
				int totalWidth = width;
				if (list->slider)
				{
					int sw, sh;
					list->slider->winGetSize(&sw, &sh);
					totalWidth -= sw;
				}
				for (int i = 0; i < list->columns; ++i)
				{
					list->columnWidth[(std::size_t)i] = list->columnWidthPercentage[(std::size_t)i] * totalWidth / 100;
				}
			}
			computeTotalHeight(window);
			break;
		}
		case GLM_UPDATE_DISPLAY:
		{
			const int at = (int)mData1;
			if (at > 0)
			{
				list->displayPos = (short)(list->listData[(std::size_t)at - 1].listHeight + 1);
			}
			else
			{
				list->displayPos = 0;
			}
			if (list->displayPos + list->displayHeight >= list->totalHeight)
			{
				list->displayPos = (short)(list->totalHeight - list->displayHeight);
			}
			adjustDisplay(window, 0, true);
			break;
		}
		case GWM_DESTROY:
			delete list;
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
		case GSM_SLIDER_TRACK:
		{
			SliderData *sData = static_cast<SliderData *>(list->slider->winGetUserData());
			list->displayPos = (short)(sData->maxVal - (int)mData2);
			if (list->displayPos > (list->totalHeight - list->displayHeight + 1))
			{
				list->displayPos = (short)(list->totalHeight - list->displayHeight + 1);
			}
			if (list->displayPos < 0)
			{
				list->displayPos = 0;
			}
			adjustDisplay(window, 0, false);
			break;
		}
		case GLM_SET_ITEM_DATA:
		{
			ICoord2D *pos = msgPtr<ICoord2D>(mData1);
			if (pos->y >= 0 && pos->y < list->endPos && list->listData[(std::size_t)pos->y].hasCells)
			{
				list->listData[(std::size_t)pos->y].cell[(std::size_t)pos->x].userData = reinterpret_cast<void *>(mData2);
			}
			break;
		}
		case GLM_GET_ITEM_DATA:
		{
			ICoord2D *pos = msgPtr<ICoord2D>(mData1);
			void **data = msgPtr<void *>(mData2);
			*data = nullptr;
			if (pos->y >= 0 && pos->y < list->endPos && list->listData[(std::size_t)pos->y].hasCells)
			{
				*data = list->listData[(std::size_t)pos->y].cell[(std::size_t)pos->x].userData;
			}
			break;
		}
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

// ---- public API -------------------------------------------------------------------------------------------------------------------------------------

UnicodeString GadgetListBoxGetText(GameWindow *listbox, int row, int column)
{
	Color color;
	return GadgetListBoxGetTextAndColor(listbox, &color, row, column);
}

UnicodeString GadgetListBoxGetTextAndColor(GameWindow *listbox, Color *color, int row, int column)
{
	ICoord2D pos;
	pos.x = column;
	pos.y = row;
	TextAndColor tAndC;
	listbox->manager().winSendSystemMsg(listbox, GLM_GET_TEXT, msgData(&pos), msgData(&tAndC));
	*color = tAndC.color;
	return tAndC.string;
}

int GadgetListBoxAddEntryText(GameWindow *listbox, UnicodeString text, Color color, int row, int column, bool overwrite)
{
	if (!listbox)
	{
		return -1;
	}
	if (text.empty())
	{
		text = UnicodeString(1, u' '); // ZH: UnicodeString(L" ")
	}
	AddMessageStruct addInfo;
	addInfo.row = row;
	addInfo.column = column;
	addInfo.type = LISTBOX_TEXT;
	addInfo.data = &text;
	addInfo.overwrite = overwrite;
	addInfo.height = -1;
	addInfo.width = -1;
	ListboxData *listData = listOf(listbox);
	if (!listData)
	{
		return -1; // lane END-1: a window without list box data (a gadget the movie has not finished building, or one that was destroyed) takes no entries
	}
	const bool wasFull = (listData->listLength <= listData->endPos);
	const int newEntryOffset = wasFull ? 0 : 1;
	const int oldBottomIndex = GadgetListBoxGetBottomVisibleEntry(listbox);
	const int index = (int)(std::intptr_t)listbox->manager().winSendSystemMsg(listbox, GLM_ADD_ENTRY, msgData(&addInfo), (WindowMsgData)color);
	if (listData->scrollIfAtEnd && index - oldBottomIndex == newEntryOffset && GadgetListBoxIsFull(listbox))
	{
		GadgetListBoxSetBottomVisibleEntry(listbox, index);
	}
	return index;
}

int GadgetListBoxAddEntryImage(GameWindow *listbox, const std::string &image, int row, int column, bool overwrite, Color color)
{
	return GadgetListBoxAddEntryImage(listbox, image, row, column, -1, -1, overwrite, color);
}

int GadgetListBoxAddEntryImage(GameWindow *listbox, const std::string &image, int row, int column, int height, int width, bool overwrite, Color color)
{
	if (!listbox)
	{
		return -1;
	}
	AddMessageStruct addInfo;
	addInfo.row = row;
	addInfo.column = column;
	addInfo.type = LISTBOX_IMAGE;
	addInfo.data = &image;
	addInfo.overwrite = overwrite;
	addInfo.height = height;
	addInfo.width = width;
	return (int)(std::intptr_t)listbox->manager().winSendSystemMsg(listbox, GLM_ADD_ENTRY, msgData(&addInfo), (WindowMsgData)color);
}

void GadgetListBoxSetFont(GameWindow *g, GameFont *font)
{
	// ZH GadgetListBoxSetFont: the cell display strings and the title take the font (here the cells draw with the window font)
	ListboxData *list = listOf(g);
	if (!list)
	{
		return;
	}
	(void)font;
	if (list->listData.size() && list->endPos > 0)
	{
		computeTotalHeight(g);
	}
}

void GadgetListboxCreateScrollbar(GameWindow *listbox)
{
	ListboxData *listData = listOf(listbox);
	GameWindowManager &mgr = listbox->manager();
	WinInstanceData winInstData;
	SliderData sData;
	std::uint32_t status = listbox->winGetStatus();
	int width, height;
	listbox->winGetSize(&width, &height);
	const bool title = listbox->winGetTextLength() != 0;
	status &= ~(WIN_STATUS_BORDER | WIN_STATUS_HIDDEN | WIN_STATUS_NO_INPUT);
	const int fontHeight = mgr.winFontHeight(listbox->winGetFont());
	const int top = title ? (fontHeight + 1) : 0;
	const int bottom = title ? (height - (fontHeight + 1)) : height;
	winInstData.init();
	const int buttonWidth = 21;
	const int buttonHeight = 22;
	status |= WIN_STATUS_IMAGE;
	winInstData.m_owner = listbox;
	winInstData.m_style = GWS_PUSH_BUTTON;
	if (BitTest(listbox->winGetStyle(), GWS_MOUSE_TRACK))
	{
		BitSet(winInstData.m_style, GWS_MOUSE_TRACK);
	}
	listData->upButton = mgr.gogoGadgetPushButton(listbox, status | WIN_STATUS_ACTIVE | WIN_STATUS_ENABLED, width - buttonWidth - 2, top + 2, buttonWidth, buttonHeight, &winInstData, nullptr, true);
	winInstData.init();
	winInstData.m_style = GWS_PUSH_BUTTON;
	winInstData.m_owner = listbox;
	if (BitTest(listbox->winGetStyle(), GWS_MOUSE_TRACK))
	{
		BitSet(winInstData.m_style, GWS_MOUSE_TRACK);
	}
	listData->downButton = mgr.gogoGadgetPushButton(listbox, status | WIN_STATUS_ACTIVE | WIN_STATUS_ENABLED, width - buttonWidth - 2, (top + bottom - buttonHeight - 2), buttonWidth, buttonHeight, &winInstData, nullptr, true);
	const int sliderButtonWidth = buttonWidth;
	winInstData.init();
	winInstData.m_style = GWS_VERT_SLIDER;
	winInstData.m_owner = listbox;
	if (BitTest(listbox->winGetStyle(), GWS_MOUSE_TRACK))
	{
		BitSet(winInstData.m_style, GWS_MOUSE_TRACK);
	}
	listData->slider = mgr.gogoGadgetSlider(listbox, status | WIN_STATUS_ACTIVE | WIN_STATUS_ENABLED, width - sliderButtonWidth - 2, (top + buttonHeight + 3), sliderButtonWidth, bottom - (2 * buttonHeight) - 6, &winInstData, &sData, nullptr, true);
	listData->scrollBar = true;
}

void GadgetListBoxAddMultiSelect(GameWindow *listbox)
{
	ListboxData *listData = listOf(listbox);
	listData->selections.assign((std::size_t)listData->listLength + 1, -1);
	listData->multiSelect = true;
	listbox->winSetInputFunc(GadgetListBoxInput); // ZH installs GadgetListBoxMultiInput [S-177]
}

void GadgetListBoxRemoveMultiSelect(GameWindow *listbox)
{
	ListboxData *listData = listOf(listbox);
	listData->selections.clear();
	listData->multiSelect = false;
	listData->selectPos = -1;
	listbox->winSetInputFunc(GadgetListBoxInput);
}

void GadgetListBoxSetListLength(GameWindow *listbox, int newLength)
{
	ListboxData *listboxData = listOf(listbox);
	if (!listboxData || listboxData->columns < 1)
	{
		return;
	}
	if (newLength < listboxData->listLength)
	{
		if (listboxData->displayPos > newLength)
		{
			listboxData->displayPos = (short)newLength;
		}
		if (listboxData->selectPos > newLength || listboxData->multiSelect)
		{
			listboxData->selectPos = -1;
		}
		if (listboxData->insertPos > newLength)
		{
			listboxData->insertPos = (short)newLength;
		}
		listboxData->endPos = (short)newLength;
	}
	listboxData->listData.resize((std::size_t)newLength);
	listboxData->listLength = (short)newLength;
	computeTotalHeight(listbox);
	if (listboxData->multiSelect)
	{
		GadgetListBoxRemoveMultiSelect(listbox);
		GadgetListBoxAddMultiSelect(listbox);
	}
}

int GadgetListBoxGetListLength(GameWindow *listbox)
{
	ListboxData *listboxData = listOf(listbox);
	return listboxData->multiSelect ? listboxData->listLength : 1;
}

int GadgetListBoxGetNumEntries(GameWindow *listbox)
{
	if (!listbox)
	{
		return 0;
	}
	ListboxData *listboxData = listOf(listbox);
	return listboxData ? listboxData->endPos : 0;
}

void GadgetListBoxGetSelected(GameWindow *listbox, int *selectList)
{
	if (listbox == nullptr)
	{
		return;
	}
	listbox->manager().winSendSystemMsg(listbox, GLM_GET_SELECTION, 0, msgData(selectList));
}

void GadgetListBoxSetSelected(GameWindow *listbox, int selectIndex)
{
	if (listbox == nullptr)
	{
		return;
	}
	listbox->manager().winSendSystemMsg(listbox, GLM_SET_SELECTION, msgData(&selectIndex), 1);
}

void GadgetListBoxSetSelected(GameWindow *listbox, const int *selectList, int selectCount)
{
	if (listbox == nullptr)
	{
		return;
	}
	listbox->manager().winSendSystemMsg(listbox, GLM_SET_SELECTION, msgData(selectList), (WindowMsgData)selectCount);
}

void GadgetListBoxReset(GameWindow *listbox)
{
	if (listbox == nullptr)
	{
		return;
	}
	listbox->manager().winSendSystemMsg(listbox, GLM_DEL_ALL, 0, 0);
}

void GadgetListBoxSetItemData(GameWindow *listbox, void *data, int row, int column)
{
	ICoord2D pos;
	pos.x = column;
	pos.y = row;
	if (listbox)
	{
		listbox->manager().winSendSystemMsg(listbox, GLM_SET_ITEM_DATA, msgData(&pos), reinterpret_cast<WindowMsgData>(data));
	}
}

void *GadgetListBoxGetItemData(GameWindow *listbox, int row, int column)
{
	void *data = nullptr;
	ICoord2D pos;
	pos.x = column;
	pos.y = row;
	if (listbox)
	{
		listbox->manager().winSendSystemMsg(listbox, GLM_GET_ITEM_DATA, msgData(&pos), msgData(&data));
	}
	return data;
}

int GadgetListBoxGetBottomVisibleEntry(GameWindow *window)
{
	if (!window)
	{
		return 0;
	}
	ListboxData *listData = listOf(window);
	return listData ? getListboxBottomEntry(listData) : 0;
}

bool GadgetListBoxIsFull(GameWindow *window)
{
	if (!window)
	{
		return false;
	}
	ListboxData *listData = listOf(window);
	if (!listData)
	{
		return false;
	}
	// lane END-1: an empty list box (a just reset one) has no entry to measure and is not full; the unchecked read of entry 0 crashed AptSkirmish::refreshProfileList
	// intermittently (an empty row vector, or a stale row of the previous contents)
	if (listData->endPos <= 0 || listData->listData.empty())
	{
		return false;
	}
	const int entry = getListboxBottomEntry(listData);
	return listData->listData[(std::size_t)entry].listHeight >= listData->displayPos + listData->displayHeight - 5;
}

void GadgetListBoxSetBottomVisibleEntry(GameWindow *window, int newPos)
{
	if (!window || !listOf(window))
	{
		return;
	}
	const int prevPos = GadgetListBoxGetBottomVisibleEntry(window);
	adjustDisplay(window, newPos - prevPos + 1, true);
}

int GadgetListBoxGetTopVisibleEntry(GameWindow *window)
{
	if (!window)
	{
		return 0;
	}
	ListboxData *listData = listOf(window);
	return listData ? getListboxTopEntry(listData) : 0;
}

void GadgetListBoxSetTopVisibleEntry(GameWindow *window, int newPos)
{
	if (!window || !listOf(window))
	{
		return;
	}
	const int prevPos = GadgetListBoxGetTopVisibleEntry(window);
	adjustDisplay(window, newPos - prevPos, true);
}

void GadgetListBoxSetAudioFeedback(GameWindow *listbox, bool enable)
{
	if (!listbox)
	{
		return;
	}
	if (ListboxData *listboxData = listOf(listbox))
	{
		listboxData->audioFeedback = enable;
	}
}

int GadgetListBoxGetNumColumns(GameWindow *listbox)
{
	if (!listbox)
	{
		return 0;
	}
	ListboxData *listboxData = listOf(listbox);
	return listboxData ? listboxData->columns : 0;
}

int GadgetListBoxGetColumnWidth(GameWindow *listbox, int column)
{
	if (!listbox)
	{
		return 0;
	}
	ListboxData *listboxData = listOf(listbox);
	if (!listboxData || listboxData->columns <= column || column < 0)
	{
		return 0;
	}
	return listboxData->columnWidth[(std::size_t)column];
}

// BFME GadgetListBoxSetColumnWidths / GadgetListBoxUpdateColumnWidths (target RotWK 0x003252EA / 0x003251F4 per open-bfme-2
// GadgetUserDataHelpers.cpp, donor BFME1 0x004B8230): the column count is replaced, the percentages are stored (null: an even split, the
// remainder spread one point over the first columns) and the pixel widths recomputed from the window width minus the slider and 2.
void GadgetListBoxSetColumnWidths(GameWindow *listbox, int count, const int *widths)
{
	if (count <= 0 || !listbox)
	{
		return;
	}
	ListboxData *data = listOf(listbox);
	if (!data)
	{
		return;
	}
	data->columns = (short)count;
	data->columnWidthPercentage.assign((std::size_t)count, 0);
	if (widths)
	{
		for (int i = 0; i < count; ++i)
		{
			data->columnWidthPercentage[(std::size_t)i] = widths[i];
		}
	}
	else
	{
		const int each = 100 / count;
		int extra = 100 % count;
		for (int i = 0; i < count; ++i)
		{
			if (extra != 0)
			{
				--extra;
				data->columnWidthPercentage[(std::size_t)i] = each + 1;
			}
			else
			{
				data->columnWidthPercentage[(std::size_t)i] = each;
			}
		}
	}
	int width = 0, height = 0;
	listbox->winGetSize(&width, &height);
	int sliderWidth = 0;
	if (data->slider)
	{
		int sh = 0;
		data->slider->winGetSize(&sliderWidth, &sh);
		sliderWidth += 2;
	}
	data->columnWidth.assign((std::size_t)count, 0);
	if (count == 1)
	{
		data->columnWidth[0] = width - sliderWidth;
		return;
	}
	for (int i = 0; i < count; ++i)
	{
		data->columnWidth[(std::size_t)i] = data->columnWidthPercentage[(std::size_t)i] * (width - sliderWidth) / 100;
	}
}

// ---- draw (W3DListBox.cpp) ---------------------------------------------------------------------------------------------------------------------

namespace
{

// drawHiliteBar: left / right caps, centre pieces, the small centre piece for the rest (clipped)
void drawHiliteBar(GameWindow *window, const std::string &left, const std::string &right, const std::string &center, const std::string &smallCenter, int startX, int startY, int endX, int endY)
{
	GameWindowManager &mgr = window->manager();
	int lw, lh, rw, rh, cw, ch, sw, sh;
	if (!mgr.imageSize(left, lw, lh) || !mgr.imageSize(right, rw, rh) || !mgr.imageSize(center, cw, ch) || !mgr.imageSize(smallCenter, sw, sh))
	{
		return;
	}
	if (cw <= 0 || sw <= 0)
	{
		return;
	}
	ICoord2D barWindowSize;
	const int xOffset = 0, yOffset = 0;
	ICoord2D start, end;
	barWindowSize.x = endX - startX;
	barWindowSize.y = endY - startY;
	if (barWindowSize.x < lw + rw)
	{
		barWindowSize.x = lw + rw;
	}
	ICoord2D leftEnd, rightStart;
	leftEnd.x = startX + lw + xOffset;
	leftEnd.y = startY + barWindowSize.y + yOffset;
	rightStart.x = startX + barWindowSize.x - rw + xOffset;
	rightStart.y = startY + yOffset;
	int centerWidth = rightStart.x - leftEnd.x;
	int pieces = centerWidth / cw;
	start.x = leftEnd.x;
	start.y = startY + yOffset;
	end.y = start.y + barWindowSize.y;
	for (int i = 0; i < pieces; ++i)
	{
		end.x = start.x + cw;
		mgr.winDrawImage(center, start.x, start.y, end.x, end.y);
		start.x += cw;
	}
	IRegion2D clipRegion;
	clipRegion.lo.x = leftEnd.x;
	clipRegion.lo.y = startY + yOffset;
	clipRegion.hi.x = leftEnd.x + centerWidth;
	clipRegion.hi.y = start.y + barWindowSize.y;
	mgr.setClipRegion(clipRegion);
	centerWidth = rightStart.x - start.x;
	if (centerWidth)
	{
		pieces = centerWidth / sw + 1;
		end.y = start.y + barWindowSize.y;
		for (int i = 0; i < pieces; ++i)
		{
			end.x = start.x + sw;
			mgr.winDrawImage(smallCenter, start.x, start.y, end.x, end.y);
			start.x += sw;
		}
	}
	mgr.enableClipping(false);
	start.x = startX + xOffset;
	start.y = startY + yOffset;
	end = leftEnd;
	mgr.winDrawImage(left, start.x, start.y, end.x, end.y);
	start = rightStart;
	end.x = start.x + rw;
	end.y = start.y + barWindowSize.y;
	mgr.winDrawImage(right, start.x, start.y, end.x, end.y);
}

void drawListBoxText(GameWindow *window, WinInstanceData *instData, int x, int y, int width, int height, bool useImages)
{
	ListboxData *list = listOf(window);
	GameWindowManager &mgr = window->manager();
	IRegion2D clipRegion;
	clipRegion.lo.x = x + 1;
	clipRegion.lo.y = y - 3;
	clipRegion.hi.x = x + width - 1;
	clipRegion.hi.y = y + height - 1;
	int drawY = y - list->displayPos;
	for (int i = 0;; ++i)
	{
		if (i > 0 && list->listData[(std::size_t)i - 1].listHeight > (list->displayPos + list->displayHeight))
		{
			break;
		}
		if (i == list->endPos)
		{
			break;
		}
		ListEntryRow &row = list->listData[(std::size_t)i];
		if (row.listHeight < list->displayPos)
		{
			drawY += (row.height + 1);
			continue;
		}
		const int listLineHeight = row.height + 1;
		bool selected = false;
		if (list->multiSelect)
		{
			for (int j = 0; list->selections[(std::size_t)j] >= 0; ++j)
			{
				if (i == list->selections[(std::size_t)j])
				{
					selected = true;
					break;
				}
			}
		}
		else if (i == ((list->trackHover && list->hoverActive) ? list->hoverPos : list->selectPos)) // lane FB7-1: RW 0x4A22D6
		{
			selected = true;
		}
		if (selected)
		{
			const bool disabled = !BitTest(window->winGetStatus(), WIN_STATUS_ENABLED);
			const bool hilited = !disabled && BitTest(instData->getState(), WIN_STATE_HILITED);
			if (useImages)
			{
				const std::string &left = disabled ? window->winGetDisabledImage(1) : hilited ? window->winGetHiliteImage(1) : window->winGetEnabledImage(1);
				const std::string &right = disabled ? window->winGetDisabledImage(2) : hilited ? window->winGetHiliteImage(2) : window->winGetEnabledImage(2);
				const std::string &center = disabled ? window->winGetDisabledImage(3) : hilited ? window->winGetHiliteImage(3) : window->winGetEnabledImage(3);
				const std::string &smallCenter = disabled ? window->winGetDisabledImage(4) : hilited ? window->winGetHiliteImage(4) : window->winGetEnabledImage(4);
				ICoord2D start, end;
				start.x = x;
				start.y = drawY;
				end.x = start.x + width;
				end.y = start.y + listLineHeight;
				if (end.y > clipRegion.hi.y)
				{
					end.y = clipRegion.hi.y;
				}
				if (start.y < clipRegion.lo.y)
				{
					start.y = clipRegion.lo.y;
				}
				if (!left.empty() && !right.empty() && !center.empty() && !smallCenter.empty())
				{
					drawHiliteBar(window, left, right, center, smallCenter, start.x + 1, start.y, end.x, end.y);
				}
				else if (!center.empty() && left.empty() && right.empty() && smallCenter.empty())
				{
					// [S-177] The shipped APT list box skins (window\apt\listbox.wnd, combobox.wnd ...) give only the centre image of the
					// selected-item bar (draw data slot 3: AptListBoxHiliteSelectedItem, a 1 x 64 gradient); ZH's four-piece bar would draw nothing for
					// them.  The retail binary's list box draw was not read: the centre image is stretched over the row.
					mgr.winDrawImage(center, start.x + 1, start.y, end.x, end.y);
				}
			}
			else
			{
				Color selectColor, selectBorder;
				if (disabled)
				{
					selectColor = window->winGetDisabledColor(1);
					selectBorder = window->winGetDisabledBorderColor(1);
				}
				else if (hilited)
				{
					selectColor = window->winGetHiliteColor(1);
					selectBorder = window->winGetHiliteBorderColor(1);
				}
				else
				{
					selectColor = window->winGetEnabledColor(1);
					selectBorder = window->winGetEnabledBorderColor(1);
				}
				ICoord2D start, end;
				start.x = x;
				start.y = drawY;
				end.x = start.x + width;
				end.y = start.y + listLineHeight;
				if (end.y > clipRegion.hi.y)
				{
					end.y = clipRegion.hi.y;
				}
				if (start.y < clipRegion.lo.y)
				{
					start.y = clipRegion.lo.y;
				}
				if (selectBorder != WIN_COLOR_UNDEFINED)
				{
					mgr.winOpenRect(selectBorder, WIN_DRAW_LINE_WIDTH, start.x, start.y, end.x, end.y);
				}
				start.x = x + 1;
				start.y = drawY + 1;
				end.x = start.x + width - 2;
				end.y = start.y + listLineHeight - 2;
				if (end.y > clipRegion.hi.y)
				{
					end.y = clipRegion.hi.y;
				}
				if (start.y < clipRegion.lo.y)
				{
					start.y = clipRegion.lo.y;
				}
				if (selectColor != WIN_COLOR_UNDEFINED)
				{
					mgr.winFillRect(selectColor, WIN_DRAW_LINE_WIDTH, start.x, start.y, end.x, end.y);
				}
			}
		}
		const Color dropColor = GameMakeColor(0, 0, 0, 255);
		int columnX = x;
		if (row.hasCells)
		{
			for (int j = 0; j < list->columns; ++j)
			{
				IRegion2D columnRegion;
				columnRegion.lo.x = columnX;
				columnRegion.lo.y = drawY;
				if (list->columns == 1 && list->slider && list->slider->winIsHidden())
				{
					columnRegion.hi.x = columnX + width - 3;
				}
				else
				{
					columnRegion.hi.x = columnX + list->columnWidth[(std::size_t)j];
				}
				columnRegion.hi.y = drawY + row.height;
				if (columnRegion.lo.y < clipRegion.lo.y)
				{
					columnRegion.lo.y = clipRegion.lo.y;
				}
				if (columnRegion.hi.y > clipRegion.hi.y)
				{
					columnRegion.hi.y = clipRegion.hi.y;
				}
				ListEntryCell &cell = row.cell[(std::size_t)j];
				if (cell.cellType == LISTBOX_TEXT)
				{
					mgr.setClipRegion(columnRegion);
					const int wrap = BitTest(window->winGetStatus(), WIN_STATUS_ONE_LINE) ? 0 : list->columnWidth[(std::size_t)j] - TEXT_WIDTH_OFFSET;
					int textX = columnX + TEXT_X_OFFSET;
					if (cell.justification == 2 && window->winGetFont())
					{
						// the cell + 4 alignment (RW 0x725D96): centred in the column (inference, see ListEntryCell)
						const int tw = mgr.fontMetrics().textWidth(*window->winGetFont(), cell.text);
						textX = std::max(textX, columnX + (list->columnWidth[(std::size_t)j] - tw) / 2);
					}
					mgr.winDrawText(cell.text, window->winGetFont(), textX, drawY, cell.color, dropColor, wrap);
				}
				else if (cell.cellType == LISTBOX_IMAGE && !cell.image.empty())
				{
					int iw = cell.width > 0 ? cell.width : list->columnWidth[(std::size_t)j];
					const int ih = cell.height > 0 ? cell.height : row.height;
					if (j == 0)
					{
						--iw;
					}
					int offsetX, offsetY;
					if (iw < list->columnWidth[(std::size_t)j])
					{
						offsetX = columnX + ((list->columnWidth[(std::size_t)j] - iw) / 2);
					}
					else
					{
						offsetX = columnX;
					}
					if (ih < row.height)
					{
						offsetY = drawY + ((row.height - ih) / 2);
					}
					else
					{
						offsetY = drawY;
					}
					++offsetY;
					if (offsetX < x + 1)
					{
						offsetX = x + 1;
					}
					mgr.setClipRegion(columnRegion);
					mgr.winDrawImage(cell.image, offsetX, offsetY, offsetX + iw, offsetY + ih, cell.color);
				}
				columnX += list->columnWidth[(std::size_t)j];
			}
		}
		drawY += listLineHeight;
		mgr.enableClipping(false);
	}
}

} // namespace

void W3DGadgetListBoxDraw(GameWindow *window, WinInstanceData *instData)
{
	ListboxData *list = listOf(window);
	GameWindowManager &mgr = window->manager();
	int x, y;
	ICoord2D size;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&size.x, &size.y);
	int width = size.x;
	int height = size.y;
	Color background, border, titleColor, titleBorder;
	if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
	{
		background = window->winGetDisabledColor(0);
		border = window->winGetDisabledBorderColor(0);
		titleColor = window->winGetDisabledTextColor();
		titleBorder = window->winGetDisabledTextBorderColor();
	}
	else if (BitTest(instData->getState(), WIN_STATE_HILITED))
	{
		background = window->winGetHiliteColor(0);
		border = window->winGetHiliteBorderColor(0);
		titleColor = window->winGetHiliteTextColor();
		titleBorder = window->winGetHiliteTextBorderColor();
	}
	else
	{
		background = window->winGetEnabledColor(0);
		border = window->winGetEnabledBorderColor(0);
		titleColor = window->winGetEnabledTextColor();
		titleBorder = window->winGetEnabledTextBorderColor();
	}
	if (instData->getTextLength())
	{
		mgr.winDrawText(instData->getText(), window->winGetFont(), x + 1, y, titleColor, titleBorder);
		y += mgr.winFontHeight(instData->getFont()) + 1;
		height -= mgr.winFontHeight(instData->getFont()) + 1;
	}
	if (border != WIN_COLOR_UNDEFINED)
	{
		mgr.winOpenRect(border, WIN_DRAW_LINE_WIDTH, x, y, x + width, y + height);
	}
	if (background != WIN_COLOR_UNDEFINED)
	{
		mgr.winFillRect(background, WIN_DRAW_LINE_WIDTH, x + 1, y + 1, x + width - 1, y + height - 1);
	}
	if (list->slider && !list->slider->winIsHidden())
	{
		int sw, sh;
		list->slider->winGetSize(&sw, &sh);
		width -= (sw + 3);
	}
	drawListBoxText(window, instData, x, y + 4, width, height - 4, true);
}

void W3DGadgetListBoxImageDraw(GameWindow *window, WinInstanceData *instData)
{
	ListboxData *list = listOf(window);
	GameWindowManager &mgr = window->manager();
	int x, y;
	ICoord2D size;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&size.x, &size.y);
	int width = size.x;
	int height = size.y;
	if (list->slider)
	{
		int sw, sh;
		list->slider->winGetSize(&sw, &sh);
		width -= sw;
	}
	const std::string *image;
	Color titleColor, titleBorder;
	if (!BitTest(window->winGetStatus(), WIN_STATUS_ENABLED))
	{
		image = &window->winGetDisabledImage(0);
		titleColor = window->winGetDisabledTextColor();
		titleBorder = window->winGetDisabledTextBorderColor();
	}
	else if (BitTest(instData->getState(), WIN_STATE_HILITED))
	{
		image = &window->winGetHiliteImage(0);
		titleColor = window->winGetHiliteTextColor();
		titleBorder = window->winGetHiliteTextBorderColor();
	}
	else
	{
		image = &window->winGetEnabledImage(0);
		titleColor = window->winGetEnabledTextColor();
		titleBorder = window->winGetEnabledTextBorderColor();
	}
	if (!image->empty())
	{
		const int sx = x + instData->m_imageOffset.x;
		const int sy = y + instData->m_imageOffset.y;
		mgr.winDrawImage(*image, sx, sy, sx + width, sy + height);
	}
	if (instData->getTextLength())
	{
		mgr.winDrawText(instData->getText(), window->winGetFont(), x + 1, y, titleColor, titleBorder);
		y += mgr.winFontHeight(instData->getFont());
		height -= mgr.winFontHeight(instData->getFont()) + 1;
	}
	drawListBoxText(window, instData, x, y + 4, width, height - 4, true);
}

// RW 0x725D96 (lane END-2): the cell's + 4 field (see ListEntryCell::justification)
void GadgetListBoxSetCellJustification(GameWindow *listbox, int row, int column, int justification)
{
	ListboxData *data = listbox ? listOf(listbox) : nullptr;
	if (!data || row < 0 || row >= (int)data->listData.size() || column < 0)
	{
		return;
	}
	ListEntryRow &r = data->listData[(std::size_t)row];
	if ((std::size_t)column < r.cell.size())
	{
		r.cell[(std::size_t)column].justification = justification;
	}
}
