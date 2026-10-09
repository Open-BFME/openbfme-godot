// OpenBFME retail golden tests for the object model (INI port steps 7-10). GPL-3.0.
//
// Runs only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise it prints SKIP). It mounts pure
// RotWK 2.01 + BFME2 1.06 and runs the whole subsystem INI load (so macros defined by earlier
// subsystems exist) with the REAL Object / ChildObject / ObjectReskin parsers and the recording stubs
// for every other block. Expected values come from outside this code:
//   * the census (workspace/rebuild/census/census.json, independent content counts): 4,634 templates =
//     3,306 Object + 569 ChildObject + 759 ObjectReskin, 245 module classes, per-class declaration counts;
//   * the independent oracle tools/object_oracle/object_oracle.py (own lexer, own structure parse, own
//     inheritance): per-template module lists, committed as engine/tests/data/object_model/.
// A disagreement is reported with its evidence; the oracle is never adjusted to match.

#include "doctest.h"

#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/SubsystemLegend.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/RwGrammar.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/WeaponStores.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "RetailTestMount.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{

struct ObjectWorldRetail
{
	bool available = false;
	std::string mountError;
	NameKeyGenerator keys;
	std::unique_ptr<RwGrammar> grammar;
	std::unique_ptr<ModuleFactory> modules;
	std::unique_ptr<ThingFactory> things;
	INIEnvironment env;
	INIBlockRecorder recorder;
	SubsystemLegend legend;
	LocomotorStore locomotors; // the Locomotor block has a real parser (TheLocomotorStore, lane HORDE-1)
	WeaponStores weaponStores; // WEAPON-1: the Weapon, Armor and DamageFX blocks have real parsers
	std::unique_ptr<INI> ini;
	SubsystemLoadReport report;
	double seconds = 0;

	~ObjectWorldRetail() { TheLocomotorStore = nullptr; }

	ObjectWorldRetail()
	{
		TheLocomotorStore = &locomotors;
		weaponStores.install();
		retailtest::Mount *mount = retailtest::pureMount();
		if (!mount)
		{
			mountError = "ROTWK_INSTALL / BFME2_INSTALL not set";
			return;
		}
		available = true;
		if (!mount->error.empty() || !mount->fs)
		{
			mountError = mount->error.empty() ? "mount failed" : mount->error;
			return;
		}
		keys.init();
		grammar = std::make_unique<RwGrammar>(RwBinaryData::embedded());
		modules = std::make_unique<ModuleFactory>(keys, *grammar);
		modules->init();
		things = std::make_unique<ThingFactory>(keys, *modules, *grammar);
		env.fileSystem = mount->fs.get();
		legend.registerBlock(env.blocks);
		things->registerBlocks(env.blocks);
		// every other block: recording stubs (their parsers belong to other port steps)
		RegisterRecordingBlockStubs(env.blocks, recorder, { "LoadSubsystem", "Object", "ObjectReskin", "ChildObject" }, StubExtent::Lenient);
		ini = std::make_unique<INI>(env);
		SubsystemLoadOptions options;
		options.collectErrors = true;
		options.cinematics = true; // the census counts the Cinematic folder (338 templates)
		const auto t0 = std::chrono::steady_clock::now();
		RunSubsystemIniLoad(legend, *ini, options, report);
		seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	}
};

ObjectWorldRetail &world()
{
	static ObjectWorldRetail w;
	return w;
}

bool haveRetail()
{
	ObjectWorldRetail &w = world();
	if (!w.available)
	{
		std::printf("SKIP: object model retail golden tests need ROTWK_INSTALL and BFME2_INSTALL (%s)\n", w.mountError.c_str());
		return false;
	}
	REQUIRE_MESSAGE(w.mountError.empty(), "retail mount failed:\n" << w.mountError);
	return true;
}

std::string moduleLine(const ThingTemplate *t)
{
	std::string s;
	for (const auto &m : t->moduleList())
	{
		if (!s.empty())
		{
			s += ",";
		}
		s += m.first + ":" + m.second;
	}
	return s;
}

} // namespace

