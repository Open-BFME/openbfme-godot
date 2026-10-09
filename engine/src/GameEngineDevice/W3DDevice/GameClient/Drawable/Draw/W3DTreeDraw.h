// OpenBFME. GPL-3.0.
//
// Module data of W3DTreeDraw, W3DPropDraw and W3DFloorDraw (lane MAPOBJ-1): the three static draw modules the RotWK map objects
// use for trees, shrubs, props and building floors ("bibs"). ZH has W3DTreeDraw only; BFME adds PropDraw and FloorDraw. Only the
// DATA and the static draw (which model, which texture) are ported here; the dynamic tree behaviour is a registered stop.
//
// Sources, in PLAN rule 1 priority order. Facts are tagged TARGET (RotWK game.dat, caveat S-001; "RW 0x..." addresses), DONOR
// (Open-BFME-2 matched files, BFME2 1.06, and the Zero Hour source) or INFERENCE.
//
//   TARGET RW 0xBE2E80  the W3DTreeDraw FieldParse table (24 rows), parse functions RW 0x42EE5E (string), 0x73A429 (duration in
//                       logic frames), 0x42ED00 (real), 0x73A302 (FXList by name), 0x42E558 (bool), 0x42EEFA (percent), 0x42ED1C
//                       (positive non-zero real), 0x42ECB2 (unsigned int); registry entry W3DTreeDraw: createData RW 0x464402,
//                       constructor RW 0x4CE3A8 (object size 0x64), table add RW 0xBE2E80, vtable RW 0xBE2C88.
//   TARGET RW 0x4CE3A8  the constructor defaults: MoveOutwardTime / MoveInwardTime 1 frame, MoveOutwardDistanceFactor 1.0,
//                       DarkeningFactor 0, InitialVelocityPercent 0.2 (RW 0xBDAD78), InitialAccelPercent 0.01 (RW 0xBE5600),
//                       BounceVelocityPercent 0.3 (RW 0xBE29D4), MinimumToppleSpeed 0.5 (RW 0xBD869C), KillWhenFinishedToppling
//                       true, DoTopple false, SinkTime and MorphTime 10 * [RW 0xD9F608] frames (that global holds 5 in the image,
//                       the logic frame rate), SinkDistance 20.0 (RW 0xBDBC6C), TaintedTree false, FadeRate 5, FadeTarget 0x69,
//                       FadeDistance 40.0 (RW 0xBDD28C).
//   TARGET RW 0xBE3340  the W3DPropDraw table (2 rows: ModelName, DistanceFog); constructor RW 0x4CE704 (ModelName empty,
//                       DistanceFog true), createData RW 0x464514, object size 0x10.
//   TARGET RW 0xBE3548  the W3DFloorDraw table (6 rows) that follows the prop table (registry tables [0xBE3340, 0xBE3548]);
//                       constructor RW 0x4CEE74 (a W3DPropDraw data, then the weather map at +0x10, the three flags at +0x1C..0x1E
//                       and FloorFadeRateOnObjectDeath 0.0 at +0x20, the hide-condition vector at +0x24), createData RW 0x46459D.
//   TARGET RW 0x4CF066  WeatherTexture = WEATHER NAME: parseIndexList over the list at RW 0xDA3A84 (NORMAL, SNOWY) then a string;
//                       RW 0x4CEF06  HideIfModelConditions = <model condition flags>: one more ModelConditionFlags is APPENDED
//                       to the vector at +0x24 per line (the retail INI repeats the line, once per condition).
//   DONOR  Open-BFME-2 GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DTreeDrawModuleDataCtor.cpp,
//          W3DPropDrawModuleDataCtor.cpp, W3DFloorDrawCtor.cpp (member layout and names), ZH W3DTreeDraw.cpp.
//
// What is NOT retail knowledge (stops, docs/STOPS.md):
//   S-111  the dynamic tree behaviour (push-aside, topple, sink, morph, fade) and the FXList / tree-template names are stored but
//          not run; the FXList registry is not ported, so a name retail would reject is accepted here.

