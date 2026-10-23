// OpenBFME. GPL-3.0.
// See Common/ModMount.h (lane MODS-1): RotWK's -mod switch.

#include "Common/ModMount.h"

#include "Common/ArchiveFile.h"
#include "Common/AsciiString.h"
#include "Common/JobSystem.h"
#include "Common/MD5.h"
#include "Common/Md5Cache.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

const char *const kRetailInstallLanguage = "English"; // RW 0xDBACD0

namespace
{

bool endsWithSeparator(const std::string &s)
{
	return !s.empty() && (s.back() == '\\' || s.back() == '/');
}

std::string lowerExtension(const fs::path &p)
{
	return AsciiStringUtil::lowered(p.extension().u8string());
}

long long mtimeOf(const fs::path &p, std::error_code &ec)
{
	return (long long)fs::last_write_time(p, ec).time_since_epoch().count();
}

struct HashJob
{
	std::string disk;  // absolute path
	std::uint64_t size = 0;
	long long mtime = 0;
	std::string key;   // Md5Cache key
	std::string md5, error;
	bool cached = false;
};

// the md5 of every job: cache hits first (path + size + mtime unchanged), the rest on the client job pool (as the retail mount, lane PERF-2)
void hashJobs(std::vector<HashJob> &jobs, Md5Cache &cache, ModMountReport &report)
{
	std::vector<size_t> todo;
	std::map<std::string, size_t> firstOfKey; // a folder's archive is also one of its loose files: hash it once
	std::vector<std::pair<size_t, size_t>> copies;
	for (size_t i = 0; i < jobs.size(); ++i)
	{
		auto it = cache.entries.find(jobs[i].key);
		if (it != cache.entries.end())
		{
			jobs[i].md5 = it->second;
			jobs[i].cached = true;
			++report.md5CacheHits;
		}
		else if (firstOfKey.count(jobs[i].key))
		{
			copies.emplace_back(i, firstOfKey[jobs[i].key]);
		}
		else
		{
			firstOfKey[jobs[i].key] = i;
			todo.push_back(i);
		}
	}
	JobSystem::client().parallelFor(todo.size(), 1, [&jobs, &todo](size_t, size_t begin, size_t end) {
		for (size_t t = begin; t < end; ++t)
		{
			HashJob &j = jobs[todo[t]];
			j.md5 = MD5::ofFile(j.disk, &j.error);
		}
	});
	for (size_t i : todo)
	{
		if (jobs[i].md5.empty())
		{
			report.errors.push_back("mod: could not hash " + jobs[i].disk + ": " + jobs[i].error);
			continue;
		}
		++report.filesHashed;
		cache.entries[jobs[i].key] = jobs[i].md5;
		cache.dirty = true;
	}
	for (const auto &c : copies)
	{
		jobs[c.first].md5 = jobs[c.second].md5;
	}
}

// fills size / mtime / key of a job; false + an error when the file cannot be read
bool statJob(const fs::path &p, HashJob &j, ModMountReport &report)
{
	std::error_code ec;
	const fs::path absolute = fs::absolute(p, ec);
	j.disk = (ec ? p : absolute).u8string();
	j.size = (std::uint64_t)fs::file_size(p, ec);
	if (!ec)
	{
		j.mtime = mtimeOf(p, ec);
	}
	if (ec)
	{
		report.errors.push_back("mod: could not read " + p.u8string() + ": " + ec.message());
		return false;
	}
	j.key = Md5Cache::key(j.disk, j.size, j.mtime);
	return true;
}

// PLAN rule 7 under -mod: RW 0xA149A2's lookups 2 and 3 ("lang\<language>\<file>", "<file>" in the game folder) come before the archives.
// Every loose file of the RotWK install that one of them would return for a name the archives hold is contamination: one error each.
void checkInstallShadows(const ArchiveFileSystem &fileSystem, const std::string &installRoot, const fs::path &modFolder, ModMountReport &report)
{
	if (installRoot.empty())
	{
		report.errors.push_back("-mod: the RotWK install folder is not known, so its loose files cannot be checked (PLAN rule 7)");
		return;
	}
	std::error_code ec;
	const fs::path root = fs::u8path(installRoot);
	const std::string langPrefix = std::string("lang\\") + AsciiStringUtil::lowered(kRetailInstallLanguage) + "\\";
	std::error_code modEc;
	const fs::path modCanonical = modFolder.empty() ? fs::path() : fs::weakly_canonical(modFolder, modEc);
	std::vector<std::string> found;
	for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
	{
		std::error_code typeEc;
		if (it->is_symlink(typeEc))
		{
			it.disable_recursion_pending(); // never followed (a loop would never end); the retail mount reads no links either
			continue;
		}
		if (it->is_directory(typeEc) && !modCanonical.empty())
		{
			std::error_code c;
			if (fs::weakly_canonical(it->path(), c) == modCanonical)
			{
				it.disable_recursion_pending(); // a mod folder inside the install is the mod, not the install's loose files
			}
			continue;
		}
		if (!it->is_regular_file(typeEc) || lowerExtension(it->path()) == ".big")
		{
			continue; // archives are the policy's business (RetailArchivePolicy)
		}
		std::string rel = AsciiStringUtil::lowered(it->path().lexically_relative(root).u8string());
		std::replace(rel.begin(), rel.end(), '/', '\\');
		if (fileSystem.doesFileExist(rel))
		{
			found.push_back(rel + " (would replace the archives' " + rel + ")");
		}
		else if (rel.compare(0, langPrefix.size(), langPrefix) == 0 && fileSystem.doesFileExist(rel.substr(langPrefix.size())))
		{
			found.push_back(rel + " (would replace the archives' " + rel.substr(langPrefix.size()) + ")");
		}
	}
	if (ec)
	{
		report.errors.push_back("-mod: could not list the RotWK install " + installRoot + ": " + ec.message());
		return;
	}
	std::sort(found.begin(), found.end());
	for (const std::string &f : found)
	{
		report.errors.push_back("-mod: a loose file in the RotWK install folder would override the game's archives while a mod is loaded (retail reads it, "
			"RW 0xA149A2); it is not part of the game: move it into the mod folder or delete it: " + f);
	}
}

// the first token of an INI line, comments (';' and "//") stripped
std::string firstToken(const std::string &line)
{
	size_t a = 0;
	while (a < line.size() && (line[a] == ' ' || line[a] == '\t'))
	{
		++a;
	}
	size_t b = a;
	while (b < line.size() && line[b] != ' ' && line[b] != '\t' && line[b] != ';' && line[b] != '=' && !(line[b] == '/' && b + 1 < line.size() && line[b + 1] == '/'))
	{
		++b;
	}
	return line.substr(a, b - a);
}

std::vector<std::string> splitLines(const std::vector<std::uint8_t> &bytes)
{
	std::vector<std::string> lines;
	std::string cur;
	for (std::uint8_t c : bytes)
	{
		if (c == '\n')
		{
			lines.push_back(cur);
			cur.clear();
		}
		else if (c != '\r')
		{
			cur.push_back((char)c);
		}
	}
	if (!cur.empty())
	{
		lines.push_back(cur);
	}
	return lines;
}

// Lane MODS-3: a GameData block in another INI file of the mod (RJ-RotWK keeps its whole GameData block in data\ini\object\gamedata.ini). Retail's
// INI load parses any block in any file of the Object folder (one block table, INI::load), so that block sets TheWritableGlobalData after
// data\ini\gamedata.ini's; OpenBFME's GameData readers read data\ini\gamedata.ini, so the mount derives that file: its own text with the other
// blocks' lines (and their files' #define lines, which retail keeps game-wide) added to its GameData block in the Object folder's load order (the
// FilenameList set, RW 0xA14D2B). Only the mod's own files are read (retail's Object folder has no GameData block).
bool mergeModGameData(ArchiveFileSystem &fileSystem, ModMountReport &report)
{
	std::set<std::string> modArchives;
	for (const MountedArchive &a : report.archives)
	{
		modArchives.insert(a.diskPath);
	}
	const char *target = "data\\ini\\gamedata.ini";
	// a mod mounted over an install-folder mod that already has a derived file (Patch 2.02 under Battle for Tamriel): that file is derived again from its
	// sources and this mod's, over the real data\\ini\\gamedata.ini that now wins (the mod's own, if it has one)
	std::set<std::string, FilenameLessNoCase> earlier;
	for (const std::string &f : fileSystem.derivedSources(target))
	{
		earlier.insert(f);
	}
	fileSystem.clearDerivedFile(target);
	FilenameList files;
	fileSystem.getFileListInDirectory("data\\ini\\object\\", "data\\ini\\object\\", "*.ini", files, true);
	std::vector<std::string> defines, body, from;
	for (const std::string &f : files)
	{
		const bool loose = fileSystem.preferLocalFiles() && fileSystem.modLooseLayer().find(f);
		if (!loose && !modArchives.count(fileSystem.getArchiveFilenameForFile(f)) && !earlier.count(f))
		{
			continue;
		}
		std::vector<std::uint8_t> bytes;
		std::string error;
		if (!fileSystem.readFile(f, bytes, &error))
		{
			report.errors.push_back("GameData merge: " + error);
			return false;
		}
		bool inBlock = false, found = false;
		std::vector<std::string> fileDefines;
		for (const std::string &line : splitLines(bytes))
		{
			const std::string t = firstToken(line);
			if (!inBlock)
			{
				if (AsciiStringUtil::compareNoCase(t, "#define") == 0)
				{
					fileDefines.push_back(line);
				}
				else if (AsciiStringUtil::compareNoCase(t, "GameData") == 0)
				{
					inBlock = found = true;
				}
				continue;
			}
			if (AsciiStringUtil::compareNoCase(t, "End") == 0)
			{
				inBlock = false;
				continue;
			}
			body.push_back(line);
		}
		if (found)
		{
			defines.insert(defines.end(), fileDefines.begin(), fileDefines.end());
			from.push_back(f);
		}
	}
	if (from.empty())
	{
		return true;
	}
	std::vector<std::uint8_t> base;
	std::string error;
	if (!fileSystem.readFile(target, base, &error))
	{
		report.errors.push_back("GameData merge: " + error);
		return false;
	}
	std::vector<std::string> lines = splitLines(base), out = defines;
	bool inBlock = false, merged = false;
	for (const std::string &line : lines)
	{
		const std::string t = firstToken(line);
		if (!merged && !inBlock && AsciiStringUtil::compareNoCase(t, "GameData") == 0)
		{
			inBlock = true;
		}
		else if (inBlock && AsciiStringUtil::compareNoCase(t, "End") == 0)
		{
			out.insert(out.end(), body.begin(), body.end()); // the later blocks' fields after its own, as retail parses them later
			inBlock = false;
			merged = true;
		}
		out.push_back(line);
	}
	if (!merged)
	{
		out.push_back("GameData");
		out.insert(out.end(), body.begin(), body.end());
		out.push_back("End");
	}
	std::string text;
	for (const std::string &l : out)
	{
		text += l + "\r\n";
	}
	std::string names;
	for (const std::string &f : from)
	{
		names += (names.empty() ? "" : ", ") + f;
	}
	fileSystem.setDerivedFile(target, std::vector<std::uint8_t>(text.begin(), text.end()), "GameData of " + names, from);
	report.notes.push_back("the GameData block of " + names + " is merged into data\\ini\\gamedata.ini (retail's INI load applies it after that file's)");
	return true;
}
} // namespace

