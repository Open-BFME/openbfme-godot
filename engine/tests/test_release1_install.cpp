// OpenBFME unit tests. GPL-3.0.
// Lane RELEASE-1: finding the player's installs (Common/InstallLocator.h). Synthetic Wine prefixes, Steam libraries and archive folders
// (sparse files of the policy's sizes) in a temporary directory; the retail cases (the real installs, gi.dat, the binary's key strings) run
// when ROTWK_INSTALL / BFME2_INSTALL / RW_GAME_DAT are set and print SKIP otherwise.

#include "doctest.h"

#include "Common/InstallLocator.h"
#include "PeImage.h"
#include "RetailTestMount.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace InstallLocator;

namespace
{

struct TempDir
{
	fs::path path;
	explicit TempDir(const char *tag)
	{
		static int counter = 0;
		path = fs::temp_directory_path() / ("openbfme-release1-" + std::string(tag) + "-" + std::to_string(++counter) + "-" + std::to_string((long long)std::rand()));
		fs::remove_all(path);
		fs::create_directories(path);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

void writeFile(const fs::path &p, const std::string &text)
{
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary) << text;
}

void sparseFile(const fs::path &p, std::uintmax_t size)
{
	fs::create_directories(p.parent_path());
	std::ofstream(p, std::ios::binary).close();
	fs::resize_file(p, size);
}

// A tiny policy of the retail shape: two root archives, one in a sub folder, two language archives, one exclusion.
RetailArchivePolicy tinyPolicy(const char *game, const char *first, int scale = 1)
{
	auto size = [scale](int n) { return std::to_string(n * scale); };
	const std::string json = std::string(R"({"schema": "openbfme.retail-archive-policy", "schemaVersion": 1, "game": ")") + game +
		R"(", "patch": "9.99", "archives": [)"
		R"({"path": ")" + first + R"(", "md5": "00000000000000000000000000000001", "size": )" + size(1000) + R"(},)"
		R"({"path": "INI.big", "md5": "00000000000000000000000000000002", "size": )" + size(2000) + R"(},)"
		R"({"path": "apt/MainMenu.big", "md5": "00000000000000000000000000000003", "size": )" + size(3000) + R"(},)"
		R"({"path": "lang/English.big", "md5": "00000000000000000000000000000004", "size": )" + size(4000) + R"(},)"
		R"({"path": "lang/englishpatch.big", "md5": "00000000000000000000000000000005", "size": )" + size(5000) + R"(}]})";
	const std::string excl = std::string(R"({"schema": "openbfme.retail-archive-exclusions", "schemaVersion": 1, "game": ")") + game +
		R"(", "patch": "9.99", "archives": [{"path": "__patch202.big", "reason": "test"}]})";
	RetailArchivePolicy p;
	std::string error;
	REQUIRE_MESSAGE(RetailArchivePolicy::parse(json, excl, p, &error), error);
	return p;
}

void fillInstall(const fs::path &root, const RetailArchivePolicy &policy)
{
	for (const RetailArchiveEntry &a : policy.archives)
		sparseFile(root / fs::u8path(a.path), a.size);
}

struct EnvGuard
{
	std::string name, old;
	bool had = false;
	EnvGuard(const char *n, const char *value) : name(n)
	{
		const char *v = std::getenv(n);
		had = v != nullptr;
		old = v ? v : "";
		set(value);
	}
	void set(const char *value)
	{
#ifdef _WIN32
		_putenv_s(name.c_str(), value ? value : "");
#else
		if (value)
			setenv(name.c_str(), value, 1);
		else
			unsetenv(name.c_str());
#endif
	}
	~EnvGuard() { set(had ? old.c_str() : nullptr); }
};

std::string wideAt(const retailtest::PeImage &pe, std::uint32_t va)
{
	std::string s;
	for (std::uint32_t i = 0; i < 400; i += 2)
	{
		std::vector<std::uint8_t> b;
		if (!pe.read(va + i, 2, &b) || (b[0] == 0 && b[1] == 0))
			break;
		s += (char)b[0];
	}
	return s;
}

} // namespace

