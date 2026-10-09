// OpenBFME. GPL-3.0.
//
// Draw module data of the RotWK 2.01 W3D draw modules, parsed from INI: W3DScriptedModelDraw, W3DHordeModelDraw and
// W3DDefaultDraw, with their ModelConditionState / AnimationState / TransitionState / IdleAnimationState blocks.
// Port of the data half of ZH GameEngineDevice/Source/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.cpp
// (W3DModelDrawModuleData, ModelConditionInfo, W3DAnimationInfo, parseConditionState) with the BFME / RotWK changes below.
//
// Sources, in PLAN rule 1 priority order (every port comment separates target facts, donor facts and inference):
//   TARGET (RotWK game.dat, caveat S-001): the FieldParse tables W3DScriptedModelDraw RW 0xBE1320 (57 rows), ModelConditionState
//     RW 0xBE0A78 (32), AnimationState RW 0xD99F60 (12), Animation RW 0xBE04B8 (11), AttachModel RW 0xBE06E8 (3), Horde
//     LodOptions RW 0xBDCAA0 -> 0xBDCA20 (5); parse bodies ModelConditionState RW 0x4C800E, AnimationState/Idle/Transition
//     RW 0x4C8840, Animation RW 0x4C7A89, Model RW 0x4C21EE, FXEvent RW 0x4C3FD5, ParticleSysBone RW 0x4BCC4D, LuaEvent RW 0x4C3236,
//     LodOptions RW 0x478B94, buildFieldParse RW 0x4C893A (Scripted: that one table) and RW 0x478C74 (Horde: LodOptions, then
//     the Scripted table). tools/draw/extract_draw_tables.py extracts the tables; engine/tests/data/draw/draw_field_tables.json
//     pins them and the unit tests compare this file's tables with it row by row.
//   DONOR (Open-BFME-1 matched files, names kept for traceability): ParseAnimation.cpp (RW 0x4C7A89 twin), Rva0077C390Parse.cpp
//     (AttachModel), ModelConditionInfo_parseModel.cpp, ParseFXEvent.cpp / FXEventParser007764E0.cpp, ParseParticleSysBone.cpp,
//     Rva0077D150ParseConditionState.cpp; ZH W3DModelDraw.cpp parseConditionState (1416-1692).
//   INFERENCE is marked as such.
//
// Differences from ZH, in one place:
//   * ZH's ConditionState / AliasConditionState / TransitionState A B blocks are gone. RotWK splits them: ModelConditionState
//     (model, skeleton, bones, particle bones, FX events, shadow, turrets) and AnimationState (animations, flags, script)
//     are separate lists, each matched against the model condition flags on its own (RW 0x4C800E and RW 0x4C8840 parse them).
//   * A state has exactly one condition set (a 0x4C byte BitFlags copy at the start of the element); there is no alias block.
//   * Selection is first match in definition order over subsets (RW 0x4B4379 / 0x4B4443), not ZH's SparseMatchFinder; see
//     findBestInfo below. The spec (w3d-and-draw.md 4.3) took the BFME2 symbol name for RotWK's behaviour; the RotWK code shows
//     otherwise.
//   * IdleAnimationState carries no conditions and is placed at the FRONT of the animation state list (RW 0x4C88F0-0x4C88FD,
//     insert at begin). TransitionState = NAME stores the name and no conditions (RW 0x4C8894-0x4C88B4) and is appended; it is
//     reached by the name a script asks for (CurDrawableSetTransitionAnimState), not by matching (see W3DScriptedModelDraw.h).
//   * There is no HideSubObject / ShowSubObject INI field in the RotWK tables: sub objects are hidden and shown by the Lua
//     BeginScript bodies (CurDrawableHideSubObject / CurDrawableShowSubObject). The spec's field list (taken from OpenSAGE) was
//     wrong about this; the strings do not occur in game.dat.
//   * BeginScript ... EndScript bodies are stored verbatim (concatenated without separators, RW 0x42D400: the loop appends each
//     line read until the first token equals the INI object's second end token "ENDSCRIPT" at INI+0x42C, compared with stricmp).
//     Lua is not executed here (stop S-091).

#pragma once

#include "Common/INI.h"
#include "Common/Module.h"
#include "Common/ModelState.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

