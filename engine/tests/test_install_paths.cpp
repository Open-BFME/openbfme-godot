// OpenBFME unit tests. GPL-3.0. Every archive is generated; no game files are used.
#include "doctest.h"
#include "BigTestUtil.h"
#include "Common/ArchiveFile.h"
#include "Common/InstallLocator.h"
#include "Common/LogPrivacy.h"
#include "Common/LooseFileScan.h"
#include "Common/MD5.h"
#include "Common/PathIO.h"
#include "Common/Recorder.h"
#include "Common/RetailArchivePolicy.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameClient/OptionPreferences.h"
#include "GameClient/CreateAHeroStats.h"
#include "GameClient/CreateAHeroHeroList.h"
#include "GameClient/GUI/AptScreens/AptMainMenu.h"
#include "GameClient/GUI/Skirmish/SkirmishPreferences.h"
#include "GameClient/VideoPlayer.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace
{
struct TempDir
{
	fs::path path = fs::temp_directory_path() / ("path-io-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	TempDir() { fs::create_directories(path); }
	~TempDir() { std::error_code ec; fs::remove_all(PathIO::native(path.u8string()), ec); }
};

void setEnv(const char *key, const std::string &value)
{
#ifdef _WIN32
	_wputenv_s(PathIO::widen(key).c_str(), PathIO::widen(value).c_str());
#else
	if (value.empty()) unsetenv(key); else setenv(key, value.c_str(), 1);
#endif
}
struct EnvGuard
{
	std::string key, old;
	EnvGuard(const char *k, const std::string &v) : key(k), old(PathIO::environment(k)) { setEnv(k, v); }
	~EnvGuard() { setEnv(key.c_str(), old); }
};

std::vector<std::string> cases()
{
	std::vector<std::string> out = {
		"Program Files (x86)/EA Games/The Lord of the Rings, The Rise of the Witch-king",
		u8"players/Jürgen/Games", u8"players/Анна/Games", u8"players/玩家/Games",
		u8"OneDrive - Example/Документы/Games", "Applications/Game With Spaces.app/Contents/Resources",
		"leading and trailing/ leading folder", "punctuation/EA & Games (test)=one;two"
	};
	std::string longPath = "long";
	for (int i = 0; i != 7; ++i) longPath += "/" + std::string(45, 'x');
	out.push_back(longPath);
#ifndef _WIN32
	out.push_back("trailing dot. /folder. ");
#endif
	return out;
}

void write(const fs::path &p, const std::vector<std::uint8_t> &bytes)
{
	fs::create_directories(PathIO::native(p.parent_path().u8string()));
	std::ofstream out(PathIO::native(p.u8string()), std::ios::binary);
	out.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize)bytes.size());
	out.close();
	REQUIRE(out.good());
}

