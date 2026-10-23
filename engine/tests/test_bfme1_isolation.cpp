// OpenBFME unit tests. GPL-3.0.
// community PR #36 review (docs/BFME1.md, Common/BfmeGame.h): BFME1 support is isolated. BFME1's extra INI rows and names are read only from BFME1
// data (its SubsystemLegend.ini without BFME2's SubsystemLegendExpansion1.ini); RotWK / BFME2 data still fail on them exactly as the binary does.

#include "doctest.h"

#include "IniTestUtil.h"
#include "ObjectTestUtil.h"
#include "RetailTestMount.h"

#include "Common/Audio/AudioIni.h"
#include "Common/BfmeGame.h"
#include "Common/INI.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/BloodthirstyUpdate.h"
#include "GameLogic/Module/BloodthirstyUpdate.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/textureloader.h"

#include <fstream>
#include <iterator>
#include <string>

namespace
{
const initest::FileList kBfme1Legend = { { "Data\\INI\\Default\\SubsystemLegend.ini", "; BFME1's legend\r\n" } };
const initest::FileList kRotwkLegends = { { "Data\\INI\\Default\\SubsystemLegend.ini", "; legend\r\n" },
	{ "Data\\INI\\Default\\SubsystemLegendExpansion1.ini", "; the expansion's legend\r\n" } };

struct Probe
{
	int a = 0;
	int b = 0;
};
const FieldParse kProbeTable[] = {
	{ "A", INI::parseInt, nullptr, (int)offsetof(Probe, a) },
	{ nullptr, nullptr, nullptr, 0 },
};
const FieldParse kProbeBfme1Rows[] = {
	{ "B", INI::parseInt, nullptr, (int)offsetof(Probe, b) },
	{ nullptr, nullptr, nullptr, 0 },
};
const Bfme1FieldRowsRegistration kProbeRegistration(kProbeTable, kProbeBfme1Rows);

std::string loadProbe(initest::Fixture &fx, Probe &probe, const std::string &text)
{
	fx.env.blocks.registerBlock("Probe", [&probe](INI *ini) { ini->initFromINI(&probe, kProbeTable); });
	return initest::loadError(fx.env, "probe.ini", text);
}
} // namespace

TEST_CASE("bfme1 isolation: the data decides the game (SubsystemLegend.ini without SubsystemLegendExpansion1.ini), cached per environment")
{
	initest::Fixture none;
	CHECK_FALSE(none.env.bfme1Data());
	initest::Fixture bfme1(kBfme1Legend);
	CHECK(isBfme1Data(bfme1.fsys));
	CHECK(bfme1.env.bfme1Data());
	initest::Fixture rotwk(kRotwkLegends);
	CHECK_FALSE(isBfme1Data(rotwk.fsys));
	CHECK_FALSE(rotwk.env.bfme1Data());
	// lane SHELLFIX-1: BFME2 1.06 on its own (the BFME2 menu style's shell source): the legend alone, plus BFME2's AwardSystem.ini
	initest::Fixture bfme2({ { "Data\\INI\\Default\\SubsystemLegend.ini", "; legend\r\n" }, { "Data\\INI\\AwardSystem.ini", "; awards\r\n" } });
	CHECK_FALSE(isBfme1Data(bfme2.fsys));
	CHECK_FALSE(bfme2.env.bfme1Data());
	INIEnvironment noFs;
	CHECK_FALSE(noFs.bfme1Data());
}

TEST_CASE("bfme1 isolation: a table's BFME1 rows are found in BFME1 data only; RotWK data fails on them as retail does")
{
	const std::string text = "Probe\r\n  A = 1\r\n  B = 2\r\nEnd\r\n";
	{
		initest::Fixture rotwk(kRotwkLegends);
		Probe p;
		const std::string error = loadProbe(rotwk, p, text);
		CHECK(error.find("Unknown field 'B' in block 'Probe'") != std::string::npos);
	}
	{
		initest::Fixture bfme1(kBfme1Legend);
		Probe p;
		REQUIRE(loadProbe(bfme1, p, text).empty());
		CHECK(p.a == 1);
		CHECK(p.b == 2);
	}
	CHECK(Bfme1FieldRows(kProbeTable) == kProbeBfme1Rows);
	CHECK(Bfme1FieldRows(kProbeBfme1Rows) == nullptr);
}

