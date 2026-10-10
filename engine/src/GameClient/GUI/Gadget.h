// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Gadget styles, messages and per-gadget data (ZH GameEngine/Include/GameClient/Gadget.h, GadgetListBox.h, GadgetComboBox.h,
// GadgetTextEntry.h, GadgetSlider.h).  Values are ZH's; the RotWK style names table (game.dat 0x00DA3280) equals ZH's WindowStyleNames.
// The pointers into other windows are raw (the window tree owns them); the arrays of ZH's raw `new[]` are std::vector.

#pragma once

#include "GameClient/GUI/GameWindow.h"

#include <vector>

enum
{
	GADGET_SIZE = 16,
	HORIZONTAL_SLIDER_THUMB_WIDTH = 13,
	HORIZONTAL_SLIDER_THUMB_HEIGHT = 16,
	HORIZONTAL_SLIDER_THUMB_POSITION = HORIZONTAL_SLIDER_THUMB_HEIGHT * 2 / 3, // GadgetSlider.h (ZH)
	HORIZONTAL_SLIDER_THUMB_Y = 0, // lane FB7-1: RotWK's thumb row (every winSetPosition of RW 0x7234EC / 0x7237F8 passes y 0)
	ENTRY_TEXT_LEN = 256,
	STATIC_TEXT_LEN = 256
};

enum
{
	TEXT_X_OFFSET = 5,
	TEXT_Y_OFFSET = 2,
	TEXT_WIDTH_OFFSET = 7,
	TOTAL_OUTLINE_HEIGHT = 2
};

enum
{
	LISTBOX_TEXT = 1,
	LISTBOX_IMAGE = 2
};

enum
{
	GWS_PUSH_BUTTON = 0x00000001,
	GWS_RADIO_BUTTON = 0x00000002,
	GWS_CHECK_BOX = 0x00000004,
	GWS_VERT_SLIDER = 0x00000008,
	GWS_HORZ_SLIDER = 0x00000010,
	GWS_SCROLL_LISTBOX = 0x00000020,
	GWS_ENTRY_FIELD = 0x00000040,
	GWS_STATIC_TEXT = 0x00000080,
	GWS_PROGRESS_BAR = 0x00000100,
	GWS_USER_WINDOW = 0x00000200,
	GWS_MOUSE_TRACK = 0x00000400,
	GWS_ANIMATED = 0x00000800,
	GWS_TAB_STOP = 0x00001000,
	GWS_TAB_CONTROL = 0x00002000,
	GWS_TAB_PANE = 0x00004000,
	GWS_COMBO_BOX = 0x00008000,
	GWS_ALL_SLIDER = GWS_VERT_SLIDER | GWS_HORZ_SLIDER
};

enum
{
	GP_DONT_UPDATE = 0x00000001
};

enum GadgetGameMessage
{
	GGM_LEFT_DRAG = 16384,
	GGM_SET_LABEL,
	GGM_GET_LABEL,
	GGM_FOCUS_CHANGE,
	GGM_RESIZED,
	GGM_CLOSE,
	GBM_MOUSE_ENTERING,
	GBM_MOUSE_LEAVING,
	GBM_SELECTED,
	GBM_SELECTED_RIGHT,
	GBM_SET_SELECTION,
	GSM_SLIDER_TRACK,
	GSM_SET_SLIDER,
	GSM_SET_MIN_MAX,
	GSM_SLIDER_DONE,
	GLM_ADD_ENTRY,
	GLM_DEL_ENTRY,
	GLM_DEL_ALL,
	GLM_SELECTED,
	GLM_DOUBLE_CLICKED,
	GLM_RIGHT_CLICKED,
	GLM_SET_SELECTION,
	GLM_GET_SELECTION,
	GLM_TOGGLE_MULTI_SELECTION,
	GLM_GET_TEXT,
	GLM_SET_UP_BUTTON,
	GLM_SET_DOWN_BUTTON,
	GLM_SET_SLIDER,
	GLM_SCROLL_BUFFER,
	GLM_UPDATE_DISPLAY,
	GLM_GET_ITEM_DATA,
	GLM_SET_ITEM_DATA,
	GCM_ADD_ENTRY,
	GCM_DEL_ENTRY,
	GCM_DEL_ALL,
	GCM_SELECTED,
	GCM_GET_TEXT,
	GCM_SET_TEXT,
	GCM_EDIT_DONE,
	GCM_GET_ITEM_DATA,
	GCM_SET_ITEM_DATA,
	GCM_GET_SELECTION,
	GCM_SET_SELECTION,
	GCM_UPDATE_TEXT,
	GEM_GET_TEXT,
	GEM_SET_TEXT,
	GEM_EDIT_DONE,
	GEM_UPDATE_TEXT,
	GPM_SET_PROGRESS
};

