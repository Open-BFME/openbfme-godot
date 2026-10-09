// OpenBFME unit tests: Armor, ArmorSet, DamageFX and the deterministic damage core (GameLogic/Damage.h). GPL-3.0. Lane WEAPON-1.
// Expected values: the golden vectors of tools/weapon/damage_golden.py (an independent model of the retail routines' instruction order,
// workspace spec weapons-and-damage.md section 5; no retail oracle runs on this machine, so every vector is "derived from the binary
// by reading", stop S-186), the FieldParse tables dumped into tests/data/weapon1, and the PLAN rule 2 conversions.

#include "doctest.h"
#include "IniTestUtil.h"
#include "RetailTestMount.h"

#include "GameLogic/Armor.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/Damage.h"
#include "GameLogic/DamageFX.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponNugget.h"
#include "GameLogic/WeaponStores.h"
#include "Common/NumericState.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>

using namespace initest;

namespace
{
// the INI percent: float32(float32(n) * 0.01f), one rounding (RW 0x42EB18)
float pct(float n)
{
	return NumericState::pc24Mul(n, 0.01f);
}

std::uint32_t bitsOf(float f)
{
	std::uint32_t u;
	std::memcpy(&u, &f, 4);
	return u;
}

float floatOf(std::uint32_t u)
{
	float f;
	std::memcpy(&f, &u, 4);
	return f;
}

bool contains(const std::string &s, const char *part)
{
	return s.find(part) != std::string::npos;
}

std::vector<std::vector<std::string>> readTsv(const std::string &name)
{
	std::vector<unsigned char> bytes;
	std::string error;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/weapon1/" + name, bytes, &error), error);
	std::vector<std::vector<std::string>> rows;
	std::vector<std::string> row;
	std::string cell;
	for (unsigned char c : bytes)
	{
		if (c == '\t')
		{
			row.push_back(cell);
			cell.clear();
		}
		else if (c == '\n')
		{
			row.push_back(cell);
			cell.clear();
			rows.push_back(row);
			row.clear();
		}
		else
		{
			cell.push_back((char)c);
		}
	}
	return rows;
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

struct CombatWorld
{
	Fixture fx;
	WeaponStores stores;
	CombatWorld()
	{
		stores.install();
		fx.env.blocks.registerBlock("Weapon", [](INI *ini) { WeaponStore::parseWeaponTemplateDefinitionGlobal(ini); });
		fx.env.blocks.registerBlock("Armor", [](INI *ini) { ArmorStore::parseArmorDefinitionGlobal(ini); });
		fx.env.blocks.registerBlock("DamageFX", [](INI *ini) { DamageFXStore::parseDamageFXDefinitionGlobal(ini); });
	}
	std::string load(const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, int *code = nullptr)
	{
		return loadError(fx.env, "t.ini", text, type, code);
	}
};

// ---- AdjustDamage host double ----
struct ArmorHost : AdjustDamageHost
{
	bool flanked = false;
	bool invulnerable = false;
	float modifierSum = 0.0f;
	float cap = 0.75f;
	bool victimFlankedByAttacker() override { return flanked; }
	bool invulnerableTo(int) override { return invulnerable; }
	float armorModifierSum(int) override { return modifierSum; }
	float armorMaxBonus() override { return cap; }
};

ArmorTemplate armorWith(int type, float coefficient, float penalty = 0.0f, float scalar = 1.0f)
{
	ArmorTemplate a;
	a.m_damageCoefficient[type] = coefficient;
	a.m_flankedPenalty = penalty;
	a.m_damageScalar = scalar;
	return a;
}

DamageInfoInput input(int type, float amount, unsigned source = 0)
{
	DamageInfoInput in;
	in.m_damageType = type;
	in.m_amount = amount;
	in.m_sourceID = source;
	return in;
}
}

// =============================================================================================================================
// Armor block
// =============================================================================================================================
TEST_CASE("Armor: the field table is the 3 rows of RW 0xBEFD98 and the defaults are RW 0x5D86AC")
{
	const auto golden = readTsv("table_armor_block.tsv");
	REQUIRE(golden.size() == 3);
	const FieldParse *t = ArmorTemplate::getFieldParse();
	CHECK(std::string(t[0].token) == golden[0][1]);
	CHECK(std::string(t[1].token) == golden[1][1]);
	CHECK(std::string(t[2].token) == golden[2][1]);
	CHECK(t[3].token == nullptr);
	CHECK(t[0].parse == ArmorTemplate::parseDamageScalar);
	CHECK(t[1].parse == INI::parsePercentToReal);
	CHECK(t[2].parse == ArmorTemplate::parseArmorCoefficients);
	CHECK(golden[1][2] == "0x42eefa");
	CHECK(t[1].offset == (int)offsetof(ArmorTemplate, m_flankedPenalty));
	const ArmorTemplate a;
	CHECK(a.m_flankedPenalty == 0.0f);
	CHECK(a.m_damageScalar == 1.0f);
	CHECK(a.m_flag == -1);
	for (int i = 0; i < DAMAGE_NUM_TYPES; ++i)
	{
		CHECK(a.m_damageCoefficient[i] == 1.0f);
	}
	CHECK(DAMAGE_NUM_TYPES == 28);
	CHECK(sizeof(a.m_damageCoefficient) / sizeof(float) == 28);
}

TEST_CASE("Armor: `Armor = <type|Default> <percent>`, FlankedPenalty and DamageScalar; percent = float32(float32(text) * 0.01f)")
{
	CombatWorld w;
	REQUIRE(w.load("Armor A\n  Armor = Default 80%\n  Armor = pierce 120%\n  Armor = FROST 0%\n  DamageScalar = 50%\n  FlankedPenalty = 25%\nEnd\n").empty());
	const ArmorTemplate *a = w.stores.armors().findArmorTemplate("A");
	REQUIRE(a != nullptr);
	CHECK(a->m_damageCoefficient[DAMAGE_SLASH] == pct(80.0f));
	CHECK(bitsOf(a->m_damageCoefficient[DAMAGE_PIERCE]) == 0x3F999999u); // "120%" is 1.1999999, not 1.2f (0x3F99999A)
	CHECK(a->m_damageCoefficient[DAMAGE_FROST] == 0.0f);
	CHECK(a->m_damageScalar == 0.5f);
	CHECK(a->m_flankedPenalty == 0.25f);
	CHECK(a->m_name == "A");
	CHECK(a->m_flag == -1);
	// Default fills every coefficient, so it must come first: a later Default overwrites the specific lines
	REQUIRE(w.load("Armor B\n  Armor = SLASH 10%\n  Armor = DEFAULT 90%\nEnd\n").empty());
	CHECK(w.stores.armors().findArmorTemplate("B")->m_damageCoefficient[DAMAGE_SLASH] == pct(90.0f));
	CHECK(w.stores.armors().findArmorTemplate("B")->m_damageCoefficient[DAMAGE_SLASH] == w.stores.armors().findArmorTemplate("B")->m_damageCoefficient[DAMAGE_CRUSH]);
	// the percent is parsed before the type is looked at: a bad percent errors first
	CHECK(contains(w.load("Armor C\n  Armor = NOSUCH xyz\nEnd\n"), "Expected floating point value"));
	CHECK(contains(w.load("Armor D\n  Armor = NOSUCH 50%\nEnd\n"), "is not a valid member of the index list"));
	CHECK(contains(w.load("Armor E\n  Bogus = 1\nEnd\n"), "Unknown field 'Bogus' in block 'Armor'"));
	CHECK(contains(w.load("Armor F\n  Armor = SLASH\nEnd\n"), "Expected additional data"));
	REQUIRE(w.load("Armor G\nEnd\n").empty());
	CHECK(w.stores.armors().findArmorTemplate("G")->m_damageCoefficient[DAMAGE_FORCE] == 1.0f);
	CHECK(w.stores.armors().findArmorTemplate("Missing") == nullptr);
	CHECK(w.stores.armors().findArmorTemplate("a") == nullptr); // case sensitive
}

TEST_CASE("Armor: the FIRST definition wins (the duplicate is parsed and discarded, its errors still fire); type 5 flags and appends")
{
	CombatWorld w;
	REQUIRE(w.load("Armor A\n  Armor = SLASH 10%\nEnd\n").empty());
	REQUIRE(w.load("Armor A\n  Armor = SLASH 99%\nEnd\n").empty());
	CHECK(w.stores.armors().findArmorTemplate("A")->m_damageCoefficient[DAMAGE_SLASH] == pct(10.0f));
	CHECK(w.stores.armors().size() == 1);
	CHECK(contains(w.load("Armor A\n  Armor = NOSUCH 50%\nEnd\n"), "is not a valid member of the index list"));
	// map.ini style (type 2) is first-wins too
	REQUIRE(w.load("Armor A\n  Armor = SLASH 55%\nEnd\n", INI_LOAD_CREATE_OVERRIDES).empty());
	CHECK(w.stores.armors().findArmorTemplate("A")->m_damageCoefficient[DAMAGE_SLASH] == pct(10.0f));
	// type 5: the existing template is flagged, the new one lands on the override list, the map entry is NOT replaced
	REQUIRE(w.load("Armor A\n  Armor = SLASH 77%\nEnd\n", INI_LOAD_RELOAD).empty());
	CHECK(w.stores.armors().overrideCount() == 1);
	CHECK(w.stores.armors().isOverridden("A"));
	CHECK(w.stores.armors().findArmorTemplate("A")->m_damageCoefficient[DAMAGE_SLASH] == pct(10.0f));
	CHECK_FALSE(w.stores.armors().isOverridden("Missing"));
	ArmorStore *saved = TheArmorStore;
	TheArmorStore = nullptr;
	CHECK(contains(w.load("Armor Z\nEnd\n"), "TheArmorStore==NULL"));
	TheArmorStore = saved;
}

// =============================================================================================================================
// ArmorSet
// =============================================================================================================================
TEST_CASE("ArmorSet: the table is RW 0xC26BB0, Conditions is a bit string over 21 names, DamageFX resolves by name")
{
	const auto golden = readTsv("table_armor_template_set.tsv");
	REQUIRE(golden.size() == 3);
	const FieldParse *t = ArmorTemplateSet::getFieldParse();
	for (int i = 0; i < 3; ++i)
	{
		CHECK(std::string(t[i].token) == golden[(size_t)i][1]);
	}
	CHECK(t[3].token == nullptr);
	CHECK(golden[0][2] == "0x73d99b");
	CHECK(golden[2][2] == "0x73af3f");
	const auto names = readTsv("list_armor_set_condition_names.tsv");
	std::vector<std::string> goldenNames;
	for (const auto &r : names)
	{
		goldenNames.push_back(r[1]);
	}
	CHECK(namesOf(TheArmorSetNames) == goldenNames);
	CHECK(goldenNames.size() == 21);

	CombatWorld w;
	REQUIRE(w.load("DamageFX FX1\nEnd\n").empty());
	ArmorTemplateSet set;
	Fixture fx;
	fx.env.blocks.registerBlock("AS", [&set](INI *ini) { set.parseArmorTemplateSet(ini); });
	REQUIRE(loadError(fx.env, "a.ini", "AS\n  Conditions = VETERAN ELITE\n  Armor = HeroArmor\n  DamageFX = FX1\nEnd\n").empty());
	CHECK(set.m_flags == 3u);
	CHECK(set.m_armorName == "HeroArmor");
	CHECK(set.m_damageFXName == "FX1");
	CHECK(set.m_damageFXResolved);
	ArmorTemplateSet none;
	Fixture fx2;
	fx2.env.blocks.registerBlock("AS", [&none](INI *ini) { none.parseArmorTemplateSet(ini); });
	REQUIRE(loadError(fx2.env, "a.ini", "AS\n  Conditions = +HERO\n  DamageFX = None\nEnd\n").empty());
	CHECK(none.m_flags == 4u);
	CHECK(none.m_damageFXName.empty());
	ArmorTemplateSet unknown;
	Fixture fx3;
	fx3.env.blocks.registerBlock("AS", [&unknown](INI *ini) { unknown.parseArmorTemplateSet(ini); });
	REQUIRE(loadError(fx3.env, "a.ini", "AS\n  DamageFX = NoSuchFX\nEnd\n").empty()); // an unknown DamageFX is NULL without an error
	CHECK_FALSE(unknown.m_damageFXResolved);
	CHECK(contains(loadError(fx3.env, "a.ini", "AS\n  Conditions = VETERAN +ELITE\nEnd\n"), "you may not mix normal and +- ops"));
	CHECK(contains(loadError(fx3.env, "a.ini", "AS\n  Conditions = NOSUCH\nEnd\n"), "is not a valid member of the index list"));
}

TEST_CASE("ArmorSet: the best match is yes-count first, then fewest unmet conditions; ties keep the earlier entry (RW 0x73D917)")
{
	std::vector<ArmorTemplateSet> sets(3);
	sets[0].m_flags = 0;        // the default set
	sets[1].m_flags = 1;        // VETERAN
	sets[2].m_flags = 1 | 2;    // VETERAN ELITE
	CHECK(FindArmorTemplateSet(sets, 0) == &sets[0]);
	CHECK(FindArmorTemplateSet(sets, 1) == &sets[1]);
	CHECK(FindArmorTemplateSet(sets, 1 | 2) == &sets[2]);
	CHECK(FindArmorTemplateSet(sets, 1 | 2 | 4) == &sets[2]);
	CHECK(FindArmorTemplateSet(sets, 4) == &sets[0]);
	CHECK(FindArmorTemplateSet(sets, 2) == &sets[2]); // yes 1 beats the default's yes 0 even though one condition is unmet
	CHECK(FindArmorTemplateSet({}, 1) == nullptr);
	std::vector<ArmorTemplateSet> tie(2);
	tie[0].m_flags = 1;
	tie[1].m_flags = 1;
	CHECK(FindArmorTemplateSet(tie, 1) == &tie[0]);
}

// =============================================================================================================================
// DamageFX
// =============================================================================================================================
TEST_CASE("DamageFX: the table is RW 0xC2CF80; Default fills every type, Veterancy rows one level; a repeated block resets; row 0 is the one read")
{
	const auto golden = readTsv("table_damage_fx_block.tsv");
	REQUIRE(golden.size() == 8);
	const FieldParse *t = DamageFX::getFieldParse();
	for (int i = 0; i < 8; ++i)
	{
		CHECK(std::string(t[i].token) == golden[(size_t)i][1]);
		CHECK((t[i].userData != nullptr) == (golden[(size_t)i][3] != "0x0"));
	}
	CHECK(t[8].token == nullptr);
	const auto typeNames = readTsv("list_damage_fx_block_type_names.tsv");
	CHECK(typeNames.size() == 36);
	CHECK(namesOf(TheDamageFXBlockTypeNames).size() == 36);

	CombatWorld w;
	REQUIRE(w.load("DamageFX D\n  AmountForMajorFX = Default 50\n  MajorFX = SWORD_SLASH FX_Big\n  MinorFX = SWORD_SLASH FX_Small\n  ThrottleTime = SWORD_SLASH 1000\n"
				   "  VeterancyAmountForMajorFX = ELITE SWORD_SLASH 80\n  VeterancyMajorFX = ELITE Default None\nEnd\n").empty());
	const DamageFX *fx = w.stores.damageFX().findDamageFX("D");
	REQUIRE(fx != nullptr);
	CHECK(fx->entry(0, 0).amountForMajorFX == 50.0f);
	CHECK(fx->entry(35, 3).amountForMajorFX == 50.0f);
	CHECK(fx->entry(0, 2).amountForMajorFX == 80.0f);
	CHECK(fx->entry(0, 1).amountForMajorFX == 50.0f);
	CHECK(fx->entry(0, 0).majorFX == "FX_Big");
	CHECK(fx->entry(0, 3).minorFX == "FX_Small");
	CHECK(fx->entry(0, 2).majorFX.empty());
	CHECK(fx->entry(5, 0).majorFX.empty());
	CHECK(fx->entry(0, 0).throttleTime == 5);
	CHECK(fx->getThrottleTime(0) == 5);
	CHECK(fx->getDamageFX(0, 0.0f).empty());
	CHECK(fx->getDamageFX(0, 49.0f) == "FX_Small");
	CHECK(fx->getDamageFX(0, 50.0f) == "FX_Big");
	CHECK(fx->getDamageFX(0, 51.0f) == "FX_Big");
	// the last definition wins: the block is cleared and re-parsed
	REQUIRE(w.load("DamageFX D\n  AmountForMajorFX = Default 5\nEnd\n").empty());
	CHECK(w.stores.damageFX().findDamageFX("D")->entry(0, 0).majorFX.empty());
	CHECK(w.stores.damageFX().findDamageFX("D")->entry(0, 0).amountForMajorFX == 5.0f);
	CHECK(w.stores.damageFX().size() == 1);
	CHECK(contains(w.load("DamageFX E\n  MajorFX = NOSUCH FX\nEnd\n"), "is not a valid member of the index list"));
	CHECK(contains(w.load("DamageFX E\n  VeterancyMajorFX = LEGENDARY Default FX\nEnd\n"), "is not a valid member of the index list"));
	// the unverified FXList names are kept for the report
	REQUIRE(w.load("DamageFX F\n  MajorFX = Default FX_X\nEnd\n").empty());
	CHECK(w.stores.damageFX().findDamageFX("F")->m_unverifiedFXLists == std::vector<std::string>({ "FX_X" }));
}

// =============================================================================================================================
// AdjustDamage golden vectors (tools/weapon/damage_golden.py)
// =============================================================================================================================
TEST_CASE("AdjustDamage: the golden vectors (RW 0x5D893C operation order, SSE float32)")
{
	ArmorHost host;
	// normal hit: PIERCE 100 vs coefficient 100%
	{
		const ArmorTemplate a = armorWith(DAMAGE_PIERCE, 1.0f);
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_PIERCE, 100.0f), false, host) == 100.0f);
	}
	// coefficient 75%: the parse and the hit
	const float c75 = floatOf(bitsOf(0.75f));
	{
		CombatWorld w;
		REQUIRE(w.load("Armor P\n  Armor = PIERCE 75%\n  Armor = SLASH 120%\nEnd\n").empty());
		const ArmorTemplate *p = w.stores.armors().findArmorTemplate("P");
		CHECK(p->m_damageCoefficient[DAMAGE_PIERCE] == c75);
		CHECK(AdjustDamage(p, p, input(DAMAGE_PIERCE, 100.0f), false, host) == 75.0f);
		CHECK(bitsOf(p->m_damageCoefficient[DAMAGE_SLASH]) == 0x3F999999u);
		CHECK(bitsOf(AdjustDamage(p, p, input(DAMAGE_SLASH, 100.0f), false, host)) == 0x42EFFFFFu); // 119.999992, not 120
	}
	// UNRESISTABLE ignores the coefficient, honours the scalar
	{
		const ArmorTemplate a = armorWith(DAMAGE_UNRESISTABLE, 0.0f);
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_UNRESISTABLE, 100.0f), false, host) == 100.0f);
		const ArmorTemplate s = armorWith(DAMAGE_UNRESISTABLE, 0.0f, 0.0f, 0.5f);
		CHECK(AdjustDamage(&s, &s, input(DAMAGE_UNRESISTABLE, 100.0f), false, host) == 50.0f);
	}
	// HEALING ignores everything
	{
		const ArmorTemplate a = armorWith(DAMAGE_HEALING, 0.0f, 0.0f, 0.5f);
		ArmorHost h;
		h.invulnerable = true;
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_HEALING, 40.0f), false, h) == 40.0f);
	}
	// FORCE skips the ARMOR modifier stage
	{
		const ArmorTemplate a = armorWith(DAMAGE_FORCE, 0.5f);
		ArmorHost h;
		h.modifierSum = 0.5f;
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_FORCE, 100.0f), false, h) == 50.0f);
	}
	// ARMOR modifiers: +50%, 0.5 + 0.5 capped at 75%, a debuff
	{
		const ArmorTemplate a = armorWith(DAMAGE_PIERCE, 1.0f);
		ArmorHost h;
		h.modifierSum = 0.5f;
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_PIERCE, 100.0f), false, h) == 50.0f);
		h.modifierSum = 0.5f + 0.5f;
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_PIERCE, 100.0f), false, h) == 25.0f);
		h.modifierSum = -0.25f;
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_PIERCE, 100.0f), false, h) == 125.0f);
		h.invulnerable = true;
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_PIERCE, 100.0f), false, h) == 0.0f);
	}
	// a flanked victim: the armour benefit is multiplied by (1 - penalty): coefficient 0.5 with penalty 0.5 gives 75
	{
		const ArmorTemplate a = armorWith(DAMAGE_PIERCE, 0.5f, 0.5f);
		ArmorHost h;
		h.flanked = true;
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_PIERCE, 100.0f, 7), false, h) == 75.0f);
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_PIERCE, 100.0f, 7), true, h) == 50.0f); // the estimate path never flanks
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_PIERCE, 100.0f, 0), false, h) == 50.0f); // no source: no flank test
		h.flanked = false;
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_PIERCE, 100.0f, 7), false, h) == 50.0f);
		CHECK(AdjustDamage(nullptr, nullptr, input(DAMAGE_PIERCE, 100.0f, 7), false, h) == 100.0f); // an unknown armour name: coefficient 1.0
	}
	// no minimum and no clamp
	{
		const ArmorTemplate a = armorWith(DAMAGE_SLASH, 0.5f);
		ArmorHost h;
		CHECK(AdjustDamage(&a, &a, input(DAMAGE_SLASH, 0.001f), false, h) == 0.001f * 0.5f);
		const ArmorTemplate n = armorWith(DAMAGE_PIERCE, -1.0f);
		CHECK(AdjustDamage(&n, &n, input(DAMAGE_PIERCE, 10.0f), false, h) == -10.0f);
	}
	// the scalar of the current armor set entry applies, a different template than the cached armour
	{
		const ArmorTemplate a = armorWith(DAMAGE_PIERCE, 1.0f);
		const ArmorTemplate s = armorWith(DAMAGE_PIERCE, 1.0f, 0.0f, 0.5f);
		ArmorHost h;
		CHECK(AdjustDamage(&a, &s, input(DAMAGE_PIERCE, 100.0f), false, h) == 50.0f);
	}
	ArmorHost bad;
	const ArmorTemplate a;
	CHECK_THROWS_AS(AdjustDamage(&a, &a, input(DAMAGE_NUM_TYPES, 1.0f), false, bad), std::out_of_range);
}

