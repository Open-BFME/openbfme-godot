// BFME1 field names against the parsers BFME1 data reaches. A typed parser that rejects one of BFME1's fields aborts the file holding it, and BFME1
// stops starting (community PRs #173 and #177). Public CI has no BFME1, so "bfme1 isolation retail" prints SKIP there. This needs only the names, which
// tools/bfme1_fields.py writes to tests/data/bfme1/ini_fields.txt from the Open-BFME-1 decomp.
//
// It checks that a field name is accepted, not that its value or behaviour is right, and does not reach sub-block fields.
#include "doctest.h"

#include "IniTestUtil.h"

#include "GameLogic/Object/RetailObjectWorld.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
const initest::FileList kBfme1Legend = { { "Data\\INI\\Default\\SubsystemLegend.ini", "; BFME1's legend\r\n" } };

struct Entry
{
	std::string keyword; // "Behavior", "Draw", ... for a module; empty for a top-level block
	std::string name;
	std::vector<std::string> fields;
};

std::vector<std::string> dataLines(const std::string &name)
{
	const std::string path = std::string(OPENBFME_TEST_DATA_DIR) + "/bfme1/" + name;
	std::ifstream in(path);
	REQUIRE_MESSAGE(in.good(), path);
	std::vector<std::string> lines;
	for (std::string line; std::getline(in, line);)
	{
		if (!line.empty() && line[0] != '#') lines.push_back(line);
	}
	return lines;
}

std::vector<Entry> readEntries()
{
	std::vector<Entry> entries;
	for (const std::string &line : dataLines("ini_fields.txt"))
	{
		std::istringstream words(line);
		std::string kind;
		Entry e;
		words >> kind;
		if (kind == "module") words >> e.keyword;
		words >> e.name;
		for (std::string field; words >> field;) e.fields.push_back(field);
		entries.push_back(e);
	}
	return entries;
}

std::string probeText(const Entry &e, const std::string &field, int serial)
{
	// these blocks take a name from a fixed set, not a free one
	static const std::map<std::string, std::string> kFixedNames = { { "AudioLOD", "Low" }, { "PredefinedEvaEvent", "DefaultEvaEvent" }, { "Rank", "1" } };
	const auto fixedName = kFixedNames.find(e.name);
	const std::string id = fixedName != kFixedNames.end() ? fixedName->second : "Bfme1FieldProbe" + std::to_string(serial);
	const std::string row = field + " = 1\n";
	if (e.keyword.empty()) return e.name + " " + id + "\n  " + row + "End\n";
	return "Object " + id + "\n  " + e.keyword + " = " + e.name + " ModuleTag_Probe\n    " + row + "  End\nEnd\n";
}

// the parser's error text for this one field, or empty if it loaded
std::string loadOne(RetailObjectWorld &world, const Entry &e, const std::string &field, int serial)
{
	try
	{
		return initest::loadError(world.iniEnvironment(), "bfme1-field-probe.ini", probeText(e, field, serial));
	}
	catch (const std::exception &ex)
	{
		return ex.what();
	}
}

bool isUnknownField(const std::string &error, const std::string &field)
{
	return error.find("Unknown field '" + field + "'") != std::string::npos;
}
}

TEST_CASE("bfme1 fields: BFME1's parsers accept every field in BFME1's tables, apart from the listed gaps")
{
	initest::Fixture fixture(kBfme1Legend);
	RetailObjectWorld world(fixture.fsys);
	const auto context = world.enterContext(); // the Weapon, Locomotor, Upgrade, ... parsers fill the stores of the selected world
	std::set<std::string> gaps, knownUnreached;
	for (const std::string &line : dataLines("known_field_gaps.txt"))
	{
		if (line.rfind("unreached ", 0) == 0) knownUnreached.insert(line.substr(10));
		else gaps.insert(line);
	}

	std::set<std::string> rejected, unreached;
	int serial = 0;
	int strict = 0, lenient = 0;
	for (const Entry &e : readEntries())
	{
		// a name no table holds shows whether the probe reaches a parser that rejects unknown fields at all
		const std::string control = loadOne(world, e, "NoSuchBfme1FieldName", ++serial);
		if (isUnknownField(control, "NoSuchBfme1FieldName")) ++strict;
		else if (control.empty()) ++lenient;
		else
		{
			unreached.insert(e.name);
			if (!knownUnreached.count(e.name)) std::printf("bfme1 fields: probe did not reach %s: %s\n", e.name.c_str(), control.substr(0, 160).c_str());
		}
		for (const std::string &field : e.fields)
		{
			if (isUnknownField(loadOne(world, e, field, ++serial), field)) rejected.insert(e.name + " " + field);
		}
	}
	std::printf("bfme1 fields: %d strict parsers, %d lenient, %zu unreached; %zu fields rejected\n", strict, lenient, unreached.size(), rejected.size());

	for (const std::string &r : rejected)
	{
		CHECK_MESSAGE(gaps.count(r) == 1, "BFME1's parser rejects a field of BFME1's tables, which stops any BFME1 file using it loading. Give BFME1 data that row "
			"(RegisterBfme1FieldRows, or bfme1Grammar in RetailObjectWorld.cpp), or say why not and list it in known_field_gaps.txt: " << r);
	}
	for (const std::string &g : gaps)
	{
		CHECK_MESSAGE(rejected.count(g) == 1, "accepted now, remove it from known_field_gaps.txt: " << g);
	}
	// a probe that stops reaching a parser would pass whatever that parser does
	CHECK(unreached == knownUnreached);
}

TEST_CASE("bfme1 fields: the check sees the two fields that stopped BFME1 starting")
{
	bool gondor = false, pickUp = false;
	for (const Entry &e : readEntries())
	{
		for (const std::string &field : e.fields)
		{
			gondor = gondor || (e.name == "LivingWorldMapInfo" && field == "GondorBanner");
			pickUp = pickUp || (e.name == "AutoPickUpUpdate" && field == "PickUpKindOf");
		}
	}
	CHECK(gondor); // #177
	CHECK(pickUp); // #173
}
