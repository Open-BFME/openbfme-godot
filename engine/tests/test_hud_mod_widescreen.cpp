// The HUD's widescreen correction on movies a mod authored for 16:9 (GameClient/GUI/AptScreens/AptWidescreen.h, docs/HUD_SCALING.md).
// Edain 4.8.5.4 and Patch 2.02 9.8.6 squeeze their Palantir, SpellStore and StrategicPalantir clips to 3/4 of their width for retail's
// stretched stage; the port's correction came on top of that (0.5625 of the width on 16:9: the squashed construction bar).

#include "doctest.h"

#include "RetailTestMount.h"

#include "Common/ArchiveFile.h"
#include "GameClient/AptCanvas.h"
#include "GameClient/GUI/AptScreens/AptWidescreen.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include <cmath>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
AptFrameItem place(float a, float d, float tx = 0.0f, const std::string &name = std::string())
{
	AptFrameItem item;
	item.type = APT_ITEM_PLACEOBJECT;
	item.place = std::make_shared<AptPlaceObject>();
	item.place->flags = APT_PLACE_HASMATRIX | (name.empty() ? 0u : (std::uint32_t)APT_PLACE_HASNAME);
	item.place->matrix[0] = a;
	item.place->matrix[3] = d;
	item.place->translation[0] = tx;
	item.place->name = name;
	return item;
}

AptCharacter sprite(const std::vector<AptFrameItem> &items)
{
	AptCharacter ch;
	ch.type = APT_CHAR_SPRITE;
	ch.frames.resize(1);
	ch.frames[0].items = items;
	return ch;
}

AptFile movie(const std::vector<AptFrameItem> &root, const std::vector<AptCharacter> &characters = {})
{
	AptFile file;
	file.width = 1024;
	file.height = 768;
	file.frames.resize(1);
	file.frames[0].items = root;
	file.characters = characters;
	return file;
}

std::vector<std::string> splitList(const std::string &list)
{
	std::vector<std::string> out;
	std::stringstream in(list);
	std::string part;
	while (std::getline(in, part, ';'))
	{
		if (!part.empty())
		{
			out.push_back(part);
		}
	}
	return out;
}
} // namespace

TEST_CASE("mod widescreen: a movie counts as authored for 16:9 by a squeezed root clip or ten squeezed placements")
{
	CHECK(AptWidescreen::authoredSqueeze(movie({})) == 1.0f);
	// retail's own non-uniform art (the Palantir's globe swirl 0.64 / 0.691, the spell store backdrop 10.24 / 7.68) is not a squeeze
	CHECK(AptWidescreen::authoredSqueeze(movie({ place(1.0f, 1.0f), place(0.64f, 0.691f), place(10.24f, 7.68f) })) == 1.0f);
	// Edain's SpellStore: one root clip (the tree) at 0.75
	CHECK(AptWidescreen::authoredSqueeze(movie({ place(1.0f, 1.0f, 512.0f, "Title"), place(0.75f, 1.0f, 512.0f, "SpellStore") })) ==
		AptWidescreen::kModSqueeze);
	// RegionBackground in Edain's Palantir: a uniformly scaled clip squeezed (0.984 / 1.311)
	CHECK(AptWidescreen::authoredSqueeze(movie({ place(0.98355f, 1.3114f) })) == AptWidescreen::kModSqueeze);
	// nested only: StrategicDetailsTray of both retail installs has 2, 2.02's SpellStore 51
	CHECK(AptWidescreen::authoredSqueeze(movie({ place(1.0f, 1.0f) }, { sprite({ place(0.75f, 1.0f), place(0.75f, 1.0f) }) })) == 1.0f);
	std::vector<AptFrameItem> nine(9, place(0.75f, 1.0f));
	CHECK(AptWidescreen::authoredSqueeze(movie({}, { sprite(nine) })) == 1.0f);
	nine.push_back(place(0.75f, 1.0f));
	CHECK(AptWidescreen::authoredSqueeze(movie({}, { sprite(nine) })) == AptWidescreen::kModSqueeze);
	// a placement without a matrix, a rotated or a mirrored one is not read as a squeeze
	AptFrameItem noMatrix = place(0.75f, 1.0f);
	noMatrix.place->flags = 0;
	AptFrameItem rotated = place(0.75f, 1.0f);
	rotated.place->matrix[1] = 0.1f;
	CHECK(AptWidescreen::authoredSqueeze(movie({ noMatrix, rotated, place(0.75f, -1.0f) })) == 1.0f);
}

