// OpenBFME unit tests. GPL-3.0.
// Lane UI-4 (the owner's finding on v0.3.0-preview.1, 2026-10-10: no portrait in the Palantir for a selected hero or builder). The Palantir's portrait is
// the selection's template SelectPortrait (template + 0x74, PalantirCommandUI RW 0x92FE33 -> RW 0x694F06 -> RW 0x73CF31), a MappedImage the HUD draws
// from its texture (art\textures\<file>, else the packed art\compiledtextures\<xx>\<stem>.dds / .jpg). This test walks every template of the retail
// object world: each SelectPortrait must name a mapped image whose texture exists, so no faction's hero, builder, horde or building draws an empty
// portrait beyond the retail data's own gaps (pinned). The live draw is checked by game.gd --play1=portraits (scripts/play1_player.gd). SKIPs loudly
// without the installs.

#include "HudTestUtil.h"

#include "Common/INI.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/GUI/Image.h"

#include <set>
#include <variant>

using namespace hudtest;

namespace
{
std::string lower(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

bool textureExists(ArchiveFileSystem &fs, const std::string &file)
{
	std::string f = file;
	for (char &c : f)
	{
		c = c == '\\' ? '/' : c;
	}
	if (fs.doesFileExist("art/textures/" + f))
	{
		return true;
	}
	const std::string stem = lower(f.substr(0, f.find_last_of('.')));
	if (stem.size() < 2)
	{
		return false;
	}
	const std::string base = "art/compiledtextures/" + stem.substr(0, 2) + "/" + stem;
	return fs.doesFileExist(base + ".dds") || fs.doesFileExist(base + ".jpg") || fs.doesFileExist("art/compiledtextures/" + stem.substr(0, 2) + "/" + lower(f));
}
} // namespace

TEST_CASE("ui4 retail: every template's SelectPortrait names a mapped image whose texture the HUD can load")
{
	if (!haveWorld("ui4 portraits"))
	{
		return;
	}
	SharedWorld &s = shared();
	MappedImageCollection images;
	INIEnvironment env;
	env.fileSystem = s.mount->fs.get();
	images.registerBlocks(env.blocks);
	INI ini(env);
	REQUIRE_NOTHROW(ini.loadDirectory("Data\\INI\\MappedImages", true, INI_LOAD_OVERWRITE));
	int withPortrait = 0;
	std::set<std::string> kinds;
	std::vector<std::string> missing;
	std::vector<std::string> keyMissing; // heroes and builders (KindOf HERO / DOZER)
	for (const ThingTemplate *t : s.world->things().templates())
	{
		const FieldValue *v = t->findField("SelectPortrait");
		const std::string *name = v ? std::get_if<std::string>(v) : nullptr;
		if (!name || name->empty())
		{
			continue;
		}
		++withPortrait;
		bool key = false;
		for (const RawTokens &row : t->kindOfRows())
		{
			for (const std::string &tok : row.tokens)
			{
				key = key || tok == "HERO" || tok == "DOZER";
			}
		}
		const Image *img = images.findImageByName(*name);
		if (!img)
		{
			missing.push_back(t->getName() + ": mapped image " + *name + " is unknown");
			if (key)
			{
				keyMissing.push_back(t->getName());
			}
			continue;
		}
		if (!textureExists(*s.mount->fs, img->filename))
		{
			missing.push_back(t->getName() + ": " + *name + "'s texture " + img->filename + " is not in the archives");
			if (key)
			{
				keyMissing.push_back(t->getName());
			}
		}
	}
	MESSAGE(withPortrait << " templates with a SelectPortrait, " << missing.size() << " unresolved");
	if (missing.size() != 212)
	{
		for (const std::string &m : missing)
		{
			MESSAGE(m);
		}
	}
	CHECK(withPortrait == 1346);
	// the retail data's own gaps (RotWK's lookup RW 0x73CF31 finds no image for them either and logs "is looking for Portrait ... but can't find
	// it"): cinematic, campaign and scenery templates, foundations, banners; among heroes and builders only these four. A port that loses
	// portraits shows up as a larger count here.
	CHECK(missing.size() == 212);
	const std::vector<std::string> expectedKey = { "CINE_MordorPorter", "MordorShelob", "MordorShelobNoFear", "GondorDamrod" };
	CHECK(keyMissing == expectedKey);
}
