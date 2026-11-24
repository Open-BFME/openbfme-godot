// OpenBFME. GPL-3.0.
//
// W3DLightDraw: the dynamic point light a drawable holds for its lifetime (lane LIGHTDRAW-1, stop S-113): the torches of the castle walls and gates
// (GBCastle*, ArnorGBCastle*, MinisWallATrebuchet), GenericTorch, the torch of GondorTownsmanTorch and MordorOrcScout_Unit, Create-a-Hero's
// CAH_FireFlash and the cinematic lights (37 RotWK templates). Client / draw only: nothing here is read by the simulation.
//
// TARGET (RotWK game.dat, read statically):
//   * module data: createData RW 0x464626 (object size 0x48), constructor RW 0x4CF515: Ambient / Diffuse / Specular (0, 0, 0) at +0x08 / +0x14 /
//     +0x20, Radius 20.0 (RW 0xBDBC6C) at +0x2C, Intensity 1.0 (RW 0xBD1908) at +0x30, FlickerAmplitude, FlickerFrequency, PulseAmplitude,
//     PulseFrequency 0 at +0x34 .. +0x40, AttachToBoneInAnotherModule "" at +0x44. Field table RW 0xBE3638 (buildFieldParse RW 0x4CF110, 10 rows and
//     no terminator, S-074): the colours parseRGBColor RW 0x42EF99, the reals parseReal RW 0x42ED00, the bone parseAsciiString RW 0x42EE5E.
//   * module (createInstance RW 0x4645EE, object size 0x20, vtable RW 0xBE36D8): the constructor RW 0x4CF121 zeroes +0x14 / +0x18 / +0x1C, sets the
//     phase +0x10 = WWMath::Random_Float (RW 0xB25D60) * 30.0 (RW 0xBDAE54), takes a light from the scene's pool (RW 0x46F1FD on RW 0xDC7574) with
//     Add_Ref, enables it (RW 0x442846), Ambient = the data's Ambient (light + 0xD4), Diffuse = Diffuse (+ 0xE0), Set_Position (0, 0, 0) (vslot
//     0x58), setFrameFade(0, 0) (RW 0x46D7A9: no ramp, no decay), FarAttenStart 1.0 (+ 0x100), FarAttenEnd = Radius (+ 0x104) and the
//     FAR_ATTENUATION flag (+ 0xC8 |= 1). Specular is parsed and never given to the light.
//   * setHidden (vslot 16, RW 0x4CF29D): light->setEnabled(!hidden). The destructor (RW 0x4CF5B6) disables and releases the light.
//   * doDrawModule (vslot 11, RW 0x4CF2B5), once per Drawable::draw:
//       phase += 1.0 / LOGICFRAMES_PER_SECOND (RW 0xD9F608, 5), SSE float; nothing else without a light or module data;
//       height = Intensity;
//       with a drawable: the light's transform keeps its rotation and takes the drawable's position (RW 0x67679B) as its translation (vslot 0x54);
//       FlickerAmplitude > 0: when the countdown +0x14 is <= 0 it restarts at 5 / FlickerFrequency (SSE) and the step +0x18 =
//         (Random_Float * (A - (-A)) + (-A) + Intensity - height+0x1C) / countdown (x87, stored as float); then countdown -= 1, height = step + +0x1C
//         and +0x1C = height;
//       PulseAmplitude > 0: height += PulseAmplitude * sin(PulseFrequency * phase * 2 pi (RW 0xBDD38C)) (the product SSE, fsin on the x87);
//       the light's Intensity (+ 0xD0) = height.
//   * reactToTransformChange (vslot 38, RW 0x4CF5EE): the light takes the drawable's transform (RW 0x6765B9), or the transform of the bone named by
//     AttachToBoneInAnotherModule when that name is set and the drawable finds it (RW 0x672B5B). The next draw replaces the translation with the
//     drawable's position, so only the bone's rotation is kept while the drawable is drawn.
// DONOR: BFME2 1.06 W3DLightDraw (Open-BFME-2 W3DLightDraw.cpp / W3DLightDrawDoDrawModule.cpp / W3DLightDrawReactToTransformChange.cpp; retail
//   0x000CF91D / 0x000CFC0C / 0x000CFDFB), the same bodies. The light reaches the objects through RTS3DScene::renderOneObject and
//   LightEnvironmentClass::Add_Light (DONOR BFME2 RTS3DSceneRenderOneObject.cpp, lightenvironment.cpp): Ambient and Diffuse times Intensity.
// INFERENCE (stop S-113): see W3DLightDrawStopLine().

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/Module.h"
#include "GameClient/ClientEffects.h"
#include "GameClient/SnowManager.h" // CrtRand: WWMath::Random_Float (RW 0xB25D60)

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class DrawableManager;
struct LogicSnapshot;