TEST_CASE("bfme1 isolation: AudioEvent Control = RANDOM and the AudioSettings 3D buffer rows are BFME1's only")
{
	const std::string event = "AudioEvent DefaultSoundEffect\r\n  Volume = 100\r\nEnd\r\nAudioEvent Shout\r\n  Sounds = shout\r\n  Control = loop random\r\nEnd\r\n";
	{
		initest::Fixture rotwk(kRotwkLegends);
		AudioIniState state;
		state.registerBlocks(rotwk.env.blocks);
		// RotWK's names have no RANDOM (RW 0xD9DB70)
		CHECK(initest::loadError(rotwk.env, "events.ini", event).find("Token 'random' is not a valid member of the index list") != std::string::npos);
	}
	{
		initest::Fixture bfme1(kBfme1Legend);
		AudioIniState state;
		state.registerBlocks(bfme1.env.blocks);
		CHECK(initest::loadError(bfme1.env, "events.ini", event).empty());
	}
	const std::string settings = "AudioSettings\r\n  3DBufferLengthMS = 80\r\nEnd\r\n";
	{
		initest::Fixture rotwk(kRotwkLegends);
		AudioIniState state;
		state.registerBlocks(rotwk.env.blocks);
		CHECK(initest::loadError(rotwk.env, "settings.ini", settings).find("Unknown field '3DBufferLengthMS'") != std::string::npos);
	}
	{
		initest::Fixture bfme1(kBfme1Legend);
		AudioIniState state;
		state.registerBlocks(bfme1.env.blocks);
		CHECK(initest::loadError(bfme1.env, "settings.ini", settings).empty());
	}
}

TEST_CASE("bfme1 isolation: BFME1's castle flag row is refused in RotWK data, and templates carry the factory's game")
{
	const std::string castle = "Object Flag\n  KindOf = STRUCTURE\n  Behavior = CastleBehavior ModuleTag_Castle\n"
							   "    UseTheNewCastleSystemInsteadOfTheClunkyBuildList = Yes\n  End\nEnd\n";
	objtest::World rotwk;
	LogicModules::registerAll(rotwk.modules);
	rotwk.fx.mount(kRotwkLegends);
	CHECK(rotwk.load(castle).find("UseTheNewCastleSystemInsteadOfTheClunkyBuildList") != std::string::npos);
	objtest::World bfme1(true);
	LogicModules::registerAll(bfme1.modules);
	bfme1.fx.mount(kBfme1Legend);
	const std::string bfme1Error = bfme1.load(castle);
	REQUIRE_MESSAGE(bfme1Error.empty(), bfme1Error);
	REQUIRE(bfme1.get("Flag") != nullptr);
	CHECK(bfme1.get("Flag")->bfme1Data());
	REQUIRE(rotwk.load("Object Plain\n  KindOf = STRUCTURE\nEnd\n").empty());
	CHECK_FALSE(rotwk.get("Plain")->bfme1Data());
}

