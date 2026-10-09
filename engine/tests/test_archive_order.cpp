// OpenBFME unit tests: archive override order and the pure-retail mount rule. GPL-3.0.

#include "doctest.h"
#include "BigTestUtil.h"

#include "Common/ArchiveFile.h"
#include "Common/MD5.h"
#include "Common/RetailArchivePolicy.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

using namespace bigtest;
namespace fs = std::filesystem;

namespace
{

std::shared_ptr<const std::vector<std::uint8_t>> shared(const std::vector<std::uint8_t> &v)
{
	return std::make_shared<const std::vector<std::uint8_t>>(v);
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

size_t indexOf(const std::vector<std::string> &v, const std::string &s)
{
	return (size_t)(std::find(v.begin(), v.end(), s) - v.begin());
}

struct TempDir
{
	fs::path path;
	TempDir()
	{
		std::mt19937_64 rng((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
		path = fs::temp_directory_path() / ("openbfme-test-" + std::to_string(rng()));
		fs::create_directories(path);
	}
	~TempDir()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

std::uint64_t writeFile(const fs::path &p, const std::vector<std::uint8_t> &bytes)
{
	fs::create_directories(p.parent_path());
	std::ofstream out(p, std::ios::binary);
	out.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize)bytes.size());
	return bytes.size();
}

RetailArchiveEntry entryFor(const std::string &canonical, const std::vector<std::uint8_t> &bytes)
{
	RetailArchiveEntry e;
	e.path = canonical;
	e.size = bytes.size();
	e.md5 = MD5::ofBytes(bytes.data(), bytes.size());
	return e;
}

} // namespace

TEST_CASE("first mounted archive wins with overwrite=false; overwrite=true replaces (ZH loadIntoDirectoryTree)")
{
	Win32BIGFileSystem fsys;
	std::string error;
	auto a = fsys.openArchiveFromMemory("a.big", shared(makeBig("BIGF", { { "data\\ini\\weapon.ini", "A" }, { "only_a.txt", "a" } })), &error);
	auto b = fsys.openArchiveFromMemory("b.big", shared(makeBig("BIG4", { { "DATA/INI/Weapon.ini", "B" }, { "only_b.txt", "b" } })), &error);
	auto c = fsys.openArchiveFromMemory("c.big", shared(makeBig("BIGF", { { "data\\ini\\weapon.ini", "C" } })), &error);
	REQUIRE(a);
	REQUIRE(b);
	REQUIRE(c);
	fsys.mountArchive(std::move(a), "a.big", false);
	fsys.mountArchive(std::move(b), "b.big", false);
	CHECK(readVirtual(fsys, "data\\ini\\weapon.ini") == "A");
	CHECK(fsys.getArchiveFilenameForFile("Data/Ini/WEAPON.INI") == "a.big");
	CHECK(readVirtual(fsys, "only_b.txt") == "b");
	fsys.mountArchive(std::move(c), "c.big", true);
	CHECK(readVirtual(fsys, "data\\ini\\weapon.ini") == "C");
	CHECK_FALSE(fsys.doesFileExist("data\\ini\\armor.ini"));
}

TEST_CASE("BFME load order is descending strcmp: retail patch archives outrank their base archives")
{
	// Retail-canonical names from the RotWK 2.01 package.
	std::vector<std::string> names = { "INI.big", "W3D.big", "_patch201.big", "_patch201ini.big", "_patch201maps.big",
		"Maps.big", "apt\\MainMenu.big", "lang\\English.big", "lang\\englishpatch201.big", "Textures2.big" };
	std::vector<std::string> order = BFMEArchiveLoadOrder(names);
	REQUIRE(order.size() == names.size());
	CHECK(indexOf(order, "_patch201ini.big") < indexOf(order, "INI.big"));
	CHECK(indexOf(order, "_patch201.big") < indexOf(order, "W3D.big"));
	CHECK(indexOf(order, "_patch201.big") < indexOf(order, "Textures2.big"));
	CHECK(indexOf(order, "_patch201maps.big") < indexOf(order, "Maps.big"));
	CHECK(indexOf(order, "lang\\englishpatch201.big") < indexOf(order, "lang\\English.big"));
	// exact sequence (strcmp descending: '_' 0x5F > 'W' > 'T' > 'M' > 'I'; 'l' > 'a')
	std::vector<std::string> expected = { "lang\\englishpatch201.big", "lang\\English.big", "apt\\MainMenu.big",
		"_patch201maps.big", "_patch201ini.big", "_patch201.big", "W3D.big", "Textures2.big", "Maps.big", "INI.big" };
	CHECK(order == expected);
}

TEST_CASE("built-in RotWK 2.01 policy orders patches first even though disk names may be lower case")
{
	RetailArchivePolicy rotwk;
	std::string error;
	REQUIRE_MESSAGE(RetailArchivePolicy::loadBuiltin("rotwk-201", rotwk, &error), error);
	std::vector<std::string> order;
	for (const RetailArchiveEntry &e : RetailArchiveLoadOrder(rotwk.archives))
	{
		order.push_back(e.path);
	}
	CHECK(indexOf(order, "_patch201ini.big") < indexOf(order, "INI.big"));
	CHECK(indexOf(order, "_patch201.big") < indexOf(order, "W3D.big"));
	CHECK(indexOf(order, "_patch201maps.big") < indexOf(order, "Maps.big"));
	CHECK(indexOf(order, "lang/englishpatch201.big") < indexOf(order, "lang/English.big"));
}

TEST_CASE("pure-retail mount: RotWK over BFME2, patch over base, 2.02 excluded, unknown/mismatch fail closed")
{
	TempDir tmp;
	fs::path rotwkRoot = tmp.path / "rotwk";
	fs::path bfme2Root = tmp.path / "bfme2";

	auto rIni = makeBig("BIGF", { { "data\\ini\\weapon.ini", "rotwk-base" }, { "data\\ini\\armor.ini", "rotwk-armor" } });
	auto rPatch = makeBig("BIG4", { { "data\\ini\\weapon.ini", "rotwk-201" } });
	auto rLang = makeBig("BIGF", { { "data\\lotr.str", "english" } });
	auto rLangPatch = makeBig("BIGF", { { "data\\lotr.str", "englishpatch201" } });
	auto r202 = makeBig("BIG4", { { "data\\ini\\weapon.ini", "COMMUNITY-202" } });
	auto bIni = makeBig("BIGF", { { "data\\ini\\weapon.ini", "bfme2" }, { "data\\ini\\bfme2only.ini", "bfme2-only" }, { "data\\ini\\armor.ini", "bfme2-armor" } });

	// On disk everything is lower case, like a copied install; the policy keeps retail case.
	writeFile(rotwkRoot / "ini.big", rIni);
	writeFile(rotwkRoot / "_patch201ini.big", rPatch);
	writeFile(rotwkRoot / "lang" / "english.big", rLang);
	writeFile(rotwkRoot / "lang" / "englishpatch201.big", rLangPatch);
	writeFile(rotwkRoot / "__patch202.big", r202);
	writeFile(bfme2Root / "ini.big", bIni);

	RetailInstall rotwk;
	rotwk.label = "rotwk";
	rotwk.root = rotwkRoot.u8string();
	rotwk.policy.game = "rotwk";
	rotwk.policy.patch = "2.01";
	rotwk.policy.archives = { entryFor("INI.big", rIni), entryFor("_patch201ini.big", rPatch),
		entryFor("lang/English.big", rLang), entryFor("lang/englishpatch201.big", rLangPatch) };
	rotwk.policy.excluded = { "__patch202.big" };

	RetailInstall bfme2;
	bfme2.label = "bfme2";
	bfme2.root = bfme2Root.u8string();
	bfme2.policy.game = "bfme2";
	bfme2.policy.patch = "1.06";
	bfme2.policy.archives = { entryFor("ini.big", bIni) };

	SUBCASE("without a cache path every mount hashes every archive")
	{
		for (int run = 0; run < 2; ++run)
		{
			Win32BIGFileSystem fsys;
			RetailMountReport report = MountRetailArchives(fsys, { rotwk, bfme2 }, RetailMountOptions());
			REQUIRE_MESSAGE(report.ok, (report.errors.empty() ? std::string() : report.errors[0]));
			CHECK(report.archivesHashed == 5);
			CHECK(report.md5CacheHits == 0);
		}
	}

	SUBCASE("the cache never trusts a file that changed: same size and changed byte is refused, rehash on a new mtime")
	{
		RetailMountOptions opts;
		opts.md5CachePath = (tmp.path / "cache" / "md5.tsv").u8string();
		{
			Win32BIGFileSystem fsys;
			REQUIRE(MountRetailArchives(fsys, { rotwk, bfme2 }, opts).ok); // cache now holds the good hashes
		}
		fs::path target = rotwkRoot / "ini.big";
		auto tampered = rIni;
		tampered.back() ^= 0x01;
		fs::file_time_type before = fs::last_write_time(target);
		writeFile(target, tampered);
		fs::last_write_time(target, before + std::chrono::seconds(2)); // the file's identity changes with the write
		Win32BIGFileSystem fsys;
		RetailMountReport report = MountRetailArchives(fsys, { rotwk, bfme2 }, opts);
		CHECK_FALSE(report.ok);
		REQUIRE(report.errors.size() == 1);
		CHECK(report.errors[0].find("md5 mismatch for INI.big") != std::string::npos);
		CHECK(report.archivesHashed == 1);
		CHECK(report.md5CacheHits == 4);
		CHECK(fsys.getArchiveCount() == 0);

		// restoring the original bytes under yet another mtime is accepted again (rehashed, not remembered as bad)
		writeFile(target, rIni);
		fs::last_write_time(target, before + std::chrono::seconds(4));
		Win32BIGFileSystem again;
		RetailMountReport restored = MountRetailArchives(again, { rotwk, bfme2 }, opts);
		CHECK_MESSAGE(restored.ok, (restored.errors.empty() ? std::string() : restored.errors[0]));
		CHECK(restored.archivesHashed == 1);
	}

	SUBCASE("a damaged or lying cache cannot refuse a good archive")
	{
		RetailMountOptions opts;
		opts.md5CachePath = (tmp.path / "cache" / "md5.tsv").u8string();
		{
			Win32BIGFileSystem fsys;
			REQUIRE(MountRetailArchives(fsys, { rotwk, bfme2 }, opts).ok);
		}
		std::string zeros(32, '0');
		{
			std::ifstream in(fs::u8path(opts.md5CachePath));
			std::string line, rewritten;
			while (std::getline(in, line))
			{
				rewritten += zeros + line.substr(line.find('\t')) + "\n"; // right keys, wrong md5s
			}
			in.close();
			std::ofstream out(fs::u8path(opts.md5CachePath), std::ios::trunc);
			out << rewritten << "garbage line without a tab\n" << "nothex" << '\t' << "key\n";
		}
		Win32BIGFileSystem fsys;
		RetailMountReport report = MountRetailArchives(fsys, { rotwk, bfme2 }, opts);
		CHECK_MESSAGE(report.ok, (report.errors.empty() ? std::string() : report.errors[0]));
		CHECK(report.archivesHashed == 5);
		CHECK(report.md5CacheHits == 0);
	}

	SUBCASE("happy path: md5 is verified for every archive, the second mount is served from the cache")
	{
		Win32BIGFileSystem fsys;
		RetailMountOptions opts;
		opts.md5CachePath = (tmp.path / "cache" / "md5.tsv").u8string();
		RetailMountReport report = MountRetailArchives(fsys, { rotwk, bfme2 }, opts);
		REQUIRE_MESSAGE(report.ok, (report.errors.empty() ? std::string() : report.errors[0]));
		CHECK(report.mounted.size() == 5);
		CHECK(report.excludedFound == std::vector<std::string>{ "rotwk:__patch202.big" });
		CHECK(readVirtual(fsys, "data\\ini\\weapon.ini") == "rotwk-201");     // patch over base, never 2.02
		CHECK(readVirtual(fsys, "data\\ini\\armor.ini") == "rotwk-armor");    // RotWK over BFME2
		CHECK(readVirtual(fsys, "data\\ini\\bfme2only.ini") == "bfme2-only"); // BFME2 fills gaps
		CHECK(readVirtual(fsys, "data\\lotr.str") == "englishpatch201");      // language patch wins
		CHECK(report.archivesHashed == 5);
		CHECK(report.md5CacheHits == 0);
		CHECK(report.warnings.empty());
		// second run is served from the md5 cache and agrees
		Win32BIGFileSystem again;
		RetailMountReport second = MountRetailArchives(again, { rotwk, bfme2 }, opts);
		CHECK(second.ok);
		CHECK(second.archivesHashed == 0);
		CHECK(second.md5CacheHits == 5);
		CHECK(readVirtual(again, "data\\ini\\weapon.ini") == "rotwk-201");
	}

	SUBCASE("same size, one byte changed: refused with default options (md5 is always checked)")
	{
		auto tampered = rIni;
		tampered.back() ^= 0x01; // "rotwk-armor" -> same length, different content
		REQUIRE(tampered.size() == rIni.size());
		writeFile(rotwkRoot / "ini.big", tampered);
		Win32BIGFileSystem fsys;
		RetailMountReport report = MountRetailArchives(fsys, { rotwk, bfme2 }, RetailMountOptions());
		CHECK_FALSE(report.ok);
		REQUIRE(report.errors.size() == 1);
		CHECK(report.errors[0].find("md5 mismatch for INI.big") != std::string::npos);
		CHECK(report.mounted.empty());
		CHECK(fsys.getArchiveCount() == 0);
		CHECK_FALSE(fsys.doesFileExist("data\\ini\\weapon.ini"));
	}

	SUBCASE("a later archive that fails to parse leaves nothing mounted")
	{
		// INI.big is last in RotWK load order (_patch201ini and lang/* come first). Give it the right
		// size and md5 for the policy but bytes that are not a BIG archive.
		std::vector<std::uint8_t> garbage(rIni.size(), 0x41);
		writeFile(rotwkRoot / "ini.big", garbage);
		rotwk.policy.archives[0] = entryFor("INI.big", garbage);
		Win32BIGFileSystem fsys;
		RetailMountReport report = MountRetailArchives(fsys, { rotwk, bfme2 }, RetailMountOptions());
		CHECK_FALSE(report.ok);
		REQUIRE_FALSE(report.errors.empty());
		CHECK(report.errors[0].find("BIGF/BIG4") != std::string::npos);
		CHECK(report.mounted.empty());
		CHECK(fsys.getArchiveCount() == 0);
		CHECK_FALSE(fsys.doesFileExist("data\\lotr.str")); // an earlier, valid archive must not stay readable
		CHECK_FALSE(fsys.doesFileExist("data\\ini\\weapon.ini"));
	}

	SUBCASE("unknown archive stops the mount and nothing is mounted")
	{
		writeFile(rotwkRoot / "zz_mod.big", makeBig("BIGF", { { "x.txt", "x" } }));
		Win32BIGFileSystem fsys;
		RetailMountReport report = MountRetailArchives(fsys, { rotwk, bfme2 }, RetailMountOptions());
		CHECK_FALSE(report.ok);
		REQUIRE(report.errors.size() == 1);
		CHECK(report.errors[0].find("unknown archive 'zz_mod.big'") != std::string::npos);
		CHECK(report.mounted.empty());
		CHECK(fsys.getArchiveCount() == 0);
	}

	SUBCASE("size mismatch fails")
	{
		rotwk.policy.archives[0].size += 1;
		Win32BIGFileSystem fsys;
		RetailMountReport report = MountRetailArchives(fsys, { rotwk, bfme2 }, RetailMountOptions());
		CHECK_FALSE(report.ok);
		CHECK(report.errors[0].find("size mismatch for INI.big") != std::string::npos);
		CHECK(fsys.getArchiveCount() == 0);
	}

	SUBCASE("md5 mismatch fails")
	{
		rotwk.policy.archives[1].md5 = "00000000000000000000000000000000";
		Win32BIGFileSystem fsys;
		RetailMountReport report = MountRetailArchives(fsys, { rotwk, bfme2 }, RetailMountOptions());
		CHECK_FALSE(report.ok);
		CHECK(report.errors[0].find("md5 mismatch for _patch201ini.big") != std::string::npos);
	}

	SUBCASE("missing archive and missing install fail")
	{
		fs::remove(rotwkRoot / "lang" / "english.big");
		RetailInstall nowhere = bfme2;
		nowhere.root = (tmp.path / "does-not-exist").u8string();
		Win32BIGFileSystem fsys;
		RetailMountReport report = MountRetailArchives(fsys, { rotwk, nowhere }, RetailMountOptions());
		CHECK_FALSE(report.ok);
		REQUIRE(report.errors.size() == 2);
		CHECK(report.errors[0].find("missing archive lang/English.big") != std::string::npos);
		CHECK(report.errors[1].find("install directory does not exist") != std::string::npos);
	}
}

TEST_CASE("loadBigFilesFromDirectory applies the same BFME order to a directory")
{
	TempDir tmp;
	writeFile(tmp.path / "INI.big", makeBig("BIGF", { { "data\\ini\\weapon.ini", "base" } }));
	writeFile(tmp.path / "_patch201ini.big", makeBig("BIGF", { { "data\\ini\\weapon.ini", "patch" } }));
	Win32BIGFileSystem fsys;
	std::string error;
	REQUIRE_MESSAGE(fsys.loadBigFilesFromDirectory(tmp.path.u8string(), "*.big", false, &error), error);
	CHECK(readVirtual(fsys, "data\\ini\\weapon.ini") == "patch");
}

TEST_CASE("install paths: environment first, then config file, else a loud error")
{
	TempDir tmp;
	fs::path cfg = tmp.path / "install.cfg";
	{
		std::ofstream out(cfg);
		out << "# comment\nOPENBFME_TEST_INSTALL_KEY = C:/Games/RotWK\r\n";
	}
	std::string value;
	std::string error;
	REQUIRE(ResolveInstallPath("OPENBFME_TEST_INSTALL_KEY", cfg.u8string(), value, &error));
	CHECK(value == "C:/Games/RotWK");
	CHECK_FALSE(ResolveInstallPath("OPENBFME_TEST_UNSET_KEY", cfg.u8string(), value, &error));
	CHECK(error.find("OPENBFME_TEST_UNSET_KEY is not set") != std::string::npos);
}
