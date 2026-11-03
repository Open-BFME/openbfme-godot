// OpenBFME unit tests (lane STEALTHLOOK-1): the ported stealth opacity of the Drawable - the look setter RW 0x6760F9 (BFME2 0x275DCE) and the
// per-client-frame step RW 0x673114 (BFME2 0x272DEE): the pulse formula, the look-change blend, min == max, the hidden flag and the horde imitation.
// The phase draw is handed in (retail: GetGameClientRandomValueReal(0, pi) inside the setter).

#include "doctest.h"

#include "GameClient/StealthLook.h"

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include <cmath>

TEST_CASE("stealthlook1: the pulse formula of RW 0x673114 frame by frame (C0 = 2 s, 0.3 / 0.8)")
{
	StealthLook::State s;
	CHECK(StealthLook::setLook(s, 1, 0.3f, 0.8f, 2.0f, 0.0f, false) == false); // look 0 -> 1, hidden stays false
	CHECK(s.look == 1);
	CHECK(s.cycle == 2.0f);
	CHECK(s.blendLeft == 60); // ftol(30 * 2.0)
	CHECK(s.latched == 1.0f); // the ctor's +0xB4
	CHECK(s.inner == 0.3f);
	CHECK(s.outer == 0.8f);
	CHECK(s.middle == 0.55f);
	CHECK(s.halfRange == 0.25f);
	CHECK(s.phase == 0.0f);
	// frame 1: blend = 0 -> the latched 1.0
	StealthLook::update(s);
	CHECK(s.opacity == 1.0f);
	CHECK(s.blendLeft == 59);
	// the phase stepped (1/30 / 2.0) * pi
	const float stepPhase = (1.0f / 30.0f) / 2.0f * StealthLook::kPi;
	CHECK(s.phase == stepPhase);
	// frame 2 .. 65: the opacity tracks pulse * blend + latched * (1 - blend); the blend and the pulse read +0xCC / +0xD4 BEFORE their step
	float expectedPhase = stepPhase; // +0xCC already holds one step
	for (int n = 2; n <= 65; ++n)
	{
		const int leftBefore = 61 - n < 0 ? 0 : 61 - n;
		const float blend = (60.0f - (float)leftBefore) / 60.0f;
		const float pulse = 0.55f + 0.25f * (float)std::sin(expectedPhase);
		const float want = pulse * blend + 1.0f * (1.0f - blend);
		StealthLook::update(s);
		expectedPhase += (1.0f / 30.0f) / 2.0f * StealthLook::kPi;
		CHECK(s.opacity == want);
		const int leftAfter = leftBefore > 0 ? leftBefore - 1 : 0;
		CHECK(s.blendLeft == leftAfter);
	}
	// after the blend the raw pulse continues: sin(phase) with phase = n * pi / 60
}

TEST_CASE("stealthlook1: a look change latches the opacity and blends (no jump)")
{
	StealthLook::State s;
	StealthLook::setLook(s, 1, 0.3f, 0.8f, 2.0f, 1.0f, false);
	for (int i = 0; i < 20; ++i)
	{
		StealthLook::update(s);
	}
	const float before = s.opacity;
	// the value the latch caught, mirrored (phase drawn = 1.0, 20 updates: blends 0 .. 19/60)
	float ph = 1.0f;
	float wantBefore = 1.0f;
	for (int i = 0; i < 20; ++i)
	{
		const float blend = (float)i / 60.0f;
		wantBefore = (0.55f + 0.25f * (float)std::sin(ph)) * blend + 1.0f * (1.0f - blend);
		ph += (1.0f / 30.0f) / 2.0f * StealthLook::kPi;
	}
	CHECK(before == wantBefore);
	// the reveal (look 1 -> 0): retail's callers pass the same cycle; +0xD0 latches `before`, +0xD4 = 30 * 2.0
	StealthLook::setLook(s, 0, 0.3f, 0.8f, 2.0f, 0.0f, false);
	CHECK(s.latched == before);
	CHECK(s.blendLeft == 60);
	CHECK(s.inner == 1.0f); // reset before the switch
	CHECK(s.outer == 1.0f);
	StealthLook::update(s);
	CHECK(s.opacity == before); // blend 0: the first frame shows the latched opacity - no jump
	for (int n = 2; n <= 61; ++n)
	{
		StealthLook::update(s);
		const float blend = (float)(n - 1) / 60.0f;
		CHECK(s.opacity == before * (1.0f - blend) + blend);
	}
	CHECK(s.opacity == 1.0f);
}

TEST_CASE("stealthlook1: min == max blends to 1.0 (it does not pulse)")
{
	StealthLook::State s;
	s.opacity = 0.4f; // as if a previous pulse had faded it: the setter latches it into +0xD0
	StealthLook::setLook(s, 1, 0.5f, 0.5f, 2.0f, 0.5f, false);
	CHECK(s.inner == s.outer); // the pulse branch never runs
	CHECK(s.latched == 0.4f);
	StealthLook::update(s);
	CHECK(s.opacity == 0.4f); // blend 0: the first frame shows the latch
	for (int n = 2; n <= 61; ++n)
	{
		StealthLook::update(s);
		const float blend = (float)(n - 1) / 60.0f;
		CHECK(s.opacity == 0.4f * (1.0f - blend) + blend);
	}
	CHECK(s.opacity == 1.0f);
}

TEST_CASE("stealthlook1: the stealth opacity composes with another drawable fade")
{
	StealthLook::State s;
	s.opacity = 0.5f;
	CHECK(StealthLook::drawOpacity(0.4f, s) == 0.2f);
	CHECK(StealthLook::drawOpacity(1.0f, s) == 0.5f);
}

TEST_CASE("stealthlook1: look 5 hides only when +0x3AB == 0")
{
	StealthLook::State s;
	CHECK(StealthLook::setLook(s, 5, 0.2f, 0.7f, 2.0f, 0.0f, false) == true); // can hide: +0x43E flips to 1
	CHECK(s.hidden);
	StealthLook::State t;
	CHECK(StealthLook::setLook(t, 5, 0.2f, 0.7f, 2.0f, 0.0f, true) == false); // a module not affected: +0x43E = 0
	CHECK(!t.hidden);
}

TEST_CASE("stealthlook1: the member imitates the parent's +0xB4 / +0x43E / +0x164 only")
{
	StealthLook::State parent;
	StealthLook::setLook(parent, 1, 0.3f, 0.8f, 2.0f, 0.25f, false);
	for (int i = 0; i < 15; ++i)
	{
		StealthLook::update(parent);
	}
	StealthLook::State member;
	member.secondPass = 0.5f; // its own flag: not copied
	CHECK(StealthLook::imitate(member, parent) == false); // hidden unchanged
	CHECK(member.opacity == parent.opacity);
	CHECK(member.look == parent.look);
	parent.hidden = true;
	CHECK(StealthLook::imitate(member, parent) == true);
	CHECK(member.hidden);
	CHECK(member.secondPass == 0.5f); // +0x358 stays the member's own
}

TEST_CASE("stealthlook1: the module data +0x78 query (AffectedByStealth)")
{
	W3DModelDrawModuleData md;
	CHECK(StealthLook::moduleDataAffectedByStealth(&md)); // ctor default 1 (RW 0x4C873C)
	md.m_affectedByStealth = false;
	CHECK(!StealthLook::moduleDataAffectedByStealth(&md));
	CHECK(StealthLook::moduleDataAffectedByStealth(nullptr)); // every other ModuleData answers the base's 1
}
