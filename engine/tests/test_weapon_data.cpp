// OpenBFME unit tests: the Weapon block, WeaponStore, nuggets, WeaponBonusSet and the object-level WeaponSet (GameLogic/Weapon.h,
// WeaponNugget.h, WeaponSet.h). GPL-3.0. Lane WEAPON-1.
// Expected values: the FieldParse tables, constructor default maps and name lists dumped from game.dat into tests/data/weapon1/*.tsv by
// tools/weapon/dump_tables.py (field names, parser addresses, userData and offsets only), the INI conversions of PLAN rule 2, and the
// independent retail line scan tools/weapon/scan_retail_blocks.py.

#include "doctest.h"
#include "IniTestUtil.h"
#include "ObjectTestUtil.h"
#include "RetailTestMount.h"

#include "Common/GameCommon.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Armor.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/DamageFX.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ThingCombatSets.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponSet.h"
#include "GameLogic/WeaponStores.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

using namespace initest;

namespace
{
// ---- golden TSV ---------------------------------------------------------------------------------
typedef std::vector<std::string> Row;

std::vector<Row> readTsv(const std::string &name, bool header = false)
{
	std::vector<unsigned char> bytes;
	std::string error;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/weapon1/" + name, bytes, &error), error);
	std::vector<Row> rows;
	std::string line;
	bool first = true;
	for (size_t i = 0; i <= bytes.size(); ++i)
	{
		if (i == bytes.size() || bytes[i] == '\n')
		{
			if (!line.empty())
			{
				if (!(header && first))
				{
					Row r;
					std::string cell;
					for (char c : line)
					{
						if (c == '\t')
						{
							r.push_back(cell);
							cell.clear();
						}
						else
						{
							cell.push_back(c);
						}
					}
					r.push_back(cell);
					rows.push_back(r);
				}
				first = false;
			}
			line.clear();
		}
		else
		{
			line.push_back((char)bytes[i]);
		}
	}
	return rows;
}

std::uint32_t hex(const std::string &s)
{
	return (std::uint32_t)std::stoul(s, nullptr, 16);
}

std::vector<std::string> readList(const std::string &name)
{
	std::vector<std::string> out;
	for (const Row &r : readTsv(name))
	{
		out.push_back(r[1]);
	}
	return out;
}

std::vector<std::string> namesOf(const char *const *list)
{
	std::vector<std::string> out;
	for (; *list; ++list)
	{
		out.push_back(*list);
	}
	return out;
}

// RW parse function address of each parser the Weapon and nugget tables use (identified by disassembly, see the headers); a pointer
// may stand for more than one binary function (parseBitString32 / the AntiCategories copy).
std::map<INIFieldParseProc, std::set<std::uint32_t>> rwParserAddresses()
{
	std::map<INIFieldParseProc, std::set<std::uint32_t>> m;
	m[INI::parseReal] = { 0x42ed00 };
	m[INI::parseBool] = { 0x42e558 };
	m[INI::parseInt] = { 0x42ec5e };
	m[INI::parseAngleReal] = { 0x42ee15 };
	m[INI::parseDurationUnsignedInt] = { 0x73a429 };
	m[INI::parseVelocityReal] = { 0x73a4b6 };
	m[INI::parsePercentToReal] = { 0x42eefa };
	m[INI::parseIndexList] = { 0x42e956 };
	m[INI::parseBitString32] = { 0x42e840, 0x89f32d };
	m[INI::parseBitInInt32] = { 0x42e574 };
	m[INI::parseAsciiString] = { 0x42ee5e };
	m[INI::parseAsciiStringVector] = { 0x42eed6 };
	m[INI::parseCoord3D] = { 0x42f247 };
	m[INI::parseLookupList] = { 0x42e9b7 };
	m[WeaponTemplate::parseFireSound] = { 0x73b217 };
	m[WeaponTemplate::parseFireFX] = { 0x6c9cdc };
	m[WeaponTemplate::parseFireFlankFX] = { 0x73a302 };
	m[WeaponTemplate::parseProjectileExhaust] = { 0x6c9d46 };
	m[WeaponTemplate::parseVeterancyFireFX] = { 0x6c9c9a };
	m[WeaponTemplate::parseVeterancyProjectileExhaust] = { 0x6c9d04 };
	m[WeaponTemplate::parseProjectileFilter] = { 0x76392f };
	m[WeaponTemplate::parseMaxAttackPassengers] = { 0x42ec5e };
	m[WeaponTemplate::parseOverrideVoice] = { 0x73acef };
	m[WeaponTemplate::parseDelayBetweenShots] = { 0x6c9d99 };
	m[WeaponTemplate::parseClipReloadTime] = { 0x6c9e7a };
	m[WeaponTemplate::parseScatterTarget] = { 0x6cf43f };
	m[WeaponTemplate::parseLinearTarget] = { 0x6cf475 };
	m[WeaponTemplate::parseWeaponBonus] = { 0x6cb5ab };
	m[WeaponTemplate::parseClearNuggets] = { 0x6cb608 };
	m[ParseObjectFilter] = { 0x76392f };
	m[ParseKindOfMask] = { 0x6564e7 };
	m[ParseDamageScalar] = { 0x90e9e9 };
	m[ParseWeaponLaunchBoneSlot] = { 0x90f933 };
	m[ParseEmotionType] = { 0x8e09ce };
	m[ParseNuggetUnsignedInt] = { 0x42ecb2 };
	m[ParseNuggetFXList] = { 0x73a302 };
	m[ParseRemoveTargetFromOtherContain] = { 0x42e558 };
	return m;
}

// userData name list VA -> the generated array
std::map<std::uint32_t, const char *const *> listByVa()
{
	return {
		{ 0xda14fc, TheFXTriggerNames }, { 0xda15a8, TheDamageNames }, { 0xda1630, TheWeaponDeathNames }, { 0xda1510, TheDamageFXTypeNames },
		{ 0xda16bc, TheWeaponReloadNames }, { 0xda16cc, TheWeaponPrefireNames }, { 0xda16e0, TheWeaponAffectsMaskNames }, { 0xda170c, TheWeaponCollideMaskNames },
		{ 0xdb5d28, TheDamageNames }, { 0xdb5db0, TheWeaponDeathNames }, { 0xdb63d8, TheWeaponDeathNames }, { 0xdb5c90, TheDamageFXTypeNames },
		{ 0xdb5f40, TheDamageFXTypeNames }, { 0xdb5d9c, TheDamageSubTypeNames }, { 0xdb6694, TheFireLogicTypeNames }, { 0xda12e4, TheWeaponSlotTypeNames },
		{ 0xd9f5e4, TheVeterancyNames }, { 0xda12fc, TheWeaponChoiceCriteriaNames }, { 0xda1314, TheCommandSourceMaskNames },
	};
}

const char *const kAnyList = "ANY";

// ---- parsing helpers ---------------------------------------------------------------------------------
struct WeaponWorld
{
	Fixture fx;
	WeaponStores stores;

	WeaponWorld()
	{
		stores.install();
		fx.env.blocks.registerBlock("Weapon", [](INI *ini) { WeaponStore::parseWeaponTemplateDefinitionGlobal(ini); });
	}

	std::string load(const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, int *code = nullptr)
	{
		return loadError(fx.env, "weapon.ini", text, type, code);
	}
	const WeaponTemplate *find(const char *name)
	{
		const WeaponTemplate *t = stores.weapons().findWeaponTemplate(name);
		REQUIRE_MESSAGE(t != nullptr, name);
		return t;
	}
};

template <typename T>
const T *nuggetAs(const WeaponTemplate *w, size_t index)
{
	REQUIRE(index < w->m_nuggets.size());
	const T *n = dynamic_cast<const T *>(w->m_nuggets[index].get());
	REQUIRE(n != nullptr);
	return n;
}

bool nearly(float a, double b, double eps = 1e-5)
{
	return std::fabs((double)a - b) <= eps;
}

bool contains(const std::string &s, const char *part)
{
	return s.find(part) != std::string::npos;
}
}

// =============================================================================================================================
// golden tables
// =============================================================================================================================
TEST_CASE("Weapon: the field table is the 124 rows of RW 0xC16DD8, in order, with the retail parser kinds")
{
	const std::vector<Row> golden = readTsv("table_weapon_template.tsv");
	REQUIRE(golden.size() == 124);
	const FieldParse *mine = WeaponTemplate::getFieldParse();
	const auto addresses = rwParserAddresses();
	const auto lists = listByVa();
	size_t i = 0;
	for (; mine[i].token; ++i)
	{
		REQUIRE(i < golden.size());
		INFO("row " << i << " " << golden[i][1]);
		CHECK(std::string(mine[i].token) == golden[i][1]);
		const std::uint32_t goldenParse = hex(golden[i][2]);
		const std::uint32_t goldenUser = hex(golden[i][3]);
		if (mine[i].parse == WeaponTemplate::parseNugget)
		{
			const WeaponNuggetInfo *info = static_cast<const WeaponNuggetInfo *>(mine[i].userData);
			REQUIRE(info != nullptr);
			CHECK(std::string(info->keyword) == golden[i][1]);
			CHECK(info->rwParseFunction == goldenParse);
			CHECK(goldenUser == 0);
			continue;
		}
		const auto it = addresses.find(mine[i].parse);
		REQUIRE(it != addresses.end());
		CHECK(it->second.count(goldenParse) == 1);
		if (mine[i].parse == INI::parseBitInInt32)
		{
			CHECK((std::uint32_t)(std::uintptr_t)mine[i].userData == goldenUser); // the Anti* masks
		}
		else if (goldenUser == 0)
		{
			CHECK(mine[i].userData == nullptr);
		}
		else
		{
			const auto l = lists.find(goldenUser);
			REQUIRE(l != lists.end());
			REQUIRE(mine[i].userData != nullptr);
			CHECK(namesOf((const char *const *)mine[i].userData) == namesOf(l->second));
		}
	}
	CHECK(i == 124);
}

