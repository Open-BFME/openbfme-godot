// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The WND v2 script loader (ZH GUI/GameWindowManagerScript.cpp; the BFME1 decompile GameWindowManagerScript.cpp is the same text where
// it is not an asm dump): `window\apt\<type>.wnd` skins the gadgets of the APT screens (spec menus-apt.md 1.11, 3.1).
//
// A file is `FILE_VERSION = 2;`, a layout block (STARTLAYOUTBLOCK ... ENDLAYOUTBLOCK with LAYOUTINIT / LAYOUTUPDATE / LAYOUTSHUTDOWN) and
// WINDOW blocks of `KEY = value;` lines (CHILD / ENDALLCHILDREN nest windows).  The value fields are parsed positionally exactly as ZH
// does (labels are skipped, not checked), including its quirks (the list box data eats COLUMNSWIDTH as the FORCESELECT label).
// The five gadget types the APT skirmish path uses are created (CHECKBOX, HORZSLIDER / VERTSLIDER, SCROLLLISTBOX, COMBOBOX, ENTRYFIELD)
// plus PUSHBUTTON and USER; a type the loader does not create is an error naming the type (stop S-177).
//
// Callbacks: SYSTEMCALLBACK / INPUTCALLBACK / TOOLTIPCALLBACK / DRAWCALLBACK name entries of the FunctionLexicon; a name that is not in the
// lexicon gives no function (ZH returns NULL: retail's "Apt:None" and "[None]" are such names) and the gadget keeps its own.  Target fact
// (RotWK game.dat 0x00DA3280 style table, 0x00DA3244 status table, key table of the parser): same keys as ZH.

#pragma once

#include "GameClient/GUI/Gadget.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/HeaderTemplate.h"

#include <map>
#include <string>
#include <vector>

// ZH FunctionLexicon (the tables the WND callbacks resolve through): name -> function.
struct WindowFunctionLexicon
{
	std::map<std::string, GameWinSystemFunc> system;
	std::map<std::string, GameWinInputFunc> input;
	std::map<std::string, GameWinTooltipFunc> tooltip;
	std::map<std::string, GameWinDrawFunc> draw;
};

// ZH WindowLayoutInfo
struct WindowLayoutInfo
{
	int version = 0;
	std::string initName, updateName, shutdownName;
	std::vector<GameWindow *> windows; // every top-level WINDOW the file defined, in order
	// callback names the lexicon did not know (not an error: retail skips them)
	std::vector<std::string> unknownCallbacks;
};

class WindowScriptLoader
{
public:
	WindowScriptLoader(GameWindowManager &manager, const HeaderTemplateManager *headerTemplates, const WindowFunctionLexicon *lexicon = nullptr)
		: m_manager(manager), m_headerTemplates(headerTemplates), m_lexicon(lexicon) {}

	// ZH winCreateFromScript: parses `text` (the content of a .wnd file called `name`) and creates its windows; returns the first top-level
	// window, nullptr with *error on a parse failure.  A missing HEADERTEMPLATE named by a window is an error (the font would be wrong).
	GameWindow *createFromScript(const std::string &name, const std::string &text, WindowLayoutInfo *info, std::string *error);

private:
	GameWindowManager &m_manager;
	const HeaderTemplateManager *m_headerTemplates;
	const WindowFunctionLexicon *m_lexicon;
};