// RW 0xDA12E4: PRIMARY SECONDARY TERTIARY QUATERNARY QUINARY.
enum
{
	W3D_WEAPONSLOT_COUNT = 5
};

// RW 0xD99EF8 (the Animation block's AnimationMode list, parsed with parseIndexList). The BFME stepper switches on these
// numbers (Open-BFME-1 Rva0076C080AdvanceAnimation.cpp:385-411). The spec's open gap "AnimMode name-to-index table not
// extracted" is closed by this table.
enum W3DAnimationMode
{
	W3D_ANIM_MODE_MANUAL = 0,
	W3D_ANIM_MODE_LOOP = 1,
	W3D_ANIM_MODE_ONCE = 2,
	W3D_ANIM_MODE_LOOP_PINGPONG = 3,
	W3D_ANIM_MODE_PLAY_TO_FRAME = 4,
	W3D_ANIM_MODE_LOOP_BACKWARDS = 5,
	W3D_ANIM_MODE_ONCE_BACKWARDS = 6,
};

// RW 0xD99F18, the AnimationState Flags list (parseBitString32: bit i is 1 << i). The ZH order differs (it has
// PRISTINE_BONE_POS_IN_FINAL_FRAME at bit 4), which closes the spec's open ACBits-order gap.
enum W3DAnimationStateFlag
{
	W3D_ACF_RANDOMSTART = 0,
	W3D_ACF_START_FRAME_FIRST = 1,
	W3D_ACF_START_FRAME_LAST = 2,
	W3D_ACF_ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT = 3,
	W3D_ACF_MAINTAIN_FRAME_ACROSS_STATES = 4,
	W3D_ACF_RESTART_ANIM_WHEN_COMPLETE = 5,
	W3D_ACF_MAINTAIN_FRAME_ACROSS_STATES2 = 6,
	W3D_ACF_MAINTAIN_FRAME_ACROSS_STATES3 = 7,
	W3D_ACF_MAINTAIN_FRAME_ACROSS_STATES4 = 8,
};

// One Animation sub block of an AnimationState (RW 0x4C7A89; table RW 0xBE04B8; element defaults RW 0x4BD5E7, donor
// ParseAnimation.cpp:108-125).
struct W3DAnimationInfo
{
	std::string label;                       ///< the token after "Animation =", lower-cased: what a script's returned name is compared with
	std::string labelOriginal;               ///< the same token, case as written
	std::vector<std::string> animationNames; ///< AnimationName is a list (parseAsciiStringVector, RW 0x42EED6); element 0 is the clip, extra words are kept
	float distance = 0.0f;                   ///< Distance: world distance covered by one loop (INI parseReal; the donor says Int)
	int mode = W3D_ANIM_MODE_LOOP;           ///< ONCE (2) when the owning state is an IdleAnimationState (RW 0x4C7AF7: userData 1 stores 2)
	float blendTime = 5.0f;                  ///< AnimationBlendTime, frames; 0 or less means 5 at use (Rva0076C080 :369-376)
	float speedFactorMin = 1.0f;             ///< AnimationSpeedFactorRange: two reals (RW 0x4B262A)
	float speedFactorMax = 1.0f;
	bool mustCompleteBlend = false;          ///< AnimationMustCompleteBlend
	bool useWeaponTiming = false;            ///< UseWeaponTiming
	int priority = 1;                        ///< AnimationPriority, clamped to [0, 100] after the block (RW 0x4C7B15-0x4C7B28)
	float fadeBeginFrame = -1.0f;
	float fadeEndFrame = -1.0f;
	bool fadingIn = false;

	const std::string &clipName() const { static const std::string none; return animationNames.empty() ? none : animationNames[0]; }
};

// ParticleSysBone = BONE SYSTEM [FollowBone: Yes] [HouseColor: Yes] [FXTrigger: X] [Persist: X] [PersistID: n]
// [OnlyIfOnWater: Yes] [OnlyIfOnLand: Yes]  (RW 0x4BCC4D; the key: value words use the colon separators, INI+0x420).
struct ParticleSysBoneInfo
{
	std::string boneName; ///< lower-cased; "none" is kept (retail stores NONE as a bone name here)
	std::string systemName; ///< the particle system template name; not validated against a particle registry (stop S-094)
	bool followBone = false;
	bool houseColor = false;
	int fxTrigger = 0;  ///< index into RW 0xD99E84: NONE CATAPULT_ROCK TREBUCHET_ROCK
	int persist = 0;    ///< index into RW 0xD99E70: NONE HOLD KILL SPAWN
	int persistID = 0;
	bool onlyIfOnWater = false;
	bool onlyIfOnLand = false;
};

