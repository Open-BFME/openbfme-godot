// OpenBFME. GPL-3.0.

#include "Common/RetailArchivePolicy.h"
#include "Common/ArchiveFile.h"
#include "Common/AsciiString.h"
#include "Common/JobSystem.h"
#include "Common/MD5.h"
#include "Common/MiniJson.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

// Generated at build time from engine/data/retail-archives (see engine/CMakeLists.txt).
bool GetEmbeddedDataFile(const std::string &name, std::string &out);

namespace fs = std::filesystem;

namespace
{

std::string slashesToBackslashes(std::string s)
{
	std::replace(s.begin(), s.end(), '/', '\\');
	return s;
}

std::string normalizedKey(std::string s)
{
	std::replace(s.begin(), s.end(), '\\', '/');
	AsciiStringUtil::toLower(s);
	return s;
}

// Threat model: the md5 check (and this cache) detects wrong or modified game files, such as a
// mod, a patch or a damaged copy. A cache hit trusts "same absolute path + size + last-write time",
// so it does not stop deliberate tampering by someone who edits a file and then restores its
// timestamp (or who edits the cache). That is accepted: cross-play integrity comes from the
// retail INI/logic CRCs exchanged between players, not from this cache.
struct Md5Cache
{
	std::map<std::string, std::string> entries; // "size\tmtime\tpath" -> md5
	std::string path;
	bool dirty = false;

	static std::string key(const std::string &file, std::uint64_t size, long long mtime)
	{
		return std::to_string(size) + "\t" + std::to_string(mtime) + "\t" + file;
	}

	static bool isMd5Hex(const std::string &s)
	{
		if (s.size() != 32)
		{
			return false;
		}
		for (char c : s)
		{
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			{
				return false;
			}
		}
		return true;
	}

	// A missing or damaged cache is only a lost speed-up (every key mismatch rehashes), so lines
	// that do not parse are dropped here; they never reach a verification decision.
	void load()
	{
		if (path.empty())
		{
			return;
		}
		std::ifstream in(fs::u8path(path));
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			size_t tab = line.find('\t');
			if (tab == std::string::npos || !isMd5Hex(line.substr(0, tab)))
			{
				continue;
			}
			entries[line.substr(tab + 1)] = line.substr(0, tab);
		}
	}

	// Writes to a sibling temp file and renames over the cache, so a crash or a second process
	// never leaves a half-written cache. Returns an empty string or a warning text.
	std::string save()
	{
		if (path.empty() || !dirty)
		{
			return std::string();
		}
		std::error_code ec;
		fs::path target = fs::u8path(path);
		fs::create_directories(target.parent_path(), ec);
		fs::path temp = target;
		temp += ".tmp";
		{
			std::ofstream out(temp, std::ios::trunc);
			for (const auto &e : entries)
			{
				out << e.second << '\t' << e.first << '\n';
			}
			out.flush();
			if (!out)
			{
				return "could not write the md5 cache " + temp.u8string() + "; every mount will rehash";
			}
		}
		fs::rename(temp, target, ec);
		if (ec)
		{
			fs::remove(temp, ec);
			return "could not replace the md5 cache " + path + ": " + ec.message() + "; every mount will rehash";
		}
		return std::string();
	}
};

} // namespace

