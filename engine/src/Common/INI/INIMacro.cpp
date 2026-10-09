// OpenBFME. GPL-3.0.
//
// Macros: the global macro table and its hash, the #define pre-pass, substitution, and the
// math evaluator (#ADD( #SUBTRACT( #MULTIPLY( #DIVIDE( in three widths).
//
// Sources: spec ini-and-object-model.md sections 2.2-2.4;
//   TARGET RW 0x42B6C1 / 0x42BC44 / 0x42C6EF (hash, lower-casing, lookup); BFME2 0x42BF8C, 0x42BA61
//   DONOR B1 Source/Common/INI/INIPreprocessMacro.cpp:83-110 (digit fast path only; its hash is wrong)
//   B1T attempts/0x00853610.cpp:209-277 (the define pass shape, partial)
//   B2 0x42D3CB-0x42D5C7 (define pre-pass), 0x42D0A9 (preprocessMacro),
//   B2 0x42E46A real / 0x42E0C9 int / 0x42E299 unsigned (math evaluators),
//   B2src Common/System/INI_scanReal.cpp, INI_scanInt.cpp, INI_scanUnsignedInt.cpp,
//   INI_scanBool.cpp (messages, call shape).

#include "Common/INI.h"

#include "Common/AsciiString.h"
#include "Common/INI/Msvcr71Real.h"
#include "Common/NumericState.h"

#include <cstdio>
#include <cstring>
#include <limits>

