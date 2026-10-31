// OpenBFME. GPL-3.0.

#include "Common/RetailArchivePolicy.h"
#include "Common/PathIO.h"
#include "Common/ArchiveFile.h"
#include "Common/AsciiString.h"
#include "Common/JobSystem.h"
#include "Common/Md5Cache.h"
#include "Common/MD5.h"
#include "Common/MiniJson.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

// Generated at build time from engine/data/retail-archives (see engine/CMakeLists.txt).
bool GetEmbeddedDataFile(const std::string &name, std::string &out);

namespace fs = std::filesystem;

namespace
{

std::string slashesToBackslashes(std::string s)
{
	std::replace(s.begin(), s.end(), '/', '\\');
	return s;
}

std::string normalizedKey(std::string s)
{
	std::replace(s.begin(), s.end(), '\\', '/');
	AsciiStringUtil::toLower(s);
	return s;
}

// lane CONTRIB-1: an entry name of a BIG directory as the entry checks compare it (lower case, backslashes: the retail directories' form)
std::string entryKey(std::string s)
{
	std::replace(s.begin(), s.end(), '/', '\\');
	AsciiStringUtil::toLower(s);
	return s;
}

std::uint32_t readBE32(const std::uint8_t *p)
{
	return ((std::uint32_t)p[0] << 24) | ((std::uint32_t)p[1] << 16) | ((std::uint32_t)p[2] << 8) | (std::uint32_t)p[3];
}

} // namespace

bool RetailArchivePolicy::parse(const std::string &policyJson, const std::string &exclusionJson, RetailArchivePolicy &out, std::string *error)
{
	out = RetailArchivePolicy();
	JsonValue root;
	if (!JsonValue::parse(policyJson, root, error))
	{
		return false;
	}
	const JsonValue *schema = root.get("schema");
	if (!schema || !schema->isString() || schema->string != "openbfme.retail-archive-policy")
	{
		if (error)
		{
			*error = "policy JSON is not an openbfme.retail-archive-policy";
		}
		return false;
	}
	const JsonValue *game = root.get("game");
	const JsonValue *patch = root.get("patch");
	const JsonValue *archives = root.get("archives");
	if (!game || !game->isString() || !patch || !patch->isString() || !archives || !archives->isArray())
	{
		if (error)
		{
			*error = "policy JSON lacks game/patch/archives";
		}
		return false;
	}
	out.game = game->string;
	out.patch = patch->string;
	for (const JsonValue &a : archives->array)
	{
		const JsonValue *p = a.get("path");
		const JsonValue *m = a.get("md5");
		const JsonValue *s = a.get("size");
		if (!p || !p->isString() || !m || !m->isString() || !s || !s->isNumber() || s->number < 0)
		{
			if (error)
			{
				*error = "policy archive entry lacks path/md5/size";
			}
			return false;
		}
		RetailArchiveEntry entry;
		entry.path = p->string;
		entry.md5 = AsciiStringUtil::lowered(m->string);
		entry.size = (std::uint64_t)s->number;
		if (const JsonValue *l = a.get("language"))
		{
			if (l->isString())
			{
				entry.language = l->string;
			}
		}
		out.archives.push_back(entry);
	}

	// lane CONTRIB-1: the other known layouts and the entry-checked archives (closed sets: every path must be a policy archive)
	auto policyArchive = [&out](const std::string &path) -> const RetailArchiveEntry * {
		for (const RetailArchiveEntry &a : out.archives)
		{
			if (normalizedKey(a.path) == normalizedKey(path))
			{
				return &a;
			}
		}
		return nullptr;
	};
	auto fail = [error](const std::string &e) {
		if (error)
		{
			*error = "policy JSON: " + e;
		}
		return false;
	};
	auto isMd5 = [](const std::string &m) { return Md5Cache::isMd5Hex(m); };
	if (const JsonValue *layouts = root.get("layouts"))
	{
		const JsonValue *base = layouts->get("base");
		const JsonValue *others = layouts->get("others");
		const JsonValue *advice = layouts->get("advice");
		const JsonValue *baseId = base ? base->get("id") : nullptr;
		const JsonValue *baseName = base ? base->get("name") : nullptr;
		if (!baseId || !baseId->isString() || !baseName || !baseName->isString() || !others || !others->isArray() || !advice || !advice->isString())
		{
			return fail("layouts needs base {id, name}, others [...] and advice");
		}
		out.baseLayoutId = baseId->string;
		out.baseLayoutName = baseName->string;
		out.advice = advice->string;
		std::map<std::string, bool> ids{ { out.baseLayoutId, true } };
		for (const JsonValue &l : others->array)
		{
			const JsonValue *id = l.get("id");
			const JsonValue *name = l.get("name");
			const JsonValue *replace = l.get("replace");
			const JsonValue *absent = l.get("absent");
			if (!id || !id->isString() || !name || !name->isString() || !replace || !replace->isArray() || !absent || !absent->isArray())
			{
				return fail("a layout needs id, name, replace [...] and absent [...]");
			}
			if (ids.count(id->string))
			{
				return fail("layout " + id->string + " is defined twice");
			}
			ids[id->string] = true;
			RetailArchiveLayout layout;
			layout.id = id->string;
			layout.name = name->string;
			std::map<std::string, bool> touched;
			for (const JsonValue &r : replace->array)
			{
				const JsonValue *p = r.get("path");
				const JsonValue *m = r.get("md5");
				const JsonValue *sz = r.get("size");
				if (!p || !p->isString() || !m || !m->isString() || !sz || !sz->isNumber() || sz->number < 0 || !isMd5(AsciiStringUtil::lowered(m->string)))
				{
					return fail("layout " + layout.id + ": a replaced archive needs path, md5 (32 hex) and size");
				}
				if (!policyArchive(p->string) || touched.count(normalizedKey(p->string)))
				{
					return fail("layout " + layout.id + " replaces " + p->string + ", which is not a policy archive or is named twice");
				}
				touched[normalizedKey(p->string)] = true;
				RetailArchiveEntry e = *policyArchive(p->string);
				e.md5 = AsciiStringUtil::lowered(m->string);
				e.size = (std::uint64_t)sz->number;
				layout.replace.push_back(e);
			}
			for (const JsonValue &a : absent->array)
			{
				if (!a.isString() || !policyArchive(a.string) || touched.count(normalizedKey(a.string)))
				{
					return fail("layout " + layout.id + " leaves out an archive that is not a policy archive or is named twice");
				}
				touched[normalizedKey(a.string)] = true;
				layout.absent.push_back(policyArchive(a.string)->path);
			}
			if (touched.empty())
			{
				return fail("layout " + layout.id + " changes nothing");
			}
			out.layouts.push_back(layout);
		}
	}
	if (const JsonValue *checked = root.get("entryChecked"))
	{
		if (!checked->isArray())
		{
			return fail("entryChecked is not an array");
		}
		for (const JsonValue &c : checked->array)
		{
			const JsonValue *p = c.get("path");
			const JsonValue *entries = c.get("entries");
			if (!p || !p->isString() || !policyArchive(p->string) || !entries || !entries->isArray() || entries->array.empty())
			{
				return fail("an entry-checked archive needs the path of a policy archive and its entries");
			}
			if (out.entryCheckOf(p->string))
			{
				return fail(p->string + " is entry-checked twice");
			}
			for (const RetailArchiveLayout &l : out.layouts)
			{
				for (const RetailArchiveEntry &r : l.replace)
				{
					if (normalizedKey(r.path) == normalizedKey(p->string))
					{
						return fail(p->string + " is entry-checked and replaced by layout " + l.id);
					}
				}
			}
			RetailEntryCheckedArchive check;
			check.path = policyArchive(p->string)->path;
			const JsonValue *advice = c.get("advice");
			if (!advice || !advice->isString())
			{
				return fail(check.path + ": an entry-checked archive needs its advice (what the player does when an entry is unknown)");
			}
			check.advice = advice->string;
			for (const JsonValue &e : entries->array)
			{
				const JsonValue *n = e.get("name");
				const JsonValue *m = e.get("md5");
				const JsonValue *sz = e.get("size");
				if (!n || !n->isString() || n->string.empty() || !m || !m->isString() || !isMd5(AsciiStringUtil::lowered(m->string)) || !sz || !sz->isNumber() ||
					sz->number < 0)
				{
					return fail(check.path + ": an entry needs name, md5 (32 hex) and size");
				}
				RetailArchiveContentEntry v;
				v.name = entryKey(n->string);
				v.md5 = AsciiStringUtil::lowered(m->string);
				v.size = (std::uint64_t)sz->number;
				for (const RetailArchiveContentEntry &o : check.versions)
				{
					if (o.name == v.name && o.md5 == v.md5)
					{
						return fail(check.path + ": entry " + v.name + " version " + v.md5 + " is listed twice");
					}
				}
				check.versions.push_back(v);
			}
			out.entryChecked.push_back(check);
		}
	}

	if (!exclusionJson.empty())
	{
		JsonValue ex;
		if (!JsonValue::parse(exclusionJson, ex, error))
		{
			return false;
		}
		const JsonValue *exSchema = ex.get("schema");
		const JsonValue *exArchives = ex.get("archives");
		if (!exSchema || !exSchema->isString() || exSchema->string != "openbfme.retail-archive-exclusions" || !exArchives || !exArchives->isArray())
		{
			if (error)
			{
				*error = "exclusion JSON is not an openbfme.retail-archive-exclusions";
			}
			return false;
		}
		for (const JsonValue &a : exArchives->array)
		{
			const JsonValue *p = a.get("path");
			if (!p || !p->isString())
			{
				if (error)
				{
					*error = "exclusion entry lacks path";
				}
				return false;
			}
			out.excluded.push_back(p->string);
		}
	}
	return true;
}

