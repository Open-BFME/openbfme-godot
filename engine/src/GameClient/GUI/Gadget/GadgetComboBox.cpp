// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// Port of ZH GameClient/GUI/Gadget/GadgetComboBox.cpp and GameEngineDevice W3DComboBox.cpp.
//
// The combo box is a window with three children (ZH gogoGadgetComboBox): the drop-down push button, a text entry (the edit box, not
// editable unless COMBOBOXDATA says ISEDITABLE) and a one-line list box that is hidden until the box is clicked.  The WND file's flat
// draw data are handed to the children by the script loader (GameWindowManagerScript.cpp createGadget).

#include "GameClient/GUI/Gadgets.h"

namespace
{

ComboBoxData *comboOf(GameWindow *w) { return static_cast<ComboBoxData *>(w->winGetUserData()); }

void HideListBox(GameWindow *window)
{
	GameWindow *listBox = GadgetComboBoxGetListBox(window);
	if (!listBox)
	{
		return;
	}
	if (!listBox->winIsHidden())
	{
		listBox->winHide(true);
		GameWindow *editBox = GadgetComboBoxGetEditBox(window);
		ICoord2D winSize, newSize;
		editBox->winGetSize(&winSize.x, &winSize.y);
		window->winGetSize(&newSize.x, &newSize.y);
		window->winSetSize(newSize.x, winSize.y);
	}
}

// The list's size and its scroll controls for `entryCount` entries (the code that GCM_ADD_ENTRY and the click both run in ZH).
void layoutDropDown(GameWindow *window, ComboBoxData *comboData, ListboxData *listData, GameWindow *listBox, int listXForFew, int &listX, int &multiplier)
{
	(void)window;
	if (comboData->entryCount <= comboData->maxDisplay)
	{
		multiplier = comboData->entryCount;
		listX = listXForFew;
		if (listData->upButton)
		{
			listData->upButton->winHide(true);
		}
		if (listData->downButton)
		{
			listData->downButton->winHide(true);
		}
		if (listData->slider)
		{
			listData->slider->winHide(true);
		}
	}
	else
	{
		multiplier = comboData->maxDisplay;
		if (listData->upButton)
		{
			listData->upButton->winHide(false);
		}
		if (listData->downButton)
		{
			listData->downButton->winHide(false);
		}
		if (listData->slider)
		{
			listData->slider->winHide(false);
		}
	}
	(void)listBox;
}

} // namespace