void mountReadRemember(const fs::path &folder)
{
	const auto bytes = bigtest::makeBig("BIG4", {{"data/path.txt", "synthetic path payload"}});
	write(folder / "Tiny.big", bytes);
	RetailArchivePolicy policy;
	policy.game = "rotwk";
	policy.patch = "synthetic";
	RetailArchiveEntry entry;
	entry.path = "Tiny.big";
	entry.size = bytes.size();
	entry.md5 = MD5::ofBytes(bytes.data(), bytes.size());
	policy.archives.push_back(entry);
	const std::string root = folder.u8string();
	CAPTURE(root);
	const auto check = InstallLocator::checkInstall("rotwk", root, policy, nullptr);
	REQUIRE_MESSAGE(check.ok, (check.errors.empty() ? "" : check.errors.front()));
	Win32BIGFileSystem archives;
	RetailMountOptions options;
	options.md5CachePath = (folder / "cache/md5.json").u8string();
	const auto mounted = MountRetailArchives(archives, {{"rotwk", root, policy}}, options);
	REQUIRE_MESSAGE(mounted.ok, (mounted.errors.empty() ? "" : mounted.errors.front()));
	CHECK(mounted.mounted.size() == 1);
	CHECK(mounted.archivesHashed == 1);
	std::vector<std::uint8_t> read;
	std::string error;
	REQUIRE_MESSAGE(archives.readFile("DATA/PATH.TXT", read, &error), error);
	CHECK(bigtest::asString(read) == "synthetic path payload");
	std::vector<LooseFileFinding> loose;
	REQUIRE_MESSAGE(LooseFileScan::scanInstalls({{"rotwk", root}}, loose, &error), error);
	CHECK(loose.empty());
	const auto config = (folder / u8"設定/install-paths.cfg").u8string();
	REQUIRE_MESSAGE(InstallLocator::remember(config, root, root + "/second", &error), error);
	const auto remembered = InstallLocator::configured(config);
	CHECK(remembered.source == "config");
	CHECK(remembered.rotwk == root);
	CHECK(remembered.bfme2 == root + "/second");
	REQUIRE(InstallLocator::remember(config, root, root + "/replacement", &error));
	CHECK(InstallLocator::configured(config).bfme2 == root + "/replacement");
	OptionPreferences prefs;
	prefs.set("Resolution", "1280 720");
	const auto ini = (folder / u8"設定/Options.ini").u8string();
	REQUIRE_MESSAGE(prefs.save(ini, &error), error);
	OptionPreferences loaded;
	REQUIRE_MESSAGE(loaded.load(ini, &error), error);
	CHECK(loaded.text() == prefs.text());
	const auto log = (folder / "session.log").u8string();
	auto *f = PathIO::open(log, "wb");
	REQUIRE(f != nullptr);
	CHECK(std::fwrite("synthetic log\n", 1, 14, f) == 14);
	CHECK(std::fclose(f) == 0);
	CHECK(fs::file_size(PathIO::native(log)) == 14);
}
}

TEST_CASE("install paths: synthetic disk BIG mounts, remembers and reads every difficult folder")
{
	TempDir temp;
	EnvGuard r("ROTWK_INSTALL", ""), b("BFME2_INSTALL", "");
	for (const auto &name : cases()) mountReadRemember(temp.path / fs::u8path(name));
}

TEST_CASE("install paths: Linux symlinked install and archive retain their identity")
{
#ifndef _WIN32
	TempDir temp;
	EnvGuard r("ROTWK_INSTALL", ""), b("BFME2_INSTALL", "");
	fs::create_directories(temp.path / u8"real Анна");
	fs::create_directory_symlink(temp.path / u8"real Анна", temp.path / "chosen link");
	mountReadRemember(temp.path / "chosen link");
	fs::rename(temp.path / u8"real Анна/Tiny.big", temp.path / "outside.big");
	fs::create_symlink(temp.path / "outside.big", temp.path / u8"real Анна/Tiny.big");
	Win32BIGFileSystem archives;
	std::string error;
	auto a = archives.openArchiveFile((temp.path / "chosen link/Tiny.big").u8string(), &error);
	REQUIRE_MESSAGE(a != nullptr, error);
	std::vector<std::uint8_t> read;
	REQUIRE(a->readFile("data/path.txt", read, &error));
	CHECK(bigtest::asString(read) == "synthetic path payload");
#endif
}

TEST_CASE("install paths: UTF-8 environment overrides and privacy agree")
{
	for (const std::string name : {u8"Jürgen", u8"Анна", u8"玩家"})
	{
		const std::string home = "/srv/players/" + name;
		EnvGuard user("USERNAME", name), profile("USERPROFILE", home), unixUser("USER", name), unixHome("HOME", home);
		EnvGuard r("ROTWK_INSTALL", home + "/Games"), b("BFME2_INSTALL", home + "/Base");
		const auto c = InstallLocator::configured("");
		CHECK(c.rotwk == home + "/Games");
		CHECK(c.bfme2 == home + "/Base");
		CHECK(c.source == "env");
		const auto rules = LogPrivacy::hostRules();
		CHECK(rules.user == name);
		CHECK(rules.home == home);
		CHECK(LogPrivacy::redact(home + "/OneDrive/Documents user=" + name + " /media/" + name + "/Game", rules) ==
			"<user>/OneDrive/Documents user=<user> /media/<user>/Game");
	}
}