bool RetailArchivePolicy::loadBuiltin(const std::string &id, RetailArchivePolicy &out, std::string *error)
{
	std::string policy;
	std::string exclusions;
	if (id == "rotwk-201")
	{
		if (!GetEmbeddedDataFile("rotwk-201-english-archives.json", policy) ||
			!GetEmbeddedDataFile("rotwk-201-excluded-archives.json", exclusions))
		{
			if (error)
			{
				*error = "embedded rotwk-201 policy missing from build";
			}
			return false;
		}
	}
	else if (id == "bfme2-106")
	{
		if (!GetEmbeddedDataFile("bfme2-106-english-archives.json", policy) ||
			!GetEmbeddedDataFile("bfme2-106-excluded-archives.json", exclusions))
		{
			if (error)
			{
				*error = "embedded bfme2-106 policy missing from build";
			}
			return false;
		}
	}
	else
	{
		if (error)
		{
			*error = "unknown built-in archive policy " + id;
		}
		return false;
	}
	return parse(policy, exclusions, out, error);
}

namespace
{

// "lang/<stem>.big" (a key: lower case, '/') -> "<stem>", else ""
std::string languageStemOf(const std::string &key)
{
	if (key.size() <= 9 || key.compare(0, 5, "lang/") != 0 || key.compare(key.size() - 4, 4, ".big") != 0)
	{
		return std::string();
	}
	const std::string stem = key.substr(5, key.size() - 9);
	return stem.find('/') == std::string::npos ? stem : std::string();
}

// a language's own archive: lang/<language>.big (not <language>audio.big or <language>patchNNN.big)
bool isBaseLanguageStem(const std::string &stem)
{
	return !stem.empty() && stem.find("patch") == std::string::npos && (stem.size() < 5 || stem.compare(stem.size() - 5, 5, "audio") != 0);
}

// a policy archive that belongs to the English language (lang/English*.big, its "language" not ALL)
bool isLanguageArchive(const RetailArchiveEntry &a)
{
	return !a.language.empty() && a.language != "ALL" && normalizedKey(a.path).compare(0, 12, "lang/english") == 0;
}

// engine/data/retail-archives/language-archives.json: "game|patch|language" (language lower case) -> its archives
const std::map<std::string, std::vector<RetailArchiveEntry>> &knownLanguageArchives()
{
	static const std::map<std::string, std::vector<RetailArchiveEntry>> known = [] {
		std::map<std::string, std::vector<RetailArchiveEntry>> out;
		std::string text;
		JsonValue root;
		std::string error;
		if (!GetEmbeddedDataFile("language-archives.json", text) || !JsonValue::parse(text, root, &error))
		{
			return out;
		}
		const JsonValue *languages = root.get("languages");
		if (!languages || !languages->isArray())
		{
			return out;
		}
		for (const JsonValue &l : languages->array)
		{
			const JsonValue *game = l.get("game");
			const JsonValue *patch = l.get("patch");
			const JsonValue *language = l.get("language");
			const JsonValue *archives = l.get("archives");
			if (!game || !game->isString() || !patch || !patch->isString() || !language || !language->isString() || !archives || !archives->isArray())
			{
				continue;
			}
			std::vector<RetailArchiveEntry> &list = out[game->string + "|" + patch->string + "|" + AsciiStringUtil::lowered(language->string)];
			for (const JsonValue &a : archives->array)
			{
				const JsonValue *p = a.get("path");
				const JsonValue *m = a.get("md5");
				const JsonValue *s = a.get("size");
				if (p && p->isString() && m && m->isString() && Md5Cache::isMd5Hex(AsciiStringUtil::lowered(m->string)) && s && s->isNumber() && s->number >= 0)
				{
					RetailArchiveEntry e;
					e.path = p->string;
					e.md5 = AsciiStringUtil::lowered(m->string);
					e.size = (std::uint64_t)s->number;
					e.sizeKnown = e.size != 0; // the service lists some files with size 0: their md5 is still pinned
					e.language = language->string;
					list.push_back(e);
				}
			}
		}
		return out;
	}();
	return known;
}

} // namespace

