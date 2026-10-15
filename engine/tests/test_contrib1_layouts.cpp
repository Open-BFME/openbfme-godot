// OpenBFME unit tests. GPL-3.0.
// Lane CONTRIB-1 (GitHub issue #21): the known layouts of one game version (the All In One package's 2.01 and a disc install patched with
// EA's official 2.01 patch) and Data1.big checked by its entries (Common/RetailArchivePolicy.h). Every archive is built here byte by byte
// (tests/BigTestUtil.h): no retail bytes. The retail cases read the real installs' Data1.big and archive directories when ROTWK_INSTALL /
// BFME2_INSTALL are set and print SKIP otherwise.

#include "doctest.h"
#include "BigTestUtil.h"
#include "RetailTestMount.h"

#include "Common/InstallLocator.h"
#include "Common/MD5.h"
#include "Common/RetailArchivePolicy.h"
#include "GameNetwork/ProfileIdentity.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>

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
		path = fs::temp_directory_path() / ("openbfme-contrib1-" + std::to_string(rng()));
		fs::create_directories(path);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

void writeFile(const fs::path &p, const std::vector<std::uint8_t> &bytes)
{
	fs::create_directories(p.parent_path());
	std::ofstream out(p, std::ios::binary);
	out.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize)bytes.size());
}

std::string md5Of(const std::vector<std::uint8_t> &b)
{
	return MD5::ofBytes(b.data(), b.size());
}

std::string pin(const std::string &path, const std::vector<std::uint8_t> &b)
{
	return R"({"path": ")" + path + R"(", "md5": ")" + md5Of(b) + R"(", "size": )" + std::to_string(b.size()) + "}";
}

std::string entryPin(const std::string &name, const std::string &bytes)
{
	return R"({"name": ")" + name + R"(", "md5": ")" + MD5::ofBytes(bytes.data(), bytes.size()) + R"(", "size": )" + std::to_string(bytes.size()) + "}";
}

// The retail shape in miniature: a base INI archive the AIO layout patches with _patch201ini.big, the same content patched in place for the
// official-patch layout, an archive both share, and Data1.big in two variants (the AIO's stores its font twice; the disc's another font build).
struct Synthetic
{
	std::vector<std::uint8_t> iniBase = makeBig("BIG4", { { "data\\ini\\weapon.ini", "weapon 2.00" }, { "data\\ini\\armor.ini", "armor" } });
	std::vector<std::uint8_t> iniPatch = makeBig("BIG4", { { "data\\ini\\weapon.ini", "weapon 2.01" }, { "data\\ini\\new.ini", "new in 2.01" } });
	std::vector<std::uint8_t> iniInPlace =
		makeBig("BIG4", { { "data\\ini\\armor.ini", "armor" }, { "data\\ini\\new.ini", "new in 2.01" }, { "data\\ini\\weapon.ini", "weapon 2.01" } });
	std::vector<std::uint8_t> w3d = makeBig("BIG4", { { "art\\w3d\\unit.w3d", "model" } });
	std::vector<std::uint8_t> data1Aio = makeBig("BIG4", { { "font.ttf", "font build A" }, { "data\\scripts\\scripts.lua", "lua" }, { "font.ttf", "font build A" } });
	std::vector<std::uint8_t> data1Disc = makeBig("BIG4", { { "data\\scripts\\scripts.lua", "lua" }, { "font.ttf", "font build B, smaller" } });