TEST_CASE("Weapon: the constructor defaults equal the stores of RW 0x6CDE89 (every plain table row)")
{
	const std::vector<Row> golden = readTsv("table_weapon_template.tsv");
	const std::vector<Row> defaults = readTsv("WeaponTemplate_offset_map.tsv", true);
	std::map<std::uint32_t, std::string> defaultAt; // retail offset -> constructor default (hex)
	for (const Row &r : defaults)
	{
		if (r.size() > 3 && !r[3].empty())
		{
			defaultAt[hex(r[0])] = r[3];
		}
	}
	const WeaponTemplate fresh;
	const FieldParse *mine = WeaponTemplate::getFieldParse();
	const auto addresses = rwParserAddresses();
	size_t checked = 0;
	for (size_t i = 0; mine[i].token; ++i)
	{
		const std::uint32_t parse = hex(golden[i][2]);
		size_t bytes = 0;
		if (parse == 0x42e558)
		{
			bytes = 1; // parseBool
		}
		else if (parse == 0x42ed00 || parse == 0x42ee15 || parse == 0x42ec5e || parse == 0x73a429 || parse == 0x73a4b6 || parse == 0x42eefa || parse == 0x42e956 ||
			parse == 0x42e840 || parse == 0x42e574)
		{
			bytes = 4;
		}
		if (bytes == 0 || std::string(mine[i].token) == "MaxAttackPassengers")
		{
			continue; // strings, filters, audio, vectors, custom parsers (tested separately)
		}
		const auto d = defaultAt.find(hex(golden[i][4]));
		REQUIRE_MESSAGE(d != defaultAt.end(), golden[i][1]);
		std::uint32_t want = hex(d->second);
		std::uint32_t got = 0;
		std::memcpy(&got, reinterpret_cast<const char *>(&fresh) + mine[i].offset, bytes);
		if (bytes == 1)
		{
			want &= 0xFF;
		}
		INFO(golden[i][1] << " want " << std::hex << want << " got " << got);
		CHECK(got == want);
		++checked;
	}
	CHECK(checked > 80);
	CHECK(fresh.m_name == "NoNameWeapon");
	CHECK(fresh.m_retiredFlag == -1);
	CHECK_FALSE(fresh.m_projectileFilterInContainer.flag);
	CHECK(fresh.m_projectileFilterInContainer.rule == ObjectFilter::RULE_NONE);
	CHECK(fresh.m_maxAttackPassengers == 0);
}

TEST_CASE("Weapon: the name registries are the binary's, and the enums follow them")
{
	CHECK(namesOf(TheDamageNames) == readList("list_damage_type_names.tsv"));
	CHECK(namesOf(TheWeaponDeathNames) == readList("list_weapon_death_type_names.tsv"));
	CHECK(namesOf(TheDamageFXTypeNames) == readList("list_damage_fx_type_names.tsv"));
	CHECK(namesOf(TheFXTriggerNames) == readList("list_fx_trigger_names.tsv"));
	CHECK(namesOf(TheWeaponReloadNames) == readList("list_auto_reload_names.tsv"));
	CHECK(namesOf(TheWeaponPrefireNames) == readList("list_pre_attack_type_names.tsv"));
	CHECK(namesOf(TheWeaponAffectsMaskNames) == readList("list_radius_damage_affects_names.tsv"));
	CHECK(namesOf(TheWeaponCollideMaskNames) == readList("list_projectile_collides_names.tsv"));
	CHECK(namesOf(TheWeaponSlotTypeNames) == readList("list_weapon_slot_names.tsv"));
	CHECK(namesOf(TheWeaponChoiceCriteriaNames) == readList("list_weapon_choice_criteria_names.tsv"));
	CHECK(namesOf(TheCommandSourceMaskNames) == readList("list_command_source_names.tsv"));
	CHECK(namesOf(TheWeaponBonusConditionNames) == readList("list_weapon_bonus_condition_names.tsv"));
	CHECK(namesOf(TheWeaponBonusFieldNames) == readList("list_weapon_bonus_field_names.tsv"));
	CHECK(namesOf(TheVeterancyNames) == readList("list_veterancy_names.tsv"));
	CHECK(namesOf(TheDamageSubTypeNames) == readList("list_damage_sub_type_names.tsv"));
	CHECK(namesOf(TheEmotionTypeNames) == readList("list_emotion_type_names.tsv"));
	CHECK(namesOf(TheAntiCategoryNames) == readList("list_anti_category_names.tsv"));
	CHECK(namesOf(TheFireLogicTypeNames) == readList("list_fire_logic_type_names.tsv"));
	// the retail slip: the DefaultWeaponChoiceCritera list has no terminator of its own, so it runs into the command source names
	CHECK(namesOf(TheWeaponChoiceCriteriaNames).size() == 9);
	CHECK(namesOf(TheWeaponChoiceCriteriaNames)[6] == "FROM_PLAYER");
	// enumerator values are the list indices
	CHECK(DAMAGE_NUM_TYPES == (int)namesOf(TheDamageNames).size());
	CHECK(DAMAGE_NUM_TYPES == 28);
	CHECK(std::string(TheDamageNames[DAMAGE_FROST]) == "FROST");
	CHECK(std::string(TheDamageNames[DAMAGE_UNDEFINED]) == "UNDEFINED");
	CHECK(std::string(TheDamageNames[DAMAGE_HEALING]) == "HEALING");
	CHECK(std::string(TheDamageNames[DAMAGE_UNRESISTABLE]) == "UNRESISTABLE");
	CHECK(DEATH_NUM_TYPES == (int)namesOf(TheWeaponDeathNames).size());
	CHECK(std::string(TheWeaponDeathNames[DEATH_SLAUGHTERED]) == "SLAUGHTERED");
	CHECK(std::string(TheWeaponDeathNames[DEATH_NONE]) == "NONE");
	CHECK(std::string(TheWeaponReloadNames[RETURN_TO_BASE_TO_RELOAD]) == "RETURN_TO_BASE");
	CHECK(std::string(TheWeaponPrefireNames[PREFIRE_PER_POSITION]) == "PER_POSITION");
	CHECK(std::string(TheWeaponAffectsMaskNames[9]) == "MINES");
	CHECK(WEAPON_AFFECTS_MINES == (1 << 9));
	CHECK(std::string(TheWeaponCollideMaskNames[10]) == "MONSTERS");
	CHECK(WEAPON_COLLIDE_MONSTERS == (1 << 10));
	CHECK(WEAPONBONUS_CONDITION_COUNT == (int)namesOf(TheWeaponBonusConditionNames).size());
	CHECK(WEAPONBONUS_FIELD_COUNT == (int)namesOf(TheWeaponBonusFieldNames).size());
	// the weapon condition names of the WeaponSet Conditions field are HORDE-1's registry
	CHECK(namesOf(TheWeaponConditionNames).size() == 104);
}

TEST_CASE("Weapon nuggets: the registry is the binary's 19 nuggets in table order, every field table matches RW")
{
	const std::vector<Row> registry = readTsv("nugget_registry.tsv", true);
	REQUIRE(registry.size() == 19);
	const std::map<std::uint32_t, std::string> tableFile = {
		{ 0xc7ae00, "table_NuggetBase_c7ae00.tsv" }, { 0xc7afb0, "table_DamageNugget_c7afb0.tsv" }, { 0xc7b378, "table_DamageFieldNugget_c7b378.tsv" },
		{ 0xc7b3f0, "table_WeaponOCLNugget_c7b3f0.tsv" }, { 0xc7b4c8, "table_ProjectileNugget_c7b4c8.tsv" }, { 0xc7b6a8, "table_MetaImpactNugget_c7b6a8.tsv" },
		{ 0xc7bc7c, "table_HordeAttackNugget_c7bc7c.tsv" }, { 0xc7bd20, "table_SpawnAndFadeNugget_c7bd20.tsv" }, { 0xc7b8e0, "table_GrabNugget_c7b8e0.tsv" },
		{ 0xc7b148, "table_AttributeModifierNugget_c7b148.tsv" }, { 0xc7b22c, "table_SpecialModelConditionNugget_c7b22c.tsv" },
		{ 0xc7b2b8, "table_ParalyzeNugget_c7b2b8.tsv" }, { 0xc7bdc8, "table_LuaEventNugget_c7bdc8.tsv" }, { 0xc7bf18, "table_FireLogicNugget_c7bf18.tsv" },
		{ 0xc84858, "table_SlaveAttackNugget_c84858.tsv" }, { 0xc7ba10, "table_DamageContainedNugget_c7ba10.tsv" }, { 0xc7bab8, "table_DOTNugget_c7bab8.tsv" },
		{ 0xc7bb24, "table_OpenGateNugget_c7bb24.tsv" }, { 0xc7bb80, "table_EmotionWeaponNugget_c7bb80.tsv" }, { 0xc7bc10, "table_StealMoneyNugget_c7bc10.tsv" },
	};
	const auto addresses = rwParserAddresses();
	const auto lists = listByVa();
	for (size_t k = 0; k < registry.size(); ++k)
	{
		const Row &r = registry[k];
		const WeaponNuggetInfo &info = WeaponNuggetInfoFor((WeaponNuggetKind)k);
		INFO("nugget " << r[1]);
		CHECK(std::string(info.keyword) == r[1]);
		CHECK(info.rwParseFunction == hex(r[2]));
		CHECK(info.kind == (WeaponNuggetKind)k);
		CHECK(FindWeaponNuggetInfo(r[1].c_str()) == &info);
		// the flag byte the row parse function sets on the weapon
		const std::string flags = r[9];
		CHECK(info.weaponFlagOffset == (flags.find("+0x114") != std::string::npos ? 0x114 : flags.find("+0x157") != std::string::npos ? 0x157 : 0));
		// the table chain
		std::vector<std::uint32_t> adds;
		std::istringstream in(r[8]);
		for (std::string part; std::getline(in, part, ';');)
		{
			adds.push_back(hex(part.substr(0, part.find('@'))));
		}
		size_t chain = 0;
		for (; info.tables[chain]; ++chain)
		{
		}
		REQUIRE(chain == adds.size());
		for (size_t t = 0; t < adds.size(); ++t)
		{
			const auto file = tableFile.find(adds[t]);
			REQUIRE(file != tableFile.end());
			const std::vector<Row> goldenRows = readTsv(file->second, true);
			const FieldParse *table = info.tables[t];
			size_t n = 0;
			for (; table[n].token; ++n)
			{
				REQUIRE(n < goldenRows.size());
				INFO(file->second << " row " << n << " " << goldenRows[n][1]);
				CHECK(std::string(table[n].token) == goldenRows[n][1]);
				const auto parse = addresses.find(table[n].parse);
				REQUIRE(parse != addresses.end());
				CHECK(parse->second.count(hex(goldenRows[n][2])) == 1);
				const std::uint32_t user = hex(goldenRows[n][4]);
				if (user != 0 && table[n].parse == INI::parseLookupList)
				{
					REQUIRE(table[n].userData != nullptr);
					CHECK(user == 0xc16928);
					const LookupListRec *pairs = static_cast<const LookupListRec *>(table[n].userData);
					const std::vector<Row> goldenPairs = readTsv("lookup_weapon_slot_lookup.tsv");
					size_t pi = 0;
					for (; pairs[pi].name; ++pi)
					{
						REQUIRE(pi < goldenPairs.size());
						CHECK(std::string(pairs[pi].name) == goldenPairs[pi][1]);
						CHECK(pairs[pi].value == std::stoi(goldenPairs[pi][2]));
					}
					CHECK(pi == goldenPairs.size());
				}
				else if (user != 0)
				{
					const auto l = lists.find(user);
					REQUIRE(l != lists.end());
					REQUIRE(table[n].userData != nullptr);
					CHECK(namesOf((const char *const *)table[n].userData) == namesOf(l->second));
				}
			}
			CHECK(n == goldenRows.size());
		}
	}
	(void)kAnyList;
}

