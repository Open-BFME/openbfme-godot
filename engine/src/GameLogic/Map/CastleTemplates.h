// OpenBFME. GPL-3.0.
//
// CastleTemplateStore (lane BUILD-1): the base layouts of the castles, from the `.bse` files of Bases.big (RotWK) / bases.big (BFME2). A CastleBehavior unpacks one
// of them (GameLogic/Module/CastleModules.h).
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; static disassembly):
//   * RW 0x7311FB (a loop over a name list at this + 0x20): for each name N the file "Bases\<N>\<N>.bse" is opened as a map and its CastleTemplates chunk is parsed
//     by the callback RW 0x731010 into the global store (RW 0xDE77A0): per template key a list of entries (position and angle relative to the castle centre) and
//     the polylines. RW 0x799021 gives the key of a castle (from its module data and its owner), RW 0x72D72F reads entry i of a key (false past the last).
//   * the chunk grammar is documented at CastleTemplateEntry in GameClient/MapChunks.h; all 207 retail .bse files parse to the last byte.
// INFERENCE (stop S-300): retail fills the store from a name list at start; the port loads a template when a castle asks for it (same files, same parser), and a missing
// or unparsable file is an error that reaches the report, never a default layout.
//
// Not simulation maths: the store holds parsed data only.

#pragma once

#include "GameClient/MapChunks.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

class ArchiveFileSystem;

class CastleTemplateStore
{
public:
	// reads the template `name`; false + *error on failure
	typedef std::function<bool(const std::string &name, CastleTemplate &out, std::string *error)> Loader;

	void setLoader(Loader loader) { m_loader = std::move(loader); }
	// the loader that reads "Bases\<name>\<name>.bse" from the mounted archives (lowercase file name as in the archives); `fs` must outlive the store
	static Loader fileSystemLoader(ArchiveFileSystem &fs);
	// parses one .bse (a map file) and returns its CastleTemplates chunk(s); false + *error when the file does not parse or has no chunk named `name`
	static bool parseBse(const std::vector<std::uint8_t> &bytes, const std::string &sourceName, const std::string &name, CastleTemplate &out, std::string *error);

	// the template, loaded on first use; null + *error when it cannot be had
	const CastleTemplate *find(const std::string &name, std::string *error);
	// a template made in memory (tests, mods generating layouts)
	void add(CastleTemplate t);
	size_t size() const { return m_templates.size(); }
	const std::vector<std::string> &errors() const { return m_errors; }

private:
	Loader m_loader;
	std::map<std::string, CastleTemplate> m_templates; // by the template name as the INI spells it (case-insensitive compare is done by the key lowercasing)
	std::vector<std::string> m_errors;
};