TEST_CASE("install paths: invalid folders are named and cannot corrupt a remembered config")
{
	TempDir temp;
	EnvGuard r("ROTWK_INSTALL", ""), b("BFME2_INSTALL", "");
	const auto cfg = (temp.path / "install-paths.cfg").u8string();
	std::string error;
	REQUIRE(InstallLocator::remember(cfg, "one", "two", &error));
	for (const std::string bad : {std::string("bad\nfolder"), std::string("bad\rfolder"), std::string("bad\0folder", 10)})
	{
		CHECK_FALSE(InstallLocator::remember(cfg, bad, "two", &error));
		CHECK(error.find(bad) != std::string::npos);
	}
	CHECK(InstallLocator::configured(cfg).rotwk == "one");
	REQUIRE(InstallLocator::remember(cfg, " leading root", " second root", &error));
	CHECK(InstallLocator::configured(cfg).rotwk == "leading root");
	CHECK(InstallLocator::configured(cfg).bfme2 == "second root");
	CHECK_FALSE(InstallLocator::remember((temp.path / "install-paths.cfg/child.cfg").u8string(), "one", "two", &error));
	CHECK(error.find("child.cfg") != std::string::npos);
	RetailArchivePolicy policy;
	const auto missing = (temp.path / u8"missing 玩家").u8string();
	const auto check = InstallLocator::checkInstall("rotwk", missing, policy, nullptr);
	CHECK_FALSE(check.ok);
	REQUIRE(check.errors.size() == 1);
	CHECK(check.errors.front().find(missing) != std::string::npos);
}

TEST_CASE("install paths: dynamically sized Unicode environment values are lossless")
{
	std::string value;
	for (int i = 0; i != 1500; ++i) value += u8"玩家";
	EnvGuard valueGuard("OPENBFME_SYNTHETIC_PATH_TEST", value);
	CHECK(PathIO::environment("OPENBFME_SYNTHETIC_PATH_TEST") == value);
}

TEST_CASE("install paths: legacy config padding is ignored but internal and trailing folder spaces survive")
{
	TempDir temp;
	EnvGuard r("ROTWK_INSTALL", ""), b("BFME2_INSTALL", "");
	const auto cfg = (temp.path / u8"設定.cfg").u8string();
	{
		std::ofstream out(PathIO::native(cfg), std::ios::binary);
		out << "ROTWK_INSTALL \t= \t C:/players/Jürgen/Game With Spaces \r\n"
		       "BFME2_INSTALL=\t  C:/players/Анна/Base Game\r\n"
		       "EMPTY_INSTALL = \t \r\n";
		REQUIRE(out.good());
	}
	const auto configured = InstallLocator::configured(cfg);
	CHECK(configured.rotwk == u8"C:/players/Jürgen/Game With Spaces ");
	CHECK(configured.bfme2 == u8"C:/players/Анна/Base Game");
	std::string value, error;
	CHECK_FALSE(ResolveInstallPath("EMPTY_INSTALL", cfg, value, &error));
	CHECK(error.find(cfg) != std::string::npos);
	EnvGuard override("ROTWK_INSTALL", u8" C:/players/玩家/override ");
	REQUIRE(ResolveInstallPath("ROTWK_INSTALL", cfg, value, &error));
	CHECK(value == u8" C:/players/玩家/override ");
}