// =============================================================================================================================
// WeaponBonusSet
// =============================================================================================================================
TEST_CASE("WeaponBonusSet: a fresh set is all 1.0, the line is `<CONDITION> <FIELD> <percent>`, appending is additive over (value - 1)")
{
	WeaponBonusSet set;
	for (int c = 0; c < WEAPONBONUS_CONDITION_COUNT; ++c)
	{
		for (int f = 0; f < WEAPONBONUS_FIELD_COUNT; ++f)
		{
			CHECK(set.get(c, f) == 1.0f);
		}
	}
	WeaponWorld w;
	REQUIRE(w.load("Weapon W\n  WeaponBonus = HORDE RATE_OF_FIRE 150%\n  WeaponBonus = NATIONALISM RATE_OF_FIRE 125%\n  WeaponBonus = VETERAN RANGE 120%\nEnd\n").empty());
	const WeaponTemplate *t = w.find("W");
	REQUIRE(t->m_extraBonus != nullptr);
	CHECK(nearly(t->m_extraBonus->get(1, WEAPONBONUS_RATE_OF_FIRE), 1.5));
	CHECK(nearly(t->m_extraBonus->get(4, WEAPONBONUS_RATE_OF_FIRE), 1.25));
	CHECK(nearly(t->m_extraBonus->get(9, WEAPONBONUS_RANGE), 1.2));
	WeaponBonus b;
	ComputeWeaponBonus((1u << 1) | (1u << 4), 0, nullptr, *t, b);
	CHECK(b.m_field[WEAPONBONUS_RATE_OF_FIRE] == 1.75f); // 1.5 + 1.25 added over 1.0, not multiplied
	CHECK(b.m_field[WEAPONBONUS_RANGE] == 1.0f);
	// the global set first, then the template's
	WeaponBonusSet global;
	WeaponWorld w2;
	REQUIRE(w2.load("Weapon G\n  WeaponBonus = ELITE RATE_OF_FIRE 200%\nEnd\n").empty());
	ComputeWeaponBonus(1u << 10, 0, w2.find("G")->m_extraBonus.get(), *t, b);
	CHECK(b.m_field[WEAPONBONUS_RATE_OF_FIRE] == 2.0f);
	// unknown names are INI errors
	CHECK(contains(w.load("Weapon X\n  WeaponBonus = NOSUCH DAMAGE 100%\nEnd\n"), "NOSUCH"));
	CHECK(contains(w.load("Weapon Y\n  WeaponBonus = HORDE NOSUCH 100%\nEnd\n"), "NOSUCH"));
}

// =============================================================================================================================
// the Weapon block
// =============================================================================================================================
TEST_CASE("Weapon block: durations convert at 5 frames per second, every field stores into its member")
{
	WeaponWorld w;
	REQUIRE(w.load(
		"Weapon W\n"
		"  AttackRange = 150\n  MinimumAttackRange = 20\n  RangeBonusMinHeight = 30\n  RangeBonus = 1\n  RangeBonusPerFoot = 0.5\n  RequestAssistRange = 70\n"
		"  AcceptableAimDelta = 90\n  AimDirection = 180\n  ScatterRadius = 5\n  ScatterTargetScalar = 2\n  ScatterRadiusVsInfantry = 3\n"
		"  ScatterIndependently = Yes\n  DisableScatterForTargetsOnWall = Yes\n  WeaponSpeed = 100\n  MinWeaponSpeed = 50\n  MaxWeaponSpeed = 200\n"
		"  ScaleWeaponSpeed = Yes\n  CanBeDodged = Yes\n  IdleAfterFiringDelay = 1000\n  HoldAfterFiringDelay = 400\n  HoldDuringReload = Yes\n"
		"  CanFireWhileMoving = Yes\n  CanFireWhileCharging = Yes\n  CanSwoop = Yes\n  WeaponRecoil = 10\n  MinTargetPitch = -30\n  MaxTargetPitch = 60\n"
		"  PreferredTargetBone = B_HEAD\n  FireSoundLoopTime = 2000\n  ClipSize = 3\n  ContinuousFireOne = 4\n  ContinuousFireTwo = 8\n  ContinuousFireCoast = 600\n"
		"  AutoReloadWhenIdle = 1500\n  ShotsPerBarrel = 2\n  DamageDealtAtSelfPosition = Yes\n  ProjectileSelf = Yes\n  MeleeWeapon = Yes\n  ChaseWeapon = Yes\n"
		"  LeechRangeWeapon = Yes\n  HitStoredTarget = Yes\n  CapableOfFollowingWaypoints = Yes\n  ShowsAmmoPips = Yes\n  AllowAttackGarrisonedBldgs = Yes\n"
		"  PlayFXWhenStealthed = Yes\n  FiringDuration = 500\n  ContinueAttackRange = 90\n  SuspendFXDelay = 800\n  IgnoreLinearFirstTarget = Yes\n"
		"  ForceDisplayPercentReady = Yes\n  IsAimingWeapon = Yes\n  NoVictimNeeded = Yes\n  RotatingTurret = Yes\n  HitPercentage = 75%\n  HitPassengerPercentage = 25%\n"
		"  PreAttackDelay = 1467\n  PreAttackRandomAmount = 200\n  PassengerProportionalAttack = Yes\n  FinishAttackOnceStarted = No\n  RestrictedHeightRange = 40\n"
		"  CannotTargetCastleVictims = Yes\n  RequireFollowThru = Yes\n  ShareTimers = Yes\n  ShouldPlayUnderAttackEvaEvent = No\n  InstantLoadClipOnActivate = Yes\n"
		"  LockWhenUsing = Yes\n  BombardType = Yes\n  UseInnateAttributes = Yes\n  PreAttackType = PER_POSITION\n  AutoReloadsClip = RETURN_TO_BASE\n"
		"  RadiusDamageAffects = SELF ENEMIES MINES\n  ProjectileCollidesWith = ENEMIES WALLS MONSTERS\n  FXTrigger = TREBUCHET_ROCK\n  DamageType = SIEGE\n"
		"  DeathType = EXPLODED\n  DamageFXType = BIG_ROCK\n  DamageSubType = POISON\n  ProjectileStreamName = Stream1\n"
		"End\n").empty());
	const WeaponTemplate *t = w.find("W");
	CHECK(t->m_attackRange == 150.0f);
	CHECK(t->m_minimumAttackRange == 20.0f);
	CHECK(t->m_rangeBonusMinHeight == 30.0f);
	CHECK(t->m_rangeBonus == 1.0f);
	CHECK(t->m_rangeBonusPerFoot == 0.5f);
	CHECK(t->m_requestAssistRange == 70.0f);
	CHECK(nearly(t->m_aimDelta, 1.5707963));
	CHECK(nearly(t->m_aimDirection, 3.1415927));
	CHECK(t->m_scatterRadius == 5.0f);
	CHECK(t->m_scatterTargetScalar == 2.0f);
	CHECK(t->m_infantryInaccuracyDist == 3.0f);
	CHECK(t->m_scatterIndependently);
	CHECK(t->m_disableScatterForTargetsOnWall);
	CHECK(t->m_weaponSpeed == 20.0f); // per-second to per-frame: 100 * 0.2
	CHECK(t->m_minWeaponSpeed == 10.0f);
	CHECK(t->m_maxWeaponSpeed == 40.0f);
	CHECK(t->m_isScaleWeaponSpeed);
	CHECK(t->m_canBeDodged);
	CHECK(t->m_idleAfterFiringDelay == 5);
	CHECK(t->m_holdAfterFiringDelay == 2);
	CHECK(t->m_holdDuringReload);
	CHECK(t->m_canFireWhileMoving);
	CHECK(t->m_canFireWhileCharging);
	CHECK(t->m_canSwoop);
	CHECK(nearly(t->m_weaponRecoil, 0.17453292));
	CHECK(nearly(t->m_minTargetPitch, -0.5235988));
	CHECK(nearly(t->m_maxTargetPitch, 1.0471976));
	CHECK(t->m_preferredTargetBone == "B_HEAD");
	CHECK(t->m_fireSoundLoopTime == 10);
	CHECK(t->m_clipSize == 3);
	CHECK(t->m_continuousFireOneShotsNeeded == 4);
	CHECK(t->m_continuousFireTwoShotsNeeded == 8);
	CHECK(t->m_continuousFireCoastFrames == 3);
	CHECK(t->m_autoReloadWhenIdle == 8);
	CHECK(t->m_shotsPerBarrel == 2);
	CHECK(t->m_damageDealtAtSelfPosition);
	CHECK(t->m_projectileSelf);
	CHECK(t->m_meleeWeapon);
	CHECK(t->m_chaseWeapon);
	CHECK(t->m_leechRangeWeapon);
	CHECK(t->m_hitStoredTarget);
	CHECK(t->m_capableOfFollowingWaypoints);
	CHECK(t->m_showsAmmoPips);
	CHECK(t->m_allowAttackGarrisonedBldgs);
	CHECK(t->m_playFXWhenStealthed);
	CHECK(t->m_firingDuration == 3);
	CHECK(t->m_continueAttackRange == 90.0f);
	CHECK(t->m_suspendFXDelay == 4);
	CHECK(t->m_ignoreLinearFirstTarget);
	CHECK(t->m_forceDisplayPercentReady);
	CHECK(t->m_isAimingWeapon);
	CHECK(t->m_noVictimNeeded);
	CHECK(t->m_rotatingTurret);
	CHECK(nearly(t->m_hitPercentage, 0.75));
	CHECK(nearly(t->m_hitPassengerPercentage, 0.25));
	CHECK(t->m_preAttackDelay == 8); // ceil(1467 * 0.005) = ceil(7.335)
	CHECK(t->m_preAttackRandomAmount == 1);
	CHECK(t->m_passengerProportionalAttack);
	CHECK_FALSE(t->m_finishAttackOnceStarted);
	CHECK(t->m_restrictedHeightRange == 40.0f);
	CHECK(t->m_cannotTargetCastleVictims);
	CHECK(t->m_requireFollowThru);
	CHECK(t->m_shareTimers);
	CHECK_FALSE(t->m_shouldPlayUnderAttackEvaEvent);
	CHECK(t->m_instantLoadClipOnActivate);
	CHECK(t->m_lockWhenUsing);
	CHECK(t->m_bombardType);
	CHECK(t->m_useInnateAttributes);
	CHECK(t->m_preAttackType == PREFIRE_PER_POSITION);
	CHECK(t->m_autoReloadsClip == RETURN_TO_BASE_TO_RELOAD);
	CHECK(t->m_affectsMask == (WEAPON_AFFECTS_SELF | WEAPON_AFFECTS_ENEMIES | WEAPON_AFFECTS_MINES));
	CHECK(t->m_collideMask == (WEAPON_COLLIDE_ENEMIES | WEAPON_COLLIDE_WALLS | WEAPON_COLLIDE_MONSTERS));
	CHECK(t->m_fxTrigger == 2);
	CHECK(t->m_damageType == DAMAGE_SIEGE);
	CHECK(t->m_deathType == DEATH_EXPLODED);
	CHECK(t->m_damageFXType == 6);
	CHECK(t->m_damageSubType == 27); // RW row 87 parses DamageSubType against the DamageFXType list (POISON is 27 there)
	CHECK(t->m_projectileStreamName == "Stream1");
}

