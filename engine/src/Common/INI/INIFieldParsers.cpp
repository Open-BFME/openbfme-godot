// OpenBFME. GPL-3.0.
//
// INI field parsers: the scan/parse family of ZH INI.cpp as changed by BFME / RotWK.
//
// Sources: spec ini-and-object-model.md section 1.8;
//   B1 Source/Common/INI/ini_parsers.cpp (parseByte..parseColorInt, scanIndexList,
//      scanLookupList, parseBitString32, parseAsciiStringVector*)
//   B1 Source/Common/INI/ini.cpp:1147-1330 (durations, velocity, flags)
//   B1 Source/Common/GameCommonConvertThunk.cpp (velocity / acceleration / angular velocity,
//      retail 0x000B94C0/0x000B94D0/0x000B94F0)
//   B2src Common/System/INI_parse*.cpp (BFME2 / RotWK constants and call shapes)
//   ZH Source/Common/INI/INI.cpp (Coord / ICoord parsers, bit strings)
//
// Value stores: AsciiString fields are std::string, string vectors are std::vector<std::string>.

#include "Common/INI.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/NumericState.h"

#include <cmath>
#include <cstring>

namespace
{
// B2 0xDBA4EC: 0.005f, the millisecond->frame scale (5 frames per 1000 ms).
const float kDurationMsecScale = LOGICFRAMES_PER_MSEC_REAL; // 5.0f / 1000.0f == 0.005f
// B2 0x9BA4F8 / B1 0x012A86A8: SECONDS_PER_LOGICFRAME_REAL = 0.2f (a writable global; the
// accelerated form squares it at run time).
const float kSecondsPerLogicFrame = SECONDS_PER_LOGICFRAME_REAL; // 1.0f / 5.0f == 0.2f
// B2src INI_parseAngleReal.cpp, B1 GameCommonConvertThunk.cpp: RADS_PER_DEGREE = 0x3C8EFA35.
const float kRadsPerDegree = 0.017453292f;

bool equalsNoCase(const char *a, const char *b)
{
	return AsciiStringUtil::compareNoCase(a, b) == 0;
}
}

// ---------------------------------------------------------------------------------------------
// Integers. B1 ini_parsers.cpp:57-141: out-of-range values throw the plain int 1.
// ---------------------------------------------------------------------------------------------
void INI::parseByte(INI *ini, void *, void *store, const void *)
{
	const int value = ini->scanInt(ini->getNextToken());
	if (value < -128 || value > 127)
	{
		throw INIPlainIntError("Bad value INI::parseByte");
	}
	*(std::int8_t *)store = (std::int8_t)value;
}

void INI::parseUnsignedByte(INI *ini, void *, void *store, const void *)
{
	const int value = ini->scanInt(ini->getNextToken());
	if (value < 0 || value > 255)
	{
		throw INIPlainIntError("Bad value INI::parseUnsignedByte");
	}
	*(std::uint8_t *)store = (std::uint8_t)value;
}

void INI::parseShort(INI *ini, void *, void *store, const void *)
{
	const int value = ini->scanInt(ini->getNextToken());
	if (value < -32768 || value > 32767)
	{
		throw INIPlainIntError("Bad value INI::parseShort");
	}
	*(std::int16_t *)store = (std::int16_t)value;
}

void INI::parseUnsignedShort(INI *ini, void *, void *store, const void *)
{
	const int value = ini->scanInt(ini->getNextToken());
	if (value < 0 || value > 65535)
	{
		throw INIPlainIntError("Bad value INI::parseUnsignedShort");
	}
	*(std::uint16_t *)store = (std::uint16_t)value;
}

void INI::parseInt(INI *ini, void *, void *store, const void *)
{
	*(int *)store = ini->scanInt(ini->getNextToken());
}

void INI::parseUnsignedInt(INI *ini, void *, void *store, const void *)
{
	*(unsigned *)store = ini->scanUnsignedInt(ini->getNextToken());
}

void INI::parseReal(INI *ini, void *, void *store, const void *)
{
	*(float *)store = ini->scanReal(ini->getNextToken());
}

