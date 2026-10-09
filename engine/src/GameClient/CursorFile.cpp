// OpenBFME. GPL-3.0.
// See CursorFile.h.

#include "GameClient/CursorFile.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

namespace
{
std::uint32_t rd32(const std::uint8_t *p) { return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24); }
std::uint16_t rd16(const std::uint8_t *p) { return (std::uint16_t)(p[0] | (p[1] << 8)); }

bool decodeCur(const std::uint8_t *d, size_t n, CursorImage &out, std::string *error)
{
	auto fail = [&](const char *m) {
		if (error)
		{
			*error = m;
		}
		return false;
	};
	if (n < 22 || (rd16(d + 2) != 2 && rd16(d + 2) != 1) || rd16(d + 4) < 1) // the reserved word is ignored (Beam.ani carries 0x0200)
	{
		return fail("not a .cur / .ico file");
	}
	// the first entry
	const int w0 = d[6] ? d[6] : 256, h0 = d[7] ? d[7] : 256;
	const std::uint32_t size = rd32(d + 14), offset = rd32(d + 18);
	const int hotX = rd16(d + 10), hotY = rd16(d + 12);
	if ((size_t)offset + 40 > n || (size_t)offset + size > n)
	{
		return fail("the image lies outside the file");
	}
	const std::uint8_t *b = d + offset;
	const std::uint32_t hdr = rd32(b);
	const int w = (int)rd32(b + 4), hh = (int)rd32(b + 8), bits = rd16(b + 14);
	const int h = hh / 2;
	if (hdr < 40 || w != w0 || h != h0 || rd32(b + 16) != 0)
	{
		return fail("unsupported bitmap header (compressed or sizes disagree)");
	}
	size_t colors = rd32(b + 32);
	if (bits <= 8 && colors == 0)
	{
		colors = (size_t)1 << bits;
	}
	const std::uint8_t *pal = b + hdr;
	const std::uint8_t *xorBits = pal + (bits <= 8 ? colors * 4 : 0);
	const size_t xorStride = (((size_t)w * (size_t)bits + 31) / 32) * 4, andStride = (((size_t)w + 31) / 32) * 4;
	const std::uint8_t *andBits = xorBits + xorStride * (size_t)h;
	if ((size_t)(andBits - d) + andStride * (size_t)h > n && bits != 32)
	{
		return fail("the bitmap is truncated");
	}
	std::vector<std::uint8_t> rgba((size_t)w * (size_t)h * 4);
	bool anyAlpha = false;
	for (int y = 0; y < h; ++y)
	{
		const std::uint8_t *row = xorBits + xorStride * (size_t)(h - 1 - y);
		for (int x = 0; x < w; ++x)
		{
			std::uint8_t *o = &rgba[((size_t)y * (size_t)w + (size_t)x) * 4];
			if (bits == 32)
			{
				o[0] = row[x * 4 + 2];
				o[1] = row[x * 4 + 1];
				o[2] = row[x * 4];
				o[3] = row[x * 4 + 3];
				anyAlpha = anyAlpha || o[3] != 0;
			}
			else if (bits == 24)
			{
				o[0] = row[x * 3 + 2];
				o[1] = row[x * 3 + 1];
				o[2] = row[x * 3];
				o[3] = 255;
			}
			else
			{
				size_t idx;
				if (bits == 8)
				{
					idx = row[x];
				}
				else if (bits == 4)
				{
					idx = (x & 1) ? (row[x / 2] & 15) : (row[x / 2] >> 4);
				}
				else if (bits == 1)
				{
					idx = (row[x / 8] >> (7 - (x & 7))) & 1;
				}
				else
				{
					return fail("unsupported bit depth");
				}
				idx = std::min(idx, colors ? colors - 1 : (size_t)0);
				o[0] = pal[idx * 4 + 2];
				o[1] = pal[idx * 4 + 1];
				o[2] = pal[idx * 4];
				o[3] = 255;
			}
		}
	}
	if (!anyAlpha)
	{
		for (int y = 0; y < h; ++y)
		{
			const std::uint8_t *row = andBits + andStride * (size_t)(h - 1 - y);
			for (int x = 0; x < w; ++x)
			{
				if ((row[x / 8] >> (7 - (x & 7))) & 1)
				{
					rgba[((size_t)y * (size_t)w + (size_t)x) * 4 + 3] = 0;
				}
			}
		}
	}
	if (out.frames.empty())
	{
		out.width = w;
		out.height = h;
		out.hotX = hotX;
		out.hotY = hotY;
	}
	else if (out.width != w || out.height != h)
	{
		return fail("the frames differ in size");
	}
	out.frames.push_back(std::move(rgba));
	return true;
}
} // namespace

