// OpenBFME. GPL-3.0.
// The Create-a-Heroes a player can pick (lane CAH-1). See GameClient/CreateAHeroHeroList.h for the target facts.

#include "GameClient/CreateAHeroHeroList.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

namespace
{
std::string lowerSlashes(std::string s)
{
	for (char &c : s)
	{
		c = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c == '/' ? '\\' : c);
	}
	return s;
}

// "MyHero*.cah" (RW 0xBFC5E8): the archives compare names without case
bool matchesPattern(const std::string &name)
{
	const std::string l = lowerSlashes(name);
	return l.size() >= 10 && l.compare(0, 6, "myhero") == 0 && l.compare(l.size() - 4, 4, ".cah") == 0;
}

std::string afterSlash(const std::string &path)
{
	const size_t p = path.find_last_of("\\/");
	return p == std::string::npos ? path : path.substr(p + 1);
}
} // namespace

void CreateAHeroHeroList::load(ArchiveFileSystem *fs, const std::string &profileDir, std::vector<std::string> *errors)
{
	m_entries.clear();
	m_profileDir = profileDir;
	if (fs)
	{
		FilenameList files;
		fs->getFileListInDirectory("", "", "*.cah", files, true);
		for (const std::string &f : files)
		{
			const std::string l = lowerSlashes(f);
			if (l.rfind("data\\systemheroes\\", 0) != 0 || l.find('\\', 18) != std::string::npos || !matchesPattern(afterSlash(f)))
			{
				continue;
			}
			std::vector<std::uint8_t> bytes;
			std::string err;
			CreateAHeroListEntry e;
			if (!fs->readFile(f, bytes, &err) || !e.hero.load(bytes, &err))
			{
				if (errors)
				{
					errors->push_back(f + ": " + err);
				}
				continue;
			}
			e.fileName = afterSlash(f);
			e.system = true;
			m_entries.push_back(std::move(e));
		}
	}
	if (!profileDir.empty())
	{
		std::error_code ec;
		const std::filesystem::path dir = std::filesystem::u8path(profileDir);
		if (std::filesystem::is_directory(dir, ec))
		{
			std::vector<std::filesystem::path> paths;
			for (const auto &it : std::filesystem::directory_iterator(dir, ec))
			{
				if (it.is_regular_file(ec) && matchesPattern(it.path().filename().u8string()))
				{
					paths.push_back(it.path());
				}
			}
			for (const auto &p : paths)
			{
				std::ifstream in(p, std::ios::binary);
				std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				std::string err;
				CreateAHeroListEntry e;
				if (!in.good() && !in.eof())
				{
					err = "cannot be read";
				}
				if (!err.empty() || !e.hero.load(bytes, &err))
				{
					if (errors)
					{
						errors->push_back(p.u8string() + ": " + err);
					}
					continue;
				}
				e.fileName = p.filename().u8string();
				e.system = false;
				m_entries.push_back(std::move(e));
			}
		}
	}
	sort();
}

void CreateAHeroHeroList::sort()
{
	std::stable_sort(m_entries.begin(), m_entries.end(), [](const CreateAHeroListEntry &a, const CreateAHeroListEntry &b) {
		if (a.system != b.system)
		{
			return a.system;
		}
		return AsciiStringUtil::compareNoCase(a.fileName, b.fileName) < 0;
	});
}

int CreateAHeroHeroList::findByUniqueID(const std::string &id) const
{
	for (size_t i = 0; i < m_entries.size(); ++i)
	{
		if (m_entries[i].hero.uniqueID == id)
		{
			return (int)i;
		}
	}
	return -1;
}

bool CreateAHeroHeroList::save(const CreateAHeroHero &hero, std::string *error, int *index)
{
	if (hero.uniqueID.empty())
	{
		if (error)
		{
			*error = "the hero has no unique id (RW 0x80A352 gives one when it is named)";
		}
		return false;
	}
	if (m_profileDir.empty())
	{
		if (error)
		{
			*error = "no profile folder to save the hero to";
		}
		return false;
	}
	std::error_code ec;
	const std::filesystem::path dir = std::filesystem::u8path(m_profileDir);
	std::filesystem::create_directories(dir, ec);
	const std::string name = fileNameOf(hero.uniqueID);
	const std::vector<std::uint8_t> bytes = hero.save();
	{
		std::ofstream out(dir / std::filesystem::u8path(name), std::ios::binary | std::ios::trunc);
		out.write((const char *)bytes.data(), (std::streamsize)bytes.size());
		if (!out.good())
		{
			if (error)
			{
				*error = "cannot write " + (dir / std::filesystem::u8path(name)).u8string();
			}
			return false;
		}
	}
	CreateAHeroListEntry e;
	std::string err;
	if (!e.hero.load(bytes, &err)) // the list holds the record as a later load reads it (version, flags, checksum)
	{
		if (error)
		{
			*error = "the saved hero does not load back: " + err;
		}
		return false;
	}
	e.fileName = name;
	e.system = false;
	const int at = findByUniqueID(hero.uniqueID);
	if (at >= 0 && !m_entries[(size_t)at].system)
	{
		m_entries[(size_t)at] = std::move(e);
	}
	else
	{
		m_entries.push_back(std::move(e));
	}
	sort();
	if (index)
	{
		*index = findByUniqueID(hero.uniqueID);
	}
	return true;
}

bool CreateAHeroHeroList::remove(int index, std::string *error)
{
	const CreateAHeroListEntry *e = at(index);
	if (!e || e->system)
	{
		if (error)
		{
			*error = e ? "a system hero cannot be deleted (RW 0x9C5D6F)" : "no hero at that index";
		}
		return false;
	}
	std::error_code ec;
	std::filesystem::remove(std::filesystem::u8path(m_profileDir) / std::filesystem::u8path(e->fileName), ec);
	if (ec)
	{
		if (error)
		{
			*error = "cannot delete " + e->fileName + ": " + ec.message();
		}
		return false;
	}
	m_entries.erase(m_entries.begin() + index);
	return true;
}

std::string CreateAHeroHeroList::newUniqueID()
{
	// CoCreateGuid (RW 0x80A3A8) -> "%X%X%X%X%X%X%X" of Data1 (u32), Data2, Data3 (u16), Data4[0..3] (bytes)
	std::random_device rd;
	const std::uint32_t d1 = rd();
	const std::uint32_t w = rd();
	const std::uint32_t b = rd();
	char buf[64];
	std::snprintf(buf, sizeof buf, "%X%X%X%X%X%X%X", d1, w & 0xFFFFu, w >> 16, b & 0xFFu, (b >> 8) & 0xFFu, (b >> 16) & 0xFFu, b >> 24);
	return buf;
}