namespace
{

// "German" from "german": the retail style of a language's name
std::string languageName(std::string stem)
{
	if (!stem.empty())
	{
		stem[0] = (char)std::toupper((unsigned char)stem[0]);
	}
	return stem;
}

// the stems of lang/<x>.big (`audio` false) or of lang/<x>audio.big (`audio` true)
std::vector<std::string> languageStems(const std::map<std::string, std::uint64_t> &onDiskSizes, bool audio)
{
	std::vector<std::string> stems;
	for (const auto &d : onDiskSizes)
	{
		const std::string stem = languageStemOf(normalizedKey(d.first));
		if (!audio && isBaseLanguageStem(stem))
		{
			stems.push_back(stem);
		}
		else if (audio && stem.size() > 5 && stem.find("patch") == std::string::npos && stem.compare(stem.size() - 5, 5, "audio") == 0)
		{
			stems.push_back(stem.substr(0, stem.size() - 5));
		}
	}
	return stems;
}

// English wins (any other language's archive is then an unknown archive of the English policy), else the single stem, else ""
std::string chooseStem(const std::vector<std::string> &stems)
{
	if (std::find(stems.begin(), stems.end(), "english") != stems.end())
	{
		return "english";
	}
	return stems.size() == 1 ? stems.front() : std::string();
}

} // namespace

std::string DetectRetailLanguage(const std::map<std::string, std::uint64_t> &onDiskSizes)
{
	return languageName(chooseStem(languageStems(onDiskSizes, false)));
}

std::string DetectRetailVoiceLanguage(const std::map<std::string, std::uint64_t> &onDiskSizes)
{
	return languageName(chooseStem(languageStems(onDiskSizes, true)));
}

namespace
{

std::map<std::string, std::uint64_t> languageKeysOfInstall(const std::string &installRoot)
{
	std::map<std::string, std::uint64_t> keys;
	std::error_code ec;
	for (fs::directory_iterator it(PathIO::native(installRoot), ec), end; !ec && it != end; it.increment(ec)) // native wide paths, as the mount
	{
		std::error_code dirError;
		if (!it->is_directory(dirError) || AsciiStringUtil::lowered(it->path().filename().u8string()) != "lang")
		{
			continue;
		}
		std::error_code langError;
		for (fs::directory_iterator f(it->path(), langError), fend; !langError && f != fend; f.increment(langError))
		{
			keys[normalizedKey("lang/" + f->path().filename().u8string())] = 0;
		}
	}
	return keys;
}

} // namespace

std::string DetectRetailLanguageOfInstall(const std::string &installRoot)
{
	return DetectRetailLanguage(languageKeysOfInstall(installRoot));
}

std::string DetectRetailVoiceLanguageOfInstall(const std::string &installRoot)
{
	return DetectRetailVoiceLanguage(languageKeysOfInstall(installRoot));
}

bool RetailStrictLanguage()
{
	const char *v = std::getenv("OPENBFME_STRICT_LANGUAGE");
	return v && *v && std::string(v) != "0";
}

RetailArchivePolicy LocalizeRetailPolicy(const RetailArchivePolicy &policy, const std::map<std::string, std::uint64_t> &onDiskSizes,
	RetailLanguageChoice *choice)
{
	RetailLanguageChoice local;
	RetailLanguageChoice &c = choice ? *choice : local;
	c = RetailLanguageChoice();
	c.language = DetectRetailLanguage(onDiskSizes);
	c.voice = DetectRetailVoiceLanguage(onDiskSizes);
	const std::string text = AsciiStringUtil::lowered(c.language);
	const std::string voice = AsciiStringUtil::lowered(c.voice);
	if ((text.empty() || text == "english") && (voice.empty() || voice == "english"))
	{
		return policy; // the English path: the policy as it is
	}
	c.english = false;
	const auto &table = knownLanguageArchives();
	// "lang/English.big" -> "lang/German.big", "lang/EnglishAudio.big" -> "lang/GermanAudio.big", "lang/englishpatch201.big" ->
	// "lang/germanpatch201.big": the English entry's case style, so BFMEArchiveLoadOrder sorts them as it sorts the English ones
	auto localized = [](const std::string &path, const std::string &stem) {
		const bool capital = path.size() > 5 && path[5] == 'E';
		return path.substr(0, 5) + (capital ? languageName(stem) : stem) + path.substr(5 + 7); // after "lang/english"
	};
	RetailArchivePolicy out = policy;
	for (RetailArchiveEntry &a : out.archives)
	{
		if (!isLanguageArchive(a))
		{
			continue;
		}
		const bool audio = AsciiStringUtil::lowered(a.path).find("audio") != std::string::npos;
		// the speech archive follows the voice language (its own lang/<x>audio.big; none: the text language's), the others the text language
		const std::string stem = audio && !voice.empty() ? voice : text;
		if (stem.empty() || stem == "english")
		{
			continue; // English text (or voice) in a non-English folder: the policy's pinned English archive
		}
		RetailArchiveEntry l;
		l.path = localized(a.path, stem);
		l.language = languageName(stem);
		if (audio && voice.empty() && a.language.find(' ') != std::string::npos && onDiskSizes.count(normalizedKey(a.path)))
		{
			continue; // an archive several languages share (EnglishAudio.big: "EN NL NO PL SV TR"): this language uses the English one, pinned
		}
		const RetailArchiveEntry *pin = nullptr;
		const auto known = table.find(policy.game + "|" + policy.patch + "|" + stem);
		if (known != table.end())
		{
			for (const RetailArchiveEntry &k : known->second)
			{
				if (normalizedKey(k.path) == normalizedKey(l.path))
				{
					pin = &k;
				}
			}
		}
		if (pin)
		{
			l.md5 = pin->md5;
			l.size = pin->size;
			l.sizeKnown = pin->sizeKnown;
		}
		else
		{
			l.pinned = false;
			l.sizeKnown = false;
			c.unpinned.push_back(l.path);
		}
		a = l;
	}
	const size_t excluded = out.excluded.size();
	for (size_t i = 0; i < excluded; ++i)
	{
		if (!text.empty() && text != "english" && normalizedKey(out.excluded[i]).compare(0, 12, "lang/english") == 0)
		{
			out.excluded.push_back(localized(out.excluded[i], text)); // e.g. 2.02's lang/<language>patch202.big
		}
	}
	return out;
}

