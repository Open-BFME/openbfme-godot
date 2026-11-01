// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH GameEngine/Source/Common/System/ArchiveFileSystem.cpp.

#include "Common/ArchiveFileSystem.h"
#include "Common/PathIO.h"
#include "Common/ArchiveFile.h"
#include "Common/AsciiString.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

using AsciiStringUtil::nextToken;

namespace
{
// lane MOD-5: a path as the local folder compares it (ASCII lower case, '/' for '\')
std::string comparablePath(const std::string &path)
{
	std::string out = AsciiStringUtil::lowered(path);
	std::replace(out.begin(), out.end(), '\\', '/');
	return out;
}
} // namespace

namespace fs = std::filesystem;

// Lane MODS-1 (see ArchiveFileSystem.h): "<modDir>\\<file>" as Windows resolves it below the mod folder.
std::string ModLooseIndex::key(const std::string &path)
{
	std::string lowered = AsciiStringUtil::lowered(path);
	std::vector<std::string> parts;
	std::string token;
	size_t i = 0;
	while (i <= lowered.size())
	{
		if (i == lowered.size() || lowered[i] == '\\' || lowered[i] == '/')
		{
			if (token == "..")
			{
				if (parts.empty())
				{
					return std::string(); // above the mod folder: not a file of the mod
				}
				parts.pop_back();
			}
			else if (!token.empty() && token != ".")
			{
				parts.push_back(token);
			}
			token.clear();
		}
		else
		{
			token += lowered[i];
		}
		++i;
	}
	std::string out;
	for (const std::string &p : parts)
	{
		if (!out.empty())
		{
			out += '\\';
		}
		out += p;
	}
	return out;
}

const ModLooseFile *ModLooseIndex::find(const std::string &path) const
{
	if (byKey.empty())
	{
		return nullptr;
	}
	std::map<std::string, size_t>::const_iterator it = byKey.find(key(path));
	return it == byKey.end() ? nullptr : &files[it->second];
}

void ArchiveFileSystem::setModLooseLayer(ModLooseIndex index)
{
	m_modLoose = std::move(index);
	m_preferLocalFiles = true; // RW 0xA14313: strcpy(0xDEC498, modDir); the flag RW 0xDEC490 = 1
}

void ArchiveFileSystem::setDerivedFile(const std::string &filename, std::vector<std::uint8_t> bytes, const std::string &origin, std::vector<std::string> sources)
{
	m_derived[ModLooseIndex::key(filename)] = DerivedFile{ std::move(bytes), origin, std::move(sources) };
}

void ArchiveFileSystem::clearDerivedFile(const std::string &filename)
{
	m_derived.erase(ModLooseIndex::key(filename));
}

std::vector<std::string> ArchiveFileSystem::derivedSources(const std::string &filename) const
{
	const DerivedFile *d = derived(filename);
	return d ? d->sources : std::vector<std::string>();
}

const ArchiveFileSystem::DerivedFile *ArchiveFileSystem::derived(const std::string &filename) const
{
	if (m_derived.empty())
	{
		return nullptr;
	}
	auto it = m_derived.find(ModLooseIndex::key(filename));
	return it == m_derived.end() ? nullptr : &it->second;
}

std::vector<std::string> ArchiveFileSystem::sourcesOf(const std::string &filename) const
{
	std::vector<std::string> out;
	if (const DerivedFile *d = derived(filename))
	{
		out.push_back("derived:" + d->origin);
	}
	if (m_preferLocalFiles)
	{
		if (const ModLooseFile *f = m_modLoose.find(filename))
		{
			out.push_back("mod-folder:" + f->virtualPath);
		}
	}
	const std::string owner = getArchiveFilenameForFile(filename);
	if (!owner.empty())
	{
		out.push_back(owner);
	}
	for (const auto &entry : m_archiveFileMap)
	{
		if (entry.first != owner && entry.second && entry.second->getArchivedFileInfo(filename))
		{
			out.push_back(entry.first);
		}
	}
	return out;
}

bool FilenameLessNoCase::operator()(const std::string &a, const std::string &b) const
{
	return AsciiStringUtil::compareNoCase(a, b) < 0;
}

ArchiveFileSystem::ArchiveFileSystem() = default;
ArchiveFileSystem::~ArchiveFileSystem() = default;

void ArchiveFileSystem::mountArchive(std::unique_ptr<ArchiveFile> archiveFile, const std::string &archiveFilename, bool overwrite)
{
	loadIntoDirectoryTree(archiveFile.get(), archiveFilename, overwrite);
	m_archiveFileMap[archiveFilename] = std::move(archiveFile);
}

// OpenBFME (S-012, docs/STOPS.md): runtime report, not a print. One line per archive whose identity
// the platform cannot enforce; empty means every mounted archive is protected.
std::vector<std::string> ArchiveFileSystem::unverifiedIdentity() const
{
	std::vector<std::string> report;
	for (const auto &entry : m_archiveFileMap)
	{
		if (!entry.second->isIdentityEnforced())
		{
			report.push_back("S-012: archive identity unverified (the file can be overwritten in place while mounted): " + entry.first);
		}
	}
	return report;
}

