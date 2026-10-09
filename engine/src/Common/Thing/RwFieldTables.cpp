// OpenBFME. GPL-3.0.
// Loader for the embedded RotWK registry / field-table goldens. See RwFieldTables.h.

#include "Common/Thing/RwFieldTables.h"

#include "Common/MiniJson.h"

#include <cstdlib>
#include <stdexcept>

bool GetEmbeddedDataFile(const std::string &name, std::string &out);

namespace
{
unsigned parseHex(const std::string &s)
{
	if (s.size() < 3 || s[0] != '0' || s[1] != 'x')
	{
		throw std::runtime_error("RwFieldTables: not a hex address: '" + s + "'");
	}
	return (unsigned)std::strtoul(s.c_str() + 2, nullptr, 16);
}

const JsonValue &need(const JsonValue &obj, const char *key)
{
	const JsonValue *v = obj.get(key);
	if (!v)
	{
		throw std::runtime_error(std::string("RwFieldTables: missing key '") + key + "'");
	}
	return *v;
}

unsigned number(const JsonValue &v)
{
	if (!v.isNumber() || v.number < 0 || v.number > 4294967295.0)
	{
		throw std::runtime_error("RwFieldTables: expected an unsigned number");
	}
	return (unsigned)v.number;
}

std::vector<RwTableRef> refs(const JsonValue &arr)
{
	std::vector<RwTableRef> out;
	for (const JsonValue &r : arr.array)
	{
		RwTableRef ref;
		ref.table = need(r, "table").string;
		ref.extra = number(need(r, "extra"));
		out.push_back(ref);
	}
	return out;
}

JsonValue parseJson(const std::string &text, const char *what)
{
	JsonValue v;
	std::string error;
	if (!JsonValue::parse(text, v, &error))
	{
		throw std::runtime_error(std::string("RwFieldTables: ") + what + ": " + error);
	}
	return v;
}
}

RwBinaryData RwBinaryData::parse(const std::string &moduleRegistryJson, const std::string &fieldTablesJson)
{
	RwBinaryData d;
	const JsonValue reg = parseJson(moduleRegistryJson, "module-registry.json");
	d.addModuleSites = number(need(reg, "addModuleSites"));
	for (const JsonValue &q : need(reg, "registrationSequence").array)
	{
		d.registrationSequence.emplace_back(need(q, "name").string, (int)number(need(q, "type")));
	}
	for (const JsonValue &c : need(reg, "classes").array)
	{
		RwClass k;
		k.name = need(c, "name").string;
		k.type = (int)number(need(c, "type"));
		k.mask = number(need(c, "mask"));
		k.isAiModuleData = need(c, "isAiModuleData").boolean;
		k.slot7 = need(c, "slot7").boolean;
		k.tables = refs(need(c, "tables"));
		for (const JsonValue &s : need(c, "sites").array)
		{
			k.sites.push_back(s.string);
		}
		d.classes.push_back(std::move(k));
	}

	const JsonValue ft = parseJson(fieldTablesJson, "field-tables.json");
	d.objectTable.table = need(need(ft, "objectTable"), "table").string;
	d.objectTable.extra = number(need(need(ft, "objectTable"), "extra"));
	d.audioTable.table = need(need(ft, "audioTable"), "table").string;
	d.audioTable.extra = number(need(need(ft, "audioTable"), "extra"));
	const JsonValue &semantics = need(ft, "objectFunctions");
	for (const auto &kv : need(ft, "tables").object)
	{
		RwTable t;
		t.id = kv.first;
		t.terminated = need(kv.second, "terminated").boolean;
		if (const JsonValue *ca = kv.second.get("catchAll"))
		{
			t.hasCatchAll = true;
			t.catchAll = parseHex(ca->string);
		}
		for (const JsonValue &r : need(kv.second, "rows").array)
		{
			RwRow row;
			row.name = need(r, "name").string;
			row.fn = parseHex(need(r, "fn").string);
			row.userData = number(need(r, "userData"));
			row.offset = (int)need(r, "offset").number;
			if (const JsonValue *names = r.get("names"))
			{
				for (const JsonValue &n : names->array)
				{
					row.names.push_back(n.string);
				}
			}
			t.rows.push_back(std::move(row));
		}
		d.tables[kv.first] = std::move(t);
	}
	for (const auto &kv : need(ft, "functions").object)
	{
		RwFunction f;
		const std::string &kind = need(kv.second, "kind").string;
		if (kind == "line")
		{
			f.kind = RwFunction::Line;
		}
		else if (kind == "block")
		{
			f.kind = RwFunction::Block;
			f.tables = refs(need(kv.second, "tables"));
			if (kv.second.get("nullGuarded"))
			{
				f.nullGuarded = true;
			}
			if (const JsonValue *c = kv.second.get("conditional"))
			{
				const std::string &opens = need(*c, "opens").string;
				if (opens == "unlessFloat")
				{
					f.opens = RwFunction::UnlessFloat;
				}
				else if (opens == "firstTokenIs")
				{
					f.opens = RwFunction::FirstTokenIs;
					f.openValue = need(*c, "value").string;
				}
				else
				{
					throw std::runtime_error("RwFieldTables: unknown conditional opener '" + opens + "'");
				}
			}
			if (const JsonValue *dsp = kv.second.get("dispatch"))
			{
				f.dispatch = true;
				for (const JsonValue &n : need(*dsp, "names").array)
				{
					f.dispatchNames.push_back(n.string);
				}
				for (const JsonValue &per : need(*dsp, "tables").array)
				{
					f.dispatchTables.push_back(refs(per));
				}
			}
		}
		else if (kind == "script")
		{
			f.kind = RwFunction::Script;
			f.terminator = need(kv.second, "terminator").string;
		}
		else
		{
			throw std::runtime_error("RwFieldTables: unknown function kind '" + kind + "'");
		}
		const unsigned va = parseHex(kv.first);
		if (const JsonValue *sem = semantics.get(kv.first))
		{
			f.semantic = need(*sem, "semantic").string;
		}
		d.functions[va] = std::move(f);
	}
	// object-table functions that are only "line" rows still need their semantic recorded
	for (const auto &kv : semantics.object)
	{
		const unsigned va = parseHex(kv.first);
		d.functions[va].semantic = need(kv.second, "semantic").string;
	}
	return d;
}

const RwBinaryData &RwBinaryData::embedded()
{
	static const RwBinaryData data = [] {
		std::string reg, ft;
		if (!GetEmbeddedDataFile("module-registry.json", reg) || !GetEmbeddedDataFile("field-tables.json", ft))
		{
			throw std::runtime_error("RwBinaryData: the RotWK registry goldens are missing from this build");
		}
		return RwBinaryData::parse(reg, ft);
	}();
	return data;
}

const RwTable &RwBinaryData::table(const std::string &id) const
{
	auto it = tables.find(id);
	if (it == tables.end())
	{
		throw std::runtime_error("RwBinaryData: unknown table " + id);
	}
	return it->second;
}

const RwFunction &RwBinaryData::function(unsigned fn) const
{
	auto it = functions.find(fn);
	if (it == functions.end())
	{
		throw std::runtime_error("RwBinaryData: unknown parse function");
	}
	return it->second;
}