	std::string json() const
	{
		return std::string(R"({"schema": "openbfme.retail-archive-policy", "schemaVersion": 1, "game": "rotwk", "patch": "2.01", "archives": [)") +
			pin("_patch201ini.big", iniPatch) + ", " + pin("INI.big", iniBase) + ", " + pin("W3D.big", w3d) + ", " + pin("Data1.big", data1Aio) +
			R"(], "layouts": {"base": {"id": "aio", "name": "the AIO layout"}, "others": [{"id": "ea", "name": "the in-place layout", "replace": [)" +
			pin("INI.big", iniInPlace) + R"(], "absent": ["_patch201ini.big"]}], "advice": "What to do: reinstall."},)" +
			R"( "entryChecked": [{"path": "Data1.big", "advice": "Report it.", "entries": [)" + entryPin("font.ttf", "font build A") + ", " + entryPin("font.ttf", "font build B, smaller") +
			", " + entryPin("data/scripts/scripts.lua", "lua") + "]}]}";
	}
	RetailArchivePolicy policy() const
	{
		RetailArchivePolicy p;
		std::string error;
		REQUIRE_MESSAGE(RetailArchivePolicy::parse(json(), "", p, &error), error);
		return p;
	}
	void writeAio(const fs::path &root) const
	{
		writeFile(root / "INI.big", iniBase);
		writeFile(root / "_patch201ini.big", iniPatch);
		writeFile(root / "W3D.big", w3d);
		writeFile(root / "Data1.big", data1Aio);
	}
	void writeOfficialPatch(const fs::path &root) const
	{
		writeFile(root / "INI.big", iniInPlace);
		writeFile(root / "W3D.big", w3d);
		writeFile(root / "Data1.big", data1Disc);
	}
};

RetailMountReport mount(Win32BIGFileSystem &fs, const RetailArchivePolicy &policy, const fs::path &root)
{
	std::vector<RetailInstall> installs(1);
	installs[0].label = "rotwk";
	installs[0].root = root.u8string();
	installs[0].policy = policy;
	return MountRetailArchives(fs, installs, RetailMountOptions());
}

std::string read(Win32BIGFileSystem &fs, const char *name)
{
	std::vector<std::uint8_t> out;
	std::string error;
	return fs.readFile(name, out, &error) ? std::string(out.begin(), out.end()) : "<missing>";
}

bool contains(const std::string &s, const std::string &part)
{
	return s.find(part) != std::string::npos;
}

std::string joined(const std::vector<std::string> &v)
{
	std::string s;
	for (const std::string &e : v)
	{
		s += e + "\n";
	}
	return s;
}

} // namespace

TEST_CASE("contrib1: both known 2.01 layouts mount and give the same files; the mount names the layout")
{
	const Synthetic syn;
	const RetailArchivePolicy policy = syn.policy();
	std::map<std::string, std::map<std::string, std::string>> files; // layout -> virtual file -> contents
	for (const char *layout : { "aio", "ea" })
	{
		CAPTURE(layout);
		TempDir tmp;
		if (std::string(layout) == "aio")
		{
			syn.writeAio(tmp.path);
		}
		else
		{
			syn.writeOfficialPatch(tmp.path);
		}
		Win32BIGFileSystem fsys;
		const RetailMountReport report = mount(fsys, policy, tmp.path);
		REQUIRE_MESSAGE(report.ok, joined(report.errors));
		CHECK(report.layouts == std::vector<std::string>{ std::string("rotwk:") + layout });
		for (const char *name : { "data\\ini\\weapon.ini", "data\\ini\\armor.ini", "data\\ini\\new.ini", "art\\w3d\\unit.w3d", "data\\scripts\\scripts.lua" })
		{
			files[layout][name] = read(fsys, name);
		}
		// the identity of the entry-checked Data1.big is the file the player has (the profile identity of a network game)
		const auto data1 = std::find_if(report.mounted.begin(), report.mounted.end(), [](const MountedArchive &m) { return m.canonicalPath == "Data1.big"; });
		REQUIRE(data1 != report.mounted.end());
		CHECK(data1->md5 == md5Of(std::string(layout) == "aio" ? syn.data1Aio : syn.data1Disc));
		CHECK(data1->size == (std::string(layout) == "aio" ? syn.data1Aio : syn.data1Disc).size());
		// the AIO layout mounts its patch archive, the in-place layout has none
		const bool hasPatch = std::any_of(report.mounted.begin(), report.mounted.end(), [](const MountedArchive &m) { return m.canonicalPath == "_patch201ini.big"; });
		CHECK(hasPatch == (std::string(layout) == "aio"));
	}
	CHECK(files["aio"]["data\\ini\\weapon.ini"] == "weapon 2.01");
	CHECK(files["aio"]["data\\ini\\new.ini"] == "new in 2.01");
	CHECK(files["aio"] == files["ea"]);
}