TEST_CASE("release1: Wine registry values (escapes, sections, str(2), case)")
{
	const std::string reg =
		"WINE REGISTRY Version 2\n"
		";; All keys relative to \\\\Machine\n\n"
		"[Software\\\\Wow6432Node\\\\Electronic Arts\\\\Electronic Arts\\\\The Lord of the Rings, The Rise of the Witch-king] 1700000000\n"
		"#time=1d9b0d2c1a2b3c4\n"
		"\"CacheSize\"=dword:00000000\n"
		"\"InstallPath\"=\"C:\\\\Program Files (x86)\\\\EA Games\\\\Witch \\\"king\\\"\\\\\"\n"
		"\n"
		"[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\App Paths\\\\lotrbfme2.exe] 1700000000\n"
		"@=\"C:\\\\x\\\\lotrbfme2.exe\"\n"
		"\"Path\"=str(2):\"D:\\\\Games\\\\B\\xe4r\\\\\"\n";
	std::string v;
	CHECK(wineRegistryValue(reg, "software\\wow6432node\\electronic arts\\Electronic Arts\\The Lord of the Rings, The Rise of the Witch-king", "installpath", v));
	CHECK(v == "C:\\Program Files (x86)\\EA Games\\Witch \"king\"\\");
	CHECK(wineRegistryValue(reg, "Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\lotrbfme2.exe", "Path", v));
	CHECK(v == "D:\\Games\\B\xC3\xA4r\\"); // \xe4 = U+00E4 as UTF-8
	CHECK_FALSE(wineRegistryValue(reg, "Software\\Electronic Arts\\Electronic Arts\\The Lord of the Rings, The Rise of the Witch-king", "InstallPath", v));
	CHECK_FALSE(wineRegistryValue(reg, "Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\lotrbfme2.exe", "CacheSize", v));
}

TEST_CASE("release1: Windows paths inside a Wine prefix")
{
	TempDir t("prefix");
	const fs::path prefix = t.path / "pfx";
	fs::create_directories(prefix / "drive_c" / "Program Files (x86)" / "EA GAMES" / "Game");
	// no dosdevices: C: is drive_c; other drives are unmapped
	CHECK(fs::path(wineToHost(prefix.u8string(), "C:\\Program Files (x86)\\EA Games\\game")) == prefix / "drive_c" / "Program Files (x86)" / "EA GAMES" / "Game");
	CHECK(wineToHost(prefix.u8string(), "D:\\x").empty());
	CHECK(wineToHost(prefix.u8string(), "relative\\x").empty());
	// dosdevices/d: -> another folder (Wine's drive mapping; Z: is "/" by default)
	fs::create_directories(t.path / "other" / "Games");
	fs::create_directories(prefix / "dosdevices");
	std::error_code ec;
	fs::create_directory_symlink(t.path / "other", prefix / "dosdevices" / "d:", ec);
	if (ec)
	{
		MESSAGE("SKIP dosdevices part: symlinks unavailable here (" << ec.message() << ")");
		return;
	}
	CHECK(fs::path(wineToHost(prefix.u8string(), "d:\\games\\")) == fs::canonical(t.path / "other") / "Games");
}

