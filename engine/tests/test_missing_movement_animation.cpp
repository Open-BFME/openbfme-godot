// OpenBFME. GPL-3.0. Owner-requested presentation repair, S-5920.
// Both BFME2 / RotWK, plus BFME1 data mode. No simulation or retail data changes.
// The RotWK INIs reference absent GUCavalry_ATRA and mounted BAKA clips. This sweep exercises
// every selectable, wholly missing movement state on cavalry / mounted-state templates through
// the real draw runtime. A repair must retain the state/error and advance a compatible cycle.

#include "doctest.h"
#include "RetailTestMount.h"
#include "W3DDrawTestUtil.h"
#include "Common/AsciiString.h"
#include "Common/ArchiveFile.h"
#include "Common/BfmeGame.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLuaDrawScriptHost.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

namespace
{
class FirstRandom : public W3DDrawRandom
{
public:
	int value(int lo, int) override { return lo; }
	float real(float lo, float) override { return lo; }
};

bool repaired(const W3DScriptedModelDraw &draw)
{
	for (const auto &stop : draw.stops())
	{
		if (stop.Id == "S-5920") return true;
	}
	return false;
}

std::map<std::string, std::set<std::string>> sweep(Win32BIGFileSystem &fs, const char *edition,
	const std::vector<std::string> &expectedLoadErrors = {})
{
	RetailObjectWorld world(fs);
	std::string error;
	REQUIRE_MESSAGE(world.load(&error), error);
	std::vector<std::string> loadErrors;
	for (const auto &failure : world.report().errors)
	{
		// The first line names the registered stop; the remaining lines are INI include / file context.
		loadErrors.push_back(failure.message.substr(0, failure.message.find('\n')));
		std::printf("movement load %s: %s: %s\n", edition, failure.file.c_str(), failure.message.c_str());
	}
	CHECK(loadErrors == expectedLoadErrors);
	const auto context = world.enterContext();
	ArchiveW3DFileSource source(fs);
	WW3DAssetManager assets(source);
	WW3DDrawAssets drawAssets(assets);
	FirstRandom random;
	const int moving = ModelCondition::indexOf("MOVING");
	const int mounted = ModelCondition::indexOf("MOUNTED");
	std::map<std::string, std::set<std::string>> cycles;
	int missing = 0, repairedCount = 0;
	for (const ThingTemplate *tt : world.things().templates())
	{
		bool cavalry = MaskTest(ObjectTemplateInfoBuilder::build(*tt).kindOf, ObjectTemplateInfoBuilder::kindOfIndex("CAVALRY"));
		for (const auto &module : tt->drawModules().nuggets())
		{
			if (const auto *data = dynamic_cast<const W3DModelDrawModuleData *>(module.data.get()))
				for (const auto &state : data->m_animationStates) cavalry = cavalry || state.conditions.test(mounted);
		}
		if (!cavalry) continue;
		for (const auto &module : tt->drawModules().nuggets())
		{
			const auto *data = dynamic_cast<const W3DModelDrawModuleData *>(module.data.get());
			if (!data) continue;
			for (const auto &state : data->m_animationStates)
			{
				if (state.kind != AnimationStateInfo::KIND_NORMAL || !state.conditions.test(moving) || state.animations.empty() ||
					data->findBestAnimationState(state.conditions) != &state) continue;
				const auto *binding = data->findBestInfo(state.conditions);
				if (!binding) continue;
				bool available = false;
				for (const auto &animation : state.animations)
				{
					W3DAnimationLookup lookup;
					lookup.skeleton = binding->skeleton;
					lookup.modelAnimationPrefix = binding->modelAnimationPrefix;
					lookup.names = animation.animationNames;
					lookup.numbered = data->m_staticModelLODMode || data->m_switchModelLODMode;
					available = available || drawAssets.animation(lookup, nullptr, &error) != nullptr;
				}
				if (available) continue;
				++missing;
				INFO(edition << " " << tt->getName() << " " << ModelCondition::describe(state.conditions));
				W3DLuaDrawScriptHost host;
				W3DScriptedModelDraw::Options options;
				options.buildBones = false;
				options.objectSpeed = [] { return 2.0f; };
				W3DScriptedModelDraw draw(*data, drawAssets, random, &host, options);
				draw.setModelConditionFlags(state.conditions);
				CHECK_FALSE(draw.errors().empty()); // Missing assets remain visible even when playback can be repaired.
				if (!repaired(draw))
				{
					std::printf("movement unrepaired %s: %s [%s]: %s\n", edition, tt->getName().c_str(),
						ModelCondition::describe(state.conditions).c_str(), draw.errors().front().c_str());
					continue; // No compatible cycle: retain the error, without borrowing another model's rules.
				}
				++repairedCount;
				CHECK(draw.currentAnimationState() == &state);
				draw.advance(1000.0); // complete any outgoing idle blend
				const auto first = draw.frame();
				REQUIRE(first.trackCount == 1);
				REQUIRE(first.tracks[0].anim);
				CHECK(AsciiStringUtil::compareNoCase(first.tracks[0].anim->Get_HName(), first.model->HierarchyName) == 0);
				draw.advance(100.0);
				const bool advanced = draw.track(0).frame != first.tracks[0].frame;
				CHECK(advanced);
				cycles[tt->getName()].insert(AsciiStringUtil::lowered(first.tracks[0].clipName));
				std::printf("movement repair %s: %s [%s] -> %s\n", edition, tt->getName().c_str(),
					ModelCondition::describe(state.conditions).c_str(), first.tracks[0].clipName.c_str());
			}
		}
	}
	std::printf("movement repair %s: %d wholly missing selectable states, %d repaired\n", edition, missing, repairedCount);
	CHECK(repairedCount == missing);
	return cycles;
}
} // namespace

