// OpenBFME unit tests. GPL-3.0.
// lane PROFILE-3 (owner: permissive; agreed with MODS / SDK-1c): the profile identity's digest covers only what decides whether two players can play
// together (engine, features, numeric, rng, a mod's sim scripts, the simulation data); the archive lines, a mod's names and content hashes and the
// asset packs' lines stay in the text as presentation. A version 3 text turns back into its exact version 2 / version 1 text for older replays and saves.

#include "Common/MD5.h"
#include "Common/System/SaveGame/SaveGameFile.h"
#include "GameNetwork/ProfileIdentity.h"

#include "doctest.h"

namespace
{
const char *const kSim = "0123456789abcdef0123456789abcdef";
const std::string kPackA = "pack HD Orcs 2 " + std::string(64, 'a');
const std::string kPackB = "pack Better Trees 1 " + std::string(64, 'b');

std::vector<MountedArchive> archivesNamed(const std::vector<std::string> &names)
{
	std::vector<MountedArchive> v;
	for (const std::string &n : names)
	{
		MountedArchive a;
		a.install = "rotwk";
		a.canonicalPath = n;
		a.md5 = MD5::ofBytes(n.data(), n.size());
		a.size = n.size();
		v.push_back(a);
	}
	return v;
}

ProfileIdentity identity(const std::vector<std::string> &mods, const std::vector<std::string> &packs, const std::string &sim = kSim,
	const std::vector<std::string> &archives = { "INI.big", "_patch201.big" })
{
	return ProfileIdentity::compute(archivesNamed(archives), RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, mods, sim, packs);
}

// what a build of PROFILE-2 wrote for the same mount: header 2, no pack lines
std::string version2Text(const ProfileIdentity &v3)
{
	std::string out;
	size_t at = 0;
	while (at < v3.text.size())
	{
		const size_t end = v3.text.find('\n', at);
		const std::string l = v3.text.substr(at, end - at);
		at = end + 1;
		if (l.rfind("pack ", 0) == 0)
		{
			continue;
		}
		out += (l.rfind("openbfme-profile ", 0) == 0 ? std::string("openbfme-profile 2") : l) + "\n";
	}
	return out;
}
} // namespace

TEST_CASE("profile3 identity: the pack lines are written after simdata and never count")
{
	const ProfileIdentity none = identity({}, {});
	const ProfileIdentity packs = identity({}, { kPackA, kPackB });
	CHECK(packs.version() == 3);
	CHECK(packs.text.rfind("openbfme-profile 3\n", 0) == 0);
	CHECK(packs.text.find(std::string("\nsimdata ") + kSim + "\n" + kPackA + "\n" + kPackB + "\narchive 0 rotwk:INI.big ") != std::string::npos);
	CHECK(packs.identityText().find("pack ") == std::string::npos);
	CHECK(packs.digest == none.digest);
	CHECK(packs == none);
	CHECK(ProfileIdentity::difference(packs, none).empty());
	CHECK(ProfileIdentity::fromText(packs.text) == packs);
	// without a simdata line (the unit tests' version 1 identity) no pack line is written
	CHECK(ProfileIdentity::compute(archivesNamed({ "INI.big" }), RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Faithful, {}, {}, { kPackA })
			.text.find("pack ") == std::string::npos);
}