// FXEvent = Frame: N FrameStep: N FrameStop: N FireWhenSkipped Name: FXLIST Bone: BONE  (RW 0x4C3FD5; donor FXEventParser007764E0).
struct FXEventInfo
{
	int frame = 0;
	int frameStep = 0;
	int frameStop = 0;
	bool fireWhenSkipped = false;
	std::string fxListName; ///< "Name:"; not validated against the FXList store (stop S-094)
	std::string bone;
};

// LuaEvent = Frame: N Data: TEXT OnStateEnter|OnStateLeave  (RW 0x4C3236).
struct LuaEventInfo
{
	int frame = 0;
	std::string data;
	int when = 0; ///< 0 none, 1 OnStateEnter, 2 OnStateLeave
};

// The shadow description a ModelConditionState creates when Shadow names a type (RW 0x4B92D3 allocates it, 0x28 bytes). The
// ShadowSizeX/Y... fields read into it only when it exists and are silently skipped (no token consumed) otherwise
// (RW 0x4B265F: `if (instance->shadow == 0) return`).
struct ModelShadowInfo
{
	int type = 0;          ///< bits of RW 0xD99EB0 (parseBitString16)
	float sizeX = 0.0f, sizeY = 0.0f, offsetX = 0.0f, offsetY = 0.0f, sunAngle = 0.0f, maxHeight = 0.0f;
	bool overrideLODVisibility = false;
	std::string texture;
};

struct ModelTurretInfo
{
	std::string angleBone;
	std::string pitchBone;
	float artAngle = 0.0f;
	float artPitch = 0.0f;
};

struct W3DRandomTextureInfo // RandomTexture = SRC N DST ... (RW 0x4C7C56, element 0x14 bytes)
{
	std::string name;
	struct Entry
	{
		std::string replacement;
		int weight = 0;
	};
	std::vector<Entry> entries;
};

struct W3DAttachModelInfo // AttachModel block (RW 0x4C7E42; donor Rva0077C390Parse.cpp)
{
	std::string bone;
	Coord3D offset = { 0.0f, 0.0f, 0.0f };
	std::vector<std::pair<std::string, int>> models; ///< (name, probability), probabilities completed to 100 as the donor does
	ModelConditionFlags all;
	ModelConditionFlags positive;
};

// A ModelConditionState / DefaultModelConditionState (RW 0x4C800E). ZH name kept.
struct ModelConditionInfo
{
	ModelConditionFlags conditions; ///< the state's one condition set (empty for the default state)
	std::vector<std::string> modelNames; ///< Model = NAME [ExtraMesh Yes]; a plain Model replaces the list, ExtraMesh Yes appends (RW 0x4C21EE)
	std::string skeleton;           ///< lower-cased
	std::string modelAnimationPrefix;
	std::vector<std::pair<std::string, std::string>> textures; ///< Texture = A B
	std::string portraitImageName;
	std::string buttonImageName;
	std::string overrideTooltip;
	std::vector<std::string> weaponFireFXBone;     ///< indexed by slot (lower-cased; NONE clears)
	std::vector<std::string> weaponLaunchBone;
	std::vector<std::string> weaponRecoilBone;
	std::vector<std::string> weaponMuzzleFlash;
	std::vector<ParticleSysBoneInfo> particleSysBones;
	std::vector<FXEventInfo> fxEvents;
	bool retainSubObjects = false;
	bool hasShadow = false;
	ModelShadowInfo shadow;
	unsigned shadowOpacityStart = 0;      ///< ShadowOpacityStart / Peak / End: parseUnsignedInt, no maximum (RW 0x42ECB2 with userData 0)
	float shadowOpacityFadeInTime = 0.0f;
	unsigned shadowOpacityPeak = 0;
	float shadowOpacityFadeOutTime = 0.0f;
	unsigned shadowOpacityEnd = 0;
	std::vector<ModelTurretInfo> turrets;
	std::vector<std::string> publicBones; ///< every bone name the state's fields mention (ZH addPublicBone), lower-cased, in first-use order

