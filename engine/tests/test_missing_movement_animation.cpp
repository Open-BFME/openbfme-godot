// OpenBFME. GPL-3.0. Owner-requested presentation repair, S-5920.
// Both BFME2 / RotWK, plus BFME1 data mode. No simulation or retail data changes.
// The RotWK INIs reference absent GUCavalry_ATRA and mounted BAKA clips. This sweep exercises
// every selectable, wholly unresolved movement state on cavalry / mounted-state templates through
// the real draw runtime. A repair must retain the state/error and advance a compatible cycle.
// Standalone file presence is separate evidence from the mounted archive directories, not playback.

#include "doctest.h"
#include "RetailTestMount.h"
#include "W3DDrawTestUtil.h"
#include "IniTestUtil.h"
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
// List archive entries directly: no W3DFileSource, asset-path builder, animation resolver or decoder.
// Basenames cover BFME1's flat layout and BFME2 / RotWK's subfolders, even a misplaced file.
// A present standalone file need not contain the requested hierarchy/clip or be decodable. An absent
// standalone file does not prove the animation is absent from every other W3D container.
std::set<std::string> standaloneW3DFiles(const ArchiveFileSystem &fs)
{
	FilenameList paths;
	fs.getFileListInDirectory("", "art\\w3d", "*.w3d", paths, true);
	std::set<std::string> files;
	for (const auto &path : paths)
	{
		const auto slash = path.find_last_of("\\/");
		files.insert(AsciiStringUtil::lowered(path.substr(slash == std::string::npos ? 0 : slash + 1)));
	}
	return files;
}

struct FilePresence
{
	std::set<std::string> present, absent;
};

FilePresence declaredFilePresence(const std::set<std::string> &files, const AnimationStateInfo &state,
	const ModelConditionInfo &binding, bool numbered)
{
	FilePresence result;
	for (const auto &animation : state.animations)
		for (std::string name : animation.animationNames)
		{
			// Inspect the declarations, including a bare AnimationName that playback cannot qualify.
			// The sweep's LOD lookup uses number 0; inventory both that candidate and the unnumbered one.
			const auto macro = name.find("#(MODEL)");
			if (macro != std::string::npos) name.replace(macro, 8, binding.modelAnimationPrefix);
			const auto dot = name.find('.');
			if (dot != std::string::npos) name.erase(0, dot + 1);
			if (name.empty()) continue;
			name = AsciiStringUtil::lowered(name);
			std::vector<std::string> candidates = { name + ".w3d" };
			if (numbered && !binding.skeleton.empty()) candidates.push_back(name + "0.w3d");
			bool present = false;
			for (const auto &file : candidates)
				if (files.count(file) == 1)
				{
					result.present.insert(file);
					present = true;
				}
			// These are alternatives for one declaration. A missing optional numbered file must not
			// make a declaration with a present unnumbered file count as partly absent.
			if (!present) result.absent.insert(candidates.begin(), candidates.end());
		}
	return result;
}

struct SweepResult
{
	std::map<std::string, std::set<std::string>> cycles;
	std::map<std::string, std::map<std::string, FilePresence>> unresolved;
};

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

SweepResult sweep(Win32BIGFileSystem &fs, const char *edition,
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
	const auto files = standaloneW3DFiles(fs);
	SweepResult result;
	int unresolved = 0, repairedCount = 0;
	int absentOnly = 0, presentOnly = 0, mixed = 0, noFileNames = 0;
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
				++unresolved;
				INFO(edition << " " << tt->getName() << " " << ModelCondition::describe(state.conditions));
				const auto presence = declaredFilePresence(files, state, *binding, data->m_staticModelLODMode || data->m_switchModelLODMode);
				result.unresolved[tt->getName()][ModelCondition::describe(state.conditions)] = presence;
				if (presence.present.empty() && presence.absent.empty()) ++noFileNames;
				else if (presence.present.empty()) ++absentOnly;
				else if (presence.absent.empty()) ++presentOnly;
				else ++mixed;
				std::printf("movement file presence %s: %s [%s]:", edition, tt->getName().c_str(),
					ModelCondition::describe(state.conditions).c_str());
				for (const auto &file : presence.present) std::printf(" present=%s", file.c_str());
				for (const auto &file : presence.absent) std::printf(" absent=%s", file.c_str());
				std::printf("\n");
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
				result.cycles[tt->getName()].insert(AsciiStringUtil::lowered(first.tracks[0].clipName));
				std::printf("movement repair %s: %s [%s] -> %s\n", edition, tt->getName().c_str(),
					ModelCondition::describe(state.conditions).c_str(), first.tracks[0].clipName.c_str());
			}
		}
	}
	std::printf("movement repair %s: %d wholly unresolved selectable states, %d repaired\n", edition, unresolved, repairedCount);
	std::printf("movement file presence %s: unresolved states by standalone W3D candidates: %d absent-only, %d present-only "
		"(lookup/content failure), %d mixed, %d without file names\n", edition, absentOnly, presentOnly, mixed, noFileNames);
	CHECK(absentOnly + presentOnly + mixed + noFileNames == unresolved);
	CHECK(noFileNames == 0);
	CHECK(repairedCount == unresolved);
	return result;
}
} // namespace

