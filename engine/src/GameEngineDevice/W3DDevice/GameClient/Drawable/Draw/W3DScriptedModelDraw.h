// OpenBFME. GPL-3.0.
//
// W3DScriptedModelDraw / W3DHordeModelDraw: the runtime half of the RotWK draw module without any Godot or engine singleton:
// given the parsed module data (W3DModelDraw.h) and the model condition flags the object has over time, it decides which W3D
// model is shown, which animations play on which of the three tracks, at which frame and with which blend weight.
//
// Sources, in PLAN rule 1 priority order. Facts are tagged TARGET (RotWK game.dat, caveat S-001; "RW 0x..." addresses),
// DONOR (Open-BFME-1 matched files and the Zero Hour source) or INFERENCE.
//
//   TARGET RW 0x4BF2D8  replaceModelConditionState(flags): stores the flags as the pending flags (+0x17C), picks the animation
//                       state with findByCondition (RW 0x4B4443) and the model state with RW 0x4B4379, keeps the current state when
//                       the pending target is the one requested and otherwise clears the pending flag, then setModelState(model
//                       state) and select(animation state).
//   TARGET RW 0x4BF15E  select(state, force): the current state again does nothing unless forced; otherwise apply(prev, fraction,
//                       0, 0, force), then OnStateLeave events of the old state and OnStateEnter events of the new one when the state
//                       changed, then the new state's EnteringStateFX when it is still the current one.
//   TARGET RW 0x4BE587  apply(prevState, fraction, A, B, restart): runs the BeginScript, handles the script's transition request and
//                       AllowToContinue, chooses the animation (script label, a single animation, or the weighted pick RW 0x4B4272),
//                       resolves it (RW 0x4BD789), computes the start frame and speed factor, and starts it (RW 0x4B55D5).
//   TARGET RW 0x4B55D5  startAnimation: the three track queue and the blend rules (no blend when AnimationBlendTime is 0 or the
//                       hierarchies differ; a third request waits in track 2; an incoming track marked AnimationMustCompleteBlend
//                       sends the next one to track 2; a blend more than half done is finished first).
//   TARGET RW 0x4BF560  advanceAnimation: pending-state resolution, the per-mode stepping, the blend countdown, completion and the
//                       restart of unconditioned / RESTART_ANIM_WHEN_COMPLETE states, the per-animation fade, the distance speed sync
//                       (RW 0x4B67D4).
//   TARGET RW 0x4B33FB  the pose request: track 0 and track 1 with percentage = 1 - countdown / initial.
//   TARGET RW 0x6D32E4  the client random wrappers (W3DDrawServices.h: shared Common/RandomValue generators, explicit algorithm).
//   TARGET the AnimationMode numbering and the state Flags bit order (RW 0xD99EF8, 0xD99F18).
//   DONOR  Open-BFME-1 Rva0076C080 / Rva0075F0E0 (names for the above), ZH W3DModelDraw.cpp (setModelState render object reuse,
//          bones, hide / show).
//
// What is NOT retail knowledge (stops, docs/STOPS.md):
//   S-090  the setModelState virtual (RW 0x4C451D) and the pieces of the retail routines that need engine objects this core does
//          not have: the shared-model-flags / attached-model update of replaceModelConditionState, the sequential-animation flag
//          (+0x28C), LuaEvent playback (the lists are stored; OnStateEnter / OnStateLeave events are logged), frame events.
//   S-091  the Lua bindings are not decompiled and the Lua runtime is another lane: the script context (previous state name and
//          animation label, fraction, requested transition, AllowToContinue) is read off RW 0x4BE587, the effect of each binding
//          function on it is inferred from the retail scripts.
//   S-093  which generator a clean 2.01 uses for the client random (the RotWK LCG at RW 0x6D315D is a community patch site shared
//          with the logic RNG, S-080 / S-001; the donor carry chain is the likely original): the caller names the algorithm, no
//          default; the initial client seed; the horde draw level global at RW 0xDE3B84+0x1788.
//   S-095  FXEvent / ParticleSysBone playback, EnteringStateFX (logged), the distance sync for objects whose speed comes from a
//          container (RW 0x693A1A), the weapon timing source (a callback in the options).
//   S-096  the camera distance fade (AlphaCameraFade*), BirthFadeTime and StaticSortLevelWhileFading are not applied; the per
//          animation FadeBeginFrame/FadeEndFrame opacity is.
//   S-097  the pose that supplies the pristine bone positions (see W3DModelDrawBones.h).
//   S-098  how hidden sub objects carry over when the render object is recreated (RetainSubObjects), and the sub object name match.