// B1 ini_parsers.cpp parsePositiveNonZeroReal.
void INI::parsePositiveNonZeroReal(INI *ini, void *, void *store, const void *)
{
	const float v = ini->scanReal(ini->getNextToken());
	*(float *)store = v;
	if (v <= 0.0f)
	{
		throw INIException(3, "invalid Real value %1.7f -- expected > 0", (double)v);
	}
}

void INI::parseBool(INI *ini, void *, void *store, const void *)
{
	*(bool *)store = ini->scanBool(ini->getNextToken());
}

void INI::parseBitInInt32(INI *ini, void *, void *store, const void *userData)
{
	unsigned *s = (unsigned *)store;
	const unsigned mask = (unsigned)(std::uintptr_t)userData;
	if (ini->scanBool(ini->getNextToken()))
	{
		*s |= mask;
	}
	else
	{
		*s &= ~mask;
	}
}

// ---------------------------------------------------------------------------------------------
// Percent, angle, durations, velocity. Spec 1.8.
// ---------------------------------------------------------------------------------------------
// B2 0x42EE10 / 0x42F1BA: the token is read with the percent separators ('%' separates), then
// scanReal * 0.01f. Macros and math work because it goes through scanReal.
void INI::parsePercentToReal(INI *ini, void *, void *store, const void *)
{
	const char *token = ini->getNextToken(ini->getSepsPercent());
	*(float *)store = ini->scanPercentToReal(token);
}

// B2src INI_parseAngleReal.cpp.
void INI::parseAngleReal(INI *ini, void *, void *store, const void *)
{
	*(float *)store = NumericState::pc24Mul(ini->scanReal(ini->getNextToken()), kRadsPerDegree);
}

// TARGET RW 0x73A403: store = 0.005f * ms, no ceil (500 ms is 2.5). Every float multiply below goes
// through NumericState::pc24Mul (x87 PC24 operate-then-store); plain float arithmetic differs for
// subnormal results.
void INI::parseDurationReal(INI *ini, void *, void *store, const void *)
{
	const float ms = ini->scanReal(ini->getNextToken());
	*(float *)store = NumericState::pc24Mul(kDurationMsecScale, ms);
}

// TARGET RW 0x73A429 (sequence 0x73A440-0x73A458, PLAN rule 2): signed fild of the integer, a
// rounded `fadd 2^32` when it was >= 2^31, multiply by the float32 0.005f under PC24, store as a
// double, MSVCR71 ceil. The integer is NEVER rounded to float32 first (52,428,805 ms is 262145
// frames in retail, 262144 after an early float conversion) and the unsigned correction rounds
// (2,147,483,749 ms is 10,737,418 frames), which is why this goes through NumericState::ceilScaled.
// DONOR (superseded for this): B2src INI_parseDurationUnsignedInt.cpp, B1 ini.cpp:1174-1183.
void INI::parseDurationUnsignedInt(INI *ini, void *, void *store, const void *)
{
	const unsigned ms = ini->scanUnsignedInt(ini->getNextToken());
	*(unsigned *)store = NumericState::ceilScaled(ms, kDurationMsecScale);
}

void INI::parseDurationUnsignedShort(INI *ini, void *, void *store, const void *)
{
	const unsigned ms = ini->scanUnsignedInt(ini->getNextToken());
	*(std::uint16_t *)store = (std::uint16_t)NumericState::ceilScaled(ms, kDurationMsecScale);
}

// B2src INI_parseVelocityReal.cpp: 0.2f * v.
void INI::parseVelocityReal(INI *ini, void *, void *store, const void *)
{
	const float v = ini->scanReal(ini->getNextToken());
	*(float *)store = NumericState::pc24Mul(kSecondsPerLogicFrame, v);
}

// B2src INI_parseAccelerationReal.cpp: (0.2f * 0.2f) * v, the square computed at run time
// (the factor is a mutable global in retail, never folded to 0.04).
void INI::parseAccelerationReal(INI *ini, void *, void *store, const void *)
{
	// (the retail square stays in an x87 register; storing it as float32 only differs for a
	// subnormal square, which 0.2 * 0.2 is not)
	const float square = NumericState::pc24Mul(kSecondsPerLogicFrame, kSecondsPerLogicFrame);
	const float v = ini->scanReal(ini->getNextToken());
	*(float *)store = NumericState::pc24Mul(square, v);
}

