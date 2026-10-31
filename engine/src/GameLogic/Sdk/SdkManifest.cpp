// OpenBFME. GPL-3.0. See GameLogic/Sdk/SdkManifest.h (lane SDK-2).

#include "GameLogic/Sdk/SdkManifest.h"

#include "Common/MiniJson.h"
#include "Common/Sha256.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace
{

bool readNames(const JsonValue &root, const char *key, std::vector<std::string> &out, std::set<std::string> &seen, std::string &error)
{
	const JsonValue *v = root.get(key);
	if (!v)
		return true;
	if (!v->isArray())
	{
		error = std::string(SdkManifest::path()) + ": \"" + key + "\" must be a list of script names";
		return false;
	}
	for (const JsonValue &e : v->array)
	{
		if (!e.isString() || !LuauScriptState::safePath(e.string) || e.string.find('.') != std::string::npos)
		{
			error = std::string(SdkManifest::path()) + ": \"" + key + "\" holds " + (e.isString() ? "'" + e.string + "'" : std::string("a non-string")) +
				", not a script name (a name of openbfme/sdk/" + key + "/<name>.luau, without the extension)";
			return false;
		}
		if (!seen.insert(std::string(key) + "/" + e.string).second)
		{
			error = std::string(SdkManifest::path()) + ": \"" + key + "\" lists '" + e.string + "' twice";
			return false;
		}
		out.push_back(e.string);
	}
	return true;
}

} // namespace

const char *SdkManifest::roleFolder(SdkScriptRole role)
{
	return role == SdkScriptRole::Sim ? "openbfme/sdk/sim/" : "openbfme/sdk/presentation/";
}

std::string SdkManifest::scriptPath(SdkScriptRole role, const std::string &name)
{
	return std::string(roleFolder(role)) + name + ".luau";
}

bool SdkManifest::parse(const std::string &json, SdkManifest &out, std::string &error)
{
	out = SdkManifest();
	JsonValue root;
	std::string jsonError;
	if (!JsonValue::parse(json, root, &jsonError))
	{
		error = std::string(path()) + ": not valid JSON: " + jsonError;
		return false;
	}
	if (!root.isObject())
	{
		error = std::string(path()) + ": must be a JSON object";
		return false;
	}
	for (const auto &kv : root.object)
	{
		if (kv.first != "sdk" && kv.first != "name" && kv.first != "sim" && kv.first != "presentation")
		{
			error = std::string(path()) + ": unknown key \"" + kv.first + "\" (known: sdk, name, sim, presentation)";
			return false;
		}
	}
	const JsonValue *version = root.get("sdk");
	if (!version || !version->isNumber() || version->number != 1.0)
	{
		error = std::string(path()) + ": \"sdk\" must be 1 (the SDK version this OpenBFME runs)";
		return false;
	}
	out.sdkVersion = 1;
	const JsonValue *name = root.get("name");
	if (!name || !name->isString() || name->string.empty())
	{
		error = std::string(path()) + ": \"name\" must be the mod's name";
		return false;
	}
	if (name->string.size() > 64)
	{
		error = std::string(path()) + ": \"name\" is longer than 64 characters";
		return false;
	}
	for (unsigned char c : name->string)
	{
		if (c < 0x20 || c == 0x7F)
		{
			error = std::string(path()) + ": \"name\" holds a control character";
			return false;
		}
	}
	out.name = name->string;
	std::set<std::string> seen;
	return readNames(root, "sim", out.sim, seen, error) && readNames(root, "presentation", out.presentation, seen, error);
}