TEST_CASE("movement animation retail: RotWK missing movement clips no longer hold the previous pose")
{
	auto *mount = retailtest::pureMount();
	if (!mount) { retailtest::printSkip("movement animation RotWK"); return; }
	REQUIRE_MESSAGE(mount->fs, mount->error);
	const auto cycles = sweep(*mount->fs, "RotWK");
	// Independent expectations: the ordinary MOVING loops named in the mounted retail INIs.
	for (const auto &expected : std::map<std::string, std::string>{
		{ "GondorCavalry", "gucavalry_skl.gucavalry_runa" },
		{ "ArnorCavalry", "gucavalry_skl.gucavalry_runa" },
		{ "GondorFaramir", "ruhhs_theo_skl.ruhhs_theo_runa" },
		{ "GondorGandalf", "gugdfhrs_skl.gugdfhrs_runa" },
		{ "ElvenArwen", "ruewnhrhrs_skl.ruewnhrhrs_runa" },
		{ "ElvenGlorfindel", "ruhhs_theo_skl.ruhhs_theo_runa" },
		{ "RohanEomer", "ruhhs_theo_skl.ruhhs_theo_runa" },
		{ "RohanEowyn", "ruewnhrhrs_skl.ruewnhrhrs_runa" },
		{ "RohanTheoden", "ruhhs_theo_skl.ruhhs_theo_runa" },
		{ "RohanEowynDisguised", "rurohrm_skl.rurohrm_runa" },
		{ "IsengardBannerOrc", "iubanner_skl.iubanner_runa" },
		{ "GondorCavalryBanner", "rurrmbnr_skl.rurrmbnr_runa" } })
	{
		INFO(expected.first);
		const auto found = cycles.find(expected.first);
		REQUIRE(found != cycles.end());
		CHECK(found->second.count(expected.second) == 1);
	}
	// RUNB, BAKA and RUNA exist: qualify those exact declarations with IUBANNER_SKL. IWKA is absent,
	// so the injured state uses the declared RUNA loop. All five states must advance (checked in sweep).
	const std::set<std::string> bannerCycles = {
		"iubanner_skl.iubanner_runb", "iubanner_skl.iubanner_baka", "iubanner_skl.iubanner_runa" };
	CHECK(cycles.at("IsengardBannerOrc") == bannerCycles);
}

TEST_CASE("movement animation retail: BFME2 movement repairs use BFME2 data")
{
	auto *mount = retailtest::bfme2Mount();
	if (!mount) { retailtest::printSkip("movement animation BFME2"); return; }
	REQUIRE_MESSAGE(mount->fs, mount->error);
	// Existing registered stop in BFME2's strategic campaign parser; unit / draw data still loads.
	// Pin it explicitly so this renderer test cannot mask any additional load failure.
	const auto cycles = sweep(*mount->fs, "BFME2", {
		"[S-1321] Living World field 'AddPlayer' is not ported (its retail parser was not read)" });
	CHECK(cycles.count("GondorCavalry") == 1);
	CHECK(cycles.count("GondorFaramir") == 1);
}

TEST_CASE("movement animation retail: BFME1 movement repairs use the flat BFME1 assets")
{
	const char *install = std::getenv("BFME1_INSTALL");
	if (!install || !*install) { retailtest::printSkip("movement animation BFME1"); return; }
	const auto root = std::filesystem::u8path(install);
	std::vector<std::string> names;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
	{
		if (!entry.is_regular_file() || entry.path().filename().u8string().rfind("!!!", 0) == 0) continue;
		std::string extension = entry.path().extension().u8string();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });
		if (extension == ".big") names.push_back(entry.path().lexically_relative(root).u8string());
	}
	Win32BIGFileSystem fs;
	std::string error;
	for (const auto &name : BFMEArchiveLoadOrder(names))
	{
		auto archive = fs.openArchiveFile((root / std::filesystem::u8path(name)).u8string(), &error);
		REQUIRE_MESSAGE(archive, error);
		fs.mountArchive(std::move(archive), name, false);
	}
	REQUIRE(isBfme1Data(fs));
	const auto cycles = sweep(fs, "BFME1");
	CHECK(cycles.count("GondorCavalry") == 1);
}
