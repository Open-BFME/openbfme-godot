// OpenBFME. GPL-3.0.
//
// The client-side effect state the FXList nuggets leave behind once they are played: the terrain scorches, the dynamic decals, the
// dynamic-light pulses and the scripted camera shakers. Client-only, presentation only: nothing here is read by the simulation, nothing
// here draws a random number from the logic stream, and a peer without a client sees the same simulation.
//
// TARGET FACTS (RotWK game.dat, caveat S-001) and DONOR facts (the BFME2 1.06 decomp bodies the port follows, whose addresses are BFME2's,
// not RotWK's): the target's nugget call sites are the RW addresses in FXList.cpp; the bodies below are the BFME2 donor's.
//   * TerrainScorch -> TARGET RW 0x5E0045 (TerrainScorchFXNugget::doFXPos) -> TheGameClient vslot 0x58 (addScorch) -> W3DGameClient::addScorch
//     -> BaseHeightMapRenderObjClass::addScorch, DONOR BFME2 0x67EA3 (BaseHeightMapAddScorch.cpp): a duplicate (each |delta| < radius * 0.25, the
//     same radius and the same type) keeps the entry already there and adds none; the buffer holds 500 entries and the oldest is replaced (the donor's ring);
//     the donor's updateScorches 0x678BB fills a terrain-conforming quad grid one unit above the ground, a 3 x 3 atlas cell per Scorches
//     value (SCORCH_MARKS_IN_TEXTURE 9, SCORCH_PER_ROW 3) and the vertex colour terrain ambient + 0.5 * diffuse * the terrain light
//     scale (TARGET RW 0x4670E1, TerrainLightScale.h; the donor body is BaseHeightMapRenderObjClass_updateScorches.cpp).
//   * DynamicDecal -> TARGET RW 0x5E05AB / 0x5DFBEC (DynamicDecalFXNugget::doFXPos) -> TheProjectedShadowManager::addDecal,
//     DONOR BFME2 0x1E0E43 (DynamicDecalFXNuggetDoFXPos.cpp): a projected decal with the opacity envelope of the donor's 0x330A37
//     and the per-frame update 0x10C785 (delay, fade one, peak hold, fade two, lifetime), the colour RGBColor::getAsInt and the
//     texture as written.
//   * LightPulse -> TARGET RW 0x5DFAA3 / 0x5DFAE4 (LightPulseFXNugget) -> TheDisplay vslot 0xBC (createLightPulse),
//     DONOR BFME2 0x4423C (W3DDisplayCreateLightPulse.cpp): a dynamic light from RTS3DScene's pool (donor 0x6F94A), enabled,
//     ambient and diffuse = the nugget's colour, FarAttenStart 1, FarAttenEnd = the radius, FrameFade(increase, decrease)
//     (donor 0x6DF81) whose On_Frame_Update (donor 0x6DE3D; W3DDynamicLightSetFrameFade.cpp) scales the range and the colours:
//     the increase ramp first, then the decay, the light disabled when the decay counter reaches 0; a range below 21 does not
//     make a light at all (the donor's createLightPulse).
//   * CameraShakerVolume -> TARGET RW 0x5DFE90 / 0x5DFECE (CameraShakerVolumeFXNugget) -> TheTacticalView vslot 0xA8 (Add_Camera_Shake),
//     DONOR BFME2 0x65DAD (FXNuggetSlotBodies.cpp / camerashakesystem_shaker.cpp): a shaker over a radius and a duration, amplitude in
//     degrees; CameraShakeSystemClass::CameraShakerClass::Compute_Rotations (donor 0x65B09) adds three sinusoids (Omega 12.5 .. 15
//     turns/s shrinking toward 1 turn/s, Phi 0 .. 2 pi, AXIS_ROTATION 7.5 / 15 / 5 degrees) times the distance and the remaining-time
//     falloff, plus a random fudge of half the intensity. The rotations are presentation
//     only; retail's WWMath::Random_Float is the client's own generator (not TheGameClient's stream).
//
// INFERENCE (stop S-687): the random fudge of Compute_Rotations uses a private deterministic generator here (retail's WWMath
// generator state is not reproduced); the light pulse's contribution to the object shading is a point light with the retail range and a
// linear far attenuation (the D3D light attenuation is linear in the range but its exact scale is not decoded); the decal texture filter is
// the asset loader's.

#pragma once

#include "GameClient/CameraShaker.h"
#include "GameClient/FXList.h"

#include <cstdint>
#include <functional>
#include <vector>

// One terrain scorch (BaseHeightMapRenderObjClass's entry; DONOR BFME2 0x67EA3).
struct ScorchFx
{
	Coord3D position;
	float radius = 0.0f;
	int type = 0; // Scorches: 0 .. 8 (SCORCH_1 .. SCORCH_9), 9 = SHADOW_SCORCH in some data
};