std::vector<std::string> ModFolderArchiveOrder(std::vector<std::string> names)
{
	// RW 0xA17FB0 walks the FilenameList set from its leftmost node (STLport: header + 8) to the end; the set's comparator is RW 0x42C33B,
	// AsciiString::compareNoCase (msvcr71 _memicmp, a shorter name first on a tie): ascending without case
	std::stable_sort(names.begin(), names.end(), FilenameLessNoCase());
	names.erase(std::unique(names.begin(), names.end(), [](const std::string &a, const std::string &b) { return AsciiStringUtil::compareNoCase(a, b) == 0; }),
		names.end());
	return names;
}

std::string ModMountStopLine()
{
	return "S-5300: -mod follows RotWK 2.01 (RW 0x7BADB9 / 0x7BAA44 / 0xA17FB0 / 0xA149A2); a mod folder's archives mount in case-insensitive name order, "
		   "the last one winning (FilenameList comparator RW 0x42C33B); the mod folder is frozen at mount; a missing path or a bad "
		   "archive is an error (retail ignores both); install loose files that would shadow an archive are refused";
}

bool ParseModArgument(const std::string &path, const std::string &userDataDir, ModOperand &io, std::string *error)
{
	// RW 0x7BAE11 .. 0x7BAE8E: "no ':' and starts with neither '/' nor '\\'" = below the user data folder (RW 0x6427C8, GlobalData + 0x1244)
	std::string modPath = path;
	const bool absolute = modPath.find(':') != std::string::npos || (!modPath.empty() && (modPath[0] == '/' || modPath[0] == '\\'));
	if (!absolute)
	{
		if (userDataDir.empty())
		{
			if (error)
			{
				*error = "-mod '" + path + "': a relative mod path is taken below the user data folder, which is not known";
			}
			return false;
		}
		modPath = userDataDir + (endsWithSeparator(userDataDir) ? "" : "/") + modPath;
	}
	std::error_code ec;
	const fs::path p = fs::u8path(modPath);
	const fs::file_status st = fs::status(p, ec);
	if (path.empty() || ec || !fs::exists(st))
	{
		if (error)
		{
			*error = "-mod '" + path + "' does not exist (looked for " + modPath + ")"; // retail ignores it (RW 0x7BAEB4 / 0x7BAECF)
		}
		return false;
	}
	if (fs::is_directory(st))
	{
		if (!endsWithSeparator(modPath))
		{
			modPath += '/'; // RW 0x7BAEF1: concat('\\')
		}
		io.dir = modPath; // GlobalData + 0xD38
	}
	else
	{
		io.archive = modPath; // GlobalData + 0xD3C
	}
	return true;
}