TEST_CASE("contrib1: a half-patched or mixed install matches no layout and is refused by name; nothing is mounted")
{
	const Synthetic syn;
	const RetailArchivePolicy policy = syn.policy();
	SUBCASE("in-place INI.big and the AIO's _patch201ini.big together")
	{
		TempDir tmp;
		syn.writeOfficialPatch(tmp.path);
		writeFile(tmp.path / "_patch201ini.big", syn.iniPatch);
		Win32BIGFileSystem fsys;
		const RetailMountReport report = mount(fsys, policy, tmp.path);
		CHECK_FALSE(report.ok);
		CHECK(report.mounted.empty());
		CHECK_FALSE(fsys.doesFileExist("data\\ini\\weapon.ini"));
		REQUIRE(report.errors.size() == 1);
		CHECK(report.errors[0] ==
			"rotwk: this folder is not a complete 2.01 install: _patch201ini.big is from the AIO layout; INI.big is from the in-place layout. What to do: reinstall.");
	}
	SUBCASE("the 2.00 INI.big without _patch201ini.big (unpatched, or a patch archive deleted)")
	{
		TempDir tmp;
		syn.writeAio(tmp.path);
		fs::remove(tmp.path / "_patch201ini.big");
		Win32BIGFileSystem fsys;
		const RetailMountReport report = mount(fsys, policy, tmp.path);
		CHECK_FALSE(report.ok);
		CHECK(report.mounted.empty());
		REQUIRE(report.errors.size() == 1);
		CHECK(report.errors[0] ==
			"rotwk: this folder is not a complete 2.01 install: INI.big is from the AIO layout; _patch201ini.big is missing (it belongs to the AIO layout). What to do: "
			"reinstall.");
	}
	SUBCASE("an INI.big of neither layout: named, and the shared archives' own problems still show")
	{
		TempDir tmp;
		syn.writeOfficialPatch(tmp.path);
		writeFile(tmp.path / "INI.big", makeBig("BIG4", { { "data\\ini\\weapon.ini", "a mod" } }));
		fs::remove(tmp.path / "W3D.big");
		Win32BIGFileSystem fsys;
		const RetailMountReport report = mount(fsys, policy, tmp.path);
		CHECK_FALSE(report.ok);
		REQUIRE(report.errors.size() == 2);
		CHECK(report.errors[0] == "rotwk: this folder is not a complete 2.01 install: _patch201ini.big is missing (it belongs to the AIO layout); INI.big (" +
			std::to_string(makeBig("BIG4", { { "data\\ini\\weapon.ini", "a mod" } }).size()) + " bytes) is no known 2.01 file. What to do: reinstall.");
		CHECK(report.errors[1] == "rotwk: missing archive W3D.big");
	}
	SUBCASE("a layout's archive with the right size and other bytes: the md5 refusal names the layout and the advice")
	{
		TempDir tmp;
		syn.writeOfficialPatch(tmp.path);
		std::vector<std::uint8_t> same = syn.iniInPlace;
		same.back() ^= 1;
		writeFile(tmp.path / "INI.big", same);
		Win32BIGFileSystem fsys;
		const RetailMountReport report = mount(fsys, policy, tmp.path);
		CHECK_FALSE(report.ok);
		REQUIRE(report.errors.size() == 1);
		CHECK(report.errors[0] == "rotwk: md5 mismatch for INI.big: expected " + md5Of(syn.iniInPlace) + ", found " + md5Of(same) +
			" (the in-place layout): a damaged, modified or other version of the file. What to do: reinstall.");
	}
}

