// OpenBFME. GPL-3.0.
//
// The ImageComboBox gadget of the lobby's colour column (window\Apt\ImageComboBox.wnd).  It is a BFME addition with no Zero Hour source.
//
// TARGET FACTS (RotWK window\Apt\ImageComboBox.wnd, game.dat names): a USER window with SYSTEMCALLBACK "GadgetImageComboBoxSystem", INPUTCALLBACK
// "GadgetImageComboBoxInput", DRAWCALLBACK "W3DGadgetImageComboBoxDraw", a CHILD push button `DropDownButton` (the AptVSliderDownButton images) and a
// hidden CHILD scroll list box.  DONOR (open-bfme-2 / BFME1 SkirmishScreenState.cpp `Rva00528B60Combo`): the screen asks it for length, item data of an
// entry, the selected entry, sets the selected entry and `invoke(bool)`; the colour column of the player list holds one per slot.
// RotWK (S-001 caveat; static disassembly, review apt4-r1): the system / input / draw callbacks are at 0x7254A7 / 0x7252B6 / 0x4A1EA3; the system
// function forwards the list's GLM_SELECTED as the owner's GCM_SELECTED; the lobby's colour population adds 20 x 20 IMAGE cells (AptRandomColor for
// random, AptWhiteBox tinted with the colour) with the colour index / -1 as item data; the draw reads the selected image and its size and centres it.
// INFERENCE [S-178]: the key and child handling of the native callbacks is broader than the port's (drop-down toggle on a click or the button, the list
// below, selection by click); the entries are images with a tint, the window draws the selected entry's image centred in the box left of the button.

#include "GameClient/GUI/Gadgets.h"

namespace
{
struct ImageCell
{
	std::string image;
	Color tint = 0xFFFFFFFFu;
};

struct ImageComboData
{
	GameWindow *dropDown = nullptr;
	GameWindow *list = nullptr;
	int entryCount = 0;
	int selected = -1;
	struct Cell
	{
		std::string image;
		Color tint = 0xFFFFFFFFu;
	};
	std::vector<Cell> cells; // the image and tint of each entry (the selected one is drawn in the box)
	int closedHeight = 0;
	int maxDisplay = 10;
};

constexpr int kCellSize = 20; // the colour population adds 20 x 20 cells

ImageComboData *dataOf(GameWindow *w) { return static_cast<ImageComboData *>(w->winGetUserData()); }

// The children exist only after winCreate returned (the loader creates them afterwards): found on first use.
ImageComboData *bind(GameWindow *window)
{
	ImageComboData *d = dataOf(window);
	if (!d)
	{
		d = new ImageComboData();
		window->winSetUserData(d);
	}
	if (!d->dropDown || !d->list)
	{
		for (GameWindow *c = window->winGetChild(); c; c = c->winGetNext())
		{
			if (!d->dropDown && BitTest(c->winGetStyle(), GWS_PUSH_BUTTON))
			{
				d->dropDown = c;
				c->winSetOwner(window);
			}
			else if (!d->list && BitTest(c->winGetStyle(), GWS_SCROLL_LISTBOX))
			{
				d->list = c;
				c->winSetOwner(window);
				c->winHide(true);
			}
		}
		if (d->dropDown && d->list)
		{
			int w, h;
			window->winGetSize(&w, &h);
			d->closedHeight = h;
			GadgetListBoxSetAudioFeedback(d->list, true);
		}
	}
	return d;
}

void hideList(GameWindow *window, ImageComboData *d)
{
	if (d->list && !d->list->winIsHidden())
	{
		d->list->winHide(true);
		int w, h;
		window->winGetSize(&w, &h);
		window->winSetSize(w, d->closedHeight);
	}
}

void showList(GameWindow *window, ImageComboData *d)
{
	GameWindowManager &mgr = window->manager();
	mgr.winSetLoneWindow(window);
	if (!d->list->winIsHidden())
	{
		hideList(window, d);
		return;
	}
	d->list->winHide(false);
	if (ListboxData *ld = static_cast<ListboxData *>(d->list->winGetUserData()))
	{
		// lane FB7-1: RW 0x725439 / 0x72525B: the image combo's list follows the pointer from the selected row
		ld->trackHover = ld->hoverActive = true;
		ld->hoverPos = ld->selectPos;
	}
	int w, h;
	window->winGetSize(&w, &h);
	d->closedHeight = h;
	const int rows = std::min(std::max(d->entryCount, 1), d->maxDisplay);
	const int rowHeight = mgr.winFontHeight(d->list->winGetInstanceData()->getFont()) + 2;
	const int listHeight = rows * rowHeight + 4;
	window->winSetSize(w, h + listHeight);
	d->list->winSetPosition(0, h);
	d->list->winSetSize(w, listHeight);
}
} // namespace

