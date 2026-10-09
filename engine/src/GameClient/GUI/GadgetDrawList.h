// OpenBFME. GPL-3.0.
//
// The draw command list of the native gadgets (build step A5: "gadgets produce draw commands in the same contract style as
// AptRenderList").  The ZH gadgets draw through TheDisplay / TheWindowManager (winDrawImage, winFillRect, winOpenRect, winDrawLine,
// DisplayString::draw, setClipRegion); the port records the same calls as commands so the renderer (the Godot canvas of the APT
// player) is a thin translation and the tests assert on the commands.
//
// Order: the draw order of GameWindowManager::winRepaint (windows ascending in the tree, parents before children, a list box's border
// after its text), one command per primitive.  Coordinates are screen pixels of the APT stage (1024 x 768; the windows are placed from
// the placeholder bounds in stage coordinates).  A colour is GameMakeColor ARGB.
//
//   Image      a mapped image by name (the renderer resolves texture and UV through the MappedImageCollection) stretched to the box, tinted
//              by `color` (ZH drawImage colour multiplier; 0xFFFFFFFF = none)
//   FillRect   solid fill of the box (ZH winFillRect)
//   OpenRect   one-pixel outline of the box (ZH winOpenRect)
//   Line       (x0,y0)-(x1,y1)
//   Text       `text` with its top-left at (x0,y0): font, `color`, drop-shadow colour; `wrapWidth` > 0 wraps to that width
//   ClipBegin  set the clip region (ZH setClipRegion + enableClipping(TRUE)); ClipEnd turns clipping off (enableClipping(FALSE))

#pragma once

#include "GameClient/GUI/GameWindow.h"

#include <set>
#include <string>
#include <vector>

struct GadgetDrawCommand
{
	enum class Kind : std::uint8_t
	{
		Image,
		FillRect,
		OpenRect,
		Line,
		Text,
		ClipBegin,
		ClipEnd
	};
	Kind kind = Kind::Image;
	std::string window;   // the window that drew it: its APT instance name when it has one, else its decorated name
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	Color color = 0;
	Color dropColor = 0;
	int lineWidth = 1;
	std::string image;
	UnicodeString text;
	GameFont font;
	int wrapWidth = 0;
	bool wrapCentered = false;
};

class GadgetDrawList
{
public:
	std::vector<GadgetDrawCommand> commands;
	// Images the list needs and the collection does not know (reported once per name, never drawn as a placeholder).
	std::vector<std::string> unresolvedImages;

	std::size_t count(GadgetDrawCommand::Kind kind) const
	{
		std::size_t n = 0;
		for (const GadgetDrawCommand &c : commands)
		{
			if (c.kind == kind)
			{
				++n;
			}
		}
		return n;
	}
	void clear()
	{
		commands.clear();
		unresolvedImages.clear();
		m_seen.clear();
	}
	void noteUnresolved(const std::string &name)
	{
		if (m_seen.insert(name).second)
		{
			unresolvedImages.push_back(name);
		}
	}

private:
	std::set<std::string> m_seen;
};