void ArchiveFileSystem::loadIntoDirectoryTree(const ArchiveFile *archiveFile, const std::string &archiveFilename, bool overwrite)
{
	FilenameList filenameList;
	archiveFile->getFileListInDirectory(std::string(""), std::string(""), std::string("*"), filenameList, true);

	for (const std::string &listed : filenameList)
	{
		std::string path = AsciiStringUtil::lowered(listed);
		std::string token;

		ArchivedDirectoryInfo *dirInfo = &m_rootDirectory;

		bool infoInPath = nextToken(path, &token, "\\/");

		while (infoInPath && ((token.find('.') == std::string::npos) || (path.find('.') != std::string::npos)))
		{
			ArchivedDirectoryInfoMap::iterator tempiter = dirInfo->m_directories.find(token);
			if (tempiter == dirInfo->m_directories.end())
			{
				dirInfo->m_directories[token].clear();
				dirInfo->m_directories[token].m_directoryName = token;
			}

			dirInfo = &(dirInfo->m_directories[token]);
			infoInPath = nextToken(path, &token, "\\/");
		}

		// token is the filename, and dirInfo is the directory that this file is in.
		// overwrite == false: the first archive to supply a path keeps it.
		if (dirInfo->m_files.find(token) == dirInfo->m_files.end() || overwrite)
		{
			dirInfo->m_files[token] = archiveFilename;
		}
	}
}

std::string ArchiveFileSystem::getArchiveFilenameForFile(const std::string &filename) const
{
	std::string path = AsciiStringUtil::lowered(filename);
	std::string token;

	const ArchivedDirectoryInfo *dirInfo = &m_rootDirectory;

	nextToken(path, &token, "\\/");

	while ((token.find('.') == std::string::npos) || (path.find('.') != std::string::npos))
	{
		ArchivedDirectoryInfoMap::const_iterator it = dirInfo->m_directories.find(token);
		if (it == dirInfo->m_directories.end())
		{
			return std::string();
		}
		dirInfo = &it->second;
		if (!nextToken(path, &token, "\\/"))
		{
			return std::string();
		}
	}

	ArchivedFileLocationMap::const_iterator it = dirInfo->m_files.find(token);
	if (it == dirInfo->m_files.end())
	{
		return std::string();
	}
	return it->second;
}

bool ArchiveFileSystem::doesFileExist(const std::string &filename) const
{
	if (m_readTrace)
	{
		trace("exists", filename); // lane PROFILE-3
	}
	// RW 0xA14AEB: archives, then the local file system; with the flag the mod folder answers too (the order does not matter for a yes / no)
	if ((m_preferLocalFiles && m_modLoose.find(filename)) || derived(filename))
	{
		return true;
	}
	// lane MOD-5: or the local folder's file (the user maps folder)
	return !getArchiveFilenameForFile(filename).empty() || !findLocalFile(filename).empty();
}

void ArchiveFileSystem::setLocalFolder(const std::string &folder)
{
	m_localFolder = folder;
	while (!m_localFolder.empty() && (m_localFolder.back() == '\\' || m_localFolder.back() == '/'))
	{
		m_localFolder.pop_back();
	}
}

std::string ArchiveFileSystem::findLocalFile(const std::string &filename) const
{
	namespace fs = std::filesystem;
	if (m_localFolder.empty())
	{
		return std::string();
	}
	const std::string folder = comparablePath(m_localFolder) + "/";
	if (filename.size() <= folder.size() || comparablePath(filename).compare(0, folder.size(), folder) != 0)
	{
		return std::string();
	}
	fs::path at = PathIO::native(m_localFolder);
	std::error_code ec;
	std::string rest = filename.substr(folder.size());
	std::replace(rest.begin(), rest.end(), '\\', '/');
	std::string part;
	std::size_t start = 0;
	while (start <= rest.size())
	{
		const std::size_t end = std::min(rest.find('/', start), rest.size());
		part = rest.substr(start, end - start);
		start = end + 1;
		if (part.empty())
		{
			continue;
		}
		// nothing outside the folder: no parent step, and no part with a ':' (a drive, "D:", would replace the whole path when joined on
		// Windows; "x.map:s" names an alternate data stream)
		if (part == "." || part == ".." || part.find(':') != std::string::npos)
		{
			return std::string();
		}
		fs::path next = at / fs::u8path(part);
		if (!fs::exists(next, ec))
		{
			// Windows opens a path without regard to case; elsewhere the folder's entry that matches it without case
			next.clear();
			for (fs::directory_iterator it(at, ec), last; !ec && it != last; it.increment(ec))
			{
				if (AsciiStringUtil::compareNoCase(it->path().filename().u8string(), part) == 0)
				{
					next = it->path();
					break;
				}
			}
			if (next.empty())
			{
				return std::string();
			}
		}
		at = next;
	}
	return fs::is_regular_file(at, ec) ? at.u8string() : std::string();
}

