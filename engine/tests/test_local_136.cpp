// OpenBFME. GPL-3.0.
//
// Retail 2.01 SpecialPowerTemplate::getName (RW 0x00896AA7 / BFME2 1.06 0x00493313):
// walks Overridable::getFinalOverride() (RW 0x688D3C) before reading m_name (+0x5C).

#include "Common/SpecialPower.h"
#include "doctest.h"

// Expectations from retail RotWK 2.01 RW 0x00896AA7 (call 0x688d3c getFinalOverride; add eax, 0x5c m_name).
TEST_CASE("SpecialPowerTemplate::getName follows final override (RW 0x00896AA7)")
{
	SpecialPowerTemplate base;
	base.m_name = "BaseSpecialPower";

	SpecialPowerTemplate ov1;
	ov1.m_name = "OverrideOne";
	ov1.m_isOverride = true;

	SpecialPowerTemplate ov2;
	ov2.m_name = "OverrideFinal";
	ov2.m_isOverride = true;

	CHECK(base.getName() == "BaseSpecialPower");

	base.m_nextOverride = &ov1;
	CHECK(base.getName() == "OverrideOne");

	ov1.m_nextOverride = &ov2;
	CHECK(base.getName() == "OverrideFinal");
	CHECK(ov1.getName() == "OverrideFinal");
	CHECK(ov2.getName() == "OverrideFinal");
}

// Neighbor check: Overridable::getFinalOverride() without overrides returns self.
TEST_CASE("SpecialPowerTemplate::getName without override returns own name (RW 0x00896AA7)")
{
	SpecialPowerTemplate standalone;
	standalone.m_name = "StandalonePower";
	CHECK(standalone.getFinalOverride() == &standalone);
	CHECK(standalone.getName() == "StandalonePower");
}