// =============================================================================================================================
// the ActiveBody health arithmetic
// =============================================================================================================================
TEST_CASE("ActiveBody: the health arithmetic of RW 0x8C3FA3 / 0x8C31A5 (golden vectors)")
{
	auto apply = [](float health, float amount, int type, float scalar, bool kill, BodyHealth &b, DamageInfo &info) {
		b.health = health;
		b.maxHealth = 500.0f;
		info = DamageInfo();
		info.m_input.m_damageType = type;
		info.m_input.m_kill = kill;
		return ActiveBodyApplyDamage(b, info, amount, scalar, false);
	};
	BodyHealth b;
	DamageInfo info;
	CHECK(apply(500.0f, 75.0f, DAMAGE_PIERCE, 1.0f, false, b, info));
	CHECK(b.health == 425.0f);
	CHECK(info.m_output.m_actualDamageDealt == 75.0f);
	CHECK(info.m_output.m_actualDamageClipped == 75.0f);
	CHECK(b.previousHealth == 500.0f);
	// overkill: dealt is the full amount, clipped the health that was left
	CHECK(apply(30.0f, 75.0f, DAMAGE_PIERCE, 1.0f, false, b, info));
	CHECK(b.health == 0.0f);
	CHECK(info.m_output.m_actualDamageDealt == 75.0f);
	CHECK(info.m_output.m_actualDamageClipped == 30.0f);
	// zero damage is skipped
	CHECK_FALSE(apply(30.0f, 0.0f, DAMAGE_PIERCE, 1.0f, false, b, info));
	CHECK(b.health == 30.0f);
	// the body scalar applies to PIERCE and not to UNRESISTABLE
	CHECK(apply(500.0f, 100.0f, DAMAGE_PIERCE, 0.5f, false, b, info));
	CHECK(b.health == 450.0f);
	CHECK(info.m_output.m_actualDamageDealt == 50.0f);
	CHECK(apply(500.0f, 100.0f, DAMAGE_UNRESISTABLE, 0.5f, false, b, info));
	CHECK(b.health == 400.0f);
	CHECK(info.m_output.m_actualDamageDealt == 100.0f);
	// kill: the amount becomes the current health, even for a zero amount
	CHECK(apply(321.0f, 0.0f, DAMAGE_PIERCE, 1.0f, true, b, info));
	CHECK(b.health == 0.0f);
	CHECK(info.m_output.m_actualDamageDealt == 321.0f);
	// a negative adjusted amount is skipped (no clamp in AdjustDamage, the body gate drops it)
	CHECK_FALSE(apply(100.0f, -10.0f, DAMAGE_PIERCE, 1.0f, false, b, info));
	// internalChangeHealth heals up to the maximum and clears the heal history there
	BodyHealth h;
	h.health = 490.0f;
	h.maxHealth = 500.0f;
	InternalChangeHealth(h, 25.0f);
	CHECK(h.health == 500.0f);
	CHECK(h.healHistoryCleared);
	CHECK(h.previousHealth == 490.0f);
	// the fire cap leaves 1 health
	BodyHealth f;
	DamageInfo fi;
	f.health = 40.0f;
	f.maxHealth = 500.0f;
	fi.m_input.m_damageType = DAMAGE_FLAME;
	CHECK(ActiveBodyApplyDamage(f, fi, 100.0f, 1.0f, true));
	CHECK(f.health == 1.0f);
	CHECK(fi.m_output.m_actualDamageDealt == 39.0f);
	// Highlander and Immortal
	CHECK(HighlanderClampAmount(100.0f, 30.0f, DAMAGE_PIERCE) == 29.0f);
	CHECK(HighlanderClampAmount(10.0f, 30.0f, DAMAGE_PIERCE) == 10.0f);
	CHECK(HighlanderClampAmount(100.0f, 30.0f, DAMAGE_UNRESISTABLE) == 100.0f);
	BodyHealth im;
	im.health = 20.0f;
	im.maxHealth = 100.0f;
	ImmortalInternalChangeHealth(im, -50.0f);
	CHECK(im.health == 1.0f);
	ImmortalInternalChangeHealth(im, -5.0f);
	CHECK(im.health == 1.0f);
	// the pending damage countdown: a hit queued with delay d is applied on update floor(d) + 1
	float delay = 2.5f;
	CHECK_FALSE(PendingDamageDue(delay));
	CHECK_FALSE(PendingDamageDue(delay));
	CHECK(PendingDamageDue(delay));
	float exact = 3.0f; // exactly 3: the third update leaves 0, which is not < 0
	CHECK_FALSE(PendingDamageDue(exact));
	CHECK_FALSE(PendingDamageDue(exact));
	CHECK_FALSE(PendingDamageDue(exact));
	CHECK(PendingDamageDue(exact));
	CHECK(PendingDamageDelayFor(0.0f, true) == 1.0f);
	CHECK(PendingDamageDelayFor(7.0f, false) == 7.0f);
}