void ArchiveFileSystem::trace(const char *what, const std::string &path) const
{
	std::string key = std::string(what) + " ";
	for (char c : path)
	{
		key += c == '/' ? '\\' : (char)std::tolower((unsigned char)c);
	}
	static std::mutex lock; // the peer's logic worker may read beside the main thread
	const std::lock_guard<std::mutex> hold(lock);
	m_readTrace->insert(key);
}

bool ArchiveFileSystem::readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error)
{
	if (m_readTrace)
	{
		trace("read", filename); // lane PROFILE-3
	}
	if (const DerivedFile *d = derived(filename))
	{
		out = d->bytes;
		return true;
	}
	// RW 0xA149A2: with the flag, "<modDir>\\<file>" first (the two install-folder lookups are empty by construction: ArchiveFileSystem.h)
	if (m_preferLocalFiles)
	{
		if (const ModLooseFile *f = m_modLoose.find(filename))
		{
			std::error_code ec;
			const fs::path disk = fs::u8path(f->diskPath);
			const std::uint64_t size = (std::uint64_t)fs::file_size(disk, ec);
			const long long mtime = ec ? 0 : (long long)fs::last_write_time(disk, ec).time_since_epoch().count();
			if (ec || size != f->size || mtime != f->mtime)
			{
				if (error)
				{
					*error = "mod file changed or vanished since the mod was mounted (mount again): " + f->virtualPath;
				}
				return false;
			}
			std::ifstream in(disk, std::ios::binary);
			out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			if (!in.good() && !in.eof())
			{
				if (error)
				{
					*error = "cannot read mod file " + f->virtualPath;
				}
				return false;
			}
			if (out.size() != f->size)
			{
				if (error)
				{
					*error = "mod file changed while it was read: " + f->virtualPath;
				}
				return false;
			}
			return true;
		}
	}
	std::string archiveFilename = getArchiveFilenameForFile(filename);
	if (archiveFilename.empty())
	{
		// lane MOD-5: RW 0xA149A2 asks TheLocalFileSystem when no archive holds the file; here only the local folder's files
		const std::string local = findLocalFile(filename);
		if (!local.empty())
		{
			std::ifstream in(PathIO::native(local), std::ios::binary);
			if (!in)
			{
				if (error)
				{
					*error = "cannot read " + local;
				}
				return false;
			}
			out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			return true;
		}
		if (error && m_localFolder.empty())
		{
			*error = "file not found in any mounted archive: " + filename;
		}
		else if (error)
		{
			*error = "file not found in any mounted archive or in " + m_localFolder + ": " + filename;
		}
		return false;
	}
	ArchiveFileMap::iterator it = m_archiveFileMap.find(archiveFilename);
	if (it == m_archiveFileMap.end() || !it->second)
	{
		if (error)
		{
			*error = "directory tree names unmounted archive " + archiveFilename + " for " + filename;
		}
		return false;
	}
	return it->second->readFile(filename, out, error);
}

void ArchiveFileSystem::getFileListInDirectory(const std::string &currentDirectory, const std::string &originalDirectory,
	const std::string &searchName, FilenameList &filenameList, bool searchSubdirectories) const
{
	if (m_readTrace)
	{
		trace("list", originalDirectory + searchName); // lane PROFILE-3
	}
	// RW 0xA14D2B: with the flag, the mod folder's files below the directory join the union first (the set keeps the first spelling of a name)
	if (m_preferLocalFiles && !m_modLoose.files.empty())
	{
		std::string dirKey = ModLooseIndex::key(originalDirectory);
		const std::string pattern = AsciiStringUtil::lowered(searchName);
		for (const ModLooseFile &f : m_modLoose.files)
		{
			const std::string k = ModLooseIndex::key(f.virtualPath);
			if (!dirKey.empty() && (k.size() <= dirKey.size() || k.compare(0, dirKey.size(), dirKey) != 0 || k[dirKey.size()] != '\\'))
			{
				continue;
			}
			const std::string rest = dirKey.empty() ? k : k.substr(dirKey.size() + 1);
			const size_t slash = rest.rfind('\\');
			if (!searchSubdirectories && slash != std::string::npos)
			{
				continue;
			}
			// the archive listing matches the stored (lower-case) entry name; Windows' FindFirstFile matches without case: lower both
			if (!SearchStringMatches(slash == std::string::npos ? rest : rest.substr(slash + 1), pattern))
			{
				continue;
			}
			std::string listed = originalDirectory;
			if (!listed.empty() && listed.back() != '\\' && listed.back() != '/')
			{
				listed += '\\';
			}
			listed += f.virtualPath.substr(f.virtualPath.size() - rest.size());
			filenameList.insert(listed);
		}
	}
	for (const auto &entry : m_archiveFileMap)
	{
		entry.second->getFileListInDirectory(currentDirectory, originalDirectory, searchName, filenameList, searchSubdirectories);
	}
}