bool DecodeCursorFile(const std::vector<std::uint8_t> &bytes, CursorImage &out, std::string *error)
{
	out = CursorImage();
	const std::uint8_t *d = bytes.data();
	const size_t n = bytes.size();
	if (n >= 12 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "ACON", 4) == 0)
	{
		size_t off = 12;
		int steps = 0, defaultJiffies = 0;
		std::vector<int> rates, seq;
		while (off + 8 <= n)
		{
			const std::uint32_t sz = rd32(d + off + 4);
			const std::uint8_t *c = d + off + 8;
			if (off + 8 + sz > n)
			{
				if (error)
				{
					*error = "a chunk is truncated";
				}
				return false;
			}
			if (std::memcmp(d + off, "anih", 4) == 0 && sz >= 36)
			{
				steps = (int)rd32(c + 12);
				defaultJiffies = (int)rd32(c + 28);
			}
			else if (std::memcmp(d + off, "rate", 4) == 0)
			{
				for (std::uint32_t i = 0; i + 4 <= sz; i += 4)
				{
					rates.push_back((int)rd32(c + i));
				}
			}
			else if (std::memcmp(d + off, "seq ", 4) == 0)
			{
				for (std::uint32_t i = 0; i + 4 <= sz; i += 4)
				{
					seq.push_back((int)rd32(c + i));
				}
			}
			else if (std::memcmp(d + off, "LIST", 4) == 0 && sz >= 4 && std::memcmp(c, "fram", 4) == 0)
			{
				size_t f = 4;
				while (f + 8 <= sz)
				{
					const std::uint32_t isz = rd32(c + f + 4);
					if (f + 8 + isz > sz)
					{
						if (error)
						{
							*error = "an icon chunk is truncated";
						}
						return false;
					}
					if (std::memcmp(c + f, "icon", 4) == 0 && !decodeCur(c + f + 8, isz, out, error))
					{
						return false;
					}
					f += 8 + isz + (isz & 1);
				}
			}
			off += 8 + sz + (sz & 1);
		}
		if (out.frames.empty())
		{
			if (error)
			{
				*error = "no frames";
			}
			return false;
		}
		if (seq.empty())
		{
			for (int i = 0; i < (int)out.frames.size(); ++i)
			{
				seq.push_back(i);
			}
		}
		out.sequence = seq;
		(void)steps;
		for (size_t i = 0; i < seq.size(); ++i)
		{
			if (seq[i] < 0 || seq[i] >= (int)out.frames.size())
			{
				if (error)
				{
					*error = "the sequence names a missing frame";
				}
				return false;
			}
			out.jiffies.push_back(i < rates.size() ? rates[i] : (defaultJiffies > 0 ? defaultJiffies : 6));
		}
		return true;
	}
	if (!decodeCur(d, n, out, error))
	{
		return false;
	}
	out.sequence = { 0 };
	out.jiffies = { 6 };
	return true;
}

bool ParseMouseCursors(const std::string &text, std::map<std::string, MouseCursorEntry> &out, std::string *error)
{
	std::istringstream in(text);
	std::string line;
	MouseCursorEntry *cur = nullptr;
	int lineNo = 0;
	while (std::getline(in, line))
	{
		++lineNo;
		const size_t c = line.find(';');
		if (c != std::string::npos)
		{
			line.erase(c);
		}
		std::istringstream ls(line);
		std::string a;
		if (!(ls >> a))
		{
			continue;
		}
		if (a == "MouseCursor")
		{
			std::string name;
			if (!(ls >> name))
			{
				if (error)
				{
					*error = "mouse.ini line " + std::to_string(lineNo) + ": a MouseCursor block without a name";
				}
				return false;
			}
			cur = &out[name];
			continue;
		}
		if (a == "End")
		{
			cur = nullptr;
			continue;
		}
		if (!cur)
		{
			continue;
		}
		std::string eq;
		if (!(ls >> eq) || eq != "=")
		{
			continue;
		}
		if (a == "Image")
		{
			ls >> cur->image;
		}
		else if (a == "Directions")
		{
			ls >> cur->directions;
		}
		else if (a == "HotSpot")
		{
			std::string x, y;
			ls >> x >> y; // "X:2" "Y:2"
			if (x.size() > 2 && y.size() > 2)
			{
				cur->hotX = std::atoi(x.c_str() + 2);
				cur->hotY = std::atoi(y.c_str() + 2);
			}
		}
	}
	return true;
}

std::string FindFileNoCase(const std::string &dir, const std::string &name)
{
	auto lower = [](std::string v) {
		for (char &c : v)
		{
			c = (char)std::tolower((unsigned char)c);
		}
		return v;
	};
	const std::string want = lower(name);
	std::error_code ec;
	for (const std::filesystem::directory_entry &e : std::filesystem::directory_iterator(dir, ec))
	{
		if (lower(e.path().filename().string()) == want)
		{
			return e.path().string();
		}
	}
	return std::string();
}

std::vector<std::string> CursorAcceptanceStops()
{
	return {
		"[S-295] cursors: the pointer images are the loose files of data/cursors named by Mouse.ini (ANI / CUR decoded to RGBA, case-insensitive lookup); a Mouse.ini block whose file the install does not ship "
		"shows the engine's own pointer (reported, not hidden); retail's Win32 cursor loading (hotspot scaling, the 3D W3D cursors, the cursor animation timing) is not ported",
	};
}