#pragma once

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawBones.h"
#include "Libraries/WWVegas/WW3D2/w3dstops.h"

#include <functional>
#include <limits>
#include <set>
#include <string>
#include <vector>

// One of the three animation tracks (RW 0x4B349F fills one: slot, anim, mustCompleteBlend (+0x19), mode (+0x10), blendTime (+0xC),
// frame (+4), prevFrame (+8), completed (+0x18), direction (+0x14); stride 0x1C from the draw object's +0x110).
struct W3DDrawTrack
{
	const HAnimClass *anim = nullptr;
	std::string clipName;     ///< the registry name the animation was resolved to (HIERARCHY.ANIM)
	float frame = 0.0f;
	float prevFrame = 0.0f;
	float blendTime = 0.0f;   ///< the animation's AnimationBlendTime (frames)
	int mode = W3D_ANIM_MODE_LOOP;
	int direction = 1;        ///< ping pong direction (+1 / -1)
	bool completed = false;   ///< a loop wrapped / a ping pong reflected during the last step
	bool mustCompleteBlend = false;
	std::string label;        ///< the Animation block label
	int animationIndex = -1;  ///< index in the state's animation list
};

// What one render frame needs (the pose request and the model).
struct W3DDrawFrame
{
	std::string modelName;                    ///< as the INI wrote it; empty when the state has no model
	const RenderObjPrototype *model = nullptr;
	const ModelConditionInfo *modelState = nullptr;
	const AnimationStateInfo *animationState = nullptr;
	std::string animationStateName;
	W3DDrawTrack tracks[3];
	int trackCount = 0;                       ///< tracks in use (a prefix: 0, 1, 2 or 3)
	// The pose request (RW 0x4B33FB): with track 0 alone, Anim_Pose(motion0, frame0); while track 1 exists too,
	// Blend_Pose(motion0, frame0, motion1, frame1, percentage) with percentage 0 = motion0 (track 0, the one playing) and
	// 1 = motion1 (track 1, the incoming one): percentage = 1 - countdown / initial.
	const HAnimClass *motion0 = nullptr;
	float frame0 = 0.0f;
	const HAnimClass *motion1 = nullptr;
	float frame1 = 0.0f;
	float blendPercentage = 0.0f;
	bool blending = false;
	float opacity = 1.0f;                     ///< from FadeBeginFrame / FadeEndFrame (S-096: no camera fade)
	bool hidden = false;                      ///< FadeBegin/End ramp reached 0
	std::vector<std::string> hiddenSubObjects; ///< names hidden by scripts (and the sub objects under them), proto order
};

struct W3DScriptedModelDrawOptions
{
	float scale = 1.0f;
	std::vector<std::string> standardPublicBones; ///< GameData StandardPublicBone, supplied by the caller
	bool buildBones = true;                        ///< compute pristine bones / barrels / turrets on every model change
	// What the object's current attack target looks like, for the scripts that ask (CurDrawableIsCurrentTargetKindof,
	// CurDrawableGetCurrentTargetBearing). When unset a script asking for it gets false / 0 and an error is recorded.
	std::function<bool(const std::string &)> targetKindOf;
	std::function<float()> targetBearing;
	// UseWeaponTiming (RW 0x4BEE24-0x4BEEA7): the length of the object's weapon cycle in logic frames (<= 0: no weapon). The speed
	// factor of such an animation becomes natural duration in frames / that number.
	std::function<int()> weaponTimingFrames;
	// Distance (RW 0x4B67D4): the object's speed in world units per logic frame, for the animations that name the distance one
	// loop covers. Unset: no speed sync.
	std::function<float()> objectSpeed;
	// AUDIO-2: Lua CurDrawablePlaySound (RW 0x73529F: an AudioEventRTS for the current drawable added through TheAudio); unset: logged only
	std::function<void(const std::string &)> playSound;
	// The LOD number appended to animation names when the module has StaticModelLODMode / SwitchModelLODMode (RW 0x4B24C4 maps the
	// static game LOD to 0 for high detail, 1 and 2 for lower ones).
	int animationLodNumber = 0;
	// lane FX-2: the client's effect player. enteringStateFX plays a state's EnteringStateFX on the drawable's object (RW 0x4BF222) and returns true when it
	// played (then no S-095 line is raised for it); particleSysBonesPlayed says the client plays the states' ParticleSysBone systems (LiveFX). Unset: logged
	// as S-095, as before.
	std::function<bool(const std::string &)> enteringStateFX;
	std::function<bool()> particleSysBonesPlayed;
	// lane BUILD-4: the drawable's draw modules for the scripts (RW 0x734EF4 -> Drawable::showModule RW 0x6789B4): hasModule answers whether one of them has
	// the tag, setModuleVisible changes that module's visibility. Unset: the requests are only recorded here (isModuleHidden)
	std::function<bool(const std::string &)> hasModule;
	std::function<void(const std::string &, bool)> setModuleVisible;
};