TEST_CASE("profile3 identity: a mod's names and content hashes never count; its sim scripts, its engine features and the simulation data do")
{
	const ProfileIdentity retail = identity({}, {});
	const ProfileIdentity mod = identity({ "mod folder Edain", "modcontent 2 archives 10 files " + std::string(32, 'c') }, {});
	const ProfileIdentity renamed = identity({ "mod folder Edain (copy)", "modcontent 2 archives 10 files " + std::string(32, 'd') }, {});
	const ProfileIdentity install = identity({ "mod install Edain", "modcontent 2 archives 0 files " + std::string(32, 'e') }, {});
	CHECK(mod.text.find("\nmods 1\nmod folder Edain\n") != std::string::npos);
	CHECK(mod == retail); // the same simulation data: a mod that only changes what the game looks like plays with a player without it
	CHECK(renamed == mod);
	CHECK(install == mod);
	CHECK(identity({}, {}, kSim, { "INI.big", "Textures_aio.big" }) == retail); // PROFILE-2's archive lines still never count

	// what still refuses: the sim scripts, the simulation data, an engine feature, the simulation profile
	const ProfileIdentity scripts = identity({ "mod folder Edain", "sdk sim " + std::string(64, '1') }, {});
	const ProfileIdentity otherScripts = identity({ "mod folder Edain", "sdk sim " + std::string(64, '2') }, {});
	CHECK(scripts != retail);
	CHECK(scripts != otherScripts);
	CHECK(identity({ "mod folder Other", "sdk sim " + std::string(64, '1') }, {}) == scripts);
	const ProfileIdentity data = identity({ "mod folder Edain" }, {}, "ffffffffffffffffffffffffffffffff");
	CHECK(data != retail);
	CHECK(ProfileIdentity::compute(archivesNamed({ "INI.big" }), RandomAlgorithm::ZH_CarryChain, { "mod-engine x" }, SimulationProfile::Faithful, {}, kSim)
		!= identity({}, {}, kSim, { "INI.big" }));
	CHECK(ProfileIdentity::compute(archivesNamed({ "INI.big" }), RandomAlgorithm::ZH_CarryChain, {}, SimulationProfile::Uber, {}, kSim)
		!= identity({}, {}, kSim, { "INI.big" }));

	// the refusal names the lines that differ and, as the mod lines no longer count, each side's mod
	const std::string d = ProfileIdentity::difference(data, retail);
	CHECK(d.find("ours: 'simdata ffffffffffffffffffffffffffffffff' theirs: 'simdata " + std::string(kSim) + "'") != std::string::npos);
	CHECK(d.find("mods ours: mod folder Edain; theirs: mods none") != std::string::npos);
	const std::string s = ProfileIdentity::difference(scripts, otherScripts);
	CHECK(s.find("ours: 'sdk sim " + std::string(64, '1') + "'") != std::string::npos);
	CHECK(s.find("mods ours: mod folder Edain; theirs: mod folder Edain") != std::string::npos);
}

TEST_CASE("profile3 formats: a version 3 identity reads as the exact version 2 and version 1 texts of the same mount")
{
	const ProfileIdentity v3 = identity({ "mod folder Edain", "modcontent 1 archives 3 files " + std::string(32, 'c') }, { kPackA });
	const ProfileIdentity v2 = ProfileIdentity::fromText(version2Text(v3));
	REQUIRE(v2.version() == 2);
	// a version 2 text keeps its own digest: every line but the archive lines, the mod lines included
	CHECK(v2.identityText().find("mod folder Edain\n") != std::string::npos);
	CHECK(v2.identityText().find("archive ") == std::string::npos);
	CHECK(ProfileIdentity::inFormatOf(v3, v2).text == v2.text);
	CHECK(ProfileIdentity::inFormatOf(v3, v2) == v2);
	const ProfileIdentity v1 = ProfileIdentity::compute(archivesNamed({ "INI.big", "_patch201.big" }), RandomAlgorithm::ZH_CarryChain, {},
		SimulationProfile::Faithful, { "mod folder Edain", "modcontent 1 archives 3 files " + std::string(32, 'c') });
	REQUIRE(v1.version() == 1);
	CHECK(ProfileIdentity::inFormatOf(v3, v1).text == v1.text);
	CHECK(ProfileIdentity::inFormatOf(v3, v1) == v1);
	CHECK(ProfileIdentity::inFormatOf(v3, v3).text == v3.text);
	CHECK(ProfileIdentity::inFormatOf(v2, v3).text == v2.text); // never upwards
	CHECK(v3 != v2); // a live peer of a PROFILE-2 build is refused (another build anyway)
	// a version 2 recording made without the mod is compared in its own format: the mod lines count there, so it does not play under the mod
	const ProfileIdentity v2NoMod = ProfileIdentity::fromText(version2Text(identity({}, {})));
	CHECK(ProfileIdentity::replayCompatible(v3, v2));
	CHECK(!ProfileIdentity::replayCompatible(v3, v2NoMod));
	CHECK(ProfileIdentity::replayCompatible(identity({}, { kPackB }), v2NoMod)); // the packs are not in a version 2 text at all
	// two version 3 recordings: other packs and another mod name play
	CHECK(ProfileIdentity::replayCompatible(v3, identity({ "mod folder Renamed" }, { kPackB })));
	// "openbfme-profile 3" without a simdata line is not version 3: the whole text counts
	CHECK(ProfileIdentity::fromText("openbfme-profile 3\npack x 1 00\n").version() == 1);
}

