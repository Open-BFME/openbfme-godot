// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameClient/Credits.cpp).
//
// CreditsManager (lane UI-4): the main menu's credits roll. AptMainMenu::Credits makes one, the main menu's update scrolls it and the movie's
// RenderCredits component draws it.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; every function below is tier A in tools/re/rw2decomp.py with byte-matched BFME2 decomp source,
// Credits.cpp / Rva005B77F1Dtor.cpp / BfmeSinkBOEDoBOE.cpp, so the decomp describes RotWK's code):
// - the object (0x48 bytes, vtable RW 0xC8B8A8): the line list + 0x0C, its iterator + 0x10, the shown lines + 0x14, ScrollRate + 0x18,
//   ScrollRateEveryFrames + 0x1C, ScrollDown + 0x20, the title / minor title / normal colours + 0x24 / 0x28 / 0x2C, the current style + 0x30,
//   finished + 0x34, frames since the start + 0x38, the normal font's height + 0x3C, the display width / height + 0x40 / + 0x44;
// - the constructor (RW 0x9C6A7F): rate 1, every 1 frame, scroll down, colours -1, style NORMAL (2), not finished, 0 frames, font height 10,
//   display 0 x 0;
// - load (vslot 2, RW 0x9C667F): "Data\INI\Credits.ini" (RW 0xC8B888) with INI_LOAD_OVERWRITE; its block runs initFromINI over the table RW 0xC8B778
//   (ScrollRate / ScrollRateEveryFrames parseInt, ScrollDown parseBool, TitleColor / MinorTitleColor / NormalColor parseColorInt, Style over the
//   lookup list RW 0xC8B750 TITLE 0 / MINORTITLE 1 / NORMAL 2 / COLUMN 3, Blank RW 0x9C70D4, Text RW 0x9C726A); a rate or frame count <= 0 becomes
//   1; the normal font's height (GlobalLanguage CreditsNormalFont through adjustFontSize RW 0x5E9BEF) is kept;
// - init (vslot 1, RW 0x9C6737): not finished, the iterator at the first line, 0 frames;
// - reset (vslot 9, RW 0x9C6748): the shown list cleared, not finished, the iterator at the first line, 0 frames;
// - Text (RW 0x9C726A -> addText RW 0x9C7113): getNextQuotedAsciiString; getUnicodeString (RW 0x9C69BD): "<BLANK>" is empty, a text with a ':' is a
//   game text label, anything else is widened byte by byte (UnicodeString::translate); a COLUMN line pairs with the previous open COLUMN line;
//   Blank (RW 0x9C70D4) appends a BLANK line;
// - update (vslot 10, RW 0x9C6BB5): ZH CreditsManager::update (the BFME2 decomp's Credits.cpp:232 is that body byte for byte);
// - draw (RW 0x9C6765, the RenderCredits callback RW 0x91B1B8 calls it with the clip's position and size): the display size is taken from the call;
//   each shown line's alpha fades in over the first third of the height and out over the last (the colour's alpha times the fraction), its drop colour
//   is black at that alpha, TITLE / MINORTITLE / NORMAL lines draw at their position, a COLUMN line's two texts are centred on a third and two thirds
//   of the width; DisplayString::draw with x / y drop offsets 1.
// DEVICE (the port): a DisplayString is a text with a font; its size is the FontMetricsSource's width and font height (S-176), the draw a
// GadgetDrawCommand Text at the line's top-left in the units of the draw call. INFERENCE: the font size is adjustFontSize of the font's point size
// against the `xResolution` the caller gives (the device passes the stage's width, where adjustFontSize is the identity at RotWK's 1024 wide menus).

#pragma once

#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/GameWindow.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;
class FontMetricsSource;
class GameTextSource;
class GlobalLanguage;
class INI;
struct FieldParse;

