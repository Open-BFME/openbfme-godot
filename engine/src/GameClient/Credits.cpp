// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameClient/Credits.cpp).
// See GameClient/Credits.h for the target facts.

#include "GameClient/Credits.h"

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"

#include <cstddef>

namespace
{
// RW 0xC8B750
const LookupListRec kCreditsStyleNames[] = { { "TITLE", CREDIT_STYLE_TITLE }, { "MINORTITLE", CREDIT_STYLE_POSITION }, { "NORMAL", CREDIT_STYLE_NORMAL },
	{ "COLUMN", CREDIT_STYLE_COLUMN }, { nullptr, 0 } };

// ZH CREDIT_SPACE_OFFSET
constexpr int kCreditSpaceOffset = 2;

template <typename T>
int fieldOffset(T CreditsManager::*member)
{
	static const CreditsManager probe;
	return (int)(reinterpret_cast<const char *>(&(probe.*member)) - reinterpret_cast<const char *>(&probe));
}

// ZH GameClient/Color.cpp GameGetColorComponents
void colorComponents(Color c, unsigned &r, unsigned &g, unsigned &b, unsigned &a)
{
	a = (c >> 24) & 0xFF;
	r = (c >> 16) & 0xFF;
	g = (c >> 8) & 0xFF;
	b = c & 0xFF;
}

int clamp255(int v)
{
	return v < 0 ? 0 : (v > 255 ? 255 : v);
}
} // namespace

const FieldParse *CreditsManager::fieldParseTable()
{
	// RW 0xC8B778, in its order
	static const FieldParse table[] = {
		{ "ScrollRate", INI::parseInt, nullptr, fieldOffset(&CreditsManager::m_scrollRate) },
		{ "ScrollRateEveryFrames", INI::parseInt, nullptr, fieldOffset(&CreditsManager::m_scrollRatePerFrames) },
		{ "ScrollDown", INI::parseBool, nullptr, fieldOffset(&CreditsManager::m_scrollDown) },
		{ "TitleColor", INI::parseColorInt, nullptr, fieldOffset(&CreditsManager::m_titleColor) },
		{ "MinorTitleColor", INI::parseColorInt, nullptr, fieldOffset(&CreditsManager::m_positionColor) },
		{ "NormalColor", INI::parseColorInt, nullptr, fieldOffset(&CreditsManager::m_normalColor) },
		{ "Style", INI::parseLookupList, kCreditsStyleNames, fieldOffset(&CreditsManager::m_currentStyle) },
		{ "Blank", CreditsManager::parseBlank, nullptr, 0 },
		{ "Text", CreditsManager::parseText, nullptr, 0 },
		{ nullptr, nullptr, nullptr, 0 },
	};
	return table;
}

CreditsManager::CreditsManager()
{
	// RW 0x9C6A7F (the member initialisers above): rate 1 every 1 frame, scroll down, colours -1, NORMAL, font height 10, display 0 x 0
	m_creditLineListIt = m_creditLineList.begin();
}

CreditsManager::~CreditsManager()
{
	// the destructor (BFME2 decomp Rva005B77F1Dtor.cpp, RW twin of 0x9B77F1): the shown list cleared, every line deleted
	m_displayedCreditLineList.clear();
	for (CreditsLine *line : m_creditLineList)
	{
		delete line;
	}
	m_creditLineList.clear();
}

void CreditsManager::load(ArchiveFileSystem &fs, const GameTextSource *text, const GlobalLanguage &language, FontMetricsSource &metrics, int xResolution)
{
	m_text = text;
	m_metrics = &metrics;
	const FontDesc *descs[3] = { &language.creditsTitleFont, &language.creditsMinorTitleFont, &language.creditsNormalFont };
	for (int i = 0; i < 3; ++i)
	{
		m_fonts[i].name = descs[i]->name;
		m_fonts[i].pointSize = GlobalLanguage::adjustFontSize(descs[i]->size, xResolution);
		m_fonts[i].bold = descs[i]->bold;
	}
	INIEnvironment env;
	env.fileSystem = &fs;
	INI ini(env);
	// RW 0x9C66A2 .. 0x9C66C3: load with the parser RW 0x9C65B6 for every line (initFromINI of the roll over RW 0xC8B778)
	const INIBlockParse parse = [this](INI *in) { in->initFromINI(this, fieldParseTable()); };
	ini.load("Data\\INI\\Credits.ini", INI_LOAD_OVERWRITE, parse);
	if (m_scrollRatePerFrames <= 0)
	{
		m_scrollRatePerFrames = 1;
	}
	if (m_scrollRate <= 0)
	{
		m_scrollRate = 1;
	}
	m_normalFontHeight = metrics.fontHeight(m_fonts[2]); // RW 0x9C66DA .. 0x9C671F: the normal font's height (+ 0x10 of the GameFont)
}

