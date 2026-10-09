// OpenBFME. GPL-3.0.
// See GameLogic/Object/ObjectNameIndex.h.

#include "GameLogic/Object/ObjectNameIndex.h"

#include "Common/AsciiString.h"

#include <cctype>

namespace
{

std::string lowerStr(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

bool isSpace(char c) { return c == ' ' || c == '\t'; }

// Lines of the form  <ws> keyword <ws> name ...   (keyword compared case-insensitively). The
// name stops at whitespace or ';'. Returns false when the line does not start with a keyword.
bool matchDefinition(const char *lineBegin, const char *lineEnd, const char *const *keywords, std::string &nameOut)
{
	const char *p = lineBegin;
	while (p < lineEnd && isSpace(*p))
	{
		++p;
	}
	for (const char *const *k = keywords; *k; ++k)
	{
		size_t n = std::char_traits<char>::length(*k);
		if ((size_t)(lineEnd - p) <= n)
		{
			continue;
		}
		bool eq = true;
		for (size_t i = 0; i < n; ++i)
		{
			if (std::tolower((unsigned char)p[i]) != std::tolower((unsigned char)(*k)[i]))
			{
				eq = false;
				break;
			}
		}
		if (!eq || !isSpace(p[n]))
		{
			continue;
		}
		const char *q = p + n;
		while (q < lineEnd && isSpace(*q))
		{
			++q;
		}
		const char *e = q;
		while (e < lineEnd && !isSpace(*e) && *e != ';' && *e != '\r')
		{
			++e;
		}
		if (e == q)
		{
			return false;
		}
		nameOut.assign(q, e);
		return true;
	}
	return false;
}

void scanWith(const std::string &text, const char *const *keywords, std::set<std::string> &names)
{
	const char *b = text.data();
	const char *end = b + text.size();
	while (b < end)
	{
		const char *nl = b;
		while (nl < end && *nl != '\n')
		{
			++nl;
		}
		std::string name;
		if (matchDefinition(b, nl, keywords, name))
		{
			names.insert(lowerStr(name));
		}
		b = nl + 1;
	}
}

} // namespace

namespace ObjectNameScan
{

void scanText(const std::string &text, std::set<std::string> &objectNames)
{
	static const char *const kw[] = { "Object", "ChildObject", "ObjectReskin", nullptr };
	scanWith(text, kw, objectNames);
}

void scanRoadText(const std::string &text, std::set<std::string> &roadNames)
{
	static const char *const kw[] = { "Road", "Bridge", nullptr };
	scanWith(text, kw, roadNames);
}

bool build(ArchiveFileSystem &fs, ObjectNameIndex &out, std::string *error)
{
	out = ObjectNameIndex();
	FilenameList files;
	fs.getFileListInDirectory("", "data\\ini", "*.*", files, true);
	for (const std::string &path : files)
	{
		std::string lower = lowerStr(path);
		bool ini = lower.size() > 4 && (lower.compare(lower.size() - 4, 4, ".ini") == 0 || lower.compare(lower.size() - 4, 4, ".inc") == 0);
		if (!ini)
		{
			continue;
		}
		std::vector<std::uint8_t> bytes;
		std::string e;
		if (!fs.readFile(path, bytes, &e))
		{
			if (error)
			{
				*error = "cannot read " + path + ": " + e;
			}
			return false;
		}
		std::string text(bytes.begin(), bytes.end());
		scanText(text, out.objectNames);
		if (lower.size() >= 9 && lower.compare(lower.size() - 9, 9, "roads.ini") == 0)
		{
			scanRoadText(text, out.roadNames);
		}
		++out.iniFilesScanned;
	}

	FilenameList maps;
	fs.getFileListInDirectory("", "maps", "map.ini", maps, true);
	for (const std::string &path : maps)
	{
		std::vector<std::uint8_t> bytes;
		std::string e;
		if (!fs.readFile(path, bytes, &e))
		{
			if (error)
			{
				*error = "cannot read " + path + ": " + e;
			}
			return false;
		}
		std::set<std::string> names;
		scanText(std::string(bytes.begin(), bytes.end()), names);
		if (!names.empty())
		{
			out.mapIniObjectNames[lowerStr(path)] = std::move(names);
		}
		++out.mapIniFilesScanned;
	}
	if (out.iniFilesScanned == 0)
	{
		if (error)
		{
			*error = "no INI files under data\\ini in the mounted archives";
		}
		return false;
	}
	return true;
}

} // namespace ObjectNameScan

const std::vector<std::string> &ObjectNameIndex::specialNames()
{
	// filled from the corpus run (see test_map_corpus.cpp); case-sensitive as the maps spell them
	static const std::vector<std::string> names = { "*Waypoints/Waypoint", "*GenericAIObjects/GenericAIObject" };
	return names;
}

ObjectNameIndex::Resolution ObjectNameIndex::resolve(const std::string &templateName, const std::string &mapIniKey) const
{
	if (!templateName.empty() && templateName[0] == '*')
	{
		for (const std::string &s : specialNames())
		{
			if (s == templateName)
			{
				return SpecialName;
			}
		}
		return Unresolved;
	}
	std::string l = lowerStr(templateName);
	if (objectNames.count(l))
	{
		return GlobalObject;
	}
	if (roadNames.count(l))
	{
		return RoadName;
	}
	auto it = mapIniObjectNames.find(lowerStr(mapIniKey));
	if (it != mapIniObjectNames.end() && it->second.count(l))
	{
		return MapIniObject;
	}
	return Unresolved;
}

ObjectNameIndex::Resolution ObjectNameIndex::resolveObject(const MapObject &object, const std::string &mapIniKey) const
{
	if (object.isScorch() || object.isWaypoint() || object.isLight())
	{
		return SpecialName;
	}
	return resolve(object.m_objectName, mapIniKey);
}
