// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/GUI/GameWindowManagerScript.h.  Port of ZH GameWindowManagerScript.cpp (parse functions, parseWindow, parseChildWindows,
// createGadget's draw data distribution, parseLayoutBlock, winCreateFromScript).

#include "GameClient/GUI/GameWindowManagerScript.h"

#include "GameClient/GUI/Gadgets.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace
{

// A malformed value is a parse failure (ZH crashes or reads garbage); the loader reports it.
struct ScriptError : std::runtime_error
{
	explicit ScriptError(const std::string &m) : std::runtime_error(m) {}
};

// ---- char cursor over the file (ZH File::scanString / readUntilSemicolon) ------------------------------------------------------------------------

class Cursor
{
public:
	explicit Cursor(const std::string &t) : m_t(t) {}

	// RAMFile::scanString: whitespace-separated token
	bool scanString(std::string &out)
	{
		out.clear();
		while (m_p < m_t.size() && std::isspace((unsigned char)m_t[m_p]))
		{
			++m_p;
		}
		if (m_p >= m_t.size())
		{
			m_p = m_t.size();
			return false;
		}
		do
		{
			out.push_back(m_t[m_p]);
			++m_p;
		} while (m_p < m_t.size() && !std::isspace((unsigned char)m_t[m_p]));
		return true;
	}
	// ZH readUntilSemicolon: leading whitespace skipped, each later whitespace character becomes one space, ';' ends the value
	std::string readUntilSemicolon()
	{
		std::string buffer;
		bool start = true;
		while (m_p < m_t.size() && buffer.size() < 2047)
		{
			const char c = m_t[m_p++];
			if (std::isspace((unsigned char)c))
			{
				if (!start)
				{
					buffer.push_back(' ');
				}
			}
			else
			{
				start = false;
				if (c == ';')
				{
					return buffer;
				}
				buffer.push_back(c);
			}
		}
		throw ScriptError("a value is not terminated by ';' (or is longer than the 2048 byte read buffer)");
	}
	void skip(std::size_t n) { m_p = std::min(m_t.size(), m_p + n); }
	int scanInt()
	{
		while (m_p < m_t.size() && std::isspace((unsigned char)m_t[m_p]))
		{
			++m_p;
		}
		int v = 0;
		bool any = false;
		while (m_p < m_t.size() && std::isdigit((unsigned char)m_t[m_p]))
		{
			const int digit = m_t[m_p++] - '0';
			if (v > (std::numeric_limits<int>::max() - digit) / 10)
			{
				throw ScriptError("FILE_VERSION does not fit a 32-bit integer");
			}
			v = v * 10 + digit;
			any = true;
		}
		if (!any)
		{
			throw ScriptError("FILE_VERSION has no number");
		}
		return v;
	}
	void nextLine()
	{
		while (m_p < m_t.size() && m_t[m_p] != '\n')
		{
			++m_p;
		}
		if (m_p < m_t.size())
		{
			++m_p;
		}
	}

private:
	const std::string &m_t;
	std::size_t m_p = 0;
};

// ---- strtok over a value --------------------------------------------------------------------------------------------------------------------------

class Tok
{
public:
	explicit Tok(const std::string &s) : m_s(s) {}
	// strtok(NULL, seps): the next token; throws when the value ends early (ZH would pass NULL to atoi/sscanf)
	std::string next(const char *seps)
	{
		std::string t;
		if (!nextOrEmpty(t, seps))
		{
			throw ScriptError("the value '" + m_s + "' ends early");
		}
		return t;
	}
	bool nextOrEmpty(std::string &out, const char *seps)
	{
		out.clear();
		while (m_p < m_s.size() && std::strchr(seps, m_s[m_p]))
		{
			++m_p;
		}
		if (m_p >= m_s.size())
		{
			return false;
		}
		while (m_p < m_s.size() && !std::strchr(seps, m_s[m_p]))
		{
			out.push_back(m_s[m_p++]);
		}
		if (m_p < m_s.size())
		{
			++m_p; // strtok replaces the delimiter that ended the token with NUL: the next call starts after it
		}
		return true;
	}
	// the rest of the buffer from the first '"' (ZH: scan to the quote, strtok with the quote as separator)
	std::string quoted(const char *stringSeps)
	{
		std::size_t q = m_s.find('"');
		if (q == std::string::npos)
		{
			throw ScriptError("the value '" + m_s + "' has no quoted string");
		}
		m_p = q + 1;
		return next(stringSeps);
	}

private:
	const std::string &m_s;
	std::size_t m_p = 0;
};

// sscanf("%d") of ZH, without its undefined behaviour on a value that does not fit an int: leading white space, an optional sign, then decimal digits (a
// trailing remainder is ignored as sscanf does); a value outside the int range is a ScriptError, never a wrapped number.
int parseLeadingInt(const std::string &s)
{
	std::size_t i = 0;
	while (i < s.size() && std::isspace((unsigned char)s[i]))
	{
		++i;
	}
	bool negative = false;
	if (i < s.size() && (s[i] == '-' || s[i] == '+'))
	{
		negative = s[i] == '-';
		++i;
	}
	if (i >= s.size() || !std::isdigit((unsigned char)s[i]))
	{
		throw ScriptError("'" + s + "' is not a number");
	}
	// accumulate as a negative number (the int range holds one more negative value)
	int v = 0;
	for (; i < s.size() && std::isdigit((unsigned char)s[i]); ++i)
	{
		const int digit = s[i] - '0';
		if (v < (std::numeric_limits<int>::min() + digit) / 10)
		{
			throw ScriptError("'" + s + "' does not fit a 32-bit integer");
		}
		v = v * 10 - digit;
	}
	if (!negative)
	{
		if (v == std::numeric_limits<int>::min())
		{
			throw ScriptError("'" + s + "' does not fit a 32-bit integer");
		}
		v = -v;
	}
	return v;
}

int scanInt(const std::string &s)
{
	return parseLeadingInt(s);
}

std::uint32_t scanUnsigned(const std::string &s)
{
	return (std::uint32_t)parseLeadingInt(s);
}

bool stricmpEq(const std::string &a, const char *b)
{
	std::size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return i == a.size() && b[i] == 0;
}

// ZH parseBitString: 'A+B+C' over a name list; an unknown flag is ignored (a DEBUG_LOG in ZH), reported here
void parseBitString(const std::string &in, std::uint32_t *bits, const char *const *flagList, std::vector<std::string> &unknown)
{
	if (in.compare(0, 4, "NULL") == 0)
	{
		return;
	}
	std::size_t pos = 0;
	while (pos <= in.size())
	{
		const std::size_t plus = in.find('+', pos);
		const std::string tok = in.substr(pos, plus == std::string::npos ? std::string::npos : plus - pos);
		if (!tok.empty())
		{
			bool found = false;
			for (int i = 0; flagList[i]; ++i)
			{
				if (stricmpEq(tok, flagList[i]))
				{
					*bits |= (1u << i);
					found = true;
					break;
				}
			}
			if (!found)
			{
				unknown.push_back(tok);
			}
		}
		if (plus == std::string::npos)
		{
			break;
		}
		pos = plus + 1;
	}
}

// ---- the parse state -------------------------------------------------------------------------------------------------------------------------------

struct DrawArrays
{
	WinDrawData dropDown[3][MAX_DRAW_DATA]; // enabled, disabled, hilite
	WinDrawData editBox[3][MAX_DRAW_DATA];
	WinDrawData listBox[3][MAX_DRAW_DATA];
	WinDrawData upButton[3][MAX_DRAW_DATA];
	WinDrawData downButton[3][MAX_DRAW_DATA];
	WinDrawData slider[3][MAX_DRAW_DATA];
	WinDrawData sliderThumb[3][MAX_DRAW_DATA];
};

struct GadgetTemplates
{
	SliderData slider;
	ListboxData list;
	EntryData entry;
	ComboBoxData combo;
};

class Parser
{
public:
	Parser(GameWindowManager &mgr, const HeaderTemplateManager *templates, const WindowFunctionLexicon *lexicon, WindowLayoutInfo &info)
		: m_mgr(mgr), m_templates(templates), m_lexicon(lexicon), m_info(info) {}

	GameWindow *parseWindow(Cursor &in);

	Color m_defEnabledColor = 0, m_defDisabledColor = 0, m_defBackgroundColor = 0, m_defHiliteColor = 0, m_defSelectedColor = 0, m_defTextColor = 0;
	GameFont *m_defFont = nullptr;
	std::vector<GameWindow *> m_stack;
	std::vector<std::string> m_unknownFlags;

private:
	struct Parsed
	{
		WinInstanceData inst;
		std::string type;
		GadgetTemplates data;
		DrawArrays draw;
		GameWinSystemFunc systemFunc;
		GameWinInputFunc inputFunc;
		GameWinTooltipFunc tooltipFunc;
		GameWinDrawFunc drawFunc;
		int x = 0, y = 0, width = 0, height = 0;
	};

	void parseScreenRect(const std::string &buffer, Parsed &p);
	bool parseField(const std::string &key, const std::string &value, Parsed &p);
	void parseDrawData(const std::string &token, const std::string &buffer, Parsed &p);
	GameWindow *createWindow(Parsed &p);
	GameWindow *createGadget(Parsed &p, GameWindow *parent);
	void parseChildWindows(GameWindow *window, Cursor &in);
	void parseDefaultColor(Color *color, Cursor &in);
	void setWindowText(GameWindow *window, const std::string &textLabel);

	GameWindowManager &m_mgr;
	const HeaderTemplateManager *m_templates;
	const WindowFunctionLexicon *m_lexicon;
	WindowLayoutInfo &m_info;

public:
	// Every top-level window created so far (a window without a parent), also while its children are still being parsed: a parse that fails
	// destroys exactly these trees, so a failed load leaves no window behind.
	std::vector<GameWindow *> m_createdRoots;
};

void parseColorRGB(Color *color, const std::string &buffer)
{
	Tok t(buffer);
	const int r = std::atoi(t.next(" \t\n\r").c_str());
	const int g = std::atoi(t.next(" \t\n\r").c_str());
	const int b = std::atoi(t.next(" \t\n\r").c_str());
	*color = GameMakeColor((std::uint8_t)r, (std::uint8_t)g, (std::uint8_t)b, 255);
}

void Parser::parseDefaultColor(Color *color, Cursor &in)
{
	std::string eq;
	in.scanString(eq); // eat '='
	const std::string buffer = in.readUntilSemicolon();
	if (buffer == "TRANSPARENT")
	{
		*color = WIN_COLOR_UNDEFINED;
	}
	else
	{
		parseColorRGB(color, buffer);
	}
}

void Parser::parseScreenRect(const std::string &buffer, Parsed &p)
{
	const char *seps = " ,:=\n\r\t";
	Tok t(buffer);
	t.next(seps); // SCREENRECT token (the key)
	t.next(seps); // UPPERLEFT
	IRegion2D screenRegion;
	ICoord2D createRes;
	screenRegion.lo.x = scanInt(t.next(seps));
	screenRegion.lo.y = scanInt(t.next(seps));
	t.next(seps); // BOTTOMRIGHT
	screenRegion.hi.x = scanInt(t.next(seps));
	screenRegion.hi.y = scanInt(t.next(seps));
	t.next(seps); // CREATIONRESOLUTION
	createRes.x = scanInt(t.next(seps));
	createRes.y = scanInt(t.next(seps));
	if (createRes.x == 0 || createRes.y == 0)
	{
		throw ScriptError("CREATIONRESOLUTION has a zero side");
	}
	// shrink or expand the screen region by the current resolution over the creation resolution (the APT stage is 1024 x 768)
	const float xScale = (float)m_mgr.displayWidth() / (float)createRes.x;
	const float yScale = (float)m_mgr.displayHeight() / (float)createRes.y;
	// a coordinate that the scale takes outside the int range, and a size or offset that does not fit, are errors (the float -> int conversion and the
	// subtractions would otherwise be undefined)
	auto scaled = [](int v, float scale) {
		const double r = (double)((float)v * scale);
		if (!(r > -2147483000.0 && r < 2147483000.0))
		{
			throw ScriptError("a SCREENRECT value is outside the 32-bit range after scaling");
		}
		return (int)r;
	};
	auto difference = [](int a, int b) {
		const long long d = (long long)a - (long long)b;
		if (d < std::numeric_limits<int>::min() || d > std::numeric_limits<int>::max())
		{
			throw ScriptError("a SCREENRECT size or offset does not fit a 32-bit integer");
		}
		return (int)d;
	};
	screenRegion.lo.x = scaled(screenRegion.lo.x, xScale);
	screenRegion.lo.y = scaled(screenRegion.lo.y, yScale);
	screenRegion.hi.x = scaled(screenRegion.hi.x, xScale);
	screenRegion.hi.y = scaled(screenRegion.hi.y, yScale);
	GameWindow *parent = m_stack.empty() ? nullptr : m_stack.back();
	if (parent)
	{
		ICoord2D parentScreenPos;
		parent->winGetScreenPosition(&parentScreenPos.x, &parentScreenPos.y);
		p.x = difference(screenRegion.lo.x, parentScreenPos.x);
		p.y = difference(screenRegion.lo.y, parentScreenPos.y);
	}
	else
	{
		p.x = screenRegion.lo.x;
		p.y = screenRegion.lo.y;
	}
	p.width = difference(screenRegion.hi.x, screenRegion.lo.x);
	p.height = difference(screenRegion.hi.y, screenRegion.lo.y);
}

void Parser::parseDrawData(const std::string &token, const std::string &buffer, Parsed &p)
{
	struct Row
	{
		const char *token;
		WinDrawData *data;
	};
	DrawArrays &d = p.draw;
	const Row rows[] = {
		{ "ENABLEDDRAWDATA", p.inst.m_enabledDrawData }, { "DISABLEDDRAWDATA", p.inst.m_disabledDrawData }, { "HILITEDRAWDATA", p.inst.m_hiliteDrawData },
		{ "LISTBOXENABLEDUPBUTTONDRAWDATA", d.upButton[0] }, { "LISTBOXDISABLEDUPBUTTONDRAWDATA", d.upButton[1] }, { "LISTBOXHILITEUPBUTTONDRAWDATA", d.upButton[2] },
		{ "LISTBOXENABLEDDOWNBUTTONDRAWDATA", d.downButton[0] }, { "LISTBOXDISABLEDDOWNBUTTONDRAWDATA", d.downButton[1] }, { "LISTBOXHILITEDOWNBUTTONDRAWDATA", d.downButton[2] },
		{ "LISTBOXENABLEDSLIDERDRAWDATA", d.slider[0] }, { "LISTBOXDISABLEDSLIDERDRAWDATA", d.slider[1] }, { "LISTBOXHILITESLIDERDRAWDATA", d.slider[2] },
		{ "SLIDERTHUMBENABLEDDRAWDATA", d.sliderThumb[0] }, { "SLIDERTHUMBDISABLEDDRAWDATA", d.sliderThumb[1] }, { "SLIDERTHUMBHILITEDRAWDATA", d.sliderThumb[2] },
		{ "COMBOBOXDROPDOWNBUTTONENABLEDDRAWDATA", d.dropDown[0] }, { "COMBOBOXDROPDOWNBUTTONDISABLEDDRAWDATA", d.dropDown[1] }, { "COMBOBOXDROPDOWNBUTTONHILITEDRAWDATA", d.dropDown[2] },
		{ "COMBOBOXEDITBOXENABLEDDRAWDATA", d.editBox[0] }, { "COMBOBOXEDITBOXDISABLEDDRAWDATA", d.editBox[1] }, { "COMBOBOXEDITBOXHILITEDRAWDATA", d.editBox[2] },
		{ "COMBOBOXLISTBOXENABLEDDRAWDATA", d.listBox[0] }, { "COMBOBOXLISTBOXDISABLEDDRAWDATA", d.listBox[1] }, { "COMBOBOXLISTBOXHILITEDRAWDATA", d.listBox[2] },
	};
	WinDrawData *target = nullptr;
	for (const Row &r : rows)
	{
		if (token == r.token)
		{
			target = r.data;
			break;
		}
	}
	if (!target)
	{
		throw ScriptError("ParseDrawData, undefined token '" + token + "'");
	}
	const char *seps = " :,\n\r\t";
	Tok t(buffer);
	for (int i = 0; i < MAX_DRAW_DATA; ++i)
	{
		t.next(seps); // IMAGE
		const std::string image = t.next(seps);
		target[i].image = image == "NoImage" ? std::string() : image;
		t.next(seps); // COLOR
		const std::uint32_t r = scanUnsigned(t.next(seps)), g = scanUnsigned(t.next(seps)), b = scanUnsigned(t.next(seps)), a = scanUnsigned(t.next(seps));
		target[i].color = GameMakeColor((int)r, (int)g, (int)b, (int)a);
		t.next(seps); // BORDERCOLOR
		const std::uint32_t br = scanUnsigned(t.next(seps)), bg = scanUnsigned(t.next(seps)), bb = scanUnsigned(t.next(seps)), ba = scanUnsigned(t.next(seps));
		target[i].borderColor = GameMakeColor((int)br, (int)bg, (int)bb, (int)ba);
	}
}

bool Parser::parseField(const std::string &key, const std::string &buffer, Parsed &p)
{
	WinInstanceData &inst = p.inst;
	const char *seps = " :,\n\r\t";
	if (key == "NAME")
	{
		Tok t(buffer);
		inst.m_decoratedNameString = t.quoted("\"");
		// ZH: m_id = nameToKey(decorated name): a stable per-name key.  The id of a gadget is only used by winGetWindowFromId and the
		// GGM_FOCUS_CHANGE message; the port hashes the name (FNV-1a) instead of using the NameKeyGenerator's counter
		std::uint32_t h = 2166136261u;
		for (char c : inst.m_decoratedNameString)
		{
			h = (h ^ (std::uint8_t)c) * 16777619u;
		}
		inst.m_id = (int)(h & 0x7FFFFFFF);
	}
	else if (key == "STATUS")
	{
		inst.m_status = 0;
		parseBitString(buffer, &inst.m_status, WindowStatusNames(), m_unknownFlags);
	}
	else if (key == "STYLE")
	{
		inst.m_style = 0;
		parseBitString(buffer, &inst.m_style, WindowStyleNames(), m_unknownFlags);
	}
	else if (key == "SYSTEMCALLBACK" || key == "INPUTCALLBACK" || key == "TOOLTIPCALLBACK" || key == "DRAWCALLBACK")
	{
		Tok t(buffer);
		const std::string name = t.quoted("\"");
		bool found = false;
		if (m_lexicon)
		{
			if (key == "SYSTEMCALLBACK")
			{
				auto it = m_lexicon->system.find(name);
				if (it != m_lexicon->system.end())
				{
					p.systemFunc = it->second;
					found = true;
				}
			}
			else if (key == "INPUTCALLBACK")
			{
				auto it = m_lexicon->input.find(name);
				if (it != m_lexicon->input.end())
				{
					p.inputFunc = it->second;
					found = true;
				}
			}
			else if (key == "TOOLTIPCALLBACK")
			{
				auto it = m_lexicon->tooltip.find(name);
				if (it != m_lexicon->tooltip.end())
				{
					p.tooltipFunc = it->second;
					found = true;
				}
			}
			else
			{
				auto it = m_lexicon->draw.find(name);
				if (it != m_lexicon->draw.end())
				{
					p.drawFunc = it->second;
					found = true;
				}
			}
		}
		if (!found)
		{
			m_info.unknownCallbacks.push_back(key + "=" + name);
		}
	}
	else if (key == "FONT")
	{
		Tok t(buffer);
		t.next(" ,\n\r\t"); // label (NAME)
		const std::string fontName = t.quoted(":,\n\r\t\"");
		t.next(" ,\n\r\t"); // SIZE
		const int fontSize = scanInt(t.next(" ,\n\r\t"));
		t.next(" ,\n\r\t"); // BOLD
		const int fontBold = scanInt(t.next(" ,\n\r\t"));
		inst.m_font = m_mgr.winFindFont(fontName, fontSize, fontBold != 0);
	}
	else if (key == "HEADERTEMPLATE")
	{
		Tok t(buffer);
		inst.m_headerTemplateName = t.quoted("\"");
	}
	else if (key == "LISTBOXDATA")
	{
		// ZH parseListboxData: positional; the optional SCROLLIFATEND is detected by the label after AUTOSCROLL
		ListboxData &l = p.data.list;
		Tok t(buffer);
		t.next(seps); // LENGTH
		l.listLength = (short)scanInt(t.next(seps));
		t.next(seps); // AUTOSCROLL
		l.autoScroll = scanInt(t.next(seps)) != 0;
		std::string label = t.next(seps);
		if (stricmpEq(label, "ScrollIfAtEnd"))
		{
			l.scrollIfAtEnd = scanInt(t.next(seps)) != 0;
			label = t.next(seps); // AUTOPURGE label
		}
		else
		{
			l.scrollIfAtEnd = false;
		}
		// ZH: "c = strtok( NULL, seps ); // value" - the token read above as `label` was the AUTOPURGE label when ScrollIfAtEnd is
		// absent; its value follows
		l.autoPurge = scanInt(t.next(seps)) != 0;
		t.next(seps); // SCROLLBAR
		l.scrollBar = scanInt(t.next(seps)) != 0;
		t.next(seps); // MULTISELECT
		l.multiSelect = scanInt(t.next(seps)) != 0;
		t.next(seps); // COLUMNS
		l.columns = (short)scanInt(t.next(seps));
		l.columnWidthPercentage.clear();
		if (l.columns > 1)
		{
			for (int i = 0; i < l.columns; ++i)
			{
				t.next(seps); // COLUMNSWIDTH label
				l.columnWidthPercentage.push_back(scanInt(t.next(seps)));
			}
		}
		t.next(seps); // FORCESELECT (when columns == 1 this reads the COLUMNSWIDTH label: ZH quirk, kept)
		l.forceSelect = scanInt(t.next(seps)) != 0;
	}
	else if (key == "COMBOBOXDATA")
	{
		ComboBoxData &c = p.data.combo;
		Tok t(buffer);
		t.next(seps);
		c.isEditable = scanInt(t.next(seps)) != 0;
		t.next(seps);
		c.maxChars = scanInt(t.next(seps));
		t.next(seps);
		c.maxDisplay = scanInt(t.next(seps));
		t.next(seps);
		c.asciiOnly = scanInt(t.next(seps)) != 0;
		t.next(seps);
		c.lettersAndNumbersOnly = scanInt(t.next(seps)) != 0;
	}
	else if (key == "SLIDERDATA")
	{
		Tok t(buffer);
		t.next(seps);
		p.data.slider.minVal = scanInt(t.next(seps));
		t.next(seps);
		p.data.slider.maxVal = scanInt(t.next(seps));
	}
	else if (key == "TOOLTIPTEXT")
	{
		Tok t(buffer);
		if (buffer.size() >= 1 && buffer.find('"') != std::string::npos)
		{
			const std::size_t q = buffer.find('"');
			if (buffer.size() - (q + 1) == 1)
			{
				return true; // ZH: strlen(ptr) == 1 -> an empty label
			}
			const std::string label = t.quoted("\n\r\t\"");
			if (label.size() >= MAX_TEXT_LABEL)
			{
				throw ScriptError("TextTooltip label '" + label + "' is too long");
			}
			inst.m_tooltipString = label;
			inst.setTooltipText(m_mgr.winTextLabelToText(label));
		}
	}
	else if (key == "TOOLTIPDELAY")
	{
		Tok t(buffer);
		inst.m_tooltipDelay = scanInt(t.next(seps));
	}
	else if (key == "TEXT")
	{
		Tok t(buffer);
		const std::string label = t.quoted("\n\r\t\"");
		if (label.size() >= MAX_TEXT_LABEL)
		{
			throw ScriptError("Text label '" + label + "' is too long");
		}
		inst.m_textLabelString = label;
	}
	else if (key == "TEXTCOLOR")
	{
		Tok t(buffer);
		TextDrawData *states[3] = { &inst.m_enabledText, &inst.m_disabledText, &inst.m_hiliteText };
		for (int i = 0; i < 3; ++i)
		{
			t.next(seps); // label
			const std::uint32_t r = scanUnsigned(t.next(seps)), g = scanUnsigned(t.next(seps)), b = scanUnsigned(t.next(seps)), a = scanUnsigned(t.next(seps));
			states[i]->color = GameMakeColor((int)r, (int)g, (int)b, (int)a);
			t.next(seps); // border label
			const std::uint32_t br = scanUnsigned(t.next(seps)), bg = scanUnsigned(t.next(seps)), bb = scanUnsigned(t.next(seps)), ba = scanUnsigned(t.next(seps));
			states[i]->borderColor = GameMakeColor((int)br, (int)bg, (int)bb, (int)ba);
		}
	}
	else if (key == "TEXTENTRYDATA")
	{
		EntryData &e = p.data.entry;
		Tok t(buffer);
		t.next(seps);
		e.maxTextLen = (short)scanInt(t.next(seps));
		t.next(seps);
		e.secretText = scanInt(t.next(seps)) != 0;
		t.next(seps);
		e.numericalOnly = scanInt(t.next(seps)) != 0;
		t.next(seps);
		e.alphaNumericalOnly = scanInt(t.next(seps)) != 0;
		t.next(seps);
		e.aSCIIOnly = scanInt(t.next(seps)) != 0;
	}
	else if (key == "IMAGEOFFSET")
	{
		Tok t(buffer);
		inst.m_imageOffset.x = std::atoi(t.next(" \t\n\r").c_str());
		inst.m_imageOffset.y = std::atoi(t.next(" \t\n\r").c_str());
	}
	else if (key == "TOOLTIP")
	{
		inst.setTooltipText(u"Need tooltip translation"); // ZH parseTooltip
	}
	else if (key.size() > 8 && key.compare(key.size() - 8, 8, "DRAWDATA") == 0)
	{
		parseDrawData(key, buffer, p);
	}
	else if (key == "RADIOBUTTONDATA" || key == "STATICTEXTDATA" || key == "TABCONTROLDATA")
	{
		// data of gadget types the loader does not create: consumed (the type check at creation reports the window)
	}
	else
	{
		return false;
	}
	return true;
}

void Parser::setWindowText(GameWindow *window, const std::string &textLabel)
{
	if (textLabel.empty())
	{
		return;
	}
	const UnicodeString theText = m_mgr.winTextLabelToText(textLabel);
	const std::uint32_t style = window->winGetStyle();
	if (BitTest(style, GWS_PUSH_BUTTON))
	{
		GadgetButtonSetText(window, theText);
	}
	else if (BitTest(style, GWS_CHECK_BOX))
	{
		GadgetCheckBoxSetText(window, theText);
	}
	else if (BitTest(style, GWS_ENTRY_FIELD))
	{
		UnicodeString entryText;
		for (char c : textLabel)
		{
			entryText.push_back((char16_t)(unsigned char)c);
		}
		GadgetTextEntrySetText(window, entryText); // ZH: entryText.translate(textLabel): the raw label, not the game text
	}
	else
	{
		window->winSetText(theText);
	}
}

GameWindow *Parser::createGadget(Parsed &p, GameWindow *parent)
{
	WinInstanceData &inst = p.inst;
	const std::string &type = p.type;
	const std::uint32_t status = inst.getStatus();
	inst.m_owner = parent;
	auto copyDraw = [](GameWindow *w, WinDrawData (&src)[3][MAX_DRAW_DATA]) {
		if (!w)
		{
			return;
		}
		WinInstanceData *i = w->winGetInstanceData();
		for (int k = 0; k < MAX_DRAW_DATA; ++k)
		{
			i->m_enabledDrawData[k] = src[0][k];
			i->m_disabledDrawData[k] = src[1][k];
			i->m_hiliteDrawData[k] = src[2][k];
		}
	};
	auto skinSlider = [&](GameWindow *slider) {
		if (!slider)
		{
			return;
		}
		copyDraw(slider, p.draw.slider);
		copyDraw(slider->winGetChild(), p.draw.sliderThumb);
	};
	auto skinList = [&](GameWindow *list) {
		if (!list)
		{
			return;
		}
		copyDraw(GadgetListBoxGetUpButton(list), p.draw.upButton);
		copyDraw(GadgetListBoxGetDownButton(list), p.draw.downButton);
		skinSlider(GadgetListBoxGetSlider(list));
	};
	if (type == "PUSHBUTTON")
	{
		inst.m_style |= GWS_PUSH_BUTTON;
		return m_mgr.gogoGadgetPushButton(parent, status, p.x, p.y, p.width, p.height, &inst, inst.m_font, false);
	}
	if (type == "CHECKBOX")
	{
		inst.m_style |= GWS_CHECK_BOX;
		return m_mgr.gogoGadgetCheckbox(parent, status, p.x, p.y, p.width, p.height, &inst, inst.m_font, false);
	}
	if (type == "VERTSLIDER" || type == "HORZSLIDER")
	{
		inst.m_style |= (type == "VERTSLIDER") ? GWS_VERT_SLIDER : GWS_HORZ_SLIDER;
		GameWindow *w = m_mgr.gogoGadgetSlider(parent, status, p.x, p.y, p.width, p.height, &inst, &p.data.slider, inst.m_font, false);
		if (w && w->winGetChild())
		{
			copyDraw(w->winGetChild(), p.draw.sliderThumb);
		}
		return w;
	}
	if (type == "SCROLLLISTBOX")
	{
		inst.m_style |= GWS_SCROLL_LISTBOX;
		GameWindow *w = m_mgr.gogoGadgetListBox(parent, status, p.x, p.y, p.width, p.height, &inst, &p.data.list, inst.m_font, false);
		skinList(w);
		return w;
	}
	if (type == "COMBOBOX")
	{
		ComboBoxData &c = p.data.combo;
		// ZH createGadget: the entry and list templates the creator copies
		c.entryTemplate = EntryData();
		c.entryTemplate.aSCIIOnly = c.asciiOnly;
		c.entryTemplate.alphaNumericalOnly = c.lettersAndNumbersOnly;
		c.entryTemplate.maxTextLen = (short)c.maxChars;
		c.listboxTemplate = ListboxData();
		c.listboxTemplate.listLength = 10;
		c.listboxTemplate.autoScroll = false;
		c.listboxTemplate.scrollIfAtEnd = false;
		c.listboxTemplate.autoPurge = false;
		c.listboxTemplate.scrollBar = true;
		c.listboxTemplate.multiSelect = false;
		c.listboxTemplate.forceSelect = true;
		c.listboxTemplate.columns = 1;
		c.entryCount = 0;
		inst.m_style |= GWS_COMBO_BOX;
		GameWindow *w = m_mgr.gogoGadgetComboBox(parent, status, p.x, p.y, p.width, p.height, &inst, &c, inst.m_font, false);
		if (w)
		{
			copyDraw(GadgetComboBoxGetDropDownButton(w), p.draw.dropDown);
			copyDraw(GadgetComboBoxGetEditBox(w), p.draw.editBox);
			GameWindow *list = GadgetComboBoxGetListBox(w);
			copyDraw(list, p.draw.listBox);
			skinList(list);
		}
		return w;
	}
	if (type == "ENTRYFIELD")
	{
		inst.m_style |= GWS_ENTRY_FIELD;
		return m_mgr.gogoGadgetTextEntry(parent, status, p.x, p.y, p.width, p.height, &inst, &p.data.entry, inst.m_font, false);
	}
	throw ScriptError("window type '" + type + "' is not created by this loader (RADIOBUTTON, STATICTEXT, PROGRESSBAR, TABCONTROL and TABPANE are not ported, S-177)");
}

GameWindow *Parser::createWindow(Parsed &p)
{
	GameWindow *parent = m_stack.empty() ? nullptr : m_stack.back();
	GameWindow *window;
	if (p.type == "USER")
	{
		window = m_mgr.winCreate(parent, p.inst.getStatus(), p.x, p.y, p.width, p.height, p.systemFunc, &p.inst);
		if (window)
		{
			window->winGetInstanceData()->m_style |= GWS_USER_WINDOW;
			window->winSetWindowId(p.inst.m_id);
		}
	}
	else
	{
		window = createGadget(p, parent);
		if (window)
		{
			window->winSetWindowId(p.inst.m_id);
		}
	}
	if (window)
	{
		if (!parent)
		{
			m_createdRoots.push_back(window);
		}
		if (p.systemFunc)
		{
			window->winSetSystemFunc(p.systemFunc);
		}
		if (p.inputFunc)
		{
			window->winSetInputFunc(p.inputFunc);
		}
		if (p.tooltipFunc)
		{
			window->winSetTooltipFunc(p.tooltipFunc);
		}
		if (p.drawFunc)
		{
			window->winSetDrawFunc(p.drawFunc);
		}
		setWindowText(window, p.inst.m_textLabelString);
		if (parent)
		{
			m_mgr.winSendInputMsg(parent, GWM_SCRIPT_CREATE, (WindowMsgData)p.inst.m_id, 0);
		}
	}
	return window;
}

void Parser::parseChildWindows(GameWindow *window, Cursor &in)
{
	m_stack.push_back(window);
	std::string tok;
	while (true)
	{
		if (!in.scanString(tok))
		{
			break;
		}
		if (tok == "ENDALLCHILDREN" || tok == "END")
		{
			break;
		}
		if (tok == "ENABLEDCOLOR")
		{
			parseDefaultColor(&m_defEnabledColor, in);
		}
		else if (tok == "DISABLEDCOLOR")
		{
			parseDefaultColor(&m_defDisabledColor, in);
		}
		else if (tok == "HILITECOLOR")
		{
			parseDefaultColor(&m_defHiliteColor, in);
		}
		else if (tok == "SELECTEDCOLOR")
		{
			parseDefaultColor(&m_defSelectedColor, in);
		}
		else if (tok == "TEXTCOLOR")
		{
			parseDefaultColor(&m_defTextColor, in);
		}
		else if (tok == "WINDOW")
		{
			if (parseWindow(in) == nullptr)
			{
				throw ScriptError("a child window could not be created");
			}
		}
	}
	GameWindow *last = m_stack.back();
	m_stack.pop_back();
	if (last != window)
	{
		throw ScriptError("parseChildWindows: unmatched window on stack");
	}
}

GameWindow *Parser::parseWindow(Cursor &in)
{
	Parsed p;
	GameWindow *window = nullptr;
	p.inst.init();
	p.inst.m_enabledText.color = p.inst.m_enabledText.borderColor = m_defTextColor;
	p.inst.m_disabledText.color = p.inst.m_disabledText.borderColor = m_defTextColor;
	p.inst.m_hiliteText.color = p.inst.m_hiliteText.borderColor = m_defTextColor;
	p.inst.m_font = m_defFont;
	// the first two lines are WINDOWTYPE and SCREENRECT
	{
		const std::string buffer = in.readUntilSemicolon();
		Tok t(buffer);
		const std::string key = t.next(" =;\n\r\t");
		if (key != "WINDOWTYPE")
		{
			throw ScriptError("the first line of a window is '" + key + "', not WINDOWTYPE");
		}
		p.type = t.next(" =;\n\r\t");
	}
	{
		const std::string buffer = in.readUntilSemicolon();
		Tok t(buffer);
		const std::string key = t.next(" =;\n\r\t");
		if (key != "SCREENRECT")
		{
			throw ScriptError("the second line of a window is '" + key + "', not SCREENRECT");
		}
		parseScreenRect(buffer, p);
	}
	std::string token;
	while (true)
	{
		if (!in.scanString(token))
		{
			throw ScriptError("the file ends inside a window (no END)");
		}
		if (token == "END")
		{
			// a HEADERTEMPLATE replaces the font; a template that does not exist is an error (ZH: getFontFromTemplate answers NULL and the
			// font silently stays; the retail data has no such case)
			if (!p.inst.m_headerTemplateName.empty())
			{
				const HeaderTemplate *ht = m_templates ? m_templates->findHeaderTemplate(p.inst.m_headerTemplateName) : nullptr;
				if (!ht)
				{
					throw ScriptError("HEADERTEMPLATE '" + p.inst.m_headerTemplateName + "' is not defined");
				}
				p.inst.m_font = m_mgr.winFindFont(ht->fontName, ht->point, ht->bold);
			}
			if (window == nullptr)
			{
				window = createWindow(p);
			}
			return window;
		}
		if (token == "CHILD")
		{
			window = createWindow(p);
			if (window == nullptr)
			{
				return nullptr;
			}
			parseChildWindows(window, in);
			continue;
		}
		std::string eq;
		std::string value;
		bool handled = false;
		// the key table of ZH gameWindowFieldList
		static const char *const keys[] = { "NAME", "STATUS", "STYLE", "SYSTEMCALLBACK", "INPUTCALLBACK", "TOOLTIPCALLBACK", "DRAWCALLBACK", "FONT", "HEADERTEMPLATE",
			"LISTBOXDATA", "COMBOBOXDATA", "SLIDERDATA", "RADIOBUTTONDATA", "TOOLTIPTEXT", "TOOLTIPDELAY", "TEXT", "TEXTCOLOR", "STATICTEXTDATA", "TEXTENTRYDATA",
			"TABCONTROLDATA", "ENABLEDDRAWDATA", "DISABLEDDRAWDATA", "HILITEDRAWDATA", "LISTBOXENABLEDUPBUTTONDRAWDATA", "LISTBOXENABLEDDOWNBUTTONDRAWDATA",
			"LISTBOXENABLEDSLIDERDRAWDATA", "LISTBOXDISABLEDUPBUTTONDRAWDATA", "LISTBOXDISABLEDDOWNBUTTONDRAWDATA", "LISTBOXDISABLEDSLIDERDRAWDATA",
			"LISTBOXHILITEUPBUTTONDRAWDATA", "LISTBOXHILITEDOWNBUTTONDRAWDATA", "LISTBOXHILITESLIDERDRAWDATA", "SLIDERTHUMBENABLEDDRAWDATA",
			"SLIDERTHUMBDISABLEDDRAWDATA", "SLIDERTHUMBHILITEDRAWDATA", "COMBOBOXDROPDOWNBUTTONENABLEDDRAWDATA", "COMBOBOXDROPDOWNBUTTONDISABLEDDRAWDATA",
			"COMBOBOXDROPDOWNBUTTONHILITEDRAWDATA", "COMBOBOXEDITBOXENABLEDDRAWDATA", "COMBOBOXEDITBOXDISABLEDDRAWDATA", "COMBOBOXEDITBOXHILITEDRAWDATA",
			"COMBOBOXLISTBOXENABLEDDRAWDATA", "COMBOBOXLISTBOXDISABLEDDRAWDATA", "COMBOBOXLISTBOXHILITEDRAWDATA", "IMAGEOFFSET", "TOOLTIP", nullptr };
		for (int i = 0; keys[i]; ++i)
		{
			if (token == keys[i])
			{
				in.scanString(eq); // eat '='
				value = in.readUntilSemicolon();
				parseField(token, value, p);
				handled = true;
				break;
			}
		}
		if (!handled)
		{
			if (token == "DATA")
			{
				// the old positional DATA field (ZH parseData): the five gadget types here carry named data blocks in every shipped file
				in.scanString(eq);
				in.readUntilSemicolon();
				throw ScriptError("the legacy DATA field is not supported");
			}
			// unrecognized: eat the associated data
			in.readUntilSemicolon();
		}
	}
}

} // namespace