TEST_CASE("movement animation file presence: archive inventory is independent of animation decoding")
{
	// Deliberately invalid W3D payloads: physical presence must not depend on successful decoding.
	initest::Fixture fixture({
		{ "ART/W3D/FLAT_RUN.W3D", "not an animation" },
		{ "art\\w3d\\nested\\unit_run.w3d", "not an animation" },
		{ "art\\w3d\\nested\\unit_run0.w3d", "not an animation" },
		{ "elsewhere\\absent.w3d", "not in the W3D directory" },
		{ "art\\w3d\\absent.w3d.txt", "not a W3D entry" } });
	const auto files = standaloneW3DFiles(fixture.fsys);
	CHECK(files == std::set<std::string>{ "flat_run.w3d", "unit_run.w3d", "unit_run0.w3d" });
	AnimationStateInfo state;
	state.animations.resize(1);
	state.animations[0].animationNames = { "SKL.FLAT_RUN", "#(MODEL)_RUN", "ABSENT" };
	ModelConditionInfo binding;
	const auto bare = declaredFilePresence(files, state, binding, false);
	CHECK(bare.present == std::set<std::string>{ "flat_run.w3d" });
	CHECK(bare.absent == std::set<std::string>{ "_run.w3d", "absent.w3d" });
	binding.skeleton = "skl";
	binding.modelAnimationPrefix = "UNIT";
	const auto numbered = declaredFilePresence(files, state, binding, true);
	CHECK(numbered.present == std::set<std::string>{ "flat_run.w3d", "unit_run.w3d", "unit_run0.w3d" });
	CHECK(numbered.absent == std::set<std::string>{ "absent.w3d", "absent0.w3d" });
}

TEST_CASE("movement animation retail: RotWK missing movement clips no longer hold the previous pose")
{
	auto *mount = retailtest::pureMount();
	if (!mount) { retailtest::printSkip("movement animation RotWK"); return; }
	REQUIRE_MESSAGE(mount->fs, mount->error);
	const auto result = sweep(*mount->fs, "RotWK");
	const auto &cycles = result.cycles;
	// Independent manual BIG-directory inventory, reported on #315 (issuecomment-6862570503).
	const auto files = standaloneW3DFiles(*mount->fs);
	for (const auto *name : { "gucavalry_atra.w3d", "ruhhs_theo_baka.w3d", "gugdfhrs_baka.w3d",
		"rurrmbnr_wlka.w3d", "iubanner_iwka.w3d", "rupsnt_4_mfda.w3d" })
	{
		INFO(name);
		CHECK(files.count(name) == 0);
	}
	for (const auto *name : { "iubanner_runb.w3d", "iubanner_baka.w3d", "iubanner_runa.w3d", "rurohrm_runa.w3d" })
	{
		INFO(name);
		CHECK(files.count(name) == 1);
	}
	int bannerPresent = 0, bannerAbsent = 0;
	for (const auto &entry : result.unresolved.at("IsengardBannerOrc"))
	{
		INFO(entry.first);
		const auto &presence = entry.second;
		if (presence.present.empty())
		{
			++bannerAbsent;
			CHECK(presence.absent == std::set<std::string>{ "iubanner_iwka.w3d" });
		}
		else
		{
			++bannerPresent;
			CHECK(presence.absent.empty());
		}
	}
	CHECK(bannerPresent == 4);
	CHECK(bannerAbsent == 1);
	const auto panicking = ModelCondition::describe(drawtest::flagsOf({ "MOVING", "PANICKING" }));
	const auto &eowyn = result.unresolved.at("RohanEowynDisguised").at(panicking);
	CHECK(eowyn.present.empty());
	CHECK(eowyn.absent == std::set<std::string>{ "rupsnt_4_mfda.w3d" });
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
	const auto result = sweep(*mount->fs, "BFME2", {
		"[S-1321] Living World field 'AddPlayer' is not ported (its retail parser was not read)" });
	const auto &cycles = result.cycles;
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
	const auto result = sweep(fs, "BFME1");
	const auto &cycles = result.cycles;
	CHECK(cycles.count("GondorCavalry") == 1);
}