TEST_CASE("mod widescreen: the correction leaves a 16:9 movie as retail shows it on 16:9 and keeps it proportional beyond")
{
	struct Case
	{
		int width, height;
		float retail, modded;
	};
	// sizes whose stage scales are exact in binary floating point, so every value below is exact
	for (const Case &c : { Case{ 1024, 768, 1.0f, 1.0f }, Case{ 1280, 960, 1.0f, 1.0f }, Case{ 1024, 1024, 1.0f, 1.0f },
		Case{ 1920, 1080, 0.75f, 1.0f }, Case{ 1280, 720, 0.75f, 1.0f }, Case{ 2560, 1080, 0.5625f, 0.75f },
		Case{ 5120, 1440, 0.375f, 0.5f } })
	{
		INFO(c.width, "x", c.height);
		CHECK(AptWidescreen::correction(1024.0f, 768.0f, c.width, c.height, 1.0f) == c.retail);
		const float modded = AptWidescreen::correction(1024.0f, 768.0f, c.width, c.height, AptWidescreen::kModSqueeze);
		CHECK(modded == c.modded);
		// a squeezed clip ends at the height-derived scale in both axes from 16:9 on: proportional, as the 4:3 movie is
		AptStageMapping mapping;
		mapping.windowW = (float)c.width;
		mapping.windowH = (float)c.height;
		if (c.width * 9 >= c.height * 16)
		{
			CHECK(modded * AptWidescreen::kModSqueeze * mapping.scaleX() == mapping.scaleY());
		}
	}
}

TEST_CASE("mod widescreen: the cache scans a movie once and again when the clip's movie changes")
{
	AptWidescreen::SqueezeCache cache;
	CHECK(cache.of(nullptr) == 1.0f);
	auto modded = std::make_shared<const AptFile>(movie({ place(0.75f, 1.0f, 256.0f, "SideCommandBar") }));
	auto retail = std::make_shared<const AptFile>(movie({ place(1.0f, 1.0f, 0.0f, "SideCommandBar") }));
	CHECK(cache.of(modded) == AptWidescreen::kModSqueeze);
	CHECK(cache.of(modded) == AptWidescreen::kModSqueeze);
	CHECK(cache.of(retail) == 1.0f);
	CHECK(cache.of(modded) == AptWidescreen::kModSqueeze);
	CHECK(cache.of(nullptr) == 1.0f);
}

TEST_CASE("mod widescreen retail: no BFME2 1.06 or RotWK 2.01 movie reads as authored for 16:9")
{
	struct Install
	{
		const char *label;
		retailtest::Mount *mount;
	};
	retailtest::Mount *pure = retailtest::pureMount();
	retailtest::Mount *bfme2 = retailtest::bfme2Mount();
	if (!pure && !bfme2)
	{
		retailtest::printSkip("mod widescreen retail");
		return;
	}
	for (const Install &install : { Install{ "RotWK 2.01", pure }, Install{ "BFME2 1.06", bfme2 } })
	{
		if (!install.mount)
		{
			continue;
		}
		REQUIRE_MESSAGE(install.mount->fs, install.mount->error);
		AptArchiveFileSource source(*install.mount->fs);
		AptLoader loader(source);
		std::size_t movies = 0;
		for (const std::string &name : source.listMovies())
		{
			std::string error;
			std::shared_ptr<const AptFile> file = loader.loadMovie(name, &error);
			if (!file)
			{
				continue; // a movie the corpus tests already report (Apt version, missing .const)
			}
			INFO(install.label, ": ", name);
			CHECK(AptWidescreen::authoredSqueeze(*file) == 1.0f);
			++movies;
		}
		INFO(install.label);
		// tolerance: the number of movies depends on the install's language and patch archives; this only guards against an empty listing
		CHECK(movies > 60);
	}
}