TEST_CASE("contrib1: Data1.big is checked by its entries: every known variant passes, anything else is named")
{
	const Synthetic syn;
	const RetailArchivePolicy policy = syn.policy();
	const RetailEntryCheckedArchive *check = policy.entryCheckOf("data1.BIG");
	REQUIRE(check != nullptr);
	auto verdict = [check](const std::vector<std::uint8_t> &b) { return CheckArchiveEntries(*check, "rotwk", b.data(), b.size()); };
	CHECK(verdict(syn.data1Aio) == "");
	CHECK(verdict(syn.data1Disc) == "");
	// one font build of each kind mixed with the other's layout of the file: every entry is a known version
	CHECK(verdict(makeBig("BIGF", { { "FONT.TTF", "font build B, smaller" }, { "data/scripts/scripts.lua", "lua" } })) == "");
	CHECK(verdict(makeBig("BIG4", { { "font.ttf", "font build C" }, { "data\\scripts\\scripts.lua", "lua" } })) ==
		"rotwk: Data1.big differs from every known copy: its 'font.ttf' (12 bytes, md5 " + MD5::ofBytes("font build C", 12) + ") is not a version OpenBFME knows");
	CHECK(verdict(makeBig("BIG4", { { "font.ttf", "font build A" }, { "data\\scripts\\scripts.lua", "lua" }, { "font.ttf", "font build B, smaller" } })) ==
		"rotwk: Data1.big differs from every known copy: 'font.ttf' is stored twice with different contents");
	CHECK(verdict(makeBig("BIG4", { { "font.ttf", "font build A" } })) == "rotwk: Data1.big differs from every known copy: it lacks 'data\\scripts\\scripts.lua'");
	CHECK(verdict(makeBig("BIG4", { { "font.ttf", "font build A" }, { "data\\scripts\\scripts.lua", "lua" }, { "data\\scripts\\extra.lua", "x" } })) ==
		"rotwk: Data1.big differs from every known copy: it holds 'data\\scripts\\extra.lua', which no known copy has");
	CHECK(verdict(makeBig("BIG4", { { "font.ttf", "font build A" }, { "data\\scripts\\scripts.lua", "LUA" } })) ==
		"rotwk: Data1.big differs from every known copy: its 'data\\scripts\\scripts.lua' (3 bytes, md5 " + MD5::ofBytes("LUA", 3) +
			") is not a version OpenBFME knows");

	SUBCASE("hostile directories are refused before anything is read past the file")
	{
		std::vector<std::uint8_t> b = syn.data1Aio;
		b[8] = 0x7f; // entry count 0x7f000003
		CHECK(verdict(b) == "rotwk: Data1.big declares " + std::to_string(0x7f000003u) + " entries, more than it can hold");
		b = syn.data1Aio;
		b[16] = 0x7f; // the first entry's offset far past the end
		CHECK(contains(verdict(b), "rotwk: Data1.big: entry 'font.ttf' runs past the end of the archive"));
		// two empty entries at offset 0 (always in range), the directory cut inside the second entry's header, then inside its name
		std::vector<std::uint8_t> cut = { 'B', 'I', 'G', '4', 54, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 54 };
		for (const char *name : { "aaaaaaaaaa", "bbbbbbbbbb" })
		{
			cut.insert(cut.end(), 8, 0);
			cut.insert(cut.end(), name, name + 11); // with its NUL
		}
		REQUIRE(cut.size() == 54u);
		CHECK(verdict(std::vector<std::uint8_t>(cut.begin(), cut.begin() + 40)) == "rotwk: Data1.big: its directory is cut short at entry 1");
		CHECK(verdict(std::vector<std::uint8_t>(cut.begin(), cut.begin() + 48)) == "rotwk: Data1.big: the name of entry 1 is cut short or too long");
		// the whole of it parses: its two names are simply not Data1.big's (and the pinned ones are missing)
		CHECK(contains(verdict(cut), "it holds 'aaaaaaaaaa', which no known copy has"));
		CHECK(verdict(std::vector<std::uint8_t>{ 'B', 'I', 'G' }) == "rotwk: Data1.big is not a BIG archive");
		CHECK(verdict(std::vector<std::uint8_t>(64, 'x')) == "rotwk: Data1.big is not a BIG archive");
	}
	SUBCASE("a Data1.big over the 64 MB budget is refused by its size before anything is allocated or read")
	{
		TempDir tmp;
		syn.writeAio(tmp.path);
		{
			std::ofstream(tmp.path / "Data1.big", std::ios::binary | std::ios::trunc).close();
		}
		fs::resize_file(tmp.path / "Data1.big", (64ull << 20) + 1); // sparse: no 64 MB written
		Win32BIGFileSystem fsys;
		const RetailMountReport report = mount(fsys, policy, tmp.path);
		CHECK_FALSE(report.ok);
		CHECK(report.mounted.empty());
		REQUIRE(report.errors.size() == 1);
		CHECK(report.errors[0] == "rotwk: Data1.big is " + std::to_string((64ull << 20) + 1) + " bytes, far larger than any known copy (the AIO layout). What to do: reinstall.");
	}
	SUBCASE("entries that overlap are refused before the same bytes are hashed again and again")
	{
		// 1000 entries naming one 4 KB range: 4 MB of hashing from a ~34 KB file without the budget
		std::vector<std::uint8_t> big = { 'B', 'I', 'G', '4', 0, 0, 0, 0, 0, 0, 0x03, 0xE8, 0, 0, 0, 0 };
		std::uint32_t directory = 0;
		for (int i = 0; i < 1000; ++i)
		{
			directory += 8 + 4 + 1;
		}
		const std::uint32_t data = 16 + directory;
		for (int i = 0; i < 1000; ++i)
		{
			for (std::uint32_t v : { data, 4096u })
			{
				big.push_back((std::uint8_t)(v >> 24));
				big.push_back((std::uint8_t)(v >> 16));
				big.push_back((std::uint8_t)(v >> 8));
				big.push_back((std::uint8_t)v);
			}
			const std::string name = "e" + std::string(i < 10 ? "00" : i < 100 ? "0" : "") + std::to_string(i % 1000);
			big.insert(big.end(), name.begin(), name.end());
			big.push_back(0);
		}
		REQUIRE(big.size() == data);
		big.resize(data + 4096);
		const std::string v = verdict(big);
		CHECK(contains(v, "rotwk: Data1.big: its entries overlap (entry 'e"));
		CHECK(contains(v, "exceed the archive's own " + std::to_string(big.size()) + " bytes)"));
	}
	SUBCASE("the mount refuses an unknown Data1.big with the advice, mounting nothing")
	{
		TempDir tmp;
		syn.writeAio(tmp.path);
		writeFile(tmp.path / "Data1.big", makeBig("BIG4", { { "font.ttf", "font build C" }, { "data\\scripts\\scripts.lua", "lua" } }));
		Win32BIGFileSystem fsys;
		const RetailMountReport report = mount(fsys, policy, tmp.path);
		CHECK_FALSE(report.ok);
		CHECK(report.mounted.empty());
		REQUIRE(report.errors.size() == 1);
		CHECK(report.errors[0] == "rotwk: Data1.big differs from every known copy: its 'font.ttf' (12 bytes, md5 " + MD5::ofBytes("font build C", 12) +
			") is not a version OpenBFME knows. Report it.");
	}
}