class W3DScriptedModelDraw : public W3DDrawScriptApi
{
public:
	typedef W3DScriptedModelDrawOptions Options;

	// Throws std::runtime_error when no model condition state matches the empty flag set ("all draw modules must have an IDLE
	// state", the W3DModelDraw constructor), or when the data is empty.
	W3DScriptedModelDraw(const W3DModelDrawModuleData &data, W3DDrawAssets &assets, W3DDrawRandom &random, W3DDrawScriptHost *scripts = nullptr,
		const Options &options = Options());
	~W3DScriptedModelDraw() override = default;

	// Drawable::setModelConditionState: the object's current model condition flags (RW 0x4BF2D8). Both lists are re-matched; see the
	// header comment for the pending state rule.
	void setModelConditionFlags(const ModelConditionFlags &flags);

	// Advances the animations by `elapsedMs` of render time (RW 0x4BF560). Call it once per render frame; logic frames (5 per second)
	// only change the flags.
	void advance(double elapsedMs);

	W3DDrawFrame frame() const;
	// lane PERF-1: the same frame written into `f`, reusing its strings' and vector's storage (the render side asks for every animated drawable's frame
	// in every render frame)
	void frame(W3DDrawFrame &f) const;

	// The construction look (lane RENDER-2). The caller passes the object's construction percent (Object + 0x288, 0..100), whether the client is on
	// the first client frame of a logic frame (RW 0x63252F: the cached percent (+0x26C) is refreshed only then), the object's build rate (RW 0x68BD71:
	// 1 / calcTimeToBuild frames, 0 without a controlling player) and the client sub-frame fraction (GameLogic + 0x3C, INFERENCE: RW 0x63256F's
	// sub-frame / client frames per logic frame).
	// RW 0x4B51B5 (vslot 0xF4; Drawable::updateDrawable RW 0x675A1B -> 0x6730E1 calls it on every draw module while the drawable's flags hold
	// ACTIVELY_BEING_CONSTRUCTED; the routine itself tests that bit of its own flags, +0x184 bit 5): with an animation on track 0, a cached percent
	// of 0 puts the frame and the previous frame at 0; otherwise t = min(alpha * rate + percent * 0.01, 1), frame = min(t * (frames - 1), frames - 1),
	// taken only when it is above the current frame (the build-up never runs backwards). Returns true when the frame changed.
	bool updateConstructionFrame(float objectPercent, bool refreshPercent, float buildRate, float alpha);
	// RW 0x4B686D (the transform adjustment of the draw, its last block): when the current animation state has ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT
	// (+0x5C bit 3) and the cached percent is >= 0, the model moves along its own Z axis by height * (alpha * rate + percent) * 0.01 - height (retail
	// adds the fraction rate to the PERCENT here, unlike RW 0x4B51B5; kept), `height` being the object's geometry height above its position
	// (RW 0xAD1920). 0 when the flag is not set. Uses the percent cached by updateConstructionFrame / refreshed by `refreshPercent`.
	float constructionHeightOffset(float objectPercent, bool refreshPercent, float buildRate, float alpha, float height);
	float cachedConstructionPercent() const { return m_constructionPercent; }
	bool adjustsHeightByConstruction() const { return m_curAnimState && m_curAnimState->testFlag(W3D_ACF_ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT); }

	// Speed multiplier of every track (RW +0x9C: the movement speed sync, recomputed at the end of every advance). Exposed for tests.
	void setSpeedMultiplier(float m) { m_speedSync = m; }
	float speedMultiplier() const { return m_speedSync; } // lane SMOOTH-3: the Distance sync (RW +0x9C)

