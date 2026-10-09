// OpenBFME retail tests: every W3D draw module declaration of the pure RotWK 2.01 data parses with zero errors (lane DRAW-1,
// checklist step 14). SKIPs when ROTWK_INSTALL / BFME2_INSTALL are unset (shared mount, tests/RetailTestMount.cpp).

#include "doctest.h"

#include "Common/INI.h"
#include "Common/INI/INIBlockStubs.h"
#include "Common/SubsystemLegend.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/RwGrammar.h"
#include "Common/Thing/ThingFactory.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/WeaponStores.h"
#include "RetailTestMount.h"
#include "W3DDrawRetail.h"

#include <cstdio>
#include <cstdlib>

using namespace drawtest;

namespace
{
bool haveRetail()
{
	RetailDrawScan &s = retailDrawScan();
	if (!s.available)
	{
		std::printf("SKIP: draw module retail tests need ROTWK_INSTALL and BFME2_INSTALL (%s)\n", s.mountError.c_str());
		return false;
	}
	REQUIRE_MESSAGE(s.mountError.empty(), s.mountError);
	return true;
}
} // namespace

TEST_CASE("retail: every W3DScriptedModelDraw / W3DHordeModelDraw / W3DDefaultDraw body parses with zero errors")
{
	if (!haveRetail())
	{
		return;
	}
	RetailDrawScan &s = retailDrawScan();
	std::printf("draw scan: %zu modules, %zu macros; load errors %zu\n", s.modules.size(), s.macroCount, s.report.errors.size());
	for (const auto &e : s.report.errors)
	{
		INFO(e.file << ": " << e.message);
		CHECK(false);
	}
	if (const char *dump = std::getenv("OPENBFME_DRAW_DUMP")) // developer aid: object, file, line, class, tag, error
	{
		if (FILE *f = std::fopen(dump, "w"))
		{
			for (const RetailDrawModule &m : s.modules)
			{
				std::fprintf(f, "%s\t%s\t%d\t%s\t%s\t%s\n", m.object.c_str(), m.file.c_str(), m.line, m.className.c_str(), m.tag.c_str(), m.error.c_str());
			}
			std::fclose(f);
		}
	}
	size_t failures = 0;
	for (const RetailDrawModule &m : s.modules)
	{
		if (!m.data && !m.defaultData)
		{
			if (++failures <= 20)
			{
				INFO(m.object << " " << m.file << ":" << m.line << "\n" << m.error);
				CHECK(false);
			}
		}
	}
	CHECK(failures == 0);
}

TEST_CASE("retail: the declaration counts equal the census module histogram")
{
	if (!haveRetail())
	{
		return;
	}
	RetailDrawScan &s = retailDrawScan();
	// workspace/rebuild/census/census.md "Module histogram (objects using each module class, inheritance resolved)":
	// W3DScriptedModelDraw 4241 declarations, W3DHordeModelDraw 120, W3DDefaultDraw 95 (the Objects column counts objects
	// using the class after ChildObject inheritance: 4208 / 182 / 103). The census reads the files the TheThingFactory legend
	// entry names; Data\INI\Crate.ini (TheCrateSystem) also defines Objects, 14 of them with a W3DScriptedModelDraw (the census
	// does not read that file). Nine retail lines spell the header "Draw == W3DScriptedModelDraw" (both '=' are separators); they are
	// counted by both.
	std::map<std::string, int> outsideCrate;
	int crateScripted = 0;
	for (const RetailDrawModule &m : s.modules)
	{
		if (m.file == "Data\\INI\\Crate.ini")
		{
			if (m.className == "W3DScriptedModelDraw")
			{
				++crateScripted;
			}
			continue;
		}
		++outsideCrate[m.className];
	}
	CHECK(crateScripted == 14);
	for (const auto &kv : s.otherDrawCounts)
	{
		std::printf("  other draw class %s: %d\n", kv.first.c_str(), kv.second);
	}
	std::printf("  declarations outside Crate.ini: Scripted %d Horde %d Default %d\n", outsideCrate["W3DScriptedModelDraw"], outsideCrate["W3DHordeModelDraw"],
		outsideCrate["W3DDefaultDraw"]);
	CHECK(outsideCrate["W3DHordeModelDraw"] == 120);
	CHECK(outsideCrate["W3DDefaultDraw"] == 95);
	CHECK(outsideCrate["W3DScriptedModelDraw"] == 4241);
}

