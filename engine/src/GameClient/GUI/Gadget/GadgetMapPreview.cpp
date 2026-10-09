// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (W3DControlBar.cpp W3DDrawMapPreview, MapUtil.cpp
// findDrawPositions), as RotWK changes them.
//
// The lobby's map preview window (lane UI-2, owner feedback F5 "no map preview"): MpGameSetup.apt's `CurrentMap` placeholder loads
// window\Apt\MpMapWindow.wnd (BFME2 window.big, mounted under RotWK): a USER window "MapWindow" with SYSTEMCALLBACK "PassSelectedButtonsToParentSystem"
// and DRAWCALLBACK "W3DDrawMapPreview", and eight push buttons "ButtonMapStartPosition0..7" (images AptPlayerStart ...).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the function lexicon pairs the names with RW 0x49D9EC (draw, table entry RW 0xD98F64) and RW 0x6C1501 (system, RW 0xDA1A44);
//   * W3DDrawMapPreview (RW 0x49D9EC): nothing without user data (the map's MapMetaData; ZH draws the default window and a skinny border, RotWK
//     returns); findDrawPositions over the window; black bars (drawFillRect) and 1-pixel 0xFF323232 lines beside the picture on the long side; then, when
//     the window status has IMAGE (0x80) and enabled image 0 exists: enabled image 1 (the ScrollShroud overlay, when set) and then image 0 over (ul, lr)
//     in opaque white; otherwise a 0xFF323232 fill of (ul, lr). ZH's TecBuilding / Cash markers and the skinny border are gone;
//   * findDrawPositions (RW 0x7018E0): k = 1 / max(extent width / w, extent height / h) (single precision), the short side centred: offset =
//     trunc((side - k * extent) * 0.5), the other corner = side - offset, the long side's end = trunc(k * extent);
//   * PassSelectedButtonsToParentSystem (RW 0x6C1501): messages 0x4006 / 0x4007 / 0x4008 / 0x4009 / 0x400B / 0x4031 (target numbering: mouse
//     entering / leaving, selected, selected right ...) go to the window's parent through the window manager's system message; others are not handled;
//   * positionStartSpot (RW 0x7019A0): see MapPreviewPositionStartSpot.
// INFERENCE [S-1480]: a gadget of an APT screen has no parent window in the port (its owner is the screen's pseudo window, AptGadgetLayer): a message the
// parent would get goes to the owner.

#include "GameClient/GUI/Gadgets.h"

#include "GameClient/MapCache.h"

namespace
{
const MapCacheEntry *mapOf(GameWindow *window) { return static_cast<const MapCacheEntry *>(window->winGetUserData()); }
} // namespace

void MapPreviewFindDrawPositions(int startX, int startY, int width, int height, const MapCacheEntry &map, ICoord2D *ul, ICoord2D *lr)
{
	// RW 0x7018E0 (SSE single precision; cvttss2si truncation)
	const float extW = map.extentMax.x - map.extentMin.x;
	const float extH = map.extentMax.y - map.extentMin.y;
	const float rx = extW / (float)width;
	const float ry = extH / (float)height;
	if (rx < ry)
	{
		const float k = 1.0f / ry;
		ul->y = 0;
		ul->x = (int)(((float)width - k * extW) * 0.5f);
		lr->x = width - ul->x;
		lr->y = (int)(k * extH);
	}
	else
	{
		const float k = 1.0f / rx;
		ul->x = 0;
		ul->y = (int)(((float)height - k * extH) * 0.5f);
		lr->x = (int)(k * extW);
		lr->y = height - ul->y;
	}
	ul->x += startX;
	ul->y += startY;
	lr->x += startX;
	lr->y += startY;
}

