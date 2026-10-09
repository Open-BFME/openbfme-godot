// OpenBFME. GPL-3.0.
//
// RwGrammar: FieldParse tables built from the RotWK grammar data (RwFieldTables.h) that CONSUME
// exactly what the binary's parse functions consume, without interpreting it. It is the extent
// machinery for module bodies and nested blocks whose typed parser is not ported: the INI core
// (INI::initFromINIMulti) does the work, so unknown fields, End handling and error texts are the
// core's; this class only supplies the rows.
//
//   line rows    do nothing (the rest of the line is dropped, as the INI loop does after any parser)
//   block rows   run INI::initFromINIMulti over the function's tables (the nested block up to its End);
//                blocks opened only for some first tokens (conditional) and blocks whose first token
//                selects the table (dispatch) are handled the way the reviewed grammar says
//   script rows  read lines until the terminator (RW 0x42D400 reads until a line whose first token is
//                ENDSCRIPT, case-insensitively; RW never leaves the loop at end of file, this port
//                reports "Missing 'ENDSCRIPT' token" instead: stop S-076)
//
// Acceptance stop S-074: the grammar (which functions open blocks and with which tables) was derived
// by static analysis of the binary and is reviewed for the 5 conditional / dispatch / null-guarded
// functions; a block opened only through an indirect call would be classified as a line. The retail
// corpus golden is the check.

#pragma once

#include "Common/INI.h"
#include "Common/Thing/RwFieldTables.h"

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

class RwGrammar
{
public:
	explicit RwGrammar(const RwBinaryData &data);
	RwGrammar(const RwGrammar &) = delete;
	RwGrammar &operator=(const RwGrammar &) = delete;

	const RwBinaryData &data() const { return m_data; }

	// The FieldParse row array of a table (terminated; a catch-all row when the binary has one).
	const FieldParse *fieldParse(const std::string &tableId) const;

	// Adds the given tables (with their extra offsets) to p.
	void addTables(const std::vector<RwTableRef> &refs, MultiIniFieldParse &p) const;

	// A module class's body tables.
	void buildClassFieldParse(const RwClass &cls, MultiIniFieldParse &p) const;

	// FieldParse procs for the three consuming kinds; `userData` of a row is a Ctx owned by the grammar.
	// Object-level raw blocks call this directly: consumes the rest of the field line and the block.
	void consumeBlock(INI *ini, void *instance, unsigned fn) const;

	struct Ctx
	{
		const RwGrammar *grammar;
		unsigned fn;
	};

private:
	void buildFunction(unsigned fn);
	static void parseLine(INI *ini, void *instance, void *store, const void *userData);
	static void parseBlock(INI *ini, void *instance, void *store, const void *userData);
	static void parseScript(INI *ini, void *instance, void *store, const void *userData);

	const RwBinaryData &m_data;
	std::map<std::string, std::vector<FieldParse>> m_fieldParse;
	std::map<unsigned, std::unique_ptr<Ctx>> m_ctx;
	std::map<unsigned, MultiIniFieldParse> m_multi;                       ///< per block function
	std::map<unsigned, std::vector<MultiIniFieldParse>> m_dispatchMulti;  ///< per dispatching function
	std::map<unsigned, std::vector<const char *>> m_dispatchNames;
};