WindowMsgHandledType GadgetImageComboBoxInput(GameWindow *window, std::uint32_t msg, WindowMsgData, WindowMsgData)
{
	switch (msg)
	{
		case GWM_LEFT_UP:
		{
			ImageComboData *d = bind(window);
			if (d->list && d->dropDown)
			{
				showList(window, d);
			}
			return MSG_HANDLED;
		}
		default:
			return MSG_IGNORED;
	}
}

WindowMsgHandledType GadgetImageComboBoxSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	GameWindowManager &mgr = window->manager();
	switch (msg)
	{
		case GWM_DESTROY:
			mgr.winSetLoneWindow(nullptr);
			delete dataOf(window);
			window->winSetUserData(nullptr);
			return MSG_HANDLED;
		case GGM_CLOSE:
		{
			ImageComboData *d = bind(window);
			hideList(window, d);
			return MSG_HANDLED;
		}
		case GGM_RESIZED:
		{
			// lane UI-2 (owner feedback F5, "the drop down arrows on the very left"): RotWK RW 0x725688 .. 0x725704. When the list (data + 8) is hidden it
			// takes the new size (RW 0x7256B2); the drop-down button (data + 4) is scaled to the box's height by the aspect of its enabled image (the
			// image's size, Image + 0x24 / + 0x28; float scale, truncated) and placed at the right edge, (width - its width, 0) (RW 0x7256F3 / 0x7256FF).
			// INFERENCE: the port finds the children here (bind) because the skin's loader creates them after the window: before this, the button kept
			// the skin's own rectangle, (0, 0)-(20, 20) on an 800 x 600 page against the parent's (549, 121), i.e. far left of every colour box.
			ImageComboData *d = bind(window);
			if (d->dropDown && d->list && d->list->winIsHidden())
			{
				const int width = (int)mData1, height = (int)mData2;
				d->list->winSetSize(width, height);
				int bw, bh;
				d->dropDown->winGetSize(&bw, &bh);
				const Image *image = nullptr;
				const std::string &imageName = d->dropDown->winGetEnabledImage(0);
				if (!imageName.empty() && mgr.images())
				{
					image = mgr.images()->findImageByName(imageName);
				}
				if (image && image->getImageHeight() > 0)
				{
					const float scale = (float)height / (float)image->getImageHeight();
					bw = (int)((float)image->getImageWidth() * scale);
					bh = (int)((float)image->getImageHeight() * scale);
				}
				else
				{
					// retail reads an unset local here (RW 0x7256EB); the port keeps the button's size and says so
					mgr.note("ImageComboBox: the drop-down button has no enabled image; its size is kept (RW 0x7256EB reads an unset width)");
				}
				d->dropDown->winSetPosition(width - bw, 0);
				d->dropDown->winSetSize(bw, bh);
				d->closedHeight = height;
			}
			return MSG_HANDLED;
		}
		case GBM_SELECTED:
		{
			ImageComboData *d = bind(window);
			if (d->dropDown && msgPtr<GameWindow>(mData1) == d->dropDown)
			{
				showList(window, d);
			}
			return MSG_HANDLED;
		}
		case GLM_SELECTED:
		{
			ImageComboData *d = bind(window);
			if (d->list && msgPtr<GameWindow>(mData1) == d->list)
			{
				hideList(window, d);
				const int selected = (int)(std::intptr_t)mData2;
				if (selected >= 0)
				{
					d->selected = selected;
					mgr.winSendSystemMsg(window->winGetOwner(), GCM_SELECTED, msgData(window), 0);
				}
			}
			return MSG_HANDLED;
		}
		case GCM_DEL_ALL:
		{
			ImageComboData *d = bind(window);
			if (d->list)
			{
				GadgetListBoxReset(d->list);
			}
			d->entryCount = 0;
			d->selected = -1;
			d->cells.clear();
			return MSG_HANDLED;
		}
		case GCM_ADD_ENTRY:
		{
			ImageComboData *d = bind(window);
			if (!d->list)
			{
				return (WindowMsgHandledType)-1;
			}
			const ImageCell &cell = *msgPtr<ImageCell>(mData1);
			ListboxData *listData = static_cast<ListboxData *>(d->list->winGetUserData());
			d->entryCount++;
			if (d->entryCount >= listData->listLength)
			{
				GadgetListBoxSetListLength(d->list, listData->listLength * 2);
			}
			const int index = GadgetListBoxAddEntryImage(d->list, cell.image, -1, 0, kCellSize, kCellSize, true, cell.tint);
			d->cells.push_back({ cell.image, cell.tint });
			return (WindowMsgHandledType)index;
		}
		case GCM_SET_SELECTION:
		{
			ImageComboData *d = bind(window);
			if (d->list)
			{
				const int index = (int)mData1;
				if (index >= 0 && index < d->entryCount)
				{
					d->selected = index;
					GadgetListBoxSetSelected(d->list, index);
				}
				else
				{
					d->selected = -1;
				}
			}
			return MSG_HANDLED;
		}
		case GCM_GET_SELECTION:
		{
			ImageComboData *d = bind(window);
			*msgPtr<int>(mData2) = d->selected;
			return MSG_HANDLED;
		}
		case GCM_SET_ITEM_DATA:
		{
			ImageComboData *d = bind(window);
			if (d->list)
			{
				GadgetListBoxSetItemData(d->list, reinterpret_cast<void *>(mData2), (int)mData1);
			}
			return MSG_HANDLED;
		}
		case GCM_GET_ITEM_DATA:
		{
			ImageComboData *d = bind(window);
			*msgPtr<void *>(mData2) = d->list ? GadgetListBoxGetItemData(d->list, (int)mData1, 0) : nullptr;
			return MSG_HANDLED;
		}
		default:
			return MSG_IGNORED;
	}
}