void MapPreviewPositionStartSpot(GameWindow *button, GameWindow *mapWindow, const Coord3D &pos, const MapCacheEntry &map, GameWindow *const buttons[8])
{
	// RW 0x7019A0: the spot's centre is the waypoint over the map's extent inside the picture (y up the map is up the screen), less half the button's
	// size; a spot overlapping an earlier one (in the button order, up to this button) moves past it on the axis where the overlap is smaller (both
	// on a tie). The overlap uses this button's size for the other one as well.
	if (!button || !mapWindow)
	{
		return;
	}
	int mw, mh, bw, bh;
	mapWindow->winGetSize(&mw, &mh);
	button->winGetSize(&bw, &bh);
	ICoord2D ul, lr;
	MapPreviewFindDrawPositions(0, 0, mw, mh, map, &ul, &lr);
	const float fx = (pos.x - map.extentMin.x) / (map.extentMax.x - map.extentMin.x);
	int x = (int)((fx * (float)(lr.x - ul.x) - (float)(bw / 2)) + (float)ul.x);
	const float fy = 1.0f - (pos.y - map.extentMin.y) / (map.extentMax.y - map.extentMin.y);
	int y = (int)((fy * (float)(lr.y - ul.y) - (float)(bh / 2)) + (float)ul.y);
	for (int i = 0; i < 8; ++i)
	{
		GameWindow *other = buttons[i];
		if (other == button)
		{
			break;
		}
		if (!other)
		{
			continue; // retail reads the position of a null button here (RW 0x701AD5); the port skips it [S-1480]
		}
		int ox, oy;
		other->winGetPosition(&ox, &oy);
		if (x > ox && x < ox + bw && y > oy && y < oy + bh)
		{
			const int dx = ox - x + bw;
			const int dy = oy - y + bh;
			if (dy <= dx)
			{
				y = oy + bh + 1;
			}
			if (dy >= dx)
			{
				x = ox + bw + 1;
			}
		}
	}
	button->winSetPosition(x, y);
}

void W3DDrawMapPreview(GameWindow *window, WinInstanceData *instData)
{
	(void)instData;
	const MapCacheEntry *map = mapOf(window);
	if (!map)
	{
		return; // RW 0x49DA1E: no user data, nothing drawn
	}
	GameWindowManager &mgr = window->manager();
	int x, y, w, h;
	window->winGetScreenPosition(&x, &y);
	window->winGetSize(&w, &h);
	ICoord2D ul, lr;
	MapPreviewFindDrawPositions(x, y, w, h, *map, &ul, &lr);
	const Color black = 0xFF000000u, line = 0xFF323232u;
	const float extW = map->extentMax.x - map->extentMin.x, extH = map->extentMax.y - map->extentMin.y;
	if (extW / (float)w < extH / (float)h)
	{
		// bars left and right (drawFillRect takes x, y, width, height; winFillRect takes the corners)
		mgr.winFillRect(black, 1.0f, x, y, x + (ul.x - x - 1), y + h);
		mgr.winFillRect(black, 1.0f, lr.x + 1, y, lr.x + 1 + (w - lr.x - 1 + x), y + h);
		mgr.winDrawLine(line, 1.0f, ul.x, y, ul.x, y + h);
		mgr.winDrawLine(line, 1.0f, lr.x + 1, y, lr.x + 1, y + h);
	}
	else
	{
		mgr.winFillRect(black, 1.0f, x, y, x + w, y + (ul.y - y - 1));
		mgr.winFillRect(black, 1.0f, x, lr.y + 1, x + w, lr.y + 1 + (h - lr.y - 1 + y));
		mgr.winDrawLine(line, 1.0f, x, ul.y, x + w, ul.y);
		mgr.winDrawLine(line, 1.0f, x, lr.y + 1, x + w, lr.y + 1);
	}
	const std::string &image0 = window->winGetEnabledImage(0);
	if (BitTest(window->winGetStatus(), WIN_STATUS_IMAGE) && !image0.empty())
	{
		const std::string &image1 = window->winGetEnabledImage(1);
		if (!image1.empty())
		{
			mgr.winDrawImage(image1, ul.x, ul.y, lr.x, lr.y, 0xFFFFFFFFu);
		}
		mgr.winDrawImage(image0, ul.x, ul.y, lr.x, lr.y, 0xFFFFFFFFu);
	}
	else
	{
		mgr.winFillRect(line, 1.0f, ul.x, ul.y, lr.x, lr.y);
	}
}

WindowMsgHandledType PassSelectedButtonsToParentSystem(GameWindow *window, std::uint32_t msg, WindowMsgData mData1, WindowMsgData mData2)
{
	// RW 0x6C1501
	if (!window)
	{
		return MSG_IGNORED;
	}
	switch (msg)
	{
		case 0x4006:
		case 0x4007:
		case 0x4008:
		case 0x4009:
		case 0x400B:
		case 0x4031:
		{
			GameWindow *to = window->winGetParent();
			if (!to)
			{
				to = window->winGetOwner(); // [S-1480] an APT gadget's parent is its owner in the port
			}
			if (!to || to == window)
			{
				return MSG_IGNORED;
			}
			return window->manager().winSendSystemMsg(to, msg, mData1, mData2);
		}
		default:
			return MSG_IGNORED;
	}
}
