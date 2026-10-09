// OpenBFME unit tests: SubsystemLegend, loadDirectory, init order, block stubs (port-order step 6).
// GPL-3.0. Expected values come from spec ini-and-object-model.md sections 3.2-3.5 and the B1
// sources cited in SubsystemLegend.h, not from running the code.

#include "doctest.h"
#include "IniTestUtil.h"

#include "Common/AsciiString.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/SubsystemLegend.h"
#include "GameLogic/WeaponStores.h"

#include <algorithm>
#include <set>

using namespace initest;

namespace
{

typedef std::vector<std::string> Strs;

// WEAPON-1: the Weapon, Armor and DamageFX blocks have real parsers now; the synthetic trees below hold empty `Armor X / End` blocks, which
// parse into the global stores this fixture installs for its lifetime.
struct LegendFixture : initest::Fixture
{
	LegendFixture() { stores.install(); }
	explicit LegendFixture(const FileList &files) : initest::Fixture(files) { stores.install(); }
	WeaponStores stores;
};

// A tree of INI files, each holding one stub block so every load is observable.
FileList tree()
{
	const std::string body = "Armor X\nEnd\n";
	return {
		{ "Data\\INI\\Z.ini", body },
		{ "Data\\INI\\a.ini", body },
		{ "Data\\INI\\M.INI", body },
		{ "Data\\INI\\notes.txt", "not an ini file\n" },
		{ "Data\\INI\\Sub\\b.ini", body },
		{ "Data\\INI\\Sub\\Deep\\c.ini", body },
		{ "Data\\INI\\Other\\d.ini", body },
		{ "Data\\INI\\Other\\skipme.ini", body },
		{ "Data\\INI\\Cinematic\\film.ini", body },
	};
}

std::set<std::string> lowerSet(const Strs &v)
{
	std::set<std::string> out;
	for (const std::string &s : v)
	{
		out.insert(AsciiStringUtil::lowered(s));
	}
	return out;
}

} // namespace

TEST_CASE("block keyword table: 131 RotWK keywords, none of the Zero Hour-only ones (spec 3.2)")
{
	const Strs &k = RotwkBlockKeywords();
	CHECK(k.size() == 131);
	const std::set<std::string> unique(k.begin(), k.end());
	CHECK(unique.size() == 131);
	for (const char *absent : { "Animation", "Campaign", "ChallengeGenerals", "Credits", "EvaEvent", "InGameUI", "MapCache", "MapData",
			 "MultiplayerStartingMoneyChoice", "ParticleSystem", "Video", "WebpageURL", "OverrideableByLikeKind" })
	{
		CHECK_MESSAGE(unique.count(absent) == 0, absent);
	}
	for (const char *present : { "Object", "ObjectReskin", "ChildObject", "LoadSubsystem", "Pathfinder", "ModifierList", "AptButtonTooltipMap" })
	{
		CHECK_MESSAGE(unique.count(present) == 1, present);
	}
}

