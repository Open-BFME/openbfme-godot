// OpenBFME. GPL-3.0.
// See GameLogic/Map/CastleTemplates.h.

#include "GameLogic/Map/CastleTemplates.h"

#include "Common/ArchiveFileSystem.h"
#include "GameClient/MapUtil.h"

#include <algorithm>
#include <cctype>

namespace
{
std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}
} // namespace

bool CastleTemplateStore::parseBse(const std::vector<std::uint8_t> &bytes, const std::string &sourceName, const std::string &name, CastleTemplate &out, std::string *error)
{
	LoadedMap map;
	MapReadOptions options; // strict: an unknown chunk or leftover bytes is an error
	std::string e;
	if (!MapReader::load(bytes, sourceName, options, map, &e))
	{
		if (error)
		{
			*error = sourceName + ": " + e;
		}
		return false;
	}
	for (const CastleTemplate &t : map.chunks.castleTemplates)
	{
		if (lower(t.name) == lower(name))
		{
			out = t;
			return true;
		}
	}
	if (error)
	{
		*error = sourceName + ": no CastleTemplates chunk named '" + name + "'";
	}
	return false;
}

CastleTemplateStore::Loader CastleTemplateStore::fileSystemLoader(ArchiveFileSystem &fs)
{
	return [&fs](const std::string &name, CastleTemplate &out, std::string *error) {
		const std::string path = "Bases\\" + lower(name) + "\\" + lower(name) + ".bse"; // RW 0xC24128 + name + "\" + name + ".bse" (0xC24120)
		std::vector<std::uint8_t> bytes;
		std::string e;
		if (!fs.readFile(path, bytes, &e))
		{
			if (error)
			{
				*error = "castle template '" + name + "': cannot read " + path + ": " + e;
			}
			return false;
		}
		return parseBse(bytes, path, name, out, error);
	};
}

const CastleTemplate *CastleTemplateStore::find(const std::string &name, std::string *error)
{
	const std::string key = lower(name);
	auto it = m_templates.find(key);
	if (it != m_templates.end())
	{
		return &it->second;
	}
	if (!m_loader)
	{
		if (error)
		{
			*error = "castle template '" + name + "': no loader installed";
		}
		return nullptr;
	}
	CastleTemplate t;
	std::string e;
	if (!m_loader(name, t, &e))
	{
		m_errors.push_back(e);
		if (error)
		{
			*error = e;
		}
		return nullptr;
	}
	return &(m_templates[key] = std::move(t));
}

void CastleTemplateStore::add(CastleTemplate t)
{
	m_templates[lower(t.name)] = std::move(t);
}