// OPENBFME_WIDESCREEN_MOD_ARCHIVES: ';'-separated .big archives a mod ships its 16:9 movies in, for example
// <Edain>\___edain_apt_widescreen.big;<Patch 2.02>\__patch202.big. A movie without its own .const takes the retail one (both mods keep the
// retail constants: Edain's StrategicPalantir, 2.02's SpellStore). Unset: skipped.
TEST_CASE("mod widescreen data: the mods' Palantir, SpellStore and StrategicPalantir are read as authored for 16:9, the bar keeps the edge")
{
	const char *list = std::getenv("OPENBFME_WIDESCREEN_MOD_ARCHIVES");
	if (!list || !*list)
	{
		retailtest::printSkip("mod widescreen data (OPENBFME_WIDESCREEN_MOD_ARCHIVES unset)");
		return;
	}
	retailtest::Mount *pure = retailtest::pureMount();
	Win32BIGFileSystem fs;
	std::size_t checked = 0;
	for (const std::string &path : splitList(list))
	{
		std::string error;
		std::unique_ptr<ArchiveFile> archive = fs.openArchiveFile(path, &error);
		REQUIRE_MESSAGE(archive, path << ": " << error);
		for (const char *name : { "Palantir", "SpellStore", "StrategicPalantir" })
		{
			auto aptBytes = std::make_shared<std::vector<std::uint8_t>>();
			if (!archive->readFile(std::string(name) + ".apt", *aptBytes, nullptr))
			{
				continue;
			}
			INFO(path, ": ", name);
			std::vector<std::uint8_t> constBytes;
			if (!archive->readFile(std::string(name) + ".const", constBytes, nullptr))
			{
				REQUIRE_MESSAGE(pure, "the retail .const of " << name << " needs ROTWK_INSTALL / BFME2_INSTALL");
				REQUIRE_MESSAGE(pure->fs->readFile(std::string(name) + ".const", constBytes, &error), error);
			}
			AptConstFile consts;
			REQUIRE_MESSAGE(AptConstFile::parse(constBytes, consts, &error), error);
			AptFile file;
			REQUIRE_MESSAGE(AptFile::parse(name, aptBytes, consts, file, &error), error);
			CHECK(AptWidescreen::authoredSqueeze(file) == AptWidescreen::kModSqueeze);
			++checked;
			// The construction bar: authored at 0.75 with its right edge on the stage's (tx 256); the correction anchors it there too
			for (const AptFrameItem &item : file.frames.empty() ? std::vector<AptFrameItem>() : file.frames[0].items)
			{
				if (!item.place || item.place->name != "SideCommandBar")
				{
					continue;
				}
				const AptPlaceObject &bar = *item.place;
				// both mods store exactly 0.75 / 1 / 256 (0x3F400000, 0x3F800000, 0x43800000)
				CHECK(bar.matrix[0] == 0.75f);
				CHECK(bar.matrix[3] == 1.0f);
				CHECK(bar.matrix[0] * 1024.0f + bar.translation[0] == 1024.0f);
				for (const int width : { 1280, 1920, 2560, 5120 })
				{
					const int height = width == 2560 ? 1080 : width == 5120 ? 1440 : width * 9 / 16;
					INFO(width, "x", height);
					AptStageMapping mapping;
					mapping.windowW = (float)width;
					mapping.windowH = (float)height;
					const float c = AptWidescreen::correction(1024.0f, 768.0f, width, height, AptWidescreen::kModSqueeze);
					// the bar's width in window pixels per stage unit: the height-derived scale (proportional), not 0.5625 of it on 16:9
					CHECK(c * bar.matrix[0] * mapping.scaleX() == mapping.scaleY() * bar.matrix[3]);
					const float right = c * (bar.matrix[0] * 1024.0f + bar.translation[0]) + 1024.0f * (1.0f - c);
					CHECK(right == 1024.0f);
				}
			}
		}
	}
	// tolerance: the archives in the list decide how many of the three movies there are; none would mean a wrong list
	CHECK(checked > 0);
}