	int getConditionsYesCount() const { return 1; }
	const ModelConditionFlags &getNthConditionsYes(int) const { return conditions; }
	const std::string &modelName() const { static const std::string none; return modelNames.empty() ? none : modelNames[0]; }
	void addPublicBone(const std::string &lowerBoneName);
};

// An AnimationState / IdleAnimationState / TransitionState (RW 0x4C8840; table RW 0xD99F60). BFME retail names the shared
// parser W3DModelDrawModuleData's neighbour; the class is not in the binary's names, so this one is a draft name.
struct AnimationStateInfo
{
	enum Kind { KIND_NORMAL = 0, KIND_IDLE = 1, KIND_TRANSITION = 2 };

	ModelConditionFlags conditions; ///< AnimationState only; empty for Idle and Transition
	Kind kind = KIND_NORMAL;
	std::string stateName;          ///< AnimationState StateName; for TransitionState the header token (a StateName field overrides it)
	std::vector<W3DAnimationInfo> animations;
	int flags = 0;                  ///< bits of W3DAnimationStateFlag
	bool shareAnimation = false;
	std::string enteringStateFX;
	std::string beginScript;        ///< BeginScript ... EndScript, concatenated without separators; a second BeginScript replaces the first
	std::vector<std::string> beginScriptLines; ///< the same lines, one per entry, for tools and tests
	int frameForPristineBonePositions = 0;
	bool allowRepeatInRandomPick = false;
	bool similarRestart = false;
	std::vector<ParticleSysBoneInfo> particleSysBones;
	std::vector<FXEventInfo> fxEvents;
	std::vector<LuaEventInfo> luaEvents;

	int getConditionsYesCount() const { return 1; }
	const ModelConditionFlags &getNthConditionsYes(int) const { return conditions; }
	bool testFlag(int bit) const { return (flags >> bit) & 1; }
};

struct W3DLodOptions // one row of Horde LodOptions (RW 0xBDCA20)
{
	bool allowMultipleModels = false;
	int maxRandomTextures = 0;
	int maxRandomAnimations = 0;
	float maxAnimFrameDelta = 0.0f;
	int randomStartFramePercent = 0;
};

// W3DModelDrawModuleData: the RotWK table of 57 rows (RW 0xBE1320). Named after ZH's class; the binary names only
// W3DScriptedModelDraw (the string "W3DModelDraw" is not in game.dat), which is the registered module.
// Derives from the ModuleData of Common/Module.h so ModuleFactory::bindTypedData<T> can bind the three draw classes (OBJ-1 contract).
class W3DModelDrawModuleData : public ModuleData
{
public:
	W3DModelDrawModuleData();
	~W3DModelDrawModuleData() override = default;

	static void buildFieldParse(MultiIniFieldParse &p);

	// Parsed lists.
	std::vector<ModelConditionInfo> m_conditionStates;
	std::vector<AnimationStateInfo> m_animationStates;
	int m_defaultState = -1; ///< index of the DefaultModelConditionState in m_conditionStates (RW [module+0x4C])