std::string RetailBaseAptStopLine()
{
	return "S-5302: the BFME2 install's apt\\*.big archives are mounted last although retail RotWK never mounts them (RW 0xA14419): they add "
		   "file names retail does not have and shadow none";
}

std::vector<RetailArchiveEntry> RetailArchiveLoadOrder(const std::vector<RetailArchiveEntry> &archives, bool gameInstall)
{
	// lane MODS-2: RotWK's startup mount, read from the binary (RW 0xA1766D for the game's own folder, RW 0xA14419 for the BFME2 install named
	// by RW 0x638D22), as a first-wins sequence:
	//  * the game folder: loadBigFilesFromDirectory("lang\\", "English*.big", 1) - overwrite 1, so the LAST of them wins: here in descending
	//    order -, then ("", "*.big", 0), then ("apt\\", "*.big", 0);
	//  * the BFME2 install: (bfme2 + "lang\\", "English*.big", 0), then (bfme2, "*.big", 0); its apt\ is never mounted (stop S-5302: the port
	//    keeps those archives last, so they only add files retail does not have).
	// loadBigFilesFromDirectory RW 0xA17FB0 lists one folder (searchSubdirectories 0) into a FilenameList ordered by AsciiString::compareNoCase
	// (RW 0x42C33B -> msvcr71 _memicmp, a shorter name first on a tie) and mounts it in that order; with overwrite 0 the first archive mounted keeps
	// a name (RW 0xA1829C). Before lane MODS-2 the port used BFME1's descending strcmp over the whole install (Win32BIGFileSystem.h): on the
	// pinned 2.01 / 1.06 archives every one of the 44,882 file names resolves to the same archive either way; the orders differ for mods.
	auto lowerName = [](const std::string &path) { return AsciiStringUtil::lowered(path); };
	auto folderOf = [&lowerName](const std::string &path) {
		const std::string l = lowerName(path);
		const size_t slash = l.find('/');
		return slash == std::string::npos ? std::string() : l.substr(0, slash);
	};
	std::vector<RetailArchiveEntry> lang, root, apt, other;
	for (const RetailArchiveEntry &a : archives)
	{
		const std::string folder = folderOf(a.path);
		if (folder.empty())
		{
			root.push_back(a);
		}
		// issue #49: another language's archives (LocalizeRetailPolicy swaps lang/English*.big for lang/<Language>*.big, their `language` not
		// ALL) take the English ones' place: retail's pattern is lang\<Language>*.big, the language from the registry (RW 0x6416AC)
		else if (folder == "lang" && a.path.find('/', 5) == std::string::npos &&
			(lowerName(a.path).compare(5, 7, "english") == 0 || (!a.language.empty() && a.language != "ALL")))
		{
			lang.push_back(a);
		}
		else if (folder == "apt" && a.path.find('/', 4) == std::string::npos)
		{
			apt.push_back(a);
		}
		else
		{
			other.push_back(a); // no retail load reads it (not in a pinned 2.01 / 1.06 policy); kept last so it can only add files
		}
	}
	auto ascending = [](std::vector<RetailArchiveEntry> &v) {
		std::stable_sort(v.begin(), v.end(), [](const RetailArchiveEntry &x, const RetailArchiveEntry &y) {
			return AsciiStringUtil::compareNoCase(x.path, y.path) < 0;
		});
	};
	ascending(lang);
	ascending(root);
	ascending(apt);
	ascending(other);
	if (gameInstall)
	{
		std::reverse(lang.begin(), lang.end()); // overwrite 1: the last one loaded wins
	}
	std::vector<RetailArchiveEntry> ordered;
	for (std::vector<RetailArchiveEntry> *group : { &lang, &root, &apt, &other })
	{
		ordered.insert(ordered.end(), group->begin(), group->end());
	}
	return ordered;
}

std::vector<RetailArchiveEntry> RetailArchivePolicy::archivesOf(const RetailArchiveLayout *layout) const
{
	if (layout == nullptr)
	{
		return archives;
	}
	std::vector<RetailArchiveEntry> out;
	for (const RetailArchiveEntry &a : archives)
	{
		const std::string key = normalizedKey(a.path);
		if (std::any_of(layout->absent.begin(), layout->absent.end(), [&key](const std::string &p) { return normalizedKey(p) == key; }))
		{
			continue;
		}
		auto r = std::find_if(layout->replace.begin(), layout->replace.end(), [&key](const RetailArchiveEntry &e) { return normalizedKey(e.path) == key; });
		out.push_back(r == layout->replace.end() ? a : *r);
	}
	return out;
}

const RetailEntryCheckedArchive *RetailArchivePolicy::entryCheckOf(const std::string &path) const
{
	for (const RetailEntryCheckedArchive &c : entryChecked)
	{
		if (normalizedKey(c.path) == normalizedKey(path))
		{
			return &c;
		}
	}
	return nullptr;
}