// ---- API -------------------------------------------------------------------------------------------------------------------------------------

void GadgetImageComboBoxReset(GameWindow *box)
{
	box->manager().winSendSystemMsg(box, GCM_DEL_ALL, 0, 0);
}

int GadgetImageComboBoxAddEntry(GameWindow *box, const std::string &image, Color tint)
{
	ImageCell cell{ image, tint };
	return (int)box->manager().winSendSystemMsg(box, GCM_ADD_ENTRY, msgData(&cell), 0);
}

int GadgetImageComboBoxGetLength(GameWindow *box)
{
	ImageComboData *d = bind(box);
	return d->entryCount;
}

void GadgetImageComboBoxSetSelectedPos(GameWindow *box, int index)
{
	box->manager().winSendSystemMsg(box, GCM_SET_SELECTION, (WindowMsgData)index, 0);
}

void GadgetImageComboBoxGetSelectedPos(GameWindow *box, int *index)
{
	box->manager().winSendSystemMsg(box, GCM_GET_SELECTION, 0, msgData(index));
}

void GadgetImageComboBoxSetItemData(GameWindow *box, int index, void *data)
{
	box->manager().winSendSystemMsg(box, GCM_SET_ITEM_DATA, (WindowMsgData)index, reinterpret_cast<WindowMsgData>(data));
}

void *GadgetImageComboBoxGetItemData(GameWindow *box, int index)
{
	void *data = nullptr;
	box->manager().winSendSystemMsg(box, GCM_GET_ITEM_DATA, (WindowMsgData)index, msgData(&data));
	return data;
}

GameWindow *GadgetImageComboBoxGetListBox(GameWindow *box)
{
	ImageComboData *d = bind(box);
	return d->list;
}

bool GadgetImageComboBoxIsOpen(GameWindow *box)
{
	if (!BitTest(box->winGetStyle(), GWS_USER_WINDOW))
	{
		return false;
	}
	ImageComboData *d = dataOf(box);
	return d && d->list && !d->list->winIsHidden();
}

// ---- draw ------------------------------------------------------------------------------------------------------------------------------------

void W3DGadgetImageComboBoxDraw(GameWindow *window, WinInstanceData *instData)
{
	(void)instData;
	GameWindowManager &mgr = window->manager();
	ImageComboData *d = dataOf(window);
	int x, y;
	ICoord2D size;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&size.x, &size.y);
	const int height = d && d->closedHeight > 0 ? d->closedHeight : size.y;
	int buttonWidth = 0, bh = 0;
	if (d && d->dropDown)
	{
		d->dropDown->winGetSize(&buttonWidth, &bh);
	}
	const Color border = window->winGetEnabledBorderColor(0);
	if (border != WIN_COLOR_UNDEFINED)
	{
		mgr.winOpenRect(border, WIN_DRAW_LINE_WIDTH, x, y, x + size.x - buttonWidth, y + height);
	}
	// the selected entry's image, centred in the box left of the button (its size is the cell size)
	if (d && d->selected >= 0 && d->selected < (int)d->cells.size())
	{
		const auto &cell = d->cells[(std::size_t)d->selected];
		const int boxWidth = size.x - buttonWidth;
		const int x0 = x + (boxWidth - kCellSize) / 2;
		const int y0 = y + (height - kCellSize) / 2;
		mgr.winDrawImage(cell.image, x0, y0, x0 + kCellSize, y0 + kCellSize, cell.tint);
	}
}
