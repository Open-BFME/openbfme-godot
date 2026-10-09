// OpenBFME. GPL-3.0.
//
// See BitFlags.h for the target facts. Lane HORDE-1.

#include "GameLogic/BitFlags.h"

#include "Common/AsciiString.h"

#include <cstring>

size_t BitFlagNameCount(const char *const *names)
{
	size_t n = 0;
	while (names[n])
	{
		++n;
	}
	return n;
}

namespace
{
bool equalsNoCase(const char *a, const char *b)
{
	return AsciiStringUtil::compareNoCase(a, b) == 0;
}

[[noreturn]] void throwMix()
{
	throw INIException(2, "you may not mix normal and +- ops in bitstring lists"); // RW 0xBD410C, code 2
}

// RW 0x73543E / 0x6C9013 / 0x655B0B / 0x4B5E05: false ends the parse (NONE).
bool handleToken(const char *tok, std::uint32_t *words, size_t wordCount, const char *const *names, bool &foundNormal, bool &foundAddOrSub)
{
	if (equalsNoCase(tok, "NONE"))
	{
		if (foundNormal || foundAddOrSub)
		{
			throwMix();
		}
		std::memset(words, 0, wordCount * sizeof(std::uint32_t));
		return false;
	}
	if (tok[0] == '+' || tok[0] == '-')
	{
		if (foundNormal)
		{
			throwMix();
		}
		const int bit = INI::scanIndexList(tok + 1, names);
		if (tok[0] == '+')
		{
			words[bit >> 5] |= (1u << (bit & 31));
		}
		else
		{
			words[bit >> 5] &= ~(1u << (bit & 31));
		}
		foundAddOrSub = true;
		return true;
	}
	if (foundAddOrSub)
	{
		throwMix();
	}
	if (!foundNormal)
	{
		std::memset(words, 0, wordCount * sizeof(std::uint32_t));
	}
	const int bit = INI::scanIndexList(tok, names);
	words[bit >> 5] |= (1u << (bit & 31));
	foundNormal = true;
	return true;
}
}

void ParseBitFlags(INI *ini, std::uint32_t *words, size_t wordCount, const char *const *names)
{
	bool foundNormal = false;
	bool foundAddOrSub = false;
	// RW 0x42DCEA: getNextTokenOrNull, then preprocessMacro; the token was a macro when the result differs.
	for (const char *token = ini->getNextTokenOrNull(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		const char *expanded = ini->preprocessMacro(token);
		if (expanded == token)
		{
			if (!handleToken(token, words, wordCount, names, foundNormal, foundAddOrSub))
			{
				return;
			}
			continue;
		}
		const std::string value = expanded; // the macro text stays valid while the pieces are read
		static const char *kWs = " \n\r\t";
		size_t pos = 0;
		while (pos < value.size())
		{
			const size_t b = value.find_first_not_of(kWs, pos);
			if (b == std::string::npos)
			{
				break;
			}
			size_t e = value.find_first_of(kWs, b);
			if (e == std::string::npos)
			{
				e = value.size();
			}
			const std::string piece = value.substr(b, e - b);
			pos = e;
			if (!handleToken(piece.c_str(), words, wordCount, names, foundNormal, foundAddOrSub))
			{
				break; // only this macro's pieces end; the line goes on (RW 0x7B1BD3 -> 0x7B1C0E)
			}
		}
	}
}

void ParseKindOfMask(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::uint32_t *>(store), 7, TheKindOfNames);
}

void ParseModelConditionFlags(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::uint32_t *>(store), 19, TheModelConditionNames);
}

void ParseObjectStatusMask(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::uint32_t *>(store), 4, TheObjectStatusNames);
}

void ParseWeaponConditionFlags(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::uint32_t *>(store), 4, TheWeaponConditionNames);
}

// RW 0x73A68A.
void ParseDeathTypeFlags(INI *ini, void *, void *store, const void *)
{
	std::uint32_t flags = 0xFFFFFFFFu;
	for (const char *token = ini->getNextToken(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		if (equalsNoCase(token, "ALL"))
		{
			flags = 0xFFFFFFFFu;
		}
		else if (equalsNoCase(token, "NONE"))
		{
			flags = 0;
		}
		else if (token[0] == '+')
		{
			const int idx = INI::scanIndexList(token + 1, TheDeathTypeNames);
			flags |= 1u << ((idx - 1) & 31); // x86 shl masks the count: index 0 (NORMAL) is bit 31
		}
		else if (token[0] == '-')
		{
			const int idx = INI::scanIndexList(token + 1, TheDeathTypeNames);
			flags &= ~(1u << ((idx - 1) & 31));
		}
		else
		{
			throw INIException(5, "ALL, NONE, +, or - expected"); // RW 0xC24F5C
		}
	}
	*static_cast<std::uint32_t *>(store) = flags;
}