namespace
{
bool isCrate(const ThingFactory::DefinitionRecord &d)
{
	return d.file == "Data\\INI\\Crate.ini";
}

std::vector<std::string> splitTabs(const std::string &line)
{
	std::vector<std::string> out;
	size_t pos = 0;
	for (;;)
	{
		const size_t tab = line.find('\t', pos);
		if (tab == std::string::npos)
		{
			out.push_back(line.substr(pos));
			return out;
		}
		out.push_back(line.substr(pos, tab - pos));
		pos = tab + 1;
	}
}

bool readText(const std::string &name, std::string &out)
{
	std::vector<unsigned char> bytes;
	std::string error;
	if (!retailtest::readLocalFile(retailtest::dataDir() + "/object_model/" + name, bytes, &error))
	{
		out = error;
		return false;
	}
	out.assign(bytes.begin(), bytes.end());
	return true;
}

std::vector<std::string> lines(const std::string &text)
{
	std::vector<std::string> out;
	std::istringstream in(text);
	std::string l;
	while (std::getline(in, l))
	{
		if (!l.empty() && l.back() == '\r')
		{
			l.pop_back();
		}
		out.push_back(l);
	}
	return out;
}
}

TEST_CASE("retail golden: every Object / ChildObject / ObjectReskin loads with zero errors; the thing factory's template count is the census's (4,634)")
{
	if (!haveRetail())
	{
		return;
	}
	ObjectWorldRetail &w = world();
	std::printf("object model retail load: %zu definitions, %zu templates, %zu module data, %.1f s\n", w.things->definitions().size(), w.things->templateCount(),
		w.modules->moduleDataCount(), w.seconds);
	for (const SubsystemLoadReport::FileError &e : w.report.errors)
	{
		MESSAGE("ERROR " << e.file << " (code " << e.code << "): " << e.message);
	}
	CHECK(w.report.errors.empty());
	size_t objects = 0, childObjects = 0, reskins = 0, crate = 0;
	for (const auto &d : w.things->definitions())
	{
		if (isCrate(d))
		{
			++crate;
			continue;
		}
		objects += d.kind == "Object";
		childObjects += d.kind == "ChildObject";
		reskins += d.kind == "ObjectReskin";
	}
	// census.json objects.by_kind: 3,306 Object (DefaultThingTemplate is one of them) + 569 ChildObject + 759 ObjectReskin = 4,634.
	// The census counts the thing factory's own files; Data\INI\Crate.ini, which the crate system loads, defines 23 more
	// (14 Object, 9 ChildObject): the engine loads 4,657. Evidence: tools/object_oracle finds the same 23 there.
	CHECK(objects == 3306);
	CHECK(childObjects == 569);
	CHECK(reskins == 759);
	CHECK(objects + childObjects + reskins == 4634);
	CHECK(crate == 23);
	CHECK(w.things->definitions().size() == 4657);
	CHECK(w.things->templateCount() == 4657); // no duplicate definitions (census: 0)
	CHECK(w.things->findTemplate("DefaultThingTemplate") != nullptr);
	CHECK(w.things->findTemplate("DefaultThingTemplate")->getTemplateID() == 1);
}

