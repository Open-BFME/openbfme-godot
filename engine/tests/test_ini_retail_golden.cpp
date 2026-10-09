// OpenBFME retail golden tests for the INI machinery (port-order steps 1-6). GPL-3.0.
//
// Runs only when ROTWK_INSTALL and BFME2_INSTALL are set; otherwise it prints SKIP and checks
// nothing. It mounts pure RotWK 2.01 + BFME2 1.06 and runs two passes over the retail INI data:
//
//   1. the subsystem INI load order, with every block keyword registered as a recording stub
//      (spec 3.4): every file the legend lists must load with zero lexer / preprocessor /
//      dispatch errors;
//   2. a pre-pass over every *.ini file in the archives (TextFile + #include + #define only),
//      which is what the spec's census of the DATA tree counts: 8,683 macros, zero duplicates,
//      1,849 math macros (spec 2.2 item 3, 2.4).
//
// The expected numbers come from the spec, never from a run of this code. Where the spec's
// number does not match the retail data, the test asserts the spec number AND names the
// evidence in the failure text (see the mixed-case test).

#include "doctest.h"

#include "Common/AsciiString.h"
#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"
#include "RetailTestMount.h"
#include "Common/SubsystemLegend.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/WeaponStores.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/file/TextFile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{

// spec 2.2 item 3, 2.4, 2.3
const size_t kSpecMacroCount = 8683;
const size_t kSpecMathMacroCount = 1849;
const size_t kSpecMixedCaseReferences = 8;

struct RetailWorld
{
	bool available = false;
	std::string mountError;
	Win32BIGFileSystem *fsys = nullptr; // owned by retailtest::pureMount(), shared by every retail test

	// pass 1: the subsystem load
	INIEnvironment env;
	INIBlockRecorder recorder;
	LocomotorStore locomotors; // HORDE-1: the Locomotor block has a real parser (TheLocomotorStore)
	WeaponStores weaponStores; // WEAPON-1: the Weapon, Armor and DamageFX blocks have real parsers
	SubsystemLegend legend;
	std::unique_ptr<INI> ini;
	SubsystemLoadReport report;
	double loadSeconds = 0;

	// pass 2: pre-pass over every *.ini
	INIEnvironment corpusEnv;
	std::vector<std::string> corpusFiles;
	std::vector<std::string> corpusErrors;

	~RetailWorld() { TheLocomotorStore = nullptr; }

	RetailWorld()
	{
		// ONE shared mount for every retail test (RetailTestMount.cpp): each mount keeps ~213 archives
		// open and a second private mount starves the CRT's stream table.
		retailtest::Mount *mount = retailtest::pureMount();
		if (!mount)
		{
			mountError = "ROTWK_INSTALL / BFME2_INSTALL not set";
			return;
		}
		available = true;
		if (!mount->error.empty() || !mount->fs)
		{
			mountError = mount->error.empty() ? "mount failed" : mount->error;
			return;
		}
		fsys = mount->fs.get();
		// pass 1
		env.fileSystem = fsys;
		TheLocomotorStore = &locomotors;
		weaponStores.install();
		legend.registerBlock(env.blocks);
		// Lenient: retail files nest blocks, and the stubs have no field tables. Block boundaries are
		// NOT certified by this traversal (see INIBlockStubs.h); lexer, #define and dispatch are.
		RegisterRecordingBlockStubs(env.blocks, recorder, { "LoadSubsystem" }, StubExtent::Lenient);
		ini = std::make_unique<INI>(env);
		SubsystemLoadOptions options;
		options.collectErrors = true;
		const auto t0 = std::chrono::steady_clock::now();
		RunSubsystemIniLoad(legend, *ini, options, report);
		loadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

		// pass 2
		const auto tCorpus = std::chrono::steady_clock::now();
		corpusEnv.fileSystem = fsys;
		FilenameList list;
		fsys->getFileListInDirectory(std::string(), "Data\\", "*.ini", list, true);
		INI corpusIni(corpusEnv);
		for (const std::string &file : list)
		{
			corpusFiles.push_back(file);
			try
			{
				corpusIni.preprocessFile(file, INI_LOAD_OVERWRITE);
			}
			catch (const INIException &e)
			{
				corpusErrors.push_back(file + ": " + e.message());
			}
		}
		std::printf("INI retail corpus pre-pass: %zu files in %.1f s\n", corpusFiles.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - tCorpus).count());
	}
};