// B1 GameCommonConvertThunk.cpp:62-65 (retail 0x000B94F0, matched): deg * (0.2f * 0.017453292f).
// This settles spec open item 9 for BFME1; the RotWK constant is not separately verified.
void INI::parseAngularVelocityReal(INI *ini, void *, void *store, const void *)
{
	const float combined = NumericState::pc24Mul(kSecondsPerLogicFrame, kRadsPerDegree);
	const float deg = ini->scanReal(ini->getNextToken());
	*(float *)store = NumericState::pc24Mul(deg, combined);
}

// B2 0x42F0F7 (dup_002F0F7): (int)(scanReal * 1000.0f), truncating. B1 INI_parseSecondsToMillis.cpp
// stores the same expression as unsigned.
void INI::parseSecondsToMillis(INI *ini, void *, void *store, const void *)
{
	const float s = ini->scanReal(ini->getNextToken());
	*(int *)store = (int)NumericState::pc24Mul(s, 1000.0f);
}

// ---------------------------------------------------------------------------------------------
// Strings. Spec 1.5.
// ---------------------------------------------------------------------------------------------
void INI::parseAsciiString(INI *ini, void *, void *store, const void *)
{
	*(std::string *)store = ini->getNextAsciiString();
}

void INI::parseQuotedAsciiString(INI *ini, void *, void *store, const void *)
{
	*(std::string *)store = ini->getNextQuotedAsciiString();
}

// B2 0x42E896 (VERIFIED in the spec): every token goes through preprocessMacro; a matched macro
// value is split on whitespace and every piece becomes its own entry. B1 ini_parsers.cpp pushes
// the raw tokens only.
void INI::parseAsciiStringVectorAppend(INI *ini, void *, void *store, const void *)
{
	std::vector<std::string> *vec = (std::vector<std::string> *)store;
	for (const char *token = ini->getNextTokenOrNull(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		const char *expanded = ini->preprocessMacro(token);
		if (expanded == token)
		{
			vec->push_back(token);
			continue;
		}
		const std::string value = expanded;
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
			vec->push_back(value.substr(b, e - b));
			pos = e;
		}
	}
}

// B1 ini_parsers.cpp:539-547: erase, then the append parser.
void INI::parseAsciiStringVector(INI *ini, void *instance, void *store, const void *userData)
{
	((std::vector<std::string> *)store)->clear();
	parseAsciiStringVectorAppend(ini, instance, store, userData);
}

// ---------------------------------------------------------------------------------------------
// Index and lookup lists (stricmp). B1 ini_parsers.cpp scanIndexList / scanLookupList; the two
// message strings are transcribed from retail (0x011303E0, 0x01130418).
// ---------------------------------------------------------------------------------------------
int INI::scanIndexList(const char *token, ConstCharPtrArray nameList)
{
	if (nameList == nullptr || nameList[0] == nullptr)
	{
		throw INIException(2, "INTERNAL ERROR! scanIndexList: No name list provided!");
	}
	int count = 0;
	for (ConstCharPtrArray name = nameList; *name; name++, count++)
	{
		if (equalsNoCase(*name, token))
		{
			return count;
		}
	}
	throw INIException(3, "Token '%s' is not a valid member of the index list", token);
}

int INI::scanLookupList(const char *token, ConstLookupListRecArray lookupList)
{
	if (lookupList == nullptr || lookupList[0].name == nullptr)
	{
		throw INIException(2, "INTERNAL ERROR! scanLookupList: No name list provided!");
	}
	for (const LookupListRec *lookup = &lookupList[0]; lookup->name; lookup++)
	{
		if (equalsNoCase(lookup->name, token))
		{
			return lookup->value;
		}
	}
	throw INIException(3, "Token '%s' is not a valid member of the lookup list", token);
}

void INI::parseIndexList(INI *ini, void *, void *store, const void *userData)
{
	*(int *)store = scanIndexList(ini->getNextToken(), (ConstCharPtrArray)userData);
}