TEST_CASE("contrib1: policy JSON layouts and entry checks are closed sets")
{
	const Synthetic syn;
	const std::string good = syn.json();
	auto refused = [](const std::string &json) {
		RetailArchivePolicy p;
		std::string error;
		const bool ok = RetailArchivePolicy::parse(json, "", p, &error);
		return ok ? std::string("<accepted>") : error;
	};
	auto edit = [&good](const std::string &from, const std::string &to) {
		std::string s = good;
		const size_t at = s.find(from);
		REQUIRE(at != std::string::npos);
		return s.replace(at, from.size(), to);
	};
	CHECK(refused(good) == "<accepted>");
	CHECK(refused(edit(R"("absent": ["_patch201ini.big"])", R"("absent": ["_patch999.big"])")) ==
		"policy JSON: layout ea leaves out an archive that is not a policy archive or is named twice");
	CHECK(refused(edit(R"("absent": ["_patch201ini.big"])", R"("absent": ["INI.big"])")) ==
		"policy JSON: layout ea leaves out an archive that is not a policy archive or is named twice");
	CHECK(refused(edit(R"("id": "ea")", R"("id": "aio")")) == "policy JSON: layout aio is defined twice");
	CHECK(refused(edit(R"("advice": "What to do: reinstall.")", R"("tip": "x")")) == "policy JSON: layouts needs base {id, name}, others [...] and advice");
	CHECK(refused(edit(R"("path": "Data1.big", "advice")", R"("path": "Data2.big", "advice")")) ==
		"policy JSON: an entry-checked archive needs the path of a policy archive and its entries");
	CHECK(refused(edit(MD5::ofBytes("lua", 3), "not-a-md5")) == "policy JSON: Data1.big: an entry needs name, md5 (32 hex) and size");
	CHECK(refused(edit(R"("advice": "Report it.", )", "")) ==
		"policy JSON: Data1.big: an entry-checked archive needs its advice (what the player does when an entry is unknown)");
	CHECK(refused(edit(entryPin("data/scripts/scripts.lua", "lua"), entryPin("data/scripts/scripts.lua", "lua") + ", " + entryPin("DATA/Scripts/scripts.lua", "lua"))) ==
		"policy JSON: Data1.big: entry data\\scripts\\scripts.lua version " + MD5::ofBytes("lua", 3) + " is listed twice");
}