RetailLayoutChoice ChooseRetailLayout(const RetailArchivePolicy &policy, const std::string &label, const std::map<std::string, std::uint64_t> &onDiskSizes)
{
	RetailLayoutChoice choice;
	// the archives that differ between layouts, in policy order
	std::vector<std::string> differing;
	for (const RetailArchiveEntry &a : policy.archives)
	{
		const std::string key = normalizedKey(a.path);
		for (const RetailArchiveLayout &l : policy.layouts)
		{
			const bool touched = std::any_of(l.absent.begin(), l.absent.end(), [&key](const std::string &p) { return normalizedKey(p) == key; }) ||
				std::any_of(l.replace.begin(), l.replace.end(), [&key](const RetailArchiveEntry &e) { return normalizedKey(e.path) == key; });
			if (touched)
			{
				differing.push_back(a.path);
				break;
			}
		}
	}
	std::vector<const RetailArchiveLayout *> candidates{ nullptr };
	for (const RetailArchiveLayout &l : policy.layouts)
	{
		candidates.push_back(&l);
	}
	auto nameOf = [&policy](const RetailArchiveLayout *l) { return l ? l->name : policy.baseLayoutName; };
	// does what the folder has for `path` fit layout `l`?
	auto fits = [&policy, &onDiskSizes](const RetailArchiveLayout *l, const std::string &path) {
		const std::vector<RetailArchiveEntry> list = policy.archivesOf(l);
		auto want = std::find_if(list.begin(), list.end(), [&path](const RetailArchiveEntry &e) { return normalizedKey(e.path) == normalizedKey(path); });
		auto have = onDiskSizes.find(normalizedKey(path));
		if (want == list.end())
		{
			return have == onDiskSizes.end();
		}
		return have != onDiskSizes.end() && (policy.entryCheckOf(path) != nullptr || have->second == want->size);
	};
	std::vector<const RetailArchiveLayout *> matching;
	for (const RetailArchiveLayout *l : candidates)
	{
		if (std::all_of(differing.begin(), differing.end(), [&](const std::string &p) { return fits(l, p); }))
		{
			matching.push_back(l);
		}
	}
	if (matching.size() == 1)
	{
		choice.ok = true;
		choice.layout = matching.front();
		choice.layoutId = matching.front() ? matching.front()->id : policy.baseLayoutId;
		choice.layoutName = nameOf(matching.front());
		choice.archives = policy.archivesOf(matching.front());
		return choice;
	}
	// no layout (or, from a policy whose layouts cannot be told apart, several): the archives every layout shares are still checked
	for (const RetailArchiveEntry &a : policy.archives)
	{
		if (std::find(differing.begin(), differing.end(), a.path) == differing.end())
		{
			choice.archives.push_back(a);
		}
	}
	if (matching.size() > 1)
	{
		choice.error = label + ": the archive policy cannot tell its layouts apart (" + nameOf(matching[0]) + "; " + nameOf(matching[1]) + ")";
		return choice;
	}
	// what the folder has, in player words: the files each layout claims, the missing ones (and whose they are), the ones no layout has
	auto listOf = [](const std::vector<std::string> &v) {
		std::string out;
		for (size_t i = 0; i < v.size(); ++i)
		{
			out += std::string(i == 0 ? "" : i + 1 == v.size() ? " and " : ", ") + v[i];
		}
		return out;
	};
	auto verb = [](const std::vector<std::string> &v, const char *one, const char *many) { return std::string(v.size() == 1 ? one : many); };
	std::vector<std::string> parts, unknown;
	std::map<std::string, std::vector<std::string>> missingBy; // whose files they are -> the missing files
	std::vector<std::string> missingOrder;
	for (const RetailArchiveLayout *l : candidates)
	{
		std::vector<std::string> claimed;
		for (const std::string &path : differing)
		{
			const bool present = onDiskSizes.count(normalizedKey(path)) > 0;
			const auto first = std::find_if(candidates.begin(), candidates.end(), [&](const RetailArchiveLayout *c) { return fits(c, path); });
			if (present && first != candidates.end() && *first == l)
			{
				claimed.push_back(path);
			}
		}
		if (!claimed.empty())
		{
			parts.push_back(listOf(claimed) + verb(claimed, " is from ", " are from ") + nameOf(l));
		}
	}
	for (const std::string &path : differing)
	{
		auto have = onDiskSizes.find(normalizedKey(path));
		if (have == onDiskSizes.end())
		{
			std::vector<std::string> owners;
			for (const RetailArchiveLayout *l : candidates)
			{
				const std::vector<RetailArchiveEntry> list = policy.archivesOf(l);
				if (std::any_of(list.begin(), list.end(), [&path](const RetailArchiveEntry &e) { return normalizedKey(e.path) == normalizedKey(path); }))
				{
					owners.push_back(nameOf(l));
				}
			}
			const std::string whose = listOf(owners);
			if (!missingBy.count(whose))
			{
				missingOrder.push_back(whose);
			}
			missingBy[whose].push_back(path);
		}
		else if (std::none_of(candidates.begin(), candidates.end(), [&](const RetailArchiveLayout *c) { return fits(c, path); }))
		{
			unknown.push_back(path + " (" + std::to_string(have->second) + " bytes)");
		}
	}
	for (const std::string &whose : missingOrder)
	{
		const std::vector<std::string> &files = missingBy[whose];
		parts.push_back(listOf(files) + verb(files, " is missing (it belongs to ", " are missing (they belong to ") + whose + ")");
	}
	if (!unknown.empty())
	{
		parts.push_back(listOf(unknown) + verb(unknown, " is no known ", " are no known ") + policy.patch + " file");
	}
	std::string findings;
	for (const std::string &part : parts)
	{
		findings += (findings.empty() ? "" : "; ") + part;
	}
	choice.error = label + ": this folder is not a complete " + policy.patch + " install: " + findings + ". " + policy.advice;
	return choice;
}

std::string CheckArchiveEntries(const RetailEntryCheckedArchive &check, const std::string &label, const std::uint8_t *data, std::uint64_t size)
{
	// the directory read as Win32BIGFileSystem's parseDirectory reads it, every count and range checked before use (hostile input: a file
	// from the player's disk)
	const std::string what = label + ": " + check.path;
	constexpr std::uint32_t kMaxEntries = 4096;
	if (size < 16 || (std::memcmp(data, "BIGF", 4) != 0 && std::memcmp(data, "BIG4", 4) != 0))
	{
		return what + " is not a BIG archive";
	}
	const std::uint32_t count = readBE32(data + 8);
	if (count > kMaxEntries || (std::uint64_t)count * 9 > size - 16)
	{
		return what + " declares " + std::to_string(count) + " entries, more than it can hold";
	}
	std::map<std::string, std::string> seen; // name -> md5 of its first copy
	std::uint64_t hashed = 0;
	std::vector<std::string> problems;
	std::uint64_t pos = 16;
	for (std::uint32_t i = 0; i < count; ++i)
	{
		if (pos + 8 > size)
		{
			return what + ": its directory is cut short at entry " + std::to_string(i);
		}
		const std::uint64_t offset = readBE32(data + pos), length = readBE32(data + pos + 4);
		pos += 8;
		std::string name;
		while (pos < size && data[pos] != 0 && name.size() < 260)
		{
			name.push_back((char)data[pos++]);
		}
		if (pos >= size || data[pos] != 0)
		{
			return what + ": the name of entry " + std::to_string(i) + " is cut short or too long";
		}
		++pos;
		if (offset + length > size)
		{
			return what + ": entry '" + name + "' runs past the end of the archive";
		}
		const std::string key = entryKey(name);
		// aggregate hashing budget (review): entries that overlap would hash the same bytes again and again; an archive whose entries do not overlap
		// hashes at most its own size, so more than that is refused by name
		hashed += length;
		if (hashed > size)
		{
			return what + ": its entries overlap (entry '" + name + "' makes the bytes to check exceed the archive's own " + std::to_string(size) + " bytes)";
		}
		const std::string md5 = MD5::ofBytes(data + offset, (size_t)length);
		auto first = seen.find(key);
		if (first != seen.end())
		{
			if (first->second != md5)
			{
				problems.push_back("'" + key + "' is stored twice with different contents");
			}
			continue;
		}
		seen[key] = md5;
		bool knownName = false, knownVersion = false;
		for (const RetailArchiveContentEntry &v : check.versions)
		{
			if (v.name == key)
			{
				knownName = true;
				knownVersion = knownVersion || (v.size == length && v.md5 == md5);
			}
		}
		if (!knownName)
		{
			problems.push_back("it holds '" + key + "', which no known copy has");
		}
		else if (!knownVersion)
		{
			problems.push_back("its '" + key + "' (" + std::to_string(length) + " bytes, md5 " + md5 + ") is not a version OpenBFME knows");
		}
	}
	for (const RetailArchiveContentEntry &v : check.versions)
	{
		if (!seen.count(v.name))
		{
			problems.push_back("it lacks '" + v.name + "'");
			seen[v.name] = std::string(); // named once
		}
	}
	if (problems.empty())
	{
		return std::string();
	}
	std::string out = what + " differs from every known copy: ";
	for (size_t i = 0; i < problems.size(); ++i)
	{
		out += (i ? "; " : "") + problems[i];
	}
	return out;
}