TEST_CASE("retail golden: per-template module lists (class, tag) equal the independent oracle's, for all 4,657 templates")
{
	if (!haveRetail())
	{
		return;
	}
	ObjectWorldRetail &w = world();
	std::string text;
	REQUIRE_MESSAGE(readText("rotwk201_template_modules.tsv", text), text);
	std::map<int, std::pair<std::string, std::string>> dictionary;
	struct Row
	{
		std::string kind;
		std::vector<int> ids;
	};
	std::map<std::string, Row> oracle;
	std::vector<std::string> oracleOrder;
	for (const std::string &l : lines(text))
	{
		if (l.empty() || l[0] == '#')
		{
			continue;
		}
		const std::vector<std::string> f = splitTabs(l);
		if (f[0] == "M")
		{
			REQUIRE(f.size() == 4);
			dictionary[std::stoi(f[1])] = { f[2], f[3] };
		}
		else
		{
			REQUIRE(f[0] == "T");
			REQUIRE(f.size() == 4);
			Row r;
			r.kind = f[2];
			std::istringstream ids(f[3]);
			int id;
			while (ids >> id)
			{
				r.ids.push_back(id);
			}
			oracle[f[1]] = r;
			oracleOrder.push_back(f[1]);
		}
	}
	CHECK(oracle.size() == 4657);
	std::map<std::string, std::string> kinds;
	for (const auto &d : w.things->definitions())
	{
		kinds[d.name] = d.kind;
	}
	size_t compared = 0, disagreements = 0, modulesCompared = 0;
	for (const ThingTemplate *t : w.things->templates())
	{
		auto it = oracle.find(t->getName());
		if (it == oracle.end())
		{
			++disagreements;
			MESSAGE("template '" << t->getName() << "' is not in the oracle golden");
			continue;
		}
		std::vector<std::pair<std::string, std::string>> expected;
		for (int id : it->second.ids)
		{
			expected.push_back(dictionary.at(id));
		}
		const auto actual = t->moduleList();
		++compared;
		modulesCompared += expected.size();
		const bool same = actual == expected && it->second.kind == kinds[t->getName()];
		if (!same)
		{
			++disagreements;
			if (disagreements <= 10)
			{
				MESSAGE("DISAGREEMENT " << t->getName() << ": engine has " << actual.size() << " modules, oracle " << expected.size() << " (kind " << kinds[t->getName()] << " vs "
					<< it->second.kind << ")");
			}
		}
	}
	std::printf("object model oracle: %zu templates and %zu module entries compared, %zu disagreements\n", compared, modulesCompared, disagreements);
	CHECK(compared == 4657);
	CHECK(disagreements == 0);
	// the load order is the oracle's too: the engine's master list is in load order
	std::vector<std::string> engineOrder;
	for (const ThingTemplate *t : w.things->templates())
	{
		engineOrder.push_back(t->getName());
	}
	std::set<std::string> engineSet(engineOrder.begin(), engineOrder.end()), oracleSet(oracleOrder.begin(), oracleOrder.end());
	CHECK(engineSet == oracleSet);
}