TEST_CASE("bfme1 isolation: S-5017 names BFME1 mode's inferences and is registered")
{
	const std::vector<std::string> lines = bfme1StopLines();
	REQUIRE(lines.size() == 1);
	CHECK(lines[0].rfind("[S-5017] ", 0) == 0);
	CHECK(lines[0].find("BuildingBehavior windows follow mirrored NIGHT/AFLAME model flags") != std::string::npos);
	CHECK(lines[0].find("external expansion threat and army helpers remain unported") != std::string::npos);
	CHECK(lines[0].find("victory rule") != std::string::npos);
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md", std::ios::binary);
	REQUIRE(in);
	const std::string doc((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	CHECK(doc.find("| S-5017 | code |") != std::string::npos);
}

TEST_CASE("bfme1 isolation: the W3D model layout is the file system's, whatever the process flag says (Sol review r2)")
{
	// the simulation's asset manager (launch bones) reads through an ArchiveW3DFileSource: its layout must follow its own files
	struct Flag
	{
		bool was = bfme1Mounted();
		~Flag() { setBfme1Mounted(was); }
	} restore;
	CHECK(W3D_Asset_Path("FooBar", false) == "art\\w3d\\fo\\foobar.w3d");
	CHECK(W3D_Asset_Path("FooBar", true) == "art\\w3d\\foobar.w3d");
	{
		setBfme1Mounted(true);
		initest::Fixture rotwk(kRotwkLegends);
		ArchiveW3DFileSource source(rotwk.fsys);
		CHECK_FALSE(source.FlatAssetLayout());
		WW3DAssetManager assets(source);
		std::string error;
		CHECK(assets.Get_HTree("FooBar", &error) == nullptr);
		CHECK(error.find("art\\w3d\\fo\\foobar.w3d") != std::string::npos);
	}
	{
		setBfme1Mounted(false);
		initest::Fixture bfme1(kBfme1Legend);
		ArchiveW3DFileSource source(bfme1.fsys);
		CHECK(source.FlatAssetLayout());
		WW3DAssetManager assets(source);
		std::string error;
		CHECK(assets.Get_HTree("FooBar", &error) == nullptr);
		CHECK(error.find("art\\w3d\\foobar.w3d") != std::string::npos);
	}
	CHECK(Resolve_W3D_Texture("Grass.tga", [](const std::string &p) { return p == "art\\textures\\grass.dds"; }, true).Found);
	CHECK_FALSE(Resolve_W3D_Texture("Grass.tga", [](const std::string &p) { return p == "art\\textures\\grass.jpg"; }, false).Found);
}

// lane SHELLFIX-1: the BFME2 main menu style's shell source is BFME2 1.06's archives alone. Read as BFME1 data, its menu models (Options.apt's and the
// main menu's View3D SFE_MenuFrame ...) were looked for in BFME1's flat art\w3d\ and never drew ("render object SFE_MenuFrame is not registered")
TEST_CASE("bfme1 isolation retail: BFME2 1.06's archives alone are BFME2 data; their menu models are found in its two-letter folders")
{
	retailtest::Mount *bfme2 = retailtest::bfme2Mount();
	if (!bfme2)
	{
		retailtest::printSkip("bfme1 isolation retail (BFME2_INSTALL unset)");
		return;
	}
	REQUIRE_MESSAGE(bfme2->fs, bfme2->error);
	CHECK_FALSE(isBfme1Data(*bfme2->fs));
	ArchiveW3DFileSource source(*bfme2->fs);
	CHECK_FALSE(source.FlatAssetLayout());
	CHECK(W3D_Asset_Path("sfe_menuframe", source.FlatAssetLayout()) == "art\\w3d\\sf\\sfe_menuframe.w3d");
	CHECK(source.Exists(W3D_Asset_Path("sfe_menuframe", source.FlatAssetLayout())));
	retailtest::Mount *pure = retailtest::pureMount();
	REQUIRE(pure);
	REQUIRE_MESSAGE(pure->fs, pure->error);
	CHECK_FALSE(isBfme1Data(*pure->fs));
}

TEST_CASE("bfme1 isolation: typed BloodthirstyUpdate stores legacy rows only in BFME1 data")
{
	const std::string text = "Object Horde\n Behavior = BloodthirstyUpdate ModuleTag_Blood\n"
		" NumToSacrifice = 3\n InitiateVoice2 = SecondVoice\n End\nEnd\n";
	objtest::World rotwk;
	LogicModules::registerAll(rotwk.modules);
	rotwk.fx.mount(kRotwkLegends);
	CHECK(rotwk.load(text).find("Unknown field 'NumToSacrifice'") != std::string::npos);
	const std::string voiceOnly = "Object Horde\n Behavior = BloodthirstyUpdate ModuleTag_Blood\n InitiateVoice2 = SecondVoice\n End\nEnd\n";
	CHECK(rotwk.load(voiceOnly).find("Unknown field 'InitiateVoice2'") != std::string::npos);
	objtest::World bfme1(true);
	LogicModules::registerAll(bfme1.modules);
	bfme1.fx.mount(kBfme1Legend);
	const std::string error = bfme1.load(text);
	REQUIRE_MESSAGE(error.empty(), error);
	const ThingTemplate *tt = bfme1.get("Horde");
	REQUIRE(tt);
	const auto &nuggets = tt->behaviorModules().nuggets();
	REQUIRE(nuggets.size() == 1);
	const auto *data = dynamic_cast<const BloodthirstyUpdateModuleData *>(nuggets[0].data.get());
	REQUIRE(data);
	CHECK(data->m_bfme1NumToSacrifice == 3);
	CHECK(data->m_bfme1InitiateVoice2 == "SecondVoice");
	CHECK(BloodthirstyUpdateModuleData().m_bfme1NumToSacrifice == 1);
	CHECK(bfme1StopLines()[0].find("BloodthirstyUpdate NumToSacrifice and InitiateVoice2 are stored but their runtime semantics are unported") != std::string::npos);
}
