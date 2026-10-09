// OpenBFME unit tests (lane MODULES-1): the module coverage of the runtime registry.
//
// The registry is built the way RetailObjectWorld builds it (ModuleFactory::init, the typed draw data, the horde contain data, LogicModules::registerAll).
// Each of the binary's 329 classes is then one of:
//   * ported: a runtime class is bound (ModuleFactory::bindModuleProc);
//   * base AI: an AI module class bound to the base AIUpdateInterface runtime, its own behaviour not ported (stop S-222);
//   * parsed only: the data is typed (its fields are parsed) but the runtime is the explicit UnportedModule (stop S-140);
//   * missing: raw data and the UnportedModule.
// The counts are pinned here, so a lane that ports a class moves them on purpose. With OPENBFME_MODULE_COVERAGE_OUT set, the test also writes the per-class
// table that tools/rw_object_model/module_coverage.py joins with the census into workspace/rebuild/specs/module-coverage.md.

#include "doctest.h"

#include "ObjectTestUtil.h"

#include "GameClient/MapHordeSpawn.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameLogic/Module/LogicModules.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <typeinfo>

namespace
{
struct CoverageWorld : objtest::World
{
	CoverageWorld()
	{
		W3DDrawModules::registerTypedDrawModuleData(modules);
		MapHordeSpawn::bindHordeContainData(modules);
		LogicModules::registerAll(modules);
	}
};

const char *const kTypeNames[] = { "Behavior", "Draw", "ClientUpdate", "ClientBehavior" };

struct Counts
{
	int ported = 0, baseAi = 0, parsedOnly = 0, missing = 0;
};

std::string statusOf(const ModuleFactory::ModuleTemplate &mt, const std::type_info &baseAiProc)
{
	if (mt.portedModule)
	{
		// every AI class without a runtime of its own shares the lambda of AIUpdateInterface::registerClasses; HordeAIUpdate shares it but is ported there
		if (mt.isAiModuleData && mt.name != "AIUpdateInterface" && mt.name != "HordeAIUpdate" && mt.newModule.target_type() == baseAiProc)
		{
			return "base-ai";
		}
		return "ported";
	}
	return mt.typed ? "parsed-only" : "missing";
}
} // namespace

TEST_CASE("module coverage: every registry class is ported, base AI, parsed only or missing; the counts are pinned (S-140)")
{
	CoverageWorld w;
	const ModuleFactory::ModuleTemplate *ai = w.modules.findModuleTemplate("AIUpdateInterface", MODULETYPE_BEHAVIOR);
	REQUIRE(ai);
	REQUIRE(ai->portedModule);
	const std::type_info &baseAiProc = ai->newModule.target_type();

	Counts c;
	std::string table = "class\ttype\tstatus\ttyped\tai\tdrawkind\n";
	// the registry golden's class order
	std::vector<std::pair<std::string, int>> classes;
	for (const RwClass &cls : RwBinaryData::embedded().classes)
	{
		classes.emplace_back(cls.name, cls.type);
	}
	int seen = 0;
	for (const auto &cls : classes)
	{
		const ModuleFactory::ModuleTemplate *mt = w.modules.findModuleTemplate(cls.first, (ModuleType)cls.second);
		REQUIRE(mt);
		if (table.find("\n" + cls.first + "\t" + kTypeNames[cls.second] + "\t") != std::string::npos) // a class listed once per (name, type)
		{
			continue; // WeaponBonusUpgrade: two sites, one class
		}
		++seen;
		const std::string status = statusOf(*mt, baseAiProc);
		// a draw class is drawn by the W3D draw runtime (GameClient), not by a bound module proc: its kind (W3DDrawKind) goes into the table
		const W3DDrawClassInfo *draw = cls.second == MODULETYPE_DRAW ? W3DDrawModules::find(cls.first) : nullptr;
		const std::string drawKind = draw ? std::to_string((int)draw->kind) : "-";
		c.ported += status == "ported";
		c.baseAi += status == "base-ai";
		c.parsedOnly += status == "parsed-only";
		c.missing += status == "missing";
		table += cls.first + "\t" + kTypeNames[cls.second] + "\t" + status + "\t" + (mt->typed ? "1" : "0") + "\t" + (mt->isAiModuleData ? "1" : "0") + "\t" + drawKind + "\n";
	}
	CHECK(seen == 329);
	CHECK(c.ported + c.baseAi + c.parsedOnly + c.missing == 329);
	CHECK((int)w.modules.portedModuleCount() == c.ported + c.baseAi);
	MESSAGE("module coverage: ported " << c.ported << ", base AI " << c.baseAi << ", parsed only " << c.parsedOnly << ", missing " << c.missing);
	// the pins: move them when a class is ported
	// before MODULES-1: ported 68, base AI 9, parsed only 13, missing 239; MODULES-1: 80 / 227; SPELL-2 (the ten spell book power classes and
	// FireWeaponUpdate): +11; lane PHYS-1: PhysicsBehavior (RW's dormant fling flight, S-782): +1 -> 92 / 215; STEALTH-1: InvisibilityUpdate,
	// StealthDetectorUpdate, StealthUpdate: +3 -> 95 / 212; MODULES-2 (EmotionTrackerUpdate, RadiateFearUpdate, LargeGroupBonusUpdate,
	// PassiveAreaEffectBehavior, HitReactionBehavior): +5 -> 100 / 207; GARRISON-1 (GarrisonContain, HordeGarrisonContain): +2; MODULES-3
	// (NotifyTargetsOfImminentProbableCrushingUpdate and its horde variant): +2 -> 104 / 203; STEALTH-2 (ToggleHiddenSpecialAbilityUpdate,
	// InvisibilitySpecialPower): +2 -> 106 / 201; HERO-2 (SpecialDisguiseUpdate, TemporarilyDefectUpdate,
	// DominateEnemySpecialPower, ActivateModuleSpecialPower, ArrowStormUpdate, CurseSpecialPower, FellBeastSwoopPower, RousingSpeechUpdate,
	// TeleportSpecialAbilityUpdate, SpecialPowerTimerRefreshSpecialPower): +10 -> 116 / 191; HERO-2 (ShareExperienceBehavior,
	// DamageFilteredCreateObjectDie): +2 -> 118 / 189; HERO-2 AutoAbilityBehavior: +1 -> 119 / 188; HERO-2 WeaponModeSpecialPowerUpdate,
	// DualWeaponBehavior: +2 -> 121 / 186; GARRISON-2 (TransportContain, HordeTransportContain, SiegeEngineContain, HordeSiegeEngineContain,
	// TunnelContain): +5 -> 126 / 181; MOD-4 (AISpecialPowerUpdate, RepairSpecialPower, SpawnBehavior, SlavedUpdate): +4 -> 130 / 177;
	// AUDIO-4 (LargeGroupAudioUpdate, AnimationSoundClientBehavior): +2 -> 132 / 175; CAMP-1 (AttachUpdate): +1 -> 133 / 174
	CHECK(c.ported == 133);
	CHECK(c.baseAi == 9);
	CHECK(c.parsedOnly == 13);
	CHECK(c.missing == 174);

	if (const char *out = std::getenv("OPENBFME_MODULE_COVERAGE_OUT"))
	{
		std::ofstream f(out, std::ios::binary);
		REQUIRE(f);
		f << table;
	}
}