TEST_CASE("Weapon block: Anti* rows set and clear bits of one mask; the default is AntiGround")
{
	WeaponWorld w;
	REQUIRE(w.load("Weapon W\n  AntiAirborneVehicle = Yes\n  AntiGround = No\n  AntiStructure = Yes\n  AntiAirborneMonster = Yes\nEnd\n").empty());
	const WeaponTemplate *t = w.find("W");
	CHECK(t->m_antiMask == (WEAPON_ANTI_AIRBORNE_VEHICLE | WEAPON_ANTI_STRUCTURE | WEAPON_ANTI_AIRBORNE_MONSTER));
	REQUIRE(w.load("Weapon D\nEnd\n").empty());
	CHECK(w.find("D")->m_antiMask == (unsigned)WEAPON_ANTI_GROUND);
}

TEST_CASE("Weapon block: DelayBetweenShots and ClipReloadTime take `N` or `Min:N [Max:M]`, converted with ceil(ms * 0.005f)")
{
	WeaponWorld w;
	REQUIRE(w.load("Weapon W\n  DelayBetweenShots = 250\n  ClipReloadTime = Min:5000 Max:6000\nEnd\n").empty());
	const WeaponTemplate *t = w.find("W");
	CHECK(t->m_delayBetweenShotsMin == 2);
	CHECK(t->m_delayBetweenShotsMax == 2);
	CHECK(t->m_clipReloadMin == 25);
	CHECK(t->m_clipReloadMax == 30);
	REQUIRE(w.load("Weapon X\n  DelayBetweenShots = Min:250\n  ClipReloadTime = min:1000 max:1001\nEnd\n").empty() == false); // retail faults on a missing Max token
	REQUIRE(w.load("Weapon V\n  DelayBetweenShots = Min:250 Max:500\n  ClipReloadTime = Min:1000 Foo:7\nEnd\n").empty());
	const WeaponTemplate *v = w.find("V");
	CHECK(v->m_delayBetweenShotsMin == 2);
	CHECK(v->m_delayBetweenShotsMax == 3);
	CHECK(v->m_clipReloadMin == 5); // `Min` without `Max`: max = min
	CHECK(v->m_clipReloadMax == 5);
	// a plain number, a macro, zero and the exact multiples of 200
	REQUIRE(w.load("#define SLOW 1000\nWeapon U\n  DelayBetweenShots = SLOW\n  ClipReloadTime = 0\nEnd\n").empty());
	CHECK(w.find("U")->m_delayBetweenShotsMin == 5);
	CHECK(w.find("U")->m_clipReloadMax == 0);
	// signed: a negative value is not corrected like the unsigned duration parser does
	REQUIRE(w.load("Weapon T\n  DelayBetweenShots = -1000\nEnd\n").empty());
	CHECK((int)w.find("T")->m_delayBetweenShotsMin == -5);
}

TEST_CASE("Weapon block: ScatterTarget and LinearTarget lines accumulate; WeaponBonus allocates the set on the first line")
{
	WeaponWorld w;
	REQUIRE(w.load("Weapon W\n  ScatterTarget = X:1.5 Y:-2\n  ScatterTarget = X:0 Y:4\n  LinearTarget = X:10 Y:20 T:0\n  LinearTarget = X:1 Y:2 T:3\nEnd\n").empty());
	const WeaponTemplate *t = w.find("W");
	REQUIRE(t->m_scatterTargets.size() == 2);
	CHECK(t->m_scatterTargets[0].x == 1.5f);
	CHECK(t->m_scatterTargets[0].y == -2.0f);
	CHECK(t->m_scatterTargets[1].y == 4.0f);
	REQUIRE(t->m_linearTargets.size() == 2);
	CHECK(t->m_linearTargets[0].x == 10.0f);
	CHECK(t->m_linearTargets[1].t == 3);
	CHECK(t->m_extraBonus == nullptr);
	CHECK(contains(w.load("Weapon X\n  ScatterTarget = Y:1 X:2\nEnd\n"), "Expected 'X'"));
}

TEST_CASE("Weapon block: MaxAttackPassengers is a parseInt into a byte and overruns into FiringDuration's low byte (RW 0x141 / 0x144)")
{
	WeaponWorld w;
	REQUIRE(w.load("Weapon A\n  FiringDuration = 1000\n  MaxAttackPassengers = 4\nEnd\n").empty());
	CHECK(w.find("A")->m_maxAttackPassengers == 4);
	CHECK(w.find("A")->m_firingDuration == 0); // 5 frames, low byte replaced by the int's top byte (0)
	REQUIRE(w.load("Weapon B\n  MaxAttackPassengers = 4\n  FiringDuration = 1000\nEnd\n").empty());
	CHECK(w.find("B")->m_maxAttackPassengers == 4);
	CHECK(w.find("B")->m_firingDuration == 5); // parsed after: intact
	REQUIRE(w.load("Weapon C\n  FiringDuration = 1000\n  MaxAttackPassengers = 16777216\nEnd\n").empty());
	CHECK(w.find("C")->m_maxAttackPassengers == 0);
	CHECK(w.find("C")->m_firingDuration == 1); // 5 with its low byte replaced by 0x01
}

TEST_CASE("Weapon block: FX, sound and particle references are recorded (stop S-181) or checked through a host, retail-exact")
{
	WeaponWorld w;
	REQUIRE(w.load("Weapon W\n  FireFX = FX_Bow\n  FireFlankFX = FX_Flank\n  PreAttackFX = None\n  VeterancyFireFX = ELITE FX_Elite\n  FireSound = BowSound\n"
				   "  ProjectileExhaust = Smoke\n  OverrideVoiceAttackSound = VoiceA\n  OverrideVoiceEnterStateAttackSound = EVA:SomeEvent\nEnd\n")
				.empty());
	const WeaponTemplate *t = w.find("W");
	CHECK(t->m_fireFXs[0] == "FX_Bow");
	CHECK(t->m_fireFXs[1] == "FX_Bow");
	CHECK(t->m_fireFXs[2] == "FX_Elite");
	CHECK(t->m_fireFXs[3] == "FX_Bow");
	CHECK(t->m_fireFlankFX == "FX_Flank");
	CHECK(t->m_preAttackFXs[0].empty());
	CHECK(t->m_fireSound == "BowSound");
	CHECK(t->m_projectileExhaust[2] == "Smoke");
	CHECK(t->m_overrideVoiceAttackSound.name == "VoiceA");
	CHECK(t->m_overrideVoiceEnterStateAttackSound.isEva);
	CHECK(t->m_overrideVoiceEnterStateAttackSound.name == "SomeEvent");
	const std::vector<WeaponStore::Unverified> refs = w.stores.weapons().unverifiedReferences();
	std::set<std::string> kinds;
	for (const auto &r : refs)
	{
		kinds.insert(r.kind + ":" + r.name);
	}
	CHECK(kinds == std::set<std::string>({ "FXList:FX_Bow", "FXList:FX_Flank", "FXList:FX_Elite", "AudioEvent:BowSound", "ParticleSystem:Smoke", "AudioEvent:VoiceA",
		"EvaEvent:SomeEvent" }));
	REQUIRE(w.stores.weapons().acceptanceStops().size() == 1);
	CHECK(w.stores.weapons().acceptanceStops()[0].find("S-181:") == 0);
	REQUIRE(w.load("Weapon S\n  FireSound = NoSound\n  VeterancyFireFX = HEROIC None\nEnd\n").empty());
	CHECK(w.find("S")->m_fireSound.empty());

	// with a host the checks are retail's
	struct Host : WeaponReferenceHost
	{
		bool fxListExists(const std::string &n) const override { return n == "FX_Known"; }
		bool audioEventExists(const std::string &n) const override { return n == "SoundKnown"; }
		bool particleSystemExists(const std::string &n) const override { return n == "PS_Known"; }
		bool evaEventExists(const std::string &n) const override { return n == "EvaKnown"; }
	} host;
	WeaponWorld h;
	h.stores.weapons().setReferenceHost(&host);
	REQUIRE(h.load("Weapon K\n  FireFX = FX_Known\n  FireSound = SoundKnown\n  ProjectileExhaust = PS_Known\n  OverrideVoiceAttackSound = EVA:EvaKnown\nEnd\n").empty());
	CHECK(h.stores.weapons().unverifiedReferences().empty());
	CHECK(contains(h.load("Weapon L\n  FireFX = FX_Missing\nEnd\n"), "iniParseFXList -- FXList FX_Missing not found! Either add the FXList or remove the reference to it."));
	CHECK(contains(h.load("Weapon M\n  FireSound = Nope\nEnd\n"), "Invalid Sound 'Nope'"));
	CHECK(contains(h.load("Weapon N\n  OverrideVoiceAttackSound = EVA:Nope\nEnd\n"), "Unknown EVA event in EVA:Nope"));
	REQUIRE(h.load("Weapon O\n  ProjectileExhaust = PS_Missing\nEnd\n").empty()); // a missing particle system is NULL without an error
	REQUIRE(h.load("Weapon P\n  FireFX = none\n  FireSound = NoSound\nEnd\n").empty());
}