TEST_CASE("recording stubs: keywords are case sensitive, an unknown first line is a loud error, extents are recorded")
{
	Fixture fx;
	INIBlockRecorder rec;
	RegisterRecordingBlockStubs(fx.env.blocks, rec, {});
	CHECK(fx.env.blocks.size() == 131);

	const std::string text =
		"; header\n"
		"Science A\n"
		"  Foo = 1\n"
		"End\n"
		"ReallyLowMHz = 1200\n"
		"Upgrade B\n"
		"  Nested = 1\n"
		"  End\n"
		"End\n"
		"Object C\n"
		"End\n";
	REQUIRE(loadError(fx.env, "t.ini", text).empty());
	REQUIRE(rec.records.size() == 4);
	CHECK(rec.records[0].keyword == "Science");
	CHECK(rec.records[0].name == "A");
	CHECK(rec.records[0].extentLines == 2);
	CHECK(rec.records[0].endsWithEnd);
	CHECK(rec.records[1].keyword == "ReallyLowMHz"); // a single-line directive: a block with no End
	CHECK(rec.records[1].extentLines == 0);
	CHECK_FALSE(rec.records[1].endsWithEnd);
	CHECK(rec.records[2].keyword == "Upgrade"); // the indented End is nested, not the block's
	CHECK(rec.records[2].extentLines == 3);
	CHECK(rec.records[3].keyword == "Object");
	CHECK(rec.endedCount() == 3);
	CHECK(rec.countByKeyword["Science"] == 1);

	// Sol review P2: an unknown block after a closed one is reported, not swallowed
	int bogusCode = 0;
	const std::string bogus = loadError(fx.env, "b.ini", "Science A\nEnd\nBogus X\nEnd\n", INI_LOAD_OVERWRITE, &bogusCode);
	CHECK(bogusCode == 5);
	CHECK(bogus.find("Unknown block 'Bogus'.") == 0);
	// and a block that never ends is code 4
	CHECK(loadError(fx.env, "m.ini", "Science A\n Foo = 1\n", INI_LOAD_OVERWRITE, &bogusCode).find("Missing 'END' token.") == 0);
	CHECK(bogusCode == 4);

	int code = 0;
	const std::string err = loadError(fx.env, "u.ini", "science A\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 5);
	CHECK(err.find("Unknown block 'science'.") == 0);
	// RotWK has no Video block: a Zero Hour file loaded through INI::load fails loudly (spec 3.5)
	CHECK(loadError(fx.env, "v.ini", "Video X\nEnd\n", INI_LOAD_OVERWRITE, &code).find("Unknown block 'Video'.") == 0);
}

TEST_CASE("lenient recording stubs traverse nested blocks but swallow anything between keywords (documented limitation)")
{
	LegendFixture fx;
	INIBlockRecorder rec;
	RegisterRecordingBlockStubs(fx.env.blocks, rec, {}, StubExtent::Lenient);
	REQUIRE(loadError(fx.env, "t.ini", "Science A\nBehavior = X tag\nEnd\nEnd\nBogus X\nEnd\nUpgrade B\nEnd\n").empty());
	REQUIRE(rec.records.size() == 2); // Bogus was swallowed by Science: lenient mode cannot see it
	CHECK(rec.records[0].extentLines == 5);
	CHECK(rec.records[1].keyword == "Upgrade");
}

TEST_CASE("loadDirectory: *.ini only, pass 1 (this directory) before pass 2 (subdirectories), each in case-insensitive sorted order (spec 3.4)")
{
	LegendFixture fx(tree());
	INIBlockRecorder rec;
	RegisterRecordingBlockStubs(fx.env.blocks, rec, {});
	INI ini(fx.env);

	ini.loadDirectory("Data\\INI", true, INI_LOAD_OVERWRITE); // no trailing separator: it is appended
	// pass 1: a, M, Z (nocase); pass 2: Cinematic\film, Other\d, Other\skipme, Sub\b, Sub\Deep\c
	std::vector<std::string> got;
	for (const std::string &f : ini.loadedFiles())
	{
		got.push_back(AsciiStringUtil::lowered(f));
	}
	const Strs expected = { "data\\ini\\a.ini", "data\\ini\\m.ini", "data\\ini\\z.ini", "data\\ini\\cinematic\\film.ini", "data\\ini\\other\\d.ini",
		"data\\ini\\other\\skipme.ini", "data\\ini\\sub\\b.ini", "data\\ini\\sub\\deep\\c.ini" };
	CHECK(got == expected);
}

TEST_CASE("loadDirectory with subdirs=false loads only the directory itself; an empty name throws")
{
	LegendFixture fx(tree());
	INIBlockRecorder rec;
	RegisterRecordingBlockStubs(fx.env.blocks, rec, {});
	INI ini(fx.env);
	ini.loadDirectory("Data\\INI\\", false, INI_LOAD_OVERWRITE);
	CHECK(ini.loadedFiles().size() == 3);
	CHECK_THROWS_AS(ini.loadDirectory("", true, INI_LOAD_OVERWRITE), INIException);
}

TEST_CASE("loadDirectory ExcludePath prunes files and directories in the subdirectory pass (spec 3.4 item 4; match rule UNVERIFIED)")
{
	LegendFixture fx(tree());
	INIBlockRecorder rec;
	RegisterRecordingBlockStubs(fx.env.blocks, rec, {});
	INI ini(fx.env);
	INILoadDirectoryOptions options;
	options.excludePaths = { "Data\\INI\\Other\\skipme.ini", "data\\ini\\cinematic\\", "Data\\INI\\Sub" };
	ini.loadDirectory("Data\\INI", true, INI_LOAD_OVERWRITE, options);
	CHECK(lowerSet(ini.loadedFiles()) == std::set<std::string>{ "data\\ini\\a.ini", "data\\ini\\m.ini", "data\\ini\\z.ini", "data\\ini\\other\\d.ini" });
}

// RW 0x4352E0-0x435321 (Sol review P2): a plain case-insensitive prefix, no separator boundary
TEST_CASE("loadDirectory ExcludePath is a plain prefix: 'Foo' also excludes 'FooExtra'")
{
	LegendFixture fx(FileList{ { "Data\\INI\\Object\\Keep\\k.ini", "Armor K\nEnd\n" }, { "Data\\INI\\Object\\Foo\\a.ini", "Armor A\nEnd\n" },
		{ "Data\\INI\\Object\\FooExtra\\Unit.ini", "Armor U\nEnd\n" }, { "Data\\INI\\Object\\Foo.ini", "Armor F\nEnd\n" } });
	INIBlockRecorder rec;
	RegisterRecordingBlockStubs(fx.env.blocks, rec, {});
	INI ini(fx.env);
	INILoadDirectoryOptions options;
	options.excludePaths = { "data\\ini\\object\\foo" };
	ini.loadDirectory("Data\\INI\\Object", true, INI_LOAD_OVERWRITE, options);
	// Foo.ini sits directly in the directory (pass 1, never excluded); everything under a path starting with Foo is skipped in pass 2
	CHECK(lowerSet(ini.loadedFiles()) == std::set<std::string>{ "data\\ini\\object\\foo.ini", "data\\ini\\object\\keep\\k.ini" });
}

TEST_CASE("loadDirectory can report per-file errors and continue (test harness mode)")
{
	LegendFixture fx(FileList{ { "Data\\INI\\ok.ini", "Armor X\nEnd\n" }, { "Data\\INI\\bad.ini", "Nonsense\n" }, { "Data\\INI\\ok2.ini", "Armor Y\nEnd\n" } });
	INIBlockRecorder rec;
	RegisterRecordingBlockStubs(fx.env.blocks, rec, {});
	INI ini(fx.env);
	// default: the exception propagates, like retail
	CHECK_THROWS_AS(ini.loadDirectory("Data\\INI", true, INI_LOAD_OVERWRITE), INIException);

	LegendFixture fx2(FileList{ { "Data\\INI\\ok.ini", "Armor X\nEnd\n" }, { "Data\\INI\\bad.ini", "Nonsense\n" }, { "Data\\INI\\ok2.ini", "Armor Y\nEnd\n" } });
	INIBlockRecorder rec2;
	RegisterRecordingBlockStubs(fx2.env.blocks, rec2, {});
	INI ini2(fx2.env);
	INILoadDirectoryOptions options;
	std::vector<std::string> failed;
	options.onFileError = [&failed](const std::string &file, const INIException &) { failed.push_back(file); };
	ini2.loadDirectory("Data\\INI", true, INI_LOAD_OVERWRITE, options);
	CHECK(failed.size() == 1);
	CHECK(ini2.loadedFiles().size() == 2);
}

TEST_CASE("SubsystemLegend: LoadSubsystem blocks with appended string vectors, Loader lookup and InitFileDebug (spec 3.4 item 1)")
{
	const std::string legend =
		"LoadSubsystem TheWritableGlobalData\n"
		"  Loader = INI\n"
		"  InitFile = Data\\INI\\Default\\GameData.ini \n"
		"  InitFile = Data\\INI\\GameData.ini\n"
		"  InitFileDebug = Data\\INI\\GameDataDebug.ini\n"
		"End\n"
		"\n"
		"LoadSubsystem TheThingFactory\n"
		"  Loader = INI\n"
		"  InitFile = Data\\INI\\Default\\Object.ini\n"
		"  InitPath = Data\\INI\\Object\n"
		"  ExcludePath = Data\\INI\\Object\\Cinematic\\CinematicWeapon.ini\t\t; trailing comment\n"
		"  ExcludePath = Data\\INI\\Object\\Cinematic\\CinematicLocomotor.ini\n"
		"  IncludePathCinematics = Data\\INI\\Object\\Cinematic\\\n"
		"  Extension = ini\n"
		"End\n";
	LegendFixture fx;
	SubsystemLegend sl;
	sl.registerBlock(fx.env.blocks);
	REQUIRE(loadError(fx.env, "legend.ini", legend).empty());
	REQUIRE(sl.entries().size() == 2);
	const SubsystemLegendEntry *g = sl.findEntry("TheWritableGlobalData");
	REQUIRE(g);
	CHECK(g->initFile == Strs{ "Data\\INI\\Default\\GameData.ini", "Data\\INI\\GameData.ini" });
	CHECK(g->initFileDebug == "Data\\INI\\GameDataDebug.ini");
	CHECK(g->loader == 0);
	const SubsystemLegendEntry *t = sl.findEntry("TheThingFactory");
	REQUIRE(t);
	CHECK(t->initPath == Strs{ "Data\\INI\\Object" });
	CHECK(t->excludePath == Strs{ "Data\\INI\\Object\\Cinematic\\CinematicWeapon.ini", "Data\\INI\\Object\\Cinematic\\CinematicLocomotor.ini" });
	CHECK(t->includePathCinematics == Strs{ "Data\\INI\\Object\\Cinematic\\" });
	CHECK(t->extension == Strs{ "ini" });
	CHECK(sl.findEntry("thethingfactory") == nullptr); // AsciiString compare: case sensitive

	// an unknown loader name is an error; an unknown field is an error
	Fixture fx2;
	SubsystemLegend sl2;
	sl2.registerBlock(fx2.env.blocks);
	CHECK(loadError(fx2.env, "l.ini", "LoadSubsystem X\n Loader = STR\nEnd\n").find("Token 'STR' is not a valid member of the lookup list") == 0);
	CHECK(loadError(fx2.env, "l.ini", "LoadSubsystem X\n Bogus = 1\nEnd\n").find("Unknown field 'Bogus' in block 'LoadSubsystem'.") == 0);
}

TEST_CASE("loadIniFilesFromLegend: every InitFile in order, then every InitPath; true iff the legend named anything (B1 SubsystemInterface.cpp:40-74)")
{
	FileList files = tree();
	files.push_back({ "Data\\INI\\Default\\First.ini", "Armor F\nEnd\n" });
	files.push_back({ "Data\\INI\\Second.ini", "Armor S\nEnd\n" });
	LegendFixture fx(files);
	INIBlockRecorder rec;
	SubsystemLegend sl;
	RegisterRecordingBlockStubs(fx.env.blocks, rec, { "LoadSubsystem" });
	sl.registerBlock(fx.env.blocks);
	const std::string legend =
		"LoadSubsystem TheThing\n"
		"  InitFile = Data\\INI\\Default\\First.ini\n"
		"  InitFile = Data\\INI\\Second.ini\n"
		"  InitPath = Data\\INI\\Sub\n"
		"  ExcludePath = Data\\INI\\Sub\\Deep\\c.ini\n"
		"End\n"
		"LoadSubsystem TheEmpty\n"
		"  Loader = INI\n"
		"End\n";
	REQUIRE(loadError(fx.env, "legend.ini", legend).empty());

	INI ini(fx.env);
	SubsystemLoadOptions options;
	CHECK(sl.loadIniFilesFromLegend("TheThing", ini, options, nullptr));
	CHECK(ini.loadedFiles() == Strs{ "Data\\INI\\Default\\First.ini", "Data\\INI\\Second.ini", "Data\\INI\\Sub\\b.ini" });
	CHECK_FALSE(sl.loadIniFilesFromLegend("TheEmpty", ini, options, nullptr)); // entry without files: hard-coded paths are used instead
	CHECK_FALSE(sl.loadIniFilesFromLegend("TheMissing", ini, options, nullptr));
}

TEST_CASE("IncludePathCinematics items are skipped unless -cinematics is set (legend header comment; UNVERIFIED against RW code)")
{
	FileList files = { { "Data\\INI\\Weapon.ini", "Armor W\nEnd\n" }, { "Data\\INI\\Object\\Cinematic\\CinematicWeapon.ini", "Armor C\nEnd\n" },
		{ "Data\\INI\\Object\\Cinematic\\Film.ini", "Armor D\nEnd\n" }, { "Data\\INI\\Object\\Units.ini", "Armor U\nEnd\n" } };
	const std::string legend =
		"LoadSubsystem TheWeaponStore\n"
		"  InitFile = Data\\INI\\Weapon.ini\n"
		"  InitFile = Data\\INI\\Object\\Cinematic\\CinematicWeapon.ini\n"
		"  IncludePathCinematics = Data\\INI\\Object\\Cinematic\\\n"
		"End\n"
		"LoadSubsystem TheThingFactory\n"
		"  InitPath = Data\\INI\\Object\n"
		"  ExcludePath = Data\\INI\\Object\\Cinematic\\CinematicWeapon.ini\n"
		"  IncludePathCinematics = Data\\INI\\Object\\Cinematic\\\n"
		"End\n";
	for (bool cinematics : { false, true })
	{
		LegendFixture fx(files);
		INIBlockRecorder rec;
		SubsystemLegend sl;
		RegisterRecordingBlockStubs(fx.env.blocks, rec, { "LoadSubsystem" });
		sl.registerBlock(fx.env.blocks);
		REQUIRE(loadError(fx.env, "legend.ini", legend).empty());
		INI ini(fx.env);
		SubsystemLoadOptions options;
		options.cinematics = cinematics;
		SubsystemLoadReport report;
		sl.loadIniFilesFromLegend("TheWeaponStore", ini, options, &report);
		sl.loadIniFilesFromLegend("TheThingFactory", ini, options, &report);
		if (cinematics)
		{
			CHECK(lowerSet(ini.loadedFiles()) == std::set<std::string>{ "data\\ini\\weapon.ini", "data\\ini\\object\\cinematic\\cinematicweapon.ini",
				"data\\ini\\object\\units.ini", "data\\ini\\object\\cinematic\\film.ini" });
			CHECK(report.skippedCinematics.empty());
		}
		else
		{
			CHECK(lowerSet(ini.loadedFiles()) == std::set<std::string>{ "data\\ini\\weapon.ini", "data\\ini\\object\\units.ini" });
			CHECK(report.skippedCinematics.size() == 2);
		}
	}
}

namespace
{
// Every hard-coded file the init order loads (the Water/Fire/Environment, audio and Eva loads that are not in the legend).
FileList hardcodedFiles(const std::string &body)
{
	FileList out;
	for (const SubsystemInitStep &s : RotwkSubsystemInitOrder())
	{
		for (const char *f : s.extraFiles)
		{
			out.push_back({ f, body });
		}
	}
	return out;
}

Strs extrasOf(const char *subsystem)
{
	for (const SubsystemInitStep &s : RotwkSubsystemInitOrder())
	{
		if (std::string(s.name) == subsystem)
		{
			return Strs(s.extraFiles.begin(), s.extraFiles.end());
		}
	}
	return Strs();
}
} // namespace

TEST_CASE("RunSubsystemIniLoad: legend first, then the RotWK init order, then the unrecovered-order entries; never-loaded entries are reported (spec 3.4, 3.5)")
{
	const std::string body = "Armor X\nEnd\n";
	FileList files = hardcodedFiles(body);
	files.push_back({ "Data\\INI\\Default\\SubsystemLegendExpansion1.ini",
		"LoadSubsystem TheWritableGlobalData\n InitFile = Data\\INI\\Default\\GameData.ini\n InitFile = Data\\INI\\GameData.ini\nEnd\n"
		"LoadSubsystem TheScienceStore\n InitFile = Data\\INI\\Science.ini\nEnd\n"
		"LoadSubsystem TheBannerUI\n InitFile = Data\\INI\\BannerUI.ini\nEnd\n"
		"LoadSubsystem Credits\n InitFile = Data\\INI\\Credits.ini\nEnd\n"
		"LoadSubsystem TheParticleSystemManager\n InitFile = Data\\INI\\ParticleSystem.ini\nEnd\n" });
	files.push_back({ "Data\\INI\\Default\\GameData.ini", "#define FIRST 1\n" + body });
	files.push_back({ "Data\\INI\\GameData.ini", body });
	files.push_back({ "Data\\INI\\Science.ini", body });
	files.push_back({ "Data\\INI\\BannerUI.ini", body });
	files.push_back({ "Data\\INI\\Credits.ini", "Credits X\nEnd\n" }); // would throw 'Unknown block' if it were loaded
	files.push_back({ "Data\\INI\\ParticleSystem.ini", "ParticleSystem X\nEnd\n" });
	LegendFixture fx(files);
	INIBlockRecorder rec;
	SubsystemLegend sl;
	RegisterRecordingBlockStubs(fx.env.blocks, rec, { "LoadSubsystem" });
	sl.registerBlock(fx.env.blocks);
	INI ini(fx.env);
	SubsystemLoadOptions options;
	options.collectErrors = true;
	SubsystemLoadReport report;
	RunSubsystemIniLoad(sl, ini, options, report);

	CHECK(report.errors.empty());
	// the legend; GameData + Water/Fire/Environment; TheAudio's 11 files; TheEva's 2; Science; then BannerUI last
	Strs expected = { "Data\\INI\\Default\\SubsystemLegendExpansion1.ini", "Data\\INI\\Default\\GameData.ini", "Data\\INI\\GameData.ini" };
	for (const std::string &f : extrasOf("TheWritableGlobalData"))
	{
		expected.push_back(f);
	}
	for (const std::string &f : extrasOf("TheAudio"))
	{
		expected.push_back(f);
	}
	for (const std::string &f : extrasOf("TheEva"))
	{
		expected.push_back(f);
	}
	expected.push_back("Data\\INI\\Science.ini");
	expected.push_back("Data\\INI\\BannerUI.ini");
	CHECK(ini.loadedFiles() == expected);
	CHECK(report.unverifiedOrder == Strs{ "TheBannerUI" });
	CHECK(report.neverLoaded == Strs{ "Credits", "TheParticleSystemManager" });
	CHECK(std::find(report.noLegendEntry.begin(), report.noLegendEntry.end(), "TheAudio") != report.noLegendEntry.end());
	CHECK(std::find(report.noLegendEntry.begin(), report.noLegendEntry.end(), "TheWritableGlobalData") == report.noLegendEntry.end());
	CHECK(fx.env.macros.findByName("FIRST") != nullptr);
}

// RW 0x4538DA-0x453A34 (audio) and 0x5DE837-0x5DE868 (Eva), disassembled; Sol review P1
TEST_CASE("hard-coded startup loads: TheAudio's eleven files and TheEva's two, in retail order")
{
	CHECK(extrasOf("TheAudio") == Strs{ "Data\\INI\\AudioSettings.ini", "Data\\INI\\Default\\Music.ini", "Data\\INI\\Default\\Speech.ini",
		"Data\\INI\\Default\\SoundEffects.ini", "Data\\INI\\Default\\AmbientStream.ini", "Data\\INI\\Music.ini", "Data\\INI\\SoundEffects.ini",
		"Data\\INI\\Speech.ini", "Data\\INI\\Voice.ini", "Data\\INI\\AmbientStream.ini", "Data\\INI\\MiscAudio.ini" });
	CHECK(extrasOf("TheEva") == Strs{ "Data\\INI\\Default\\Eva.ini", "Data\\INI\\Eva.ini" });
	CHECK(extrasOf("TheWritableGlobalData") == Strs{ "Data\\INI\\Default\\Water.ini", "Data\\INI\\Water.ini", "Data\\INI\\Default\\Fire.ini", "Data\\INI\\Fire.ini",
		"Data\\INI\\Default\\Environment.ini", "Data\\INI\\Environment.ini" });
}

// RW 0x63BF51 (TheAttributeModifierStore) before 0x63BFDC (TheScriptEngine); Sol review P2
TEST_CASE("RunSubsystemIniLoad uses retail's call order, not legend order: a macro from the AttributeModifier file is visible to the ScriptEngine file")
{
	const std::string body = "Armor X\nEnd\n";
	FileList files = hardcodedFiles(body);
	// legend order lists TheScriptEngine FIRST; the retail init calls TheAttributeModifierStore first
	files.push_back({ "Data\\INI\\Default\\SubsystemLegendExpansion1.ini",
		"LoadSubsystem TheScriptEngine\n InitFile = Data\\Scripts\\Scripts.ini\nEnd\n"
		"LoadSubsystem TheAttributeModifierStore\n InitFile = Data\\INI\\AttributeModifier.ini\nEnd\n" });
	files.push_back({ "Data\\INI\\AttributeModifier.ini", "#define ATTR_BONUS 42\n" + body });
	files.push_back({ "Data\\Scripts\\Scripts.ini", "Weapon W\n Key ATTR_BONUS\nEnd\n" });
	LegendFixture fx(files);
	INIBlockRecorder rec;
	SubsystemLegend sl;
	RegisterRecordingBlockStubs(fx.env.blocks, rec, { "LoadSubsystem", "Weapon" });
	sl.registerBlock(fx.env.blocks);
	int seen = -1;
	fx.env.blocks.registerBlock("Weapon", [&seen](INI *i) {
		i->readLine();
		i->firstToken();
		seen = i->scanInt(i->getNextToken()); // throws 'Expected signed integer' if the macro is not defined yet
		i->readLine();
	});
	INI ini(fx.env);
	SubsystemLoadOptions options;
	options.collectErrors = true;
	SubsystemLoadReport report;
	RunSubsystemIniLoad(sl, ini, options, report);
	CHECK(report.errors.empty());
	CHECK(seen == 42);
	CHECK(report.unverifiedOrder.empty());
}

TEST_CASE("RunSubsystemIniLoad: without collectErrors an INI error propagates (retail does not catch, spec 1.9)")
{
	const std::string body = "Armor X\nEnd\n";
	FileList files = hardcodedFiles(body);
	files.push_back({ "Data\\INI\\Default\\SubsystemLegendExpansion1.ini", "LoadSubsystem TheScienceStore\n InitFile = Data\\INI\\Science.ini\nEnd\n" });
	files.push_back({ "Data\\INI\\Science.ini", "Nonsense\n" });
	LegendFixture fx(files);
	INIBlockRecorder rec;
	SubsystemLegend sl;
	RegisterRecordingBlockStubs(fx.env.blocks, rec, { "LoadSubsystem" });
	sl.registerBlock(fx.env.blocks);
	INI ini(fx.env);
	SubsystemLoadReport report;
	try
	{
		RunSubsystemIniLoad(sl, ini, SubsystemLoadOptions(), report);
		FAIL("expected an INIException");
	}
	catch (const INIException &e)
	{
		CHECK(e.code() == 5);
		CHECK(e.message().find("Unknown block 'Nonsense'.") == 0);
		CHECK(e.message().find("Data\\INI\\Science.ini") != std::string::npos);
	}
}

TEST_CASE("RotWK subsystem init order: recovered from GameEngine::init (RW 0x63AF2D-0x63C7D5)")
{
	std::vector<std::string> names;
	for (const SubsystemInitStep &s : RotwkSubsystemInitOrder())
	{
		names.push_back(s.name);
	}
	auto at = [&](const char *n) { return std::find(names.begin(), names.end(), n) - names.begin(); };
	REQUIRE(at("TheWritableGlobalData") == 0);
	for (const char *before : { "TheWeaponStore", "TheLocomotorStore", "TheObjectCreationListStore", "TheSpecialPowerStore", "TheArmorStore", "TheUpgradeCenter" })
	{
		CHECK_MESSAGE(at(before) < at("TheThingFactory"), before);
	}
	// the two orderings the Sol review named, with their call-site addresses
	CHECK(at("TheLinearCampaignManager") < at("TheAI")); // 0x63BE2D < 0x63BE74
	CHECK(at("TheAttributeModifierStore") < at("TheScriptEngine")); // 0x63BF51 < 0x63BFDC
	CHECK(at("TheThingFactory") < at("TheAI"));
	CHECK(at("TheScriptEngine") < at("TheMetaMap"));
	CHECK(at("TheMetaMap") < at("TheHouseColorSystem"));
	CHECK(at("TheMeshInstancingManager") < at("TheVictorySystem"));
	CHECK(at("TheAudio") < at("TheEva"));
	CHECK(RotwkNeverLoadedSubsystems() == Strs{ "Credits", "InGameUI", "Animation2D", "TheParticleSystemManager" });
	CHECK(std::string(kSubsystemLegendFile) == "Data\\INI\\Default\\SubsystemLegendExpansion1.ini");
	// no duplicates
	CHECK(std::set<std::string>(names.begin(), names.end()).size() == names.size());
}