TEST_CASE("release1: discovery in Wine, Proton and Lutris prefixes (Linux)")
{
	TempDir t("discover");
	const fs::path home = t.path / "home";
	// 1. ~/.wine, a 64-bit prefix: RotWK's GameRegPath in the 32-bit view, the folder under drive_c
	const fs::path wine = home / ".wine";
	fs::create_directories(wine / "drive_c" / "Program Files (x86)" / "EA Games" / "RotWK here");
	writeFile(wine / "system.reg",
		"WINE REGISTRY Version 2\n"
		"[Software\\\\Wow6432Node\\\\Electronic Arts\\\\Electronic Arts\\\\The Lord of the Rings, The Rise of the Witch-king] 1\n"
		"\"InstallPath\"=\"C:\\\\Program Files (x86)\\\\EA Games\\\\RotWK here\\\\\"\n");
	writeFile(wine / "user.reg", "WINE REGISTRY Version 2\n");
	// 2. a Proton prefix in a second Steam library (libraryfolders.vdf), BFME2 through App Paths, plus the default folder hint
	const fs::path library = t.path / "sdcard";
	writeFile(home / ".local/share/Steam/steamapps/libraryfolders.vdf",
		"\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + home.u8string() + "/.local/share/Steam\"\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + library.u8string() + "\"\n\t}\n}\n");
	const fs::path proton = library / "steamapps/compatdata/123456/pfx";
	fs::create_directories(proton / "drive_c" / "Games" / "BFME2");
	fs::create_directories(proton / "drive_c" / "Program Files" / "EA Games" / "The Battle for Middle-earth (tm) II");
	writeFile(proton / "system.reg",
		"WINE REGISTRY Version 2\n"
		"[Software\\\\Wow6432Node\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\App Paths\\\\lotrbfme2.exe] 1\n"
		"\"Path\"=\"C:\\\\Games\\\\BFME2\"\n");
	// 3. a Lutris prefix (~/Games/<name>) whose 32-bit registry has RotWK in HKCU, pointing at a folder that does not exist: not offered
	writeFile(home / "Games/bfme/system.reg", "WINE REGISTRY Version 2\n");
	writeFile(home / "Games/bfme/user.reg",
		"WINE REGISTRY Version 2\n[Software\\\\Electronic Arts\\\\Electronic Arts\\\\The Lord of the Rings, The Rise of the Witch-king] 1\n"
		"\"InstallPath\"=\"C:\\\\gone\"\n");
	// 4. a folder without system.reg is no prefix
	fs::create_directories(home / "Games/notaprefix/drive_c");

	DiscoveryEnvironment env;
	env.windows = false;
	env.home = home.u8string();
	const std::vector<std::string> prefixes = winePrefixes(env);
	REQUIRE(prefixes.size() == 3);
	CHECK(fs::path(prefixes[0]) == fs::canonical(wine));
	CHECK(fs::path(prefixes[1]) == fs::canonical(proton));
	CHECK(fs::path(prefixes[2]) == fs::canonical(home / "Games/bfme"));

	const std::vector<Candidate> found = discover(env);
	REQUIRE(found.size() == 3);
	CHECK(found[0].game == kRotwk);
	CHECK(fs::path(found[0].path) == fs::canonical(wine / "drive_c/Program Files (x86)/EA Games/RotWK here"));
	CHECK(found[0].source == "Wine prefix ~/.wine: registry HKLM\\Software\\Wow6432Node\\Electronic Arts\\Electronic Arts\\The Lord of the Rings, The Rise of the Witch-king InstallPath");
	CHECK(found[1].game == kBfme2);
	CHECK(fs::path(found[1].path) == fs::canonical(proton / "drive_c/Games/BFME2"));
	CHECK(found[1].source.find("App Paths\\lotrbfme2.exe Path") != std::string::npos);
	CHECK(found[2].game == kBfme2);
	CHECK(found[2].source.find("folder C:\\Program Files\\EA Games\\The Battle for Middle-earth (tm) II [inference, S-1560]") != std::string::npos);
	// target facts carry no tag (S-1560 marks only the inferred hints)
	CHECK(found[0].source.find("S-1560") == std::string::npos);
	CHECK(found[1].source.find("S-1560") == std::string::npos);

	// $WINEPREFIX comes first
	env.env["WINEPREFIX"] = (home / "Games/bfme").u8string();
	CHECK(fs::path(winePrefixes(env)[0]) == fs::canonical(home / "Games/bfme"));
}