// ZH GameClient/KeyDefs.h: DirectInput scan codes (KEY_*) and the key state bits of GWM_CHAR's second argument
enum
{
	KEY_NONE = 0x00,
	KEY_ESC = 0x01,
	KEY_BACKSPACE = 0x0E,
	KEY_TAB = 0x0F,
	KEY_ENTER = 0x1C,
	KEY_SPACE = 0x39,
	KEY_CAPS = 0x3A,
	KEY_F1 = 0x3B,
	KEY_F2 = 0x3C,
	KEY_F3 = 0x3D,
	KEY_F4 = 0x3E,
	KEY_F5 = 0x3F,
	KEY_F6 = 0x40,
	KEY_F7 = 0x41,
	KEY_F8 = 0x42,
	KEY_F9 = 0x43,
	KEY_F10 = 0x44,
	KEY_HOME = 0xC7,
	KEY_UP = 0xC8,
	KEY_PGUP = 0xC9,
	KEY_LEFT = 0xCB,
	KEY_RIGHT = 0xCD,
	KEY_END = 0xCF,
	KEY_DOWN = 0xD0,
	KEY_PGDN = 0xD1,
	KEY_DEL = 0xD3,
	KEY_F11 = 0x57,
	KEY_F12 = 0x58,
	KEY_KPENTER = 0x9C
};

enum
{
	KEY_STATE_NONE = 0x0000,
	KEY_STATE_UP = 0x0001,
	KEY_STATE_DOWN = 0x0002,
	KEY_STATE_LCONTROL = 0x0004,
	KEY_STATE_RCONTROL = 0x0008,
	KEY_STATE_LSHIFT = 0x0010,
	KEY_STATE_RSHIFT = 0x0020,
	KEY_STATE_LALT = 0x0040,
	KEY_STATE_RALT = 0x0080,
	KEY_STATE_AUTOREPEAT = 0x0100,
	KEY_STATE_CAPSLOCK = 0x0200,
	KEY_STATE_SHIFT2 = 0x0400,
	KEY_STATE_CONTROL = KEY_STATE_LCONTROL | KEY_STATE_RCONTROL,
	KEY_STATE_SHIFT = KEY_STATE_LSHIFT | KEY_STATE_RSHIFT | KEY_STATE_SHIFT2,
	KEY_STATE_ALT = KEY_STATE_LALT | KEY_STATE_RALT
};

// ---- slider (GadgetSlider.h SliderData) --------------------------------------------------------------------------------------
struct SliderData
{
	int minVal = 0;
	int maxVal = 0;
	float numTicks = 0;
	int position = 0;
};

// ---- text entry (GadgetTextEntry.h EntryData; DisplayStrings are plain text here) -----------------------------------------------
struct EntryData
{
	UnicodeString text;
	UnicodeString sText;         // the '*' mask of a secret entry
	UnicodeString constructText; // IME composition (not driven: stop S-177)
	short maxTextLen = 0;
	bool secretText = false;
	bool numericalOnly = false;
	bool alphaNumericalOnly = false;
	bool aSCIIOnly = false;
	bool drawTextFromStart = false;
	bool receivedUnichar = false;
	short charPos = 0;
	short conCharPos = 0;
	GameWindow *constructList = nullptr; // only for the Korean / Japanese IME of ZH: never created
};