bool SdkSimFingerprint(const SdkManifest &manifest, const LuauScriptState::Reader &reader, const SdkLister &lister, std::string &out,
	std::string &error)
{
	out.clear();
	std::vector<std::string> files;
	if (!lister(SdkManifest::roleFolder(SdkScriptRole::Sim), files, error))
		return false;
	// (mount key, the path the lister gave): hashed and sorted by the key, read by the given path
	std::vector<std::pair<std::string, std::string>> keyed;
	for (const std::string &f : files)
		keyed.emplace_back(SdkModFiles::key(f), f);
	std::sort(keyed.begin(), keyed.end());
	files.clear();
	for (const auto &k : keyed)
		files.push_back(k.first);
	for (const std::string &entry : manifest.sim)
	{
		const std::string p = SdkManifest::scriptPath(SdkScriptRole::Sim, entry);
		if (!std::binary_search(files.begin(), files.end(), SdkModFiles::key(p)))
		{
			error = std::string(SdkManifest::path()) + ": the sim script '" + entry + "' is not in the mod (" + p + ")";
			return false;
		}
	}
	// the presentation entries must exist too (they are not part of the fingerprint)
	std::vector<std::string> shown;
	if (!lister(SdkManifest::roleFolder(SdkScriptRole::Presentation), shown, error))
		return false;
	for (std::string &f : shown)
		f = SdkModFiles::key(f);
	std::sort(shown.begin(), shown.end());
	for (const std::string &entry : manifest.presentation)
	{
		const std::string p = SdkManifest::scriptPath(SdkScriptRole::Presentation, entry);
		if (!std::binary_search(shown.begin(), shown.end(), SdkModFiles::key(p)))
		{
			error = std::string(SdkManifest::path()) + ": the presentation script '" + entry + "' is not in the mod (" + p + ")";
			return false;
		}
	}
	if (files.empty() && manifest.sim.empty())
		return true;
	// Anti-cheat pin 1: every sim file as "<path> <sha256>" (mount keys: lower case, '\\'), sorted; then the sim entries; then the SDK version
	std::string text;
	for (const auto &k : keyed)
	{
		std::string bytes;
		if (!reader(k.second, bytes, error))
			return false;
		const auto digest = Sha256::hash(bytes.data(), bytes.size());
		text += k.first + " " + HexBytes(digest.data(), digest.size()) + "\n";
	}
	for (const std::string &entry : manifest.sim)
		text += "sim " + entry + "\n";
	text += "sdk " + std::to_string(manifest.sdkVersion) + "\n";
	const auto digest = Sha256::hash(text.data(), text.size());
	out = HexBytes(digest.data(), digest.size());
	return true;
}

SdkModFiles::SdkModFiles(const ModLooseIndex &index)
{
	for (const ModLooseFile &f : index.files)
		m_files[key(f.virtualPath)] = f;
}

std::string SdkModFiles::key(const std::string &path)
{
	return ModLooseIndex::key(path); // the mount's own normalization (lower case, '\\' separators; Anti-cheat pin 2)
}

bool SdkModFiles::isPresentationFile(const std::string &path)
{
	static const std::string prefix = key(SdkManifest::roleFolder(SdkScriptRole::Presentation)) + "\\";
	return key(path).rfind(prefix, 0) == 0;
}

bool SdkModFiles::read(const std::string &path, std::string &out, std::string &error) const
{
	auto it = m_files.find(key(path));
	if (it == m_files.end())
	{
		error = "no file " + path + " in the mod";
		return false;
	}
	const ModLooseFile &f = it->second;
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path p = fs::u8path(f.diskPath);
	const std::uint64_t size = (std::uint64_t)fs::file_size(p, ec);
	const long long mtime = ec ? 0 : (long long)fs::last_write_time(p, ec).time_since_epoch().count();
	if (ec || size != f.size || mtime != f.mtime)
	{
		error = "the mod file " + f.virtualPath + " changed or vanished since the mod was loaded (restart to use it)";
		return false;
	}
	std::ifstream in(p, std::ios::binary);
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	if (out.size() != f.size)
	{
		error = "cannot read the mod file " + f.virtualPath;
		return false;
	}
	return true;
}

LuauScriptState::Reader SdkModFiles::reader() const
{
	return [this](const std::string &path, std::string &out, std::string &error) { return read(path, out, error); };
}

SdkLister SdkModFiles::lister() const
{
	return [this](const std::string &folder, std::vector<std::string> &paths, std::string &) {
		const std::string prefix = key(folder) + "\\";
		for (auto it = m_files.lower_bound(prefix); it != m_files.end() && it->first.rfind(prefix, 0) == 0; ++it)
			paths.push_back(it->first);
		return true;
	};
}
