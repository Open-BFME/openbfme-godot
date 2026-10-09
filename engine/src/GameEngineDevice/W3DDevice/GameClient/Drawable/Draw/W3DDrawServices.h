// OpenBFME. GPL-3.0.
//
// The services a W3D draw module needs from the rest of the engine, as small interfaces so the draw logic is a plain C++ unit
// (no Godot, no global singletons):
//   * W3DDrawRandom        ZH GetGameClientRandomValue / GetGameClientRandomValueReal (render-side random, never the logic RNG)
//   * W3DDrawAssets        Create_Render_Obj and the animation lookup
//   * W3DDrawScriptApi     what a Lua BeginScript may call back into (CurDrawable* functions)
//   * W3DDrawScriptHost    the Lua runtime that runs a BeginScript body (not part of this lane; stop S-091)

#pragma once

#include "Common/RandomValue.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------------
// Random
// ---------------------------------------------------------------------------------------------------------------------
class W3DDrawRandom
{
public:
	virtual ~W3DDrawRandom() = default;
	// RW 0x6D32E4 / ZH RandomValue.cpp: delta = hi - lo + 1 as an unsigned; delta == 0 gives hi.
	virtual int value(int lo, int hi) = 0;
	// RW 0x6D33AB / ZH RandomValue.cpp: delta <= 0 gives hi.
	virtual float real(float lo, float hi) = 0;
	// Acceptance stop lines the source of the numbers carries (PLAN "Acceptance stops"); the draw reports them on its first draw.
	virtual std::vector<std::string> unverified() const { return {}; }
};

// The client random of the draw modules: GetGameClientRandomValue / ...Real (RW 0x6D32E4 / 0x6D33AB), an instance of the shared
// generators of Common/RandomValue.h (lane HORDE-1) of its OWN state, never the logic generator's: rendering must not consume
// simulation randomness (PLAN). The generator algorithm has NO default: the caller names it.
//
// TARGET (RotWK game.dat on this machine, community patched, S-001): the client wrappers call RW 0x6D315D, the same 21 byte LCG
// replacement (followed by 98 NOPs) the logic wrappers call; the client seed array is at RW 0xDA1C74 with the same initial words as
// the logic one (RandomValue.h). The real draw multiplies by theMultFactor = 2^-32 (RW 0xDE4A6C, RandomValue.cpp).
// DONOR (BFME2 1.06, clean): the client wrappers 0x63404A / 0x634111 call the six-word carry generator 0x633EC3.
// INFERENCE: a clean RotWK 2.01 most likely uses the carry generator, the LCG being the patch (one shared patch site). Which one a
// retail replay used is unproven: stop S-093, cross-referenced to S-080 (logic RNG) and S-001 (patched binary).
class W3DClientRandom : public W3DDrawRandom
{
public:
	explicit W3DClientRandom(RandomAlgorithm algorithm) : m_generator(algorithm) {}
	RandomAlgorithm algorithm() const { return m_generator.algorithm(); }
	// seedRandom of the algorithm on the client array (RW 0x6D31D4 / ZH seedRandom); without a call the static initial array stands
	void seed(std::uint32_t seed) { m_generator.seedRandom(seed); }
	std::uint32_t next() { return m_generator.randomValue(); }
	const GameLogicRandom::Seed &seedArray() const { return m_generator.seedArray(); }
	int value(int lo, int hi) override { return m_generator.getValue(lo, hi, nullptr, 0); }
	float real(float lo, float hi) override { return m_generator.getValueReal(lo, hi, nullptr, 0); }
	std::vector<std::string> unverified() const override;

private:
	GameLogicRandom m_generator; ///< its own state (separate from TheGameLogicRandom)
};

// ---------------------------------------------------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------------------------------------------------
// What an AnimationName list needs to be resolved (the BFME resolver, RW 0x4BD789 with 0x4B9100 and 0x54C76D):
//   for each name in `names`, in order: replace the first "#(MODEL)" in it by `modelAnimationPrefix` (RW 0x4BD4CE), build the registry
//   name from `skeleton` (empty: the name alone; else skeleton + [number] + "." + name + [number], the numbers only when `numbered`),
//   and take the first that exists; when `numbered` and `skeleton` is not empty a missing numbered name is retried as
//   skeleton + "." + name. The registry key is "a*" + the lower-cased name (RW 0x54C76D). `skeleton` and `modelAnimationPrefix` are
//   the Skeleton and ModelAnimationPrefix of the current ModelConditionState (RW 0x4BEC4F: ModelConditionInfo +0x58 and +0x70).
struct W3DAnimationLookup
{
	std::string skeleton;
	std::string modelAnimationPrefix;
	std::vector<std::string> names;
	bool numbered = false;
	int number = 0;
};