void CreditsManager::init()
{
	m_isFinished = false;
	m_creditLineListIt = m_creditLineList.begin();
	m_framesSinceStarted = 0;
}

void CreditsManager::reset()
{
	m_displayedCreditLineList.clear();
	m_isFinished = false;
	m_creditLineListIt = m_creditLineList.begin();
	m_framesSinceStarted = 0;
}

std::size_t CreditsManager::linesLeft() const
{
	std::size_t n = 0;
	for (auto it = m_creditLineListIt; it != m_creditLineList.end(); ++it)
	{
		++n;
	}
	return n;
}

UnicodeString CreditsManager::getUnicodeString(const std::string &text) const
{
	if (text == "<BLANK>")
	{
		return UnicodeString();
	}
	if (text.find(':') != std::string::npos)
	{
		return fetchOrMissing(m_text, text);
	}
	// UnicodeString::translate: each byte widened (a UTF-8 name in the file shows its bytes, as in retail)
	UnicodeString out;
	for (char c : text)
	{
		out.push_back((char16_t)(unsigned char)c);
	}
	return out;
}

void CreditsManager::parseBlank(INI *, void *instance, void *, const void *)
{
	static_cast<CreditsManager *>(instance)->addBlank();
}

void CreditsManager::parseText(INI *ini, void *instance, void *, const void *)
{
	const std::string text = ini->getNextQuotedAsciiString();
	static_cast<CreditsManager *>(instance)->addText(text);
}

void CreditsManager::addBlank()
{
	CreditsLine *line = new CreditsLine();
	line->style = CREDIT_STYLE_BLANK;
	m_creditLineList.push_back(line);
}

void CreditsManager::addText(const std::string &text)
{
	CreditsLine *line = new CreditsLine();
	switch (m_currentStyle)
	{
		case CREDIT_STYLE_TITLE:
		case CREDIT_STYLE_POSITION:
		case CREDIT_STYLE_NORMAL:
			line->text = getUnicodeString(text);
			line->style = m_currentStyle;
			m_creditLineList.push_back(line);
			break;
		case CREDIT_STYLE_COLUMN:
		{
			CreditsLine *last = m_creditLineList.empty() ? nullptr : m_creditLineList.back();
			if (!last || last->style != CREDIT_STYLE_COLUMN || last->done)
			{
				line->text = getUnicodeString(text);
				line->style = CREDIT_STYLE_COLUMN;
				line->useSecond = true;
				m_creditLineList.push_back(line);
			}
			else
			{
				last->secondText = getUnicodeString(text);
				last->done = true;
				delete line;
			}
			break;
		}
		default:
			delete line;
			break;
	}
}

GameFont CreditsManager::fontOf(int style) const
{
	return style == CREDIT_STYLE_TITLE ? m_fonts[0] : (style == CREDIT_STYLE_POSITION ? m_fonts[1] : m_fonts[2]);
}

