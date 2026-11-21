// OpenBFME unit tests: LOCAL-15 War of the Ring texts: Object/DisplayNameStrategic, DescriptionStrategic (issue fork issue 136, TOP20 fork issue 16).
//
// RotWK addresses:
//   Object field parse table: RW 0xDA3DF8 (row 0xDA4048 off 0x3C DescriptionStrategic, 0xDA4078 off 0x54 DisplayNameStrategic; parser RW 0x73D3E0)
//   Fallback at load: RW 0x73CE7E (ThingTemplate::resolveNames):
//     RW 0x73CEB5: if DisplayNameStrategic (+0x58) is empty, copies DisplayName (+0x30) to DisplayNameStrategic
//     RW 0x73CECE: if DescriptionStrategic (+0x40) is empty, copies Description (+0x38) to DescriptionStrategic
//   Getters:
//     RW 0x80EFBD / RW 0x80EFD0: GetDisplayNameStrategic returns [template + 0x58] (or custom hero name)
//   Living World readers:
//     RW 0x9D8AA5 / 0x9D8B1D: StrategicInGameUI::GetDisplayName (HeroArmyName "%s's Army" with hero's DisplayNameStrategic)
//     RW 0x9F6FF7: StrategicInGameUI::ArmyDetailsPanel (rollover _MemberName via GetDisplayNameStrategic RW 0x80EFD0)
//     RW 0x9F986A / 0x9F989F: StrategicDetailsBuildQueue.swf (hover tooltip Title=DisplayNameStrategic, Body=DescriptionStrategic via RW 0x9402E7)
//     RW 0xA07D76 / 0xA082C8: StrategicArmyUnitSwapper.swf (tooltips append DescriptionStrategic to description)
//     RW 0xA006B5: StrategicVeterancy.apt (row unit name via GetDisplayNameStrategic RW 0x80EFD0)

#include "doctest.h"
#include "ObjectTestUtil.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/LivingWorld/LivingWorldUnits.h"

TEST_CASE("LOCAL-15: ThingTemplate strategic texts getters and fallbacks (RW 0x73CEB5, RW 0x73CECE)")
{
	objtest::World w;
	const std::string ini =
		"Object UnitWithStrategic\n"
		"  DisplayName = OBJECT:UnitA\n"
		"  Description = CONTROLBAR:UnitADesc\n"
		"  DisplayNameStrategic = CONTROLBAR:LW_UnitA\n"
		"  DescriptionStrategic = CONTROLBAR:LW_UnitADesc\n"
		"End\n"
		"Object UnitStandardOnly\n"
		"  DisplayName = OBJECT:UnitB\n"
		"  Description = CONTROLBAR:UnitBDesc\n"
		"End\n"
		"Object UnitEmpty\n"
		"End\n";
	REQUIRE(w.load(ini).empty());

	// 1. Template with explicit strategic fields returns strategic values
	const ThingTemplate *t1 = w.things.findTemplate("UnitWithStrategic");
	REQUIRE(t1 != nullptr);
	CHECK(t1->displayName() == "OBJECT:UnitA");
	CHECK(t1->description() == "CONTROLBAR:UnitADesc");
	CHECK(t1->displayNameStrategic() == "CONTROLBAR:LW_UnitA");
	CHECK(t1->descriptionStrategic() == "CONTROLBAR:LW_UnitADesc");

	// 2. Template without strategic fields falls back to standard fields (RW 0x73CEB5, RW 0x73CECE)
	const ThingTemplate *t2 = w.things.findTemplate("UnitStandardOnly");
	REQUIRE(t2 != nullptr);
	CHECK(t2->displayName() == "OBJECT:UnitB");
	CHECK(t2->description() == "CONTROLBAR:UnitBDesc");
	CHECK(t2->displayNameStrategic() == "OBJECT:UnitB");
	CHECK(t2->descriptionStrategic() == "CONTROLBAR:UnitBDesc");

	// 3. Template without either returns empty strings
	const ThingTemplate *t3 = w.things.findTemplate("UnitEmpty");
	REQUIRE(t3 != nullptr);
	CHECK(t3->displayName().empty());
	CHECK(t3->description().empty());
	CHECK(t3->displayNameStrategic().empty());
	CHECK(t3->descriptionStrategic().empty());
}

TEST_CASE("LOCAL-15: LivingWorld army description hero labels (RW 0x9D8AA5, RW 0x9D8B1D)")
{
	objtest::World w;
	const std::string ini =
		"Object HeroWithStrategic\n"
		"  DisplayName = OBJECT:HeroStandard\n"
		"  DisplayNameStrategic = CONTROLBAR:LW_HeroStrategic\n"
		"  KindOf = HERO\n"
		"End\n"
		"Object HeroStandardOnly\n"
		"  DisplayName = OBJECT:HeroOnlyStandard\n"
		"  KindOf = HERO\n"
		"End\n"
		"Object RegularUnit\n"
		"  DisplayName = OBJECT:RegularUnit\n"
		"End\n";
	REQUIRE(w.load(ini).empty());

	LivingWorld::ArmySummaryDescription desc;
	desc.heroFilter.rule = ObjectFilter::RULE_ANY;
	desc.heroFilter.includeNames = { "HeroWithStrategic", "HeroStandardOnly" };

	// Hero with strategic name: heroLabels receives DisplayNameStrategic
	std::vector<std::pair<std::string, int>> entries1 = {
		{ "HeroWithStrategic", 1 },
		{ "RegularUnit", 5 }
	};
	const LivingWorld::ArmyDescription army1 = LivingWorld::describeArmy(desc, w.things, entries1);
	REQUIRE(army1.heroLabels.size() == 1);
	CHECK(army1.heroLabels[0] == "CONTROLBAR:LW_HeroStrategic");

	// Hero without strategic name: heroLabels falls back to DisplayName
	std::vector<std::pair<std::string, int>> entries2 = {
		{ "HeroStandardOnly", 1 }
	};
	const LivingWorld::ArmyDescription army2 = LivingWorld::describeArmy(desc, w.things, entries2);
	REQUIRE(army2.heroLabels.size() == 1);
	CHECK(army2.heroLabels[0] == "OBJECT:HeroOnlyStandard");
}

