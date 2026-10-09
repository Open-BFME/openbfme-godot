// OpenBFME. GPL-3.0.
// The Create-a-Hero record and its .cah file (lane HERO-2). See Common/CreateAHeroRecord.h for the target facts.

#include "Common/CreateAHeroRecord.h"

#include <cstring>

namespace
{
const std::uint32_t kMagicALAE = 0x45414C41; // RW 0xA200E0
const std::uint32_t kType1STR = 0x52545331;  // RW 0xA200E9
const std::uint32_t kType2STR = 0x52545332;  // RW 0xA200F3

struct Table
{
	std::uint32_t v[256];
	Table()
	{
		for (std::uint32_t i = 0; i < 256; ++i)
		{
			std::uint32_t c = i;
			for (int k = 0; k < 8; ++k)
			{
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			}
			v[i] = c;
		}
	}
};

// the reader: every field read through it also feeds the checksum (unless told not to)
class Reader
{
public:
	Reader(const std::vector<std::uint8_t> &b) : m_b(b) {}
	bool ok() const { return m_ok; }
	size_t pos() const { return m_pos; }
	std::uint32_t crc = 0;
	std::uint32_t u32(bool sum = true)
	{
		std::uint8_t r[4] = {};
		bytes(r, 4);
		if (sum)
		{
			crc = CreateAHeroRecord::crc32(r, 4, crc);
		}
		return (std::uint32_t)r[0] | ((std::uint32_t)r[1] << 8) | ((std::uint32_t)r[2] << 16) | ((std::uint32_t)r[3] << 24);
	}
	std::uint8_t u8()
	{
		std::uint8_t r = 0;
		bytes(&r, 1);
		return r;
	}
	std::string ascii(bool sum)
	{
		const size_t n = u8();
		std::string s(n, '\0');
		bytes(n ? &s[0] : nullptr, n);
		const size_t z = s.find('\0'); // RW 0xA2D7B0 stops at the terminator
		if (sum)
		{
			crc = CreateAHeroRecord::crc32(s.data(), z == std::string::npos ? s.size() : z, crc);
		}
		return s;
	}
	std::u16string unicode()
	{
		const size_t n = u8();
		std::u16string s;
		std::string translated;
		for (size_t i = 0; i < n; ++i)
		{
			std::uint8_t r[2] = {};
			bytes(r, 2);
			const char16_t c = (char16_t)(r[0] | (r[1] << 8));
			s.push_back(c);
			translated.push_back((char)(c & 0xFF)); // RW 0x437B90
		}
		const size_t z = translated.find('\0');
		crc = CreateAHeroRecord::crc32(translated.data(), z == std::string::npos ? translated.size() : z, crc);
		return s;
	}
	bool boolean(bool sum)
	{
		const std::uint8_t r = u8();
		if (sum)
		{
			crc = CreateAHeroRecord::crc32(&r, 1, crc);
		}
		return r != 0;
	}

private:
	void bytes(void *out, size_t n)
	{
		if (!m_ok || m_pos + n > m_b.size())
		{
			m_ok = false;
			if (out && n)
			{
				std::memset(out, 0, n);
			}
			return;
		}
		if (n)
		{
			std::memcpy(out, m_b.data() + m_pos, n);
		}
		m_pos += n;
	}
	const std::vector<std::uint8_t> &m_b;
	size_t m_pos = 0;
	bool m_ok = true;
};

class Writer
{
public:
	std::vector<std::uint8_t> out;
	std::uint32_t crc = 0;
	void u32(std::uint32_t v, bool sum = true)
	{
		const std::uint8_t r[4] = { (std::uint8_t)v, (std::uint8_t)(v >> 8), (std::uint8_t)(v >> 16), (std::uint8_t)(v >> 24) };
		out.insert(out.end(), r, r + 4);
		if (sum)
		{
			crc = CreateAHeroRecord::crc32(r, 4, crc);
		}
	}
	void ascii(const std::string &s, bool sum)
	{
		const size_t n = s.size() > 255 ? 255 : s.size();
		out.push_back((std::uint8_t)n);
		out.insert(out.end(), s.begin(), s.begin() + (std::ptrdiff_t)n);
		if (sum)
		{
			const size_t z = s.find('\0');
			crc = CreateAHeroRecord::crc32(s.data(), z == std::string::npos || z > n ? n : z, crc);
		}
	}
	void unicode(const std::u16string &s)
	{
		const size_t n = s.size() > 255 ? 255 : s.size();
		out.push_back((std::uint8_t)n);
		std::string translated;
		for (size_t i = 0; i < n; ++i)
		{
			out.push_back((std::uint8_t)(s[i] & 0xFF));
			out.push_back((std::uint8_t)(s[i] >> 8));
			translated.push_back((char)(s[i] & 0xFF));
		}
		const size_t z = translated.find('\0');
		crc = CreateAHeroRecord::crc32(translated.data(), z == std::string::npos ? translated.size() : z, crc);
	}
	void boolean(bool b)
	{
		const std::uint8_t r = b ? 1 : 0;
		out.push_back(r);
		crc = CreateAHeroRecord::crc32(&r, 1, crc);
	}
};
} // namespace

