// OpenBFME. GPL-3.0.
// See GameText.h for the format and the sources.

#include "GameClient/GameText.h"

#include <cctype>

namespace
{

bool isSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

std::string lowerAscii(const std::string &s)
{
	std::string out = s;
	for (char &c : out)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return out;
}

// removeLeadingAndTrailing
std::string trim(const std::string &s)
{
	std::size_t a = 0, b = s.size();
	while (a < b && isSpace(s[a]))
	{
		++a;
	}
	while (b > a && isSpace(s[b - 1]))
	{
		--b;
	}
	return s.substr(a, b - a);
}

// translateCopy (GameText.cpp, the non-debug path): backslash escapes.
std::string translateCopy(const std::string &in)
{
	std::string out;
	bool slash = false;
	for (char c : in)
	{
		if (slash)
		{
			slash = false;
			switch (c)
			{
				case '\\': out += '\\'; break;
				case '\'': out += '\''; break;
				case '"': out += '"'; break;
				case '?': out += '?'; break;
				case 't': out += '\t'; break;
				case 'n': out += '\n'; break;
				default: out += c; break;
			}
		}
		else if (c != '\\')
		{
			out += c;
		}
		else
		{
			slash = true;
		}
	}
	return out;
}

// stripSpaces
std::string stripSpaces(const std::string &in)
{
	std::string out;
	char last = 0;
	bool skipAll = true;
	for (char ch : in)
	{
		if (ch == ' ')
		{
			if (last == ' ' || skipAll)
			{
				continue;
			}
		}
		if (ch == '\n' || ch == '\t')
		{
			if (last == ' ' && !out.empty())
			{
				out.pop_back();
			}
			skipAll = true;
			out += ch;
			last = ch;
			continue;
		}
		out += ch;
		last = ch;
		skipAll = false;
	}
	if (last == ' ' && !out.empty())
	{
		out.pop_back();
	}
	return out;
}

// Windows-1252 0x80..0x9F -> Unicode (the other bytes equal their code points).
const std::uint16_t kCp1252[32] = { 0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178 };

void appendUtf8(std::string &out, std::uint32_t cp)
{
	if (cp < 0x80)
	{
		out += (char)cp;
	}
	else if (cp < 0x800)
	{
		out += (char)(0xC0 | (cp >> 6));
		out += (char)(0x80 | (cp & 0x3F));
	}
	else
	{
		out += (char)(0xE0 | (cp >> 12));
		out += (char)(0x80 | ((cp >> 6) & 0x3F));
		out += (char)(0x80 | (cp & 0x3F));
	}
}

struct Stream
{
	const std::uint8_t *data;
	std::size_t size;
	std::size_t pos = 0;
	bool eof() const { return pos >= size; }
	int next() { return pos < size ? (int)data[pos++] : -1; }
	// readLine: up to (not including) the newline
	bool readLine(std::string &out)
	{
		if (pos >= size)
		{
			return false;
		}
		out.clear();
		while (pos < size && data[pos] != '\n')
		{
			out += (char)data[pos++];
		}
		if (pos < size)
		{
			++pos; // the newline
		}
		return true;
	}
};

} // namespace

std::string Windows1252ToUtf8(const std::string &bytes)
{
	std::string out;
	out.reserve(bytes.size());
	for (unsigned char c : bytes)
	{
		if (c >= 0x80 && c < 0xA0)
		{
			appendUtf8(out, kCp1252[c - 0x80]);
		}
		else
		{
			appendUtf8(out, c);
		}
	}
	return out;
}