class W3DLightDrawModuleData : public ModuleData
{
public:
	W3DLightDrawModuleData() = default;
	static void buildFieldParse(MultiIniFieldParse &p);

	RGBColor m_ambient = { 0.0f, 0.0f, 0.0f };  ///< Ambient, 0..1 (RW +0x08)
	RGBColor m_diffuse = { 0.0f, 0.0f, 0.0f };  ///< Diffuse (+0x14)
	RGBColor m_specular = { 0.0f, 0.0f, 0.0f }; ///< Specular (+0x20): parsed, never given to the light (RW 0x4CF121)
	float m_radius = 20.0f;                     ///< Radius (+0x2C): the light's FarAttenEnd
	float m_intensity = 1.0f;                   ///< Intensity (+0x30)
	float m_flickerAmplitude = 0.0f;            ///< FlickerAmplitude (+0x34)
	float m_flickerFrequency = 0.0f;            ///< FlickerFrequency (+0x38)
	float m_pulseAmplitude = 0.0f;              ///< PulseAmplitude (+0x3C)
	float m_pulseFrequency = 0.0f;              ///< PulseFrequency (+0x40)
	std::string m_attachToBone;                 ///< AttachToBoneInAnotherModule (+0x44)
};

namespace W3DLightDrawTables
{
const FieldParse *light(); ///< RW 0xBE3638, 10 rows
}

// The module's own state (W3DLightDraw + 0x10 .. + 0x1C) and its arithmetic.
class W3DLightDraw
{
public:
	// RW 0x4CF121: the phase from one Random_Float; the light itself is the caller's (W3DLightDrawSet)
	explicit W3DLightDraw(CrtRand &random);
	// RW 0x4CF121's light setup on a pool light
	static void initLight(const W3DLightDrawModuleData &data, LightPulseFx &light);
	// RW 0x4CF2B5 without the transform: the light's new Intensity
	float doDrawModule(const W3DLightDrawModuleData &data, CrtRand &random);

	float phase() const { return m_phase; }       ///< +0x10
	float countdown() const { return m_countdown; } ///< +0x14
	float step() const { return m_step; }         ///< +0x18
	float height() const { return m_height; }     ///< +0x1C

private:
	float m_phase = 0.0f;
	float m_countdown = 0.0f;
	float m_step = 0.0f;
	float m_height = 0.0f;
};

// The W3DLightDraw modules of a game's drawables and the pool lights they hold. Called once per client frame (the effect player's 30 Hz step: retail
// draws each drawable once per Display::draw at the 30 FPS limit).
class W3DLightDrawSet
{
public:
	// One W3DLightDraw module of a drawable, as the frame sees it.
	struct Source
	{
		std::uint64_t key = 0;                        ///< drawable id << 8 | entry index (stable for the module's lifetime)
		const W3DLightDrawModuleData *data = nullptr;
		Coord3D position;                             ///< the drawable's position (RW 0x67679B)
		bool hidden = false;                          ///< the drawable is hidden (setHidden: the light is off)
		bool drawn = true;                            ///< Drawable::draw reaches the module this frame (not under the shroud)
	};
	// The frame: new sources make their module (in the given order), sources gone release their light, the rest set the light's state and draw.
	// `sources` must be sorted by key.
	void update(const std::vector<Source> &sources, LightPulsePool &pool);
	// The drawables' W3DLightDraw entries (DrawEntry kind W3D_DRAWKIND_LIGHT): the slots made since the last call are scanned once (a drawable's
	// entries never change), the known ones each frame. `presented` and `shroudDrawn` decide hidden / drawn as the model draws do
	// (Drawable::presentedLook). A different manager, or a cleared pool, starts the set again.
	void update(const DrawableManager &drawables, const LogicSnapshot *presented, bool shroudDrawn, LightPulsePool &pool);
	void reset();

	size_t size() const { return m_modules.size(); }
	const W3DLightDraw *find(std::uint64_t key) const;
	size_t lightHandle(std::uint64_t key) const; ///< (size_t)-1 when unknown
	CrtRand &random() { return m_random; }

private:
	struct Module
	{
		W3DLightDraw draw;
		size_t handle = 0;
		bool seen = false;
	};
	std::map<std::uint64_t, Module> m_modules;
	std::vector<std::uint64_t> m_candidates; ///< keys of the drawables' light entries, scanned so far
	size_t m_scannedSlots = 1;
	const DrawableManager *m_manager = nullptr;
	std::uint64_t m_poolGeneration = ~0ull;
	std::vector<Source> m_frame;
	CrtRand m_random; ///< WWMath::Random_Float's MSVCR71 rand(), srand's default seed 1 (retail shares the process's generator, S-113)
};

// The run report line of stop S-113 for this module.
std::string W3DLightDrawStopLine();