ModMountReport MountRetailMod(Win32BIGFileSystem &fileSystem, const ModOperand &mod, const ModMountOptions &options)
{
	ModMountReport report;
	if (mod.empty())
	{
		report.errors.push_back("-mod: no mod given (a folder or an archive file is required)");
		return report;
	}
	Md5Cache cache;
	cache.path = options.md5CachePath;
	cache.load();

	struct Planned
	{
		fs::path disk;
		std::string name; // the archive's name (identity and report)
		HashJob hash;
	};
	std::vector<Planned> archives; // mount order (RW 0x7BAA44): m_modBIG first, then the folder's archives; with overwrite the last wins
	std::vector<HashJob> loose;    // the folder's loose files, by virtual path
	std::vector<std::string> looseVirtual;

	if (!mod.archive.empty())
	{
		const fs::path p = fs::u8path(mod.archive);
		std::error_code ec;
		if (!fs::is_regular_file(p, ec))
		{
			report.errors.push_back("-mod archive is not a file: '" + mod.archive + "'");
		}
		else
		{
			Planned a{ p, p.filename().u8string(), HashJob() };
			if (statJob(p, a.hash, report))
			{
				archives.push_back(a);
			}
		}
	}

	fs::path folder;
	if (!mod.dir.empty())
	{
		folder = fs::u8path(mod.dir);
		std::error_code ec;
		if (!fs::is_directory(folder, ec))
		{
			report.errors.push_back("-mod folder does not exist: '" + mod.dir + "'");
		}
		else
		{
			// RW 0xA17FB0: the folder's direct "*.BIG" children (searchSubdirectories 0; Windows matches the mask without case)
			std::vector<std::string> names;
			for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
			{
				std::error_code typeEc;
				if (!it->is_symlink(typeEc) && it->is_regular_file(typeEc) && lowerExtension(it->path()) == ".big")
				{
					names.push_back(it->path().filename().u8string());
				}
			}
			if (ec)
			{
				report.errors.push_back("-mod: could not list " + mod.dir + ": " + ec.message());
			}
			const std::vector<std::string> ordered = ModFolderArchiveOrder(names);
			if (ordered.size() != names.size())
			{
				report.errors.push_back("-mod: the folder " + mod.dir + " has two archives whose names differ only in case (Windows cannot hold both)");
			}
			for (const std::string &n : ordered)
			{
				Planned a{ folder / fs::u8path(n), n, HashJob() };
				if (statJob(a.disk, a.hash, report))
				{
					archives.push_back(a);
				}
			}
			// the loose layer (RW 0xA149A2 "<modDir>\<file>"): every regular file below the folder, the archives included (retail can open them as files)
			for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
			{
				std::error_code typeEc;
				if (it->is_symlink(typeEc))
				{
					// Sol r1: a link could reach outside the mod folder or loop; mods are plain folders (Windows has no links in them)
					report.errors.push_back("-mod: the mod folder holds a symbolic link, which is not supported: " + it->path().u8string());
					it.disable_recursion_pending();
					continue;
				}
				if (!it->is_regular_file(typeEc))
				{
					continue;
				}
				std::string rel = it->path().lexically_relative(folder).u8string();
				std::replace(rel.begin(), rel.end(), '/', '\\');
				HashJob j;
				if (statJob(it->path(), j, report))
				{
					loose.push_back(j);
					looseVirtual.push_back(rel);
				}
			}
			if (ec)
			{
				report.errors.push_back("-mod: could not walk " + mod.dir + ": " + ec.message());
			}
			std::string name = folder.filename().u8string();
			if (name.empty())
			{
				name = folder.parent_path().filename().u8string(); // "<dir>/" has an empty filename
			}
			report.folderName = name;
		}
	}
	if (!report.errors.empty())
	{
		return report;
	}

	// hash: the archives and the loose files together (one pool pass)
	std::vector<HashJob> all;
	for (const Planned &a : archives)
	{
		all.push_back(a.hash);
	}
	all.insert(all.end(), loose.begin(), loose.end());
	hashJobs(all, cache, report);
	if (!report.errors.empty())
	{
		return report;
	}
	for (size_t i = 0; i < archives.size(); ++i)
	{
		archives[i].hash = all[i];
	}
	for (size_t i = 0; i < loose.size(); ++i)
	{
		loose[i] = all[archives.size() + i];
	}

	// the loose index (byte order of the virtual path); two names that differ only in case cannot both be in a Windows folder
	ModLooseIndex index;
	index.root = mod.dir;
	{
		std::vector<size_t> order(loose.size());
		for (size_t i = 0; i < order.size(); ++i)
		{
			order[i] = i;
		}
		std::sort(order.begin(), order.end(), [&looseVirtual](size_t a, size_t b) { return looseVirtual[a] < looseVirtual[b]; });
		for (size_t i : order)
		{
			ModLooseFile f;
			f.virtualPath = looseVirtual[i];
			f.diskPath = loose[i].disk;
			f.size = loose[i].size;
			f.mtime = loose[i].mtime;
			f.md5 = loose[i].md5;
			const std::string key = ModLooseIndex::key(f.virtualPath);
			if (index.byKey.count(key))
			{
				report.errors.push_back("-mod: two files of the mod folder differ only in case (Windows cannot hold both): " + index.files[index.byKey[key]].virtualPath +
					" and " + f.virtualPath);
				continue;
			}
			index.byKey[key] = index.files.size();
			index.files.push_back(std::move(f));
		}
	}
	if (!report.errors.empty())
	{
		return report;
	}

	// open and validate every archive before the first one is mounted (as the retail mount: a failure mounts nothing)
	std::vector<std::unique_ptr<ArchiveFile>> opened;
	for (const Planned &a : archives)
	{
		std::string error;
		std::unique_ptr<ArchiveFile> archive = fileSystem.openArchiveFile(a.disk.u8string(), &error);
		if (!archive)
		{
			report.errors.push_back("mod: " + a.name + " is not a valid archive: " + error); // retail's loadBigFile skips it (RW 0xA1841F)
			continue;
		}
		opened.push_back(std::move(archive));
	}
	if (!report.errors.empty())
	{
		return report;
	}

	// PLAN rule 7 (before anything of the mod is mounted, against the retail tree only)
	checkInstallShadows(fileSystem, options.rotwkInstallRoot, folder, report);
	if (!report.errors.empty())
	{
		return report;
	}

	for (size_t i = 0; i < archives.size(); ++i)
	{
		fileSystem.mountArchive(std::move(opened[i]), archives[i].disk.u8string(), true); // loadBigFile(name, overwrite 1)
		report.archives.push_back({ "mod", archives[i].name, archives[i].disk.u8string(), archives[i].hash.size, archives[i].hash.md5 });
	}

	// identity lines (ProfileIdentity): the mount order and every byte the mod contributes; no absolute path (privacy: only names below the mod)
	if (!mod.archive.empty())
	{
		report.identity.push_back("mod archive " + archives.front().name);
	}
	if (!mod.dir.empty())
	{
		report.identity.push_back("mod folder " + report.folderName);
	}
	// Sol r1: one bounded line for the whole content. A line per file made a big folder mod's identity too long for the save header and the
	// LAN JOIN datagram. The manifest (every archive in mount order, every loose file in byte order) is the report's (ModMountReport::manifest)
	for (size_t i = 0; i < report.archives.size(); ++i)
	{
		const MountedArchive &a = report.archives[i];
		report.manifest.push_back("modarchive " + std::to_string(i) + " " + a.canonicalPath + " " + a.md5 + " " + std::to_string(a.size));
	}
	for (size_t i = 0; i < index.files.size(); ++i)
	{
		const ModLooseFile &f = index.files[i];
		report.manifest.push_back("modfile " + std::to_string(i) + " " + f.virtualPath + " " + f.md5 + " " + std::to_string(f.size));
	}
	std::string manifestText;
	for (const std::string &l : report.manifest)
	{
		manifestText += l + "\n";
	}
	report.identity.push_back("modcontent " + std::to_string(report.archives.size()) + " archives " + std::to_string(index.files.size()) + " files " +
		MD5::ofBytes(manifestText.data(), manifestText.size()));

	if (!mod.dir.empty())
	{
		report.loose = index;
		fileSystem.setModLooseLayer(std::move(index)); // RW 0xA14313: the folder and the flag
	}
	else
	{
		fileSystem.setModLooseLayer(ModLooseIndex()); // RW 0x7BADD0: parseMod sets the flag for an archive too
	}
	if (!mergeModGameData(fileSystem, report))
	{
		return report;
	}
	report.stops.push_back(ModMountStopLine());
	const std::string warning = cache.save(); // a lost cache only costs a rehash next time
	if (!warning.empty())
	{
		report.warnings.push_back(warning);
	}
	report.ok = true;
	return report;
}

