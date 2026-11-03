// OpenBFME unit tests: lane MODS-1, -mod over the real RotWK 2.01 archives, and the mod corpus gate. GPL-3.0.
//
// Both need ROTWK_INSTALL and BFME2_INSTALL (SKIP otherwise). The data-only mod is generated from the player's own files at runtime (nothing
// retail is committed). The corpus gate mounts every mod found in OPENBFME_MOD_CORPUS (a folder of mod folders and mod archives on the
// developer's machine, never committed: PLAN rule 6) over retail and loads the object world through the retail parsers.

#include "doctest.h"
#include "BigTestUtil.h"
#include "RetailTestMount.h"

#include "Common/AsciiString.h"
#include "Common/MiniJson.h"
#include "Common/ModEngineFeatures.h"
#include "Common/ModMount.h"
#include "Libraries/Source/Apt/AptFile.h"
#include "Libraries/Source/Apt/AptLoad.h"
#include "Common/RetailArchivePolicy.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponStores.h"
#include "Common/Thing/ThingFactory.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <memory>
#include <random>
#include <regex>

namespace fs = std::filesystem;

namespace
{

std::string env(const char *name)
{
	const char *v = std::getenv(name);
	return v ? std::string(v) : std::string();
}

struct TempDir
{
	fs::path path;
	TempDir()
	{
		std::mt19937_64 rng((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
		path = fs::temp_directory_path() / ("openbfme-mods1r-" + std::to_string(rng()));
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

// a fresh pure 2.01 mount (a mod changes the tree, so the process-wide retailtest::pureMount is not used); the md5 cache keeps repeat runs fast
std::unique_ptr<Win32BIGFileSystem> freshRetailMount(std::string *error, RetailMountReport *reportOut = nullptr)
{
	std::vector<RetailInstall> installs(2);
	installs[0].label = "rotwk";
	installs[0].root = env("ROTWK_INSTALL");
	installs[1].label = "bfme2";
	installs[1].root = env("BFME2_INSTALL");
	if (!RetailArchivePolicy::loadBuiltin("rotwk-201", installs[0].policy, error) || !RetailArchivePolicy::loadBuiltin("bfme2-106", installs[1].policy, error))
	{
		return nullptr;
	}
	auto fsys = std::make_unique<Win32BIGFileSystem>();
	RetailMountOptions options;
	options.md5CachePath = (fs::temp_directory_path() / "openbfme-mods1-retail-md5.tsv").u8string();
	const RetailMountReport report = MountRetailArchives(*fsys, installs, options);
	if (reportOut)
	{
		*reportOut = report;
	}
	if (!report.ok)
	{
		for (const std::string &e : report.errors)
		{
			*error += e + "\n";
		}
		return nullptr;
	}
	return fsys;
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

// lane MODUI-1: one corpus entry mounted over a fresh 2.01 mount as the launcher mounts it (shared by the MODS-1 object world and the
// MODUI-1 Apt checks); skip = a dependency laid out for another package
struct CorpusMount
{
	bool skip = false;
	bool aio = false;
	bool installMod = false;
	fs::path modPart;
	std::string engineEdits;
	std::vector<std::string> engineMd5s;
	ModMountReport report;
	std::unique_ptr<Win32BIGFileSystem> fsys;
};

CorpusMount mountCorpusEntry(const fs::path &p)
{
	CorpusMount c;
	std::string error;
	RetailMountReport retail;
	c.fsys = freshRetailMount(&error, &retail);
	REQUIRE_MESSAGE(c.fsys, error);
	ModMountOptions options;
	options.rotwkInstallRoot = env("ROTWK_INSTALL");
	options.md5CachePath = (fs::temp_directory_path() / "openbfme-mods1-corpus-md5.tsv").u8string();
	// an All In One package laid out by tools/mod/aio_package.py (openbfme-aio.json schema 2): the install/ folders of its stack in priority
	// order are one install-folder mod (lane MODS-2), the stack's mod/ folder a -mod operand; any other folder or .big is a -mod operand
	c.aio = fs::exists(p / "openbfme-aio.json");
	std::vector<std::string> installs;
	c.modPart = p;
	if (c.aio)
	{
		std::ifstream manifest(p / "openbfme-aio.json", std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(manifest)), std::istreambuf_iterator<char>());
		JsonValue root;
		REQUIRE_MESSAGE(JsonValue::parse(text, root, &error), error);
		const JsonValue *stack = root.get("stack");
		if (!stack || !stack->isArray())
		{
			c.skip = true; // a dependency laid out for another package (dependency_only)
			return c;
		}
		c.modPart.clear();
		for (const JsonValue &member : stack->array)
		{
			const fs::path folder = p.parent_path() / fs::u8path(member.get("folder")->string);
			if (fs::is_directory(folder / "install"))
			{
				installs.push_back((folder / "install").u8string());
			}
			if (c.modPart.empty() && fs::is_directory(folder / "mod"))
			{
				c.modPart = folder / "mod";
			}
		}
		if (const JsonValue *edits = root.get("engine_edits"))
		{
			for (const JsonValue &e : edits->array)
			{
				c.engineEdits += (c.engineEdits.empty() ? "" : "; ") + e.string;
				const size_t at = e.string.rfind(" md5 "); // "<package>: <file> md5 <hex>" (tools/mod/aio_package.py)
				if (at != std::string::npos)
				{
					c.engineMd5s.push_back(e.string.substr(at + 5));
				}
			}
		}
	}
	c.installMod = !installs.empty();
	c.report.ok = true;
	if (c.installMod)
	{
		auto merged = std::make_unique<Win32BIGFileSystem>();
		c.report = MountInstallOverlay(*merged, retail, installs, options);
		if (c.report.ok)
		{
			c.fsys = std::move(merged);
		}
	}
	if (c.report.ok && !c.modPart.empty())
	{
		ModOperand op;
		REQUIRE_MESSAGE(ParseModArgument(c.modPart.u8string(), std::string(), op, &error), error);
		const ModMountReport modReport = MountRetailMod(*c.fsys, op, options);
		if (!c.installMod)
		{
			c.report = modReport;
		}
		else if (!modReport.ok)
		{
			c.report.ok = false;
			c.report.errors = modReport.errors;
		}
	}
	return c;
}

// the mods of OPENBFME_MOD_CORPUS (a folder of mod folders and mod archives), sorted
std::vector<fs::path> corpusEntries(const std::string &corpus)
{
	std::vector<fs::path> mods;
	std::error_code ec;
	for (fs::directory_iterator it(fs::u8path(corpus), ec), end; !ec && it != end; it.increment(ec))
	{
		if (it->is_directory() || AsciiStringUtil::lowered(it->path().extension().u8string()) == ".big")
		{
			mods.push_back(it->path());
		}
	}
	REQUIRE_MESSAGE(!ec, "cannot list " << corpus << ": " << ec.message());
	REQUIRE_MESSAGE(!mods.empty(), "OPENBFME_MOD_CORPUS holds no mod folder or .big: " << corpus);
	std::sort(mods.begin(), mods.end());
	return mods;
}

} // namespace

TEST_CASE("MODS-1 retail: a generated data-only mod over the 2.01 archives: a replaced INI file, a loose and an archived new object")
{
	if (env("ROTWK_INSTALL").empty() || env("BFME2_INSTALL").empty())
	{
		retailtest::printSkip("MODS-1 retail");
		return;
	}
	std::string error;
	std::unique_ptr<Win32BIGFileSystem> fsys = freshRetailMount(&error);
	REQUIRE_MESSAGE(fsys, error);

	// the mod: retail's weapon.ini with LegolasBow's AttackRange changed (a replaced file, stored with another case), one object as a loose
	// file and one in the mod folder's archive (new files that only the directory listing finds)
	std::vector<std::uint8_t> weaponIni;
	REQUIRE_MESSAGE(fsys->readFile("data\\ini\\weapon.ini", weaponIni, &error), error);
	std::string text(weaponIni.begin(), weaponIni.end());
	const std::regex attackRange(R"((Weapon LegolasBow\r?\n[\s\S]*?)AttackRange[ \t]*=[^\r\n]*)");
	std::smatch m;
	REQUIRE(std::regex_search(text, m, attackRange));
	const std::string modded = m.prefix().str() + m[1].str() + "AttackRange = 123.0" + m.suffix().str();
	REQUIRE(modded != text);

	TempDir tmp;
	const fs::path mod = tmp.path / "Mods1TestMod";
	writeFile(mod / "Data" / "INI" / "Weapon.ini", modded);
	writeFile(mod / "data" / "ini" / "object" / "mods1" / "mods1_loose.ini", "Object MODS1_LooseObject\n  KindOf = INERT\nEnd\n");
	const std::vector<std::uint8_t> big =
		bigtest::makeBig("BIGF", { { "data\\ini\\object\\mods1\\mods1_archive.ini", "Object MODS1_ArchiveObject\n  KindOf = INERT\nEnd\n" } });
	writeFile(mod / "Mods1Test.big", std::string(big.begin(), big.end()));

	ModOperand op;
	REQUIRE_MESSAGE(ParseModArgument(mod.u8string(), std::string(), op, &error), error);
	ModMountOptions options;
	options.rotwkInstallRoot = env("ROTWK_INSTALL"); // the player's install: no loose file there may shadow an archive (PLAN rule 7)
	const ModMountReport report = MountRetailMod(*fsys, op, options);
	REQUIRE_MESSAGE(report.ok, joined(report.errors));
	CHECK(report.archives.size() == 1);
	CHECK(report.loose.files.size() == 3);
	CHECK(fsys->sourcesOf("data\\ini\\weapon.ini").front() == "mod-folder:Data\\INI\\Weapon.ini");

	RetailObjectWorld world(*fsys);
	REQUIRE_MESSAGE(world.load(&error), error);
	for (const auto &e : world.report().errors)
	{
		MESSAGE(e.file << ": " << e.message);
	}
	CHECK(world.report().errors.empty()); // as without the mod (test_weapon_data.cpp "WeaponSet retail")
	CHECK(world.things().findTemplate("MODS1_LooseObject") != nullptr);
	CHECK(world.things().findTemplate("MODS1_ArchiveObject") != nullptr);
	CHECK(world.things().findTemplate("GondorFighter") != nullptr); // the rest of retail is still there
	const WeaponTemplate *bow = world.weaponStores().weapons().findWeaponTemplate("LegolasBow");
	REQUIRE(bow != nullptr);
	CHECK(bow->m_attackRange == 123.0f);
	CHECK(world.weaponStores().weapons().templates().size() == 747); // test_weapon_data.cpp: the retail count, the file replaced not added
}

TEST_CASE("MODS-1 mod corpus: every mod in OPENBFME_MOD_CORPUS mounts over 2.01 and its object world loads through the retail parsers")
{
	const std::string corpus = env("OPENBFME_MOD_CORPUS");
	if (env("ROTWK_INSTALL").empty() || env("BFME2_INSTALL").empty() || corpus.empty())
	{
		retailtest::printSkip("MODS-1 mod corpus (OPENBFME_MOD_CORPUS unset)");
		return;
	}
	const std::vector<fs::path> mods = corpusEntries(corpus);
	for (const fs::path &p : mods)
	{
		CAPTURE(p.u8string());
		std::string error;
		CorpusMount c = mountCorpusEntry(p);
		if (c.skip)
		{
			continue;
		}
		std::unique_ptr<Win32BIGFileSystem> &fsys = c.fsys;
		const bool aio = c.aio, installMod = c.installMod;
		const fs::path &modPart = c.modPart;
		const std::string &engineEdits = c.engineEdits;
		const std::vector<std::string> &engineMd5s = c.engineMd5s;
		const ModMountReport &report = c.report;
		if (aio)
		{
			MESSAGE("MODCORPUS " << p.filename().u8string() << " engine edits (not loaded): " << (engineEdits.empty() ? "none" : engineEdits));
		}
		// lane MODS-3: the stack's modified game binaries turn on their ModEngineFeatures profiles (the launcher's --mod-engine); every binary of the
		// corpus must be a known one
		struct EngineGuard
		{
			~EngineGuard() { ModEngineFeatures::reset(); }
		} engineGuard;
		CHECK_MESSAGE(ModEngineFeatures::activate(engineMd5s, &error), error);
		for (const std::string &f : ModEngineFeatures::identityFeatures())
		{
			MESSAGE("MODCORPUS   " << f);
		}
		if (!report.ok)
		{
			continue;
		}
		RetailObjectWorld world(*fsys);
		const bool loaded = world.load(&error);
		CHECK_MESSAGE(loaded, error);
		const size_t errors = loaded ? world.report().errors.size() : 0;
		// the report a T1 run pins once the corpus is on the gate hosts (counts only, no mod bytes): MODS-2 turns the errors into fixes
		MESSAGE("MODCORPUS " << p.filename().u8string() << (installMod ? " (install-folder mod" : " (-mod") << (installMod && !modPart.empty() ? " + -mod)" : ")") << ": archives " << report.archives.size() << ", loose files " << report.loose.files.size()
							 << ", INI errors " << errors << ", load " << (loaded ? world.loadSeconds() : 0.0) << " s");
		if (loaded)
		{
			for (const std::string &n : world.loadNotes())
			{
				MESSAGE("MODCORPUS   note: " << n);
			}
			size_t shown = 0;
			for (const auto &e : world.report().errors)
			{
				if (shown++ == 20)
				{
					break;
				}
				MESSAGE("MODCORPUS   " << e.file << ": " << e.message);
			}
		}
	}
}

TEST_CASE("MODS-2 retail: the pinned 2.01 mount reports S-5302 (the BFME2 apt archives retail never mounts)")
{
	if (env("ROTWK_INSTALL").empty() || env("BFME2_INSTALL").empty())
	{
		retailtest::printSkip("MODS-2 retail S-5302");
		return;
	}
	std::string error;
	RetailMountReport report;
	REQUIRE_MESSAGE(freshRetailMount(&error, &report), error);
	CHECK(report.stops == std::vector<std::string>{ RetailBaseAptStopLine() });
}

namespace
{

// lane MODUI-1: every Apt movie the mounted tree holds at the archive root (the shell screens, their sub-movies, the HUD) loads with its
// .const and resolves its imports; the failures as "<movie>: <why>" (empty = all load)
std::vector<std::string> aptLoadFailures(Win32BIGFileSystem &fsys, size_t *movies)
{
	AptArchiveFileSource source(fsys);
	AptLoader loader(source);
	std::vector<std::string> failures;
	const std::vector<std::string> names = source.listMovies();
	*movies = names.size();
	for (const std::string &name : names)
	{
		std::string error;
		std::shared_ptr<const AptFile> movie = loader.loadMovie(name, &error);
		if (!movie)
		{
			failures.push_back(name + ": " + error);
			continue;
		}
		std::vector<AptResolvedImport> imports;
		if (!loader.resolveImports(*movie, imports, &error))
		{
			failures.push_back(name + ": " + error);
		}
	}
	return failures;
}

} // namespace

TEST_CASE("MODUI-1 mod corpus: every Apt movie of every mod in OPENBFME_MOD_CORPUS loads and resolves its imports, as on 2.01")
{
	const std::string corpus = env("OPENBFME_MOD_CORPUS");
	if (env("ROTWK_INSTALL").empty() || env("BFME2_INSTALL").empty() || corpus.empty())
	{
		retailtest::printSkip("MODUI-1 mod corpus Apt movies (OPENBFME_MOD_CORPUS unset)");
		return;
	}
	{
		std::string error;
		std::unique_ptr<Win32BIGFileSystem> retail = freshRetailMount(&error);
		REQUIRE_MESSAGE(retail, error);
		size_t movies = 0;
		const std::vector<std::string> failures = aptLoadFailures(*retail, &movies);
		MESSAGE("MODUI1 retail 2.01: " << movies << " Apt movies, " << failures.size() << " failed");
		CHECK_MESSAGE(failures.empty(), joined(failures));
	}
	for (const fs::path &p : corpusEntries(corpus))
	{
		CAPTURE(p.u8string());
		CorpusMount c = mountCorpusEntry(p);
		if (c.skip || !c.report.ok)
		{
			continue; // a mount failure is the MODS-1 corpus case's
		}
		size_t movies = 0;
		const std::vector<std::string> failures = aptLoadFailures(*c.fsys, &movies);
		MESSAGE("MODUI1 " << p.filename().u8string() << ": " << movies << " Apt movies, " << failures.size() << " failed");
		CHECK_MESSAGE(failures.empty(), p.filename().u8string() << ":\n" << joined(failures));
	}
}