RetailMountReport MountRetailArchives(Win32BIGFileSystem &fileSystem, const std::vector<RetailInstall> &installs,
	const RetailMountOptions &options)
{
	RetailMountReport report;
	Md5Cache cache;
	cache.path = options.md5CachePath;
	cache.load();

	struct Planned
	{
		std::string install;
		RetailArchiveEntry entry;
		std::string diskPath;
	};
	std::vector<Planned> plan;
	// lane PERF-2: the archives' MD5s are computed on the client job pool. The installs are walked first, recording in order every error and every
	// archive that reached its MD5 check (`steps`); every cache key the sequential pass could hash is hashed ONCE, in parallel (each job writes only its
	// own key's result); then the steps are replayed in order and the replay decides each check as the sequential pass did (a cache hit against the
	// cache as the earlier steps left it, else the key's hash), so the errors, the counters (archivesHashed, md5CacheHits), the cache and the plan are
	// exactly the sequential pass's, duplicate installs included (Sol r1).
	struct Pending
	{
		std::string install;
		RetailArchiveEntry entry;
		std::string diskPath; ///< the found path (the plan's)
		std::string key;      ///< the cache key (absolute path + size + modification time)
		const RetailEntryCheckedArchive *entryCheck = nullptr; ///< lane CONTRIB-1: verified by its entries (no cache, no whole-file pin)
		std::string layoutName;                                ///< the install's layout (player words), for the messages
		std::string advice;
	};
	struct KeyHash
	{
		std::string disk; ///< the absolute path
		std::string md5, error;
	};
	std::vector<KeyHash> hashes;
	std::map<std::string, size_t> hashOfKey;
	struct Step
	{
		std::string error; ///< an error, in its place
		int pending = -1;  ///< or an archive at its MD5 check
	};
	std::vector<Pending> pending;
	std::vector<Step> steps;
	auto stepError = [&steps](std::string e) { steps.push_back(Step{ std::move(e), -1 }); };

	if (installs.empty())
	{
		report.errors.push_back("no installs given");
	}
	std::deque<RetailArchivePolicy> localizedPolicies; // one per install, its language's (stable addresses)

	for (const RetailInstall &install : installs)
	{
		std::string pathError;
		if (!PathIO::checkInstallPath(install.root, pathError))
		{
			stepError(install.label + ": " + pathError);
			continue;
		}
		std::error_code ec;
		fs::path root = PathIO::native(install.root);
		if (install.root.empty() || !fs::is_directory(root, ec))
		{
			stepError(install.label + ": install directory does not exist: '" + install.root + "'");
			continue;
		}

		// Every *.big under the install, keyed by lower-case relative path.
		std::map<std::string, fs::path> onDisk;
		std::map<std::string, std::uint64_t> onDiskSizes; // lane CONTRIB-1: for the layout choice
		for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_regular_file())
			{
				continue;
			}
			std::string ext = AsciiStringUtil::lowered(it->path().extension().u8string());
			if (ext != ".big")
			{
				continue;
			}
			// lane CONTRIB-1: the path below the install as listed (fs::relative resolves symlinks: a symlinked archive got a key outside the install)
			const std::string key = normalizedKey(it->path().lexically_relative(root).u8string());
			onDisk[key] = it->path();
			std::error_code sizeError;
			const std::uintmax_t size = it->file_size(sizeError);
			onDiskSizes[key] = sizeError ? ~(std::uint64_t)0 : (std::uint64_t)size; // an unreadable size fits no layout; its own check names it
		}
		if (ec)
		{
			stepError(install.label + ": could not list " + install.root + ": " + ec.message());
			continue;
		}

		// the install's language (RetailArchivePolicy.h "Languages"): an English folder keeps the policy as it is
		RetailLanguageChoice language;
		// kept until the replay: the pending checks point into it (entryCheck)
		const RetailArchivePolicy &policy = localizedPolicies.emplace_back(LocalizeRetailPolicy(install.policy, onDiskSizes, &language));
		if (!language.language.empty())
		{
			report.languages.push_back(install.label + ":" + language.language +
				(!language.voice.empty() && language.voice != language.language ? " (voice " + language.voice + ")" : std::string()));
		}
		const bool strictLanguage = RetailStrictLanguage();

		std::map<std::string, bool> known;
		for (const RetailArchiveEntry &entry : policy.archives)
		{
			known[normalizedKey(entry.path)] = true;
		}
		for (const std::string &ex : policy.excluded)
		{
			std::string key = normalizedKey(ex);
			if (onDisk.count(key))
			{
				report.excludedFound.push_back(install.label + ":" + ex);
			}
			known[key] = true;
		}
		for (const auto &disk : onDisk)
		{
			if (!known.count(disk.first))
			{
				stepError(install.label + ": unknown archive '" + disk.first +
					"' is not part of " + policy.game + " " + policy.patch + " and not a known exclusion");
			}
		}

		// lane CONTRIB-1: which known layout the folder holds (RetailArchivePolicy.h "Layouts"); none: the error names what the folder has, and the
		// archives every layout shares are still checked so their own problems show too
		const RetailLayoutChoice layout = ChooseRetailLayout(policy, install.label, onDiskSizes);
		if (!layout.ok)
		{
			stepError(layout.error);
		}
		else if (!policy.layouts.empty())
		{
			report.layouts.push_back(install.label + ":" + layout.layoutId);
		}
		const std::string layoutWords = layout.layoutName.empty() ? std::string() : " (" + layout.layoutName + ")";
		const std::string advice = policy.advice.empty() ? std::string() : " " + policy.advice;
		for (const RetailArchiveEntry &entry : RetailArchiveLoadOrder(layout.archives, &install == &installs.front()))
		{
			auto found = onDisk.find(normalizedKey(entry.path));
			if (found == onDisk.end())
			{
				stepError(install.label + ": missing archive " + entry.path + layoutWords);
				continue;
			}
			if (!entry.pinned && strictLanguage)
			{
				stepError(install.label + ": " + entry.path + " is a " + language.language + " language archive with no known md5 in this build, and " +
					"OPENBFME_STRICT_LANGUAGE is set: unset it to mount it unverified");
				continue;
			}
			const RetailEntryCheckedArchive *entryCheck = policy.entryCheckOf(entry.path);
			std::uint64_t size = (std::uint64_t)fs::file_size(found->second, ec);
			if (ec || (entryCheck == nullptr && entry.pinned && entry.sizeKnown && size != entry.size))
			{
				stepError(install.label + ": size mismatch for " + entry.path + ": expected " +
					std::to_string(entry.size) + ", found " + (ec ? ec.message() : std::to_string(size)) + layoutWords +
					": a damaged, modified or other version of the file." + advice);
				continue;
			}
			// md5 is always verified. The cache key is absolute path + size + last-write time, so any
			// change to the file's identity rehashes it; a cached value that disagrees with the
			// policy is rehashed too, so a stale or damaged cache can never refuse (or admit) a file.
			fs::path absolute = fs::absolute(found->second, ec).lexically_normal();
			if (ec)
			{
				stepError(install.label + ": could not resolve the path of " + entry.path + ": " + ec.message());
				continue;
			}
			std::string disk = absolute.u8string();
			long long mtime = (long long)fs::last_write_time(absolute, ec).time_since_epoch().count();
			if (ec)
			{
				stepError(install.label + ": could not read the modification time of " + entry.path + ": " + ec.message());
				continue;
			}
			Pending p;
			p.install = install.label;
			p.entry = entry;
			p.diskPath = found->second.u8string();
			p.key = Md5Cache::key(disk, size, mtime);
			p.entryCheck = entryCheck;
			p.layoutName = layoutWords;
			p.advice = advice;
			if (!entry.pinned || !entry.sizeKnown)
			{
				p.entry.size = size; // the file's own size (no pin)
			}
			if (entryCheck)
			{
				// read and checked entry by entry in the replay below (a small archive: Data1.big is under 1 MB)
				p.entry.size = size;
				steps.push_back(Step{ std::string(), (int)pending.size() });
				pending.push_back(std::move(p));
				continue;
			}
			// the hash this check may need: none when the cache (as the earlier checks leave it) surely vouches for it; a key already scheduled is
			// hashed once (its result is what the sequential pass would compute again: the key fixes path, size and modification time)
			if (!hashOfKey.count(p.key))
			{
				auto cached = cache.entries.find(p.key);
				// an unpinned language archive: any cached md5 of the same path + size + modification time is its md5
				if (cached == cache.entries.end() || (entry.pinned && cached->second != entry.md5))
				{
					hashOfKey[p.key] = hashes.size();
					hashes.push_back(KeyHash{ disk, std::string(), std::string() });
				}
			}
			steps.push_back(Step{ std::string(), (int)pending.size() });
			pending.push_back(std::move(p));
		}
	}

	// the hashes (MD5::ofFile reads its whole archive: the client pool reads and hashes several at once)
	JobSystem::client().parallelFor(hashes.size(), 1, [&hashes](size_t, size_t begin, size_t end) {
		for (size_t i = begin; i < end; ++i)
		{
			hashes[i].md5 = MD5::ofFile(hashes[i].disk, &hashes[i].error);
		}
	});

	for (const Step &step : steps)
	{
		if (step.pending < 0)
		{
			report.errors.push_back(step.error);
			continue;
		}
		const Pending &p = pending[(size_t)step.pending];
		if (p.entryCheck)
		{
			// lane CONTRIB-1: the whole file (size checked against a budget before the allocation), its md5 (the mount's identity of it) and its entries
			constexpr std::uint64_t kMaxEntryChecked = 64ull << 20;
			if (p.entry.size > kMaxEntryChecked)
			{
				report.errors.push_back(p.install + ": " + p.entry.path + " is " + std::to_string(p.entry.size) + " bytes, far larger than any known copy" + p.layoutName +
					"." + p.advice);
				continue;
			}
			std::vector<std::uint8_t> bytes((size_t)p.entry.size);
			std::ifstream in(PathIO::native(p.diskPath), std::ios::binary);
			if (!in || !in.read(reinterpret_cast<char *>(bytes.data()), (std::streamsize)bytes.size()) || in.peek() != std::char_traits<char>::eof())
			{
				report.errors.push_back(p.install + ": could not read " + p.entry.path + " (" + p.diskPath + ")");
				continue;
			}
			++report.archivesHashed;
			const std::string problem = CheckArchiveEntries(*p.entryCheck, p.install, bytes.data(), bytes.size());
			if (!problem.empty())
			{
				report.errors.push_back(problem + ". " + p.entryCheck->advice);
				continue;
			}
			RetailArchiveEntry verified = p.entry;
			verified.md5 = MD5::ofBytes(bytes.data(), bytes.size());
			plan.push_back({ p.install, verified, p.diskPath });
			continue;
		}
		std::string md5;
		bool fromCache = false;
		auto cached = cache.entries.find(p.key);
		if (cached != cache.entries.end() && (cached->second == p.entry.md5 || !p.entry.pinned))
		{
			md5 = cached->second;
			fromCache = true;
		}
		else
		{
			const KeyHash &h = hashes[hashOfKey.at(p.key)]; // scheduled above: this check is not a sure cache hit
			if (h.md5.empty())
			{
				report.errors.push_back(p.install + ": " + h.error);
				continue;
			}
			md5 = h.md5;
			++report.archivesHashed;
			if (cached == cache.entries.end() || cached->second != md5)
			{
				cache.entries[p.key] = md5;
				cache.dirty = true;
			}
		}
		if (!p.entry.pinned)
		{
			// a language archive this build has no md5 for: present, opened as a valid BIG below (the transactional open), mounted with its own md5
			RetailArchiveEntry unpinned = p.entry;
			unpinned.md5 = md5;
			report.warnings.push_back(p.install + ": " + RetailPrintableText(p.entry.path) + " (" + RetailPrintableText(p.entry.language) +
				") has no known md5 in this build; it is mounted unverified (md5 " + md5 + ", " + std::to_string(p.entry.size) +
				" bytes). Set OPENBFME_STRICT_LANGUAGE=1 to refuse it.");
			if (fromCache)
			{
				++report.md5CacheHits;
			}
			plan.push_back({ p.install, unpinned, p.diskPath });
			continue;
		}
		if (md5 != p.entry.md5)
		{
			report.errors.push_back(p.install + ": md5 mismatch for " + p.entry.path + ": expected " + p.entry.md5 + ", found " + md5 + p.layoutName +
				": a damaged, modified or other version of the file." + p.advice);
			continue;
		}
		if (fromCache)
		{
			++report.md5CacheHits;
		}
		plan.push_back({ p.install, p.entry, p.diskPath });
	}

	{
		std::string warning = cache.save();
		if (!warning.empty())
		{
			report.warnings.push_back(warning);
		}
	}

	if (!report.errors.empty())
	{
		return report; // fail closed: nothing mounted
	}

	// Transactional: open and validate every archive first, mount only when all of them opened.
	std::vector<std::unique_ptr<ArchiveFile>> opened;
	for (const Planned &p : plan)
	{
		std::string error;
		std::unique_ptr<ArchiveFile> archive = fileSystem.openArchiveFile(p.diskPath, &error);
		if (!archive)
		{
			report.errors.push_back(p.install + ": " + error);
			continue;
		}
		opened.push_back(std::move(archive));
	}
	if (!report.errors.empty())
	{
		return report; // nothing has been mounted yet
	}
	for (size_t i = 0; i < plan.size(); ++i)
	{
		fileSystem.mountArchive(std::move(opened[i]), plan[i].diskPath, false);
		report.mounted.push_back({ plan[i].install, plan[i].entry.path, plan[i].diskPath, plan[i].entry.size, plan[i].entry.md5 });
		if (!plan[i].entry.pinned)
		{
			report.unverifiedLanguage.push_back({ plan[i].install, plan[i].entry.path, plan[i].entry.language, plan[i].entry.md5, plan[i].entry.size });
		}
		// lane MODS-2: a base install's (not the first install's) apt\ archive: retail never mounts it (stop S-5302)
		if (report.stops.empty() && !installs.empty() && plan[i].install != installs.front().label &&
			AsciiStringUtil::lowered(plan[i].entry.path).compare(0, 4, "apt/") == 0)
		{
			report.stops.push_back(RetailBaseAptStopLine());
		}
	}
	report.ok = true;
	return report;
}