RetailWorld &world()
{
	static RetailWorld w;
	return w;
}

// Returns false (after printing SKIP) when the retail installs are not configured.
bool haveRetail()
{
	RetailWorld &w = world();
	if (!w.available)
	{
		std::printf("SKIP: INI retail golden tests need ROTWK_INSTALL and BFME2_INSTALL (%s)\n", w.mountError.c_str());
		return false;
	}
	REQUIRE_MESSAGE(w.mountError.empty(), "retail mount failed:\n" << w.mountError);
	return true;
}

TextFile::FileReader readerFor(RetailWorld &w)
{
	Win32BIGFileSystem *fs = w.fsys;
	return [fs](const std::string &path, std::vector<std::uint8_t> &out, std::string *error) { return fs->readFile(path, out, error); };
}

bool equalsNoCase(const std::string &a, const std::string &b)
{
	return AsciiStringUtil::compareNoCase(a, b) == 0;
}

} // namespace

TEST_CASE("retail golden: TextFile expands the riskcampaign.ini include tree (spec 2.1)")
{
	if (!haveRetail())
	{
		return;
	}
	RetailWorld &w = world();
	TextFile tf;
	std::string error;
	REQUIRE_MESSAGE(tf.parseFile("Data\\INI\\Campaigns\\RiskCampaign.ini", readerFor(w), &error), error);
	// 18 scenario includes, each with its own nested includes of the Common\ files, counted by
	// walking the uncommented #include lines of the retail files (the ';#include' and
	// '//#include' lines in that tree are commented out and do not fire)
	CHECK(tf.fileCount() == 62);
	CHECK(tf.fileName(1) == "Data\\INI\\Campaigns\\Scenarios\\WOTRScenario044.inc");
	for (const TextFile::Line &line : tf.lines())
	{
		CHECK_MESSAGE(line.text.find("#include") == std::string::npos, line.text);
	}
}

TEST_CASE("retail golden: a file full of lone '/' parses, each line appended slashes+1 times (spec 1.3)")
{
	if (!haveRetail())
	{
		return;
	}
	RetailWorld &w = world();
	TextFile tf;
	std::string error;
	REQUIRE_MESSAGE(tf.parseFile("Data\\INI\\AptButtonTooltipMap.ini", readerFor(w), &error), error);
	int hits = 0;
	for (const TextFile::Line &line : tf.lines())
	{
		if (line.text == "\tButtonMap = MainMenu/TutorialsNav/BasicTutorial/bttn OpeningMenu/BasicTutorialButton")
		{
			++hits;
		}
	}
	// the line holds 4 lone slashes, so retail appends it 5 times (RW 0xA15C1F-0xA15D2F)
	CHECK(hits == 5);
}