TEST_CASE("Weapon block: errors are retail's (unknown field, bad index name, missing End)")
{
	WeaponWorld w;
	int code = 0;
	CHECK(contains(w.load("Weapon W\n  Bogus = 1\nEnd\n", INI_LOAD_OVERWRITE, &code), "Unknown field 'Bogus' in block 'Weapon'"));
	CHECK(code == 5);
	CHECK(contains(w.load("Weapon W2\n  DamageType = NOSUCH\nEnd\n"), "is not a valid member of the index list"));
	// DamageSubType takes the DamageFXType names, so its own names (NORMAL, ...) are rejected at the weapon level
	CHECK(contains(w.load("Weapon W3\n  DamageSubType = NORMAL\nEnd\n"), "is not a valid member of the index list"));
	CHECK(w.load("Weapon W4\n  DamageSubType = SWORD_SLASH\nEnd\n").empty());
	CHECK(contains(w.load("Weapon W5\n  PreAttackType = per_shot\n  AutoReloadsClip = YES\n"), "Missing 'END' token"));
	CHECK(w.load("Weapon W6\n  PreAttackType = per_shot\n  AutoReloadsClip = yes\nEnd\n").empty()); // index lists are case-insensitive
	// field names are case-sensitive
	CHECK(contains(w.load("Weapon W7\n  attackrange = 5\nEnd\n"), "Unknown field 'attackrange'"));
	// the block needs the store
	WeaponStore *saved = TheWeaponStore;
	TheWeaponStore = nullptr;
	int missingCode = 0;
	CHECK(contains(w.load("Weapon W8\nEnd\n", INI_LOAD_OVERWRITE, &missingCode), "TheWeaponStore==NULL"));
	CHECK(missingCode == 3);
	TheWeaponStore = saved;
}

// =============================================================================================================================
// nuggets
// =============================================================================================================================
TEST_CASE("Weapon nuggets: every nugget parses into its fields; constructor defaults are the binary's")
{
	WeaponWorld w;
	REQUIRE(w.load("Weapon D\n  DamageNugget\n  End\n  DOTNugget\n  End\n  FireLogicNugget\n  End\n  MetaImpactNugget\n  End\n  GrabNugget\n  End\n"
				   "  AttributeModifierNugget\n  End\n  ProjectileNugget\n  End\n  HordeAttackNugget\n  End\n  EmotionWeaponNugget\n  End\n  SlaveAttackNugget\n  End\n"
				   "  DamageContainedNugget\n  End\n  SpawnAndFadeNugget\n  End\nEnd\n")
				.empty());
	const WeaponTemplate *t = w.find("D");
	REQUIRE(t->m_nuggets.size() == 12);
	const DamageNugget *dmg = nuggetAs<DamageNugget>(t, 0);
	CHECK(dmg->m_damage == 0.0f);
	CHECK(dmg->m_damageTaperOff == -1.0f);
	CHECK(dmg->m_acceptDamageAdd);
	CHECK(dmg->m_radius == 0.0f);
	CHECK(dmg->m_damageArc == 3.14159274f);
	CHECK(dmg->m_damageMaxHeight == -1.0f);
	CHECK(dmg->m_damageMaxHeightAboveTerrain == -1.0f);
	CHECK(dmg->m_damageType == DAMAGE_UNDEFINED);
	CHECK(dmg->m_deathType == DEATH_NORMAL);
	CHECK(dmg->m_damageFXType == 29);
	CHECK(dmg->m_flankedScalar == 1.0f);
	CHECK(dmg->m_flankingBonus == 0.0f);
	CHECK(dmg->m_drainLifeMultiplier == 1.0f);
	CHECK(dmg->m_forceKillObjectFilter.rule == ObjectFilter::RULE_NONE);
	CHECK(dmg->m_specialObjectFilter.rule == ObjectFilter::RULE_NONE);
	const DOTNugget *dot = nuggetAs<DOTNugget>(t, 1);
	CHECK(dot->m_damageInterval == 0);
	CHECK(dot->m_damageType == DAMAGE_UNDEFINED);
	const FireLogicNugget *fire = nuggetAs<FireLogicNugget>(t, 2);
	CHECK(fire->m_maxResistance == 10000);
	CHECK(fire->m_logicType == 0);
	const MetaImpactNugget *meta = nuggetAs<MetaImpactNugget>(t, 3);
	CHECK(meta->m_shockWaveZMult == 1.0f);
	CHECK(meta->m_shockWaveClearMult == 2.0f);
	CHECK(meta->m_shockWaveClearFlingHeight == 100.0f);
	CHECK(meta->m_shockWaveArc == 3.14159274f);
	CHECK(meta->m_killObjectFilter.rule == ObjectFilter::RULE_NONE);
	const GrabNugget *grab = nuggetAs<GrabNugget>(t, 4);
	CHECK(grab->m_containTargetOnEffect);
	CHECK_FALSE(grab->m_impactTargetOnEffect);
	CHECK_FALSE(grab->m_removeTargetFromOtherContainSet);
	CHECK(grab->m_shockWaveZMult == 1.0f);
	CHECK(nuggetAs<AttributeModifierNugget>(t, 5)->m_damageFXType == 29);
	CHECK(nuggetAs<AttributeModifierNugget>(t, 5)->m_damageArc == 3.14159274f);
	CHECK(nuggetAs<ProjectileNugget>(t, 6)->m_weaponLaunchBoneSlotOverride == 6);
	CHECK(nuggetAs<HordeAttackNugget>(t, 7)->m_lockWeaponSlot == 5);
	CHECK(nuggetAs<EmotionWeaponNugget>(t, 8)->m_emotionType == -1);
	CHECK(nuggetAs<DamageContainedNugget>(t, 10)->m_deathType == DEATH_NONE);
	CHECK(t->m_hasDamageNugget);
	CHECK(t->m_hasGrabNugget);
	for (const auto &n : t->m_nuggets)
	{
		CHECK(n->m_owner == t);
		CHECK_FALSE(n->m_ownedByOverride);
	}
}