void INI::parseByteSizedIndexList(INI *ini, void *, void *store, const void *userData)
{
	const int value = scanIndexList(ini->getNextToken(), (ConstCharPtrArray)userData);
	if (value < 0 || value > 255)
	{
		throw INIPlainIntError("Bad index list INI::parseByteSizedIndexList");
	}
	*(std::uint8_t *)store = (std::uint8_t)value;
}

void INI::parseLookupList(INI *ini, void *, void *store, const void *userData)
{
	*(int *)store = scanLookupList(ini->getNextToken(), (ConstLookupListRecArray)userData);
}

// ---------------------------------------------------------------------------------------------
// Bit strings. ZH INI.cpp:903-969; B1 ini_parsers.cpp parseBitString32.
//   NONE clears all bits (mixing it with other tokens throws); a plain name clears everything
//   once, then sets that bit; +name / -name edit the existing value; plain and +/- do not mix.
//   A name list longer than 32 (the SpecialPower Flags list RW 0xDA5F34 runs on into 35 names) shifts by (index & 31): the retail `shl` takes
//   the count modulo 32, so index 32 is bit 0 (lane SPELL-1, Sol review r1; a C++ shift by 32 or more would be undefined).
// ---------------------------------------------------------------------------------------------
void INI::parseBitString32(INI *ini, void *, void *store, const void *userData)
{
	ConstCharPtrArray flagList = (ConstCharPtrArray)userData;
	unsigned *bits = (unsigned *)store;

	if (flagList == nullptr || flagList[0] == nullptr)
	{
		throw INIException(2, "INTERNAL ERROR! parseBitString32: No flag list provided!");
	}

	bool foundNormal = false;
	bool foundAddOrSub = false;

	for (const char *token = ini->getNextTokenOrNull(); token != nullptr; token = ini->getNextTokenOrNull())
	{
		if (equalsNoCase(token, "NONE"))
		{
			if (foundNormal || foundAddOrSub)
			{
				throw INIException(3, "you may not mix normal and +- ops in bitstring lists");
			}
			*bits = 0;
			break;
		}

		if (token[0] == '+')
		{
			if (foundNormal)
			{
				throw INIException(3, "you may not mix normal and +- ops in bitstring lists");
			}
			const int bitIndex = scanIndexList(token + 1, flagList);
			*bits |= (1u << (bitIndex & 31));
			foundAddOrSub = true;
		}
		else if (token[0] == '-')
		{
			if (foundNormal)
			{
				throw INIException(3, "you may not mix normal and +- ops in bitstring lists");
			}
			const int bitIndex = scanIndexList(token + 1, flagList);
			*bits &= ~(1u << (bitIndex & 31));
			foundAddOrSub = true;
		}
		else
		{
			if (foundAddOrSub)
			{
				throw INIException(3, "you may not mix normal and +- ops in bitstring lists");
			}
			if (!foundNormal)
			{
				*bits = 0;
			}
			const int bitIndex = scanIndexList(token, flagList);
			*bits |= (1u << (bitIndex & 31));
			foundNormal = true;
		}
	}
}

// B1 ini_parsers.cpp parseBitString8: a plain int 1 when bits above 8 are set.
void INI::parseBitString8(INI *ini, void *, void *store, const void *userData)
{
	unsigned tmp = 0;
	parseBitString32(ini, nullptr, &tmp, userData);
	if (tmp & 0xffffff00u)
	{
		throw INIPlainIntError("Bad bitstring list INI::parseBitString8");
	}
	*(std::uint8_t *)store = (std::uint8_t)tmp;
}

// Type flag lists: start from ALL; ALL / NONE reset; +x / -x edit; anything else code 5.
// B1 ini.cpp:1309-1331 (parseDamageTypeFlags: `1 << (dt - 1)`), ZH INI.cpp parseDeathTypeFlags /
// parseVeterancyLevelFlags (same loop over a different name table and bit offset).
void INI::parseTypeFlagList(INI *ini, void *, void *store, const void *userData)
{
	const INITypeFlagListSpec *spec = (const INITypeFlagListSpec *)userData;
	unsigned flags = 0xFFFFFFFFu;
	for (const char *token = ini->getNextToken(); token; token = ini->getNextTokenOrNull())
	{
		if (equalsNoCase(token, "ALL"))
		{
			flags = 0xFFFFFFFFu;
			continue;
		}
		if (equalsNoCase(token, "NONE"))
		{
			flags = 0;
			continue;
		}
		if (token[0] == '+')
		{
			const int dt = scanIndexList(token + 1, spec->names);
			flags |= 1u << (dt - spec->firstBitIndex);
			continue;
		}
		if (token[0] == '-')
		{
			const int dt = scanIndexList(token + 1, spec->names);
			flags &= ~(1u << (dt - spec->firstBitIndex));
			continue;
		}
		throw INIException(5, "ALL, NONE, + or - expected");
	}
	*(unsigned *)store = flags;
}