void CreditsManager::update()
{
	if (m_isFinished)
	{
		return;
	}
	if (m_displayWidth <= 0 || m_displayHeight <= 0)
	{
		return;
	}
	m_framesSinceStarted++;
	if (m_framesSinceStarted % m_scrollRatePerFrames != 0)
	{
		return;
	}
	int y = 0;
	int yTest = 0;
	int lastHeight = 0;
	const int start = m_scrollDown ? 0 : m_displayHeight;
	const int end = m_scrollDown ? m_displayHeight : 0;
	const int offsetStartMultiplyer = m_scrollDown ? -1 : 0; // from the top, the height is subtracted
	const int offsetEndMultiplyer = m_scrollDown ? 0 : 1;
	const int directionMultiplyer = m_scrollDown ? 1 : -1;
	for (auto drawIt = m_displayedCreditLineList.begin(); drawIt != m_displayedCreditLineList.end();)
	{
		CreditsLine *line = *drawIt;
		y = line->posY = line->posY + (m_scrollRate * directionMultiplyer);
		lastHeight = line->height;
		yTest = y + ((lastHeight + kCreditSpaceOffset) * offsetEndMultiplyer);
		if ((m_scrollDown && yTest > end) || (!m_scrollDown && yTest < end))
		{
			line->hasDisplay = false; // freeDisplayString
			line->hasSecondDisplay = false;
			drawIt = m_displayedCreditLineList.erase(drawIt);
		}
		else
		{
			++drawIt;
		}
	}
	y = y + ((lastHeight + kCreditSpaceOffset) * offsetStartMultiplyer);
	(void)y;
	// time to add a new line?
	if (!((m_scrollDown && yTest >= start) || (!m_scrollDown && yTest <= start)))
	{
		return;
	}
	if (m_displayedCreditLineList.empty() && m_creditLineListIt == m_creditLineList.end())
	{
		m_isFinished = true;
	}
	if (m_creditLineListIt == m_creditLineList.end())
	{
		return;
	}
	CreditsLine *line = *m_creditLineListIt;
	auto place = [&](const UnicodeString &text, int &width) {
		// a DisplayString of the style's font: getSize, then the line is centred and starts at the edge it enters from
		line->font = fontOf(line->style);
		width = m_metrics ? m_metrics->textWidth(line->font, text) : 0;
		line->height = m_metrics ? m_metrics->fontHeight(line->font) : 0;
		line->posX = m_displayWidth / 2 - width / 2;
		line->posY = start + (line->height * offsetStartMultiplyer);
	};
	switch (line->style)
	{
		case CREDIT_STYLE_TITLE:
		case CREDIT_STYLE_POSITION:
		case CREDIT_STYLE_NORMAL:
			line->color = line->style == CREDIT_STYLE_TITLE ? m_titleColor : (line->style == CREDIT_STYLE_POSITION ? m_positionColor : m_normalColor);
			if (!line->text.empty())
			{
				place(line->text, line->width);
				line->hasDisplay = true;
			}
			break;
		case CREDIT_STYLE_COLUMN:
			line->color = m_normalColor;
			if (!line->text.empty())
			{
				place(line->text, line->width);
				line->hasDisplay = true;
			}
			if (!line->secondText.empty())
			{
				place(line->secondText, line->secondWidth); // ZH: the second string's size and position replace the first's
				line->hasSecondDisplay = true;
			}
			break;
		case CREDIT_STYLE_BLANK:
			line->height = m_normalFontHeight;
			line->posY = start + (line->height * offsetStartMultiplyer);
			break;
		default:
			break;
	}
	m_displayedCreditLineList.push_back(line);
	if (m_creditLineListIt != m_creditLineList.end())
	{
		++m_creditLineListIt;
	}
}

void CreditsManager::draw(float x, float y, float w, float h, GadgetDrawList &out)
{
	// RW 0x9C6765 (cvttss2si: truncation)
	m_displayWidth = (int)w;
	m_displayHeight = (int)h;
	for (CreditsLine *line : m_displayedCreditLineList)
	{
		const int heightChunk = m_displayHeight / 3;
		float perc = 0.0f;
		if (line->posY < heightChunk || line->posY > heightChunk * 2)
		{
			if (line->posY < 0 || line->posY > m_displayHeight)
			{
				perc = 0.0f;
			}
			else if (line->posY < heightChunk)
			{
				perc = (float)line->posY / (float)heightChunk;
			}
			else
			{
				perc = 1.0f - (float)(line->posY - 2 * heightChunk) / (float)heightChunk;
			}
		}
		else
		{
			perc = 1.0f;
		}
		unsigned r, g, b, a;
		colorComponents(line->color, r, g, b, a);
		const int alpha = clamp255((int)((float)a * perc));
		const Color color = GameMakeColor((int)r, (int)g, (int)b, alpha);
		const Color drop = GameMakeColor(0, 0, 0, alpha);
		auto text = [&](const UnicodeString &s, int px, int py) {
			GadgetDrawCommand c;
			c.kind = GadgetDrawCommand::Kind::Text;
			c.window = "AptMainMenu::RenderCredits";
			c.x0 = px;
			c.y0 = py;
			c.color = color;
			c.dropColor = drop;
			c.text = s;
			c.font = line->font;
			out.commands.push_back(c);
		};
		switch (line->style)
		{
			case CREDIT_STYLE_TITLE:
			case CREDIT_STYLE_POSITION:
			case CREDIT_STYLE_NORMAL:
				if (line->hasDisplay)
				{
					text(line->text, (int)((float)line->posX + x), (int)((float)line->posY + y));
				}
				break;
			case CREDIT_STYLE_COLUMN:
			{
				const int chunk = m_displayWidth / 3;
				if (line->hasDisplay)
				{
					text(line->text, (int)(x + (float)(chunk - line->width / 2)), (int)((float)line->posY + y));
				}
				if (line->hasSecondDisplay)
				{
					text(line->secondText, (int)(x + (float)(2 * chunk - line->secondWidth / 2)), (int)((float)line->posY + y));
				}
				break;
			}
			default:
				break;
		}
	}
}