TEST_CASE("contrib1: the first-run folder check (InstallLocator::checkInstall) chooses the layout as the mount does")
{
	const Synthetic syn;
	const RetailArchivePolicy policy = syn.policy();
	{
		TempDir tmp;
		syn.writeAio(tmp.path);
		const InstallLocator::InstallCheck c = InstallLocator::checkInstall("rotwk", tmp.path.u8string(), policy, nullptr);
		CHECK_MESSAGE(c.ok, joined(c.errors));
		CHECK(c.layout == "aio");
		CHECK(c.expected == 4);
	}
	{
		TempDir tmp;
		syn.writeOfficialPatch(tmp.path);
		const InstallLocator::InstallCheck c = InstallLocator::checkInstall("rotwk", tmp.path.u8string(), policy, nullptr);
		CHECK_MESSAGE(c.ok, joined(c.errors));
		CHECK(c.layout == "ea");
		CHECK(c.expected == 3);
	}
	{
		TempDir tmp;
		syn.writeOfficialPatch(tmp.path);
		writeFile(tmp.path / "_patch201ini.big", syn.iniPatch);
		Win32BIGFileSystem fsys;
		const RetailMountReport report = mount(fsys, policy, tmp.path);
		const InstallLocator::InstallCheck c = InstallLocator::checkInstall("rotwk", tmp.path.u8string(), policy, nullptr);
		CHECK_FALSE(c.ok);
		CHECK(c.layout == "");
		REQUIRE(c.errors.size() == 1);
		REQUIRE(report.errors.size() == 1);
		// the same words as the mount's refusal, with the game's name instead of the mount's label
		CHECK(c.errors[0] == "The Rise of the Witch-king" + report.errors[0].substr(std::string("rotwk").size()));
	}
	{
		// a folder of symlinked archives (a Linux player's links into another drive) is checked and mounted like the files themselves
		TempDir tmp;
		syn.writeOfficialPatch(tmp.path / "real");
		fs::create_directories(tmp.path / "links");
		std::error_code ec;
		for (const char *name : { "INI.big", "W3D.big", "Data1.big" })
		{
			fs::create_symlink(tmp.path / "real" / name, tmp.path / "links" / name, ec);
		}
		if (ec)
		{
			MESSAGE("SKIP: symlinks cannot be created here (" << ec.message() << "); the symlinked folder case is not run");
		}
		else
		{
			const InstallLocator::InstallCheck c = InstallLocator::checkInstall("rotwk", (tmp.path / "links").u8string(), policy, nullptr);
			CHECK_MESSAGE(c.ok, joined(c.errors));
			CHECK(c.layout == "ea");
			Win32BIGFileSystem fsys;
			const RetailMountReport report = mount(fsys, policy, tmp.path / "links");
			CHECK_MESSAGE(report.ok, joined(report.errors));
			CHECK(read(fsys, "data\\ini\\new.ini") == "new in 2.01");
		}
	}
	{
		TempDir tmp; // an empty folder is still "no archives here", not a layout puzzle
		const InstallLocator::InstallCheck c = InstallLocator::checkInstall("rotwk", tmp.path.u8string(), policy, nullptr);
		REQUIRE(c.errors.size() == 1);
		CHECK(contains(c.errors[0], "no The Rise of the Witch-king 2.01 archives in "));
	}
}