GameWindow *WindowScriptLoader::createFromScript(const std::string &name, const std::string &text, WindowLayoutInfo *info, std::string *error)
{
	WindowLayoutInfo scriptInfo;
	GameWindow *firstWindow = nullptr;
	Parser parser(m_manager, m_headerTemplates, m_lexicon, scriptInfo);
	try
	{
		Cursor in(text);
		in.skip(std::strlen("FILE_VERSION = "));
		scriptInfo.version = in.scanInt();
		in.nextLine();
		if (scriptInfo.version >= 2)
		{
			std::string tok;
			if (!in.scanString(tok) || tok != "STARTLAYOUTBLOCK")
			{
				throw ScriptError("the layout block (STARTLAYOUTBLOCK) is missing");
			}
			while (true)
			{
				if (!in.scanString(tok))
				{
					throw ScriptError("the layout block is not closed");
				}
				if (tok == "ENDLAYOUTBLOCK")
				{
					break;
				}
				if (tok == "LAYOUTINIT" || tok == "LAYOUTUPDATE" || tok == "LAYOUTSHUTDOWN")
				{
					// ZH parseLayoutBlock: readUntilSemicolon, then strtok( buffer, " =" ) - the first token is the callback name
					const std::string buffer = in.readUntilSemicolon();
					Tok t(buffer);
					const std::string first = t.next(" =");
					(tok == "LAYOUTINIT" ? scriptInfo.initName : tok == "LAYOUTUPDATE" ? scriptInfo.updateName : scriptInfo.shutdownName) = first;
				}
			}
		}
		else
		{
			scriptInfo.initName = scriptInfo.updateName = scriptInfo.shutdownName = "[None]";
		}
		std::string tok;
		while (in.scanString(tok))
		{
			if (tok == "END")
			{
				continue;
			}
			std::string eq;
			if (tok == "ENABLEDCOLOR" || tok == "DISABLEDCOLOR" || tok == "HILITECOLOR" || tok == "SELECTEDCOLOR" || tok == "TEXTCOLOR" || tok == "BACKGROUNDCOLOR")
			{
				in.scanString(eq);
				const std::string buffer = in.readUntilSemicolon();
				Color *target = tok == "ENABLEDCOLOR" ? &parser.m_defEnabledColor : tok == "DISABLEDCOLOR" ? &parser.m_defDisabledColor : tok == "HILITECOLOR" ? &parser.m_defHiliteColor
					: tok == "SELECTEDCOLOR" ? &parser.m_defSelectedColor : tok == "TEXTCOLOR" ? &parser.m_defTextColor : &parser.m_defBackgroundColor;
				if (buffer == "TRANSPARENT")
				{
					*target = WIN_COLOR_UNDEFINED;
				}
				else
				{
					parseColorRGB(target, buffer);
				}
			}
			else if (tok == "FONT")
			{
				in.scanString(eq);
				in.readUntilSemicolon(); // ZH parseDefaultFont: the value is read and dropped ("@todo font parsing")
			}
			else if (tok == "WINDOW")
			{
				GameWindow *window = parser.parseWindow(in);
				if (firstWindow == nullptr)
				{
					firstWindow = window;
				}
				scriptInfo.windows.push_back(window);
			}
		}
	}
	catch (const ScriptError &e)
	{
		if (error)
		{
			*error = name + ": " + e.what();
		}
		// transactional: every tree this load created, whether or not it reached scriptInfo.windows (a parent whose CHILD failed is only in the parser's
		// list), is destroyed; windows that existed before are untouched
		for (GameWindow *w : parser.m_createdRoots)
		{
			m_manager.winDestroy(w);
		}
		m_manager.processDestroyList();
		return nullptr;
	}
	for (const std::string &u : parser.m_unknownFlags)
	{
		scriptInfo.unknownCallbacks.push_back("flag:" + u); // ParseBitString: Invalid flag (a DEBUG_LOG in ZH), visible to the caller
	}
	if (info)
	{
		*info = scriptInfo;
	}
	return firstWindow;
}
