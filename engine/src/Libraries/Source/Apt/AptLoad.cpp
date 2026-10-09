// OpenBFME. GPL-3.0.
// See AptLoad.h for the citations.

#include "Libraries/Source/Apt/AptLoad.h"

#include <set>

#include "Common/ArchiveFileSystem.h"

#include <algorithm>

namespace
{

std::string lowerAscii(const std::string &s)
{
	std::string o = s;
	for (char &ch : o)
	{
		if (ch >= 'A' && ch <= 'Z')
		{
			ch = (char)(ch - 'A' + 'a');
		}
	}
	return o;
}

} // namespace

bool AptArchiveFileSource::readFile(const std::string &virtualPath, std::vector<std::uint8_t> &out, std::string *error)
{
	return m_fs.readFile(virtualPath, out, error);
}

bool AptArchiveFileSource::fileExists(const std::string &virtualPath)
{
	return m_fs.doesFileExist(virtualPath);
}

std::vector<std::string> AptArchiveFileSource::listMovies()
{
	FilenameList list;
	m_fs.getFileListInDirectory(std::string(), std::string(), std::string("*.apt"), list, true);
	std::vector<std::string> names;
	for (const std::string &path : list)
	{
		// Movies live at the archive root; anything with a directory part is not an Apt movie.
		if (path.find('/') != std::string::npos || path.find('\\') != std::string::npos)
		{
			continue;
		}
		names.push_back(path.substr(0, path.size() - 4));
	}
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	return names;
}

std::shared_ptr<const AptFile> AptLoader::loadMovie(const std::string &movieName, std::string *error)
{
	std::string key = lowerAscii(movieName);
	auto it = m_movies.find(key);
	if (it != m_movies.end())
	{
		return it->second;
	}
	std::vector<std::uint8_t> constBytes;
	auto aptBytes = std::make_shared<std::vector<std::uint8_t>>();
	std::string err;
	if (!m_source.readFile(movieName + ".const", constBytes, &err))
	{
		if (error)
		{
			*error = "movie " + movieName + ": " + err;
		}
		return nullptr;
	}
	if (!m_source.readFile(movieName + ".apt", *aptBytes, &err))
	{
		if (error)
		{
			*error = "movie " + movieName + ": " + err;
		}
		return nullptr;
	}
	AptConstFile consts;
	if (!AptConstFile::parse(constBytes, consts, &err))
	{
		if (error)
		{
			*error = "movie " + movieName + " " + err;
		}
		return nullptr;
	}
	auto file = std::make_shared<AptFile>();
	if (!AptFile::parse(movieName, aptBytes, consts, *file, &err))
	{
		if (error)
		{
			*error = err;
		}
		return nullptr;
	}
	m_movies[key] = file;
	return file;
}

bool AptLoader::resolveImport(const AptFile &importer, const AptImport &import, AptResolvedImport &out, std::string *error)
{
	std::string label = importer.name + " imports " + import.movie + "::" + import.name;
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = label + ": " + why;
		}
		return false;
	};

	// The importer's destination slot must exist and be a null placeholder (an imported character occupies
	// a null slot of the importing movie: AptLevel0 has 4 imports and 4 null slots; spec menus-apt.md).
	if (import.characterId >= importer.characters.size())
	{
		return fail("destination character slot " + std::to_string(import.characterId) + " is outside the importer's " +
			std::to_string(importer.characters.size()) + " characters");
	}
	if (importer.characters[import.characterId].type != APT_CHAR_NULL)
	{
		return fail("destination character slot " + std::to_string(import.characterId) + " is not a null placeholder");
	}

	std::string err;
	std::shared_ptr<const AptFile> current = loadMovie(import.movie, &err);
	if (!current)
	{
		return fail(err);
	}
	std::string exportName = import.name;
	std::set<std::pair<std::string, std::uint32_t>> visited;
	for (;;)
	{
		std::uint32_t id = 0;
		bool ambiguous = false;
		if (!current->findExport(exportName, id, &ambiguous))
		{
			return fail(current->name + " does not export '" + exportName + "'");
		}
		if (ambiguous)
		{
			return fail("the export name '" + exportName + "' in " + current->name + " maps to several different characters");
		}
		if (id >= current->characters.size())
		{
			return fail("export '" + exportName + "' of " + current->name + " names character id " + std::to_string(id) + ", which is out of range");
		}
		if (current->characters[id].type != APT_CHAR_NULL)
		{
			out.movie = current;
			out.characterId = id;
			out.localSlot = import.characterId;
			return true;
		}
		// the export names a null slot: it is itself an import (a re-export); follow that import
		if (!visited.insert({ lowerAscii(current->name), id }).second)
		{
			return fail("import cycle through " + current->name + " character " + std::to_string(id));
		}
		const AptImport *next = nullptr;
		for (const AptImport &candidate : current->imports)
		{
			if (candidate.characterId == id)
			{
				next = &candidate;
				break;
			}
		}
		if (!next)
		{
			return fail("export '" + exportName + "' of " + current->name + " resolves to a null character slot with no import behind it");
		}
		std::shared_ptr<const AptFile> forward = loadMovie(next->movie, &err);
		if (!forward)
		{
			return fail(err);
		}
		current = forward;
		exportName = next->name;
	}
}

bool AptLoader::resolveImports(const AptFile &importer, std::vector<AptResolvedImport> &out, std::string *error)
{
	out.clear();
	for (const AptImport &imp : importer.imports)
	{
		AptResolvedImport r;
		if (!resolveImport(importer, imp, r, error))
		{
			return false;
		}
		out.push_back(std::move(r));
	}
	return true;
}

bool AptLoader::loadImageMap(const std::string &movieName, AptImageMap &out, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	std::string err;
	if (!m_source.readFile(movieName + ".dat", bytes, &err) || !AptImageMap::parse(bytes, out, &err))
	{
		if (error)
		{
			*error = movieName + ".dat: " + err;
		}
		return false;
	}
	return true;
}

bool AptLoader::loadGeometry(const std::string &movieName, std::uint32_t geometryId, AptGeometry &out, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	std::string err;
	std::string path = movieName + "_geometry/" + std::to_string(geometryId) + ".ru";
	if (!m_source.readFile(path, bytes, &err) || !AptGeometry::parse(bytes, out, &err))
	{
		if (error)
		{
			*error = path + ": " + err;
		}
		return false;
	}
	return true;
}

bool AptLoader::movieNameFromSwf(const std::string &url, std::string &movieName)
{
	if (url.size() < 5)
	{
		return false;
	}
	std::string tail = lowerAscii(url.substr(url.size() - 4));
	if (tail != ".swf")
	{
		return false;
	}
	movieName = url.substr(0, url.size() - 4);
	return true;
}