WindowMsgHandledType GadgetComboBoxInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	WinInstanceData *instData = window->winGetInstanceData();
	GameWindowManager &mgr = window->manager();
	GameWindow *editBox = GadgetComboBoxGetEditBox(window);
	switch (msg)
	{
		case GWM_CHAR:
			switch (mData1)
			{
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
					return mgr.winSendInputMsg(editBox, GWM_CHAR, mData1, mData2);
			}
			break;
		case GWM_WHEEL_DOWN:
		case GWM_WHEEL_UP:
			break;
		case GWM_LEFT_UP:
		{
			ComboBoxData *comboData = comboOf(window);
			comboData->dontHide = false;
			GameWindow *listBox = GadgetComboBoxGetListBox(window);
			if (listBox)
			{
				mgr.winSetLoneWindow(window);
				if (listBox->winIsHidden())
				{
					listBox->winHide(false);
					ICoord2D winSize;
					window->winGetSize(&winSize.x, &winSize.y);
					WinInstanceData *listInstData = listBox->winGetInstanceData();
					ListboxData *listData = static_cast<ListboxData *>(listBox->winGetUserData());
					int listX, multiplier;
					layoutDropDown(window, comboData, listData, listBox, winSize.x, listX, multiplier);
					if (comboData->entryCount > comboData->maxDisplay)
					{
						listX = winSize.x;
					}
					const int newSizeY = (mgr.winFontHeight(listInstData->getFont()) * multiplier) + multiplier * 2 + 4;
					window->winSetSize(winSize.x, winSize.y + newSizeY);
					listBox->winSetPosition(0, winSize.y);
					listBox->winSetSize(listX, newSizeY);
				}
				else
				{
					HideListBox(window);
				}
			}
			break;
		}
		case GWM_RIGHT_UP:
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

WindowMsgHandledType GadgetComboBoxSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	WinInstanceData *instData = window->winGetInstanceData();
	ComboBoxData *comboData = comboOf(window);
	GameWindowManager &mgr = window->manager();
	switch (msg)
	{
		case GGM_SET_LABEL:
			instData->setText(*msgPtr<UnicodeString>(mData1));
			break;
		case GCM_GET_TEXT:
			if (comboData->editBox)
			{
				*msgPtr<UnicodeString>(mData2) = GadgetTextEntryGetText(comboData->editBox);
			}
			break;
		case GCM_SET_TEXT:
			if (comboData->editBox)
			{
				GadgetTextEntrySetText(comboData->editBox, *msgPtr<const UnicodeString>(mData1));
			}
			break;
		case GEM_UPDATE_TEXT:
			mgr.winSendSystemMsg(window->winGetOwner(), GCM_UPDATE_TEXT, msgData(window), 0);
			if (comboData->listBox)
			{
				GadgetListBoxSetSelected(comboData->listBox, -1);
				HideListBox(window);
			}
			break;
		case GEM_EDIT_DONE:
			if (msgPtr<GameWindow>(mData1) == comboData->editBox)
			{
				HideListBox(window);
				mgr.winSendSystemMsg(window->winGetOwner(), GCM_SELECTED, msgData(window), 0);
			}
			break;
		case GCM_SET_SELECTION:
		{
			GameWindow *listBox = GadgetComboBoxGetListBox(window);
			if (listBox)
			{
				if (!listBox->winIsHidden() && mData2 == 1)
				{
					comboData->dontHide = true;
				}
				GadgetListBoxSetSelected(listBox, (int)mData1);
			}
			break;
		}
		case GCM_GET_SELECTION:
			if (comboData->listBox)
			{
				GadgetListBoxGetSelected(comboData->listBox, msgPtr<int>(mData2));
			}
			else
			{
				*msgPtr<int>(mData2) = -1;
			}
			break;
		case GCM_SET_ITEM_DATA:
			if (comboData->listBox)
			{
				GadgetListBoxSetItemData(comboData->listBox, reinterpret_cast<void *>(mData2), (int)mData1);
			}
			break;
		case GCM_GET_ITEM_DATA:
			if (comboData->listBox)
			{
				*msgPtr<void *>(mData2) = GadgetListBoxGetItemData(comboData->listBox, (int)mData1, 0);
			}
			break;
		case GLM_SELECTED:
			if (msgPtr<GameWindow>(mData1) == comboData->listBox)
			{
				if (comboData->dontHide)
				{
					comboData->dontHide = false;
				}
				else
				{
					HideListBox(window);
				}
				const int selected = (int)(std::intptr_t)mData2;
				if (selected == -1)
				{
					break;
				}
				Color color;
				const UnicodeString text = GadgetListBoxGetTextAndColor(comboData->listBox, &color, selected, 0);
				GadgetTextEntrySetTextColor(comboData->editBox, color);
				GadgetTextEntrySetText(comboData->editBox, text);
				mgr.winSendSystemMsg(window->winGetOwner(), GCM_SELECTED, msgData(window), 0);
			}
			break;
		case GGM_LEFT_DRAG:
			break;
		case GCM_DEL_ALL:
			if (comboData->listBox)
			{
				GadgetListBoxReset(comboData->listBox);
			}
			if (comboData->editBox)
			{
				GadgetTextEntrySetText(comboData->editBox, UnicodeString());
			}
			comboData->entryCount = 0;
			break;
		case GCM_DEL_ENTRY:
			break; // ZH leaves it empty
		case GGM_CLOSE:
			HideListBox(window);
			break;
		case GCM_ADD_ENTRY:
		{
			GameWindow *listBox = GadgetComboBoxGetListBox(window);
			int addedIndex = -1;
			if (listBox)
			{
				ListboxData *listData = static_cast<ListboxData *>(listBox->winGetUserData());
				comboData->entryCount++;
				if (comboData->entryCount >= listData->listLength)
				{
					GadgetListBoxSetListLength(listBox, listData->listLength * 2);
				}
				addedIndex = GadgetListBoxAddEntryText(listBox, *msgPtr<UnicodeString>(mData1), (Color)mData2, -1, 0);
				ICoord2D winSize, editBoxSize;
				WinInstanceData *listInstData = listBox->winGetInstanceData();
				GameWindow *editBox = GadgetComboBoxGetEditBox(window);
				window->winGetSize(&winSize.x, &winSize.y);
				editBox->winGetSize(&editBoxSize.x, &editBoxSize.y);
				int listX, multiplier;
				layoutDropDown(window, comboData, listData, listBox, winSize.x + 16, listX, multiplier);
				if (comboData->entryCount > comboData->maxDisplay)
				{
					listX = winSize.x;
				}
				const int newSizeY = (mgr.winFontHeight(listInstData->getFont()) * multiplier) + multiplier * 2 + 4;
				listBox->winSetPosition(0, editBoxSize.y);
				listBox->winSetSize(listX, newSizeY);
			}
			return (WindowMsgHandledType)addedIndex;
		}
		case GWM_CREATE:
			break;
		case GGM_RESIZED:
		{
			if (comboData == nullptr || comboData->dropDownButton == nullptr || comboData->listBox == nullptr)
			{
				break; // the children do not exist yet (winCreate sends the first resize before the creator finished)
			}
			const int width = (int)mData1;
			const int height = (int)mData2;
			ICoord2D dropDownSize;
			comboData->dropDownButton->winGetSize(&dropDownSize.x, &dropDownSize.y);
			GameWindow *listBox = GadgetComboBoxGetListBox(window);
			if (listBox->winIsHidden())
			{
				listBox->winSetSize(width, height);
				comboData->dropDownButton->winSetPosition(width - dropDownSize.x, 0);
				if (comboData->editBox)
				{
					comboData->editBox->winSetPosition(0, 0);
					comboData->editBox->winSetSize(width - dropDownSize.x, height);
				}
			}
			break;
		}
		case GWM_DESTROY:
			mgr.winSetLoneWindow(nullptr);
			delete comboData;
			window->winSetUserData(nullptr);
			break;
		case GWM_INPUT_FOCUS:
		{
			if (mData1 == 0)
			{
				BitClear(instData->m_state, WIN_STATE_HILITED);
			}
			else
			{
				BitSet(instData->m_state, WIN_STATE_HILITED);
			}
			mgr.winSendSystemMsg(window->winGetOwner(), GGM_FOCUS_CHANGE, mData1, (WindowMsgData)window->winGetWindowId());
			bool wantsFocus = false;
			GameWindow *editBox = GadgetComboBoxGetEditBox(window);
			mgr.winSendSystemMsg(editBox, GWM_INPUT_FOCUS, mData1, msgData(&wantsFocus));
			*msgPtr<bool>(mData2) = true;
			break;
		}
		case GBM_SELECTED:
			if (msgPtr<GameWindow>(mData1) == comboData->dropDownButton)
			{
				comboData->dontHide = false;
				GameWindow *listBox = GadgetComboBoxGetListBox(window);
				if (listBox)
				{
					mgr.winSetLoneWindow(window);
					if (listBox->winIsHidden())
					{
						listBox->winHide(false);
						ICoord2D winSize;
						window->winGetSize(&winSize.x, &winSize.y);
						WinInstanceData *listInstData = listBox->winGetInstanceData();
						ListboxData *listData = static_cast<ListboxData *>(listBox->winGetUserData());
						int listX, multiplier;
						layoutDropDown(window, comboData, listData, listBox, winSize.x, listX, multiplier);
						if (comboData->entryCount > comboData->maxDisplay)
						{
							listX = winSize.x;
						}
						const int newSizeY = (mgr.winFontHeight(listInstData->getFont()) * multiplier) + multiplier * 2 + 4;
						window->winSetSize(winSize.x, winSize.y + newSizeY);
						listBox->winSetPosition(0, winSize.y);
						listBox->winSetSize(listX, newSizeY);
					}
					else
					{
						HideListBox(window);
					}
				}
			}
			break;
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

// ---- API -----------------------------------------------------------------------------------------------------------------------------------------

void GadgetComboBoxSetIsEditable(GameWindow *comboBox, bool isEditable)
{
	ComboBoxData *comboData = comboOf(comboBox);
	GameWindow *editBox = GadgetComboBoxGetEditBox(comboBox);
	if (!editBox)
	{
		return;
	}
	comboData->isEditable = isEditable;
	std::uint32_t status = editBox->winGetStatus();
	if (isEditable)
	{
		BitClear(status, WIN_STATUS_NO_INPUT);
	}
	else
	{
		BitSet(status, WIN_STATUS_NO_INPUT);
	}
	editBox->winSetStatus(status);
	if (isEditable)
	{
		editBox->winClearStatus(WIN_STATUS_NO_INPUT);
	}
}

void GadgetComboBoxSetLettersAndNumbersOnly(GameWindow *comboBox, bool v)
{
	if (comboBox == nullptr)
	{
		return;
	}
	ComboBoxData *comboData = comboOf(comboBox);
	comboData->lettersAndNumbersOnly = v;
	if (comboData->editBox)
	{
		static_cast<EntryData *>(comboData->editBox->winGetUserData())->alphaNumericalOnly = v;
	}
}

void GadgetComboBoxSetAsciiOnly(GameWindow *comboBox, bool isAsciiOnly)
{
	if (comboBox == nullptr)
	{
		return;
	}
	ComboBoxData *comboData = comboOf(comboBox);
	comboData->asciiOnly = isAsciiOnly;
	if (comboData->editBox)
	{
		static_cast<EntryData *>(comboData->editBox->winGetUserData())->aSCIIOnly = isAsciiOnly;
	}
}

void GadgetComboBoxSetMaxChars(GameWindow *comboBox, int maxChars)
{
	if (comboBox == nullptr)
	{
		return;
	}
	ComboBoxData *comboData = comboOf(comboBox);
	comboData->maxChars = maxChars;
	if (comboData->editBox)
	{
		static_cast<EntryData *>(comboData->editBox->winGetUserData())->maxTextLen = (short)maxChars;
	}
}

void GadgetComboBoxSetMaxDisplay(GameWindow *comboBox, int maxDisplay)
{
	comboOf(comboBox)->maxDisplay = maxDisplay;
}

UnicodeString GadgetComboBoxGetText(GameWindow *comboBox)
{
	if (comboBox == nullptr || !BitTest(comboBox->winGetStyle(), GWS_COMBO_BOX))
	{
		return UnicodeString();
	}
	return GadgetTextEntryGetText(GadgetComboBoxGetEditBox(comboBox));
}

void GadgetComboBoxSetText(GameWindow *comboBox, const UnicodeString &text)
{
	if (comboBox == nullptr)
	{
		return;
	}
	GadgetTextEntrySetText(GadgetComboBoxGetEditBox(comboBox), text);
}

int GadgetComboBoxAddEntry(GameWindow *comboBox, const UnicodeString &text, Color color)
{
	if (comboBox == nullptr)
	{
		return -1;
	}
	UnicodeString copy = text;
	return (int)(std::intptr_t)comboBox->manager().winSendSystemMsg(comboBox, GCM_ADD_ENTRY, msgData(&copy), (WindowMsgData)color);
}

void GadgetComboBoxReset(GameWindow *comboBox)
{
	if (comboBox == nullptr)
	{
		return;
	}
	comboBox->manager().winSendSystemMsg(comboBox, GCM_DEL_ALL, 0, 0);
}

void GadgetComboBoxHideList(GameWindow *comboBox)
{
	if (comboBox == nullptr)
	{
		return;
	}
	comboBox->manager().winSendSystemMsg(comboBox, GGM_CLOSE, 0, 0);
}

void GadgetComboBoxSetFont(GameWindow *comboBox, GameFont *font)
{
	if (comboBox == nullptr)
	{
		return;
	}
	ComboBoxData *c = comboOf(comboBox);
	if (!c)
	{
		return;
	}
	if (c->listBox)
	{
		c->listBox->winSetFont(font);
	}
	if (c->editBox)
	{
		c->editBox->winSetFont(font);
	}
}

void GadgetComboBoxGetSelectedPos(GameWindow *comboBox, int *selectedIndex)
{
	if (comboBox == nullptr)
	{
		return;
	}
	comboBox->manager().winSendSystemMsg(comboBox, GCM_GET_SELECTION, 0, msgData(selectedIndex));
}

void GadgetComboBoxSetSelectedPos(GameWindow *comboBox, int selectedIndex, bool dontHide)
{
	if (comboBox == nullptr)
	{
		return;
	}
	comboBox->manager().winSendSystemMsg(comboBox, GCM_SET_SELECTION, (WindowMsgData)selectedIndex, dontHide ? 1 : 0);
}

void GadgetComboBoxSetItemData(GameWindow *comboBox, int index, void *data)
{
	if (comboBox == nullptr)
	{
		return;
	}
	comboBox->manager().winSendSystemMsg(comboBox, GCM_SET_ITEM_DATA, (WindowMsgData)index, reinterpret_cast<WindowMsgData>(data));
}

void *GadgetComboBoxGetItemData(GameWindow *comboBox, int index)
{
	void *data = nullptr;
	if (comboBox == nullptr)
	{
		return nullptr;
	}
	comboBox->manager().winSendSystemMsg(comboBox, GCM_GET_ITEM_DATA, (WindowMsgData)index, msgData(&data));
	return data;
}

int GadgetComboBoxGetLength(GameWindow *comboBox)
{
	ComboBoxData *c = comboOf(comboBox);
	return c ? c->entryCount : 0;
}

// ---- draw (W3DComboBox.cpp) --------------------------------------------------------------------------------------------------------------------

void W3DGadgetComboBoxDraw(GameWindow *window, WinInstanceData *instData)
{
	GameWindowManager &mgr = window->manager();
	int x, y;
	ICoord2D size;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&size.x, &size.y);
	const int fontHeight = mgr.winFontHeight(instData->getFont());
	const int width = size.x;
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
		y += fontHeight + 1;
		height -= fontHeight + 1;
	}
	if (border != WIN_COLOR_UNDEFINED)
	{
		mgr.winOpenRect(border, WIN_DRAW_LINE_WIDTH, x, y, x + width, y + height);
	}
	if (background != WIN_COLOR_UNDEFINED)
	{
		mgr.winFillRect(background, WIN_DRAW_LINE_WIDTH, x + 1, y + 1, x + width - 1, y + height - 1);
	}
}

void W3DGadgetComboBoxImageDraw(GameWindow *window, WinInstanceData *instData)
{
	GameWindowManager &mgr = window->manager();
	int x, y;
	ICoord2D size;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&size.x, &size.y);
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
		mgr.winDrawImage(*image, sx, sy, sx + size.x, sy + size.y);
	}
	if (instData->getTextLength())
	{
		mgr.winDrawText(instData->getText(), window->winGetFont(), x + 1, y, titleColor, titleBorder);
	}
}