TEST_CASE("release1: discovery on Windows (registry first, then the folder hints)")
{
	TempDir t("windows");
	const fs::path regRotwk = t.path / "anywhere" / "RotWK";
	const fs::path hinted = t.path / "D" / "Games" / "EA Games" / "The Battle for Middle-earth II";
	fs::create_directories(regRotwk);
	fs::create_directories(hinted);
	DiscoveryEnvironment env;
	env.windows = true;
	env.drives = { (t.path / "D").u8string() + "/" };
	std::vector<std::string> asked;
	env.registry = [&](const RegistryQuery &q, std::string &out) {
		asked.push_back(q.hive + " " + q.value);
		if (q.hive == "HKCU" && q.value == "InstallPath" && q.key.find("Witch-king") != std::string::npos)
		{
			out = regRotwk.u8string() + "\\";
			return true;
		}
		return false;
	};
	const std::vector<Candidate> found = discover(env);
	REQUIRE(found.size() == 2);
	CHECK(found[0].game == kRotwk);
	CHECK(found[0].source.find("registry HKCU\\SOFTWARE\\Electronic Arts\\Electronic Arts\\The Lord of the Rings, The Rise of the Witch-king InstallPath") == 0);
	CHECK(found[1].game == kBfme2);
	CHECK(found[1].source.find("folder ") == 0);
	CHECK(found[1].source.find("[inference, S-1560]") != std::string::npos);
	// the registry rows that are inference (S-1560): RotWK's App Paths entry, BFME2's GameRegPath in HKLM and HKCU
	int inferred = 0;
	for (const char *game : { kRotwk, kBfme2 })
		for (const RegistryQuery &q : registryQueries(game))
			inferred += q.why.compare(0, 9, "inference") == 0 ? 1 : 0;
	CHECK(inferred == 3);
	// RW 0x64148E: HKLM before HKCU; RW 0x638D22: BFME2 through App Paths first
	REQUIRE(asked.size() == 6);
	CHECK(asked[0] == "HKLM InstallPath");
	CHECK(asked[1] == "HKCU InstallPath");
	CHECK(asked[3] == "HKLM Path");
}

