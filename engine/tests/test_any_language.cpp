// OpenBFME unit tests. GPL-3.0.
// Installs in any language (RetailArchivePolicy.h "Languages"): the language is detected from lang/*.big, the English
// language archives are swapped for that language's (pinned when known, else mounted with a warning), and the
// language-independent archives are verified as for English.

#include "doctest.h"
#include "BigTestUtil.h"
#include "RetailTestMount.h"

#include "Common/AsciiString.h"
#include "Common/InstallLocator.h"
#include "Common/MD5.h"
#include "Common/RetailArchivePolicy.h"
#include "GameClient/GameText.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

using namespace bigtest;
namespace fs = std::filesystem;

namespace
{

struct LangTempDir
{
	fs::path path;
	LangTempDir()
	{
		std::random_device rd;
		path = fs::temp_directory_path() / ("openbfme_lang_" + std::to_string(rd()) + std::to_string(rd()));
		fs::create_directories(path);
	}
	~LangTempDir()
	{
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

void writeBytes(const fs::path &p, const std::vector<std::uint8_t> &bytes)
{
	fs::create_directories(p.parent_path());
	std::ofstream out(p, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize)bytes.size());
}

RetailArchiveEntry pin(const std::string &path, const std::vector<std::uint8_t> &bytes, const std::string &language)
{
	RetailArchiveEntry e;
	e.path = path;
	e.md5 = MD5::ofBytes(bytes.data(), bytes.size());
	e.size = bytes.size();
	e.language = language;
	return e;
}

std::map<std::string, std::uint64_t> keys(const std::vector<std::string> &paths)
{
	std::map<std::string, std::uint64_t> out;
	for (const std::string &p : paths)
		out[p] = 1;
	return out;
}

const RetailArchiveEntry *find(const RetailArchivePolicy &p, const std::string &path)
{
	for (const RetailArchiveEntry &a : p.archives)
		if (a.path == path)
			return &a;
	return nullptr;
}

std::string readVirtualFile(ArchiveFileSystem &fsys, const std::string &name)
{
	std::vector<std::uint8_t> bytes;
	std::string error;
	if (!fsys.readFile(name, bytes, &error))
		return "<" + error + ">";
	return std::string(bytes.begin(), bytes.end());
}

struct ScopedEnv
{
	std::string name;
	ScopedEnv(const char *n, const char *value) : name(n)
	{
#ifdef _WIN32
		_putenv_s(n, value);
#else
		setenv(n, value, 1);
#endif
	}
	~ScopedEnv()
	{
#ifdef _WIN32
		_putenv_s(name.c_str(), "");
#else
		unsetenv(name.c_str());
#endif
	}
};

} // namespace

TEST_CASE("anylang: the install language is the stem of its single lang/<language>.big")
{
	CHECK(DetectRetailLanguage(keys({ "lang/english.big", "lang/englishaudio.big", "lang/englishpatch201.big" })) == "English");
	CHECK(DetectRetailLanguage(keys({ "lang/german.big", "lang/germanaudio.big", "lang/germanpatch201.big", "ini.big" })) == "German");
	CHECK(DetectRetailLanguage(keys({ "lang/dutch.big", "lang/englishaudio.big", "lang/dutchpatch201.big" })) == "Dutch"); // shares EnglishAudio
	CHECK(DetectRetailLanguage(keys({ "lang/English.big", "lang/german.big" })) == "English"); // English wins: german.big is then unknown
	CHECK(DetectRetailLanguage(keys({ "lang/german.big", "lang/french.big" })).empty());       // ambiguous
	CHECK(DetectRetailLanguage(keys({ "ini.big", "lang/germanaudio.big" })).empty());          // no language's own archive
	CHECK(DetectRetailLanguage(keys({ "lang/sub/german.big" })).empty());
	// the voice language: the stem of the single lang/<language>audio.big
	CHECK(DetectRetailVoiceLanguage(keys({ "lang/german.big", "lang/germanaudio.big", "lang/germanpatch201.big" })) == "German");
	CHECK(DetectRetailVoiceLanguage(keys({ "lang/dutch.big", "lang/englishaudio.big" })) == "English");
	CHECK(DetectRetailVoiceLanguage(keys({ "lang/german.big", "lang/frenchaudio.big" })) == "French");
	CHECK(DetectRetailVoiceLanguage(keys({ "lang/german.big" })).empty());
	CHECK(DetectRetailVoiceLanguage(keys({ "lang/frenchaudio.big", "lang/germanaudio.big" })).empty());
}

TEST_CASE("anylang: localizing the built-in policies")
{
	RetailArchivePolicy rotwk, bfme2;
	std::string error;
	REQUIRE_MESSAGE(RetailArchivePolicy::loadBuiltin("rotwk-201", rotwk, &error), error);
	REQUIRE_MESSAGE(RetailArchivePolicy::loadBuiltin("bfme2-106", bfme2, &error), error);
	std::map<std::string, std::uint64_t> rotwkDisk, bfme2Disk;
	for (const RetailArchiveEntry &a : rotwk.archives)
		if (a.language == "ALL")
			rotwkDisk[AsciiStringUtil::lowered(a.path)] = a.size;
	for (const RetailArchiveEntry &a : bfme2.archives)
		if (a.language == "ALL")
			bfme2Disk[AsciiStringUtil::lowered(a.path)] = a.size;

	SUBCASE("an English folder keeps the policy as it is")
	{
		auto disk = rotwkDisk;
		disk["lang/english.big"] = disk["lang/englishaudio.big"] = disk["lang/englishpatch201.big"] = 1;
		RetailLanguageChoice c;
		const RetailArchivePolicy same = LocalizeRetailPolicy(rotwk, disk, &c);
		CHECK(c.english);
		CHECK(c.language == "English");
		REQUIRE(same.archives.size() == rotwk.archives.size());
		for (size_t i = 0; i < same.archives.size(); ++i)
		{
			CHECK(same.archives[i].path == rotwk.archives[i].path);
			CHECK(same.archives[i].md5 == rotwk.archives[i].md5);
			CHECK(same.archives[i].pinned);
		}
		CHECK(same.excluded == rotwk.excluded);
	}

	SUBCASE("German: the known pins, the English case style and the English mount order")
	{
		auto disk = rotwkDisk;
		disk["lang/german.big"] = disk["lang/germanaudio.big"] = disk["lang/germanpatch201.big"] = 1;
		RetailLanguageChoice c;
		const RetailArchivePolicy de = LocalizeRetailPolicy(rotwk, disk, &c);
		CHECK_FALSE(c.english);
		CHECK(c.language == "German");
		CHECK(c.unpinned.empty());
		REQUIRE(de.archives.size() == rotwk.archives.size());
		CHECK(find(de, "lang/English.big") == nullptr);
		const RetailArchiveEntry *base = find(de, "lang/German.big");
		const RetailArchiveEntry *audio = find(de, "lang/GermanAudio.big");
		const RetailArchiveEntry *patch = find(de, "lang/germanpatch201.big");
		REQUIRE(base);
		REQUIRE(audio);
		REQUIRE(patch);
		CHECK(base->md5 == "dc848c78316743147da8c4faefa13813");
		CHECK(base->size == 4851405);
		CHECK(audio->md5 == "0ab4448d26b969aa745858397035827e");
		CHECK(patch->md5 == "ae6b32eca5888d9069e72df59a714771");
		CHECK(std::find(de.excluded.begin(), de.excluded.end(), "lang/germanpatch202.big") != de.excluded.end());
		// the load order puts each German archive where its English one was
		const auto en = RetailArchiveLoadOrder(rotwk.archives);
		const auto ge = RetailArchiveLoadOrder(de.archives);
		REQUIRE(en.size() == ge.size());
		for (size_t i = 0; i < en.size(); ++i)
		{
			if (en[i].language == "ALL")
				CHECK(ge[i].path == en[i].path);
			else
				CHECK(AsciiStringUtil::lowered(ge[i].path).compare(0, 11, "lang/german") == 0);
		}

		auto bdisk = bfme2Disk;
		bdisk["lang/german.big"] = bdisk["lang/germanaudio.big"] = bdisk["lang/germanpatch105.big"] = 1;
		RetailLanguageChoice bc;
		const RetailArchivePolicy bde = LocalizeRetailPolicy(bfme2, bdisk, &bc);
		CHECK(bc.unpinned.empty());
		REQUIRE(find(bde, "lang/german.big"));
		CHECK(find(bde, "lang/german.big")->md5 == "6b095f659c94f545afaa7e51b8c8e92f");
		REQUIRE(find(bde, "lang/germanpatch105.big"));
		CHECK(find(bde, "lang/germanaudio.big")->size == 557553322);
	}

	SUBCASE("Dutch: pinned from the service's manifest; its shared audio archive stays the pinned English one")
	{
		auto disk = rotwkDisk;
		disk["lang/dutch.big"] = disk["lang/dutchpatch201.big"] = disk["lang/englishaudio.big"] = 1;
		RetailLanguageChoice c;
		const RetailArchivePolicy nl = LocalizeRetailPolicy(rotwk, disk, &c);
		CHECK(c.language == "Dutch");
		CHECK(c.voice == "English");
		CHECK(c.unpinned.empty());
		REQUIRE(find(nl, "lang/EnglishAudio.big"));
		CHECK(find(nl, "lang/EnglishAudio.big")->md5 == "b0b988f245fa7023652d206968132563");
		REQUIRE(find(nl, "lang/Dutch.big"));
		CHECK(find(nl, "lang/Dutch.big")->md5 == "01235aba7f533ed3b7350beed93d0de4");
		CHECK(find(nl, "lang/Dutch.big")->sizeKnown);
	}

	SUBCASE("a language with no pins is unpinned")
	{
		auto disk = rotwkDisk;
		disk["lang/czech.big"] = disk["lang/czechpatch201.big"] = disk["lang/englishaudio.big"] = 1;
		RetailLanguageChoice c;
		const RetailArchivePolicy cs = LocalizeRetailPolicy(rotwk, disk, &c);
		CHECK(c.language == "Czech");
		CHECK(c.unpinned == std::vector<std::string>{ "lang/Czech.big", "lang/czechpatch201.big" });
		REQUIRE(find(cs, "lang/Czech.big"));
		CHECK_FALSE(find(cs, "lang/Czech.big")->pinned);
		CHECK(find(cs, "lang/EnglishAudio.big")->pinned);
	}

	SUBCASE("a pin the service lists with size 0 checks the md5 only (Turkish)")
	{
		auto disk = rotwkDisk;
		disk["lang/turkish.big"] = disk["lang/turkishpatch201.big"] = disk["lang/englishaudio.big"] = 1;
		RetailLanguageChoice c;
		const RetailArchivePolicy tr = LocalizeRetailPolicy(rotwk, disk, &c);
		CHECK(c.unpinned.empty());
		REQUIRE(find(tr, "lang/Turkish.big"));
		CHECK(find(tr, "lang/Turkish.big")->md5 == "393ff5cd0365f288c089790ace39e068");
		CHECK_FALSE(find(tr, "lang/Turkish.big")->sizeKnown);
	}

	SUBCASE("issue #49: one language's text with another's voice")
	{
		auto disk = rotwkDisk;
		disk["lang/german.big"] = disk["lang/germanpatch201.big"] = disk["lang/frenchaudio.big"] = 1;
		RetailLanguageChoice c;
		const RetailArchivePolicy mix = LocalizeRetailPolicy(rotwk, disk, &c);
		CHECK(c.language == "German");
		CHECK(c.voice == "French");
		CHECK(c.unpinned.empty());
		REQUIRE(find(mix, "lang/German.big"));
		REQUIRE(find(mix, "lang/FrenchAudio.big"));
		CHECK(find(mix, "lang/FrenchAudio.big")->md5 == "d38937c37e726f288e022c5d8064e4e3");
		CHECK(find(mix, "lang/GermanAudio.big") == nullptr);

		auto en = rotwkDisk; // English text, German voice: only the audio archive is swapped
		en["lang/english.big"] = en["lang/englishpatch201.big"] = en["lang/germanaudio.big"] = 1;
		RetailLanguageChoice ec;
		const RetailArchivePolicy enDe = LocalizeRetailPolicy(rotwk, en, &ec);
		CHECK_FALSE(ec.english);
		CHECK(ec.language == "English");
		CHECK(ec.voice == "German");
		REQUIRE(find(enDe, "lang/English.big"));
		CHECK(find(enDe, "lang/English.big")->md5 == "3b49cdaf47b25729ff1e162885772662");
		REQUIRE(find(enDe, "lang/GermanAudio.big"));
		CHECK(find(enDe, "lang/GermanAudio.big")->md5 == "0ab4448d26b969aa745858397035827e");
	}
}

TEST_CASE("anylang: mounting a German folder, an unknown language and strict mode")
{
	LangTempDir tmp;
	const fs::path root = tmp.path / "rotwk";
	auto ini = makeBig("BIGF", { { "data\\ini\\weapon.ini", "base" } });
	auto en = makeBig("BIGF", { { "data\\lotr.str", "english" } });
	auto enPatch = makeBig("BIGF", { { "data\\lotr.str", "englishpatch201" } });
	auto de = makeBig("BIGF", { { "lotr.csf", "german" }, { "language.ini", "de" } });
	auto dePatch = makeBig("BIGF", { { "lotr.csf", "germanpatch201" } });
	writeBytes(root / "ini.big", ini);
	writeBytes(root / "lang" / "german.big", de);
	writeBytes(root / "lang" / "germanpatch201.big", dePatch);

	RetailInstall install;
	install.label = "rotwk";
	install.root = root.u8string();
	install.policy.game = "rotwk-test"; // no built-in pins: the German archives are unpinned here
	install.policy.patch = "2.01";
	install.policy.archives = { pin("INI.big", ini, "ALL"), pin("lang/English.big", en, "EN"), pin("lang/englishpatch201.big", enPatch, "EN") };

	SUBCASE("the default mounts them with a warning, the language patch wins")
	{
		Win32BIGFileSystem fsys;
		RetailMountReport r = MountRetailArchives(fsys, { install }, RetailMountOptions());
		REQUIRE_MESSAGE(r.ok, (r.errors.empty() ? std::string() : r.errors[0]));
		CHECK(r.languages == std::vector<std::string>{ "rotwk:German" });
		CHECK(r.mounted.size() == 3);
		CHECK(r.warnings.size() == 2);
		CHECK(r.warnings[0].find("no known md5") != std::string::npos);
		CHECK(readVirtualFile(fsys, "lotr.csf") == "germanpatch201");
		CHECK(readVirtualFile(fsys, "language.ini") == "de");
		for (const MountedArchive &m : r.mounted)
			CHECK(m.md5.size() == 32);
	}

	SUBCASE("OPENBFME_STRICT_LANGUAGE refuses unpinned language archives")
	{
		ScopedEnv strict("OPENBFME_STRICT_LANGUAGE", "1");
		Win32BIGFileSystem fsys;
		RetailMountReport r = MountRetailArchives(fsys, { install }, RetailMountOptions());
		CHECK_FALSE(r.ok);
		CHECK(r.errors.size() == 2);
		CHECK(fsys.getArchiveCount() == 0);
	}

	SUBCASE("a language archive that is not a BIG is still refused")
	{
		std::vector<std::uint8_t> junk = { 'n', 'o', 'p', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		writeBytes(root / "lang" / "germanpatch201.big", junk);
		Win32BIGFileSystem fsys;
		RetailMountReport r = MountRetailArchives(fsys, { install }, RetailMountOptions());
		CHECK_FALSE(r.ok);
		CHECK(fsys.getArchiveCount() == 0);
	}

	SUBCASE("a missing language archive names the language's file; the English archives are untouched")
	{
		fs::remove(root / "lang" / "germanpatch201.big");
		Win32BIGFileSystem fsys;
		RetailMountReport r = MountRetailArchives(fsys, { install }, RetailMountOptions());
		CHECK_FALSE(r.ok);
		REQUIRE(r.errors.size() == 1);
		CHECK(r.errors[0].find("missing archive lang/germanpatch201.big") != std::string::npos);
	}

	SUBCASE("a language-independent archive is still md5-checked")
	{
		auto bad = ini;
		bad.back() ^= 1;
		writeBytes(root / "ini.big", bad);
		Win32BIGFileSystem fsys;
		RetailMountReport r = MountRetailArchives(fsys, { install }, RetailMountOptions());
		CHECK_FALSE(r.ok);
		REQUIRE(r.errors.size() == 1);
		CHECK(r.errors[0].find("md5 mismatch for INI.big") != std::string::npos);
	}
}

TEST_CASE("anylang retail: the installs in ROTWK_INSTALL / BFME2_INSTALL check, mount and read their game text")
{
	const char *rotwkRoot = std::getenv("ROTWK_INSTALL");
	const char *bfme2Root = std::getenv("BFME2_INSTALL");
	if (!rotwkRoot || !*rotwkRoot || !bfme2Root || !*bfme2Root)
	{
		retailtest::printSkip("anylang retail");
		return;
	}
	const std::string language = DetectRetailLanguageOfInstall(rotwkRoot);
	CHECK_FALSE(language.empty());
	CHECK(DetectRetailLanguageOfInstall(bfme2Root) == language);
	MESSAGE("install language: " << language);

	const std::vector<InstallLocator::InstallCheck> checks = InstallLocator::checkPair(rotwkRoot, bfme2Root);
	REQUIRE(checks.size() == 2);
	for (const InstallLocator::InstallCheck &c : checks)
	{
		CHECK_MESSAGE(c.ok, (c.errors.empty() ? std::string() : c.errors[0]));
		CHECK(c.language == language);
	}

	retailtest::Mount *mount = retailtest::pureMount();
	REQUIRE(mount);
	REQUIRE_MESSAGE(mount->error.empty(), mount->error);
	// The text and the voice archives are counted apart: the voice language can differ from the text language (Dutch, Norwegian, Polish and
	// Swedish share EnglishAudio.big; the launcher can download a text and a voice language that differ), so 4 text + 2 voice archives.
	const std::string voice = DetectRetailVoiceLanguageOfInstall(rotwkRoot);
	CHECK_FALSE(voice.empty());
	CHECK(DetectRetailVoiceLanguageOfInstall(bfme2Root) == voice);
	MESSAGE("install voice language: " << voice);
	const std::string textPrefix = ":lang/" + AsciiStringUtil::lowered(language);
	const std::string voiceName = ":lang/" + AsciiStringUtil::lowered(voice) + "audio.big";
	size_t textArchives = 0;
	size_t voiceArchives = 0;
	for (const std::string &a : mount->archives)
	{
		const std::string low = AsciiStringUtil::lowered(a);
		const bool isVoice = low.size() >= voiceName.size() && low.compare(low.size() - voiceName.size(), voiceName.size(), voiceName) == 0;
		if (isVoice)
			++voiceArchives;
		else if (low.find(textPrefix) != std::string::npos && low.find("audio.big") == std::string::npos)
			++textArchives;
	}
	CHECK(textArchives == 4);
	CHECK(voiceArchives == 2);

	GameTextTable text;
	std::string error;
	REQUIRE_MESSAGE(text.loadRetail(*mount->fs, &error), error);
	CHECK(text.size() > 1000); // tolerance: the label count differs per language and patch; a real table has thousands
}

namespace
{

// a CSF string table (" FSC" version 3, one " LBL" with one " RTS" each; the text stored complemented as retail does)
std::string makeCsf(const std::vector<std::pair<std::string, std::u16string>> &labels)
{
	std::string out;
	auto raw = [&](const char *s) { out.append(s, 4); };
	auto word = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
			out.push_back((char)(std::uint8_t)(v >> (8 * i)));
	};
	raw(" FSC");
	word(3);
	word((std::uint32_t)labels.size());
	word((std::uint32_t)labels.size());
	word(0);
	word(0);
	for (const auto &l : labels)
	{
		raw(" LBL");
		word(1);
		word((std::uint32_t)l.first.size());
		out += l.first;
		raw(" RTS");
		word((std::uint32_t)l.second.size());
		for (char16_t c : l.second)
		{
			const auto v = (std::uint16_t)~c;
			out.push_back((char)(v & 255));
			out.push_back((char)(v >> 8));
		}
	}
	return out;
}

} // namespace

// PR #77, owner decision "load with warning": an install whose language no pin covers mounts, and the warning's texts (the message, the details
// "Copy details" copies, the log line) carry the language and file names as text. The names come from the disk and the strings from an unverified
// archive, so a hostile % sequence in them must stay text: none of these paths hands them to a printf-style formatter.
TEST_CASE("anylang: an unknown language mounts with the warning; a hostile % sequence in its names and strings stays text")
{
	const std::string hostile = "%s%s%n%x";
	LangTempDir tmp;
	const fs::path root = tmp.path / "rotwk";
	auto ini = makeBig("BIGF", { { "data\\ini\\weapon.ini", "base" } });
	auto en = makeBig("BIGF", { { "data\\lotr.str", "english" } });
	auto enPatch = makeBig("BIGF", { { "data\\lotr.str", "englishpatch201" } });
	auto text = makeBig("BIGF", { { "lotr.csf", makeCsf({ { "APT:RankLabel", u"%s%s%n%x" }, { "CONTROLBAR:UnderConstructionDesc", u"%n%n%s %p %999999999d" } }) } });
	auto textPatch = makeBig("BIGF", { { "language.ini", "patch" } });
	writeBytes(root / "ini.big", ini);
	writeBytes(root / "lang" / fs::u8path(hostile + ".big"), text);
	writeBytes(root / "lang" / fs::u8path(hostile + "patch201.big"), textPatch);

	RetailInstall install;
	install.label = "rotwk";
	install.root = root.u8string();
	install.policy.game = "rotwk-test";
	install.policy.patch = "2.01";
	install.policy.archives = { pin("INI.big", ini, "ALL"), pin("lang/English.big", en, "EN"), pin("lang/englishpatch201.big", enPatch, "EN") };

	Win32BIGFileSystem fsys;
	RetailMountReport r = MountRetailArchives(fsys, { install }, RetailMountOptions());
	REQUIRE_MESSAGE(r.ok, (r.errors.empty() ? std::string() : r.errors[0]));
	CHECK(r.languages == std::vector<std::string>{ "rotwk:" + hostile });

	// the archives the warning names: the two unpinned language archives, with the md5 and size the mount computed
	REQUIRE(r.unverifiedLanguage.size() == 2);
	std::vector<std::string> paths;
	for (const UnverifiedLanguageArchive &a : r.unverifiedLanguage)
	{
		CHECK(a.install == "rotwk");
		CHECK(a.language == hostile);
		paths.push_back(a.path);
		CHECK(a.md5 == MD5::ofBytes(a.path == "lang/" + hostile + ".big" ? text.data() : textPatch.data(), a.path == "lang/" + hostile + ".big" ? text.size() : textPatch.size()));
		CHECK(a.size == (a.path == "lang/" + hostile + ".big" ? text.size() : textPatch.size()));
	}
	std::sort(paths.begin(), paths.end());
	CHECK(paths == std::vector<std::string>{ "lang/" + hostile + ".big", "lang/" + hostile + "patch201.big" });

	// the message: the language as text, the three facts the owner asked for (crash risk, online play, how to help)
	const std::string message = RetailLanguageWarningText(r.unverifiedLanguage);
	CHECK(message.find("a language we have not checked yet: " + hostile + ".\n") != std::string::npos);
	CHECK(message.find("some text may look wrong") != std::string::npos);
	CHECK(message.find("in rare cases the game can crash") != std::string::npos);
	CHECK(message.find("you can still play with players who have exactly the same language files") != std::string::npos);
	CHECK(message.find("\"Copy details\"") != std::string::npos);
	CHECK(message.find("https://github.com/Open-BFME/openbfme-godot/issues") != std::string::npos);
	CHECK(message.find("#feedback") != std::string::npos);

	// the details: the version line, the language, each file's install, name, size and md5, as text; no folder path
	const std::string details = RetailLanguageWarningDetails(r.unverifiedLanguage, "OpenBFME test %s%n");
	CHECK(details.find("OpenBFME test %s%n\n") != std::string::npos);
	CHECK(details.find("language not checked: " + hostile + "\n") != std::string::npos);
	for (const UnverifiedLanguageArchive &a : r.unverifiedLanguage)
	{
		CHECK(details.find("rotwk " + a.path + " " + std::to_string(a.size) + " bytes md5 " + a.md5 + "\n") != std::string::npos);
	}
	CHECK(details.find(tmp.path.u8string()) == std::string::npos);

	// the log line (RetailMountReport::warnings, printed by the mount): the names as text
	REQUIRE(r.warnings.size() == 2);
	for (const std::string &w : r.warnings)
	{
		CHECK(w.find("rotwk: lang/" + hostile) == 0);
		CHECK(w.find("(" + hostile + ") has no known md5 in this build") != std::string::npos);
	}

	// "Don't show this again" holds for these files: the signature is stable and moves when a file changes
	const std::string signature = RetailLanguageWarningSignature(r.unverifiedLanguage);
	CHECK(signature.size() == 32);
	CHECK(RetailLanguageWarningSignature(r.unverifiedLanguage) == signature);
	std::vector<UnverifiedLanguageArchive> changed = r.unverifiedLanguage;
	changed[0].md5[0] = changed[0].md5[0] == '0' ? '1' : '0';
	CHECK(RetailLanguageWarningSignature(changed) != signature);

	// the unverified archive's strings: the game text reads them as text (its lotr.csf: the install has no data/lotr.str)
	GameTextTable table;
	std::string error;
	REQUIRE_MESSAGE(table.loadRetail(fsys, &error), error);
	std::string label;
	REQUIRE(table.lookup("APT:RankLabel", label));
	CHECK(label == hostile);
	REQUIRE(table.lookup("CONTROLBAR:UnderConstructionDesc", label));
	CHECK(label == "%n%n%s %p %999999999d");
}

TEST_CASE("anylang: the warning's texts replace control characters in names from the disk; a pinned or English install has no warning")
{
	CHECK(RetailPrintableText("Kl\ningon\r\t\x7f") == "Kl?ingon???");
	CHECK(RetailPrintableText("Fran\xc3\xa7" "ais") == "Fran\xc3\xa7" "ais"); // UTF-8 is kept
	std::vector<UnverifiedLanguageArchive> archives = { { "rotwk", "lang/Kl\ningon.big", "Kl\ningon", "0123456789abcdef0123456789abcdef", 12 },
		{ "bfme2", "lang/Elvish.big", "Elvish", "fedcba9876543210fedcba9876543210", 34 } };
	const std::string message = RetailLanguageWarningText(archives);
	CHECK(message.find("not checked yet: Kl?ingon and Elvish.\n") != std::string::npos);
	const std::string details = RetailLanguageWarningDetails(archives, "OpenBFME\nforged line");
	CHECK(details == "OpenBFME language report\nOpenBFME?forged line\nlanguage not checked: Kl?ingon and Elvish\n"
					 "rotwk lang/Kl?ingon.big 12 bytes md5 0123456789abcdef0123456789abcdef\n"
					 "bfme2 lang/Elvish.big 34 bytes md5 fedcba9876543210fedcba9876543210\n");

	// an English folder: the policy as it is, nothing unverified
	LangTempDir tmp;
	const fs::path root = tmp.path / "rotwk";
	auto ini = makeBig("BIGF", { { "data\\ini\\weapon.ini", "base" } });
	auto en = makeBig("BIGF", { { "data\\lotr.str", "english" } });
	writeBytes(root / "ini.big", ini);
	writeBytes(root / "lang" / "English.big", en);
	RetailInstall install;
	install.label = "rotwk";
	install.root = root.u8string();
	install.policy.game = "rotwk-test";
	install.policy.patch = "2.01";
	install.policy.archives = { pin("INI.big", ini, "ALL"), pin("lang/English.big", en, "EN") };
	Win32BIGFileSystem fsys;
	RetailMountReport r = MountRetailArchives(fsys, { install }, RetailMountOptions());
	REQUIRE_MESSAGE(r.ok, (r.errors.empty() ? std::string() : r.errors[0]));
	CHECK(r.unverifiedLanguage.empty());
	CHECK(r.warnings.empty());
}
