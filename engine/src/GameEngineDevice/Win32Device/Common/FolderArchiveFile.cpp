// OpenBFME. GPL-3.0. See FolderArchiveFile.h (lane MODS-1).

#include "GameEngineDevice/Win32Device/Common/FolderArchiveFile.h"

#include "Common/AsciiString.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace
{
bool isProgram(const std::string &lowerName)
{
	for (const char *ext : { ".exe", ".dll", ".so", ".dylib", ".bat", ".cmd", ".sh", ".com", ".scr" })
	{
		const std::string e = ext;
		if (lowerName.size() >= e.size() && lowerName.compare(lowerName.size() - e.size(), e.size(), e) == 0)
			return true;
	}
	return false;
}
} // namespace

std::string FolderArchiveFile::key(const std::string &path)
{
	std::string k = path;
	std::replace(k.begin(), k.end(), '/', '\\');
	AsciiStringUtil::toLower(k);
	return k;
}

bool FolderArchiveFile::open(const std::string &root, std::string *error, const std::vector<std::string> &skipExtensions)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path base = fs::u8path(root);
	if (!fs::is_directory(base, ec))
	{
		if (error)
			*error = "mod folder " + root + " is not a folder";
		return false;
	}
	m_root = root;
	m_name = base.filename().u8string();
	std::vector<std::pair<std::string, std::string>> bigs; // relative key, disk path
	for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
	{
		if (ec)
		{
			if (error)
				*error = "mod folder " + root + ": " + ec.message();
			return false;
		}
		const fs::path &p = it->path();
		const std::string leaf = p.filename().u8string();
		if (!leaf.empty() && leaf[0] == '.')
		{
			if (it->is_directory(ec))
				it.disable_recursion_pending(); // .git, .DS_Store folders ...
			continue;
		}
		if (!it->is_regular_file(ec))
			continue;
		const std::string rel = key(fs::relative(p, base, ec).generic_u8string());
		if (ec || rel.empty())
			continue;
		if (isProgram(rel))
		{
			++m_skippedPrograms; // never mounted (a mod may ship its own game executable)
			continue;
		}
		bool skip = false;
		for (const std::string &ext : skipExtensions)
			skip = skip || (rel.size() > ext.size() && rel.compare(rel.size() - ext.size(), ext.size(), ext) == 0);
		if (skip)
		{
			++m_skippedByExtension;
			continue;
		}
		if (rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".big") == 0)
		{
			bigs.emplace_back(rel, p.u8string());
			continue;
		}
		m_disk[rel] = p.u8string();
		ArchivedFileInfo info;
		const size_t sep = rel.find_last_of('\\');
		info.m_filename = sep == std::string::npos ? rel : rel.substr(sep + 1);
		info.m_archiveFilename = m_root;
		info.m_size = (std::uint32_t)std::min<std::uintmax_t>(it->file_size(ec), 0xFFFFFFFFu);
		addFile(sep == std::string::npos ? std::string() : rel.substr(0, sep + 1), &info);
	}
	std::sort(bigs.begin(), bigs.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
	for (const auto &b : bigs)
		m_bigs.push_back(b.second);
	return true;
}

bool FolderArchiveFile::readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error)
{
	const auto it = m_disk.find(key(filename));
	if (it == m_disk.end())
	{
		if (error)
			*error = "file " + filename + " is not in mod folder " + m_root;
		return false;
	}
	std::ifstream in(std::filesystem::u8path(it->second), std::ios::binary);
	if (!in)
	{
		if (error)
			*error = "mod file " + it->second + " cannot be read";
		return false;
	}
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

void MemoryArchiveFile::put(const std::string &path, std::vector<std::uint8_t> bytes)
{
	const std::string k = FolderArchiveFile::key(path);
	ArchivedFileInfo info;
	const size_t sep = k.find_last_of('\\');
	info.m_filename = sep == std::string::npos ? k : k.substr(sep + 1);
	info.m_archiveFilename = m_name;
	info.m_size = (std::uint32_t)bytes.size();
	addFile(sep == std::string::npos ? std::string() : k.substr(0, sep + 1), &info);
	m_files[k] = std::move(bytes);
}

bool MemoryArchiveFile::readFile(const std::string &filename, std::vector<std::uint8_t> &out, std::string *error)
{
	const auto it = m_files.find(FolderArchiveFile::key(filename));
	if (it == m_files.end())
	{
		if (error)
			*error = "file " + filename + " is not in " + m_name;
		return false;
	}
	out = it->second;
	return true;
}

namespace
{
std::string trimmed(const std::string &line)
{
	size_t a = 0, b = line.size();
	while (a < b && (line[a] == ' ' || line[a] == '\t' || line[a] == '\r'))
		++a;
	while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r'))
		--b;
	return line.substr(a, b - a);
}
// the first token of a line, before a ';' comment
std::string firstToken(const std::string &line)
{
	std::string t = trimmed(line.substr(0, line.find(';')));
	const size_t sp = t.find_first_of(" \t");
	return sp == std::string::npos ? t : t.substr(0, sp);
}
} // namespace

std::string GameDataBlockRows(const std::string &text, std::string *outside)
{
	std::string rows, rest, line;
	bool in = false;
	size_t start = 0;
	while (start <= text.size())
	{
		const size_t nl = text.find('\n', start);
		line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
		start = nl == std::string::npos ? text.size() + 1 : nl + 1;
		const std::string tok = firstToken(line);
		if (!in && AsciiStringUtil::compareNoCase(tok, "GameData") == 0)
		{
			in = true;
			continue;
		}
		if (in && AsciiStringUtil::compareNoCase(tok, "End") == 0)
		{
			in = false;
			continue;
		}
		(in ? rows : rest) += line + "\n";
	}
	if (outside)
		*outside = rest;
	return rows;
}

std::string MergeGameDataBlocks(const std::string &gameDataIni, const std::vector<std::pair<std::string, std::string>> &objectFiles)
{
	std::string outside;
	std::string merged = GameDataBlockRows(gameDataIni, &outside);
	for (const auto &f : objectFiles)
		merged += "; OpenBFME: the GameData rows of " + f.first + "\n" + GameDataBlockRows(f.second);
	return outside + "\nGameData\n" + merged + "End\n";
}