bool GameTextTable::parse(const std::uint8_t *data, std::size_t size, std::string *error)
{
	Stream in{ data, size };
	std::string line;
	while (in.readLine(line))
	{
		line = trim(line);
		if (line.empty() || (line.size() >= 2 && line[0] == '/' && line[1] == '/'))
		{
			continue;
		}
		const std::string label = line;
		bool haveString = false;
		std::string text;
		bool ended = false;
		while (in.readLine(line))
		{
			line = trim(line);
			if (!line.empty() && line[0] == '"')
			{
				// readToEndOfQuote: the rest of this line, a newline, then the file
				std::string rest = line.substr(1) + "\n";
				std::size_t rp = 0;
				bool slash = false;
				std::string raw;
				bool closed = false;
				auto nextChar = [&]() -> int {
					if (rp < rest.size())
					{
						return (unsigned char)rest[rp++];
					}
					return in.next();
				};
				for (;;)
				{
					int ch = nextChar();
					if (ch < 0)
					{
						break;
					}
					if (ch == '\n')
					{
						slash = false;
						ch = ' ';
					}
					else if (ch == '\\' && !slash)
					{
						slash = true;
					}
					else if (ch == '\\' && slash)
					{
						slash = false;
					}
					else if (ch == '"' && !slash)
					{
						closed = true;
						break;
					}
					else
					{
						slash = false;
					}
					if (isSpace((char)ch))
					{
						ch = ' ';
					}
					raw += (char)ch;
				}
				if (!closed)
				{
					if (error)
					{
						*error = "label '" + label + "': the string is not closed before the end of the file";
					}
					return false;
				}
				// the speech file name after the closing quote runs to the end of the line (not kept)
				for (;;)
				{
					int ch = nextChar();
					if (ch < 0 || ch == '\n')
					{
						break;
					}
				}
				if (!haveString)
				{
					text = stripSpaces(translateCopy(raw));
					haveString = true;
				}
			}
			else if (lowerAscii(line) == "end")
			{
				ended = true;
				break;
			}
		}
		if (!ended)
		{
			if (error)
			{
				*error = "label '" + label + "': unexpected end of the string file (no END)";
			}
			return false;
		}
		const std::string key = lowerAscii(label);
		if (m_strings.count(key))
		{
			m_duplicates.push_back(label);
		}
		else
		{
			m_strings[key] = Windows1252ToUtf8(text);
		}
	}
	return true;
}

bool GameTextTable::lookup(const std::string &label, std::string &text) const
{
	auto it = m_strings.find(lowerAscii(label));
	if (it == m_strings.end())
	{
		return false;
	}
	text = it->second;
	return true;
}

AptTextResolution ResolveAptText(const std::string &raw, const GameTextTable &table)
{
	return ResolveAptText(raw, table, nullptr);
}

AptTextResolution ResolveAptText(const std::string &raw, const GameTextTable &table, const AptTextRecordLookup *records)
{
	AptTextResolution r;
	std::string text = raw;
	// `&dropShadow` (exactly, case-insensitive) at the end of a suffix cuts the text there (AptDisplayStringAllocation_ctor.cpp:131-137)
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '&' && lowerAscii(text.substr(i + 1)) == "dropshadow")
		{
			text = text.substr(0, i);
			r.dropShadow = true;
			break;
		}
	}
	if (!text.empty() && text[0] == '$')
	{
		std::string label = text.substr(1);
		if (label.find(':') == std::string::npos)
		{
			label = "APT:" + label;
		}
		r.wasLabel = true;
		r.label = label;
		std::string recordText;
		if (records && *records && (*records)(label, recordText)) // lane UI-1: the lookup decides; an explicitly set empty record shows empty
		{
			r.text = recordText;
			r.found = true;
		}
		else if (table.lookup(label, r.text))
		{
			r.found = true;
		}
		else
		{
			// lane HUD-5: retail's text for an unknown label (GameTextManager::fetchPtr, BFME2 decomp GameText.cpp 0x2E65B1: format L"MISSING: '%hs'", the
			// RotWK string is in game.dat); still reported by the caller
			r.found = false;
			r.text = "MISSING: '" + label + "'";
		}
	}
	else
	{
		r.text = Windows1252ToUtf8(text);
	}
	return r;
}
