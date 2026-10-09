// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The public functions of the gadgets (ZH GadgetPushButton.h, GadgetCheckBox.h, GadgetSlider.h, GadgetListBox.h, GadgetComboBox.h,
// GadgetTextEntry.h) with their ZH names, plus the system / input / draw entry points the creators install.

#pragma once

#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/GameWindowManager.h"

// ---- push button (internal: list box arrows, combo drop-down, slider thumbs) -------------------------------------------------------------
WindowMsgHandledType GadgetPushButtonInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
WindowMsgHandledType GadgetPushButtonSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
void GadgetButtonSetText(GameWindow *g, const UnicodeString &text);
void W3DGadgetPushButtonDraw(GameWindow *window, WinInstanceData *instData);
void W3DGadgetPushButtonImageDraw(GameWindow *window, WinInstanceData *instData);

// ---- check box ----------------------------------------------------------------------------------------------------------------------------------
WindowMsgHandledType GadgetCheckBoxInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
WindowMsgHandledType GadgetCheckBoxSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
void GadgetCheckBoxSetText(GameWindow *g, const UnicodeString &text);
void GadgetCheckBoxSetChecked(GameWindow *g, bool isChecked);
void GadgetCheckBoxToggle(GameWindow *g);
bool GadgetCheckBoxIsChecked(GameWindow *g);
void W3DGadgetCheckBoxDraw(GameWindow *window, WinInstanceData *instData);
void W3DGadgetCheckBoxImageDraw(GameWindow *window, WinInstanceData *instData);

// ---- sliders ----------------------------------------------------------------------------------------------------------------------------------------
WindowMsgHandledType GadgetHorizontalSliderInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
WindowMsgHandledType GadgetHorizontalSliderSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
WindowMsgHandledType GadgetVerticalSliderInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
WindowMsgHandledType GadgetVerticalSliderSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
void W3DGadgetHorizontalSliderDraw(GameWindow *window, WinInstanceData *instData);
void W3DGadgetHorizontalSliderImageDraw(GameWindow *window, WinInstanceData *instData);
void W3DGadgetVerticalSliderDraw(GameWindow *window, WinInstanceData *instData);
void W3DGadgetVerticalSliderImageDraw(GameWindow *window, WinInstanceData *instData);
void GadgetSliderGetMinMax(GameWindow *g, int *min, int *max);
void GadgetSliderSetPosition(GameWindow *win, int pos);
int GadgetSliderGetPosition(GameWindow *win);
inline GameWindow *GadgetSliderGetThumb(GameWindow *g) { return g->winGetChild(); }