class W3DDrawAssets
{
public:
	virtual ~W3DDrawAssets() = default;
	// Create_Render_Obj by (case-insensitive) name. nullptr + *error when it is not registered.
	virtual const RenderObjPrototype *model(const std::string &modelName, std::string *error) = 0;
	// The first resolvable animation of the lookup; *resolvedName the registry name used. nullptr + *error when none exists.
	virtual const HAnimClass *animation(const W3DAnimationLookup &lookup, std::string *resolvedName, std::string *error) = 0;
};

class WW3DDrawAssets : public W3DDrawAssets
{
public:
	explicit WW3DDrawAssets(WW3DAssetManager &assets) : m_assets(assets) {}
	const RenderObjPrototype *model(const std::string &modelName, std::string *error) override { return m_assets.Create_Render_Obj(modelName, error); }
	const HAnimClass *animation(const W3DAnimationLookup &lookup, std::string *resolvedName, std::string *error) override;

private:
	WW3DAssetManager &m_assets;
};

// ---------------------------------------------------------------------------------------------------------------------
// Scripts
// ---------------------------------------------------------------------------------------------------------------------
// The functions the retail Lua scripts call on the current drawable. The set is every CurDrawable* / draw script function that
// the 3,500 retail BeginScript bodies use (counted over data\\INI: HideSubObject 2280, SetTransitionAnimState 2009,
// PrevAnimationState 1456, ShowSubObject 835, PrevAnimation 250, AllowToContinue 237, PlaySound 220, PrevAnimFraction 118,
// HideSubObjectPermanently 116, HideModule 45, GetClientRandomNumberReal 39, ShowSubObjectPermanently 26, Modelcondition 25,
// ShowModule 17, IsCurrentTargetKindof 5, GetCurrentTargetBearing 2, TransitionAnimState 1). What each does is INFERENCE
// (stop S-091): the Lua bindings are not decompiled; the names and the way the retail scripts use them are the evidence.
class W3DDrawScriptApi
{
public:
	virtual ~W3DDrawScriptApi() = default;
	virtual std::string prevAnimationState() = 0;                    ///< StateName of the animation state we came from ("" if none)
	virtual std::string prevAnimation() = 0;                         ///< label of the Animation that was playing in it ("" if none)
	virtual float prevAnimFraction() = 0;                            ///< how far that animation had played, 0..1 (0 when none)
	virtual std::string transitionAnimState() = 0;                   ///< StateName of the transition state being played ("" if none)
	virtual bool modelCondition(const std::string &name) = 0;        ///< is the named model condition flag set (false for an unknown name)
	virtual void setTransitionAnimState(const std::string &name) = 0; ///< play TransitionState `name` first, then the state that was being entered
	virtual void allowToContinue() = 0;                              ///< do not start this state's animation: the one playing continues
	virtual void hideSubObject(const std::string &name) = 0;
	virtual void showSubObject(const std::string &name) = 0;
	virtual void hideSubObjectPermanently(const std::string &name) = 0; ///< also kept across render object changes
	virtual void showSubObjectPermanently(const std::string &name) = 0;
	virtual void hideModule(const std::string &name) = 0;
	virtual void showModule(const std::string &name) = 0;
	virtual void playSound(const std::string &name) = 0;
	// AUDIO-2: true when playSound reaches the audio manager (the drawable gave the draw module an audio service); false: the request is only logged
	virtual bool playsSound() const { return false; }
	// lane BUILD-4: the drawable's draw module tags (RW 0x734EF4: CurDrawableShowSubObject / HideSubObject give a name that is a draw module's tag to
	// Drawable::showModule RW 0x6789B4 first). knowsModules: the api can answer and apply; false: hasModule answers false and the module requests are
	// only recorded on this draw module
	virtual bool knowsModules() const { return false; }
	virtual bool hasModule(const std::string &name) { (void)name; return false; }
	virtual float clientRandomReal(float lo, float hi) = 0;          ///< GetClientRandomNumberReal
	virtual bool isCurrentTargetKindOf(const std::string &kind) = 0;
	virtual float currentTargetBearing() = 0;
};

class W3DDrawScriptHost
{
public:
	virtual ~W3DDrawScriptHost() = default;
	// Runs one BeginScript body. *returned receives the string the script returns (an Animation label), empty when it returns
	// nothing. Returns false and sets *error when the script fails.
	virtual bool run(const std::string &script, W3DDrawScriptApi &api, std::string *returned, std::string *error) = 0;
};