TEST_CASE("retail golden: LENIENT-stub traversal of the subsystem INI load has zero lexer / preprocessor / dispatch errors (does NOT certify block boundaries or End validation)")
{
	if (!haveRetail())
	{
		return;
	}
	RetailWorld &w = world();
	std::printf("INI retail load: %zu files in %.1f s, %zu stub blocks recorded (%zu ended on End), %zu macros\n", w.ini->loadedFiles().size(), w.loadSeconds,
		w.recorder.records.size(), w.recorder.endedCount(), w.env.macros.size());
	std::printf("  legend entries %zu; order-table names without a legend entry (hard-coded paths not recovered): ", w.legend.entries().size());
	for (const std::string &n : w.report.noLegendEntry)
	{
		std::printf("%s ", n.c_str());
	}
	std::printf("\n  legend entries GameEngine::init never names (ACCEPTANCE STOP: order UNVERIFIED, loaded last): ");
	for (const std::string &n : w.report.unverifiedOrder)
	{
		std::printf("%s ", n.c_str());
	}
	std::printf("\n  never loaded (spec 3.5): ");
	for (const std::string &n : w.report.neverLoaded)
	{
		std::printf("%s ", n.c_str());
	}
	std::printf("\n  skipped (IncludePathCinematics): ");
	for (const std::string &n : w.report.skippedCinematics)
	{
		std::printf("[%s] ", n.c_str());
	}
	std::printf("\n");

	for (const SubsystemLoadReport::FileError &e : w.report.errors)
	{
		MESSAGE("ERROR " << e.file << " (code " << e.code << "): " << e.message);
	}
	CHECK(w.report.errors.empty());
	CHECK(w.recorder.records.size() > 1000);
	// ORDER IS UNVERIFIED for these six legend entries (GameEngine::init does not name them; RW xrefs 0x616FC2, 0x618684, 0x6209EA, 0x620DE3, 0x621D68, 0x624A07):
	// the loader must report exactly this list, so a clean error list cannot be read as a verified startup order
	CHECK(w.report.unverifiedOrder == std::vector<std::string>{ "TheBannerUI", "TheFontLibrary", "ArmySummaryDescription", "StrategicHUD", "InGameNotificationBox",
		"AptButtonTooltipMap" });
}

TEST_CASE("retail golden: every file the subsystem legend lists was loaded (spec 3.4)")
{
	if (!haveRetail())
	{
		return;
	}
	RetailWorld &w = world();
	std::set<std::string> loaded;
	for (const std::string &f : w.ini->loadedFiles())
	{
		loaded.insert(AsciiStringUtil::lowered(f));
	}
	const std::vector<std::string> &never = RotwkNeverLoadedSubsystems();
	size_t listed = 0;
	size_t missing = 0;
	for (const SubsystemLegendEntry &entry : w.legend.entries())
	{
		if (std::find(never.begin(), never.end(), entry.name) != never.end())
		{
			continue;
		}
		for (const std::string &f : entry.initFile)
		{
			bool skippedCinematic = false;
			for (const std::string &c : entry.includePathCinematics)
			{
				if (equalsNoCase(f.substr(0, std::min(f.size(), c.size())), c))
				{
					skippedCinematic = true;
				}
			}
			if (skippedCinematic)
			{
				continue;
			}
			++listed;
			if (!loaded.count(AsciiStringUtil::lowered(f)))
			{
				++missing;
				MESSAGE("legend file not loaded: " << entry.name << " " << f);
			}
		}
	}
	CHECK(listed > 40);
	CHECK(missing == 0);
	// the directory loads (TheThingFactory InitPath) contributed many more
	CHECK(loaded.size() > listed);
}

TEST_CASE("retail golden: all 8,683 macros define with zero duplicates and zero errors across every *.ini (spec 2.2 item 3)")
{
	if (!haveRetail())
	{
		return;
	}
	RetailWorld &w = world();
	for (const std::string &e : w.corpusErrors)
	{
		MESSAGE("pre-pass error: " << e);
	}
	CHECK(w.corpusErrors.empty());
	CHECK_MESSAGE(w.corpusFiles.size() > 600, "ini files listed: " << w.corpusFiles.size());
	CHECK_MESSAGE(w.corpusEnv.macros.size() == kSpecMacroCount, "macros defined across all *.ini files: " << w.corpusEnv.macros.size());
}