// ---- list box ---------------------------------------------------------------------------------------------------------------------------------------
WindowMsgHandledType GadgetListBoxInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
WindowMsgHandledType GadgetListBoxSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
void W3DGadgetListBoxDraw(GameWindow *window, WinInstanceData *instData);
void W3DGadgetListBoxImageDraw(GameWindow *window, WinInstanceData *instData);
int GadgetListBoxGetEntryBasedOnXY(GameWindow *listbox, int x, int y, int &row, int &column);
void GadgetListboxCreateScrollbar(GameWindow *listbox);
void GadgetListBoxAddMultiSelect(GameWindow *listbox);
void GadgetListBoxRemoveMultiSelect(GameWindow *listbox);
void GadgetListBoxSetListLength(GameWindow *listbox, int newLength);
int GadgetListBoxGetListLength(GameWindow *listbox);
int GadgetListBoxGetNumEntries(GameWindow *listbox);
int GadgetListBoxGetNumColumns(GameWindow *listbox);
int GadgetListBoxGetColumnWidth(GameWindow *listbox, int column);
// BFME addition (RotWK 0x003252EA): replaces the column count with `count` columns of `widths` percent (null: an even split).
void GadgetListBoxSetColumnWidths(GameWindow *listbox, int count, const int *widths);
void GadgetListBoxSetFont(GameWindow *listbox, GameFont *font);
UnicodeString GadgetListBoxGetText(GameWindow *listbox, int row, int column = 0);
UnicodeString GadgetListBoxGetTextAndColor(GameWindow *listbox, Color *color, int row, int column = 0);
int GadgetListBoxAddEntryText(GameWindow *listbox, UnicodeString text, Color color, int row, int column = -1, bool overwrite = true);
int GadgetListBoxAddEntryImage(GameWindow *listbox, const std::string &image, int row, int column = -1, bool overwrite = true, Color color = 0xFFFFFFFFu);
int GadgetListBoxAddEntryImage(GameWindow *listbox, const std::string &image, int row, int column, int height, int width, bool overwrite = true, Color color = 0xFFFFFFFFu);
void GadgetListBoxSetSelected(GameWindow *listbox, int selectIndex);
void GadgetListBoxSetSelected(GameWindow *listbox, const int *selectList, int selectCount = 1);
// Single select: `selectList` receives one int (the row, -1 none).  Multi select: it receives the selection array (-1 terminated), at
// most listLength + 1 ints.
void GadgetListBoxGetSelected(GameWindow *listbox, int *selectList);
void GadgetListBoxReset(GameWindow *listbox);
void GadgetListBoxSetCellJustification(GameWindow *listbox, int row, int column, int justification); // RW 0x725D96 (lane END-2)
void GadgetListBoxSetItemData(GameWindow *listbox, void *data, int row, int column = 0);
void *GadgetListBoxGetItemData(GameWindow *listbox, int row, int column = 0);
bool GadgetListBoxIsFull(GameWindow *window);
int GadgetListBoxGetBottomVisibleEntry(GameWindow *window);
void GadgetListBoxSetBottomVisibleEntry(GameWindow *window, int newPos);
int GadgetListBoxGetTopVisibleEntry(GameWindow *window);
void GadgetListBoxSetTopVisibleEntry(GameWindow *window, int newPos);
void GadgetListBoxSetAudioFeedback(GameWindow *listbox, bool enable);
inline GameWindow *GadgetListBoxGetSlider(GameWindow *g)
{
	ListboxData *listData = static_cast<ListboxData *>(g->winGetUserData());
	return listData ? listData->slider : nullptr;
}
inline GameWindow *GadgetListBoxGetUpButton(GameWindow *g)
{
	ListboxData *listData = static_cast<ListboxData *>(g->winGetUserData());
	return listData ? listData->upButton : nullptr;
}
inline GameWindow *GadgetListBoxGetDownButton(GameWindow *g)
{
	ListboxData *listData = static_cast<ListboxData *>(g->winGetUserData());
	return listData ? listData->downButton : nullptr;
}

