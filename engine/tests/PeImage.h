// OpenBFME unit tests. GPL-3.0. A read-only view of a PE image by virtual address (a game.dat named by RW_GAME_DAT), for binary-fact tests.
// Nothing of the image is stored or copied anywhere; the tests read addresses, names and counts from it at run time.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace retailtest
{
class PeImage
{
public:
	// nullptr when RW_GAME_DAT is unset; throws std::runtime_error when it names something that is not a readable PE
	static const PeImage *fromEnvironment()
	{
		static std::unique_ptr<PeImage> image;
		static bool tried = false;
		if (!tried)
		{
			tried = true;
			const char *path = std::getenv("RW_GAME_DAT");
			if (path && *path)
			{
				image.reset(new PeImage(path));
			}
		}
		return image.get();
	}

	explicit PeImage(const std::string &path)
	{
		FILE *f = std::fopen(path.c_str(), "rb");
		if (!f)
		{
			throw std::runtime_error("RW_GAME_DAT cannot be opened");
		}
		std::fseek(f, 0, SEEK_END);
		const long size = std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
		m_data.resize((size_t)size);
		if (std::fread(m_data.data(), 1, (size_t)size, f) != (size_t)size)
		{
			std::fclose(f);
			throw std::runtime_error("RW_GAME_DAT cannot be read");
		}
		std::fclose(f);
		const std::uint32_t pe = u32(0x3C);
		if (m_data.size() < pe + 0x100 || std::memcmp(&m_data[pe], "PE\0\0", 4) != 0)
		{
			throw std::runtime_error("RW_GAME_DAT is not a PE image");
		}
		const unsigned sections = u16(pe + 6);
		const std::uint32_t opt = pe + 24;
		m_base = u32(opt + 28);
		const std::uint32_t first = opt + u16(pe + 20);
		for (unsigned i = 0; i < sections; ++i)
		{
			Section s;
			s.virtualSize = u32(first + i * 40 + 8);
			s.rva = u32(first + i * 40 + 12);
			s.rawSize = u32(first + i * 40 + 16);
			s.rawPtr = u32(first + i * 40 + 20);
			m_sections.push_back(s);
		}
	}

	// n bytes at a virtual address; false when the range is not inside one section's raw data
	bool read(std::uint32_t va, size_t n, std::vector<std::uint8_t> *out) const
	{
		const std::uint32_t rva = va - m_base;
		for (const Section &s : m_sections)
		{
			if (rva >= s.rva && rva + n <= s.rva + (s.rawSize > s.virtualSize ? s.rawSize : s.virtualSize) && s.rawPtr + (rva - s.rva) + n <= m_data.size())
			{
				out->assign(m_data.begin() + s.rawPtr + (rva - s.rva), m_data.begin() + s.rawPtr + (rva - s.rva) + n);
				return true;
			}
		}
		return false;
	}
	std::uint32_t u32At(std::uint32_t va) const
	{
		std::vector<std::uint8_t> b;
		if (!read(va, 4, &b))
		{
			throw std::runtime_error("address outside the image");
		}
		return (std::uint32_t)b[0] | ((std::uint32_t)b[1] << 8) | ((std::uint32_t)b[2] << 16) | ((std::uint32_t)b[3] << 24);
	}
	std::string cstring(std::uint32_t va, size_t limit = 200) const
	{
		std::string s;
		for (size_t i = 0; i < limit; ++i)
		{
			std::vector<std::uint8_t> b;
			if (!read(va + (std::uint32_t)i, 1, &b) || b[0] == 0)
			{
				break;
			}
			s.push_back((char)b[0]);
		}
		return s;
	}
	std::string hex(std::uint32_t va, size_t n) const
	{
		std::vector<std::uint8_t> b;
		if (!read(va, n, &b))
		{
			return "";
		}
		static const char *d = "0123456789abcdef";
		std::string s;
		for (std::uint8_t c : b)
		{
			s.push_back(d[c >> 4]);
			s.push_back(d[c & 15]);
		}
		return s;
	}
	std::uint32_t base() const { return m_base; }

private:
	struct Section
	{
		std::uint32_t virtualSize, rva, rawSize, rawPtr;
	};
	std::uint32_t u32(size_t o) const { return (std::uint32_t)m_data[o] | ((std::uint32_t)m_data[o + 1] << 8) | ((std::uint32_t)m_data[o + 2] << 16) | ((std::uint32_t)m_data[o + 3] << 24); }
	unsigned u16(size_t o) const { return (unsigned)m_data[o] | ((unsigned)m_data[o + 1] << 8); }
	std::vector<std::uint8_t> m_data;
	std::vector<Section> m_sections;
	std::uint32_t m_base = 0;
};
} // namespace retailtest