// =============================================================================================================================
// FillDamageInfo
// =============================================================================================================================
namespace
{
struct NuggetHost : NuggetDamageHost
{
	bool victim = true;
	float distSqr = 0.0f;
	Coord3D victimPos{}, sourcePos{};
	bool hasContain = false;
	int passengers = 0;
	bool scalarAllows = false;
	bool addFound = false;
	float addValue = 0.0f;
	bool multFound = false;
	float multValue = 1.0f;
	int multType = 0;
	bool multInnate = false;
	bool victimFlanked = false;
	bool sourceFlanked = false;
	bool selfHit = false;
	bool forceKill = false;
	bool hasVictim() override { return victim; }
	float victimDistanceSqr2D(const Coord3D &) override { return distSqr; }
	Coord3D victimPosition() override { return victimPos; }
	Coord3D sourcePosition() override { return sourcePos; }
	bool sourceHasContain() override { return hasContain; }
	int sourcePassengerCount() override { return passengers; }
	bool filterAllowsVictim(const ObjectFilter &f) override { return scalarAllows || (forceKill && f.rule == ObjectFilter::RULE_ANY); }
	bool sourceAdditive(int, float &out) override
	{
		out = addValue;
		return addFound;
	}
	bool sourceMultiplicative(int type, bool innate, float &out) override
	{
		multType = type;
		multInnate = innate;
		out = multValue;
		return multFound;
	}
	bool victimFlankedBySource() override { return victimFlanked; }
	bool sourceFlankedByVictim() override { return sourceFlanked; }
	bool weaponSourceIsVictim() override { return selfHit; }
	std::uint32_t sourcePlayerMask() override { return 4; }
	unsigned weaponSourceID() override { return 9; }
};
}