TEST_CASE("contrib1: the built-in policies: RotWK's two layouts, both games' Data1.big entries")
{
	RetailArchivePolicy rotwk, bfme2;
	std::string error;
	REQUIRE_MESSAGE(RetailArchivePolicy::loadBuiltin("rotwk-201", rotwk, &error), error);
	REQUIRE_MESSAGE(RetailArchivePolicy::loadBuiltin("bfme2-106", bfme2, &error), error);
	CHECK(rotwk.baseLayoutId == "aio");
	REQUIRE(rotwk.layouts.size() == 1);
	const RetailArchiveLayout &ea = rotwk.layouts[0];
	CHECK(ea.id == "ea-official-patch");
	REQUIRE(ea.replace.size() == 2);
	CHECK(ea.replace[0].path == "INI.big");
	CHECK(ea.replace[0].size == 29059250u);
	CHECK(ea.replace[0].md5 == "b488351a1d3ff02f43228201ca9d5f73");
	CHECK(ea.replace[1].path == "Maps.big");
	CHECK(ea.replace[1].size == 86684399u);
	CHECK(ea.replace[1].md5 == "9f7ddc331cfa6b6b612c8f4963e55cd3");
	CHECK(ea.absent == std::vector<std::string>{ "_patch201ini.big", "_patch201maps.big" });
	CHECK(rotwk.archivesOf(&ea).size() == rotwk.archives.size() - 2);
	CHECK(bfme2.layouts.empty());

	auto names = [](const RetailArchivePolicy &p) {
		std::set<std::string> out;
		REQUIRE(p.entryChecked.size() == 1);
		for (const RetailArchiveContentEntry &e : p.entryChecked[0].versions)
		{
			out.insert(e.name);
		}
		return out;
	};
	const std::set<std::string> common = { "albertusmt.otf", "omnialtstd.ttf", "sachwt__.ttf", "data\\scripts\\scriptevents.xml", "data\\scripts\\scripts.ini",
		"data\\scripts\\scripts.lua", "data\\systemheroes\\myhero_47c6206b5c124324a54a2da3.cah", "data\\systemheroes\\myhero_53f8791579b841c595864db3.cah",
		"data\\systemheroes\\myhero_7fa6f036a68f4381b2929ff7.cah", "data\\systemheroes\\myhero_86f62c3a695e43ecac101f38.cah",
		"data\\systemheroes\\myhero_cd5992a9cc24a29bc03282.cah", "data\\systemheroes\\myhero_d24a5d30798e43d4986d3ec.cah",
		"data\\systemheroes\\myhero_e06bd0f22e8d478db6484e29.cah" };
	std::set<std::string> rotwkNames = common, bfme2Names = common;
	rotwkNames.insert("data\\systemheroes\\myhero_54e1572918804a4cb8ed643.cah");
	bfme2Names.insert("data\\scripts\\scriptevents.xsd");
	bfme2Names.insert("data\\scripts\\scriptevents.xsx");
	CHECK(names(rotwk) == rotwkNames);
	CHECK(names(bfme2) == bfme2Names);
	CHECK(rotwk.entryCheckOf("Data1.big") != nullptr);
	CHECK(bfme2.entryCheckOf("Data1.big") != nullptr);
}