std::string RetailPrintableText(const std::string &text)
{
	std::string out = text;
	for (char &c : out)
	{
		const unsigned char u = (unsigned char)c;
		if (u < 0x20 || u == 0x7F)
		{
			c = '?';
		}
	}
	return out;
}

namespace
{

// "Klingon", "Klingon and Elvish", "Klingon, Elvish and Entish": each language once, in the archives' order
std::string unverifiedLanguageNames(const std::vector<UnverifiedLanguageArchive> &archives)
{
	std::vector<std::string> names;
	for (const UnverifiedLanguageArchive &a : archives)
	{
		const std::string name = RetailPrintableText(a.language);
		if (std::find(names.begin(), names.end(), name) == names.end())
		{
			names.push_back(name);
		}
	}
	std::string out;
	for (size_t i = 0; i < names.size(); ++i)
	{
		out += (i == 0 ? std::string() : (i + 1 == names.size() ? std::string(" and ") : std::string(", "))) + names[i];
	}
	return out;
}

std::string unverifiedArchiveLine(const UnverifiedLanguageArchive &a)
{
	return a.install + " " + RetailPrintableText(a.path) + " " + std::to_string(a.size) + " bytes md5 " + a.md5;
}

} // namespace

std::string RetailLanguageWarningText(const std::vector<UnverifiedLanguageArchive> &archives)
{
	// plain concatenation: the language names come from file names on disk
	return "OpenBFME recognised your copies of The Rise of the Witch-king 2.01 and The Battle for Middle-earth II 1.06, but they are in a "
		   "language we have not checked yet: " + unverifiedLanguageNames(archives) + ".\n\n"
		   "The game starts as usual. Because these language files have not been checked:\n"
		   "  - some text may look wrong or be missing;\n"
		   "  - in rare cases the game can crash.\n\n"
		   "Online and LAN games: you can still play with players who have exactly the same language files. Players whose files differ (an "
		   "English install, for example) cannot join your games and you cannot join theirs; the game tells you so when you try. Replays and "
		   "saved games work the same way.\n\n"
		   "You can help us check your language: press \"Copy details\" and post them in the OpenBFME Discord, channel #feedback, or in a new "
		   "issue at https://github.com/Open-BFME/openbfme-godot/issues. The details hold only the language, the file names, their sizes and "
		   "checksums.";
}