#pragma once

#include "Common/INI.h"
#include "Common/Module.h"
#include "Common/ModelState.h"

#include <string>
#include <utility>
#include <vector>

// ZH W3DTreeDrawModuleData with the BFME2 / RotWK additions (names kept from the BFME2 donor, INI names in the comments).
class W3DTreeDrawModuleData : public ModuleData
{
public:
	W3DTreeDrawModuleData();
	~W3DTreeDrawModuleData() override = default;
	static void buildFieldParse(MultiIniFieldParse &p);

	std::string m_modelName;       ///< ModelName
	std::string m_textureName;     ///< TextureName
	unsigned m_framesToMoveOutward = 1; ///< MoveOutwardTime (ms in the INI, logic frames here)
	unsigned m_framesToMoveInward = 1;  ///< MoveInwardTime
	float m_maxOutwardMovement = 1.0f;  ///< MoveOutwardDistanceFactor
	float m_darkening = 0.0f;           ///< DarkeningFactor
	std::string m_toppleFX;        ///< ToppleFX (FXList name; S-111)
	std::string m_bounceFX;        ///< BounceFX
	std::string m_stumpName;       ///< StumpName
	float m_initialVelocityPercent = 0.2f;   ///< InitialVelocityPercent
	float m_initialAccelPercent = 0.01f;     ///< InitialAccelPercent
	float m_bounceVelocityPercent = 0.3f;    ///< BounceVelocityPercent
	float m_minimumToppleSpeed = 0.5f;       ///< MinimumToppleSpeed
	bool m_killWhenToppled = true;           ///< KillWhenFinishedToppling
	bool m_doTopple = false;                 ///< DoTopple
	unsigned m_sinkFrames = 50;              ///< SinkTime
	float m_sinkDistance = 20.0f;            ///< SinkDistance
	std::string m_morphTree;                 ///< MorphTree
	unsigned m_morphTime = 50;               ///< MorphTime
	std::string m_morphFX;                   ///< MorphFX
	bool m_taintedTree = false;              ///< TaintedTree
	unsigned m_fadeRate = 5;                 ///< FadeRate
	unsigned m_fadeTarget = 0x69;            ///< FadeTarget
	float m_fadeDistance = 40.0f;            ///< FadeDistance
};

class W3DPropDrawModuleData : public ModuleData
{
public:
	W3DPropDrawModuleData() = default;
	~W3DPropDrawModuleData() override = default;
	static void buildFieldParse(MultiIniFieldParse &p);

	std::string m_modelName;  ///< ModelName
	bool m_distanceFog = true; ///< DistanceFog (constructor default 1, RW 0x4CE710)
};

class W3DFloorDrawModuleData : public W3DPropDrawModuleData
{
public:
	W3DFloorDrawModuleData() = default;
	static void buildFieldParse(MultiIniFieldParse &p);

	std::vector<std::pair<int, std::string>> m_weatherTextures; ///< WeatherTexture = WEATHER (0 NORMAL, 1 SNOWY) NAME, in file order
	std::vector<ModelConditionFlags> m_hideIfModelConditions;   ///< HideIfModelConditions, one set per line
	bool m_staticModelLODMode = false;  ///< StaticModelLODMode
	bool m_forceToBack = false;         ///< ForceToBack
	bool m_startHidden = false;         ///< StartHidden
	float m_floorFadeRateOnObjectDeath = 0.0f; ///< FloorFadeRateOnObjectDeath
};

namespace W3DTreeDrawTables
{
const FieldParse *tree();      ///< RW 0xBE2E80, 24 rows
const FieldParse *prop();      ///< RW 0xBE3340, 2 rows
const FieldParse *floor();     ///< RW 0xBE3548, 6 rows
const char *const *weatherNames(); ///< RW 0xDA3A84: NORMAL, SNOWY (NULL terminated)
} // namespace W3DTreeDrawTables