TEST_CASE("release1: checking a picked folder")
{
	TempDir t("check");
	const RetailArchivePolicy rotwk = tinyPolicy("rotwk", "_patch.big");
	const RetailArchivePolicy bfme2 = tinyPolicy("bfme2", "_patchb.big", 3); // same names, other sizes (like the real pair)
	const fs::path good = t.path / "good";
	fillInstall(good, rotwk);
	// a lower-case copy is the same install (the mount matches case-insensitively)
	fs::rename(good / "INI.big", good / "ini.big");
	InstallCheck c = checkInstall(kRotwk, good.u8string(), rotwk, &bfme2);
	CHECK_MESSAGE(c.ok, (c.errors.empty() ? std::string() : c.errors[0]));
	CHECK(c.present == 5);
	CHECK(c.expected == 5);
	// a known exclusion (2.02) is allowed next to the archives; the mount skips it
	sparseFile(good / "__patch202.big", 10);
	CHECK(checkInstall(kRotwk, good.u8string(), rotwk, &bfme2).ok);

	SUBCASE("missing folder, file instead of folder, empty pick")
	{
		InstallCheck m = checkInstall(kRotwk, (t.path / "nope").u8string(), rotwk, &bfme2);
		REQUIRE(m.errors.size() == 1);
		CHECK(m.errors[0].find("the folder does not exist") == 0);
		CHECK(checkInstall(kRotwk, (good / "ini.big").u8string(), rotwk, &bfme2).errors[0].find("not a folder") == 0);
		CHECK(checkInstall(kRotwk, "", rotwk, &bfme2).errors[0] == "no folder chosen for The Rise of the Witch-king 9.99");
	}
	SUBCASE("an unrelated folder names an archive it expected")
	{
		fs::create_directories(t.path / "docs");
		InstallCheck m = checkInstall(kRotwk, (t.path / "docs").u8string(), rotwk, &bfme2);
		REQUIRE(m.errors.size() == 1);
		CHECK(m.errors[0].find("no The Rise of the Witch-king 9.99 archives in") == 0);
		CHECK(m.errors[0].find("expected _patch.big and 4 more") != std::string::npos);
	}
	SUBCASE("the other game's folder")
	{
		const fs::path other = t.path / "bfme2";
		fillInstall(other, bfme2);
		InstallCheck m = checkInstall(kRotwk, other.u8string(), rotwk, &bfme2);
		REQUIRE(m.errors.size() == 1);
		CHECK(m.errors[0] == "this folder holds The Battle for Middle-earth II, not The Rise of the Witch-king: " + other.u8string());
		CHECK(checkInstall(kBfme2, other.u8string(), bfme2, &rotwk).ok);
	}
	SUBCASE("missing, wrong size, unknown archive")
	{
		fs::remove(good / "apt" / "MainMenu.big");
		fs::resize_file(good / "_patch.big", 999);
		sparseFile(good / "MyMod.big", 1);
		InstallCheck m = checkInstall(kRotwk, good.u8string(), rotwk, &bfme2);
		CHECK_FALSE(m.ok);
		REQUIRE(m.errors.size() == 3);
		CHECK(m.errors[0] == "missing 1 of 5 The Rise of the Witch-king 9.99 archives: apt/MainMenu.big (an incomplete install, or not version 9.99)");
		CHECK(m.errors[1] == "1 archives differ from The Rise of the Witch-king 9.99: _patch.big (999 bytes, 9.99 has 1000) (another version or modified "
							 "files: OpenBFME needs the unmodified 9.99 files)");
		CHECK(m.errors[2].find("archives in") != std::string::npos);
		CHECK(m.errors[2].find("are not part of The Rise of the Witch-king 9.99: mymod.big") != std::string::npos);
	}
	SUBCASE("archives deeper than three folders are not looked for (a pick of a disk root stays cheap)")
	{
		sparseFile(good / "a" / "b" / "c" / "deep.big", 1);
		CHECK(checkInstall(kRotwk, good.u8string(), rotwk, &bfme2).ok);
		sparseFile(good / "a" / "b" / "shallow.big", 1);
		InstallCheck m = checkInstall(kRotwk, good.u8string(), rotwk, &bfme2);
		REQUIRE(m.unknown.size() == 1);
		CHECK(m.unknown[0] == "a/b/shallow.big");
	}
	SUBCASE("another language")
	{
		fs::remove(good / "lang" / "English.big");
		fs::remove(good / "lang" / "englishpatch.big");
		sparseFile(good / "lang" / "German.big", 7);
		InstallCheck m = checkInstall(kRotwk, good.u8string(), rotwk, &bfme2);
		REQUIRE(m.errors.size() >= 1);
		CHECK(m.errors[0].find("(another language version? this build supports the English 9.99 archives)") != std::string::npos);
	}
}

TEST_CASE("release1: remembering the pick")
{
	TempDir t("remember");
	EnvGuard r("ROTWK_INSTALL", nullptr);
	EnvGuard b("BFME2_INSTALL", nullptr);
	const std::string cfg = (t.path / "user" / "install-paths.cfg").u8string();
	CHECK(configured(cfg).source == "none");
	std::string error;
	CHECK_FALSE(remember(cfg, "", "x", &error));
	CHECK_FALSE(remember(cfg, "a\nROTWK_INSTALL=evil", "x", &error));
	REQUIRE(remember(cfg, "/games/rotwk with space", "/games/bfme2", &error));
	Configured c = configured(cfg);
	CHECK(c.source == "config");
	CHECK(c.rotwk == "/games/rotwk with space");
	CHECK(c.bfme2 == "/games/bfme2");
	REQUIRE(remember(cfg, "/elsewhere", "/games/bfme2", &error)); // replaced, not appended
	CHECK(configured(cfg).rotwk == "/elsewhere");
	CHECK_FALSE(fs::exists(cfg + ".tmp"));
	// the environment is a developer override and wins
	r.set("/env/rotwk");
	CHECK(configured(cfg).source == "env");
	CHECK(configured(cfg).rotwk == "/env/rotwk");
}