TEST_CASE("profile3 saves: a save of a version 2 build loads under a version 3 build with the same data and packs added")
{
	const ProfileIdentity running = identity({}, { kPackA });
	SaveGameHeader save;
	save.profile = ProfileIdentity::fromText(version2Text(identity({}, {})));
	CHECK(SaveGameCompatibility::checkMenuSave(save, running, "").loadable);
	save.profile = ProfileIdentity::fromText(version2Text(identity({}, {}, "ffffffffffffffffffffffffffffffff")));
	CHECK(!SaveGameCompatibility::checkMenuSave(save, running, "").loadable);
	save.profile = identity({}, { kPackB }); // a version 3 save of other packs
	CHECK(SaveGameCompatibility::checkMenuSave(save, running, "").loadable);
}

TEST_CASE("profile3 checker: presentationDifference names the packs, mods and archives the players had")
{
	const ProfileIdentity a = identity({ "mod folder Edain" }, { kPackA, kPackB });
	CHECK(ProfileIdentity::presentationDifference(a, a).empty());
	CHECK(ProfileIdentity::presentationDifference(a, identity({ "mod folder Edain" }, { kPackA, kPackB }, kSim, { "_patch201.big", "INI.big" }))
			.empty()); // the archives sorted: their order is the simdata line's business
	const std::string d = ProfileIdentity::presentationDifference(a, identity({}, { kPackA }, kSim, { "INI.big", "Textures_aio.big" }));
	CHECK(d.find("packs only ours: " + kPackB + "; only theirs: none") != std::string::npos);
	CHECK(d.find("mods only ours: mod folder Edain, mods 1; only theirs: mods none") != std::string::npos);
	CHECK(d.find("archives only ours: rotwk:_patch201.big ") != std::string::npos);
	CHECK(d.find("only theirs: rotwk:Textures_aio.big ") != std::string::npos);
	CHECK(ProfileIdentity::presentationDifference(a, identity({ "mod folder Edain" }, { kPackB, kPackA })) == "packs in another order");
}

TEST_CASE("profile3 simdata: the AI's base layouts and script libraries and a map folder's scripts count; its pictures and strings do not")
{
	// what the read trace of a retail game showed the simulation asks for (tests/test_profile3_retail.cpp)
	CHECK(SimulationData::covers("bases\\ai base - mordor - rush base\\ai base - mordor - rush base.bse"));
	CHECK(SimulationData::covers("bases\\fortress_men\\fortress_men.bse"));
	CHECK(SimulationData::covers("libraries\\multiplayer_human\\multiplayer_human.map"));
	CHECK(SimulationData::covers("maps\\map mp evendim\\scripts.lua"));
	CHECK(SimulationData::covers("maps\\map mp evendim\\scriptevents.xml"));
	CHECK(SimulationData::covers("maps\\map mp evendim\\map mp evendim.map"));
	CHECK(SimulationData::covers("maps\\map mp evendim\\something.new")); // an unknown kind of file in a map folder counts
	CHECK(!SimulationData::covers("maps\\map mp evendim\\map mp evendim_art.tga"));
	CHECK(!SimulationData::covers("maps\\map mp evendim\\map.str"));
	CHECK(!SimulationData::covers("data\\english\\lotr.str"));
	CHECK(!SimulationData::covers("art\\textures\\gumaarms.dds"));
}