TEST_CASE("retail: every scripted / horde module has a state for the empty condition set, and the state counts")
{
	if (!haveRetail())
	{
		return;
	}
	RetailDrawScan &s = retailDrawScan();
	size_t mcs = 0, def = 0, anim = 0, idle = 0, trans = 0, animations = 0, scripts = 0, noDefault = 0, noMatch = 0;
	size_t defaultDraws = 0;
	for (const RetailDrawModule &m : s.modules)
	{
		if (m.defaultData)
		{
			CHECK(m.className == "W3DDefaultDraw");
			++defaultDraws;
			continue;
		}
		if (!m.data)
		{
			continue;
		}
		def += m.data->m_defaultState >= 0 ? 1 : 0;
		mcs += m.data->m_conditionStates.size();
		if (m.data->m_defaultState < 0)
		{
			++noDefault;
		}
		if (!m.data->findBestInfo(ModelConditionFlags()))
		{
			++noMatch;
			// ZH W3DModelDraw's constructor throws "all draw modules must have an IDLE state" for these: none may occur
			CHECK_MESSAGE(false, m.object << " " << m.file << ":" << m.line);
		}
		// every module data starts with the constructor's empty state (RW 0x4C87DD); the census counts the INI's states only
		REQUIRE(!m.data->m_animationStates.empty());
		CHECK(m.data->findBestAnimationState(ModelConditionFlags()) != nullptr);
		for (const AnimationStateInfo &a : m.data->m_animationStates)
		{
			if (a.stateName == "<DefaultEmptyIdleAnimationState>")
			{
				continue;
			}
			animations += a.animations.size();
			scripts += a.beginScript.empty() ? 0 : 1;
			switch (a.kind)
			{
			case AnimationStateInfo::KIND_NORMAL: ++anim; break;
			case AnimationStateInfo::KIND_IDLE: ++idle; break;
			default: ++trans; break;
			}
		}
	}
	CHECK(noMatch == 0);
	CHECK(defaultDraws == 95); // W3DDefaultDraw has no states at all (its own data class)
	std::printf("states: model condition %zu (default %zu, modules without default %zu, without any match for no flags %zu), AnimationState %zu, Idle %zu, Transition %zu; animations %zu; scripts %zu\n",
		mcs, def, noDefault, noMatch, anim, idle, trans, animations, scripts);
}

// The whole retail data through the REAL object parsers with the three draw classes bound to their typed data classes (OBJ-1's
// ModuleFactory::bindTypedData<T>): the factory's data objects are the typed ones and parse with no error.
TEST_CASE("retail: the three draw classes bound through ModuleFactory::bindTypedData load every Object, ChildObject and ObjectReskin")
{
	if (!haveRetail())
	{
		return;
	}
	retailtest::Mount *mount = retailtest::pureMount();
	REQUIRE(mount != nullptr);
	REQUIRE_MESSAGE(mount->fs, mount->error);
	NameKeyGenerator keys;
	keys.init();
	RwGrammar grammar(RwBinaryData::embedded());
	ModuleFactory modules(keys, grammar);
	modules.init();
	modules.bindTypedData<W3DScriptedModelDrawModuleData>("W3DScriptedModelDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DHordeModelDrawModuleData>("W3DHordeModelDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DDefaultDrawModuleData>("W3DDefaultDraw", MODULETYPE_DRAW);
	ThingFactory things(keys, modules, grammar);
	INIEnvironment env;
	INIBlockRecorder recorder;
	SubsystemLegend legend;
	LocomotorStore locomotors;
	LocomotorStore *const savedLocomotors = TheLocomotorStore;
	TheLocomotorStore = &locomotors;
	WeaponStores weaponStores; // WEAPON-1: the Weapon, Armor and DamageFX blocks have real parsers
	weaponStores.install();
	env.fileSystem = mount->fs.get();
	legend.registerBlock(env.blocks);
	things.registerBlocks(env.blocks);
	RegisterRecordingBlockStubs(env.blocks, recorder, { "LoadSubsystem", "Object", "ObjectReskin", "ChildObject" }, StubExtent::Lenient);
	INI ini(env);
	SubsystemLoadOptions options;
	options.collectErrors = true;
	options.cinematics = true;
	SubsystemLoadReport report;
	RunSubsystemIniLoad(legend, ini, options, report);
	TheLocomotorStore = savedLocomotors;
	for (const SubsystemLoadReport::FileError &e : report.errors)
	{
		INFO(e.file << ": " << e.message);
		CHECK(false);
	}
	// every declaration made typed data: count them by dynamic type over the templates' own draw modules
	size_t scripted = 0, horde = 0, defaults = 0, raw = 0;
	const W3DHordeModelDrawModuleData *gondor = nullptr;
	for (const auto &def : things.definitions())
	{
		const ThingTemplate *t = things.findTemplate(def.name);
		if (!t)
		{
			continue;
		}
		for (const ThingTemplate::Nugget &n : t->drawModules().nuggets())
		{
			if (n.name != "W3DScriptedModelDraw" && n.name != "W3DHordeModelDraw" && n.name != "W3DDefaultDraw")
			{
				continue;
			}
			if (dynamic_cast<const W3DHordeModelDrawModuleData *>(n.data.get()))
			{
				++horde;
				if (def.name == "GondorFighter")
				{
					gondor = static_cast<const W3DHordeModelDrawModuleData *>(n.data.get());
				}
			}
			else if (dynamic_cast<const W3DScriptedModelDrawModuleData *>(n.data.get()))
			{
				++scripted;
			}
			else if (dynamic_cast<const W3DDefaultDrawModuleData *>(n.data.get()))
			{
				++defaults;
			}
			else
			{
				++raw;
			}
		}
	}
	std::printf("factory draw modules: scripted %zu horde %zu default %zu raw %zu\n", scripted, horde, defaults, raw);
	CHECK(raw == 0);
	CHECK(scripted > 0);
	CHECK(horde > 0);
	CHECK(defaults > 0);
	// the declaration counts of the factory equal the scan's (Crate.ini included, its 14 scripted draws)
	const auto &counts = modules.declarationCounts();
	CHECK(counts.at("W3DScriptedModelDraw") == 4241 + 14);
	CHECK(counts.at("W3DHordeModelDraw") == 120);
	CHECK(counts.at("W3DDefaultDraw") == 95);
	// the soldier read through the factory equals the one the scan parsed
	REQUIRE(gondor != nullptr);
	CHECK(gondor->m_conditionStates.size() == 3);
	CHECK(gondor->m_lodOptions[2].maxRandomAnimations == 4);
	CHECK(gondor->m_animationStates.front().stateName == "STATE_Idle");
}