TEST_CASE("Weapon nuggets: field values (every nugget class)")
{
	WeaponWorld w;
	REQUIRE(w.load(
		"Weapon W\n"
		"  DamageNugget\n    Damage = 55\n    DamageTaperOff = 20\n    AcceptDamageAdd = No\n    Radius = 30\n    MinRadius = 5\n    DamageArc = 90\n    DamageArcInverted = Yes\n"
		"    DamageMaxHeight = 12\n    DamageMaxHeightAboveTerrain = 8\n    DelayTime = 600\n    DamageType = HERO_RANGED\n    DeathType = BURNED\n    DamageFXType = GOOD_ARROW_PIERCE\n"
		"    DamageSubType = BECOME_UNDEAD\n    DamageScalar = 25000% NONE +INFANTRY\n    DamageScalar = 50% ALL\n    DamageSpeed = 50\n    LostLeadershipUselessAgainst = HERO\n"
		"    FlankingBonus = 50%\n    FlankedScalar = 5%\n    DrainLife = Yes\n    DrainLifeMultiplier = 0.5\n    CylinderAOE = Yes\n    ForceKillObjectFilter = ANY\n"
		"    RequiredUpgradeNames = Upgrade_A Upgrade_B\n    ForbiddenUpgradeNames = Upgrade_C\n    SpecialObjectFilter = ANY +STRUCTURE\n  End\n"
		"  DamageFieldNugget\n    WeaponTemplateName = Field\n    Duration = 3000\n  End\n"
		"  WeaponOCLNugget\n    WeaponOCLName = OCL_Boom\n  End\n"
		"  ProjectileNugget\n    WarheadTemplateName = Warhead\n    ProjectileTemplateName = Arrow\n    ProjectileStreamName = Stream\n    WeaponLaunchBoneSlotOverride = TERTIARY\n"
		"    AlwaysAttackHereOffset = X:1 Y:2 Z:3\n    UseAlwaysAttackOffset = Yes\n  End\n"
		"  MetaImpactNugget\n    ShockWaveAmount = 50\n    ShockWaveRadius = 10\n    ShockWaveArc = 45\n    ShockWaveArcInverted = Yes\n    ShockWaveTaperOff = 0.5\n    ShockWaveSpeed = 25\n"
		"    ShockWaveZMult = 3\n    DelayTime = 200\n    InvertShockWave = Yes\n    FlipDirection = Yes\n    HeroResist = .75\n    OnlyWhenJustDied = Yes\n    CyclonicFactor = 2\n"
		"    ShockWaveClearRadius = Yes\n    ShockWaveClearMult = 4\n    ShockWaveClearFlingHeight = 60\n    KillObjectFilter = ANY\n    AffectHordes = Yes\n  End\n"
		"  HordeAttackNugget\n    ClosestMemberOnly = Yes\n    LockWeaponSlot = SECONDARY\n  End\n"
		"  SpawnAndFadeNugget\n    ObjectTargetFilter = NONE\n    SpawnedObjectName = Spawn\n    SpawnOffset = X:4 Y:5 Z:6\n  End\n"
		"  GrabNugget\n    ContainTargetOnEffect = No\n    ImpactTargetOnEffect = Yes\n    RemoveTargetFromOtherContain = Yes\n    ShockWaveAmount = 2\n    ShockWaveRadius = 3\n"
		"    ShockWaveTaperOff = 4\n    ShockWaveSpeed = 5\n    ShockWaveZMult = 6\n  End\n"
		"  AttributeModifierNugget\n    AttributeModifier = Buff\n    DamageFXType = MAGIC\n    Radius = 20\n    DamageArc = 180\n    AntiCategories = SPELL BUFF\n    AntiFX = FX_Anti\n    AffectHordeMembers = Yes\n  End\n"
		"  SpecialModelConditionNugget\n    ModelConditionNames = USER_1 USER_2\n    ModelConditionDuration = 1000\n  End\n"
		"  ParalyzeNugget\n    Radius = 15\n    Duration = 2000\n    DamageArc = 270\n    ParalyzeFX = FX_Par\n    FreezeAnimation = Yes\n    AffectHordeMembers = Yes\n  End\n"
		"  LuaEventNugget\n    LuaEvent = BeUncontrollablyAfraid\n    Radius = 99\n    SendToEnemies = Yes\n    SendToAllies = Yes\n    SendToNeutral = Yes\n  End\n"
		"  FireLogicNugget\n    Damage = 7\n    LogicType = INCREASE_FUEL\n    MinMaxBurnRate = 3\n    MinDecay = 4\n    MaxResistance = 5\n  End\n"
		"  SlaveAttackNugget\n  End\n"
		"  DamageContainedNugget\n    KillCount = 2\n    KillKindof = INFANTRY\n    KillKindofNot = HERO\n    DeathType = CRUSHED\n  End\n"
		"  DOTNugget\n    Damage = 9\n    DamageInterval = 1000\n    DamageDuration = 5000\n  End\n"
		"  OpenGateNugget\n    Radius = 25\n  End\n"
		"  EmotionWeaponNugget\n    EmotionType = terror\n    Radius = 30\n    Duration = 5\n  End\n"
		"  StealMoneyNugget\n    AmountStolenPerAttack = 3.5\n  End\n"
		"End\n").empty());
	const WeaponTemplate *t = w.find("W");
	REQUIRE(t->m_nuggets.size() == 19);
	const DamageNugget *d = nuggetAs<DamageNugget>(t, 0);
	CHECK(d->m_damage == 55.0f);
	CHECK(d->m_damageTaperOff == 20.0f);
	CHECK_FALSE(d->m_acceptDamageAdd);
	CHECK(d->m_radius == 30.0f);
	CHECK(d->m_minRadius == 5.0f);
	CHECK(nearly(d->m_damageArc, 1.5707963));
	CHECK(d->m_damageArcInverted);
	CHECK(d->m_damageMaxHeight == 12.0f);
	CHECK(d->m_damageMaxHeightAboveTerrain == 8.0f);
	CHECK(d->m_delayTime == 3);
	CHECK(d->m_damageType == DAMAGE_HERO_RANGED);
	CHECK(d->m_deathType == DEATH_BURNED);
	CHECK(d->m_damageFXType == 3);
	CHECK(d->m_damageSubType == 1);
	REQUIRE(d->m_damageScalar.size() == 2);
	CHECK(nearly(d->m_damageScalar[0].scalar, 250.0));
	CHECK(d->m_damageScalar[0].filter.rule == ObjectFilter::RULE_NONE);
	CHECK(d->m_damageScalar[0].filter.includeKindOf != KindOfMaskType{});
	CHECK(nearly(d->m_damageScalar[1].scalar, 0.5));
	CHECK(d->m_damageScalar[1].filter.rule == ObjectFilter::RULE_ALL);
	CHECK(nearly(d->m_damageSpeed, 10.0));
	CHECK(d->m_lostLeadershipUselessAgainst != KindOfMaskType{});
	CHECK(nearly(d->m_flankingBonus, 0.5));
	CHECK(nearly(d->m_flankedScalar, 0.05));
	CHECK(d->m_drainLife);
	CHECK(d->m_drainLifeMultiplier == 0.5f);
	CHECK(d->m_cylinderAOE);
	CHECK(d->m_forceKillObjectFilter.rule == ObjectFilter::RULE_ANY);
	CHECK(d->m_requiredUpgradeNames == std::vector<std::string>({ "Upgrade_A", "Upgrade_B" }));
	CHECK(d->m_forbiddenUpgradeNames == std::vector<std::string>({ "Upgrade_C" }));
	CHECK(d->m_specialObjectFilter.rule == ObjectFilter::RULE_ANY);
	CHECK(nuggetAs<DamageFieldNugget>(t, 1)->m_weaponTemplateName == "Field");
	CHECK(nuggetAs<DamageFieldNugget>(t, 1)->m_duration == 15);
	CHECK(nuggetAs<WeaponOCLNugget>(t, 2)->m_weaponOCLName == "OCL_Boom");
	const ProjectileNugget *p = nuggetAs<ProjectileNugget>(t, 3);
	CHECK(p->m_warheadTemplateName == "Warhead");
	CHECK(p->m_projectileTemplateName == "Arrow");
	CHECK(p->m_projectileStreamName == "Stream");
	CHECK(p->m_weaponLaunchBoneSlotOverride == 2);
	CHECK(p->m_alwaysAttackHereOffset.z == 3.0f);
	CHECK(p->m_useAlwaysAttackOffset);
	const MetaImpactNugget *m = nuggetAs<MetaImpactNugget>(t, 4);
	CHECK(m->m_shockWaveAmount == 10.0f);
	CHECK(m->m_shockWaveRadius == 10.0f);
	CHECK(nearly(m->m_shockWaveArc, 0.7853982));
	CHECK(m->m_shockWaveArcInverted);
	CHECK(m->m_shockWaveTaperOff == 0.5f);
	CHECK(m->m_shockWaveSpeed == 5.0f);
	CHECK(m->m_shockWaveZMult == 3.0f);
	CHECK(m->m_delayTime == 1);
	CHECK(m->m_invertShockWave);
	CHECK(m->m_flipDirection);
	CHECK(m->m_heroResist == 0.75f);
	CHECK(m->m_onlyWhenJustDied);
	CHECK(m->m_cyclonicFactor == 2.0f);
	CHECK(m->m_shockWaveClearRadius);
	CHECK(m->m_shockWaveClearMult == 4.0f);
	CHECK(m->m_shockWaveClearFlingHeight == 60.0f);
	CHECK(m->m_killObjectFilter.rule == ObjectFilter::RULE_ANY);
	CHECK(m->m_affectHordes);
	CHECK(nuggetAs<HordeAttackNugget>(t, 5)->m_closestMemberOnly);
	CHECK(nuggetAs<HordeAttackNugget>(t, 5)->m_lockWeaponSlot == 1);
	CHECK(nuggetAs<SpawnAndFadeNugget>(t, 6)->m_spawnedObjectName == "Spawn");
	CHECK(nuggetAs<SpawnAndFadeNugget>(t, 6)->m_spawnOffset.y == 5.0f);
	const GrabNugget *g = nuggetAs<GrabNugget>(t, 7);
	CHECK_FALSE(g->m_containTargetOnEffect);
	CHECK(g->m_impactTargetOnEffect);
	CHECK(g->m_removeTargetFromOtherContain);
	CHECK(g->m_removeTargetFromOtherContainSet);
	CHECK(g->m_shockWaveAmount == 2.0f);
	CHECK(g->m_shockWaveSpeed == 5.0f); // Grab's ShockWaveSpeed is a plain real (no per-frame conversion)
	const AttributeModifierNugget *a = nuggetAs<AttributeModifierNugget>(t, 8);
	CHECK(a->m_attributeModifier == "Buff");
	CHECK(a->m_damageFXType == 9);
	CHECK(a->m_radius == 20.0f);
	CHECK(nearly(a->m_damageArc, 3.1415927));
	CHECK(a->m_antiCategories == ((1u << 3) | (1u << 7)));
	CHECK(a->m_antiFX == "FX_Anti");
	CHECK(a->m_affectHordeMembers);
	const SpecialModelConditionNugget *s = nuggetAs<SpecialModelConditionNugget>(t, 9);
	CHECK(s->m_modelConditionNames == std::vector<std::string>({ "USER_1", "USER_2" }));
	CHECK(s->m_modelConditionDuration == 5);
	const ParalyzeNugget *par = nuggetAs<ParalyzeNugget>(t, 10);
	CHECK(par->m_radius == 15.0f);
	CHECK(par->m_duration == 10);
	CHECK(nearly(par->m_damageArc, 4.712389));
	CHECK(par->m_paralyzeFX == "FX_Par");
	CHECK(par->m_freezeAnimation);
	CHECK(par->m_affectHordeMembers);
	const LuaEventNugget *lua = nuggetAs<LuaEventNugget>(t, 11);
	CHECK(lua->m_luaEvent == "BeUncontrollablyAfraid");
	CHECK(lua->m_radius == 99.0f);
	CHECK(lua->m_sendToEnemies);
	CHECK(lua->m_sendToAllies);
	CHECK(lua->m_sendToNeutral);
	const FireLogicNugget *fire = nuggetAs<FireLogicNugget>(t, 12);
	CHECK(fire->m_damage == 7.0f);
	CHECK(fire->m_logicType == 2);
	CHECK(fire->m_minMaxBurnRate == 3);
	CHECK(fire->m_minDecay == 4);
	CHECK(fire->m_maxResistance == 5);
	CHECK(nuggetAs<SlaveAttackNugget>(t, 13) != nullptr);
	const DamageContainedNugget *dc = nuggetAs<DamageContainedNugget>(t, 14);
	CHECK(dc->m_killCount == 2);
	CHECK(dc->m_deathType == DEATH_CRUSHED);
	CHECK(dc->m_killKindof != KindOfMaskType{});
	CHECK(dc->m_killKindofNot != KindOfMaskType{});
	const DOTNugget *dot = nuggetAs<DOTNugget>(t, 15);
	CHECK(dot->m_damage == 9.0f);
	CHECK(dot->m_damageInterval == 5);
	CHECK(dot->m_damageDuration == 25);
	CHECK(nuggetAs<OpenGateNugget>(t, 16)->m_radius == 25.0f);
	const EmotionWeaponNugget *e = nuggetAs<EmotionWeaponNugget>(t, 17);
	CHECK(e->m_emotionType == 6);
	CHECK(e->m_radius == 30.0f);
	CHECK(e->m_duration == 5); // a raw unsigned int, not converted to frames
	CHECK(nuggetAs<StealMoneyNugget>(t, 18)->m_amountStolenPerAttack == 3.5f);
	// the unknown emotion name is -1, not an error
	REQUIRE(w.load("Weapon E2\n  EmotionWeaponNugget\n    EmotionType = NOSUCH\n  End\nEnd\n").empty());
	CHECK(nuggetAs<EmotionWeaponNugget>(w.find("E2"), 0)->m_emotionType == -1);
	// LuaEventNugget does not chain the base table
	CHECK(contains(w.load("Weapon E3\n  LuaEventNugget\n    RequiredUpgradeNames = X\n  End\nEnd\n"), "Unknown field 'RequiredUpgradeNames'"));
	CHECK(contains(w.load("Weapon E4\n  DamageNugget\n    Bogus = 1\n  End\nEnd\n"), "Unknown field 'Bogus'"));
	// every flag byte on the weapon
	CHECK(t->m_hasDamageNugget);
	CHECK(t->m_hasGrabNugget);
	REQUIRE(w.load("Weapon F\n  FireLogicNugget\n  End\nEnd\n").empty());
	CHECK_FALSE(w.find("F")->m_hasDamageNugget); // FireLogicNugget derives from DamageNugget but its row does not set the flag
	REQUIRE(w.load("Weapon F2\n  DamageFieldNugget\n  End\nEnd\n").empty());
	CHECK(w.find("F2")->m_hasDamageNugget);
	REQUIRE(w.load("Weapon F3\n  DOTNugget\n  End\nEnd\n").empty());
	CHECK(w.find("F3")->m_hasDamageNugget);
}

