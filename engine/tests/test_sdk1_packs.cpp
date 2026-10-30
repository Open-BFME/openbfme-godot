// OpenBFME unit tests: lane SDK-1, the modding SDK's asset packs made cosmetic-only and checked (GameClient/AssetPacks.h). GPL-3.0.
// Every pack, model and archive here is written by the test (no retail bytes).

#include "doctest.h"
#include "BigTestUtil.h"
#include "RetailTestMount.h"
#include "W3dSynth.h"

#include "Common/ArchiveFile.h"
#include "Common/Sha256.h"
#include "GameClient/AssetPacks.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

using namespace bigtest;
using namespace w3dsynth;
namespace fs = std::filesystem;

namespace
{

struct TempDir
{
	fs::path path;
	TempDir()
	{
		std::mt19937_64 rng((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
		path = fs::temp_directory_path() / ("openbfme-sdk1-" + std::to_string(rng()));
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

std::string str(const std::vector<std::uint8_t> &v)
{
	return std::string(v.begin(), v.end());
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

std::string readSource(W3DFileSource &src, const std::string &path)
{
	std::vector<std::uint8_t> out;
	std::string error;
	if (!src.Read(path, out, &error))
	{
		return "<" + error + ">";
	}
	return str(out);
}

// a made-up unit: its skeleton file (gumaarms_skl.w3d: ROOTTRANSFORM, B_SPINE, B_HEAD) and its model (gumaarms.w3d: HLOD GUMaArms on it)
std::vector<std::uint8_t> skeleton(const char *spine = "B_SPINE", std::uint32_t headParent = 1)
{
	return hierarchyChunk("GUMaArms_SKL", { pivot("ROOTTRANSFORM", 0xffffffffu, 0, 0, 0), pivot(spine, 0, 0, 0, 1), pivot("B_HEAD", headParent, 0, 0, 2) });
}

std::vector<std::uint8_t> model(const char *hierarchy = "GUMaArms_SKL", const char *mesh = "GUMaArms.BODY")
{
	return hlodChunk("GUMaArms", hierarchy, { { mesh, 1 } });
}

void mount(Win32BIGFileSystem &fsys, const char *name, const std::vector<std::pair<std::string, std::string>> &files, bool overwrite)
{
	std::string error;
	auto a = fsys.openArchiveFromMemory(name, std::make_shared<const std::vector<std::uint8_t>>(makeBig("BIGF", files)), &error);
	REQUIRE_MESSAGE(a, error);
	fsys.mountArchive(std::move(a), name, overwrite);
}

} // namespace

TEST_CASE("SDK-1: a pack holds models, textures and its own text only; any other file refuses the whole pack")
{
	TempDir tmp;
	writeFile(tmp.path / "Clean" / "README.md", "about");
	writeFile(tmp.path / "Clean" / "LICENSE", "cc-by");
	writeFile(tmp.path / "Clean" / "credits.txt", "me");
	writeFile(tmp.path / "Clean" / "gumaarms.dds", "DDS ");
	AssetPacks packs;
	std::vector<std::string> errors;
	REQUIRE_MESSAGE(packs.addPack((tmp.path / "Clean").u8string(), errors), joined(errors));
	CHECK(packs.notes().empty());

	for (const char *data : { "data/ini/weapon.ini", "!!HD.big", "data/scripts/scripts.scb", "maps/m/m.map", "audio/sound.wav", "x.lua", "d3d8.dll" })
	{
		CAPTURE(data);
		TempDir t;
		writeFile(t.path / "Sneaky" / "tree.dds", "DDS ");
		writeFile(t.path / "Sneaky" / data, "data");
		AssetPacks p;
		std::vector<std::string> e;
		CHECK_FALSE(p.addPack((t.path / "Sneaky").u8string(), e));
		REQUIRE(e.size() == 1);
		CHECK(e[0] == "asset pack Sneaky: " + std::string(data) + " is not a model or texture (.w3d, .dds, .tga, .png, .jpg): an asset pack only "
			"changes how the game looks; game data (INI, scripts, maps, archives, sounds, programs) belongs in a mod (-mod)");
		CHECK(p.empty()); // nothing of the pack, not even its texture
	}
}

TEST_CASE("SDK-1: a replacement model must keep the skeleton, models and animations of the file it replaces")
{
	Win32BIGFileSystem fsys;
	mount(fsys, "W3D.big", { { "art\\w3d\\gu\\gumaarms_skl.w3d", str(skeleton()) }, { "art\\w3d\\gu\\gumaarms.w3d", str(model()) } }, false);
	ArchiveW3DFileSource mounted(fsys);

	SUBCASE("an HD model on the same skeleton fits; a model replacing nothing is a note")
	{
		TempDir tmp;
		writeFile(tmp.path / "HD" / "gumaarms.w3d", str(model("gumaarms_skl", "GUMaArms.BODY_HD"))); // other meshes, same hierarchy (any case)
		writeFile(tmp.path / "HD" / "gumaarms_skl.w3d", str(skeleton())); // the same bones, new rest pose values would be fine too
		writeFile(tmp.path / "HD" / "gumystery.w3d", str(model()));
		AssetPacks packs;
		std::vector<std::string> errors;
		REQUIRE(packs.addPack((tmp.path / "HD").u8string(), errors));
		CHECK_MESSAGE(packs.checkModels(mounted, errors), joined(errors));
		CHECK(packs.notes() == std::vector<std::string>{ "HD: gumystery.w3d replaces no model of the game or the mod (art\\w3d\\gu\\gumystery.w3d): not used" });
	}
	SUBCASE("a renamed bone, a moved bone, another hierarchy or a dropped HLOD leaves that model out with a note; a broken file is an error")
	{
		const std::string tail = "): not used, the game's model is drawn (a replacement model keeps the original's skeleton, models and animations, "
								 "or the game's animations and effects would not fit it)";
		struct Case
		{
			const char *what;
			std::string bytes;
			std::string file;
			std::string why;
		};
		const std::vector<Case> cases = {
			{ "renamed bone", str(skeleton("B_TORSO")), "gumaarms_skl.w3d",
				"Bad: gumaarms_skl.w3d does not fit art\\w3d\\gu\\gumaarms_skl.w3d (its hierarchy GUMaArms_SKL: bone 1 is B_TORSO (parent 0), the "
				"original's B_SPINE (parent 0)" + tail },
			{ "moved bone", str(skeleton("B_SPINE", 0)), "gumaarms_skl.w3d",
				"Bad: gumaarms_skl.w3d does not fit art\\w3d\\gu\\gumaarms_skl.w3d (its hierarchy GUMaArms_SKL: bone 2 is B_HEAD (parent 0), the "
				"original's B_HEAD (parent 1)" + tail },
			{ "other hierarchy", str(model("GUOther_SKL")), "gumaarms.w3d",
				"Bad: gumaarms.w3d does not fit art\\w3d\\gu\\gumaarms.w3d (its model GUMaArms uses the hierarchy GUOther_SKL, the original's "
				"GUMaArms_SKL" + tail },
			{ "fewer bones", str(hierarchyChunk("GUMaArms_SKL", { pivot("ROOTTRANSFORM", 0xffffffffu, 0, 0, 0), pivot("B_SPINE", 0, 0, 0, 1) })),
				"gumaarms_skl.w3d",
				"Bad: gumaarms_skl.w3d does not fit art\\w3d\\gu\\gumaarms_skl.w3d (its hierarchy GUMaArms_SKL has 2 bones, the original's 3" + tail },
			{ "bones in another order", str(hierarchyChunk("GUMaArms_SKL", { pivot("ROOTTRANSFORM", 0xffffffffu, 0, 0, 0), pivot("B_HEAD", 0, 0, 0, 2),
											   pivot("B_SPINE", 0, 0, 0, 1) })),
				"gumaarms_skl.w3d",
				"Bad: gumaarms_skl.w3d does not fit art\\w3d\\gu\\gumaarms_skl.w3d (its hierarchy GUMaArms_SKL: bone 1 is B_HEAD (parent 0), the "
				"original's B_SPINE (parent 0)" + tail },
			{ "dropped HLOD", str(skeleton()), "gumaarms.w3d",
				"Bad: gumaarms.w3d does not fit art\\w3d\\gu\\gumaarms.w3d (it has no model GUMaArms (the original's HLOD)" + tail },
		};
		for (const Case &c : cases)
		{
			CAPTURE(c.what);
			TempDir tmp;
			writeFile(tmp.path / "Bad" / c.file, c.bytes);
			writeFile(tmp.path / "Bad" / "gumaarms.dds", "DDS ");
			AssetPacks packs;
			std::vector<std::string> errors;
			REQUIRE(packs.addPack((tmp.path / "Bad").u8string(), errors));
			CHECK(packs.checkModels(mounted, errors));
			CHECK(errors.empty());
			CHECK(packs.notes() == std::vector<std::string>{ c.why });
			CHECK(packs.model(c.file) == nullptr); // left out: the game's model is drawn
			CHECK(packs.texture("gumaarms.tga") != nullptr); // the rest of the pack stays
		}
		TempDir tmp;
		writeFile(tmp.path / "Bad" / "gumaarms.w3d", "not a w3d");
		AssetPacks packs;
		std::vector<std::string> errors;
		REQUIRE(packs.addPack((tmp.path / "Bad").u8string(), errors));
		CHECK_FALSE(packs.checkModels(mounted, errors));
		REQUIRE(errors.size() == 1);
		CHECK(errors[0].rfind("asset pack Bad: gumaarms.w3d is not a W3D file the game can read: ", 0) == 0);
	}
}

TEST_CASE("SDK-1: a replacement animation file must keep the original's animations")
{
	const std::vector<SynthChannel> channel = { { ANIM_CHANNEL_X, 1, 0, 1, { 0.0f, 1.0f } } };
	Win32BIGFileSystem fsys;
	mount(fsys, "W3D.big", { { "art\\w3d\\gu\\gumaarms_atka.w3d", str(rawAnimChunk("GUMaArms_ATKA", "GUMaArms_SKL", 2, 30, channel)) } }, false);
	ArchiveW3DFileSource mounted(fsys);
	W3DFileContents original;
	std::string error;
	std::vector<std::uint8_t> bytes = rawAnimChunk("GUMaArms_ATKA", "GUMaArms_SKL", 2, 30, channel);
	REQUIRE_MESSAGE(Load_W3D_File(bytes.data(), bytes.size(), original, &error), error);
	REQUIRE(original.RawAnims.size() == 1);
	const std::string animName = original.RawAnims[0].Get_Name();

	TempDir tmp;
	writeFile(tmp.path / "Same" / "gumaarms_atka.w3d", str(rawAnimChunk("GUMaArms_ATKA", "GUMaArms_SKL", 2, 30, { { ANIM_CHANNEL_X, 1, 0, 1, { 0.0f, 2.0f } } })));
	writeFile(tmp.path / "Renamed" / "gumaarms_atka.w3d", str(rawAnimChunk("GUMaArms_ATKB", "GUMaArms_SKL", 2, 30, channel)));
	AssetPacks same, renamed;
	std::vector<std::string> errors;
	REQUIRE(same.addPack((tmp.path / "Same").u8string(), errors));
	CHECK(same.checkModels(mounted, errors));
	CHECK(same.notes().empty()); // other key values on the same animation: fits
	REQUIRE(renamed.addPack((tmp.path / "Renamed").u8string(), errors));
	CHECK(renamed.checkModels(mounted, errors));
	REQUIRE(renamed.notes().size() == 1);
	CHECK(renamed.notes()[0].find("(it has no animation " + animName + ")") != std::string::npos);
	CHECK(renamed.model("gumaarms_atka.w3d") == nullptr);
}

TEST_CASE("SDK-1: precedence: base game < mod < asset pack for drawing; the simulation keeps the mod's file and the skeleton check uses it")
{
	Win32BIGFileSystem fsys;
	mount(fsys, "W3D.big", { { "art\\w3d\\gu\\gumaarms.w3d", "retail" } }, false);
	// the mod's archive mounted over the game's (as -mod does: Common/ModMount.h), with its own skeleton name
	mount(fsys, "!!Mod.big", { { "art\\w3d\\gu\\gumaarms.w3d", str(model("GUModArms_SKL")) } }, true);
	ArchiveW3DFileSource simulation(fsys);
	REQUIRE(readSource(simulation, "art\\w3d\\gu\\gumaarms.w3d") == str(model("GUModArms_SKL")));

	TempDir tmp;
	writeFile(tmp.path / "HD" / "GUMaArms.w3d", str(model("GUModArms_SKL", "GUMaArms.HD")));
	AssetPacks packs;
	std::vector<std::string> errors;
	REQUIRE(packs.addPack((tmp.path / "HD").u8string(), errors));
	CHECK_MESSAGE(packs.checkModels(simulation, errors), joined(errors)); // fits the mod's model, which is what it replaces
	AssetPackW3DFileSource render(std::unique_ptr<W3DFileSource>(new ArchiveW3DFileSource(fsys)), packs);
	CHECK(readSource(render, "art\\w3d\\gu\\gumaarms.w3d") == str(model("GUModArms_SKL", "GUMaArms.HD")));
	CHECK(readSource(simulation, "art\\w3d\\gu\\gumaarms.w3d") == str(model("GUModArms_SKL"))); // unchanged: the logic never sees a pack

	// a pack made for the unmodded game (the retail skeleton) does not fit the mod's model: left out, rather than drawing a broken unit
	TempDir other;
	writeFile(other.path / "Vanilla" / "gumaarms.w3d", str(model()));
	AssetPacks vanilla;
	REQUIRE(vanilla.addPack((other.path / "Vanilla").u8string(), errors));
	CHECK(vanilla.checkModels(simulation, errors));
	REQUIRE(vanilla.notes().size() == 1);
	CHECK(vanilla.notes()[0].find("its model GUMaArms uses the hierarchy GUMaArms_SKL, the original's GUModArms_SKL") != std::string::npos);
	CHECK(vanilla.model("gumaarms.w3d") == nullptr);
}

namespace
{
std::string envOr(const char *name)
{
	const char *v = std::getenv(name);
	return v ? std::string(v) : std::string();
}
} // namespace

TEST_CASE("SDK-1 retail: a pack holding a retail model fits it; the same model with a renamed bone is left out")
{
	retailtest::Mount *m = retailtest::pureMount();
	if (!m)
	{
		retailtest::printSkip("SDK-1 retail skeleton check");
		return;
	}
	REQUIRE_MESSAGE(m->fs, m->error);
	ArchiveW3DFileSource mounted(*m->fs);
	// the Gondor archer's skeleton, copied from the player's own archives at run time (nothing retail is committed)
	const std::string path = W3D_Asset_Path("guarcher_skl", mounted.FlatAssetLayout());
	std::vector<std::uint8_t> bytes;
	std::string error;
	REQUIRE_MESSAGE(mounted.Read(path, bytes, &error), error);
	W3DFileContents original;
	REQUIRE_MESSAGE(Load_W3D_File(bytes.data(), bytes.size(), original, &error), error);
	REQUIRE(!original.HTrees.empty());
	REQUIRE(original.HTrees[0].Num_Pivots() > 2); // tolerance: any retail skeleton with a bone 2 serves (its size is the player's data)

	TempDir tmp;
	writeFile(tmp.path / "Same" / "guarcher_skl.w3d", str(bytes));
	AssetPacks same;
	std::vector<std::string> errors;
	REQUIRE(same.addPack((tmp.path / "Same").u8string(), errors));
	CHECK_MESSAGE(same.checkModels(mounted, errors), joined(errors));

	// rename bone 2 in the file's pivot table (W3dPivotStruct.Name, 16 bytes) to a name no retail skeleton has
	const std::string bone = original.HTrees[0].Get_Pivot(2).Name;
	std::string renamed = str(bytes);
	const size_t at = renamed.find(bone + std::string(1, '\0'));
	REQUIRE(at != std::string::npos);
	renamed.replace(at, 4, "ZZZZ");
	writeFile(tmp.path / "Renamed" / "guarcher_skl.w3d", renamed);
	AssetPacks bad;
	REQUIRE(bad.addPack((tmp.path / "Renamed").u8string(), errors));
	CHECK(bad.checkModels(mounted, errors));
	REQUIRE(bad.notes().size() == 1);
	CHECK(bad.notes()[0].find("Renamed: guarcher_skl.w3d does not fit " + path + " (its hierarchy ") == 0);
	CHECK(bad.model(path) == nullptr);
}

TEST_CASE("SDK-1 retail: the asset pack in OPENBFME_ASSET_PACK loads over 2.01; its models that do not fit are listed")
{
	const std::string pack = envOr("OPENBFME_ASSET_PACK");
	retailtest::Mount *m = retailtest::pureMount();
	if (!m || pack.empty())
	{
		retailtest::printSkip("SDK-1 asset pack corpus (OPENBFME_ASSET_PACK unset)");
		return;
	}
	REQUIRE_MESSAGE(m->fs, m->error);
	ArchiveW3DFileSource mounted(*m->fs);
	AssetPacks packs;
	std::vector<std::string> errors;
	REQUIRE_MESSAGE(packs.addPack(pack, errors), joined(errors));
	const bool fits = packs.checkModels(mounted, errors);
	MESSAGE(packs.modelCount() << " models used, " << packs.textureCount() << " textures; " << packs.notes().size()
							   << " notes (models left out or replacing nothing)");
	MESSAGE(joined(errors));
	MESSAGE(joined(packs.notes()));
	CHECK(fits);
}

TEST_CASE("SDK-1b (a): suspended packs answer nothing (the game's own files are drawn); the packs stay loaded")
{
	TempDir tmp;
	writeFile(tmp.path / "HD" / "gumaarms.dds", "DDS ");
	writeFile(tmp.path / "HD" / "gumaarms.w3d", str(model()));
	AssetPacks packs;
	std::vector<std::string> errors;
	REQUIRE(packs.addPack((tmp.path / "HD").u8string(), errors));
	Win32BIGFileSystem fsys;
	mount(fsys, "W3D.big", { { "art\\w3d\\gu\\gumaarms.w3d", "retail" } }, false);
	AssetPackW3DFileSource render(std::unique_ptr<W3DFileSource>(new ArchiveW3DFileSource(fsys)), packs);
	REQUIRE(packs.texture("GUMaArms.tga") != nullptr);
	REQUIRE(readSource(render, "art\\w3d\\gu\\gumaarms.w3d") == str(model()));
	packs.setSuspended(true);
	CHECK(packs.suspended());
	CHECK(packs.loaded());
	CHECK(packs.empty()); // RetailFileSystem::renderW3DSource then hands out the archives alone
	CHECK(packs.texture("GUMaArms.tga") == nullptr);
	CHECK(packs.model("art\\w3d\\gu\\gumaarms.w3d") == nullptr);
	CHECK(readSource(render, "art\\w3d\\gu\\gumaarms.w3d") == "retail"); // a source made before also stops answering with the pack
	packs.setSuspended(false);
	CHECK(packs.texture("GUMaArms.tga") != nullptr);
}

TEST_CASE("SDK-1c: a pack's presentation line names the files the game uses by their bytes, in mount order (profile 3, agreed with Anti-cheat)")
{
	Win32BIGFileSystem fsys;
	mount(fsys, "W3D.big", { { "art\\w3d\\gu\\gumaarms_skl.w3d", str(skeleton()) }, { "art\\w3d\\gu\\gumaarms.w3d", str(model()) } }, false);
	ArchiveW3DFileSource mounted(fsys);
	TempDir tmp;
	const std::string arms = str(model("gumaarms_skl", "GUMaArms.BODY_HD"));
	writeFile(tmp.path / "HD Units" / "w3d" / "gumaarms.w3d", arms);              // fits: used
	writeFile(tmp.path / "HD Units" / "w3d" / "gumaarms_skl.w3d", str(skeleton("B_TORSO"))); // a misfit: not used, not in the line
	writeFile(tmp.path / "HD Units" / "GUMaArms.DDS", "DDS old");                  // the later pack wins this name
	writeFile(tmp.path / "HD Units" / "GUMaHelm.dds", "DDS helm");
	writeFile(tmp.path / "Later" / "gumaarms.png", "PNG new");
	writeFile(tmp.path / "Empty" / "gumaarms.tga", "TGA lost");                    // every file taken by the later pack: no line
	AssetPacks packs;
	std::vector<std::string> errors;
	REQUIRE(packs.addPack((tmp.path / "HD Units").u8string(), errors));
	REQUIRE(packs.addPack((tmp.path / "Empty").u8string(), errors));
	REQUIRE(packs.addPack((tmp.path / "Later").u8string(), errors));
	REQUIRE_MESSAGE(packs.checkModels(mounted, errors), joined(errors));
	auto sha = [](const std::string &s) {
		const auto d = Sha256::hash(s.data(), s.size());
		return HexBytes(d.data(), d.size());
	};
	const std::string hdText = "model gumaarms.w3d " + sha(arms) + "\n" + "texture gumahelm " + sha("DDS helm") + "\n";
	const std::string laterText = "texture gumaarms " + sha("PNG new") + "\n";
	std::vector<std::string> lines;
	std::string error;
	REQUIRE_MESSAGE(packs.identityLines(lines, error), error);
	CHECK(lines == std::vector<std::string>{ "pack HD Units 2 " + sha(hdText), "pack Later 1 " + sha(laterText) });
	// a file changed after the load is a read error naming it, never a silent line
	writeFile(tmp.path / "Later" / "gumaarms.png", "PNG changed, longer");
	CHECK_FALSE(packs.identityLines(lines, error));
	CHECK(error.find("Later/gumaarms.png") != std::string::npos);
}
