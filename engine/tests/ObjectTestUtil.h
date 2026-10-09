// OpenBFME unit tests. GPL-3.0.
// Fixture for the object model tests: a NameKeyGenerator, the RotWK registry and grammar (embedded
// goldens), a ModuleFactory with the full registry, a ThingFactory, and an INI environment whose
// Object / ChildObject / ObjectReskin blocks are the real parsers.

#pragma once

#include "IniTestUtil.h"

#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/RwGrammar.h"
#include "Common/Thing/ThingFactory.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace objtest
{

struct World
{
	initest::Fixture fx;
	NameKeyGenerator keys;
	RwGrammar grammar;
	ModuleFactory modules;
	ThingFactory things;

	World()
		: grammar(RwBinaryData::embedded())
		, modules(keys, grammar)
		, things(keys, modules, grammar)
	{
		keys.init();
		modules.init();
		things.registerBlocks(fx.env.blocks);
	}

	// Loads `text` as file `name`; returns "" on success, else the INIException message. Retail files are CRLF and
	// the engine's source line numbers count CRs (spec 1.3, "the \r line counter"), so LF in `text` becomes CRLF.
	std::string load(const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, const std::string &name = "obj.ini", int *code = nullptr)
	{
		std::string crlf;
		for (char c : text)
		{
			if (c == '\n')
			{
				crlf += '\r';
			}
			crlf += c;
		}
		return initest::loadError(fx.env, name, crlf, type, code);
	}

	const ThingTemplate *get(const std::string &name) const { return things.findTemplate(name); }
};

typedef std::vector<std::pair<std::string, std::string>> Mods;

// "Class Tag" pairs of a template's modules: behavior, draw, client update, client behavior
inline Mods modulesOf(const ThingTemplate *t)
{
	return t ? t->moduleList() : Mods();
}

inline bool contains(const std::string &haystack, const std::string &needle)
{
	return haystack.find(needle) != std::string::npos;
}

} // namespace objtest