// ---- list box (Gadget.h ListboxData) -----------------------------------------------------------------------------------------------
struct ListEntryCell
{
	int cellType = 0;     // LISTBOX_TEXT / LISTBOX_IMAGE, 0 = empty
	Color color = 0;
	UnicodeString text;   // LISTBOX_TEXT (ZH keeps a DisplayString)
	std::string image;    // LISTBOX_IMAGE: the mapped image name
	void *userData = nullptr;
	int width = 0;        // image cells
	int height = 0;
	// RotWK cell + 4 (the cell is 0x1C bytes; RW 0x725D96 writes it): set to 2 for the score screen's values (RW 0x9F0876). Taken as the text's horizontal
	// alignment, 2 = centred in the column (INFERENCE: the drawing code that reads it was not located, stop S-1063)
	int justification = 0;
};

struct ListEntryRow
{
	int listHeight = 0;   // cumulative height up to and including this row (+1 per row)
	int height = 0;
	bool hasCells = false;
	std::vector<ListEntryCell> cell;
};

struct ListboxData
{
	short listLength = 0;
	short columns = 0;
	std::vector<int> columnWidthPercentage;
	bool autoScroll = false;
	bool autoPurge = false;
	bool scrollBar = false;
	bool multiSelect = false;
	bool forceSelect = false;
	bool scrollIfAtEnd = false;
	bool audioFeedback = false;

	std::vector<int> columnWidth;
	std::vector<ListEntryRow> listData;
	GameWindow *upButton = nullptr;
	GameWindow *downButton = nullptr;
	GameWindow *slider = nullptr;
	int totalHeight = 0;
	short endPos = 0;
	short insertPos = 0;
	int selectPos = 0;
	std::vector<int> selections;
	short displayHeight = 0;
	std::uint32_t doubleClickTime = 0;
	short displayPos = 0;
	// lane FB7-1: RotWK's pointer-following highlight of a drop-down list (ListboxData +0x12 / +0x13 / +0x30): the combo boxes set both flags and
	// start the row at the selection when they open the list (RW 0x72454E, RW 0x725439 / 0x72525B); GWM_MOUSE_POS moves it to the row under the
	// pointer, -1 past the last (RW 0x727081 case 0x18 -> RW 0x7258E1); the draw highlights it instead of the selection (RW 0x4A22D6)
	bool trackHover = false;  // +0x12
	bool hoverActive = false; // +0x13
	int hoverPos = -1;        // +0x30
};

// the payload of GLM_ADD_ENTRY (Gadget.h AddMessageStruct)
struct AddMessageStruct
{
	int row = -1;
	int column = -1;
	int type = 0;
	const void *data = nullptr; // UnicodeString* (text) or std::string* (image name)
	bool overwrite = true;
	int height = -1;
	int width = -1;
};

struct TextAndColor
{
	UnicodeString string;
	Color color = 0;
};

struct RightClickStruct
{
	int mouseX = 0;
	int mouseY = 0;
	int pos = 0;
};

// ---- combo box (Gadget.h ComboBoxData) --------------------------------------------------------------------------------------------
struct ComboBoxData
{
	bool isEditable = false;
	int maxChars = 0;
	int maxDisplay = 0;
	bool asciiOnly = false;
	bool lettersAndNumbersOnly = false;
	int entryCount = 0;
	bool dontHide = false;
	GameWindow *dropDownButton = nullptr;
	GameWindow *editBox = nullptr;
	GameWindow *listBox = nullptr;
	// the templates the WND parser fills before the gadget exists (ZH ComboBoxData.entryData / listboxData pointers)
	EntryData entryTemplate;
	ListboxData listboxTemplate;
};