enum CreditsStyle
{
	CREDIT_STYLE_TITLE = 0,
	CREDIT_STYLE_POSITION = 1, // MINORTITLE
	CREDIT_STYLE_NORMAL = 2,
	CREDIT_STYLE_COLUMN = 3,
	CREDIT_STYLE_BLANK = 4
};

struct CreditsLine
{
	int style = CREDIT_STYLE_NORMAL;
	UnicodeString text, secondText;
	bool useSecond = false;
	bool done = false;
	// the line's DisplayStrings: made when the line comes on screen (update), freed when it leaves
	bool hasDisplay = false, hasSecondDisplay = false;
	GameFont font;
	int width = 0, secondWidth = 0;
	int posX = 0, posY = 0;
	int height = 0;
	Color color = 0;
};

class CreditsManager
{
public:
	CreditsManager(); // RW 0x9C6A7F

	// vslot 2 (RW 0x9C667F): Credits.ini through `fs`. Throws INIException as INI::load does (the caller reports it).
	void load(ArchiveFileSystem &fs, const GameTextSource *text, const GlobalLanguage &language, FontMetricsSource &metrics, int xResolution);
	void init();   // vslot 1 (RW 0x9C6737)
	void reset();  // vslot 9 (RW 0x9C6748)
	void update(); // vslot 10 (RW 0x9C6BB5)
	// RW 0x9C6765: the shown lines at (x, y) in a w x h area, appended to `out`
	void draw(float x, float y, float w, float h, GadgetDrawList &out);

	bool isFinished() const { return m_isFinished; }
	int scrollRate() const { return m_scrollRate; }
	int scrollRatePerFrames() const { return m_scrollRatePerFrames; }
	bool scrollDown() const { return m_scrollDown; }
	Color titleColor() const { return m_titleColor; }
	Color positionColor() const { return m_positionColor; }
	Color normalColor() const { return m_normalColor; }
	int normalFontHeight() const { return m_normalFontHeight; }
	int framesSinceStarted() const { return m_framesSinceStarted; }
	const std::list<CreditsLine *> &lines() const { return m_creditLineList; }
	const std::list<CreditsLine *> &shownLines() const { return m_displayedCreditLineList; }
	std::size_t linesLeft() const;

	~CreditsManager();
	CreditsManager(const CreditsManager &) = delete;
	CreditsManager &operator=(const CreditsManager &) = delete;

	static const FieldParse *fieldParseTable(); // RW 0xC8B778
	static void parseBlank(INI *ini, void *instance, void *store, const void *userData); // RW 0x9C710A -> RW 0x9C70D4
	static void parseText(INI *ini, void *instance, void *store, const void *userData);  // RW 0x9C726A

	void addText(const std::string &text); // RW 0x9C7113
	void addBlank();                       // RW 0x9C70D4

private:
	UnicodeString getUnicodeString(const std::string &text) const; // RW 0x9C69BD
	GameFont fontOf(int style) const;

	std::list<CreditsLine *> m_creditLineList;
	std::list<CreditsLine *>::iterator m_creditLineListIt;
	std::list<CreditsLine *> m_displayedCreditLineList;

public: // the INI table writes these by offset
	int m_scrollRate = 1;
	int m_scrollRatePerFrames = 1;
	bool m_scrollDown = true;
	Color m_titleColor = 0xFFFFFFFFu;
	Color m_positionColor = 0xFFFFFFFFu;
	Color m_normalColor = 0xFFFFFFFFu;
	int m_currentStyle = CREDIT_STYLE_NORMAL;

private:
	bool m_isFinished = false;
	int m_framesSinceStarted = 0;
	int m_normalFontHeight = 10;
	int m_displayWidth = 0;
	int m_displayHeight = 0;

	const GameTextSource *m_text = nullptr;
	FontMetricsSource *m_metrics = nullptr;
	GameFont m_fonts[3]; // TITLE, MINORTITLE, NORMAL (GlobalLanguage + 0xE0 / 0xEC / 0xF8, sizes through adjustFontSize)
};