	std::vector<std::string> m_extraPublicBones; ///< ExtraPublicBone (parseAsciiStringVectorAppend, macros expanded)
	std::string m_trackFile;                      ///< TrackMarks (lower-cased)
	std::string m_trackMarksLeftBone, m_trackMarksRightBone;
	std::string m_attachToDrawableBone;           ///< AttachToBoneInAnotherModule (lower-cased)
	ModelConditionFlags m_dependencySharedModelFlags; ///< DependencySharedModelFlags
	int m_projectileBoneFeedbackEnabledSlots = 0; ///< bits over the weapon slot names
	// Constructor defaults (RW 0x4C85E9, stores at +0x54..+0x60): 2.0, 3.0, 0.4, 0.065 (floats at RW 0xBD889C, 0xBDD42C, 0xBE0DE4, 0xBE0DE0)
	float m_initialRecoil = 2.0f, m_maxRecoil = 3.0f, m_recoilDamping = 0.4f, m_recoilSettle = 0.065f;
	int m_minLODRequired = 0;                     ///< index into the StaticGameLODLevel names (Low Medium High UltraHigh VeryLow)
	bool m_okToChangeModelColor = false;
	bool m_animationsRequirePower = true;     ///< constructor default 1 (RW 0x4C8664, +0x6B)
	bool m_useProducerTexture = false;
	bool m_noRotate = false;
	bool m_useFiringArcRotation = false;
	bool m_randomTextureFixedRandomIndex = false;
	bool m_particlesAttachedToAnimatedBones = false;
	bool m_glowEnabled = false;
	bool m_glowEmissive = false;
	bool m_particleBonesCheckDrawable = false;
	bool m_shadowForceDisable = false;
	float m_highDetailLODThreshold = 0.0f, m_lowDetailLODThreshold = 0.0f;
	bool m_switchModelLODMode = false;
	bool m_staticModelLODMode = false;
	bool m_showShadowWhileContained = false;
	bool m_useStandardModelNames = false;
	bool m_useDefaultAnimation = false;
	bool m_alphaRefAnimated = false;
	int m_alphaRefRangeX = 0x60, m_alphaRefRangeY = 0x60; ///< constructor default 0x60 / 0x60 (RW 0x4C8814, 0x4C881A)
	std::string m_wadingParticleSys;
	float m_alphaCameraFadeOuterRadius = 0.0f, m_alphaCameraFadeInnerRadius = 0.0f;
	float m_alphaCameraAtInnerRadius = 1.0f; ///< constructor default 1.0 (float at RW 0xBD1908, store RW 0x4C8722)
	int m_staticSortLevelWhileFading = -1; ///< constructor default -1 (RW 0x4C86DC: or dword ptr [esi + 0x158], 0xffffffff)
	std::uint32_t m_birthFadeTime = 0; ///< logic frames (parseDurationUnsignedInt, RW 0x73A429)
	bool m_birthFadeAdditive = false;
	bool m_zWriteDisableOverride = false;
	bool m_multiPlayerOnly = false;
	bool m_affectedByStealth = true;          ///< constructor default 1 (RW 0x4C873C, +0x15E)
	bool m_highDetailOnly = false;
	std::string m_rampMesh1, m_rampMesh2, m_wallBoundsMesh, m_raisedWallMesh;
	std::string m_embedPortalName, m_embedPortalKind; ///< EmbedPortal NAME ramp|ladder (RW 0x4C30BD; kind kept as the text)
	std::vector<W3DRandomTextureInfo> m_randomTextures;
	std::vector<W3DAttachModelInfo> m_attachModels;
	std::vector<std::string> m_timeOfDayTextureRaw; ///< TimeOfDayTexture / BurntTexture words (RW 0x4C42A8 / 0x4C3321), kept raw (stop S-094)
	std::vector<std::string> m_burntTextureRaw;

	// The two selection rules of the RotWK binary. They are NOT the SparseMatchFinder of ZH / BFME2 (SparseMatchFinder.h, kept as the
	// donor port): both pick the FIRST state in definition order whose condition set is a subset of the queried flags, so the INI
	// order is the precedence ("This is before Attacking so it overrides it", GondorFighter.ini).
	//
	// findBestInfo: RW 0x4B4379. Empty query: the default state when there is one (RW 0x4B43A2), else the first state with no
	// conditions. Non-empty query: the first state (in order) with conditions that are all in the query; else the default state;
	// else the last state with no conditions seen; else nullptr.
	const ModelConditionInfo *findBestInfo(const ModelConditionFlags &c) const;
	// findBestAnimationState (BFME1 name findByCondition): RW 0x4B4443. Pass 1: the first state with a non-empty condition set that
	// is a subset of the query. Pass 2: the first state with no conditions (the IdleAnimationState, which parse put first; a
	// TransitionState also has none, so a module without an idle state can fall back to one). nullptr when neither exists.
	const AnimationStateInfo *findBestAnimationState(const ModelConditionFlags &c) const;
	// A transition state by name (case-insensitive); nullptr if none. See W3DScriptedModelDraw.h for how scripts reach it.
	const AnimationStateInfo *findTransitionState(const std::string &name) const;
	// A state with StateName == name (any kind), first in list order.
	const AnimationStateInfo *findStateByName(const std::string &name) const;
	void clearMatchCaches() const {} // kept for the parse code; the RotWK rules keep no cache