	// Introspection for tests and tools.
	const ModelConditionInfo *currentModelState() const { return m_curModelInfo; }
	// lane QA-1 (the HUD's pick, GameClient/DrawablePick): the render object's prototype now (nullptr: none) and the sub objects hidden on it
	const RenderObjPrototype *currentModel() const { return m_model; }
	const std::set<int> &hiddenSubObjects() const { return m_hiddenSubObjects; }
	const AnimationStateInfo *currentAnimationState() const { return m_curAnimState; }
	int currentAnimationIndex() const { return m_whichAnim; }
	bool pendingStatePending() const { return m_pending; }
	const AnimationStateInfo *pendingTarget() const { return m_pendingTarget; }
	int renderObjectsCreated() const { return m_renderObjectsCreated; }
	const W3DModelBones &bones() const { return m_bones; }
	const std::vector<W3DStopHit> &stops() const { return m_stops; }
	const std::vector<std::string> &errors() const { return m_errors; }
	// Things the state machine did, in order ("enter STATE_Idle", "script ...", "fx FX_Foo", ...), for tests.
	const std::vector<std::string> &log() const { return m_log; }
	bool isSubObjectHidden(int subObjectIndex) const { return m_hiddenSubObjects.count(subObjectIndex) != 0; }
	bool isModuleHidden(const std::string &name) const { return m_hiddenModules.count(name) != 0; }

	// W3DDrawScriptApi
	std::string prevAnimationState() override { return m_ctx.prevState; }
	std::string prevAnimation() override { return m_ctx.prevAnimLabel; }
	float prevAnimFraction() override { return m_ctx.fraction; }
	std::string transitionAnimState() override { return m_ctx.prevState; }
	bool modelCondition(const std::string &name) override;
	void setTransitionAnimState(const std::string &name) override;
	void allowToContinue() override;
	void hideSubObject(const std::string &name) override { setSubObjectHidden(name, true, false); }
	void showSubObject(const std::string &name) override { setSubObjectHidden(name, false, false); }
	void hideSubObjectPermanently(const std::string &name) override { setSubObjectHidden(name, true, true); }
	void showSubObjectPermanently(const std::string &name) override { setSubObjectHidden(name, false, true); }
	void hideModule(const std::string &name) override;
	void showModule(const std::string &name) override;
	void playSound(const std::string &name) override;
	bool playsSound() const override { return static_cast<bool>(m_options.playSound); }
	bool knowsModules() const override { return m_options.hasModule && m_options.setModuleVisible; }
	bool hasModule(const std::string &name) override { return knowsModules() && m_options.hasModule(name); }
	float clientRandomReal(float lo, float hi) override { return drawReal(lo, hi); }
	bool isCurrentTargetKindOf(const std::string &kind) override;
	float currentTargetBearing() override;

protected:
	// For a derived class whose virtual overrides must be in place before the first state is selected (a constructor cannot call
	// them): it passes initialize = false and calls initializeState() at the end of its own constructor.
	W3DScriptedModelDraw(const W3DModelDrawModuleData &data, W3DDrawAssets &assets, W3DDrawRandom &random, W3DDrawScriptHost *scripts,
		const Options &options, bool initialize);
	void initializeState();

	// vtable slot 0x108 (RW 0x4C0917 returns 100000; the horde draw returns its LOD row's MaxRandomAnimations, RW 0x478A6C)
	virtual int maxRandomAnimations() const { return 100000; }
	// vtable slot 0x10C (RW 0x4B3766: GameClientRandomValue(0, frames - 1); the horde draw scales it, RW 0x478A74)
	virtual int randomStartFrame(int frames);

private:
	// The script context the Lua bindings read and write (the structure at [ebp-0x44] of RW 0x4BE587).
	struct ScriptContext
	{
		std::string prevState;      ///< name of the previous state when it is a different state, else ""
		std::string prevAnimLabel;  ///< label of the animation the previous state was playing
		float fraction = 0.0f;      ///< RW 0x4B2785 for the previous animation
		std::string transition;     ///< the transition state a script asked for
		bool allowContinue = false;
	};