TEST_CASE("release1: the registry names match retail (gi.dat and the binary)")
{
	const std::string rotwkDir = std::getenv("ROTWK_INSTALL") ? std::getenv("ROTWK_INSTALL") : "";
	const std::string bfme2Dir = retailtest::bfme2Install();
	if (rotwkDir.empty() || bfme2Dir.empty())
	{
		retailtest::printSkip("release1: gi.dat GameRegPath");
	}
	else
	{
		// gi.dat: "GI  ", count, then NUL-terminated key / value pairs (RW 0xAAA6C0)
		auto gameRegPath = [](const std::string &dir) {
			std::ifstream in(fs::u8path(dir) / "gi.dat", std::ios::binary);
			std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			REQUIRE(data.size() > 8);
			REQUIRE(data.compare(0, 4, "GI  ") == 0);
			std::vector<std::string> parts;
			size_t p = 8;
			while (p < data.size())
			{
				size_t e = data.find('\0', p);
				if (e == std::string::npos)
					break;
				parts.push_back(data.substr(p, e - p));
				p = e + 1;
			}
			for (size_t i = 0; i + 1 < parts.size(); i += 2)
				if (parts[i] == "GameRegPath")
					return parts[i + 1];
			return std::string();
		};
		CHECK(gameRegPath(rotwkDir) == registryQueries(kRotwk)[0].key);
		CHECK(gameRegPath(bfme2Dir) == registryQueries(kBfme2)[1].key);
	}
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("release1: registry strings in game.dat (RW_GAME_DAT)");
		return;
	}
	CHECK(wideAt(*pe, 0xBE8368) == registryQueries(kBfme2)[0].key); // RW 0x638D49 push 0xBE8368 -> RegOpenKeyExW(HKLM, ...)
	CHECK(wideAt(*pe, 0xBE835C) == registryQueries(kBfme2)[0].value); // L"Path"
	CHECK(pe->cstring(0xC850F4) == registryQueries(kRotwk)[0].value); // "InstallPath", RW 0x9787FE
	CHECK(pe->u32At(0x638D4F) == 0x80000002u);                         // HKEY_LOCAL_MACHINE
}

TEST_CASE("release1: the real installs pass the check, swapped they are named")
{
	const std::string rotwkDir = std::getenv("ROTWK_INSTALL") ? std::getenv("ROTWK_INSTALL") : "";
	const std::string bfme2Dir = retailtest::bfme2Install();
	if (rotwkDir.empty() || bfme2Dir.empty())
	{
		retailtest::printSkip("release1: real installs");
		return;
	}
	std::vector<InstallCheck> ok = checkPair(rotwkDir, bfme2Dir);
	REQUIRE(ok.size() == 2);
	CHECK_MESSAGE(ok[0].ok, (ok[0].errors.empty() ? std::string() : ok[0].errors[0]));
	CHECK_MESSAGE(ok[1].ok, (ok[1].errors.empty() ? std::string() : ok[1].errors[0]));
	CHECK(ok[0].present == ok[0].expected);
	std::vector<InstallCheck> swapped = checkPair(bfme2Dir, rotwkDir);
	CHECK_FALSE(swapped[0].ok);
	CHECK_FALSE(swapped[1].ok);
	REQUIRE_FALSE(swapped[0].errors.empty());
	CHECK(swapped[0].errors[0] == "this folder holds The Battle for Middle-earth II, not The Rise of the Witch-king: " + bfme2Dir);
	CHECK(swapped[1].errors[0] == "this folder holds The Rise of the Witch-king, not The Battle for Middle-earth II: " + rotwkDir);
	std::vector<InstallCheck> same = checkPair(rotwkDir, rotwkDir);
	CHECK(same[0].errors.back() == "the same folder was picked for both games: RotWK and BFME2 are two separate installs");
}