	// Stop S-094: what this module's data holds that the parse stored without a recovered meaning or without checking it against a
	// registry that is not ported. Each entry is one "[S-094] ..." message; empty when nothing applies.
	std::vector<std::string> unverifiedParseItems() const;

	// Mirrors the parse-time consistency checks that retail makes after a whole module is read (none beyond the per-state ones
	// of RW 0x4C800E); reports problems instead of throwing, for tools.
	std::vector<std::string> validate() const;

};

// RotWK registers W3DScriptedModelDraw (RW 0xBDBBB0); its buildFieldParse adds the one table above (RW 0x4C893A).
class W3DScriptedModelDrawModuleData : public W3DModelDrawModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);
};

// W3DHordeModelDraw (RW 0xBDBB9C): LodOptions (RW 0x478B94: LOW | MEDIUM | HIGH, the token compared with stricmp, then the
// 5 row table RW 0xBDCA20 into slot 0/1/2 of the 0x14 byte array at +0x188), then the Scripted table (RW 0x478C74 jumps to 0x4C893A).
class W3DHordeModelDrawModuleData : public W3DScriptedModelDrawModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);
	// The constructor (RW 0x478289) sets the rows to: LOW (no multiple models, 1 texture, 1 animation, 15.0 frames, 0 %),
	// MEDIUM (yes, 2, 2, 8.0, 50 %), HIGH (yes, 999, 999, 0.5, 100 %); the retail INI never sets RandomStartFramePercent.
	W3DHordeModelDrawModuleData();
	W3DLodOptions m_lodOptions[3]; ///< LOW, MEDIUM, HIGH
};

// W3DDefaultDraw (RW 0xBDBBD8): no fields beyond ModuleData; the retail INI bodies are empty ("End" only).
class W3DDefaultDrawModuleData : public ModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);
};

// The nested FieldParse tables, for tools and for the tests that compare them with the binary's (engine/tests/data/draw).
namespace W3DDrawTables
{
const FieldParse *modelCondition();  ///< RW 0xBE0A78, 32 rows
const FieldParse *animationState();  ///< RW 0xD99F60, 12 rows
const FieldParse *animation();       ///< RW 0xBE04B8, 11 rows
const FieldParse *lodOptionsRow();   ///< RW 0xBDCA20, 5 rows
const FieldParse *hordeLodOptions(); ///< RW 0xBDCAA0, 1 row
const FieldParse *moduleTable();     ///< RW 0xBE1320, 57 rows
// The retail name lists (see W3DModelDrawNameLists.inc), NULL terminated.
const char *const *weaponSlotNames();
const char *const *animationStateFlagNames();
const char *const *animationModeNames();
const char *const *shadowTypeNames();
const char *const *staticGameLODNames();
const char *const *fxTriggerNames();
const char *const *persistNames();
const char *const *timeOfDayNames();
} // namespace W3DDrawTables

// Case-insensitive equality used for names that retail compares with stricmp.
bool W3DNamesEqual(const std::string &a, const std::string &b);

// The registered draw module class names this file parses. The three strings are the binary's (RW 0xBDBBB0, 0xBDBB9C, 0xBDBBD8).
enum W3DDrawModuleClass
{
	W3D_DRAW_NONE = 0,
	W3D_DRAW_SCRIPTED_MODEL,
	W3D_DRAW_HORDE_MODEL,
	W3D_DRAW_DEFAULT,
};
W3DDrawModuleClass W3DDrawModuleClassFromName(const std::string &className);

// Parses one module body (the lines after the "Draw = Class Tag" header, through its End) into a freshly created module data
// object of the class. Throws INIException as retail's INI would.
// For W3D_DRAW_SCRIPTED_MODEL and W3D_DRAW_HORDE_MODEL; W3D_DRAW_DEFAULT has its own data class (W3DParseDefaultDrawBody) and throws
// std::logic_error here.
std::unique_ptr<W3DModelDrawModuleData> W3DParseDrawModuleBody(INI *ini, W3DDrawModuleClass cls);
std::unique_ptr<W3DDefaultDrawModuleData> W3DParseDefaultDrawBody(INI *ini);
