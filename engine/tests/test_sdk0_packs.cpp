// OpenBFME unit tests: lane SDK-0, cosmetic asset packs (GameClient/AssetPacks.h). GPL-3.0.
// Every pack and archive here is written by the test (no retail bytes).

#include "doctest.h"
#include "BigTestUtil.h"

#include "Common/ArchiveFile.h"
#include "GameClient/AssetPacks.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

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
		path = fs::temp_directory_path() / ("openbfme-sdk0-" + std::to_string(rng()));
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

std::string readSource(W3DFileSource &src, const std::string &path)
{
	std::vector<std::uint8_t> out;
	std::string error;
	if (!src.Read(path, out, &error))
	{
		return "<" + error + ">";
	}
	return asString(out);
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

TEST_CASE("SDK-0: a pack replaces textures by stem and models by file name, any folder, any case; a later pack wins")
{
	TempDir tmp;
	writeFile(tmp.path / "HDUnits" / "textures" / "GUMaArms.DDS", "DDS hd");
	writeFile(tmp.path / "HDUnits" / "gumaarms_skn.W3D", "w3d hd");
	writeFile(tmp.path / "HDUnits" / "readme.txt", "notes");
	writeFile(tmp.path / "HDUnits" / "preview.blend", "x");
	writeFile(tmp.path / "Better" / "gumaarms.png", "png better");

	AssetPacks packs;
	std::vector<std::string> errors;
	REQUIRE_MESSAGE(packs.addPack((tmp.path / "HDUnits").u8string(), errors), joined(errors));
	REQUIRE(packs.texture("GUMaArms.tga") != nullptr); // the retail name's extension and case do not matter
	CHECK(packs.texture("art\\textures\\gumaarms.tga")->relative == "textures/GUMaArms.DDS");
	CHECK(packs.model("art\\w3d\\gu\\GUMaArms_SKN.w3d")->relative == "gumaarms_skn.W3D");
	CHECK(packs.model("art\\w3d\\gu\\gumaarms_skl.w3d") == nullptr);
	CHECK(packs.texture("gumaarmsx") == nullptr);
	CHECK(packs.notes() == std::vector<std::string>{ "HDUnits: preview.blend is not a model or texture (.w3d, .dds, .tga, .png, .jpg): not used" });

	REQUIRE_MESSAGE(packs.addPack((tmp.path / "Better").u8string(), errors), joined(errors));
	CHECK(packs.texture("gumaarms.tga")->relative == "gumaarms.png");
	CHECK(packs.notes().back() == "Better: gumaarms.png replaces HDUnits's textures/GUMaArms.DDS");
	CHECK(packs.packs() == std::vector<std::string>{ "HDUnits", "Better" });
	CHECK(packs.textureCount() == 1);
	CHECK(packs.modelCount() == 1);
}

TEST_CASE("SDK-0: one pack holding a name twice is an error and adds nothing")
{
	TempDir tmp;
	writeFile(tmp.path / "Dup" / "a" / "tree.dds", "1");
	writeFile(tmp.path / "Dup" / "b" / "TREE.tga", "2");
	AssetPacks packs;
	std::vector<std::string> errors;
	CHECK_FALSE(packs.addPack((tmp.path / "Dup").u8string(), errors));
	CHECK(errors == std::vector<std::string>{ "asset pack Dup: a/tree.dds and b/TREE.tga replace the same asset 'tree': keep one" });
	CHECK(packs.empty());
	CHECK(packs.packs().empty());
	CHECK_FALSE(packs.addPack((tmp.path / "Nope").u8string(), errors));
	CHECK(errors.back() == "asset pack folder does not exist: '" + (tmp.path / "Nope").u8string() + "'");
}

TEST_CASE("SDK-0: packs reach only the render side's W3D source; the archive file system the simulation reads is unchanged")
{
	TempDir tmp;
	writeFile(tmp.path / "Pack" / "gumaarms_skn.w3d", "hd model");
	Win32BIGFileSystem fsys;
	std::string error;
	auto base = fsys.openArchiveFromMemory("W3D.big",
		std::make_shared<const std::vector<std::uint8_t>>(
			makeBig("BIGF", { { "art\\w3d\\gu\\gumaarms_skn.w3d", "retail model" }, { "art\\w3d\\gu\\gumaarms_skl.w3d", "retail skeleton" } })),
		&error);
	REQUIRE_MESSAGE(base, error);
	fsys.mountArchive(std::move(base), "W3D.big", false);

	AssetPacks packs;
	std::vector<std::string> errors;
	REQUIRE(packs.addPack((tmp.path / "Pack").u8string(), errors));
	AssetPackW3DFileSource render(std::unique_ptr<W3DFileSource>(new ArchiveW3DFileSource(fsys)), packs);
	ArchiveW3DFileSource simulation(fsys); // LiveGame's m_simAssetSource: launch bones (RW 0x6756A1) and PristinePose read this one

	CHECK(readSource(render, "art\\w3d\\gu\\gumaarms_skn.w3d") == "hd model");
	CHECK(readSource(render, "art\\w3d\\gu\\gumaarms_skl.w3d") == "retail skeleton"); // not in the pack: the archives'
	CHECK(readSource(simulation, "art\\w3d\\gu\\gumaarms_skn.w3d") == "retail model");
	std::vector<std::uint8_t> bytes;
	REQUIRE(fsys.readFile("art\\w3d\\gu\\gumaarms_skn.w3d", bytes, &error));
	CHECK(asString(bytes) == "retail model");
	CHECK(render.Exists("art\\w3d\\zz\\gumaarms_skn.w3d")); // matched by file name, whatever the folder

	// a pack file that changes after loading is a read error, not new art
	writeFile(tmp.path / "Pack" / "gumaarms_skn.w3d", "changed model!");
	CHECK(readSource(render, "art\\w3d\\gu\\gumaarms_skn.w3d") ==
		"<asset pack file changed or vanished since the packs were loaded (restart to use it): Pack/gumaarms_skn.w3d>");
}

TEST_CASE("SDK-0: a symbolic link inside a pack is refused")
{
	TempDir tmp;
	writeFile(tmp.path / "outside.dds", "x");
	writeFile(tmp.path / "Linky" / "ok.dds", "y");
	std::error_code ec;
	fs::create_symlink(tmp.path / "outside.dds", tmp.path / "Linky" / "evil.dds", ec);
	if (ec)
	{
		MESSAGE("SKIP: this file system cannot make symbolic links: " << ec.message());
		return;
	}
	AssetPacks packs;
	std::vector<std::string> errors;
	CHECK_FALSE(packs.addPack((tmp.path / "Linky").u8string(), errors));
	CHECK(errors == std::vector<std::string>{ "asset pack Linky: symbolic links are not supported: evil.dds" });
	CHECK(packs.empty());
}