bool RetailArchivePolicy::parse(const std::string &policyJson, const std::string &exclusionJson, RetailArchivePolicy &out, std::string *error)
{
	out = RetailArchivePolicy();
	JsonValue root;
	if (!JsonValue::parse(policyJson, root, error))
	{
		return false;
	}
	const JsonValue *schema = root.get("schema");
	if (!schema || !schema->isString() || schema->string != "openbfme.retail-archive-policy")
	{
		if (error)
		{
			*error = "policy JSON is not an openbfme.retail-archive-policy";
		}
		return false;
	}
	const JsonValue *game = root.get("game");
	const JsonValue *patch = root.get("patch");
	const JsonValue *archives = root.get("archives");
	if (!game || !game->isString() || !patch || !patch->isString() || !archives || !archives->isArray())
	{
		if (error)
		{
			*error = "policy JSON lacks game/patch/archives";
		}
		return false;
	}
	out.game = game->string;
	out.patch = patch->string;
	for (const JsonValue &a : archives->array)
	{
		const JsonValue *p = a.get("path");
		const JsonValue *m = a.get("md5");
		const JsonValue *s = a.get("size");
		if (!p || !p->isString() || !m || !m->isString() || !s || !s->isNumber() || s->number < 0)
		{
			if (error)
			{
				*error = "policy archive entry lacks path/md5/size";
			}
			return false;
		}
		RetailArchiveEntry entry;
		entry.path = p->string;
		entry.md5 = AsciiStringUtil::lowered(m->string);
		entry.size = (std::uint64_t)s->number;
		out.archives.push_back(entry);
	}

	if (!exclusionJson.empty())
	{
		JsonValue ex;
		if (!JsonValue::parse(exclusionJson, ex, error))
		{
			return false;
		}
		const JsonValue *exSchema = ex.get("schema");
		const JsonValue *exArchives = ex.get("archives");
		if (!exSchema || !exSchema->isString() || exSchema->string != "openbfme.retail-archive-exclusions" || !exArchives || !exArchives->isArray())
		{
			if (error)
			{
				*error = "exclusion JSON is not an openbfme.retail-archive-exclusions";
			}
			return false;
		}
		for (const JsonValue &a : exArchives->array)
		{
			const JsonValue *p = a.get("path");
			if (!p || !p->isString())
			{
				if (error)
				{
					*error = "exclusion entry lacks path";
				}
				return false;
			}
			out.excluded.push_back(p->string);
		}
	}
	return true;
}

bool RetailArchivePolicy::loadBuiltin(const std::string &id, RetailArchivePolicy &out, std::string *error)
{
	std::string policy;
	std::string exclusions;
	if (id == "rotwk-201")
	{
		if (!GetEmbeddedDataFile("rotwk-201-english-archives.json", policy) ||
			!GetEmbeddedDataFile("rotwk-201-excluded-archives.json", exclusions))
		{
			if (error)
			{
				*error = "embedded rotwk-201 policy missing from build";
			}
			return false;
		}
	}
	else if (id == "bfme2-106")
	{
		if (!GetEmbeddedDataFile("bfme2-106-english-archives.json", policy) ||
			!GetEmbeddedDataFile("bfme2-106-excluded-archives.json", exclusions))
		{
			if (error)
			{
				*error = "embedded bfme2-106 policy missing from build";
			}
			return false;
		}
	}
	else
	{
		if (error)
		{
			*error = "unknown built-in archive policy " + id;
		}
		return false;
	}
	return parse(policy, exclusions, out, error);
}

std::vector<RetailArchiveEntry> RetailArchiveLoadOrder(const std::vector<RetailArchiveEntry> &archives)
{
	std::vector<std::string> names;
	std::map<std::string, RetailArchiveEntry> byName;
	for (const RetailArchiveEntry &a : archives)
	{
		std::string name = slashesToBackslashes(a.path);
		names.push_back(name);
		byName[name] = a;
	}
	std::vector<RetailArchiveEntry> ordered;
	for (const std::string &name : BFMEArchiveLoadOrder(names))
	{
		ordered.push_back(byName[name]);
	}
	return ordered;
}

