// OpenBFME. GPL-3.0.
// RwGrammar: see RwGrammar.h. Binary facts are recorded per function in tools/rw_object_model.

#include "Common/Thing/RwGrammar.h"

#include "Common/AsciiString.h"
#include "Common/INI/Msvcr71Real.h"

#include <cstdio>
#include <stdexcept>

RwGrammar::RwGrammar(const RwBinaryData &data)
	: m_data(data)
{
	// every function first, so rows can point at their contexts
	for (const auto &kv : m_data.functions)
	{
		m_ctx[kv.first] = std::make_unique<Ctx>(Ctx{ this, kv.first });
	}
	for (const auto &kv : m_data.tables)
	{
		const RwTable &t = kv.second;
		std::vector<FieldParse> rows;
		rows.reserve(t.rows.size() + 1);
		for (const RwRow &r : t.rows)
		{
			const RwFunction &f = m_data.function(r.fn);
			FieldParse fp;
			fp.token = r.name.c_str();
			fp.userData = m_ctx.at(r.fn).get();
			fp.offset = 0;
			switch (f.kind)
			{
				case RwFunction::Line:
					fp.parse = &RwGrammar::parseLine;
					break;
				case RwFunction::Block:
					fp.parse = &RwGrammar::parseBlock;
					break;
				case RwFunction::Script:
					fp.parse = &RwGrammar::parseScript;
					break;
			}
			rows.push_back(fp);
		}
		FieldParse end = { nullptr, nullptr, nullptr, 0 };
		if (t.hasCatchAll)
		{
			if (m_data.function(t.catchAll).kind != RwFunction::Line)
			{
				throw std::runtime_error("RwGrammar: table " + t.id + " has a catch-all row that opens a block (not supported)");
			}
			end.parse = &RwGrammar::parseLine;
		}
		rows.push_back(end);
		m_fieldParse[kv.first] = std::move(rows);
	}
	for (const auto &kv : m_data.functions)
	{
		buildFunction(kv.first);
	}
}

void RwGrammar::buildFunction(unsigned fn)
{
	const RwFunction &f = m_data.function(fn);
	if (f.kind != RwFunction::Block)
	{
		return;
	}
	MultiIniFieldParse multi;
	addTables(f.tables, multi);
	m_multi[fn] = multi;
	if (f.dispatch)
	{
		std::vector<MultiIniFieldParse> per;
		for (const auto &refs : f.dispatchTables)
		{
			MultiIniFieldParse p;
			addTables(refs, p);
			per.push_back(p);
		}
		m_dispatchMulti[fn] = std::move(per);
		std::vector<const char *> names;
		for (const std::string &n : f.dispatchNames)
		{
			names.push_back(n.c_str());
		}
		names.push_back(nullptr);
		m_dispatchNames[fn] = std::move(names);
	}
}

const FieldParse *RwGrammar::fieldParse(const std::string &tableId) const
{
	auto it = m_fieldParse.find(tableId);
	if (it == m_fieldParse.end())
	{
		throw std::runtime_error("RwGrammar: unknown table " + tableId);
	}
	return it->second.data();
}

void RwGrammar::addTables(const std::vector<RwTableRef> &refs, MultiIniFieldParse &p) const
{
	for (const RwTableRef &r : refs)
	{
		// The grammar callbacks never touch their storage pointer, but INI still computes it as instance + offset + extra
		// (INI::initFromINIMulti), and the retail extra offsets (e.g. 324 for AudioLoopUpgrade) point past a 80-byte
		// RawModuleData. Raw grammar tables are therefore added with extra 0; the retail offsets stay in the golden.
		p.add(fieldParse(r.table), 0);
	}
}

void RwGrammar::buildClassFieldParse(const RwClass &cls, MultiIniFieldParse &p) const
{
	addTables(cls.tables, p);
}

void RwGrammar::parseLine(INI *, void *, void *, const void *)
{
}

void RwGrammar::parseBlock(INI *ini, void *instance, void *, const void *userData)
{
	const Ctx *ctx = static_cast<const Ctx *>(userData);
	ctx->grammar->consumeBlock(ini, instance, ctx->fn);
}

void RwGrammar::consumeBlock(INI *ini, void *instance, unsigned fn) const
{
	const RwFunction &f = m_data.function(fn);
	switch (f.opens)
	{
		case RwFunction::Always:
			break;
		case RwFunction::UnlessFloat:
		{
			// RW 0x73B723: getNextToken, then sscanf(token, "%f"); one conversion means a plain value
			const char *token = ini->getNextToken();
			float value = 0.0f;
			if (Msvcr71Real::scanfFloat(token, value)) // MSVCR71's own sscanf("%f") on every OS (Common/INI/Msvcr71Real.h, lane WIN-1)
			{
				return;
			}
			break;
		}
		case RwFunction::FirstTokenIs:
		{
			// RW 0x8B618C: stricmp(firstToken, "override") == 0 runs the block
			const char *token = ini->getNextToken();
			if (AsciiStringUtil::compareNoCase(token, f.openValue) != 0)
			{
				return;
			}
			break;
		}
	}
	if (f.dispatch)
	{
		// RW 0x86C30A: scanIndexList(token, names); the chosen object's table set parses the block
		const char *token = ini->getNextToken();
		const int index = INI::scanIndexList(token, m_dispatchNames.at(fn).data());
		ini->initFromINIMulti(instance, m_dispatchMulti.at(fn).at((size_t)index));
		return;
	}
	ini->initFromINIMulti(instance, m_multi.at(fn));
}

void RwGrammar::parseScript(INI *ini, void *, void *, const void *userData)
{
	const Ctx *ctx = static_cast<const Ctx *>(userData);
	const std::string &terminator = ctx->grammar->m_data.function(ctx->fn).terminator;
	// RW 0x42D400: readLine; strtok the line with the field separators; a line whose first token equals
	// the terminator (stricmp) ends the block, any other line (even an empty one) is part of it.
	for (;;)
	{
		ini->readLine();
		const char *first = ini->firstToken(ini->getSeps());
		if (first && AsciiStringUtil::compareNoCase(first, terminator) == 0)
		{
			return;
		}
		if (ini->isEOF())
		{
			throw INIException(4, "Missing '%s' token.\n\nError parsing block '%s' in file '%s', line %i.\n", terminator.c_str(), ini->getCurBlockStart(), ini->getFilename().c_str(),
				ini->currentSourceLine());
		}
	}
}