TEST_CASE("Weapon nuggets: ClearNuggets drops every nugget and the damage flag but not the grab flag")
{
	WeaponWorld w;
	REQUIRE(w.load("Weapon W\n  DamageNugget\n  End\n  GrabNugget\n  End\n  ClearNuggets\n  OpenGateNugget\n  End\nEnd\n").empty());
	const WeaponTemplate *t = w.find("W");
	REQUIRE(t->m_nuggets.size() == 1);
	CHECK(nuggetAs<OpenGateNugget>(t, 0) != nullptr);
	CHECK_FALSE(t->m_hasDamageNugget);
	CHECK(t->m_hasGrabNugget); // RW 0x6CB608 leaves +0x157 alone
}

// =============================================================================================================================
// the store: duplicates, overrides, reload
// =============================================================================================================================
TEST_CASE("WeaponStore: a repeated name is skipped for load types 1 and 3, overridden by type 2, replaced by type 5")
{
	WeaponWorld w;
	REQUIRE(w.load("Weapon W\n  AttackRange = 10\n  DamageNugget\n    Damage = 5\n  End\n  ScatterTarget = X:1 Y:1\nEnd\n").empty());
	CHECK(w.stores.weapons().size() == 1);
	// load type 1: the second block is NOT read; its lines reach the dispatcher, which finds no block called AttackRange
	int code = 0;
	CHECK(contains(w.load("Weapon W\n  AttackRange = 99\nEnd\n", INI_LOAD_OVERWRITE, &code), "Unknown block"));
	CHECK(code == 5);
	CHECK(w.find("W")->m_attackRange == 10.0f);
	CHECK(w.stores.weapons().size() == 1);

	// type 2: a new template replaces the entry, linked to the parent; nuggets are shared, scatter targets are copied and appended to
	REQUIRE(w.load("Weapon W\n  AttackRange = 99\n  ScatterTarget = X:2 Y:2\n  DamageNugget\n    Damage = 7\n  End\nEnd\n", INI_LOAD_CREATE_OVERRIDES).empty());
	const WeaponTemplate *o = w.find("W");
	CHECK(w.stores.weapons().size() == 1);
	CHECK(o->isOverride());
	REQUIRE(o->getNextOverride() != nullptr);
	CHECK(o->getNextOverride()->m_attackRange == 10.0f);
	CHECK(o->m_attackRange == 99.0f);
	CHECK(o->m_scatterTargets.size() == 2);
	CHECK(o->getNextOverride()->m_scatterTargets.size() == 1);
	REQUIRE(o->m_nuggets.size() == 2);
	CHECK(o->m_nuggets[0].get() == o->getNextOverride()->m_nuggets[0].get()); // the same nugget object
	CHECK_FALSE(o->m_nuggets[0]->m_ownedByOverride);
	CHECK(o->m_nuggets[1]->m_ownedByOverride);
	// ClearNuggets in an override drops the override's list only
	// a second override of an overridden weapon: retail's newOverride returns NULL and initFromINI rejects it
	CHECK(contains(w.load("Weapon W\n  AttackRange = 5\nEnd\n", INI_LOAD_CREATE_OVERRIDES), "INI::initFromINI - Invalid parameters supplied!"));
	// the between-maps reset returns every entry to its root
	w.stores.resetOverrides();
	CHECK(w.find("W")->m_attackRange == 10.0f);
	CHECK_FALSE(w.find("W")->isOverride());
	REQUIRE(w.load("Weapon W\n  AttackRange = 55\nEnd\n", INI_LOAD_CREATE_OVERRIDES).empty());
	CHECK(w.find("W")->m_attackRange == 55.0f);

	// a new name under type 2 is a plain new template (not flagged as an override: the weapon parser has no such rule)
	REQUIRE(w.load("Weapon N\n  AttackRange = 3\nEnd\n", INI_LOAD_CREATE_OVERRIDES).empty());
	CHECK_FALSE(w.find("N")->isOverride());

	// type 5: the old template is retired and a fresh one takes the name
	const size_t before = w.stores.weapons().retiredCount();
	REQUIRE(w.load("Weapon N\n  ClipSize = 9\nEnd\n", INI_LOAD_RELOAD).empty());
	CHECK(w.stores.weapons().retiredCount() == before + 1);
	CHECK(w.find("N")->m_clipSize == 9);
	CHECK(w.find("N")->m_attackRange == 0.0f);
	CHECK(w.find("N")->m_retiredFlag == 0);
}

TEST_CASE("WeaponStore: names are case sensitive, the store keeps definition order")
{
	WeaponWorld w;
	REQUIRE(w.load("Weapon Bow\nEnd\nWeapon bow\nEnd\nWeapon Axe\nEnd\n").empty());
	CHECK(w.stores.weapons().size() == 3);
	CHECK(w.stores.weapons().findWeaponTemplate("BOW") == nullptr);
	CHECK(w.stores.weapons().templates()[0]->getName() == "Bow");
	CHECK(w.stores.weapons().templates()[2]->getName() == "Axe");
}

// =============================================================================================================================
// WeaponSet
// =============================================================================================================================
TEST_CASE("WeaponSet: the field table is the 10 rows of RW 0xC16D20 and the defaults are RW 0x6C827A")
{
	const std::vector<Row> golden = readTsv("table_weapon_template_set.tsv");
	REQUIRE(golden.size() == 10);
	const FieldParse *mine = WeaponTemplateSet::getFieldParse();
	size_t i = 0;
	for (; mine[i].token; ++i)
	{
		REQUIRE(i < golden.size());
		CHECK(std::string(mine[i].token) == golden[i][1]);
	}
	CHECK(i == 10);
	CHECK(hex(golden[6][2]) == 0x42e558);
	CHECK(hex(golden[9][2]) == 0x42e956);
	CHECK(namesOf((const char *const *)mine[9].userData) == readList("list_weapon_choice_criteria_names.tsv"));
	const WeaponTemplateSet set;
	for (int s = 0; s < WEAPONSLOT_COUNT; ++s)
	{
		CHECK(set.m_template[s] == nullptr);
		CHECK(set.m_autoChooseMask[s] == 0xFFFFFFFFu);
	}
	CHECK_FALSE(set.hasAnyWeapons());
	CHECK_FALSE(set.m_isReloadTimeShared);
	CHECK(set.m_defaultWeaponChoiceCriteria == 0);
}

TEST_CASE("WeaponSet: Weapon = <SLOT> <name> resolves through the store; None and unknown names store NULL")
{
	WeaponWorld w;
	REQUIRE(w.load("Weapon Sword\nEnd\nWeapon Bow\nEnd\n").empty());
	WeaponTemplateSet set;
	{
		Fixture fx;
		INIEnvironment &env = fx.env;
		env.blocks.registerBlock("WeaponSetBlock", [&set](INI *ini) { set.parseWeaponTemplateSet(ini, nullptr); });
		const std::string text =
			"WeaponSetBlock\n  Conditions = VETERAN ELITE\n  Weapon = PRIMARY Sword\n  Weapon = SECONDARY Bow\n  Weapon = TERTIARY None\n  Weapon = QUATERNARY Missing\n"
			"  AutoChooseSources = SECONDARY FROM_PLAYER FROM_AI\n  PreferredAgainst = PRIMARY INFANTRY\n  OnlyAgainst = SECONDARY CAVALRY\n  OnlyInCondition = PRIMARY MOVING\n"
			"  ShareWeaponReloadTime = Yes\n  WeaponLockSharedAcrossSets = Yes\n  ReadyStatusSharedWithinSet = Yes\n  DefaultWeaponChoiceCritera = PREFER_LONGEST_RANGE\nEnd\n";
		CHECK(loadError(env, "set.ini", text).empty());
	}
	CHECK(set.m_template[PRIMARY_WEAPON] == w.find("Sword"));
	CHECK(set.m_template[SECONDARY_WEAPON] == w.find("Bow"));
	CHECK(set.m_template[TERTIARY_WEAPON] == nullptr);
	CHECK(set.m_template[QUATERNARY_WEAPON] == nullptr);
	CHECK(set.unresolvedWeapons == std::vector<std::string>({ "Missing" }));
	CHECK(set.hasAnyWeapons());
	CHECK(set.testWeaponSetFlag(0)); // VETERAN
	CHECK(set.testWeaponSetFlag(1)); // ELITE
	CHECK_FALSE(set.testWeaponSetFlag(2));
	CHECK(set.m_autoChooseMask[SECONDARY_WEAPON] == 5u); // FROM_PLAYER | FROM_AI (bits 0 and 2)
	CHECK(set.m_autoChooseMask[PRIMARY_WEAPON] == 0xFFFFFFFFu);
	CHECK(set.m_isReloadTimeShared);
	CHECK(set.m_isWeaponLockSharedAcrossSets);
	CHECK(set.m_isReadyStatusSharedWithinSet);
	CHECK(set.m_defaultWeaponChoiceCriteria == 1);
	CHECK(set.m_onlyInCondition[PRIMARY_WEAPON] != ModelConditionMask{});
	CHECK(set.m_onlyInCondition[SECONDARY_WEAPON] == ModelConditionMask{});
	CHECK(set.m_preferredAgainst[PRIMARY_WEAPON] != KindOfMaskType{});
	CHECK(set.m_onlyAgainst[SECONDARY_WEAPON] != KindOfMaskType{});
}

TEST_CASE("WeaponSet: a slot name outside the five names is an INI error (the sixth array slot is unreachable)")
{
	WeaponWorld w;
	WeaponTemplateSet set;
	Fixture fx;
	fx.env.blocks.registerBlock("WS", [&set](INI *ini) { set.parseWeaponTemplateSet(ini, nullptr); });
	CHECK(contains(loadError(fx.env, "s.ini", "WS\n  Weapon = SIXTH Sword\nEnd\n"), "is not a valid member of the index list"));
	CHECK(contains(loadError(fx.env, "s.ini", "WS\n  Bogus = 1\nEnd\n"), "Unknown field 'Bogus'"));
	// DefaultWeaponChoiceCritera accepts the three command source names too (the list runs into them)
	WeaponTemplateSet set2;
	Fixture fx2;
	fx2.env.blocks.registerBlock("WS", [&set2](INI *ini) { set2.parseWeaponTemplateSet(ini, nullptr); });
	CHECK(loadError(fx2.env, "s.ini", "WS\n  DefaultWeaponChoiceCritera = FROM_AI\nEnd\n").empty());
	CHECK(set2.m_defaultWeaponChoiceCriteria == 8);
}