// Sol review P1: accounting of the 8,683 against what normal startup loads. Normal startup now
// includes the hard-coded TheAudio / TheEva loads (RW 0x4538DA-0x453A34, 0x5DE837-0x5DE868), so
// the legend-driven load misses only the cinematic files (RW 0x5B4C25-0x5B4C42 excludes them when
// cinematics are off: 33 macros) and optionregistry.ini (an OptionGroup file the block table
// cannot load: 2 macros). 140 before the audio/Eva loads = 105 + 33 + 2.
TEST_CASE("retail golden: normal startup defines 8,683 - 33 cinematic - 2 OptionGroup macros")
{
	if (!haveRetail())
	{
		return;
	}
	RetailWorld &w = world();
	CHECK_MESSAGE(w.env.macros.size() == kSpecMacroCount - 33 - 2, "macros defined by the startup load: " << w.env.macros.size());
	// every macro the startup load misses lives in cinematicobjects.ini or optionregistry.ini
	size_t missing = 0;
	for (const auto &entry : w.corpusEnv.macros.entries())
	{
		if (!w.env.macros.find(entry.second.name.c_str()))
		{
			++missing;
		}
	}
	CHECK(missing == 35);
	// audio and Eva macros named in the review are now defined
	CHECK(w.env.macros.find("MOVIE_VOLUME") != nullptr);
	CHECK(w.env.macros.find("EVA_UNIT_CREATED_PRIORITY") != nullptr);
}

namespace
{
std::string goldenPath(const char *name)
{
	return std::string(OPENBFME_TESTDATA_DIR) + "/" + name;
}

// A complete "0x" + 8 hex digit field, nothing else (strtoul would accept a valid prefix followed by garbage)
bool parseBits32(const std::string &field, std::uint32_t &out)
{
	if (field.size() != 10 || field[0] != '0' || field[1] != 'x')
	{
		return false;
	}
	std::uint32_t v = 0;
	for (size_t i = 2; i < 10; ++i)
	{
		const char c = field[i];
		const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
		if (d < 0)
		{
			return false;
		}
		v = (v << 4) | (std::uint32_t)d;
	}
	out = v;
	return true;
}

// tab-separated rows; fails loudly when the golden table is absent
std::vector<std::vector<std::string>> readTsv(const char *name)
{
	std::vector<std::vector<std::string>> rows;
	std::FILE *f = std::fopen(goldenPath(name).c_str(), "rb");
	REQUIRE_MESSAGE(f != nullptr, "golden table missing: " << goldenPath(name));
	std::string line;
	int ch;
	while ((ch = std::fgetc(f)) != EOF)
	{
		if (ch == '\n')
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back(); // the table may have been checked out with CRLF line endings
			}
			if (!line.empty())
			{
				std::vector<std::string> cols;
				size_t pos = 0;
				for (;;)
				{
					const size_t t = line.find('\t', pos);
					cols.push_back(line.substr(pos, t == std::string::npos ? std::string::npos : t - pos));
					if (t == std::string::npos)
					{
						break;
					}
					pos = t + 1;
				}
				rows.push_back(cols);
			}
			line.clear();
		}
		else
		{
			line.push_back((char)ch);
		}
	}
	std::fclose(f);
	return rows;
}
}