TEST_CASE("FillDamageInfo: the nugget amount in execution order (RW 0x90E28C, golden vectors)")
{
	WeaponTemplate weapon;
	DamageNugget n;
	n.m_damage = 100.0f;
	NuggetHost host;
	DamageInfo out;
	// a plain direct hit
	REQUIRE(FillDamageInfo(n, weapon, host, false, nullptr, out));
	CHECK(out.m_input.m_amount == 100.0f);
	CHECK(out.m_input.m_sourceID == 9);
	CHECK(out.m_input.m_sourcePlayerMask == 4u);
	CHECK(out.m_input.m_damageType == DAMAGE_UNDEFINED);
	CHECK(out.m_input.m_damageFXOverride == 29);
	CHECK(out.m_input.m_shouldPlayUnderAttackEva);
	CHECK(out.m_input.m_delay == 0.0f);
	CHECK_FALSE(out.m_input.m_kill);

	// taper-off 100 -> 20 over [10, 50] at distance 30: 60; a direct hit takes the full damage
	{
		DamageNugget t;
		t.m_damage = 100.0f;
		t.m_damageTaperOff = 20.0f;
		t.m_radius = 50.0f;
		t.m_minRadius = 10.0f;
		NuggetHost h;
		h.distSqr = 900.0f;
		const Coord3D center{ 1, 2, 3 };
		REQUIRE(FillDamageInfo(t, weapon, h, false, &center, out));
		CHECK(out.m_input.m_amount == 60.0f);
		REQUIRE(FillDamageInfo(t, weapon, h, false, nullptr, out));
		CHECK(out.m_input.m_amount == 100.0f);
		h.distSqr = 100.0f * 100.0f; // beyond the radius: clamped to R, the full taper
		REQUIRE(FillDamageInfo(t, weapon, h, false, &center, out));
		CHECK(out.m_input.m_amount == 20.0f);
		h.distSqr = 1.0f; // inside the min radius: clamped to min, the full damage
		REQUIRE(FillDamageInfo(t, weapon, h, false, &center, out));
		CHECK(out.m_input.m_amount == 100.0f);
	}
	// passengers 3 of 4
	{
		WeaponTemplate pw;
		pw.m_passengerProportionalAttack = true;
		pw.m_maxAttackPassengers = 4;
		NuggetHost h;
		h.hasContain = true;
		h.passengers = 3;
		REQUIRE(FillDamageInfo(n, pw, h, false, nullptr, out));
		CHECK(out.m_input.m_amount == 75.0f);
		h.passengers = 9; // never more than the full damage
		REQUIRE(FillDamageInfo(n, pw, h, false, nullptr, out));
		CHECK(out.m_input.m_amount == 100.0f);
		h.hasContain = false;
		REQUIRE(FillDamageInfo(n, pw, h, false, nullptr, out));
		CHECK(out.m_input.m_amount == 100.0f);
	}
	// DamageScalar: the first matching entry
	{
		DamageNugget s;
		s.m_damage = 100.0f;
		DamageScalarEntry e;
		e.scalar = 0.5f;
		s.m_damageScalar.push_back(e);
		e.scalar = 3.0f;
		s.m_damageScalar.push_back(e);
		NuggetHost h;
		h.scalarAllows = true;
		REQUIRE(FillDamageInfo(s, weapon, h, false, nullptr, out));
		CHECK(out.m_input.m_amount == 50.0f);
		h.scalarAllows = false;
		REQUIRE(FillDamageInfo(s, weapon, h, false, nullptr, out));
		CHECK(out.m_input.m_amount == 100.0f);
	}
	// DAMAGE_ADD then DAMAGE_MULT: 50 + 10, * 1.5 = 90; skipped for a zero amount; AcceptDamageAdd off
	{
		DamageNugget a;
		a.m_damage = 50.0f;
		NuggetHost h;
		h.addFound = true;
		h.addValue = 10.0f;
		h.multFound = true;
		h.multValue = 1.5f;
		REQUIRE(FillDamageInfo(a, weapon, h, false, nullptr, out));
		CHECK(out.m_input.m_amount == 90.0f);
		CHECK(h.multType == ATTRIBUTE_MODIFIER_DAMAGE_MULT);
		CHECK_FALSE(h.multInnate);
		a.m_damage = 0.0f;
		h.multFound = false;
		REQUIRE(FillDamageInfo(a, weapon, h, false, nullptr, out));
		CHECK(out.m_input.m_amount == 0.0f);
		a.m_damage = 50.0f;
		a.m_acceptDamageAdd = false;
		REQUIRE(FillDamageInfo(a, weapon, h, false, nullptr, out));
		CHECK(out.m_input.m_amount == 50.0f);
		// MAGIC uses SPELL_DAMAGE and always counts the innate attributes; the weapon's UseInnateAttributes feeds the other types
		DamageNugget m;
		m.m_damage = 10.0f;
		m.m_damageType = DAMAGE_MAGIC;
		h.addFound = false;
		h.multFound = true;
		h.multValue = 2.0f;
		REQUIRE(FillDamageInfo(m, weapon, h, false, nullptr, out));
		CHECK(out.m_input.m_amount == 20.0f);
		CHECK(h.multType == ATTRIBUTE_MODIFIER_SPELL_DAMAGE);
		CHECK(h.multInnate);
		WeaponTemplate innate;
		innate.m_useInnateAttributes = true;
		REQUIRE(FillDamageInfo(a, innate, h, false, nullptr, out));
		CHECK(h.multType == ATTRIBUTE_MODIFIER_DAMAGE_MULT);
		CHECK(h.multInnate);
	}
	// flanking
	{
		DamageNugget f;
		f.m_damage = 100.0f;
		f.m_flankingBonus = 0.5f;
		NuggetHost h;
		h.victimFlanked = true;
		REQUIRE(FillDamageInfo(f, weapon, h, false, nullptr, out));
		CHECK(out.m_input.m_amount == 150.0f);
		REQUIRE(FillDamageInfo(f, weapon, h, true, nullptr, out)); // noFlank
		CHECK(out.m_input.m_amount == 100.0f);
		DamageNugget g;
		g.m_damage = 100.0f;
		g.m_flankedScalar = 0.005f;
		NuggetHost h2;
		h2.sourceFlanked = true;
		CHECK_FALSE(FillDamageInfo(g, weapon, h2, false, nullptr, out)); // 0.5 < 1.0: the hit is dropped
		g.m_flankedScalar = 0.5f;
		REQUIRE(FillDamageInfo(g, weapon, h2, false, nullptr, out));
		CHECK(out.m_input.m_amount == 50.0f);
	}
	// the delay: DelayTime frames plus the travel time at DamageSpeed
	{
		DamageNugget d;
		d.m_damage = 1.0f;
		d.m_delayTime = 2;
		d.m_damageSpeed = 10.0f;
		NuggetHost h;
		h.victimPos = Coord3D{ 30, 40, 0 };
		REQUIRE(FillDamageInfo(d, weapon, h, false, nullptr, out));
		CHECK(out.m_input.m_delay == 7.0f); // 2 + 50 / 10
		h.victim = false;
		REQUIRE(FillDamageInfo(d, weapon, h, false, nullptr, out));
		CHECK(out.m_input.m_delay == 2.0f);
	}
	// kills: ForceKillObjectFilter and a suicide weapon hitting its own source
	{
		DamageNugget k;
		k.m_damage = 1.0f;
		k.m_forceKillObjectFilter = ObjectFilter();
		k.m_forceKillObjectFilter.rule = ObjectFilter::RULE_ANY;
		NuggetHost h;
		h.forceKill = true;
		REQUIRE(FillDamageInfo(k, weapon, h, false, nullptr, out));
		CHECK(out.m_input.m_kill);
		WeaponTemplate suicide;
		suicide.m_affectsMask |= WEAPON_KILLS_SELF;
		NuggetHost h2;
		h2.selfHit = true;
		REQUIRE(FillDamageInfo(n, suicide, h2, false, nullptr, out));
		CHECK(out.m_input.m_kill);
		NuggetHost h3;
		h3.selfHit = true;
		REQUIRE(FillDamageInfo(n, weapon, h3, false, nullptr, out));
		CHECK_FALSE(out.m_input.m_kill);
	}
	// every field the nugget carries reaches the DamageInfo
	{
		DamageNugget f;
		f.m_damageType = DAMAGE_CAVALRY;
		f.m_deathType = DEATH_BURNED;
		f.m_damageFXType = 3;
		f.m_damageSubType = 2;
		WeaponTemplate fw;
		fw.m_fxTrigger = 2;
		fw.m_shouldPlayUnderAttackEvaEvent = false;
		NuggetHost h;
		REQUIRE(FillDamageInfo(f, fw, h, false, nullptr, out));
		CHECK(out.m_input.m_damageType == DAMAGE_CAVALRY);
		CHECK(out.m_input.m_deathType == DEATH_BURNED);
		CHECK(out.m_input.m_damageFXOverride == 3);
		CHECK(out.m_input.m_damageSubType == 2);
		CHECK(out.m_input.m_fxTrigger == 2);
		CHECK_FALSE(out.m_input.m_shouldPlayUnderAttackEva);
	}
}

TEST_CASE("FillDamageInfo: the taper-off chain stays wide in the x87 register until the single store (the span 6e38 is not narrowed)")
{
	WeaponTemplate weapon;
	DamageNugget n;
	n.m_damage = 3.0e38f;
	n.m_damageTaperOff = -3.0e38f; // Damage - TaperOff = 6e38 overflows binary32, not the register
	n.m_radius = 50.0f;
	n.m_minRadius = 10.0f;
	NuggetHost host;
	host.distSqr = 900.0f; // d = 30: q = 0.5, part = 3e38, amount = 0
	DamageInfo out;
	const Coord3D center{ 0, 0, 0 };
	REQUIRE(FillDamageInfo(n, weapon, host, false, &center, out));
	CHECK(out.m_input.m_amount == 0.0f); // narrowing the span first gives inf and amount = -inf
}
