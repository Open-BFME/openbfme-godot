// OpenBFME. GPL-3.0.
// See Common/InstallLocator.h (lane RELEASE-1).

#include "Common/InstallLocator.h"

#include "Common/AsciiString.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif

#endif

namespace fs = std::filesystem;

namespace InstallLocator
{

const char *const kRotwk = "rotwk";
const char *const kBfme2 = "bfme2";

namespace
{

// gi.dat GameRegPath of each game (RotWK: target fact through RW 0xAAA6C0 / 0x9787C9; BFME2: its gi.dat, inference that
// the same reader is used). Pinned against the retail gi.dat files by test_release1_install.cpp.
const char *const kRotwkGameRegPath = "SOFTWARE\\Electronic Arts\\Electronic Arts\\The Lord of the Rings, The Rise of the Witch-king";
const char *const kBfme2GameRegPath = "SOFTWARE\\Electronic Arts\\Electronic Arts\\The Battle for Middle-earth II";
const char *const kAppPaths = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\"; // RW 0xBE8368 (up to the exe name)

std::string displayName(const std::string &game)
{
	return game == kRotwk ? "The Rise of the Witch-king" : "The Battle for Middle-earth II";
}

std::string lower(std::string s)
{
	AsciiStringUtil::toLower(s);
	return s;
}

std::string normalizedKey(std::string s)
{
	std::replace(s.begin(), s.end(), '\\', '/');
	return lower(s);
}

std::string readText(const fs::path &p)
{
	std::ifstream in(p, std::ios::binary);
	std::ostringstream o;
	o << in.rdbuf();
	return o.str();
}

std::string envOf(const DiscoveryEnvironment &e, const char *name)
{
	auto it = e.env.find(name);
	return it == e.env.end() ? std::string() : it->second;
}

void appendUtf8(std::string &out, unsigned cp)
{
	if (cp < 0x80)
	{
		out += (char)cp;
	}
	else if (cp < 0x800)
	{
		out += (char)(0xC0 | (cp >> 6));
		out += (char)(0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000)
	{
		out += (char)(0xE0 | (cp >> 12));
		out += (char)(0x80 | ((cp >> 6) & 0x3F));
		out += (char)(0x80 | (cp & 0x3F));
	}
	else
	{
		out += (char)(0xF0 | (cp >> 18));
		out += (char)(0x80 | ((cp >> 12) & 0x3F));
		out += (char)(0x80 | ((cp >> 6) & 0x3F));
		out += (char)(0x80 | (cp & 0x3F));
	}
}

int hexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

// Wine's registry escapes (dlls/advapi32 / server/registry.c dump_strW): \\ \" \n \r \t \0 \a.. and \xHHHH (UTF-16 code
// units, up to 4 hex digits). `text` starts after the opening quote; stops at the closing quote (returns its position
// + 1 in `end`), or at `stop` for a key name in brackets.
std::string unescapeWine(const std::string &text, size_t begin, char stop, size_t &end)
{
	std::string out;
	unsigned pendingHigh = 0;
	size_t i = begin;
	while (i < text.size() && text[i] != stop && text[i] != '\n')
	{
		char c = text[i];
		if (c != '\\' || i + 1 >= text.size())
		{
			out += c;
			++i;
			continue;
		}
		char n = text[i + 1];
		i += 2;
		unsigned unit = 0;
		bool isUnit = false;
		switch (n)
		{
		case 'n': out += '\n'; break;
		case 'r': out += '\r'; break;
		case 't': out += '\t'; break;
		case '0': out += '\0'; break;
		case 'x':
		{
			int digits = 0;
			while (digits < 4 && i < text.size() && hexDigit(text[i]) >= 0)
			{
				unit = unit * 16 + (unsigned)hexDigit(text[i]);
				++i;
				++digits;
			}
			isUnit = true;
			break;
		}
		default: out += n; break; // \\ and \" and anything else: the character itself
		}
		if (isUnit)
		{
			if (unit >= 0xD800 && unit < 0xDC00)
			{
				pendingHigh = unit;
				continue;
			}
			if (unit >= 0xDC00 && unit < 0xE000 && pendingHigh)
			{
				appendUtf8(out, 0x10000 + ((pendingHigh - 0xD800) << 10) + (unit - 0xDC00));
			}
			else
			{
				appendUtf8(out, unit);
			}
			pendingHigh = 0;
		}
	}
	end = i < text.size() && text[i] == stop ? i + 1 : i;
	return out;
}

std::string stripTrailingSeparators(std::string s)
{
	while (s.size() > 1 && (s.back() == '\\' || s.back() == '/') && !(s.size() == 3 && s[1] == ':'))
	{
		s.pop_back();
	}
	return s;
}

bool isDir(const fs::path &p)
{
	std::error_code ec;
	return fs::is_directory(p, ec);
}

std::vector<fs::path> sortedSubdirs(const fs::path &dir)
{
	std::vector<fs::path> out;
	std::error_code ec;
	for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
	{
		if (isDir(it->path()))
		{
			out.push_back(it->path());
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

// One component matched case-insensitively (a Windows path inside a Linux file system).
fs::path childCaseInsensitive(const fs::path &dir, const std::string &name)
{
	fs::path exact = dir / fs::u8path(name);
	std::error_code ec;
	if (fs::exists(exact, ec))
	{
		return exact;
	}
	const std::string want = lower(name);
	std::vector<fs::path> matches;
	for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
	{
		if (lower(it->path().filename().u8string()) == want)
		{
			matches.push_back(it->path());
		}
	}
	std::sort(matches.begin(), matches.end());
	return matches.empty() ? exact : matches.front();
}

std::vector<std::string> splitWindowsPath(const std::string &rest)
{
	std::vector<std::string> parts;
	std::string cur;
	for (char c : rest)
	{
		if (c == '\\' || c == '/')
		{
			if (!cur.empty())
				parts.push_back(cur);
			cur.clear();
		}
		else
		{
			cur += c;
		}
	}
	if (!cur.empty())
		parts.push_back(cur);
	return parts;
}

std::vector<std::string> steamLibraries(const std::string &home)
{
	std::vector<std::string> roots = { home + "/.steam/steam", home + "/.steam/root", home + "/.local/share/Steam",
		home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam", home + "/.var/app/com.valvesoftware.Steam/data/Steam" };
	std::vector<std::string> libs;
	for (const std::string &root : roots)
	{
		if (!isDir(fs::u8path(root)))
			continue;
		libs.push_back(root);
		for (const char *vdf : { "/steamapps/libraryfolders.vdf", "/config/libraryfolders.vdf" })
		{
			std::istringstream in(readText(fs::u8path(root + vdf)));
			std::string line;
			while (std::getline(in, line))
			{
				// "path"		"/run/media/mmcblk0p1"
				size_t k = line.find("\"path\"");
				if (k == std::string::npos)
					continue;
				size_t a = line.find('"', k + 6);
				if (a == std::string::npos)
					continue;
				size_t e = 0;
				std::string p = unescapeWine(line, a + 1, '"', e);
				if (!p.empty())
					libs.push_back(p);
			}
		}
	}
	return libs;
}

struct Collector
{
	std::vector<Candidate> out;
	std::set<std::string> seen;

	void add(const std::string &game, const fs::path &path, const std::string &source)
	{
		if (path.empty() || !isDir(path))
			return;
		std::error_code ec;
		fs::path canon = fs::weakly_canonical(path, ec);
		std::string key = game + "|" + lower((ec ? path : canon).u8string());
		if (!seen.insert(key).second)
			return;
		out.push_back(Candidate{ game, (ec ? path : canon).u8string(), source });
	}
};

// stop S-1560: a hint that is inference (a registry value retail is not seen reading, an installer default folder) says so in its source
const char *const kInferenceTag = " [inference, S-1560]";

std::string queryTag(const RegistryQuery &q)
{
	return q.why.compare(0, 9, "inference") == 0 ? kInferenceTag : "";
}

std::string shortHome(const std::string &path, const std::string &home)
{
	if (!home.empty() && path.compare(0, home.size(), home) == 0)
		return "~" + path.substr(home.size());
	return path;
}

void discoverInPrefix(Collector &c, const std::string &prefix, const std::string &home)
{
	const std::string systemReg = readText(fs::u8path(prefix) / "system.reg");
	const std::string userReg = readText(fs::u8path(prefix) / "user.reg");
	const std::string label = "Wine prefix " + shortHome(prefix, home);
	for (const char *game : { kRotwk, kBfme2 })
	{
		for (const RegistryQuery &q : registryQueries(game))
		{
			// HKLM\SOFTWARE\x of a 32-bit program: Software\Wow6432Node\x in a 64-bit prefix, Software\x in a 32-bit one
			std::vector<std::string> keys;
			const std::string rest = q.key.substr(q.key.find('\\') + 1); // after "SOFTWARE"
			if (q.hive == "HKLM")
				keys = { "Software\\Wow6432Node\\" + rest, "Software\\" + rest };
			else
				keys = { "Software\\" + rest };
			for (const std::string &k : keys)
			{
				std::string value;
				if (wineRegistryValue(q.hive == "HKLM" ? systemReg : userReg, k, q.value, value) && !value.empty())
				{
					c.add(game, fs::u8path(wineToHost(prefix, stripTrailingSeparators(value))),
						label + ": registry " + q.hive + "\\" + k + " " + q.value + queryTag(q));
				}
			}
		}
		for (const char *pf : { "C:\\Program Files (x86)\\", "C:\\Program Files\\", "C:\\Games\\" })
		{
			for (const std::string &hint : folderHints(game))
			{
				c.add(game, fs::u8path(wineToHost(prefix, pf + hint)), label + ": folder " + pf + hint + kInferenceTag);
			}
		}
	}
}

} // namespace

std::string configKey(const std::string &game)
{
	return game == kRotwk ? "ROTWK_INSTALL" : "BFME2_INSTALL";
}

std::vector<RegistryQuery> registryQueries(const std::string &game)
{
	if (game == kRotwk)
	{
		return {
			{ "HKLM", kRotwkGameRegPath, "InstallPath", "RW 0x9787C9 / 0x64148E: gi.dat GameRegPath, HKLM first" },
			{ "HKCU", kRotwkGameRegPath, "InstallPath", "RW 0x64148E: then HKCU" },
			{ "HKLM", std::string(kAppPaths) + "lotrbfme2ep1.exe", "Path", "inference: App Paths like RW 0x638D22's lotrbfme2.exe" },
		};
	}
	return {
		{ "HKLM", std::string(kAppPaths) + "lotrbfme2.exe", "Path", "RW 0x638D22: how RotWK finds BFME2" },
		{ "HKLM", kBfme2GameRegPath, "InstallPath", "inference: BFME2's gi.dat GameRegPath read like RotWK's" },
		{ "HKCU", kBfme2GameRegPath, "InstallPath", "inference: then HKCU (RW 0x64148E's order)" },
	};
}

std::vector<std::string> folderHints(const std::string &game)
{
	if (game == kRotwk)
	{
		return { "EA Games\\The Lord of the Rings, The Rise of the Witch-king", "Electronic Arts\\The Lord of the Rings, The Rise of the Witch-king" };
	}
	return { "EA Games\\The Battle for Middle-earth (tm) II", "Electronic Arts\\The Battle for Middle-earth (tm) II",
		"EA Games\\The Battle for Middle-earth II", "Electronic Arts\\The Battle for Middle-earth II" };
}

DiscoveryEnvironment DiscoveryEnvironment::host()
{
	DiscoveryEnvironment e;
	auto get = [](const char *name) {
		const char *v = std::getenv(name);
		return v ? std::string(v) : std::string();
	};
	for (const char *name : { "WINEPREFIX", "XDG_DATA_HOME", "ProgramFiles(x86)", "ProgramFiles", "ProgramW6432" })
	{
		std::string v = get(name);
		if (!v.empty())
			e.env[name] = v;
	}
#ifdef _WIN32
	e.windows = true;
	e.home = get("USERPROFILE");
	DWORD mask = GetLogicalDrives();
	for (int i = 0; i < 26; ++i)
	{
		if (!(mask & (1u << i)))
			continue;
		std::string root = std::string(1, (char)('A' + i)) + ":\\";
		if (GetDriveTypeA(root.c_str()) == DRIVE_FIXED)
			e.drives.push_back(root);
	}
	e.registry = [](const RegistryQuery &q, std::string &out) {
		auto widen = [](const std::string &s) {
			int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
			std::wstring w(n > 0 ? (size_t)n : 1, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
			w.resize(wcslen(w.c_str()));
			return w;
		};
		HKEY hive = q.hive == "HKCU" ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
		std::wstring key = widen(q.key), value = widen(q.value);
		// the 32-bit view: retail is a 32-bit program and its installers write there (WOW6432Node on 64-bit Windows). RegOpenKeyExW with
		// KEY_WOW64_32KEY, then RegQueryValueExW: RegGetValueW's RRF_SUBKEY_WOW6432KEY found nothing under Wine (RELEASE-1 r5, the first run
		// of the Windows package), and the flag needs Windows 10.
		HKEY handle = nullptr;
		if (RegOpenKeyExW(hive, key.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &handle) != ERROR_SUCCESS)
			return false;
		wchar_t buffer[1024] = {};
		DWORD bytes = sizeof(buffer) - sizeof(wchar_t); // room for a terminator the stored string may lack
		DWORD type = 0;
		const LONG r = RegQueryValueExW(handle, value.c_str(), nullptr, &type, reinterpret_cast<BYTE *>(buffer), &bytes);
		RegCloseKey(handle);
		if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
			return false;
		buffer[bytes / sizeof(wchar_t)] = L'\0';
		if (type == REG_EXPAND_SZ)
		{
			wchar_t expanded[1024] = {};
			const DWORD n = ExpandEnvironmentStringsW(buffer, expanded, 1024);
			if (n == 0 || n > 1024)
				return false;
			wcscpy(buffer, expanded);
		}
		int n = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
		std::string s(n > 0 ? (size_t)n : 1, '\0');
		WideCharToMultiByte(CP_UTF8, 0, buffer, -1, &s[0], n, nullptr, nullptr);
		s.resize(strlen(s.c_str()));
		out = s;
		return true;
	};
#else
	e.home = get("HOME");
#endif
	return e;
}

bool wineRegistryValue(const std::string &regText, const std::string &key, const std::string &value, std::string &out)
{
	const std::string wantKey = lower(key);
	const std::string wantValue = lower(value);
	bool inKey = false;
	size_t pos = 0;
	while (pos < regText.size())
	{
		size_t eol = regText.find('\n', pos);
		if (eol == std::string::npos)
			eol = regText.size();
		if (regText[pos] == '[')
		{
			size_t end = 0;
			inKey = lower(unescapeWine(regText, pos + 1, ']', end)) == wantKey;
		}
		else if (inKey && (regText[pos] == '"' || regText[pos] == '@'))
		{
			std::string name;
			size_t after = pos + 1;
			if (regText[pos] == '"')
				name = unescapeWine(regText, pos + 1, '"', after);
			if (after < regText.size() && regText[after] == '=' && lower(name) == wantValue)
			{
				size_t v = after + 1;
				if (regText.compare(v, 7, "str(2):") == 0)
					v += 7;
				if (v < regText.size() && regText[v] == '"')
				{
					size_t end = 0;
					out = unescapeWine(regText, v + 1, '"', end);
					return true;
				}
			}
		}
		pos = eol + 1;
	}
	return false;
}

std::string wineToHost(const std::string &prefix, const std::string &windowsPath)
{
	if (windowsPath.size() < 2 || windowsPath[1] != ':' || !std::isalpha((unsigned char)windowsPath[0]))
		return std::string();
	const char drive = (char)std::tolower((unsigned char)windowsPath[0]);
	fs::path root;
	std::error_code ec;
	fs::path dosdevice = fs::u8path(prefix) / "dosdevices" / (std::string(1, drive) + ":");
	if (fs::exists(dosdevice, ec))
	{
		root = fs::canonical(dosdevice, ec);
		if (ec)
			return std::string();
	}
	else if (drive == 'c' && isDir(fs::u8path(prefix) / "drive_c"))
	{
		root = fs::u8path(prefix) / "drive_c";
	}
	else
	{
		return std::string();
	}
	fs::path p = root;
	for (const std::string &part : splitWindowsPath(windowsPath.substr(2)))
	{
		p = childCaseInsensitive(p, part);
	}
	return p.u8string();
}

std::vector<std::string> winePrefixes(const DiscoveryEnvironment &e)
{
	std::vector<std::string> found;
	std::set<std::string> seen;
	auto consider = [&](const fs::path &p) {
		std::error_code ec;
		if (!fs::is_regular_file(p / "system.reg", ec))
			return;
		fs::path canon = fs::weakly_canonical(p, ec);
		std::string s = (ec ? p : canon).u8string();
		if (seen.insert(s).second)
			found.push_back(s);
	};
	auto eachChild = [&](const std::string &dir) {
		for (const fs::path &p : sortedSubdirs(fs::u8path(dir)))
			consider(p);
	};
	const std::string wineprefix = envOf(e, "WINEPREFIX");
	if (!wineprefix.empty())
		consider(fs::u8path(wineprefix));
	if (e.home.empty())
		return found;
	const std::string &h = e.home;
	consider(fs::u8path(h + "/.wine"));
	for (const std::string &lib : steamLibraries(h))
	{
		for (const fs::path &app : sortedSubdirs(fs::u8path(lib) / "steamapps" / "compatdata"))
			consider(app / "pfx");
	}
	eachChild(h + "/Games");                          // Lutris (one prefix per game)
	eachChild(h + "/Games/Heroic/Prefixes");          // Heroic
	eachChild(h + "/Games/Heroic/Prefixes/default");
	std::string xdgData = envOf(e, "XDG_DATA_HOME");
	eachChild((xdgData.empty() ? h + "/.local/share" : xdgData) + "/bottles/bottles"); // Bottles
	eachChild(h + "/.var/app/com.usebottles.bottles/data/bottles/bottles");
	eachChild(h + "/.PlayOnLinux/wineprefix");        // PlayOnLinux
	return found;
}

std::vector<Candidate> discover(const DiscoveryEnvironment &e)
{
	Collector c;
	if (e.windows)
	{
		for (const char *game : { kRotwk, kBfme2 })
		{
			if (e.registry)
			{
				for (const RegistryQuery &q : registryQueries(game))
				{
					std::string value;
					if (e.registry(q, value) && !value.empty())
						c.add(game, fs::u8path(stripTrailingSeparators(value)), "registry " + q.hive + "\\" + q.key + " " + q.value + queryTag(q));
				}
			}
		}
		std::vector<std::string> roots;
		for (const char *name : { "ProgramFiles(x86)", "ProgramFiles", "ProgramW6432" })
		{
			std::string v = envOf(e, name);
			if (!v.empty())
				roots.push_back(v);
		}
		for (const std::string &d : e.drives)
		{
			for (const char *sub : { "Program Files (x86)", "Program Files", "Games", "" })
				roots.push_back(d + sub);
		}
		for (const char *game : { kRotwk, kBfme2 })
		{
			for (const std::string &root : roots)
			{
				for (const std::string &hint : folderHints(game))
				{
					fs::path p = fs::u8path(root);
					for (const std::string &part : splitWindowsPath(hint))
						p /= fs::u8path(part);
					c.add(game, p, "folder " + p.u8string() + kInferenceTag);
				}
			}
		}
		return c.out;
	}
	for (const std::string &prefix : winePrefixes(e))
	{
		discoverInPrefix(c, prefix, e.home);
	}
	return c.out;
}

InstallCheck checkInstall(const std::string &game, const std::string &root, const RetailArchivePolicy &policy, const RetailArchivePolicy *other)
{
	InstallCheck r;
	r.game = game;
	r.root = root;
	r.expected = (int)policy.archives.size();
	const std::string name = displayName(game) + " " + policy.patch;
	if (root.empty())
	{
		r.errors.push_back("no folder chosen for " + name);
		return r;
	}
	std::error_code ec;
	const fs::path base = fs::u8path(root);
	if (!fs::exists(base, ec))
	{
		r.errors.push_back("the folder does not exist: " + root);
		return r;
	}
	if (!fs::is_directory(base, ec))
	{
		r.errors.push_back("not a folder: " + root);
		return r;
	}
	// every *.big below the folder, by lower-case relative path (the mount's rule: Common/System/RetailArchivePolicy.cpp), down to
	// kMaxDepth folders: the policies' archives are at most one folder deep (apt/, lang/), and a pick like "/" or "C:\" must not walk a
	// whole disk. The mount itself still refuses an unknown archive at any depth. (depth() is 0 for the folder's own entries.)
	constexpr int kMaxDepth = 3;
	std::map<std::string, std::uint64_t> onDisk;
	for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
	{
		if (it.depth() >= kMaxDepth - 1)
			it.disable_recursion_pending(); // a/b/x.big is listed, a/b/c/ is not entered
		std::error_code entryError;
		if (!it->is_regular_file(entryError) || lower(it->path().extension().u8string()) != ".big")
			continue;
		onDisk[normalizedKey(fs::relative(it->path(), base, entryError).u8string())] = it->file_size(entryError);
	}
	if (ec)
	{
		r.errors.push_back("could not list " + root + ": " + ec.message());
		return r;
	}
	std::set<std::string> known;
	for (const RetailArchiveEntry &a : policy.archives)
	{
		const std::string k = normalizedKey(a.path);
		known.insert(k);
		auto f = onDisk.find(k);
		if (f == onDisk.end())
			r.missing.push_back(a.path);
		else if (f->second != a.size)
			r.wrongSize.push_back(a.path + " (" + std::to_string(f->second) + " bytes, " + policy.patch + " has " + std::to_string(a.size) + ")");
		else
			++r.present;
	}
	for (const std::string &x : policy.excluded)
		known.insert(normalizedKey(x));
	for (const auto &d : onDisk)
	{
		if (!known.count(d.first))
			r.unknown.push_back(d.first);
	}

	auto list = [](const std::vector<std::string> &v) {
		std::string s;
		for (size_t i = 0; i < v.size() && i < 6; ++i)
			s += (i ? ", " : "") + v[i];
		if (v.size() > 6)
			s += " and " + std::to_string(v.size() - 6) + " more";
		return s;
	};
	const bool complete = r.missing.empty() && r.wrongSize.empty() && r.unknown.empty();
	if (!complete && other)
	{
		// the two games share archive names, many with the same size (apt/*.big): a swapped pick is a folder that is exactly the other game
		InstallCheck o = checkInstall(game == kRotwk ? kBfme2 : kRotwk, root, *other, nullptr);
		if (o.ok)
		{
			r.errors.push_back("this folder holds " + displayName(o.game) + ", not " + displayName(game) + ": " + root);
			return r;
		}
	}
	if (r.present == 0 && r.wrongSize.empty())
	{
		r.errors.push_back("no " + name + " archives in " + root + " (expected " + (policy.archives.empty() ? std::string("?") : policy.archives.front().path) +
			" and " + std::to_string(policy.archives.size() > 0 ? policy.archives.size() - 1 : 0) + " more): pick the folder that holds the game's .big files");
		return r;
	}
	if (!r.missing.empty())
	{
		bool allLang = std::all_of(r.missing.begin(), r.missing.end(), [](const std::string &p) { return normalizedKey(p).compare(0, 5, "lang/") == 0; });
		r.errors.push_back("missing " + std::to_string(r.missing.size()) + " of " + std::to_string(r.expected) + " " + name + " archives: " + list(r.missing) +
			(allLang ? " (another language version? this build supports the English " + policy.patch + " archives)"
					 : " (an incomplete install, or not version " + policy.patch + ")"));
	}
	if (!r.wrongSize.empty())
	{
		r.errors.push_back(std::to_string(r.wrongSize.size()) + " archives differ from " + name + ": " + list(r.wrongSize) +
			" (another version or modified files: OpenBFME needs the unmodified " + policy.patch + " files)");
	}
	if (!r.unknown.empty())
	{
		r.errors.push_back(std::to_string(r.unknown.size()) + " archives in " + root + " are not part of " + name + ": " + list(r.unknown) +
			" (a mod or patch copied into the game folder; OpenBFME refuses to mount them)");
	}
	r.ok = r.errors.empty();
	return r;
}

std::vector<InstallCheck> checkPair(const std::string &rotwkRoot, const std::string &bfme2Root)
{
	RetailArchivePolicy rotwk, bfme2;
	std::string error;
	std::vector<InstallCheck> out(2);
	out[0].game = kRotwk;
	out[0].root = rotwkRoot;
	out[1].game = kBfme2;
	out[1].root = bfme2Root;
	if (!RetailArchivePolicy::loadBuiltin("rotwk-201", rotwk, &error) || !RetailArchivePolicy::loadBuiltin("bfme2-106", bfme2, &error))
	{
		out[0].errors.push_back(error);
		out[1].errors.push_back(error);
		return out;
	}
	out[0] = checkInstall(kRotwk, rotwkRoot, rotwk, &bfme2);
	out[1] = checkInstall(kBfme2, bfme2Root, bfme2, &rotwk);
	std::error_code e1, e2;
	if (!rotwkRoot.empty() && !bfme2Root.empty() && fs::exists(fs::u8path(rotwkRoot), e1) && fs::exists(fs::u8path(bfme2Root), e2) &&
		fs::equivalent(fs::u8path(rotwkRoot), fs::u8path(bfme2Root), e1))
	{
		const std::string msg = "the same folder was picked for both games: RotWK and BFME2 are two separate installs";
		for (InstallCheck &c : out)
		{
			c.errors.push_back(msg);
			c.ok = false;
		}
	}
	return out;
}

Configured configured(const std::string &configPath)
{
	Configured c;
	c.source = "none";
	const char *er = std::getenv("ROTWK_INSTALL");
	const char *eb = std::getenv("BFME2_INSTALL");
	if ((er && er[0]) || (eb && eb[0]))
	{
		// a developer / test override (ResolveInstallPath reads the environment first): the mount uses it as is
		c.source = "env";
	}
	std::string error;
	bool r = ResolveInstallPath("ROTWK_INSTALL", configPath, c.rotwk, &error);
	bool b = ResolveInstallPath("BFME2_INSTALL", configPath, c.bfme2, &error);
	if (c.source == "none" && r && b)
		c.source = "config";
	return c;
}

bool remember(const std::string &configPath, const std::string &rotwkRoot, const std::string &bfme2Root, std::string *error)
{
	auto fail = [error](const std::string &e) {
		if (error)
			*error = e;
		return false;
	};
	if (rotwkRoot.empty() || bfme2Root.empty() || rotwkRoot.find('\n') != std::string::npos || bfme2Root.find('\n') != std::string::npos)
		return fail("both folders are needed (one line each)");
	const fs::path target = fs::u8path(configPath);
	std::error_code ec;
	if (target.has_parent_path())
		fs::create_directories(target.parent_path(), ec);
	const fs::path temp = fs::u8path(configPath + ".tmp");
	{
		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		out << "# OpenBFME: the game folders chosen on the first start. Delete this file to choose again.\n";
		out << "ROTWK_INSTALL=" << rotwkRoot << "\n";
		out << "BFME2_INSTALL=" << bfme2Root << "\n";
		out.flush();
		if (!out)
			return fail("could not write " + temp.u8string());
	}
	fs::rename(temp, target, ec);
	if (ec)
	{
		fs::remove(temp, ec);
		return fail("could not replace " + configPath);
	}
	return true;
}

} // namespace InstallLocator