TEST_CASE("install paths: loose file scans preserve Unicode names and report the selected missing root")
{
	TempDir temp;
	for (const auto &name : cases())
	{
		const auto root = temp.path / fs::u8path(name);
		CAPTURE(name);
		const std::string relative = u8"Maps/Анна Jürgen 玩家.MAP";
		write(root / fs::u8path(relative), {});
		std::vector<LooseFile> loose;
		std::string error;
		REQUIRE_MESSAGE(LooseFileScan::findLooseMaps(root.u8string(), loose, &error), error);
		REQUIRE(loose.size() == 1);
		CHECK(loose.front().relativePath == relative);
		CHECK(loose.front().size == 0);
		std::vector<LooseFileFinding> findings;
		REQUIRE_MESSAGE(LooseFileScan::scanInstalls({{"rotwk", root.u8string()}}, findings, &error), error);
		REQUIRE(findings.size() == 1);
		CHECK(findings.front().line.find(relative) != std::string::npos);
		const auto missing = (root / u8"missing 玩家").u8string();
		CHECK_FALSE(LooseFileScan::scanInstalls({{"rotwk", missing}}, findings, &error));
		CHECK(findings.empty());
		CHECK(error.find(missing) != std::string::npos);
	}
}

TEST_CASE("install paths: profile statistics, hero files, skirmish preferences and continue probe use native paths")
{
	TempDir temp;
	for (const auto &name : cases())
	{
		const auto folder = temp.path / fs::u8path(name);
		const auto root = folder.u8string();
		std::string error;
		CreateAHeroStats stats;
		stats.add("wins", 7);
		REQUIRE_MESSAGE(stats.save(root, "ABC123", &error), error);
		stats.add("wins");
		REQUIRE_MESSAGE(stats.save(root, "ABC123", &error), error);
		CreateAHeroStats loaded;
		REQUIRE_MESSAGE(loaded.load(root, "ABC123", &error), error);
		CHECK(loaded.get("wins") == 8);
		CreateAHeroHeroList heroes;
		std::vector<std::string> errors;
		heroes.load(nullptr, root, &errors);
		REQUIRE(errors.empty());
		CreateAHeroHero hero;
		hero.uniqueID = "ABC123";
		hero.name = u"Synthetic";
		REQUIRE_MESSAGE(heroes.save(hero, &error), error);
		CreateAHeroHeroList reread;
		reread.load(nullptr, root, &errors);
		REQUIRE(errors.empty());
		REQUIRE(reread.size() == 1);
		CHECK(reread.at(0)->hero.name == hero.name);
		REQUIRE_MESSAGE(reread.remove(0, &error), error);
		CHECK_FALSE(fs::exists(PathIO::native((folder / "MyHero_ABC123.cah").u8string())));
		const auto ini = (folder / "Skirmish.ini").u8string();
		SkirmishPreferences prefs(ini);
		REQUIRE(prefs.errors().empty());
		REQUIRE(prefs.add(u"Synthetic"));
		REQUIRE(prefs.setCurrent(u"Synthetic"));
		REQUIRE(prefs.errors().empty());
		SkirmishPreferences remembered(ini);
		CHECK(remembered.current() == u"Synthetic");
		CHECK(remembered.errors().empty());
		CHECK(fs::equivalent(PathIO::native(remembered.folder()), PathIO::native(root)));
		CHECK_FALSE(AptMainMenu::continueCampaignSaveExists(root));
		write(folder / "00000000.sav", {});
		CHECK(AptMainMenu::continueCampaignSaveExists(root));
	}
}