	void applyModelState(const ModelConditionInfo *state);
	bool select(const AnimationStateInfo *state, bool force);
	void apply(const AnimationStateInfo *prevState, float fraction, bool restart);
	int pickAnimation(const AnimationStateInfo &state, int avoidIndex, int limit);
	bool isIdleLike(const AnimationStateInfo &state) const; // RW 0x4B424F
	float currentAnimFraction() const;                      // RW 0x4B2785
	const HAnimClass *resolveAnimation(const W3DAnimationInfo &info, std::string *resolved, std::string *error);
	void startAnimation(const HAnimClass *anim, const std::string &clipName, const W3DAnimationInfo &info, int animationIndex, float startFrame,
		float speedFactor);
	void setTrack(int slot, const W3DDrawTrack &track);
	void clearTrack(int slot);
	void promote();
	void initBlend(const W3DDrawTrack &incoming);
	bool trackComplete(const W3DDrawTrack &t, bool flag) const; // RW 0x4B271C
	void stepTrack(W3DDrawTrack &t, float elapsedMs);
	void syncSpeedToMovement();                                // RW 0x4B67D4
	void runScript(const AnimationStateInfo &state, std::string *returnedLabel);
	void fireLuaEvents(const AnimationStateInfo *state, int when);
	void setSubObjectHidden(const std::string &name, bool hide, bool permanent);
	// applies one hide / show to the current render object (the sub object and everything on a bone below it); false when the model lacks it
	bool applySubObjectHidden(const std::string &name, bool hide);
	// lane PROJ-2, TARGET RW 0x4BA74F (the draw's apply of its sub object records, run when either list changed): every non-permanent record (draw +0x5C,
	// RW 0x4B3155) in list order, THEN every permanent record (+0x68, RW 0x4B31C9): a permanent hide wins over a later plain show of a parent bone
	void applySubObjectRecords();
	void stop(const char *id, const std::string &detail);
	void reportRandomSource();
	int drawValue(int lo, int hi);
	float drawReal(float lo, float hi);
	bool m_randomReported = false;
	void error(const std::string &message);

	const W3DModelDrawModuleData &m_data;
	W3DDrawAssets &m_assets;
	W3DDrawRandom &m_random;
	W3DDrawScriptHost *m_scripts;
	Options m_options;

	ModelConditionFlags m_pendingFlags;     // RW +0x17C
	const ModelConditionInfo *m_curModelInfo = nullptr; // RW +0x14
	const AnimationStateInfo *m_curAnimState = nullptr; // RW +0x18
	int m_whichAnim = -1;                   // RW +0x44
	int m_lastAnim = 0;                     // RW +0x88

	const RenderObjPrototype *m_model = nullptr;
	std::string m_modelName; // lower case, the render object identity
	int m_renderObjectsCreated = 0;
	W3DModelBones m_bones;
	std::set<int> m_hiddenSubObjects;

	W3DDrawTrack m_tracks[3];                // RW +0x110, stride 0x1C
	float m_blendCountdown = 0.0f;           // RW +0x90
	float m_blendInitial = 1.0f;             // RW +0x94
	float m_speedFactor = 1.0f;              // RW +0x98 (set by startAnimation)
	float m_speedSync = 1.0f;                // RW +0x9C
	float m_opacity = 1.0f;
	bool m_hiddenByFade = false;
	float m_constructionPercent = 0.0f;      // RW +0x26C (the object's percent, cached on the first client frame of a logic frame)

	bool m_pending = false;                  // RW +0x1CB
	const AnimationStateInfo *m_pendingTarget = nullptr; // RW +0x1CC
	ScriptContext m_ctx;
	bool m_inScript = false;
	// lane RENDER-3: RW 0x4C3B25's permanent list (draw + 0x68): one record per name (_strcmpi), kept whether or not the current render object has the
	// sub object, applied by name to every new render object (a model without it is skipped silently)
	std::vector<std::pair<std::string, bool>> m_permanentSubObjectOps;
	std::vector<std::pair<std::string, bool>> m_subObjectOps; ///< lane PROJ-2: the non-permanent records (RW draw +0x5C), name -> hide, list order
	std::set<std::string> m_hiddenModules;

	std::vector<W3DStopHit> m_stops;
	std::set<std::string> m_stopKeys;
	std::vector<std::string> m_errors;
	std::vector<std::string> m_log;
};

// W3DHordeModelDraw (RotWK registers it as a W3DScriptedModelDraw subclass, spec 4.7): the LodOptions row of the object's current
// detail level (0 LOW, 1 MEDIUM, 2 HIGH; the dynamic game LOD, supplied by the caller) limits the weighted animation pick of
// states that are not idle-like (RW 0x478A6C) and scales a RANDOMSTART start frame by RandomStartFramePercent (RW 0x478A74).
// AllowMultipleModels, MaxRandomTextures and MaxAnimFrameDelta (RW 0x478A05) belong to the horde / formation code, not to one draw
// module.
class W3DHordeModelDraw : public W3DScriptedModelDraw
{
public:
	W3DHordeModelDraw(const W3DHordeModelDrawModuleData &data, int lodLevel, W3DDrawAssets &assets, W3DDrawRandom &random,
		W3DDrawScriptHost *scripts = nullptr, const Options &options = Options());
	int lodLevel() const { return m_lodLevel; }

protected:
	int maxRandomAnimations() const override { return m_lod.maxRandomAnimations; }
	int randomStartFrame(int frames) override;

private:
	static int checkedLevel(int lodLevel);
	W3DLodOptions m_lod;
	int m_lodLevel;
};