TEST_CASE("retail golden: the module class histogram equals the oracle's and the census's declaration counts")
{
	if (!haveRetail())
	{
		return;
	}
	ObjectWorldRetail &w = world();
	// engine: objects per class (templates whose final list has the class) and declarations per class
	std::map<std::string, size_t> objects, declarations, declarationsNoCrate;
	for (const ThingTemplate *t : w.things->templates())
	{
		std::set<std::string> classes;
		for (const auto &m : t->moduleList())
		{
			classes.insert(m.first);
		}
		for (const std::string &c : classes)
		{
			++objects[c];
		}
	}
	for (const auto &d : w.things->definitions())
	{
		for (size_t i = d.firstModuleData; i < d.endModuleData; ++i)
		{
			const std::string &c = w.modules->moduleDataClass(i);
			++declarations[c];
			if (!isCrate(d))
			{
				++declarationsNoCrate[c];
			}
		}
	}
	CHECK(w.modules->moduleDataCount() == 22967);

	// the oracle's histogram: class, objects, declarations, census-style objects
	std::string text;
	REQUIRE_MESSAGE(readText("rotwk201_module_histogram.tsv", text), text);
	std::map<std::string, std::vector<size_t>> oracleHist;
	for (const std::string &l : lines(text))
	{
		if (l.empty())
		{
			continue;
		}
		const auto f = splitTabs(l);
		REQUIRE(f.size() == 4);
		oracleHist[f[0]] = { (size_t)std::stoul(f[1]), (size_t)std::stoul(f[2]), (size_t)std::stoul(f[3]) };
	}
	size_t histDiffs = 0;
	for (const auto &kv : oracleHist)
	{
		if (objects[kv.first] != kv.second[0] || declarations[kv.first] != kv.second[1])
		{
			++histDiffs;
			MESSAGE("histogram " << kv.first << ": engine objects/declarations " << objects[kv.first] << "/" << declarations[kv.first] << ", oracle " << kv.second[0] << "/" << kv.second[1]);
		}
	}
	CHECK(histDiffs == 0);
	std::set<std::string> engineClasses;
	for (const auto &kv : objects)
	{
		engineClasses.insert(kv.first);
	}
	std::set<std::string> oracleClasses;
	for (const auto &kv : oracleHist)
	{
		oracleClasses.insert(kv.first);
	}
	CHECK(engineClasses == oracleClasses);

	// the census (census.json objects.module_histogram, 245 classes): the thing factory's files only
	REQUIRE_MESSAGE(readText("census_module_histogram.tsv", text), text);
	std::map<std::string, std::vector<size_t>> census;
	for (const std::string &l : lines(text))
	{
		if (l.empty())
		{
			continue;
		}
		const auto f = splitTabs(l);
		REQUIRE(f.size() == 3);
		census[f[0]] = { (size_t)std::stoul(f[1]), (size_t)std::stoul(f[2]) };
	}
	CHECK(census.size() == 245);
	size_t objectDiffs = 0, censusStyleDiffs = 0;
	std::map<std::string, long> declDiffs;
	for (const auto &kv : census)
	{
		if (declarationsNoCrate[kv.first] != kv.second[1])
		{
			declDiffs[kv.first] = (long)declarationsNoCrate[kv.first] - (long)kv.second[1];
		}
		objectDiffs += objects[kv.first] != kv.second[0];
		censusStyleDiffs += oracleHist.at(kv.first)[2] != kv.second[0];
	}
	// 243 of the 245 classes have exactly the census's declaration count. The census undercounts two: plain `grep` over the
	// object tree finds 1,790 `Body = ActiveBody` lines (engine 1,790, oracle 1,790, census 1,788) and 13 `Draw = W3DSailModelDraw`
	// lines (engine 13, oracle 13, census 11). The census parse is the one that is wrong; its structure heuristics lose two
	// declarations of each class.
	CHECK(declDiffs == std::map<std::string, long>{ { "ActiveBody", 2 }, { "W3DSailModelDraw", 2 } });
	// every class the thing factory's files declare is in the census, and the crate file adds exactly two more
	std::set<std::string> declared;
	for (const auto &kv : declarationsNoCrate)
	{
		declared.insert(kv.first);
	}
	std::set<std::string> censusClasses;
	for (const auto &kv : census)
	{
		censusClasses.insert(kv.first);
	}
	CHECK(declared == censusClasses);
	CHECK(engineClasses.size() == 247);
	CHECK(declarations["RadarMarkerClientUpdate"] == 9);
	CHECK(declarations["VeterancyCrateCollide"] == 2);
	// The census's per-class OBJECT counts follow a simpler inheritance than the engine's (it applies neither
	// DefaultThingTemplate nor the mask / AI clearing: e.g. 840 templates inherit the default InactiveBody, 4,657 inherit its
	// inheritable modules), so the two columns differ by construction. The oracle's census-style recount (same simple rule on the
	// oracle's own parse) reproduces the census for all classes but ActiveBody, where it counts 2,667 against the census's 2,665:
	// not attributable, because the census does not export per-object data.
	std::printf("module histogram: %zu classes; object-count differences vs the census (expected, different inheritance): %zu; census-style recount differences: %zu\n",
		engineClasses.size(), objectDiffs, censusStyleDiffs);
	CHECK(censusStyleDiffs == 1);
}