std::uint32_t CreateAHeroRecord::crc32(const void *data, size_t n, std::uint32_t crc)
{
	static const Table table;
	const std::uint8_t *p = static_cast<const std::uint8_t *>(data);
	crc = ~crc;
	for (size_t i = 0; i < n; ++i)
	{
		crc = table.v[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
	}
	return ~crc;
}

void CreateAHeroHero::setBling(const std::string &group, std::uint32_t index)
{
	for (auto &b : bling)
	{
		if (b.first == group)
		{
			b.second = index;
			return;
		}
	}
	bling.emplace_back(group, index);
}

const std::uint32_t *CreateAHeroHero::findBling(const std::string &group) const
{
	for (const auto &b : bling)
	{
		if (b.first == group)
		{
			return &b.second;
		}
	}
	return nullptr;
}

bool CreateAHeroHero::load(const std::vector<std::uint8_t> &bytes, std::string *error)
{
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = why;
		}
		return false;
	};
	Reader r(bytes);
	// RW 0xA200A3: the stream header
	const std::uint32_t magic = r.u32(false), type = r.u32(false);
	if (!r.ok() || magic != kMagicALAE || (type != kType1STR && type != kType2STR))
	{
		return fail("not an ALAE 1STR / 2STR stream");
	}
	if (type == kType2STR)
	{
		const std::uint32_t streamVersion = r.u32(false);
		if (streamVersion > 1)
		{
			return fail("stream version " + std::to_string(streamVersion) + " (1 is the newest the loader RW 0x80C2E8 accepts)");
		}
	}
	if (r.u32(false) != 0)
	{
		return fail("a KLBE block stream (RW 0xA20149) is not ported"); // no retail .cah uses one
	}
	// RW 0x80B4F5
	*this = CreateAHeroHero();
	version = r.u8();
	if (version > XFER_VERSION)
	{
		return fail("record version " + std::to_string(version) + " is newer than 8");
	}
	r.crc = 0;
	objectID = r.u32();
	name = r.unicode();
	classIndex = r.u32();
	subClassIndex = r.u32();
	r.u32(); // Dummy
	r.u32(); // Dummy
	primaryColor = r.u32();
	secondaryColor = r.u32();
	tertiaryColor = r.u32();
	for (CreateAHeroPower &p : powers) // RW 0x809A31
	{
		p.commandButton = r.ascii(true);
		p.expLevel = r.u32();
		p.buttonIndex = r.u32();
	}
	const std::uint32_t count = r.u32(); // BlingCount
	for (std::uint32_t i = 0; i < count && r.ok(); ++i)
	{
		std::string group = r.ascii(true);
		if (group.empty())
		{
			return fail("Fatal error in CreateAHeroHero::DoXfer(Xfer *xfer): an empty GroupName"); // RW 0xC4F2D8
		}
		const std::uint32_t index = r.u32(); // BlingIndex
		setBling(group, index);
	}
	if (version > 6)
	{
		uniqueID = r.ascii(false);
	}
	if (version > 7)
	{
		isSystemHero = r.boolean(true);
		const std::uint32_t computed = r.crc;
		checksum = r.u32(false);
		valid = checksum == computed; // RW 0x80BA7C
	}
	else
	{
		valid = true; // RW 0x80BAA1
		checksum = r.crc;
	}
	if (!r.ok())
	{
		return fail("the record is truncated");
	}
	if (r.pos() != bytes.size())
	{
		return fail(std::to_string(bytes.size() - r.pos()) + " bytes after the record");
	}
	flags = LOAD_FLAGS; // RW 0x80BAB7
	return true;
}

std::vector<std::uint8_t> CreateAHeroHero::save() const
{
	Writer w;
	w.u32(kMagicALAE, false);
	w.u32(kType2STR, false);
	w.u32(1, false); // the version word of every retail file
	w.u32(0, false); // not a block stream
	w.out.push_back((std::uint8_t)XFER_VERSION);
	w.crc = 0;
	w.u32(objectID);
	w.unicode(name);
	w.u32(classIndex);
	w.u32(subClassIndex);
	w.u32(0); // Dummy
	w.u32(0); // Dummy
	w.u32(primaryColor);
	w.u32(secondaryColor);
	w.u32(tertiaryColor);
	for (const CreateAHeroPower &p : powers)
	{
		w.ascii(p.commandButton, true);
		w.u32(p.expLevel);
		w.u32(p.buttonIndex);
	}
	w.u32((std::uint32_t)bling.size());
	for (const auto &b : bling)
	{
		w.ascii(b.first, true);
		w.u32(b.second);
	}
	w.ascii(uniqueID, false);
	w.boolean(isSystemHero);
	w.u32(w.crc, false);
	return w.out;
}

std::uint32_t CreateAHeroHero::computeChecksum() const
{
	const std::vector<std::uint8_t> bytes = save();
	return (std::uint32_t)bytes[bytes.size() - 4] | ((std::uint32_t)bytes[bytes.size() - 3] << 8) | ((std::uint32_t)bytes[bytes.size() - 2] << 16)
		| ((std::uint32_t)bytes[bytes.size() - 1] << 24);
}

bool CreateAHeroHero::operator==(const CreateAHeroHero &o) const
{
	for (int i = 0; i < POWER_COUNT; ++i)
	{
		const CreateAHeroPower &a = powers[(size_t)i], &b = o.powers[(size_t)i];
		if (a.commandButton != b.commandButton || a.expLevel != b.expLevel || a.buttonIndex != b.buttonIndex)
		{
			return false;
		}
	}
	return objectID == o.objectID && name == o.name && classIndex == o.classIndex && subClassIndex == o.subClassIndex && primaryColor == o.primaryColor
		&& secondaryColor == o.secondaryColor && tertiaryColor == o.tertiaryColor && bling == o.bling && uniqueID == o.uniqueID && isSystemHero == o.isSystemHero
		&& checksum == o.checksum && valid == o.valid && flags == o.flags && version == o.version;
}