// ---------------------------------------------------------------------------------------------
// The hash and the table. TARGET FACTS (RW): the name is lower-cased (0x42BC44), then
// h = h * 5 + (signed char)c from h = 0 (0x42B6C1-0x42B6D8); the bucket is h % count and the
// chain is searched comparing COMPLETE names (0x42C6EF-0x42C72B). BFME2 0x42BF8C, 0x42BA61 and
// 0x42C9E7 agree. Lookup is therefore by case-insensitive full name; the hash only chooses a
// bucket and is not observable, so the table is keyed by the lower-cased name. The first port
// used BFME1's xor/shift hash compared by hash alone, which is wrong for the target (it made
// AF and GZ collide and invented the Upgrade_ArmorAttribute02 alias).
// ---------------------------------------------------------------------------------------------
std::uint32_t INIMacroTable::hash(const char *name)
{
	std::uint32_t h = 0;
	for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
	{
		const char lower = (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : (char)*p;
		h = h * 5u + (std::uint32_t)(int)(signed char)lower;
	}
	return h;
}

// A duplicate name is a hard error except on the load type 5 reload path (spec 2.2 item 1.7).
bool INIMacroTable::define(const std::string &name, const std::string &value, bool allowOverwrite)
{
	const std::string key = AsciiStringUtil::lowered(name);
	auto it = m_entries.find(key);
	if (it != m_entries.end())
	{
		if (!allowOverwrite)
		{
			return false;
		}
		it->second.value = value;
		return true;
	}
	Entry e;
	e.name = name;
	e.value = value;
	m_entries.emplace(key, std::move(e));
	return true;
}

const INIMacroTable::Entry *INIMacroTable::find(const char *name) const
{
	auto it = m_entries.find(AsciiStringUtil::lowered(name));
	return it == m_entries.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------------------------
// The #define pre-pass (prepFile). Runs over all of a file's lines, after TextFile has split and
// expanded includes, before any line is parsed. Spec 2.2:
//   1. a line is a define iff it startsWith("#define") (case sensitive, column 0)
//   2. the line is emptied so the main parser sees nothing
//   3. tokens split on space/LF/CR/tab (AsciiString::nextToken defaults); token 1 is the name
//   4. a file name ending in "map.ini" (byte compare) throws code 8 "MACROs not allowed in map.ini"
//   5. any name character c with 'a' < c < 'z' (signed char compare) throws code 8
//      "MACRO names must use UPPERCASE letters"  (a and z themselves pass)
//   6. value = the remaining tokens joined with single spaces (B2/RW; B1 kept only the first)
//   7. empty value throws code 8 "Error parsing MACRO"
//   8. duplicate hash throws code 8 "Duplicate MACRO names" (load type 5 overwrites instead)
// ---------------------------------------------------------------------------------------------
void INI::runDefinePass()
{
	static const char *kSeps = " \n\r\t";
	for (TextFile::Line &line : m_lines)
	{
		if (line.text.compare(0, 7, "#define") != 0)
		{
			continue;
		}

		const std::string lineText = line.text;
		line.text.clear(); // line[0] = 0
		if (line.bufferId != 0)
		{
			// RW 0x42D104 clears the first byte of the TEXT, which lone-slash duplicates share
			// (RW 0xA158E9-0xA158FE), so the other entries become empty and the macro defines once
			for (TextFile::Line &other : m_lines)
			{
				if (other.bufferId == line.bufferId)
				{
					other.text.clear();
				}
			}
		}

		std::vector<std::string> tokens;
		size_t pos = 0;
		while (pos < lineText.size())
		{
			const size_t b = lineText.find_first_not_of(kSeps, pos);
			if (b == std::string::npos)
			{
				break;
			}
			size_t e = lineText.find_first_of(kSeps, b);
			if (e == std::string::npos)
			{
				e = lineText.size();
			}
			tokens.push_back(lineText.substr(b, e - b));
			pos = e;
		}
		const std::string name = tokens.size() > 1 ? tokens[1] : std::string();

		static const char kMapIni[] = "map.ini";
		if (m_filename.size() >= sizeof(kMapIni) - 1 && m_filename.compare(m_filename.size() - (sizeof(kMapIni) - 1), std::string::npos, kMapIni) == 0)
		{
			throw INIException(8, "%s:\nMACROs not allowed in map.ini.\n%s.", m_filename.c_str(), lineText.c_str());
		}

		for (char ch : name)
		{
			const signed char c = (signed char)ch;
			if ('a' < c && c < 'z')
			{
				throw INIException(8, "%s:\nMACRO names must use UPPERCASE letters.\n%s.", m_filename.c_str(), name.c_str());
			}
		}

		std::string value;
		for (size_t i = 2; i < tokens.size(); ++i)
		{
			if (i > 2)
			{
				value += ' ';
			}
			value += tokens[i];
		}
		if (value.empty())
		{
			throw INIException(8, "%s:\nError parsing MACRO.\n%s has no value", m_filename.c_str(), name.c_str());
		}

		if (!m_env.macros.define(name, value, m_loadType == INI_LOAD_RELOAD))
		{
			throw INIException(8, "%s:\nDuplicate MACRO names.\n%s.", m_filename.c_str(), name.c_str());
		}
	}
}

// ---------------------------------------------------------------------------------------------
// preprocessMacro. B1 INIPreprocessMacro.cpp:83-110, B2 0x42D0A9.
// ---------------------------------------------------------------------------------------------
const char *INI::preprocessMacro(const char *token)
{
	const char c = token[0];
	if (c <= '9' && c >= '0')
	{
		return token; // digit fast path; '-' and '.' are NOT special-cased
	}
	const INIMacroTable::Entry *node = m_env.macros.find(token);
	if (node)
	{
		return node->value.c_str();
	}
	return token;
}

// ---------------------------------------------------------------------------------------------
// Scanners. B2src Common/System/INI_scanInt.cpp, INI_scanUnsignedInt.cpp, INI_scanReal.cpp,
// INI_scanBool.cpp; spec 1.8. sscanf reads only a prefix of the text: "1.0," scans as 1.0,
// "110%" as 110.
// ---------------------------------------------------------------------------------------------
int INI::scanInt(const char *token)
{
	const char *text = preprocessMacro(token);
	if (*text == '#')
	{
		return parseMathInt(text);
	}
	int value;
	if (std::sscanf(text, "%d", &value) != 1)
	{
		throw INIException(3, "Expected signed integer value, math op, or predefined macro, but found '%s'", text);
	}
	return value;
}

unsigned INI::scanUnsignedInt(const char *token)
{
	const char *text = preprocessMacro(token);
	if (*text == '#')
	{
		return parseMathUnsigned(text);
	}
	unsigned value;
	if (std::sscanf(text, "%u", &value) != 1)
	{
		throw INIException(3, "Expected unsigned integer value, math op, or predefined macro, but found '%s'", text);
	}
	return value;
}

float INI::scanReal(const char *token)
{
	const char *text = preprocessMacro(token);
	if (*text == '#')
	{
		return parseMathReal(text);
	}
	float value;
	if (!Msvcr71Real::scanfFloat(text, value)) // RW sscanf(token, "%f"): MSVCR71's own converter on every OS (Common/INI/Msvcr71Real.h, lane WIN-1)
	{
		throw INIException(3, "Expected floating point value, math op, or predefined macro, but found '%s'", text);
	}
	return value;
}

// The error reports the ORIGINAL token, not the substituted text (B2src INI_scanBool.cpp).
bool INI::scanBool(const char *token)
{
	const char *text = preprocessMacro(token);
	if (AsciiStringUtil::compareNoCase(text, "yes") == 0)
	{
		return true;
	}
	if (AsciiStringUtil::compareNoCase(text, "no") == 0)
	{
		return false;
	}
	throw INIException(3, "invalid boolean token %s -- expected Yes or No", token);
}

// B2 0x42EE10 (dup_002EE10): scanReal(token) * 0.01f (ZH divided by 100.0f instead).
float INI::scanPercentToReal(const char *token)
{
	return NumericState::pc24Mul(scanReal(token), 0.01f);
}

// ---------------------------------------------------------------------------------------------
// Math evaluator. B2 0x42E46A (real), 0x42E0C9 (int), 0x42E299 (unsigned); spec 2.4:
//
//   pushText(text); op = getNextToken(); a = getNextToken();
//   #ADD(      acc = V(a); loop { t = getNextToken(); if t == ")" return acc; acc = acc + V(t) }
//   #SUBTRACT( acc = V(a) - V(getNextToken()); then ")" must follow else code 8
//              "#SUBTRACT takes only 2 operands"
//   #MULTIPLY( like ADD with *
//   #DIVIDE(   like SUBTRACT with /, code 8 "#DIVIDE takes only 2 operands"
//   anything else: code 3 "Expected known math operation after #, but found '%s'"
//
// V is the calling scanner, so operands may be numbers, macros or nested groups. The operator
// and "(" are one token ("#ADD(" ; "#ADD (" fails), ")" must be its own token, and operator
// names are case sensitive (strcmp). Real results are float32 after every step (fstp DWORD under
// 24-bit precision). Int / unsigned wrap on 32 bits; their divide-by-zero is a CPU exception in
// retail and an INIException here (retail would crash; a crash must not be silent).
// ---------------------------------------------------------------------------------------------
namespace
{
template <typename T>
struct MathOps;

template <>
struct MathOps<float>
{
	// fld a; f<op> b; fstp dword (RW 0x42E28F, 0x42E2B2): PC24 operate, then the float32 store.
	// x/0 -> inf, 0/0 -> NaN, as in retail.
	static float add(float a, float b) { return NumericState::pc24Add(a, b); }
	static float sub(float a, float b) { return NumericState::pc24Sub(a, b); }
	static float mul(float a, float b) { return NumericState::pc24Mul(a, b); }
	static float div(float a, float b) { return NumericState::pc24Div(a, b); }
};

template <>
struct MathOps<int>
{
	static int add(int a, int b) { return (int)((std::uint32_t)a + (std::uint32_t)b); }
	static int sub(int a, int b) { return (int)((std::uint32_t)a - (std::uint32_t)b); }
	static int mul(int a, int b) { return (int)((std::uint32_t)a * (std::uint32_t)b); }
	static int div(int a, int b)
	{
		if (b == 0 || (a == std::numeric_limits<int>::min() && b == -1))
		{
			throw INIException(8, "#DIVIDE integer division fault (%d / %d); retail raises a CPU exception here", a, b);
		}
		return a / b;
	}
};

template <>
struct MathOps<unsigned>
{
	static unsigned add(unsigned a, unsigned b) { return a + b; }
	static unsigned sub(unsigned a, unsigned b) { return a - b; }
	static unsigned mul(unsigned a, unsigned b) { return a * b; }
	static unsigned div(unsigned a, unsigned b)
	{
		if (b == 0)
		{
			throw INIException(8, "#DIVIDE integer division fault (%u / 0); retail raises a CPU exception here", a);
		}
		return a / b;
	}
};

template <typename T, typename Scan>
T evalMath(INI &ini, const char *text, Scan scan)
{
	typedef MathOps<T> Ops;
	ini.pushText(text);
	const std::string op = ini.getNextToken(nullptr);
	const std::string a = ini.getNextToken(nullptr);

	if (std::strcmp(op.c_str(), "#ADD(") == 0)
	{
		T acc = scan(a.c_str());
		for (;;)
		{
			const std::string t = ini.getNextToken(nullptr);
			if (std::strcmp(t.c_str(), ")") == 0)
			{
				return acc;
			}
			acc = Ops::add(acc, scan(t.c_str()));
		}
	}
	if (std::strcmp(op.c_str(), "#SUBTRACT(") == 0)
	{
		T acc = scan(a.c_str());
		acc = Ops::sub(acc, scan(ini.getNextToken(nullptr)));
		if (std::strcmp(ini.getNextToken(nullptr), ")") != 0)
		{
			throw INIException(8, "#SUBTRACT takes only 2 operands");
		}
		return acc;
	}
	if (std::strcmp(op.c_str(), "#MULTIPLY(") == 0)
	{
		T acc = scan(a.c_str());
		for (;;)
		{
			const std::string t = ini.getNextToken(nullptr);
			if (std::strcmp(t.c_str(), ")") == 0)
			{
				return acc;
			}
			acc = Ops::mul(acc, scan(t.c_str()));
		}
	}
	if (std::strcmp(op.c_str(), "#DIVIDE(") == 0)
	{
		T acc = scan(a.c_str());
		acc = Ops::div(acc, scan(ini.getNextToken(nullptr)));
		if (std::strcmp(ini.getNextToken(nullptr), ")") != 0)
		{
			throw INIException(8, "#DIVIDE takes only 2 operands");
		}
		return acc;
	}
	throw INIException(3, "Expected known math operation after #, but found '%s'", op.c_str());
}
}

float INI::parseMathReal(const char *text)
{
	return evalMath<float>(*this, text, [this](const char *t) { return scanReal(t); });
}

int INI::parseMathInt(const char *text)
{
	return evalMath<int>(*this, text, [this](const char *t) { return scanInt(t); });
}

unsigned INI::parseMathUnsigned(const char *text)
{
	return evalMath<unsigned>(*this, text, [this](const char *t) { return scanUnsignedInt(t); });
}