// ---------------------------------------------------------------------------------------------
// Colors and coordinates. B1 ini_parsers.cpp:208-384; ZH INI.cpp parseCoord2D/3D/ICoord2D.
// ---------------------------------------------------------------------------------------------
void INI::parseRGBColor(INI *ini, void *, void *store, const void *)
{
	const char *names[3] = { "R", "G", "B" };
	int colors[3];
	for (int i = 0; i < 3; i++)
	{
		colors[i] = ini->scanInt(ini->getNextSubToken(names[i]));
		if (colors[i] < 0 || colors[i] > 255)
		{
			throw INIException(3, "color value %s=%i out of range (0..255)", names[i], colors[i]);
		}
	}
	RGBColor *c = (RGBColor *)store;
	c->red = NumericState::pc24Mul((float)colors[0], 1.0f / 255.0f); // retail multiplies by the constant at 0x0107C64C
	c->green = NumericState::pc24Mul((float)colors[1], 1.0f / 255.0f);
	c->blue = NumericState::pc24Mul((float)colors[2], 1.0f / 255.0f);
}

namespace
{
void readRGBA(INI *ini, int (&colors)[4])
{
	const char *names[4] = { "R", "G", "B", "A" };
	for (int i = 0; i < 4; i++)
	{
		const char *token = ini->getNextTokenOrNull(ini->getSepsColon());
		if (token == nullptr)
		{
			if (i < 3)
			{
				throw INIException(3, "can't omit value for color %s", names[i]);
			}
			colors[i] = 255; // it's ok for A to be omitted
		}
		else
		{
			if (!equalsNoCase(token, names[i]))
			{
				throw INIException(3, "expected '%s'", names[i]);
			}
			colors[i] = ini->scanInt(ini->getNextToken(ini->getSepsColon()));
		}
		if (colors[i] < 0 || colors[i] > 255)
		{
			throw INIException(3, "color value %s=%i out of range (0..255)", names[i], colors[i]);
		}
	}
}
}

void INI::parseRGBAColorInt(INI *ini, void *, void *store, const void *)
{
	int colors[4];
	readRGBA(ini, colors);
	RGBAColorInt *c = (RGBAColorInt *)store;
	c->red = colors[0];
	c->green = colors[1];
	c->blue = colors[2];
	c->alpha = colors[3];
}

void INI::parseColorInt(INI *ini, void *, void *store, const void *)
{
	int colors[4];
	readRGBA(ini, colors);
	*(std::uint32_t *)store = GameMakeColor(colors[0], colors[1], colors[2], colors[3]);
}

void INI::parseCoord3D(INI *ini, void *, void *store, const void *)
{
	Coord3D *c = (Coord3D *)store;
	c->x = ini->scanReal(ini->getNextSubToken("X"));
	c->y = ini->scanReal(ini->getNextSubToken("Y"));
	c->z = ini->scanReal(ini->getNextSubToken("Z"));
}

void INI::parseCoord2D(INI *ini, void *, void *store, const void *)
{
	Coord2D *c = (Coord2D *)store;
	c->x = ini->scanReal(ini->getNextSubToken("X"));
	c->y = ini->scanReal(ini->getNextSubToken("Y"));
}

void INI::parseICoord2D(INI *ini, void *, void *store, const void *)
{
	ICoord2D *c = (ICoord2D *)store;
	c->x = ini->scanInt(ini->getNextSubToken("X"));
	c->y = ini->scanInt(ini->getNextSubToken("Y"));
}