std::string RetailLanguageWarningDetails(const std::vector<UnverifiedLanguageArchive> &archives, const std::string &versionLine)
{
	std::string out = "OpenBFME language report\n" + RetailPrintableText(versionLine) + "\nlanguage not checked: " + unverifiedLanguageNames(archives) + "\n";
	for (const UnverifiedLanguageArchive &a : archives)
	{
		out += unverifiedArchiveLine(a) + "\n";
	}
	return out;
}

std::string RetailLanguageWarningSignature(const std::vector<UnverifiedLanguageArchive> &archives)
{
	std::string lines;
	for (const UnverifiedLanguageArchive &a : archives)
	{
		lines += unverifiedArchiveLine(a) + "\n";
	}
	return MD5::ofBytes(lines.data(), lines.size());
}

bool ResolveInstallPath(const std::string &key, const std::string &configPath, std::string &out, std::string *error)
{
	const std::string env = PathIO::environment(key.c_str());
	if (!env.empty())
	{
		out = env;
		return true;
	}
	if (!configPath.empty())
	{
		std::ifstream in(PathIO::native(configPath));
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			size_t eq = line.find('=');
			if (eq == std::string::npos || line[0] == '#' || line[0] == ';')
			{
				continue;
			}
			std::string k = line.substr(0, eq);
			std::string v = line.substr(eq + 1);
			const auto first = v.find_first_not_of(" \t");
			v = first == std::string::npos ? std::string() : v.substr(first);
			while (!k.empty() && (k.back() == ' ' || k.back() == '\t'))
			{
				k.pop_back();
			}
			if (k == key && !v.empty())
			{
				out = v;
				return true;
			}
		}
	}
	if (error)
	{
		*error = key + " is not set (environment variable" + (configPath.empty() ? std::string("") : ", or " + configPath) + ")";
	}
	return false;
}
