// OpenBFME unit tests: lane MODS-1, RotWK's -mod switch (Common/ModMount.h). GPL-3.0.
//
// Every mod here is written by the test at runtime (BigTestUtil): no retail bytes. The expected values come from the RotWK code the port follows:
//  * parseMod RW 0x7BADB9: relative paths below the user data folder, a folder gets a trailing separator, a second -mod replaces the first;
//  * RW 0x7BAA44: m_modBIG mounted first, then the folder's archives (RW 0xA17FB0), all with overwrite, so the last one mounted wins;
//  * RW 0xA17FB0: the folder's direct *.BIG children in the FilenameList set's order (no case);
//  * FileSystem::openFile RW 0xA149A2: with the flag, "<modDir>\<file>" before the archives;
//  * FileSystem::getFileListInDirectory RW 0xA14D2B: the mod folder's files join the listing.

#include "doctest.h"
#include "BigTestUtil.h"

#include "Common/ArchiveFile.h"
#include "Common/MD5.h"
#include "Common/ModMount.h"
#include "Common/RetailArchivePolicy.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameNetwork/NetPacket.h"
#include "GameNetwork/ProfileIdentity.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

using namespace bigtest;
namespace fs = std::filesystem;

namespace
{

struct TempDir
{
	fs::path path;
	TempDir()
	{
		std::mt19937_64 rng((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
		path = fs::temp_directory_path() / ("openbfme-mods1-" + std::to_string(rng()));
		fs::create_directories(path);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

void writeFile(const fs::path &p, const std::string &bytes)
{
	fs::create_directories(p.parent_path());
	std::ofstream out(p, std::ios::binary);
	out.write(bytes.data(), (std::streamsize)bytes.size());
}

void writeBig(const fs::path &p, const std::vector<std::pair<std::string, std::string>> &files)
{
	const std::vector<std::uint8_t> big = makeBig("BIGF", files);
	writeFile(p, std::string(big.begin(), big.end()));
}

std::string readVirtual(ArchiveFileSystem &fsys, const std::string &name)
{
	std::vector<std::uint8_t> out;
	std::string error;
	if (!fsys.readFile(name, out, &error))
	{
		return "<missing: " + error + ">";
	}
	return asString(out);
}

// the "retail" set: one archive mounted the way MountRetailArchives mounts (overwrite false)
void mountBase(Win32BIGFileSystem &fsys)
{
	std::string error;
	auto base = fsys.openArchiveFromMemory("rotwk/INI.big",
		std::make_shared<const std::vector<std::uint8_t>>(makeBig("BIGF", { { "data\\ini\\a.ini", "retail-a" }, { "data\\ini\\b.ini", "retail-b" },
																		  { "data\\ini\\c.ini", "retail-c" }, { "data\\ini\\d.ini", "retail-d" } })),
		&error);
	REQUIRE_MESSAGE(base, error);
	fsys.mountArchive(std::move(base), "rotwk/INI.big", false);
}

std::string joined(const std::vector<std::string> &v)
{
	std::string out;
	for (const std::string &s : v)
	{
		out += s + "\n";
	}
	return out;
}

} // namespace

TEST_CASE("MODS-1: parseMod (RW 0x7BADB9): user data folder, folder vs archive, the last -mod of a kind wins, a missing path is an error")
{
	TempDir tmp;
	const fs::path user = tmp.path / "user";
	fs::create_directories(user / "MyMod");
	writeFile(user / "single.big", "x");
	writeFile(tmp.path / "abs.big", "y");

	ModOperand op;
	std::string error;
	REQUIRE_MESSAGE(ParseModArgument("MyMod", user.u8string(), op, &error), error);
	CHECK(op.dir == (user / "MyMod").u8string() + "/"); // RW 0x7BAEF1: the trailing separator
	CHECK(op.archive.empty());
	REQUIRE_MESSAGE(ParseModArgument("single.big", user.u8string(), op, &error), error);
	CHECK(op.archive == (user / "single.big").u8string());
	CHECK(op.dir == (user / "MyMod").u8string() + "/"); // the other kind stays (GlobalData + 0xD38 / + 0xD3C)
	REQUIRE_MESSAGE(ParseModArgument((tmp.path / "abs.big").u8string(), user.u8string(), op, &error), error);
	CHECK(op.archive == (tmp.path / "abs.big").u8string()); // a leading '/' is absolute; the second archive replaces the first

	ModOperand missing;
	CHECK_FALSE(ParseModArgument("NoSuchMod", user.u8string(), missing, &error));
	CHECK(error == "-mod 'NoSuchMod' does not exist (looked for " + user.u8string() + "/NoSuchMod)");
	CHECK(missing.empty());
	CHECK_FALSE(ParseModArgument("MyMod", "", missing, &error));
	CHECK(error == "-mod 'MyMod': a relative mod path is taken below the user data folder, which is not known");
}

TEST_CASE("MODS-1: a mod folder's archives mount in the FilenameList's case-insensitive order (RW 0xA17FB0), the last one winning")
{
	// _stricmp order: '!' 0x21 < '_' 0x5F < 'a' 0x61 < 'b' 0x62 (both sides lower-cased)
	CHECK(ModFolderArchiveOrder({ "b.big", "A.big", "_c.big", "!z.big" }) == std::vector<std::string>{ "!z.big", "_c.big", "A.big", "b.big" });
	CHECK(ModFolderArchiveOrder({ "X.big", "x.BIG" }).size() == 1); // one Windows folder cannot hold both
}

TEST_CASE("MODS-1: precedence: loose mod files, then the folder's archives (last wins), then the -mod archive, then retail")
{
	TempDir tmp;
	const fs::path mod = tmp.path / "MyMod";
	writeBig(mod / "a.big", { { "data\\ini\\a.ini", "mod-a" }, { "data\\ini\\b.ini", "mod-a-b" } });
	writeBig(mod / "B.big", { { "data\\ini\\a.ini", "mod-B" } });                                 // sorts after a.big: wins a.ini
	writeFile(mod / "Data" / "INI" / "C.ini", "loose-c");                                          // another case on disk: Windows finds it
	// merge queue q24: under C.ini's own "Data" / "INI" spelling: on a case-insensitive file system (Windows, macOS) a second "data" / "ini" is that same
	// folder, so the listing's order below held on Linux only (a macOS gate)
	writeFile(mod / "Data" / "INI" / "object" / "new.ini", "loose-new");                          // a file retail does not have
	writeFile(mod / "readme.txt", "hello");
	const fs::path single = tmp.path / "single.big";
	writeBig(single, { { "data\\ini\\b.ini", "modbig-b" }, { "data\\ini\\a.ini", "modbig-a" }, { "data\\ini\\d.ini", "modbig-d" } });

	Win32BIGFileSystem fsys;
	mountBase(fsys);
	ModOperand op;
	op.dir = mod.u8string() + "/";
	op.archive = single.u8string();
	ModMountOptions options;
	options.rotwkInstallRoot = (tmp.path / "install").u8string();
	fs::create_directories(options.rotwkInstallRoot);
	const ModMountReport report = MountRetailMod(fsys, op, options);
	REQUIRE_MESSAGE(report.ok, joined(report.errors));

	CHECK(fsys.preferLocalFiles()); // RW 0xDEC490
	CHECK(readVirtual(fsys, "data\\ini\\a.ini") == "mod-B");      // B.big mounted last
	CHECK(readVirtual(fsys, "data\\ini\\b.ini") == "mod-a-b");    // the folder's a.big beats m_modBIG (mounted after it)
	CHECK(readVirtual(fsys, "DATA/ini/c.INI") == "loose-c");      // RW 0xA149A2: "<modDir>\<file>" before every archive, without case
	CHECK(readVirtual(fsys, "data\\ini\\d.ini") == "modbig-d");   // m_modBIG beats retail
	CHECK(readVirtual(fsys, "data\\ini\\object\\new.ini") == "loose-new");
	CHECK(fsys.doesFileExist("data\\ini\\object\\new.ini"));
	CHECK(readVirtual(fsys, "data\\..\\data\\ini\\c.ini") == "loose-c");
	CHECK_FALSE(fsys.doesFileExist("..\\MyMod\\readme.txt")); // never above the mod folder

	// the resolver: the winner first
	CHECK(fsys.sourcesOf("data\\ini\\c.ini") == std::vector<std::string>{ "mod-folder:Data\\INI\\C.ini", "rotwk/INI.big" });
	CHECK(fsys.sourcesOf("data\\ini\\a.ini") ==
		std::vector<std::string>{ (mod / "B.big").u8string(), (mod / "a.big").u8string(), single.u8string(), "rotwk/INI.big" }); // the rest by name

	// the mount order and the identity lines
	REQUIRE(report.archives.size() == 3);
	CHECK(report.archives[0].canonicalPath == "single.big");
	CHECK(report.archives[1].canonicalPath == "a.big");
	CHECK(report.archives[2].canonicalPath == "B.big");
	REQUIRE(report.loose.files.size() == 5);
	CHECK(report.loose.files[0].virtualPath == "B.big"); // byte order
	CHECK(report.loose.files[1].virtualPath == "Data\\INI\\C.ini");
	CHECK(report.loose.files[2].virtualPath == "Data\\INI\\object\\new.ini");
	CHECK(report.loose.files[3].virtualPath == "a.big");
	CHECK(report.loose.files[4].virtualPath == "readme.txt");
	REQUIRE(report.manifest.size() == 8);
	CHECK(report.manifest[0].rfind("modarchive 0 single.big ", 0) == 0);
	CHECK(report.manifest[2] == "modarchive 2 B.big " + report.archives[2].md5 + " " + std::to_string(report.archives[2].size));
	CHECK(report.manifest[4] == "modfile 1 Data\\INI\\C.ini " + report.loose.files[1].md5 + " 7");
	std::string manifestText;
	for (const std::string &l : report.manifest)
	{
		manifestText += l + "\n";
	}
	REQUIRE(report.identity.size() == 3);
	CHECK(report.identity[0] == "mod archive single.big");
	CHECK(report.identity[1] == "mod folder MyMod");
	CHECK(report.identity[2] == "modcontent 3 archives 5 files " + MD5::ofBytes(manifestText.data(), manifestText.size()));
	CHECK(report.loose.files[1].md5 == MD5::ofBytes("loose-c", 7));

	const ProfileIdentity id = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, report.identity);
	CHECK(id.text.find("\nmods 2\nmod archive single.big\nmod folder MyMod\n" + report.identity[2] + "\n") != std::string::npos);
	const ProfileIdentity none = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain);
	CHECK(none.text.find("\nmods none\n") != std::string::npos); // the text of every build before MODS-1
	CHECK(id != none);
	CHECK(report.stops == std::vector<std::string>{ ModMountStopLine() });
}

TEST_CASE("MODS-1: the mod folder joins directory listings (RW 0xA14D2B), recursive only when asked")
{
	TempDir tmp;
	const fs::path mod = tmp.path / "MyMod";
	writeFile(mod / "data" / "ini" / "object" / "New.ini", "x");
	writeFile(mod / "data" / "ini" / "Extra.INI", "y");
	writeFile(mod / "data" / "ini" / "notes.txt", "z");
	Win32BIGFileSystem fsys;
	mountBase(fsys);
	ModOperand op;
	op.dir = mod.u8string() + "/";
	ModMountOptions options;
	options.rotwkInstallRoot = (tmp.path / "install").u8string();
	fs::create_directories(options.rotwkInstallRoot);
	const ModMountReport report = MountRetailMod(fsys, op, options);
	REQUIRE_MESSAGE(report.ok, joined(report.errors));

	FilenameList flat, deep;
	fsys.getFileListInDirectory("", "data\\ini", "*.ini", flat, false);
	fsys.getFileListInDirectory("", "data\\ini", "*.ini", deep, true);
	CHECK(flat.count("data\\ini\\Extra.INI") == 1);
	CHECK(flat.count("data\\ini\\object\\New.ini") == 0);
	CHECK(deep.count("data\\ini\\object\\New.ini") == 1);
	CHECK(deep.count("data\\ini\\notes.txt") == 0);
	CHECK(deep.count("data\\ini\\a.ini") == 1); // the archives' files stay in the union
}

TEST_CASE("MODS-1: errors mount nothing: a missing folder, a bad archive, an install file that would shadow an archive (PLAN rule 7)")
{
	TempDir tmp;
	ModMountOptions options;
	options.rotwkInstallRoot = (tmp.path / "install").u8string();
	fs::create_directories(options.rotwkInstallRoot);

	{
		Win32BIGFileSystem fsys;
		mountBase(fsys);
		ModOperand op;
		op.dir = (tmp.path / "gone").u8string() + "/";
		const ModMountReport r = MountRetailMod(fsys, op, options);
		CHECK_FALSE(r.ok);
		CHECK(joined(r.errors) == "-mod folder does not exist: '" + op.dir + "'\n");
		CHECK_FALSE(fsys.preferLocalFiles());
	}
	{
		const fs::path mod = tmp.path / "Broken";
		writeBig(mod / "good.big", { { "data\\ini\\a.ini", "mod-a" } });
		writeFile(mod / "bad.big", "not an archive");
		Win32BIGFileSystem fsys;
		mountBase(fsys);
		ModOperand op;
		op.dir = mod.u8string() + "/";
		const ModMountReport r = MountRetailMod(fsys, op, options);
		CHECK_FALSE(r.ok);
		REQUIRE(r.errors.size() == 1);
		CHECK(r.errors[0].rfind("mod: bad.big is not a valid archive: ", 0) == 0);
		CHECK(readVirtual(fsys, "data\\ini\\a.ini") == "retail-a"); // good.big was not mounted either
		CHECK(fsys.getArchiveCount() == 1);
		CHECK_FALSE(fsys.preferLocalFiles());
	}
	{
		const fs::path mod = tmp.path / "Fine";
		writeFile(mod / "data" / "ini" / "a.ini", "mod-a");
		const fs::path install = tmp.path / "install2";
		writeFile(install / "data" / "ini" / "b.ini", "loose-b");                    // lookup 3: "<file>"
		writeFile(install / "lang" / "English" / "data" / "ini" / "c.ini", "loose-c"); // lookup 2: "lang\English\<file>"
		writeFile(install / "data" / "movies" / "intro.vp6", "movie");                // retail has it; no archive holds it
		Win32BIGFileSystem fsys;
		mountBase(fsys);
		ModOperand op;
		op.dir = mod.u8string() + "/";
		ModMountOptions o2;
		o2.rotwkInstallRoot = install.u8string();
		const ModMountReport r = MountRetailMod(fsys, op, o2);
		CHECK_FALSE(r.ok);
		REQUIRE(r.errors.size() == 2);
		const std::string head = "-mod: a loose file in the RotWK install folder would override the game's archives while a mod is loaded (retail reads it, "
								 "RW 0xA149A2); it is not part of the game: move it into the mod folder or delete it: ";
		CHECK(r.errors[0] == head + "data\\ini\\b.ini (would replace the archives' data\\ini\\b.ini)");
		CHECK(r.errors[1] == head + "lang\\english\\data\\ini\\c.ini (would replace the archives' data\\ini\\c.ini)");
		CHECK_FALSE(fsys.preferLocalFiles());
		CHECK(readVirtual(fsys, "data\\ini\\a.ini") == "retail-a");
	}
}

TEST_CASE("MODS-1: a loose mod file that changes after the mount is a read error, not new data")
{
	TempDir tmp;
	const fs::path mod = tmp.path / "MyMod";
	writeFile(mod / "data" / "ini" / "a.ini", "mod-a");
	Win32BIGFileSystem fsys;
	mountBase(fsys);
	ModOperand op;
	op.dir = mod.u8string() + "/";
	ModMountOptions options;
	options.rotwkInstallRoot = (tmp.path / "install").u8string();
	fs::create_directories(options.rotwkInstallRoot);
	const ModMountReport report = MountRetailMod(fsys, op, options);
	REQUIRE_MESSAGE(report.ok, joined(report.errors));
	CHECK(readVirtual(fsys, "data\\ini\\a.ini") == "mod-a");
	writeFile(mod / "data" / "ini" / "a.ini", "changed!");
	CHECK(readVirtual(fsys, "data\\ini\\a.ini") ==
		"<missing: mod file changed or vanished since the mod was mounted (mount again): data\\ini\\a.ini>");
}

TEST_CASE("MODS-1: the md5 cache makes a second mount of the same mod hash nothing")
{
	TempDir tmp;
	const fs::path mod = tmp.path / "MyMod";
	writeBig(mod / "m.big", { { "data\\ini\\a.ini", "mod-a" } });
	writeFile(mod / "data" / "ini" / "b.ini", "mod-b");
	ModOperand op;
	op.dir = mod.u8string() + "/";
	ModMountOptions options;
	options.rotwkInstallRoot = (tmp.path / "install").u8string();
	options.md5CachePath = (tmp.path / "cache.tsv").u8string();
	fs::create_directories(options.rotwkInstallRoot);
	Win32BIGFileSystem first, second;
	mountBase(first);
	mountBase(second);
	const ModMountReport a = MountRetailMod(first, op, options);
	const ModMountReport b = MountRetailMod(second, op, options);
	REQUIRE(a.ok);
	REQUIRE(b.ok);
	CHECK(a.filesHashed == 2); // m.big once (it is the folder's archive and one of its loose files) and b.ini
	CHECK(a.md5CacheHits == 0);
	CHECK(b.filesHashed == 0);
	CHECK(b.md5CacheHits == 3); // the archive and its loose-file entry, b.ini
	CHECK(a.identity == b.identity);
}

TEST_CASE("MODS-1 (Sol r1): a big folder mod's identity stays one line: the save header and the LAN datagram still carry it")
{
	TempDir tmp;
	const fs::path mod = tmp.path / "BigMod";
	for (int i = 0; i < 2000; ++i)
	{
		writeFile(mod / "art" / "textures" / ("t" + std::to_string(i) + ".tga"), std::to_string(i));
	}
	Win32BIGFileSystem fsys;
	mountBase(fsys);
	ModOperand op;
	op.dir = mod.u8string() + "/";
	ModMountOptions options;
	options.rotwkInstallRoot = (tmp.path / "install").u8string();
	fs::create_directories(options.rotwkInstallRoot);
	const ModMountReport report = MountRetailMod(fsys, op, options);
	REQUIRE_MESSAGE(report.ok, joined(report.errors));
	CHECK(report.manifest.size() == 2000);
	REQUIRE(report.identity.size() == 2);
	CHECK(report.identity[1].rfind("modcontent 0 archives 2000 files ", 0) == 0);
	CHECK(report.identity[1].size() == std::string("modcontent 0 archives 2000 files ").size() + 32);

	const ProfileIdentity id = ProfileIdentity::compute({}, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, report.identity);
	SaveGameFile save, parsed;
	save.header.profile = id;
	save.header.engineId = ProfileIdentity::buildId();
	save.header.executionProfile = "enhanced";
	std::string error;
	REQUIRE_MESSAGE(SaveGameFile::parse(save.encode(), parsed, &error), error);
	CHECK(parsed.header.profile == id);
	NetByteWriter writer;
	writer.str(id.text);
	NetByteReader reader(writer.data());
	CHECK(reader.str() == id.text);
	CHECK_FALSE(reader.failed());
}

TEST_CASE("MODS-1 (Sol r1): a symbolic link inside a mod folder is refused (it could leave the folder or loop)")
{
	TempDir tmp;
	const fs::path mod = tmp.path / "LinkMod";
	writeFile(mod / "data" / "ini" / "a.ini", "mod-a");
	writeFile(tmp.path / "outside.ini", "outside");
	std::error_code ec;
	fs::create_symlink(tmp.path / "outside.ini", mod / "data" / "ini" / "b.ini", ec);
	fs::create_directory_symlink(mod, mod / "loop", ec);
	if (ec)
	{
		MESSAGE("SKIP: this file system cannot make symbolic links: " << ec.message());
		return;
	}
	Win32BIGFileSystem fsys;
	mountBase(fsys);
	ModOperand op;
	op.dir = mod.u8string() + "/";
	ModMountOptions options;
	options.rotwkInstallRoot = (tmp.path / "install").u8string();
	fs::create_directories(options.rotwkInstallRoot);
	const ModMountReport report = MountRetailMod(fsys, op, options);
	CHECK_FALSE(report.ok);
	REQUIRE(report.errors.size() == 2);
	CHECK(report.errors[0].rfind("-mod: the mod folder holds a symbolic link, which is not supported: ", 0) == 0);
	CHECK(readVirtual(fsys, "data\\ini\\b.ini") == "retail-b");
	CHECK_FALSE(fsys.preferLocalFiles());
}

TEST_CASE("MODS-2: an install-folder mod is merged into the RotWK install's load order; a same-path archive replaces the retail one")
{
	TempDir tmp;
	// the "retail" install as MountRetailArchives reports it (canonical paths, retail case), and BFME2 after it
	writeBig(tmp.path / "rotwk" / "INI.big", { { "data\\ini\\a.ini", "rotwk-INI" }, { "data\\ini\\b.ini", "rotwk-INI-b" } });
	writeBig(tmp.path / "rotwk" / "_patch201ini.big", { { "data\\ini\\a.ini", "rotwk-patch" } });
	writeBig(tmp.path / "rotwk" / "lang" / "English.big", { { "data\\lotr.str", "retail strings" } });
	writeBig(tmp.path / "bfme2" / "ini.big", { { "data\\ini\\a.ini", "bfme2" }, { "data\\ini\\c.ini", "bfme2-c" } });
	RetailMountReport retail;
	retail.ok = true;
	for (const auto &m : std::vector<std::pair<std::string, std::string>>{ { "rotwk", "INI.big" }, { "rotwk", "_patch201ini.big" }, { "rotwk", "lang/English.big" },
			 { "bfme2", "ini.big" } })
	{
		const fs::path disk = tmp.path / m.first / fs::u8path(m.second);
		retail.mounted.push_back({ m.first, m.second, disk.u8string(), (std::uint64_t)fs::file_size(disk), "md5-" + m.second });
	}
	// the mod as the All In One launcher lays it into the game folder
	const fs::path mod = tmp.path / "Edain";
	writeBig(mod / "__edain_data.big", { { "data\\ini\\a.ini", "edain" }, { "data\\ini\\b.ini", "edain-b" } });
	writeBig(mod / "lang" / "English.big", { { "data\\ini\\credits.ini", "edain credits" } });
	writeFile(mod / "aio-manifest.json", "{}");

	Win32BIGFileSystem merged;
	const ModMountReport r = MountInstallOverlay(merged, retail, mod.u8string(), ModMountOptions());
	REQUIRE_MESSAGE(r.ok, joined(r.errors));
	// RotWK's startup order (RW 0xA1766D: lang\English*.big, then the folder's *.big in compareNoCase order, first wins): '_' (0x5F) sorts before
	// letters, and "__edain" before "_patch" (second character '_' before 'p'): the mod's archive outranks the 2.01 patch, as Edain's names intend
	REQUIRE(r.merged.size() == 5);
	CHECK(r.merged[0].install + ":" + r.merged[0].canonicalPath == "mod:lang/English.big");
	CHECK(r.merged[1].canonicalPath == "__edain_data.big");
	CHECK(r.merged[2].canonicalPath == "_patch201ini.big");
	CHECK(r.merged[3].canonicalPath == "INI.big");
	CHECK(r.merged[4].install + ":" + r.merged[4].canonicalPath == "bfme2:ini.big");
	CHECK(readVirtual(merged, "data\\ini\\a.ini") == "edain");
	CHECK(readVirtual(merged, "data\\ini\\b.ini") == "edain-b");
	CHECK(readVirtual(merged, "data\\ini\\c.ini") == "bfme2-c");
	CHECK(readVirtual(merged, "data\\ini\\credits.ini") == "edain credits");
	CHECK_FALSE(merged.doesFileExist("data\\lotr.str")); // the retail lang\English.big was replaced by the mod's file of that path
	CHECK_FALSE(merged.preferLocalFiles());                // no -mod: no prefer-local flag
	CHECK(r.archives.size() == 2);
	CHECK(r.notes == std::vector<std::string>{ "install mod Edain: aio-manifest.json is not an archive: not used" });
	REQUIRE(r.identity.size() == 2);
	CHECK(r.identity[0] == "mod install Edain");
	CHECK(r.identity[1].rfind("modcontent 2 archives 0 files ", 0) == 0);
	CHECK(r.stops == std::vector<std::string>{ InstallOverlayStopLine() });

	Win32BIGFileSystem empty;
	const ModMountReport bad = MountInstallOverlay(empty, retail, (tmp.path / "nothing").u8string(), ModMountOptions());
	CHECK_FALSE(bad.ok);
	CHECK(empty.getArchiveCount() == 0);
}

TEST_CASE("MODS-2: RotWK's startup load order (RW 0xA1766D / 0xA14419): lang\\English*.big last-wins, the folder's archives without case, apt\\ after")
{
	auto order = [](const std::vector<std::string> &paths, bool gameInstall) {
		std::vector<RetailArchiveEntry> in;
		for (const std::string &p : paths)
		{
			in.push_back({ p, "", 0 });
		}
		std::vector<std::string> out;
		for (const RetailArchiveEntry &e : RetailArchiveLoadOrder(in, gameInstall))
		{
			out.push_back(e.path);
		}
		return out;
	};
	const std::vector<std::string> rotwk = { "INI.big", "_patch201ini.big", "W3D.big", "lotrsec.big", "apt/MainMenu.big", "lang/English.big",
		"lang/EnglishAudio.big", "lang/englishpatch201.big", "data2.big" };
	// the game folder: lang\ loaded with overwrite (the last one wins: listed here first), then the root in _memicmp order, then apt\ (the last group)
	CHECK(order(rotwk, true) == std::vector<std::string>{ "lang/englishpatch201.big", "lang/EnglishAudio.big", "lang/English.big", "_patch201ini.big",
								  "data2.big", "INI.big", "lotrsec.big", "W3D.big", "apt/MainMenu.big" });
	// the BFME2 install: lang\ with overwrite 0 (the first one wins), and its apt\ is not mounted by retail (kept last, stop S-5302)
	CHECK(order({ "ini.big", "lang/English.big", "lang/englishpatch106.big", "apt/MainMenu.big", "_patch106.big" }, false) ==
		std::vector<std::string>{ "lang/English.big", "lang/englishpatch106.big", "_patch106.big", "ini.big", "apt/MainMenu.big" });
}

TEST_CASE("MODS-2: stacked install-folder mods: the first folder holding an archive path wins it (WorkshopKit L1 .. L3)")
{
	TempDir tmp;
	writeBig(tmp.path / "rotwk" / "INI.big", { { "data\\ini\\a.ini", "rotwk" } });
	RetailMountReport retail;
	retail.ok = true;
	retail.mounted.push_back({ "rotwk", "INI.big", (tmp.path / "rotwk" / "INI.big").u8string(), (std::uint64_t)fs::file_size(tmp.path / "rotwk" / "INI.big"), "x" });
	writeBig(tmp.path / "Addon" / "install" / "__edain_data.big", { { "data\\ini\\a.ini", "addon" } });
	writeBig(tmp.path / "Edain" / "install" / "__edain_data.big", { { "data\\ini\\a.ini", "edain" }, { "data\\ini\\b.ini", "edain-b" } });
	writeBig(tmp.path / "Edain" / "install" / "__edain_maps.big", { { "maps\\m.map", "map" } });
	Win32BIGFileSystem merged;
	const ModMountReport r = MountInstallOverlay(merged, retail,
		std::vector<std::string>{ (tmp.path / "Addon" / "install").u8string(), (tmp.path / "Edain" / "install").u8string() }, ModMountOptions());
	REQUIRE_MESSAGE(r.ok, joined(r.errors));
	CHECK(readVirtual(merged, "data\\ini\\a.ini") == "addon");
	CHECK_FALSE(merged.doesFileExist("data\\ini\\b.ini")); // the add-on's __edain_data.big replaced Edain's file of that path as a whole
	CHECK(readVirtual(merged, "maps\\m.map") == "map");
	CHECK(r.folderName == "Addon + Edain");
	CHECK(r.notes == std::vector<std::string>{ "install mod Addon: __edain_data.big replaces Edain's" });
}

TEST_CASE("MODS-3: a mod's GameData block in the Object folder is merged into data\\ini\\gamedata.ini (RJ-RotWK keeps it there)")
{
	TempDir tmp;
	const fs::path mod = tmp.path / "RJ";
	writeFile(mod / "data" / "ini" / "gamedata.ini", "; only comments\r\n");
	writeFile(mod / "data" / "ini" / "object" / "gamedata.ini",
		"#define RJ_SPEED 2.0\r\nGameData\r\n  MultipleFactory = RJ_SPEED ; a comment\r\n  ShellMapName = x\r\nEnd\r\nObject Thing\r\nEnd\r\n");
	writeFile(mod / "data" / "ini" / "object" / "units.ini", "Object Other\r\nEnd\r\n");

	Win32BIGFileSystem fsys;
	mountBase(fsys);
	ModOperand op;
	op.dir = mod.u8string() + "/";
	ModMountOptions options;
	options.rotwkInstallRoot = (tmp.path / "install").u8string();
	fs::create_directories(options.rotwkInstallRoot);
	const ModMountReport report = MountRetailMod(fsys, op, options);
	REQUIRE_MESSAGE(report.ok, joined(report.errors));
	CHECK(readVirtual(fsys, "data\\ini\\gamedata.ini") ==
		"#define RJ_SPEED 2.0\r\n; only comments\r\nGameData\r\n  MultipleFactory = RJ_SPEED ; a comment\r\n  ShellMapName = x\r\nEnd\r\n");
	CHECK(fsys.sourcesOf("data\\ini\\gamedata.ini").front() == "derived:GameData of data\\ini\\object\\gamedata.ini");
	CHECK(readVirtual(fsys, "data\\ini\\object\\gamedata.ini").find("Object Thing") != std::string::npos); // the file itself is unchanged
	REQUIRE(report.notes.size() == 1);

	// a gamedata.ini with its own block: the Object folder's lines go in before its End (parsed after its own fields, as retail does)
	TempDir tmp2;
	const fs::path mod2 = tmp2.path / "Two";
	writeFile(mod2 / "data" / "ini" / "gamedata.ini", "GameData\nA = 1\nEnd\n");
	writeFile(mod2 / "data" / "ini" / "object" / "x.ini", "GameData\nA = 2\nEnd\n");
	Win32BIGFileSystem fsys2;
	mountBase(fsys2);
	ModOperand op2;
	op2.dir = mod2.u8string() + "/";
	options.rotwkInstallRoot = (tmp2.path / "install").u8string();
	fs::create_directories(options.rotwkInstallRoot);
	REQUIRE(MountRetailMod(fsys2, op2, options).ok);
	CHECK(readVirtual(fsys2, "data\\ini\\gamedata.ini") == "GameData\r\nA = 1\r\nA = 2\r\nEnd\r\n");
}

TEST_CASE("MODS-3: a -mod over an install-folder mod's merged gamedata.ini: its own gamedata.ini is the base, the lower layer's Object-folder lines stay")
{
	TempDir tmp;
	Win32BIGFileSystem fsys;
	std::string error;
	// the lower layer (an install-folder mod like Patch 2.02): its Object-folder GameData merged into a derived gamedata.ini
	auto lower = fsys.openArchiveFromMemory("install/patch.big",
		std::make_shared<const std::vector<std::uint8_t>>(makeBig("BIGF", { { "data\\ini\\gamedata.ini", "GameData\nA = 1\nEnd\n" },
																		   { "data\\ini\\object\\patchgd.ini", "GameData\nB = 2\nEnd\n" } })),
		&error);
	REQUIRE_MESSAGE(lower, error);
	fsys.mountArchive(std::move(lower), "install/patch.big", false);
	fsys.setDerivedFile("data\\ini\\gamedata.ini", { 'x' }, "GameData of data\\ini\\object\\patchgd.ini", { "data\\ini\\object\\patchgd.ini" });

	const fs::path mod = tmp.path / "Tamriel";
	writeFile(mod / "data" / "ini" / "gamedata.ini", "#define MY_MACRO 3\nGameData\nC = MY_MACRO\nEnd\n");
	ModOperand op;
	op.dir = mod.u8string() + "/";
	ModMountOptions options;
	options.rotwkInstallRoot = (tmp.path / "install").u8string();
	fs::create_directories(options.rotwkInstallRoot);
	const ModMountReport report = MountRetailMod(fsys, op, options);
	REQUIRE_MESSAGE(report.ok, joined(report.errors));
	CHECK(readVirtual(fsys, "data\\ini\\gamedata.ini") == "#define MY_MACRO 3\r\nGameData\r\nC = MY_MACRO\r\nB = 2\r\nEnd\r\n");
}