RetailMountReport MountRetailArchives(Win32BIGFileSystem &fileSystem, const std::vector<RetailInstall> &installs,
	const RetailMountOptions &options)
{
	RetailMountReport report;
	Md5Cache cache;
	cache.path = options.md5CachePath;
	cache.load();

	struct Planned
	{
		std::string install;
		RetailArchiveEntry entry;
		std::string diskPath;
	};
	std::vector<Planned> plan;
	// lane PERF-2: the archives' MD5s are computed on the client job pool. The installs are walked first, recording in order every error and every
	// archive that reached its MD5 check (`steps`); every cache key the sequential pass could hash is hashed ONCE, in parallel (each job writes only its
	// own key's result); then the steps are replayed in order and the replay decides each check as the sequential pass did (a cache hit against the
	// cache as the earlier steps left it, else the key's hash), so the errors, the counters (archivesHashed, md5CacheHits), the cache and the plan are
	// exactly the sequential pass's, duplicate installs included (Sol r1).
	struct Pending
	{
		std::string install;
		RetailArchiveEntry entry;
		std::string diskPath; ///< the found path (the plan's)
		std::string key;      ///< the cache key (absolute path + size + modification time)
	};
	struct KeyHash
	{
		std::string disk; ///< the absolute path
		std::string md5, error;
	};
	std::vector<KeyHash> hashes;
	std::map<std::string, size_t> hashOfKey;
	struct Step
	{
		std::string error; ///< an error, in its place
		int pending = -1;  ///< or an archive at its MD5 check
	};
	std::vector<Pending> pending;
	std::vector<Step> steps;
	auto stepError = [&steps](std::string e) { steps.push_back(Step{ std::move(e), -1 }); };

	if (installs.empty())
	{
		report.errors.push_back("no installs given");
	}

	for (const RetailInstall &install : installs)
	{
		std::error_code ec;
		fs::path root = fs::u8path(install.root);
		if (install.root.empty() || !fs::is_directory(root, ec))
		{
			stepError(install.label + ": install directory does not exist: '" + install.root + "'");
			continue;
		}

		// Every *.big under the install, keyed by lower-case relative path.
		std::map<std::string, fs::path> onDisk;
		for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_regular_file())
			{
				continue;
			}
			std::string ext = AsciiStringUtil::lowered(it->path().extension().u8string());
			if (ext != ".big")
			{
				continue;
			}
			onDisk[normalizedKey(fs::relative(it->path(), root).u8string())] = it->path();
		}
		if (ec)
		{
			stepError(install.label + ": could not list " + install.root + ": " + ec.message());
			continue;
		}

		std::map<std::string, bool> known;
		for (const RetailArchiveEntry &entry : install.policy.archives)
		{
			known[normalizedKey(entry.path)] = true;
		}
		for (const std::string &ex : install.policy.excluded)
		{
			std::string key = normalizedKey(ex);
			if (onDisk.count(key))
			{
				report.excludedFound.push_back(install.label + ":" + ex);
			}
			known[key] = true;
		}
		for (const auto &disk : onDisk)
		{
			if (!known.count(disk.first))
			{
				stepError(install.label + ": unknown archive '" + disk.first +
					"' is not part of " + install.policy.game + " " + install.policy.patch + " and not a known exclusion");
			}
		}

		for (const RetailArchiveEntry &entry : RetailArchiveLoadOrder(install.policy.archives))
		{
			auto found = onDisk.find(normalizedKey(entry.path));
			if (found == onDisk.end())
			{
				stepError(install.label + ": missing archive " + entry.path);
				continue;
			}
			std::uint64_t size = (std::uint64_t)fs::file_size(found->second, ec);
			if (ec || size != entry.size)
			{
				stepError(install.label + ": size mismatch for " + entry.path + ": expected " +
					std::to_string(entry.size) + ", found " + (ec ? ec.message() : std::to_string(size)));
				continue;
			}
			// md5 is always verified. The cache key is absolute path + size + last-write time, so any
			// change to the file's identity rehashes it; a cached value that disagrees with the
			// policy is rehashed too, so a stale or damaged cache can never refuse (or admit) a file.
			fs::path absolute = fs::absolute(found->second, ec).lexically_normal();
			if (ec)
			{
				stepError(install.label + ": could not resolve the path of " + entry.path + ": " + ec.message());
				continue;
			}
			std::string disk = absolute.u8string();
			long long mtime = (long long)fs::last_write_time(absolute, ec).time_since_epoch().count();
			if (ec)
			{
				stepError(install.label + ": could not read the modification time of " + entry.path + ": " + ec.message());
				continue;
			}
			Pending p;
			p.install = install.label;
			p.entry = entry;
			p.diskPath = found->second.u8string();
			p.key = Md5Cache::key(disk, size, mtime);
			// the hash this check may need: none when the cache (as the earlier checks leave it) surely vouches for it; a key already scheduled is
			// hashed once (its result is what the sequential pass would compute again: the key fixes path, size and modification time)
			if (!hashOfKey.count(p.key))
			{
				auto cached = cache.entries.find(p.key);
				if (cached == cache.entries.end() || cached->second != entry.md5)
				{
					hashOfKey[p.key] = hashes.size();
					hashes.push_back(KeyHash{ disk, std::string(), std::string() });
				}
			}
			steps.push_back(Step{ std::string(), (int)pending.size() });
			pending.push_back(std::move(p));
		}
	}

	// the hashes (MD5::ofFile reads its whole archive: the client pool reads and hashes several at once)
	JobSystem::client().parallelFor(hashes.size(), 1, [&hashes](size_t, size_t begin, size_t end) {
		for (size_t i = begin; i < end; ++i)
		{
			hashes[i].md5 = MD5::ofFile(hashes[i].disk, &hashes[i].error);
		}
	});

	for (const Step &step : steps)
	{
		if (step.pending < 0)
		{
			report.errors.push_back(step.error);
			continue;
		}
		const Pending &p = pending[(size_t)step.pending];
		std::string md5;
		bool fromCache = false;
		auto cached = cache.entries.find(p.key);
		if (cached != cache.entries.end() && cached->second == p.entry.md5)
		{
			md5 = cached->second;
			fromCache = true;
		}
		else
		{
			const KeyHash &h = hashes[hashOfKey.at(p.key)]; // scheduled above: this check is not a sure cache hit
			if (h.md5.empty())
			{
				report.errors.push_back(p.install + ": " + h.error);
				continue;
			}
			md5 = h.md5;
			++report.archivesHashed;
			if (cached == cache.entries.end() || cached->second != md5)
			{
				cache.entries[p.key] = md5;
				cache.dirty = true;
			}
		}
		if (md5 != p.entry.md5)
		{
			report.errors.push_back(p.install + ": md5 mismatch for " + p.entry.path + ": expected " + p.entry.md5 + ", found " + md5);
			continue;
		}
		if (fromCache)
		{
			++report.md5CacheHits;
		}
		plan.push_back({ p.install, p.entry, p.diskPath });
	}

	{
		std::string warning = cache.save();
		if (!warning.empty())
		{
			report.warnings.push_back(warning);
		}
	}

	if (!report.errors.empty())
	{
		return report; // fail closed: nothing mounted
	}

	// Transactional: open and validate every archive first, mount only when all of them opened.
	std::vector<std::unique_ptr<ArchiveFile>> opened;
	for (const Planned &p : plan)
	{
		std::string error;
		std::unique_ptr<ArchiveFile> archive = fileSystem.openArchiveFile(p.diskPath, &error);
		if (!archive)
		{
			report.errors.push_back(p.install + ": " + error);
			continue;
		}
		opened.push_back(std::move(archive));
	}
	if (!report.errors.empty())
	{
		return report; // nothing has been mounted yet
	}
	for (size_t i = 0; i < plan.size(); ++i)
	{
		fileSystem.mountArchive(std::move(opened[i]), plan[i].diskPath, false);
		report.mounted.push_back({ plan[i].install, plan[i].entry.path, plan[i].diskPath, plan[i].entry.size, plan[i].entry.md5 });
	}
	report.ok = true;
	return report;
}

bool ResolveInstallPath(const std::string &key, const std::string &configPath, std::string &out, std::string *error)
{
	const char *env = std::getenv(key.c_str());
	if (env != nullptr && env[0] != 0)
	{
		out = env;
		return true;
	}
	if (!configPath.empty())
	{
		std::ifstream in(fs::u8path(configPath));
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			size_t eq = line.find('=');
			if (eq == std::string::npos || line[0] == '#' || line[0] == ';')
			{
				continue;
			}
			std::string k = line.substr(0, eq);
			std::string v = line.substr(eq + 1);
			while (!k.empty() && (k.back() == ' ' || k.back() == '\t'))
			{
				k.pop_back();
			}
			while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
			{
				v.erase(v.begin());
			}
			if (k == key && !v.empty())
			{
				out = v;
				return true;
			}
		}
	}
	if (error)
	{
		*error = key + " is not set (environment variable" + (configPath.empty() ? std::string("") : ", or " + configPath) + ")";
	}
	return false;
}