TEST_CASE("contrib1 retail: the installs' Data1.big pass their entry checks; the in-place pins are the AIO base + patch merged in mount order")
{
	const char *rotwkRoot = std::getenv("ROTWK_INSTALL");
	const std::string bfme2Root = retailtest::bfme2Install();
	if (!rotwkRoot || !*rotwkRoot || bfme2Root.empty())
	{
		retailtest::printSkip("contrib1 retail Data1.big / layout evidence");
		return;
	}
	RetailArchivePolicy rotwk, bfme2;
	std::string error;
	REQUIRE_MESSAGE(RetailArchivePolicy::loadBuiltin("rotwk-201", rotwk, &error), error);
	REQUIRE_MESSAGE(RetailArchivePolicy::loadBuiltin("bfme2-106", bfme2, &error), error);
	auto findCase = [](const fs::path &root, const std::string &name) {
		for (const auto &e : fs::directory_iterator(root))
		{
			std::string n = e.path().filename().u8string(), w = name;
			std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			std::transform(w.begin(), w.end(), w.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			if (n == w)
			{
				return e.path();
			}
		}
		return fs::path();
	};
	for (const auto &game : { std::make_pair(&rotwk, std::string(rotwkRoot)), std::make_pair(&bfme2, bfme2Root) })
	{
		const fs::path p = findCase(fs::u8path(game.second), "Data1.big");
		std::vector<unsigned char> bytes;
		REQUIRE_MESSAGE(retailtest::readLocalFile(p.u8string(), bytes, &error), error);
		CHECK(CheckArchiveEntries(game.first->entryChecked[0], game.first->game, bytes.data(), bytes.size()) == "");
	}

	// The evidence for the official-patch layout (RetailArchivePolicy.h "Layouts", stop S-4700): the in-place INI.big / Maps.big are exactly as
	// large as the AIO's base archive and its _patch201 archive merged entry by entry (the patch's entry wins, as the mount order resolves it),
	// written as the base archive is written (its header, its directory, the bytes between the directory and the first file, the files).
	struct Dir
	{
		std::map<std::string, std::pair<std::string, std::uint32_t>> entries; // lower-case name -> (name as stored, size)
		std::uint32_t gap = 0;                                                // bytes between the directory's end and the first file
	};
	auto dir = [&](const char *name) {
		Dir d;
		std::vector<unsigned char> b;
		REQUIRE_MESSAGE(retailtest::readLocalFile(findCase(fs::u8path(rotwkRoot), name).u8string(), b, &error), error);
		REQUIRE(b.size() > 16); // tolerance: a guard before the header is read, not a measured value
		auto be = [&b](size_t at) { return ((std::uint32_t)b[at] << 24) | ((std::uint32_t)b[at + 1] << 16) | ((std::uint32_t)b[at + 2] << 8) | b[at + 3]; };
		const std::uint32_t count = be(8);
		size_t pos = 16;
		std::uint32_t firstData = ~0u;
		for (std::uint32_t i = 0; i < count; ++i)
		{
			REQUIRE(pos + 8 < b.size()); // tolerance: a bounds guard for the directory walk
			const std::uint32_t offset = be(pos), size = be(pos + 4);
			pos += 8;
			std::string n;
			while (pos < b.size() && b[pos] != 0)
			{
				n.push_back((char)b[pos++]);
			}
			++pos;
			std::string k = n;
			std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			d.entries[k] = { n, size };
			firstData = std::min(firstData, offset);
		}
		d.gap = firstData - (std::uint32_t)pos;
		return d;
	};
	for (const auto &pair : { std::make_tuple("INI.big", "_patch201ini.big", 763u, 29059250ull), std::make_tuple("Maps.big", "_patch201maps.big", 302u, 86684399ull) })
	{
		const Dir base = dir(std::get<0>(pair)), patch = dir(std::get<1>(pair));
		std::map<std::string, std::pair<std::string, std::uint32_t>> merged = base.entries;
		for (const auto &e : patch.entries)
		{
			merged[e.first] = e.second;
		}
		std::uint64_t size = 16 + base.gap;
		for (const auto &e : merged)
		{
			size += 8 + e.second.first.size() + 1 + e.second.second;
		}
		CAPTURE(std::get<0>(pair));
		CHECK(merged.size() == std::get<2>(pair));
		const auto layoutPin = std::find_if(rotwk.layouts[0].replace.begin(), rotwk.layouts[0].replace.end(),
			[&pair](const RetailArchiveEntry &e) { return e.path == std::get<0>(pair); });
		REQUIRE(layoutPin != rotwk.layouts[0].replace.end());
		CHECK(layoutPin->size == std::get<3>(pair));
		CHECK(size == layoutPin->size);
	}
}

TEST_CASE("contrib1: a profile mismatch over the archive lists tells the players what to do; other differences keep the old words")
{
	MountedArchive a, b;
	a.install = b.install = "rotwk";
	a.canonicalPath = b.canonicalPath = "INI.big";
	a.size = 10;
	b.size = 11;
	a.md5 = b.md5 = "0123456789abcdef0123456789abcdef";
	const ProfileIdentity disc = ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {});
	const ProfileIdentity aio = ProfileIdentity::compute({ b }, RandomAlgorithm::ZH_CarryChain, {});
	const std::string d = ProfileIdentity::difference(disc, aio);
	CHECK(contains(d, "ours: 'archive 0 rotwk:INI.big 0123456789abcdef0123456789abcdef 10' theirs: 'archive 0 rotwk:INI.big 0123456789abcdef0123456789abcdef 11'"));
	CHECK(d.substr(d.size() - 39) == "both players need the same game install");
	const ProfileIdentity uber = ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Uber);
	CHECK(ProfileIdentity::difference(disc, uber).find("both players need the same game install") == std::string::npos);
}