// =============================================================================================================================
// retail corpus (SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset)
// =============================================================================================================================
namespace
{
std::string lowerCopy(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}
}

TEST_CASE("Weapon retail: every Weapon, Armor, DamageFX block and object-level WeaponSet / ArmorSet of the pure 2.01 INI set parses, every name resolves")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("WeaponSet retail");
		return;
	}
	REQUIRE_MESSAGE(mount->error.empty(), mount->error);
	RetailObjectWorld world(*mount->fs);
	std::string error;
	REQUIRE_MESSAGE(world.load(&error), error);
	for (const auto &e : world.report().errors)
	{
		MESSAGE(e.file << ": " << e.message);
	}
	CHECK(world.report().errors.empty());
	WeaponStores &stores = world.weaponStores();
	// tools/weapon/scan_retail_blocks.py: 747 Weapon (741 + 6 cinematic), 221 Armor, 12 DamageFX blocks, all names unique
	CHECK(stores.weapons().size() == 747);
	CHECK(stores.armors().size() == 221);
	CHECK(stores.damageFX().size() == 12);
	CHECK(stores.armors().overrideCount() == 0);
	// nugget census (tools/weapon/scan_retail_blocks.py counts the nuggets directly inside Weapon blocks the same way)
	std::map<std::string, size_t> perKind;
	size_t total = 0;
	size_t damageFlagged = 0;
	for (const auto &t : stores.weapons().templates())
	{
		for (const auto &n : t->m_nuggets)
		{
			perKind[n->keyword()]++;
			++total;
		}
		damageFlagged += t->m_hasDamageNugget ? 1 : 0;
	}
	CHECK(perKind["DamageNugget"] == 653);
	CHECK(perKind["ProjectileNugget"] == 269);
	CHECK(perKind["MetaImpactNugget"] == 171);
	CHECK(perKind["FireLogicNugget"] == 65);
	CHECK(perKind["HordeAttackNugget"] == 37);
	CHECK(perKind["DOTNugget"] == 27);
	CHECK(perKind["AttributeModifierNugget"] == 23);
	CHECK(perKind["WeaponOCLNugget"] == 13);
	CHECK(perKind["ParalyzeNugget"] == 8);
	CHECK(perKind["SlaveAttackNugget"] == 8);
	CHECK(perKind["LuaEventNugget"] == 5);
	CHECK(perKind["GrabNugget"] == 4);
	CHECK(perKind["SpecialModelConditionNugget"] == 4);
	CHECK(perKind["EmotionWeaponNugget"] == 3);
	CHECK(perKind["StealMoneyNugget"] == 3);
	CHECK(perKind["OpenGateNugget"] == 3);
	CHECK(perKind["SpawnAndFadeNugget"] == 2);
	CHECK(perKind["DamageFieldNugget"] == 1);
	CHECK(perKind["DamageContainedNugget"] == 1);
	CHECK(perKind.size() == 19);
	CHECK(total == 1300);
	CHECK(damageFlagged > 450);
	size_t passengerWeapons = 0;
	for (const auto &t : stores.weapons().templates())
	{
		passengerWeapons += t->m_maxAttackPassengers != 0 ? 1 : 0;
	}
	CHECK(passengerWeapons == 1);
	const WeaponTemplate *legolas = stores.weapons().findWeaponTemplate("LegolasBow");
	REQUIRE(legolas != nullptr);
	CHECK(legolas->m_clipSize == 1);
	CHECK(legolas->m_preAttackType == PREFIRE_PER_POSITION);
	CHECK(legolas->m_autoReloadsClip == AUTO_RELOAD);
	CHECK(legolas->m_canFireWhileMoving);
	CHECK((legolas->m_antiMask & WEAPON_ANTI_AIRBORNE_VEHICLE) != 0);
	CHECK((legolas->m_antiMask & WEAPON_ANTI_GROUND) != 0);
	REQUIRE(legolas->m_nuggets.size() == 1);
	CHECK(nuggetAs<ProjectileNugget>(legolas, 0)->m_projectileTemplateName == "GoodFactionArrow");
	const WeaponTemplate *warhead = stores.weapons().findWeaponTemplate("LegolasBowWarhead");
	REQUIRE(warhead != nullptr);
	const DamageNugget *wd = nuggetAs<DamageNugget>(warhead, 0);
	CHECK(wd->m_damageType == DAMAGE_HERO_RANGED);
	CHECK(wd->m_damageFXType == 3);
	CHECK(warhead->m_hitStoredTarget);
	CHECK(warhead->m_affectsMask == (WEAPON_AFFECTS_ENEMIES | WEAPON_AFFECTS_NEUTRALS | WEAPON_DOESNT_AFFECT_SIMILAR));
	// the references the missing FXList / audio / particle stores leave open (stop S-181) are reported, not dropped
	CHECK_FALSE(stores.weapons().unverifiedReferences().empty());
	CHECK(stores.weapons().acceptanceStops().size() == 1);
	// tools/weapon/scan_retail_blocks.py: 792 WeaponSet and 1903 ArmorSet field lines in the object files (the engine loads them with -cinematics on)
	CHECK(world.things().rawFieldCounts().at("WeaponSet") == 792);
	CHECK(world.things().rawFieldCounts().at("ArmorSet") == 1903);
	size_t weaponSets = 0, armorSets = 0, withWeapons = 0;
	std::set<std::string> unresolved;
	size_t unknownArmors = 0, unknownFX = 0;
	std::set<ArmorSetFlags> flagValues;
	for (const ThingTemplate *t : world.things().templates())
	{
		weaponSets += t->weaponTemplateSets().size();
		armorSets += t->armorTemplateSets().size();
		for (const WeaponTemplateSet &s : t->weaponTemplateSets())
		{
			withWeapons += s.hasAnyWeapons() ? 1 : 0;
			for (const std::string &n : s.unresolvedWeapons)
			{
				unresolved.insert(n);
			}
		}
		for (const ArmorTemplateSet &s : t->armorTemplateSets())
		{
			flagValues.insert(s.m_flags);
			if (!s.m_armorName.empty() && world.weaponStores().armors().findArmorTemplate(s.m_armorName) == nullptr)
			{
				++unknownArmors;
			}
			if (!s.m_damageFXName.empty() && !s.m_damageFXResolved)
			{
				++unknownFX;
			}
		}
	}
	for (const std::string &n : unresolved)
	{
		MESSAGE("unresolved weapon " << n);
	}
	CHECK(unresolved.empty());
	CHECK(unknownArmors == 0);
	CHECK(unknownFX == 0);
	CHECK(weaponSets > 400);
	CHECK(armorSets > 1000);
	CHECK(withWeapons > 300);
	CHECK(flagValues.size() > 3);
}

TEST_CASE("Weapon lane: the acceptance stops S-180 .. S-189 are reported exactly (docs/STOPS.md)")
{
	const std::vector<std::string> stops = WeaponLaneStops();
	std::vector<std::string> ids;
	for (const std::string &line : stops)
	{
		ids.push_back(line.substr(0, 5));
	}
	CHECK(ids == std::vector<std::string>({ "S-180", "S-182", "S-183", "S-184", "S-185", "S-186", "S-187", "S-188", "S-189" }));
	CHECK(contains(stops[3], "FiringTracker"));
	CHECK(contains(stops[4], "0x5D8A64"));
	// S-181 is the store's own report (the references a missing store leaves unchecked), pinned in the Weapon block test above
	WeaponWorld w;
	CHECK(w.stores.weapons().acceptanceStops().empty());
	REQUIRE(w.load("Weapon W\n  FireFX = FX_A\nEnd\n").empty());
	REQUIRE(w.stores.weapons().acceptanceStops().size() == 1);
	CHECK(w.stores.weapons().acceptanceStops()[0].find("S-181: 1 ") == 0);
}

TEST_CASE("Weapon block: the sentinels and prefixes compare case-insensitively like retail (_strcmpi NoSound, _strnicmp EVA: and +SOUND:)")
{
	struct Host : WeaponReferenceHost
	{
		bool fxListExists(const std::string &) const override { return true; }
		bool audioEventExists(const std::string &n) const override { return n == "RealSound"; }
		bool particleSystemExists(const std::string &) const override { return true; }
		bool evaEventExists(const std::string &n) const override { return n == "RealEva"; }
	} host;
	WeaponWorld w;
	w.stores.weapons().setReferenceHost(&host);
	REQUIRE(w.load("Weapon A\n  FireSound = nosound\n  OverrideVoiceAttackSound = NOSOUND\n  OverrideVoiceEnterStateAttackSound = eva:RealEva\nEnd\n").empty());
	CHECK(w.find("A")->m_fireSound.empty());
	CHECK(w.find("A")->m_overrideVoiceAttackSound.name.empty());
	CHECK(w.find("A")->m_overrideVoiceEnterStateAttackSound.isEva);
	CHECK(w.find("A")->m_overrideVoiceEnterStateAttackSound.name == "RealEva");
	REQUIRE(w.load("Weapon B\n  OverrideVoiceAttackSound = +sound:RealSound\nEnd\n").empty());
	CHECK(w.find("B")->m_overrideVoiceAttackSound.plusSound);
	CHECK(w.find("B")->m_overrideVoiceAttackSound.name == "RealSound");
	REQUIRE(w.load("Weapon C\n  OverrideVoiceAttackSound = +Sound:RealSound\nEnd\n").empty());
	CHECK(w.find("C")->m_overrideVoiceAttackSound.plusSound);
	// the checks still fire for unknown names under any spelling
	CHECK(contains(w.load("Weapon D\n  OverrideVoiceAttackSound = +sound:Nope\nEnd\n"), "Invalid Sound 'Nope'"));
	CHECK(contains(w.load("Weapon E\n  OverrideVoiceAttackSound = Eva:Nope\nEnd\n"), "Unknown EVA event in EVA:Nope"));
	// the nugget-level FXList "None" is case-insensitive as well
	REQUIRE(w.load("Weapon F\n  AttributeModifierNugget\n    AntiFX = NONE\n  End\nEnd\n").empty());
	CHECK(w.stores.weapons().unverifiedReferences().empty());
}