// ---- combo box ---------------------------------------------------------------------------------------------------------------------------------------
WindowMsgHandledType GadgetComboBoxInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
WindowMsgHandledType GadgetComboBoxSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
void W3DGadgetComboBoxDraw(GameWindow *window, WinInstanceData *instData);
void W3DGadgetComboBoxImageDraw(GameWindow *window, WinInstanceData *instData);
void GadgetComboBoxSetFont(GameWindow *comboBox, GameFont *font);
UnicodeString GadgetComboBoxGetText(GameWindow *comboBox);
void GadgetComboBoxSetText(GameWindow *comboBox, const UnicodeString &text);
int GadgetComboBoxAddEntry(GameWindow *comboBox, const UnicodeString &text, Color color);
void GadgetComboBoxReset(GameWindow *comboBox);
void GadgetComboBoxHideList(GameWindow *comboBox);
void GadgetComboBoxSetSelectedPos(GameWindow *comboBox, int selectedIndex, bool dontHide = false);
void GadgetComboBoxGetSelectedPos(GameWindow *comboBox, int *selectedIndex);
void GadgetComboBoxSetItemData(GameWindow *comboBox, int index, void *data);
void *GadgetComboBoxGetItemData(GameWindow *comboBox, int index);
int GadgetComboBoxGetLength(GameWindow *comboBox);
void GadgetComboBoxSetIsEditable(GameWindow *comboBox, bool isEditable);
void GadgetComboBoxSetMaxChars(GameWindow *comboBox, int maxChars);
void GadgetComboBoxSetMaxDisplay(GameWindow *comboBox, int maxDisplay);
void GadgetComboBoxSetAsciiOnly(GameWindow *comboBox, bool isAsciiOnly);
void GadgetComboBoxSetLettersAndNumbersOnly(GameWindow *comboBox, bool v);
inline GameWindow *GadgetComboBoxGetDropDownButton(GameWindow *g)
{
	ComboBoxData *c = static_cast<ComboBoxData *>(g->winGetUserData());
	return c ? c->dropDownButton : nullptr;
}
inline GameWindow *GadgetComboBoxGetListBox(GameWindow *g)
{
	ComboBoxData *c = static_cast<ComboBoxData *>(g->winGetUserData());
	return c ? c->listBox : nullptr;
}
inline GameWindow *GadgetComboBoxGetEditBox(GameWindow *g)
{
	ComboBoxData *c = static_cast<ComboBoxData *>(g->winGetUserData());
	return c ? c->editBox : nullptr;
}

// ---- text entry --------------------------------------------------------------------------------------------------------------------------------------
WindowMsgHandledType GadgetTextEntryInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
WindowMsgHandledType GadgetTextEntrySystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
void W3DGadgetTextEntryDraw(GameWindow *window, WinInstanceData *instData);
void W3DGadgetTextEntryImageDraw(GameWindow *window, WinInstanceData *instData);
UnicodeString GadgetTextEntryGetText(GameWindow *textentry);
void GadgetTextEntrySetText(GameWindow *textentry, const UnicodeString &text);
void GadgetTextEntrySetTextColor(GameWindow *textentry, Color color);

// ---- image combo box (BFME addition: the colour column of the lobby, window\Apt\ImageComboBox.wnd) -------------------------------------------------------
WindowMsgHandledType GadgetImageComboBoxInput(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
WindowMsgHandledType GadgetImageComboBoxSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
void W3DGadgetImageComboBoxDraw(GameWindow *window, WinInstanceData *instData);
void GadgetImageComboBoxReset(GameWindow *box);
// One 20 x 20 image cell (mapped image name, tint); returns the entry index.
int GadgetImageComboBoxAddEntry(GameWindow *box, const std::string &image, Color tint);
int GadgetImageComboBoxGetLength(GameWindow *box);
void GadgetImageComboBoxSetSelectedPos(GameWindow *box, int index);
void GadgetImageComboBoxGetSelectedPos(GameWindow *box, int *index);
void GadgetImageComboBoxSetItemData(GameWindow *box, int index, void *data);
void *GadgetImageComboBoxGetItemData(GameWindow *box, int index);
GameWindow *GadgetImageComboBoxGetListBox(GameWindow *box);
bool GadgetImageComboBoxIsOpen(GameWindow *box);

// ---- map preview (lane UI-2: window\Apt\MpMapWindow.wnd, the lobby's CurrentMap; GUI/Gadget/GadgetMapPreview.cpp) -----------------------------------
// The window's user data is the map (const MapCacheEntry *); RotWK RW 0x49D9EC / 0x6C1501 / 0x7018E0 / 0x7019A0.
struct MapCacheEntry;
void W3DDrawMapPreview(GameWindow *window, WinInstanceData *instData);
WindowMsgHandledType PassSelectedButtonsToParentSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2);
void MapPreviewFindDrawPositions(int startX, int startY, int width, int height, const MapCacheEntry &map, ICoord2D *ul, ICoord2D *lr);
void MapPreviewPositionStartSpot(GameWindow *button, GameWindow *mapWindow, const Coord3D &pos, const MapCacheEntry &map, GameWindow *const buttons[8]);