TEST_CASE("retail golden: the object model reports its acceptance stops and what retail data stored raw")
{
	if (!haveRetail())
	{
		return;
	}
	ObjectWorldRetail &w = world();
	std::set<std::string> ids;
	for (const std::string &s : w.things->acceptanceStops())
	{
		ids.insert(s.substr(0, 5));
	}
	CHECK(ids == std::set<std::string>{ "S-070", "S-071", "S-072", "S-073", "S-074", "S-075", "S-076", "S-077" });
	size_t rawModules = 0, typedModules = 0;
	for (const ThingTemplate *t : w.things->templates())
	{
		for (const ThingTemplate::ModuleInfo *list : { &t->behaviorModules(), &t->drawModules(), &t->clientUpdateModules(), &t->clientBehaviorModules() })
		{
			for (const auto &n : list->nuggets())
			{
				(dynamic_cast<const RawModuleData *>(n.data.get()) ? rawModules : typedModules)++;
			}
		}
	}
	CHECK(typedModules == 0);
	size_t rawFieldLines = 0;
	for (const auto &kv : w.things->rawFieldCounts())
	{
		rawFieldLines += kv.second;
	}
	std::printf("object model stops: %zu module nuggets raw (S-070); %zu object fields stored raw (%zu field names, S-071 / S-072)\n", rawModules, rawFieldLines,
		w.things->rawFieldCounts().size());
	CHECK(w.things->rawFieldCounts().size() == 108);
	CHECK(w.things->rawFieldCounts().count("KindOf") == 1);
	CHECK(rawModules == 96512); // every nugget of every final module list: 22,967 declarations plus the copies ChildObject/Reskin/default inheritance makes
}

TEST_CASE("retail golden: dump of the engine's per-template module lists (OBJ1_DUMP_DIR, for the oracle diff)")
{
	if (!haveRetail())
	{
		return;
	}
	const char *dir = std::getenv("OBJ1_DUMP_DIR");
	if (!dir)
	{
		return;
	}
	ObjectWorldRetail &w = world();
	std::ofstream out(std::string(dir) + "/engine_modules.tsv", std::ios::binary);
	std::map<std::string, std::string> kinds;
	for (const auto &d : w.things->definitions())
	{
		kinds[d.name] = d.kind;
	}
	for (const ThingTemplate *t : w.things->templates())
	{
		out << t->getName() << "\t" << kinds[t->getName()] << "\t" << moduleLine(t) << "\n";
	}
}

TEST_CASE("retail golden: module class histogram dump (OBJ1_DUMP_DIR)")
{
	if (!haveRetail())
	{
		return;
	}
	const char *dir = std::getenv("OBJ1_DUMP_DIR");
	if (!dir)
	{
		return;
	}
	ObjectWorldRetail &w = world();
	std::map<std::string, size_t> objects;
	for (const ThingTemplate *t : w.things->templates())
	{
		std::set<std::string> classes;
		for (const auto &m : t->moduleList())
		{
			classes.insert(m.first);
		}
		for (const std::string &c : classes)
		{
			++objects[c];
		}
	}
	std::ofstream out(std::string(dir) + "/engine_hist.tsv", std::ios::binary);
	std::set<std::string> names;
	for (const auto &kv : objects)
	{
		names.insert(kv.first);
	}
	for (const auto &kv : w.modules->declarationCounts())
	{
		names.insert(kv.first);
	}
	for (const std::string &n : names)
	{
		const size_t o = objects.count(n) ? objects.at(n) : 0;
		const size_t d = w.modules->declarationCounts().count(n) ? w.modules->declarationCounts().at(n) : 0;
		out << n << "\t" << o << "\t" << d << "\n";
	}
}

TEST_CASE("retail golden: definitions by file (OBJ1_DUMP_DIR)")
{
	if (!haveRetail())
	{
		return;
	}
	const char *dir = std::getenv("OBJ1_DUMP_DIR");
	if (!dir)
	{
		return;
	}
	ObjectWorldRetail &w = world();
	std::ofstream out(std::string(dir) + "/engine_definitions.tsv", std::ios::binary);
	for (const auto &d : w.things->definitions())
	{
		out << d.name << "\t" << d.kind << "\t" << d.file << "\t" << d.line << "\n";
	}
}
