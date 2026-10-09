// OpenBFME. GPL-3.0.
//
// The RotWK binary's module registry and INI field-table grammar, loaded from the embedded goldens
// engine/data/rotwk-201/module-registry.json and field-tables.json (generated from game.dat by
// tools/rw_object_model/extract.py; see there for the binary facts).
//
// These are data about the ENGINE, not about retail content: the registry lists every module class
// the binary registers (329, with their interface masks), whether or not any retail object uses it,
// and the tables are the ones the binary parses with (PLAN rule 6).
//
// Table rows are {token, parse function, userData, offset} exactly as in the binary. A parse
// function is identified by its RotWK virtual address, kept as the key of "functions": what a
// function consumes (one line, a nested block, a BeginScript body) was worked out from its code.

#pragma once

#include <map>
#include <string>
#include <vector>

struct RwTableRef
{
	std::string table; ///< key into RwFieldTables::tables ("0xda3df8")
	unsigned extra = 0; ///< MultiIniFieldParse extra offset (0x124 for the audio table)
};

struct RwRow
{
	std::string name;
	unsigned fn = 0; ///< RotWK VA of the parse function
	unsigned userData = 0;
	int offset = 0;
	std::vector<std::string> names; ///< resolved name list when userData points at one
};

struct RwTable
{
	std::string id;
	std::vector<RwRow> rows;
	bool terminated = true; ///< false: the binary's table has no NULL row (the next object follows)
	bool hasCatchAll = false;
	unsigned catchAll = 0; ///< fn of the NULL-token row with a parse function (any field name)
};

// What a parse function consumes after the field name.
struct RwFunction
{
	enum Kind { Line, Block, Script };
	enum Opens { Always, UnlessFloat, FirstTokenIs };
	Kind kind = Line;
	std::vector<RwTableRef> tables; ///< a block: the tables its lines are parsed with
	Opens opens = Always;           ///< a block opened only for some first tokens (reviewed by hand)
	std::string openValue;
	bool nullGuarded = false;       ///< reviewed: the only early return is an instance NULL test
	std::string terminator;         ///< a script: the line that ends it (first token, case-insensitive)
	bool dispatch = false;          ///< a block whose first token selects the table set
	std::vector<std::string> dispatchNames;
	std::vector<std::vector<RwTableRef>> dispatchTables;
	std::string semantic;           ///< object-table functions: the engine's semantic ("parseReal", "raw", ...)
};

struct RwClass
{
	std::string name;
	int type = 0;
	unsigned mask = 0;
	bool isAiModuleData = false;
	bool slot7 = false;
	std::vector<RwTableRef> tables;
	std::vector<std::string> sites; ///< addModule call sites (more than one: registered twice)
};

class RwBinaryData
{
public:
	// The embedded goldens. Throws std::runtime_error when missing or malformed (no fallback).
	static const RwBinaryData &embedded();
	// For tests: parse the two JSON texts.
	static RwBinaryData parse(const std::string &moduleRegistryJson, const std::string &fieldTablesJson);

	std::vector<RwClass> classes;
	unsigned addModuleSites = 0;
	// every addModule call in the order the binary executes them: (class name, module type). Retail's NAMEKEY ids follow it.
	std::vector<std::pair<std::string, int>> registrationSequence;
	std::map<std::string, RwTable> tables;
	std::map<unsigned, RwFunction> functions;
	RwTableRef objectTable;
	RwTableRef audioTable;

	const RwTable &table(const std::string &id) const;
	const RwFunction &function(unsigned fn) const;
};