TEST_CASE("install paths: replay and save disk I/O preserve UTF-8 and long folders")
{
	TempDir temp;
	for (const auto &name : cases())
	{
		const auto folder = temp.path / fs::u8path(name);
		fs::create_directories(PathIO::native(folder.u8string()));
		const auto path = (folder / "game.rep").u8string();
		ReplayWriter writer;
		ReplayHeader header;
		std::string error;
		REQUIRE_MESSAGE(writer.open(path, header, &error), error);
		writer.close(0, 0);
		ReplayFile replay;
		REQUIRE_MESSAGE(ReplayFile::load(path, replay, &error), error);
		CHECK(replay.complete);
		CHECK(replay.finalFrame == 0);
		SaveGameFile save;
		const auto savePath = (folder / "game.sav").u8string();
		REQUIRE_MESSAGE(save.write(savePath, &error), error);
		CHECK(fs::exists(PathIO::native(savePath)));
		std::ifstream in(PathIO::native(savePath), std::ios::binary);
		std::vector<std::uint8_t> read((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		CHECK(read == save.encode());
	}
}

TEST_CASE("install paths: process argument boundary preserves UTF-8, lifetime and the null sentinel")
{
	char executable[] = "path-boundary";
	char path[] = u8"D:\\players\\Jürgen\\Анна\\玩家\\😀\\Game With Spaces";
	char *args[] = {executable, path, nullptr};
	const int result = PathIO::runMain(2, args, [](int argc, char **argv) {
#ifdef _WIN32
		const auto expected = PathIO::windowsArguments(GetCommandLineW());
#else
		const std::vector<std::string> expected = {"path-boundary", u8"D:\\players\\Jürgen\\Анна\\玩家\\😀\\Game With Spaces"};
#endif
		CHECK(argc == (int)expected.size());
		CHECK(argv[argc] == nullptr);
		std::vector<std::string> actual;
		for (int i = 0; i < argc; ++i) actual.emplace_back(argv[i]);
		CHECK(actual == expected);
		return 37;
	});
	CHECK(result == 37);
}

#ifdef _WIN32
TEST_CASE("install paths: Windows UTF-8 and UTF-16 conversion is strict and lossless")
{
	for (const std::string &path : std::vector<std::string>{u8"D:\\players\\Jürgen\\Анна\\玩家\\😀", std::string(400, 'x'), std::string()})
		CHECK(PathIO::narrow(PathIO::widen(path)) == path);
	CHECK_THROWS_AS(PathIO::widen(std::string("\xc0\xaf", 2)), std::invalid_argument);
	CHECK_THROWS_AS(PathIO::widen(std::string("\xe2\x82", 2)), std::invalid_argument);
	CHECK_THROWS_AS(PathIO::narrow(std::wstring(1, 0xD800)), std::invalid_argument);
}

TEST_CASE("install paths: Windows command line converts Unicode paths to UTF-8 without changing arguments")
{
	const auto args = PathIO::windowsArguments(
		L"\"C:\\Program Files\\OpenBFME\\openbfme_peer.exe\" --rotwk \"C:\\players\\J\u00fcrgen\\RotWK\" "
		L"--bfme2 \"D:\\\u0410\u043d\u043d\u0430\\\u73a9\u5bb6\\\U0001f600\\BFME II\" --report \"logs\\match report.txt\" \"\" \"C:\\trailing\\\\\"");
	const std::vector<std::string> expected = {
		"C:\\Program Files\\OpenBFME\\openbfme_peer.exe", "--rotwk", u8"C:\\players\\J\u00fcrgen\\RotWK",
		"--bfme2", u8"D:\\\u0410\u043d\u043d\u0430\\\u73a9\u5bb6\\\U0001f600\\BFME II", "--report", "logs\\match report.txt", "", "C:\\trailing\\"};
	REQUIRE(args == expected);
	CHECK(PathIO::widen(args[2]) == L"C:\\players\\J\u00fcrgen\\RotWK");
	const std::wstring longPath = L"D:\\players\\J\u00fcrgen\\" + std::wstring(400, L'x');
	CHECK(PathIO::windowsArguments((L"peer --rotwk \"" + longPath + L"\"").c_str()) ==
		std::vector<std::string>{"peer", "--rotwk", PathIO::narrow(longPath)});
	const std::wstring invalid = L"peer --rotwk C:\\players\\" + std::wstring(1, 0xD800);
	CHECK_THROWS_AS(PathIO::windowsArguments(invalid.c_str()), std::invalid_argument);
}

TEST_CASE("install paths: Windows long drive and UNC paths use extended absolute spelling")
{
	const std::string tail = "/" + std::string(240, 'a') + "/file.big";
	CHECK(PathIO::native("D:/games" + tail).wstring().rfind(L"\\\\?\\D:\\games\\", 0) == 0);
	CHECK(PathIO::native("\\\\server\\share" + tail).wstring().rfind(L"\\\\?\\UNC\\server\\share\\", 0) == 0);
	CHECK(PathIO::native("relative" + tail).wstring().rfind(L"\\\\?\\", 0) == 0);
	const auto extended = PathIO::native("D:/games" + tail);
	CHECK(PathIO::native(extended.u8string()) == extended);
}

TEST_CASE("install paths: Windows trailing spaces and dots are refused without normalization")
{
	std::string error;
	for (const std::string path : {"D:/Game. ", "D:/Game./child", "D:/Game /child"})
	{
		CHECK_FALSE(PathIO::checkInstallPath(path, error));
		CHECK(error.find(path) != std::string::npos);
	}
	CHECK(PathIO::checkInstallPath("D:/Game With Spaces/./child/..", error));
}

TEST_CASE("install paths: Windows extended paths normalize mixed separators for streams and stdio")
{
	TempDir temp;
	const auto folder = temp.path / fs::u8path(u8"Jürgen/Анна/玩家");
	fs::create_directories(PathIO::native(folder.u8string()));
	const auto extended = "\\\\?\\" + folder.u8string();
	const auto mixed = extended + "/movie.vp6";
	const auto native = PathIO::native(mixed);
	CHECK(native.wstring().find(L'/') == std::wstring::npos);
	CHECK(PathIO::native("\\\\?\\UNC\\server/share/movie.vp6").wstring() == L"\\\\?\\UNC\\server\\share\\movie.vp6");
	CHECK(PathIO::native("//?/C:/Movies/test.vp6").wstring() == L"\\\\?\\C:\\Movies\\test.vp6");
	{
		std::ofstream out(native, std::ios::binary);
		out << "synthetic";
		REQUIRE(out.good());
	}
	std::ifstream in(PathIO::native(mixed), std::ios::binary);
	std::string value;
	REQUIRE(std::getline(in, value));
	CHECK(value == "synthetic");
	in.close();
	auto *f = PathIO::open(mixed, "ab");
	REQUIRE(f != nullptr);
	CHECK(std::fwrite(" append", 1, 7, f) == 7);
	CHECK(std::fclose(f) == 0);
	CHECK(fs::file_size(PathIO::native(mixed)) == 16);
}

TEST_CASE("install paths: Windows movie lookup opens the mixed-separator path built below an extended root")
{
	TempDir temp;
	const auto folder = temp.path / fs::u8path(u8"Jürgen/Анна/玩家");
	std::vector<std::uint8_t> movie(32, 0);
	const std::string header = "MVhd", codec = "vp60";
	std::copy(header.begin(), header.end(), movie.begin());
	movie[4] = 32;
	std::copy(codec.begin(), codec.end(), movie.begin() + 8);
	movie[12] = movie[14] = 16;
	movie[16] = 1;
	movie[24] = 30;
	movie[28] = 1;
	const std::uint8_t key[] = {'M', 'V', '0', 'K', 12, 0, 0, 0, 0, 0, 0, 0};
	movie.insert(movie.end(), key, key + sizeof key);
	write(folder / "Data/Movies/synthetic.vp6", movie);
	VideoPlayer player;
	Video definition;
	definition.internalName = "Synthetic";
	definition.filename = "synthetic";
	player.addVideo(definition);
	const std::string root = "\\\\?\\" + folder.u8string();
	const auto found = player.locate("Synthetic", root, "", "");
	REQUIRE_MESSAGE(found.ok, found.error);
	CHECK(found.width == 16);
	CHECK(found.height == 16);
	CHECK(found.frames == 1);
	std::ifstream reopened(PathIO::native(found.fullPath), std::ios::binary);
	const std::vector<std::uint8_t> read((std::istreambuf_iterator<char>(reopened)), std::istreambuf_iterator<char>());
	CHECK(read == movie);
}
#endif