class ScorchBuffer
{
public:
	static constexpr int kMaxScorches = 500; // DONOR BFME2 0x67EA3: 500 entries
	// DONOR BFME2 0x67EA3: a duplicate keeps the entry already there and adds none; the 501st add replaces the oldest
	void add(const Coord3D &pos, float radius, int type);
	const std::vector<ScorchFx> &entries() const { return m_entries; }
	std::uint64_t version() const { return m_version; }
	std::uint64_t additions() const { return m_additions; }
	std::uint64_t duplicates() const { return m_duplicates; }
	std::uint64_t evictions() const { return m_evictions; }
	void clear();

private:
	std::vector<ScorchFx> m_entries; // insertion order, oldest first
	std::uint64_t m_version = 0, m_additions = 0, m_duplicates = 0, m_evictions = 0;
};

// One dynamic decal (DynamicDecalFX), with the client frame it started on. Opacity follows DONOR BFME2 0x10C785.
struct DecalFx
{
	FXDecalDesc desc;
	std::uint32_t startFrame = 0;
	bool expired(std::uint32_t frame) const;
	int opacityAt(std::uint32_t frame) const; // 0 .. 255
};

// One active dynamic light (createLightPulse / W3DLightDraw's light; DONOR BFME2 0x6DF81 / 0x6DE3D).
struct LightPulseFx
{
	Coord3D position;
	RGBColor color{ 0.0f, 0.0f, 0.0f };
	float farAttenStart = 1.0f;
	float farAttenEnd = 0.0f;
	std::uint32_t increaseFrames = 0, decreaseFrames = 0;
	std::uint32_t currentIncrease = 0, currentDecrease = 0;
	float targetEnd = 0.0f;
	RGBColor targetColor{ 0.0f, 0.0f, 0.0f };
	// LightClass::Intensity (light + 0xD0): W3DLightDraw writes it every draw (RW 0x4CF479); the light environment multiplies the diffuse and the ambient by
	// it (DONOR BFME2 lightenvironment.cpp Init_From_Point_Or_Spot_Light). A pulse keeps the new light's 1.
	float intensity = 1.0f;
	bool enabled = false;
	// DONOR BFME2 0x6DE3D On_Frame_Update: one client frame; returns false when the light expired on this frame
	bool step();
	// the scale On_Frame_Update last wrote (0 .. 1)
	float scale() const { return m_scale; }
	float range() const;
	RGBColor colorNow() const;

private:
	float m_scale = 1.0f;
};

class LightPulsePool
{
public:
	static constexpr size_t kMaxLights = 64; // the pool grows in retail; 64 active pulses is already far past any retail FXList
	// DONOR BFME2 0x4423C: a range below 21 makes no light; the new light takes a disabled pool slot (the retail pool reuses disabled lights)
	bool add(const Coord3D &pos, const RGBColor &color, float radius, std::uint32_t increaseFrames, std::uint32_t decreaseFrames);
	void step();
	const std::vector<LightPulseFx> &lights() const { return m_lights; }
	unsigned long long additions() const { return m_additions; }
	unsigned long long refusedRange() const { return m_refusedRange; }
	unsigned long long overflow() const { return m_overflow; }
	size_t enabledCount() const; // the lights still lit (expired ones stay in the pool as reusable slots)
	void clear();

	// lane LIGHTDRAW-1: the lights W3DLightDraw holds for its drawable's lifetime (RW 0x4CF16B: RTS3DScene::getADynamicLight + Add_Ref; released by the
	// destructor). They are kept apart from the pulses: a pulse never takes one (retail's pool can hand a hidden draw's disabled light to a pulse, S-113)
	// and there is no cap (retail's pool grows). A handle stays valid until releaseDrawLight or clear(); generation() changes on clear().
	size_t acquireDrawLight();
	void releaseDrawLight(size_t handle);
	LightPulseFx &drawLight(size_t handle) { return m_drawLights[handle]; }
	const std::vector<LightPulseFx> &drawLights() const { return m_drawLights; } // released slots are disabled
	size_t drawLightCount() const { return m_drawLights.size() - m_freeDrawLights.size(); }
	std::uint64_t generation() const { return m_generation; }

	// lane LIGHTDRAW-1: the lights the object shaders get (they have four slots): the enabled pulses and draw lights whose sphere (position, range())
	// `visible` accepts, nearest to `eye` first (pulses before draw lights on a tie, then pool order), at most `max`. INFERENCE (S-687): retail picks per
	// object (RTS3DScene::renderOneObject: every enabled dynamic light whose sphere meets the object's, the four strongest of those and the scene lights by
	// LightEnvironmentClass::Add_Light); one set for the whole frame is what four global shader slots allow.
	std::vector<const LightPulseFx *> select(const Coord3D &eye, size_t max, const std::function<bool(const Coord3D &, float)> &visible) const;

private:
	std::vector<LightPulseFx> m_lights;
	std::vector<LightPulseFx> m_drawLights;
	std::vector<size_t> m_freeDrawLights;
	std::uint64_t m_generation = 0;
	unsigned long long m_additions = 0, m_refusedRange = 0, m_overflow = 0;
};
