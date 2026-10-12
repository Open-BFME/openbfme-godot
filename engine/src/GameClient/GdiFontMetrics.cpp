// OpenBFME. GPL-3.0.
// See GameClient/GdiFontMetrics.h.

#include "GameClient/GdiFontMetrics.h"

#include <algorithm>

namespace
{
std::uint32_t be32(const std::uint8_t *p)
{
	return ((std::uint32_t)p[0] << 24) | ((std::uint32_t)p[1] << 16) | ((std::uint32_t)p[2] << 8) | p[3];
}

std::uint16_t be16(const std::uint8_t *p)
{
	return (std::uint16_t)((p[0] << 8) | p[1]);
}

std::int16_t bes16(const std::uint8_t *p)
{
	return (std::int16_t)be16(p);
}

// a design-unit value at ppem pixels, rounded to the nearest pixel (halves up), as Wine's GDI (FT_MulFix, then (v + 32) >> 6)
int scaled(int value, int ppem, int unitsPerEm)
{
	const long long v = (long long)value * ppem * 2;
	if (v >= 0)
	{
		return (int)((v + unitsPerEm) / (2LL * unitsPerEm));
	}
	return -(int)((-v + unitsPerEm - 1) / (2LL * unitsPerEm));
}
} // namespace

bool ReadGdiFaceMetrics(const std::uint8_t *data, std::size_t size, GdiFaceMetrics &out, std::string *error)
{
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = why;
		}
		return false;
	};
	if (!data || size < 12)
	{
		return fail("not a font file (no table directory)");
	}
	const std::uint16_t tables = be16(data + 4);
	if (12 + (std::size_t)tables * 16 > size)
	{
		return fail("the table directory runs past the file");
	}
	const std::uint8_t *head = nullptr, *hhea = nullptr, *os2 = nullptr;
	std::size_t os2Size = 0;
	for (std::uint16_t i = 0; i < tables; ++i)
	{
		const std::uint8_t *rec = data + 12 + (std::size_t)i * 16;
		const std::uint32_t offset = be32(rec + 8), length = be32(rec + 12);
		if ((std::size_t)offset > size || (std::size_t)length > size - offset)
		{
			return fail("a table runs past the file");
		}
		const std::string tag(reinterpret_cast<const char *>(rec), 4);
		if (tag == "head" && length >= 54)
		{
			head = data + offset;
		}
		else if (tag == "hhea" && length >= 36)
		{
			hhea = data + offset;
		}
		else if (tag == "OS/2")
		{
			os2 = data + offset;
			os2Size = length;
		}
	}
	if (!head || !hhea)
	{
		return fail("no head or hhea table");
	}
	if (!os2 || os2Size < 78)
	{
		return fail("no OS/2 table with the win metrics");
	}
	GdiFaceMetrics m;
	m.unitsPerEm = be16(head + 18);
	m.hheaAscent = bes16(hhea + 4);
	m.hheaDescent = bes16(hhea + 6);
	m.hheaLineGap = bes16(hhea + 8);
	m.winAscent = be16(os2 + 74);
	m.winDescent = be16(os2 + 76);
	if (m.unitsPerEm <= 0)
	{
		return fail("unitsPerEm is 0");
	}
	if (m.winAscent + m.winDescent <= 0)
	{
		return fail("the OS/2 win metrics are 0");
	}
	out = m;
	return true;
}

GdiTextCell GdiTextCellAt(const GdiFaceMetrics &face, int ppem, int extra)
{
	GdiTextCell c;
	if (ppem <= 0 || extra <= 0 || face.unitsPerEm <= 0)
	{
		return c;
	}
	const int em = ppem * extra; // RW 0x553FD0: CreateFontA(-floor(size * extra + 0.5)), the size already on the 1 / extra grid (RW 0x61829B)
	c.ascent = scaled(face.winAscent, em, face.unitsPerEm);
	c.descent = scaled(face.winDescent, em, face.unitsPerEm);
	// tmExternalLeading = max(0, lineGap - ((usWinAscent + usWinDescent) - (ascender - descender))), in design units, then scaled
	const int leading = face.hheaLineGap - ((face.winAscent + face.winDescent) - (face.hheaAscent - face.hheaDescent));
	c.externalLeading = std::max(0, scaled(leading, em, face.unitsPerEm));
	c.height = (extra + c.externalLeading + (c.ascent + c.descent) - 1) / extra; // + 0x2C
	c.leading = c.externalLeading / extra;                                       // + 0x30
	c.baseline = (float)(c.leading / 2) + (float)c.ascent / (float)extra;      // RW 0x556579: (+ 0x30) / 2 rows above the glyph
	return c;
}