ModMountReport MountInstallOverlay(Win32BIGFileSystem &out, const RetailMountReport &retail, const std::string &overlay, const ModMountOptions &options)
{
	return MountInstallOverlay(out, retail, std::vector<std::string>{ overlay }, options);
}

ModMountReport MountInstallOverlay(Win32BIGFileSystem &out, const RetailMountReport &retail, const std::vector<std::string> &overlays,
	const ModMountOptions &options)
{
	ModMountReport report;
	if (!retail.ok)
	{
		report.errors.push_back("install mod: the retail game files are not mounted");
		return report;
	}
	if (overlays.empty())
	{
		report.errors.push_back("install mod: no folder given");
		return report;
	}

	// the overlays' archives by their path in the game folder (any depth: the startup mount reads lang\ as well, RW 0xA14419); with several
	// folders (an All In One package with its enhancements and patches: L1 .. L3, WorkshopKit AddFiles) the first folder holding a path wins
	struct OverlayArchive
	{
		std::string path; // '/' separators, on-disk case
		fs::path disk;
		HashJob hash;
	};
	std::vector<OverlayArchive> mine;
	std::map<std::string, size_t> owner; // lower-case path -> the index of the folder that supplies it
	std::vector<std::string> notes;
	std::vector<std::string> names;
	for (size_t index = 0; index < overlays.size(); ++index)
	{
		const std::string &overlay = overlays[index];
		std::error_code ec;
		const fs::path root = fs::u8path(overlay);
		if (overlay.empty() || !fs::is_directory(root, ec))
		{
			report.errors.push_back("install mod folder does not exist: '" + overlay + "'");
			names.push_back(overlay);
			continue;
		}
		// the folder's own name; an "install" folder (tools/mod/aio_package.py's layout) is named after its package folder
		const fs::path leaf = root.filename().empty() ? root.parent_path() : root;
		std::string name = leaf.filename().u8string();
		if (AsciiStringUtil::lowered(name) == "install" && !leaf.parent_path().filename().empty())
		{
			name = leaf.parent_path().filename().u8string();
		}
		names.push_back(name);
		for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
		{
			std::error_code typeEc;
			const std::string rel = it->path().lexically_relative(root).generic_u8string();
			if (it->is_symlink(typeEc))
			{
				report.errors.push_back("install mod: the folder holds a symbolic link, which is not supported: " + rel);
				it.disable_recursion_pending();
				continue;
			}
			if (!it->is_regular_file(typeEc))
			{
				continue;
			}
			if (lowerExtension(it->path()) != ".big")
			{
				notes.push_back("install mod " + name + ": " + rel + " is not an archive: not used");
				continue;
			}
			const std::string key = AsciiStringUtil::lowered(rel);
			auto taken = owner.find(key);
			if (taken != owner.end())
			{
				if (taken->second != index)
				{
					notes.push_back("install mod " + names[taken->second] + ": " + rel + " replaces " + name + "'s");
				}
				else
				{
					report.errors.push_back("install mod: two archives differ only in case (Windows cannot hold both): " + rel);
				}
				continue;
			}
			OverlayArchive a{ rel, it->path(), HashJob() };
			if (statJob(it->path(), a.hash, report))
			{
				owner[key] = index;
				mine.push_back(a);
			}
		}
		if (ec)
		{
			report.errors.push_back("install mod: could not list " + overlay + ": " + ec.message());
		}
	}
	for (size_t i = 0; i < names.size(); ++i)
	{
		report.folderName += (i ? " + " : "") + names[i];
	}
	if (mine.empty() && report.errors.empty())
	{
		report.errors.push_back("install mod: the folder(s) " + report.folderName + " hold no .big archive");
	}
	if (!report.errors.empty())
	{
		return report;
	}
	Md5Cache cache;
	cache.path = options.md5CachePath;
	cache.load();
	std::vector<HashJob> jobs;
	for (const OverlayArchive &a : mine)
	{
		jobs.push_back(a.hash);
	}
	hashJobs(jobs, cache, report);
	if (!report.errors.empty())
	{
		return report;
	}
	for (size_t i = 0; i < mine.size(); ++i)
	{
		mine[i].hash = jobs[i];
	}

	// the merged RotWK install: retail archives the overlay does not replace, plus the overlay's, in the install's load order
	std::map<std::string, size_t> overlayByKey; // lower-case path -> mine index
	for (size_t i = 0; i < mine.size(); ++i)
	{
		overlayByKey[AsciiStringUtil::lowered(mine[i].path)] = i;
	}
	if (!report.errors.empty())
	{
		return report;
	}
	std::vector<RetailArchiveEntry> merged;
	std::map<std::string, MountedArchive> byCanonical; // the entry each canonical path stands for
	std::vector<MountedArchive> others;                // the other installs (BFME2), in the retail mount order, after RotWK
	for (const MountedArchive &m : retail.mounted)
	{
		if (m.install != "rotwk")
		{
			others.push_back(m);
			continue;
		}
		if (overlayByKey.count(AsciiStringUtil::lowered(m.canonicalPath)))
		{
			continue; // replaced by the mod's file of the same path (it sits where the retail file was)
		}
		RetailArchiveEntry entry{ m.canonicalPath, m.md5, m.size };
		// issue #49: a retail language archive of another language (lang/German.big ...) keeps its place in the load order (RetailArchiveLoadOrder)
		const std::string lowered = AsciiStringUtil::lowered(m.canonicalPath);
		if (lowered.compare(0, 5, "lang/") == 0 && lowered.find('/', 5) == std::string::npos && lowered.compare(5, 7, "english") != 0)
		{
			entry.language = "lang";
		}
		merged.push_back(entry);
		byCanonical[m.canonicalPath] = m;
	}
	for (const OverlayArchive &a : mine)
	{
		merged.push_back({ a.path, a.hash.md5, a.hash.size });
		byCanonical[a.path] = MountedArchive{ "mod", a.path, a.disk.u8string(), a.hash.size, a.hash.md5 };
	}
	std::vector<MountedArchive> order;
	for (const RetailArchiveEntry &e : RetailArchiveLoadOrder(merged))
	{
		order.push_back(byCanonical[e.path]);
	}
	order.insert(order.end(), others.begin(), others.end());

	// open every archive before mounting the first (a failure leaves `out` empty)
	std::vector<std::unique_ptr<ArchiveFile>> opened;
	for (const MountedArchive &m : order)
	{
		std::string error;
		std::unique_ptr<ArchiveFile> archive = out.openArchiveFile(m.diskPath, &error);
		if (!archive)
		{
			report.errors.push_back((m.install == "mod" ? "install mod: " + m.canonicalPath + " is not a valid archive: " : m.install + ": ") + error);
			continue;
		}
		opened.push_back(std::move(archive));
	}
	if (!report.errors.empty())
	{
		return report;
	}
	for (size_t i = 0; i < order.size(); ++i)
	{
		out.mountArchive(std::move(opened[i]), order[i].diskPath, false); // the startup mount's overwrite 0: the first archive keeps a name
		if (order[i].install == "mod")
		{
			report.archives.push_back(order[i]);
		}
	}
	report.merged = order;
	for (size_t i = 0; i < order.size(); ++i)
	{
		const MountedArchive &m = order[i];
		report.manifest.push_back("installarchive " + std::to_string(i) + " " + m.install + ":" + m.canonicalPath + " " + m.md5 + " " + std::to_string(m.size));
	}
	std::string manifestText;
	for (const std::string &l : report.manifest)
	{
		manifestText += l + "\n";
	}
	report.identity.push_back("mod install " + report.folderName);
	report.identity.push_back("modcontent " + std::to_string(report.archives.size()) + " archives 0 files " + MD5::ofBytes(manifestText.data(), manifestText.size()));
	report.notes = notes;
	if (!mergeModGameData(out, report))
	{
		return report;
	}
	report.stops.push_back(InstallOverlayStopLine());
	const std::string warning = cache.save();
	if (!warning.empty())
	{
		report.warnings.push_back(warning);
	}
	report.ok = true;
	return report;
}

std::string InstallOverlayStopLine()
{
	return "S-5301: an install-folder mod's archives are merged with the RotWK 2.01 archives in RotWK's startup order (RW 0xA1766D: "
		   "lang\\English*.big, the game folder's *.big in compareNoCase order, apt\\*.big; first mounted wins) as if they sat in the game "
		   "folder; the mod stays in its own folder and the verified install is not changed (an OpenBFME arrangement of retail's single folder)";
}