// The expected tokens and values come from tools/ini_macro_oracle.py (an independent scan of the
// data tree), never from the table under test.
TEST_CASE("retail golden: the 8 mixed-case macro references resolve through the case-insensitive name lookup (spec 2.3)")
{
	if (!haveRetail())
	{
		return;
	}
	RetailWorld &w = world();
	TextFile::FileReader reader = readerFor(w);
	INIEnvironment &env = w.corpusEnv;

	// every uncommented token of every *.ini (includes expanded) whose name differs from a macro only by case
	std::set<std::string> found;
	for (const std::string &file : w.corpusFiles)
	{
		TextFile tf;
		std::string error;
		REQUIRE_MESSAGE(tf.parseFile(file, reader, &error), file << ": " << error);
		for (const TextFile::Line &line : tf.lines())
		{
			if (line.text.compare(0, 7, "#define") == 0)
			{
				continue;
			}
			size_t pos = 0;
			while (pos < line.text.size())
			{
				const size_t b = line.text.find_first_not_of(" \n\r\t=", pos);
				if (b == std::string::npos)
				{
					break;
				}
				size_t e = line.text.find_first_of(" \n\r\t=", b);
				if (e == std::string::npos)
				{
					e = line.text.size();
				}
				const std::string token = line.text.substr(b, e - b);
				pos = e;
				if (token[0] >= '0' && token[0] <= '9')
				{
					continue;
				}
				const INIMacroTable::Entry *m = env.macros.find(token.c_str());
				if (m && m->name != token)
				{
					found.insert(token);
				}
			}
		}
	}

	const std::vector<std::vector<std::string>> golden = readTsv("ini_mixed_case_refs.tsv");
	std::set<std::string> expected;
	INI ini(env);
	for (const auto &row : golden)
	{
		REQUIRE(row.size() == 3);
		expected.insert(row[0]);
		// it resolves to the value the independent scan read from the file
		CHECK_MESSAGE(std::string(ini.preprocessMacro(row[0].c_str())) == row[2], row[0]);
		const INIMacroTable::Entry *m = env.macros.find(row[0].c_str());
		REQUIRE_MESSAGE(m != nullptr, row[0]);
		CHECK(m->name == row[1]);
	}
	CHECK(golden.size() == kSpecMixedCaseReferences);
	CHECK(found == expected);
}

// Expected float32 bits come from tools/ini_macro_oracle.py (numpy float32, a different form of
// evaluator); the engine must reproduce every one.
TEST_CASE("retail golden: all 1,849 math macros evaluate to the oracle's float32 bits (spec 2.2 item 3, 2.4)")
{
	if (!haveRetail())
	{
		return;
	}
	RetailWorld &w = world();
	const std::vector<std::vector<std::string>> golden = readTsv("ini_math_macros.tsv");
	CHECK(golden.size() == kSpecMathMacroCount);

	size_t math = 0;
	for (const auto &entry : w.corpusEnv.macros.entries())
	{
		if (!entry.second.value.empty() && entry.second.value[0] == '#')
		{
			++math;
		}
	}
	CHECK(math == kSpecMathMacroCount);

	INI ini(w.corpusEnv);
	size_t mismatches = 0;
	size_t errors = 0;
	for (const auto &row : golden)
	{
		REQUIRE(row.size() == 2);
		std::uint32_t expectedBits = 0;
		REQUIRE_MESSAGE(parseBits32(row[1], expectedBits), "golden table field is not a complete 0xXXXXXXXX value: '" << row[1] << "' for " << row[0]);
		try
		{
			const float v = ini.scanReal(row[0].c_str());
			std::uint32_t bits;
			std::memcpy(&bits, &v, sizeof(bits));
			if (bits != expectedBits)
			{
				++mismatches;
				MESSAGE("math macro " << row[0] << ": engine bits " << bits << " oracle " << row[1]);
			}
		}
		catch (const INIException &e)
		{
			++errors;
			MESSAGE("math macro " << row[0] << ": " << e.message());
		}
	}
	CHECK(errors == 0);
	CHECK(mismatches == 0);
}

// not retail-gated: guards the golden table reader itself (Sol review round 3)
TEST_CASE("golden table numeric fields must be complete: a valid prefix followed by garbage is rejected")
{
	std::uint32_t v = 0;
	CHECK(parseBits32("0x0077CF3C", v));
	CHECK(v == 0x0077CF3Cu);
	CHECK(parseBits32("0xdeadBEEF", v));
	CHECK_FALSE(parseBits32("0x0077CF3CZ", v));
	CHECK_FALSE(parseBits32("0x0077CF3C ", v));
	CHECK_FALSE(parseBits32("0x0077CF3", v));
	CHECK_FALSE(parseBits32("0077CF3C00", v));
	CHECK_FALSE(parseBits32("0x0077CF3G", v));
	CHECK_FALSE(parseBits32("", v));
}
