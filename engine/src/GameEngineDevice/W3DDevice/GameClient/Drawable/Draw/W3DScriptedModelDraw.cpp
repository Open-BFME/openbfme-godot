// OpenBFME. GPL-3.0. See W3DScriptedModelDraw.h for the sources and the stops.

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"

#include "Common/AsciiString.h"
#include "Common/Audio/AudioLog.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
// RW 0x4BED76: `mov edx, 0x1D0; test edx, flags`: bits 4, 6, 7, 8 = the four MAINTAIN_FRAME_ACROSS_STATES flags (RW 0xD99F18)
const int kAllMaintainFrameFlags = (1 << W3D_ACF_MAINTAIN_FRAME_ACROSS_STATES) | (1 << W3D_ACF_MAINTAIN_FRAME_ACROSS_STATES2) |
	(1 << W3D_ACF_MAINTAIN_FRAME_ACROSS_STATES3) | (1 << W3D_ACF_MAINTAIN_FRAME_ACROSS_STATES4);
static_assert(kAllMaintainFrameFlags == 0x1d0, "RW 0x4BED8A tests 0x1D0");

bool anyMaintainFrameFlagSet(int flags) { return (flags & kAllMaintainFrameFlags) != 0; }
bool commonMaintainFrameFlagSet(int a, int b) { return (a & b & kAllMaintainFrameFlags) != 0; }

bool turretNamesDiffer(const ModelConditionInfo *a, const ModelConditionInfo *b)
{
	if (a->turrets.size() != b->turrets.size())
	{
		return true;
	}
	for (size_t i = 0; i < a->turrets.size(); ++i)
	{
		if (a->turrets[i].angleBone != b->turrets[i].angleBone || a->turrets[i].pitchBone != b->turrets[i].pitchBone)
		{
			return true;
		}
	}
	return false;
}

const float kDefaultBlendFrames = 5.0f; // RW 0xBDF964
} // namespace

W3DScriptedModelDraw::W3DScriptedModelDraw(const W3DModelDrawModuleData &data, W3DDrawAssets &assets, W3DDrawRandom &random, W3DDrawScriptHost *scripts,
	const Options &options)
	: W3DScriptedModelDraw(data, assets, random, scripts, options, true)
{
}

W3DScriptedModelDraw::W3DScriptedModelDraw(const W3DModelDrawModuleData &data, W3DDrawAssets &assets, W3DDrawRandom &random, W3DDrawScriptHost *scripts,
	const Options &options, bool initialize)
	: m_data(data)
	, m_assets(assets)
	, m_random(random)
	, m_scripts(scripts)
	, m_options(options)
{
	if (initialize)
	{
		initializeState();
	}
}

void W3DScriptedModelDraw::initializeState()
{
	if (!m_data.findBestInfo(ModelConditionFlags()))
	{
		// W3DModelDraw::W3DModelDraw (ZH W3DModelDraw.cpp:1741-1746): "all draw modules must have an IDLE state"
		throw std::runtime_error("*** ASSET ERROR: all draw modules must have an IDLE state");
	}
	if (m_data.m_alphaCameraFadeOuterRadius > 0.0f || m_data.m_birthFadeTime != 0 || m_data.m_staticSortLevelWhileFading != -1)
	{
		stop("S-096", "the module sets AlphaCameraFade* / BirthFadeTime / StaticSortLevelWhileFading; the camera distance fade and the birth fade are not applied");
	}
	if (!m_data.m_attachModels.empty() || m_data.m_dependencySharedModelFlags.any())
	{
		stop("S-090", "the module has AttachModel / DependencySharedModelFlags; the attached model update of replaceModelConditionState (RW 0x4BF2D8) is not ported");
	}
	// stop S-094: what the parse stored without a recovered meaning (and the constructor members that were not recovered)
	for (const std::string &item : m_data.unverifiedParseItems())
	{
		const std::string prefix = "[S-094] ";
		stop("S-094", item.compare(0, prefix.size(), prefix) == 0 ? item.substr(prefix.size()) : item);
	}
	setModelConditionFlags(ModelConditionFlags());
}

void W3DScriptedModelDraw::stop(const char *id, const std::string &detail)
{
	const std::string key = std::string(id) + detail;
	if (!m_stopKeys.insert(key).second)
	{
		return;
	}
	W3DStopHit hit;
	hit.Id = id;
	hit.Message = W3D_Stop_Message(id, detail);
	m_stops.push_back(hit);
}

// Every draw goes through these two: the first one reports the provenance of the random source (stop S-093).
void W3DScriptedModelDraw::reportRandomSource()
{
	if (m_randomReported)
	{
		return;
	}
	m_randomReported = true;
	for (const std::string &line : m_random.unverified())
	{
		const std::string prefix = "S-093: ";
		stop("S-093", line.compare(0, prefix.size(), prefix) == 0 ? line.substr(prefix.size()) : line);
	}
}

int W3DScriptedModelDraw::drawValue(int lo, int hi)
{
	reportRandomSource();
	return m_random.value(lo, hi);
}

float W3DScriptedModelDraw::drawReal(float lo, float hi)
{
	reportRandomSource();
	return m_random.real(lo, hi);
}

void W3DScriptedModelDraw::error(const std::string &message)
{
	m_errors.push_back(message);
}

int W3DScriptedModelDraw::randomStartFrame(int frames)
{
	return drawValue(0, frames - 1); // RW 0x4B3766
}

// ---------------------------------------------------------------------------------------------------------------------
// Model condition flags in, states out (RW 0x4BF2D8)
// ---------------------------------------------------------------------------------------------------------------------
void W3DScriptedModelDraw::setModelConditionFlags(const ModelConditionFlags &flags)
{
	m_pendingFlags = flags; // RW 0x4BF420: rep movsd 0x13 into +0x17C, every time
	const ModelConditionInfo *modelInfo = m_data.findBestInfo(flags);
	const AnimationStateInfo *next = m_data.findBestAnimationState(flags);
	// RW 0x4BF442-0x4BF467: when a state is pending and the flags ask for that very state, the current state (the transition being
	// played) stays; any other request clears the pending state and takes over at once.
	if (m_pending && m_curAnimState && next == m_pendingTarget)
	{
		next = m_curAnimState;
	}
	else
	{
		m_pendingTarget = nullptr;
		m_pending = false;
	}
	if (modelInfo)
	{
		applyModelState(modelInfo);
	}
	else
	{
		error("no model condition state matches " + ModelCondition::describe(flags));
	}
	if (next)
	{
		select(next, false);
	}
}

// ZH W3DModelDraw::setModelState (W3DModelDraw.cpp:2911-3181), the render object part (the RotWK virtual is RW 0x4C451D, not
// transcribed: S-090): recreate the render object when the model or the turret bones differ, validate the bones either way.
void W3DScriptedModelDraw::applyModelState(const ModelConditionInfo *state)
{
	if (state == m_curModelInfo)
	{
		return; // ZH: "if the requested state is the current state ... just punt"
	}
	const std::string newName = AsciiStringUtil::lowered(state->modelName());
	const bool recreate = m_curModelInfo == nullptr || newName != m_modelName || turretNamesDiffer(state, m_curModelInfo);
	m_curModelInfo = state;
	if (!state->particleSysBones.empty() || !state->fxEvents.empty())
	{
		stop("S-095", "model state " + ModelCondition::describe(state->conditions) + " has ParticleSysBone / FXEvent entries, which are stored but not played");
	}
	if (recreate)
	{
		++m_renderObjectsCreated;
		m_modelName = newName;
		m_model = nullptr;
		if (!newName.empty())
		{
			std::string err;
			m_model = m_assets.model(state->modelName(), &err);
			if (!m_model)
			{
				error("model " + state->modelName() + " is not available: " + err);
			}
		}
		// hidden sub objects: a new render object starts with its meshes' own HIDDEN flags; RetainSubObjects keeps what scripts hid
		m_hiddenSubObjects.clear();
		if (m_model)
		{
			for (size_t i = 0; i < m_model->SubObjects.size(); ++i)
			{
				const RenderSubObject &s = m_model->SubObjects[i];
				if (s.Type == RenderSubObject::SUB_MESH && s.Mesh && s.Mesh->Is_Hidden())
				{
					m_hiddenSubObjects.insert((int)i);
				}
			}
		}
		// lane RENDER-3: the permanent records (RW 0x4C3B25, draw + 0x68) apply by name to the new render object; a record whose sub object this model
		// lacks stays recorded for the next one (e.g. the BeingConstructed BeginScript's HideSubObjectPermanently("V1") reaches the finished model)
		m_subObjectOps.clear(); // a new render object drops the plain hide / show records (S-098: RetainSubObjects is not decoded)
		for (const auto &op : m_permanentSubObjectOps)
		{
			applySubObjectHidden(op.first, op.second);
		}
		if (state->retainSubObjects)
		{
			stop("S-098", "RetainSubObjects is set on a state; what it keeps across a render object change is not decoded, hidden sub objects are reset");
		}
	}
	if (m_options.buildBones)
	{
		stop("S-097", "pristine bone positions are taken from the bind pose, not from the animation retail poses the model with");
		m_bones.build(*state, m_model, m_data.m_extraPublicBones, m_options.standardPublicBones, m_options.scale, nullptr, 0.0f);
		for (const std::string &b : m_bones.missingBones)
		{
			error("bone " + b + " not found in model " + m_modelName);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Animation state selection (RW 0x4BF15E select, RW 0x4BE587 apply)
// ---------------------------------------------------------------------------------------------------------------------
bool W3DScriptedModelDraw::select(const AnimationStateInfo *state, bool force)
{
	if (m_curAnimState == state)
	{
		// RW 0x4BF166-0x4BF18D: the same state does nothing unless forced (a SimilarRestart state only differs in the refresh call the
		// retail makes, which has no effect here)
		if (!force)
		{
			return false;
		}
	}
	const AnimationStateInfo *prev = m_curAnimState;
	const float fraction = currentAnimFraction();
	m_curAnimState = state;
	m_log.push_back("enter " + (state->stateName.empty() ? std::string("<unnamed>") : state->stateName));
	const bool particlesPlayed = m_options.particleSysBonesPlayed && m_options.particleSysBonesPlayed();
	if (!state->fxEvents.empty() || (!state->particleSysBones.empty() && !particlesPlayed))
	{
		stop("S-095", "state " + state->stateName + (particlesPlayed ? " has FXEvent entries, which are stored but not played" : " has FXEvent / ParticleSysBone entries, which are stored but not played"));
	}
	apply(prev, fraction, force);
	if (prev != m_curAnimState)
	{
		fireLuaEvents(prev, 2);
		fireLuaEvents(m_curAnimState, 1);
	}
	if (m_curAnimState == state && !state->enteringStateFX.empty())
	{
		m_log.push_back("fx " + state->enteringStateFX); // RW 0x4BF222: the FX is played at the object when the state is still current
		if (!(m_options.enteringStateFX && m_options.enteringStateFX(state->enteringStateFX)))
		{
			stop("S-095", "state " + state->stateName + " has EnteringStateFX " + state->enteringStateFX + ", which is logged, not played");
		}
	}
	return true;
}

void W3DScriptedModelDraw::fireLuaEvents(const AnimationStateInfo *state, int when)
{
	if (!state)
	{
		return;
	}
	for (const LuaEventInfo &e : state->luaEvents)
	{
		if (e.when == when)
		{
			m_log.push_back(std::string("lua ") + (when == 1 ? "enter " : "leave ") + e.data); // RW 0x4B91E2 runs them through Lua; not run here (S-090)
			stop("S-090", "state " + state->stateName + " has an OnState" + (when == 1 ? "Enter" : "Leave") + " LuaEvent (" + e.data + "); it is logged, not run");
		}
		else if (e.when == 0)
		{
			stop("S-090", "state " + state->stateName + " has a frame LuaEvent; frame events are not played");
		}
	}
}

void W3DScriptedModelDraw::runScript(const AnimationStateInfo &state, std::string *returnedLabel)
{
	returnedLabel->clear();
	if (state.beginScript.empty())
	{
		return;
	}
	if (!m_scripts)
	{
		stop("S-091", "state " + state.stateName + " has a BeginScript and no Lua host is attached: its transition request, returned animation label and sub object changes do not happen");
		return;
	}
	m_log.push_back("script " + state.stateName);
	m_inScript = true;
	std::string err;
	std::string returned;
	const bool ok = m_scripts->run(state.beginScript, *this, &returned, &err);
	m_inScript = false;
	if (!ok)
	{
		error("script of state " + state.stateName + " failed: " + err);
		return;
	}
	*returnedLabel = returned;
}

// RW 0x4B424F
bool W3DScriptedModelDraw::isIdleLike(const AnimationStateInfo &state) const
{
	return !state.conditions.any() || state.shareAnimation || state.testFlag(W3D_ACF_RESTART_ANIM_WHEN_COMPLETE);
}

void W3DScriptedModelDraw::apply(const AnimationStateInfo *prevState, float fraction, bool restart)
{
	const AnimationStateInfo *S = m_curAnimState;
	if (!S || !m_curModelInfo)
	{
		return; // RW 0x4BE5A2 / 0x4BE5AB
	}
	const bool renderObject = m_model != nullptr;
	const bool hasScript = !S->beginScript.empty();
	const int prevIdx = m_whichAnim;
	if (restart)
	{
		prevState = nullptr; // RW 0x4BE5E3
		m_lastAnim = 0;
	}
	const int n = (int)S->animations.size();
	if (n <= 0 && !hasScript)
	{
		// RW 0x4BE602-0x4BE62C
		if (renderObject)
		{
			clearTrack(0);
			clearTrack(1);
		}
		m_whichAnim = -1;
		return;
	}
	if (renderObject && n == 0)
	{
		clearTrack(0);
		clearTrack(1);
	}
	m_whichAnim = -1; // RW 0x4BE65F

	if (hasScript)
	{
		// RW 0x4BE66C-0x4BE70E: the context the Lua bindings fill in
		m_ctx = ScriptContext();
		if (prevState)
		{
			// the retail bounds the previous state's animation list with THIS state's animation count (RW 0x4BE693); the previous
			// list's own size is checked as well here so a shorter list cannot be read past its end
			if (prevIdx >= 0 && prevIdx < n && prevIdx < (int)prevState->animations.size())
			{
				m_ctx.prevAnimLabel = prevState->animations[(size_t)prevIdx].label;
			}
			if (prevState != S)
			{
				m_ctx.prevState = prevState->stateName;
				m_ctx.fraction = fraction;
			}
		}
		std::string label;
		runScript(*S, &label);

		// RW 0x4BE704-0x4BE846: a requested transition
		const bool transitionAsked = !m_ctx.transition.empty();
		if (m_pending && transitionAsked)
		{
			m_pending = false; // RW 0x4BE721
			m_pendingTarget = nullptr;
		}
		if (transitionAsked && !m_pending)
		{
			const AnimationStateInfo *transition = m_data.findStateByName(m_ctx.transition); // any state of that name, in list order
			if (transition)
			{
				m_pending = true;
				m_pendingTarget = S;
				m_log.push_back("transition " + transition->stateName);
				select(transition, false); // RW 0x4BE7C4
				return;
			}
			// lane FX-3 (QA-1 U9): RW 0x4BE74B .. 0x4BE846 finds no state of that name, logs retail's own line (format RW 0xBDFF78, the object's
			// template name or "NoObject", through the log object 0xDC62C0 at level 2 when its flag +0x9F57 is set, RW 0x4380F0) and goes on as if no
			// transition was asked. A retail data defect (RohanEntBirch / RohanEntFir / RohanTreeBerd ask for TRANS_Sprout, which only RohanEntAsh
			// defines): counted as such (classifyDrawMessage), not an error
			// (retail's second %s is the object's template name, which the collector prefixes; the asking state is added here)
			error("W3DScriptedModelDraw::adjustAnimation: Unable to find transition state named '" + m_ctx.transition + "' (asked by state " + S->stateName + ")");
		}

		// RW 0x4BE846-0x4BE8B2: AllowToContinue. When the animation that was playing is not finished, the previous state stays current
		// and the new one becomes the pending target.
		if (prevState && m_ctx.allowContinue)
		{
			W3DDrawTrack &latest = m_tracks[1].anim ? m_tracks[1] : m_tracks[0];
			if (!trackComplete(latest, false))
			{
				m_pending = true;
				m_pendingTarget = S;
				latest.completed = false;
				fireLuaEvents(S, 2);
				m_curAnimState = prevState;
				fireLuaEvents(prevState, 1);
				m_whichAnim = 0;
				return;
			}
		}

		// RW 0x4BE8B7-0x4BE961: the script's returned string names an Animation of this state
		if (!label.empty())
		{
			const std::string lower = AsciiStringUtil::lowered(label);
			int found = -1;
			for (int i = 0; i < n; ++i)
			{
				if (S->animations[(size_t)i].label == lower)
				{
					found = i;
					break;
				}
			}
			if (found >= 0)
			{
				m_whichAnim = found;
			}
			else
			{
				error("Cannot find animation named " + label + " in state " + S->stateName);
				m_whichAnim = 0; // RW 0x4BE961
			}
		}
	}

	// RW 0x4BE97E-0x4BE9FA
	if (n < 1)
	{
		m_whichAnim = -1;
		return;
	}
	if (n == 1)
	{
		m_whichAnim = 0;
	}
	else if (m_whichAnim < 0)
	{
		// RW 0x4BE9B1-0x4BE9FA: the animation to avoid is the previous one when the same state starts again, unless the state allows
		// repeats; the pick limit is the draw's (virtual 0x108) for a state that is not idle-like, 9999 otherwise
		const int avoid = (prevState == S && !S->allowRepeatInRandomPick) ? prevIdx : -1;
		const int limit = isIdleLike(*S) ? 9999 : maxRandomAnimations();
		m_whichAnim = pickAnimation(*S, avoid, limit);
	}

	// RW 0x4BE9FD-0x4BEA4B: the same state and the same animation again does not restart while it plays (unless SimilarRestart)
	const int idx = m_whichAnim;
	const W3DAnimationInfo &info = S->animations[(size_t)idx];
	if (prevState == S && !S->similarRestart && idx == prevIdx)
	{
		if (m_tracks[0].anim && m_tracks[1].anim)
		{
			return;
		}
		if (!trackComplete(m_tracks[0], false))
		{
			return;
		}
	}

	// RW 0x4BEA51-0x4BEC98: resolve; a missing animation is reported and the others of the state are tried in order
	std::string resolved;
	std::string err;
	const HAnimClass *handle = resolveAnimation(info, &resolved, &err);
	if (!handle)
	{
		error("animation " + (info.animationNames.empty() ? info.labelOriginal : info.animationNames[0]) + " of state " + S->stateName + ": " + err);
		if (n > 1)
		{
			for (int i = 0; i < n; ++i)
			{
				if (i == m_whichAnim)
				{
					continue;
				}
				if (const HAnimClass *other = resolveAnimation(S->animations[(size_t)i], &resolved, &err))
				{
					handle = other;
					m_whichAnim = i;
					break;
				}
			}
		}
	}
	m_lastAnim = m_whichAnim; // RW 0x4BEC9B
	if (!handle)
	{
		return;
	}

	// RW 0x4BECAC-0x4BEDD1: the start frame
	const int frames = handle->Get_Num_Frames();
	int startFrame = 0;
	if (info.mode == W3D_ANIM_MODE_ONCE_BACKWARDS || info.mode == W3D_ANIM_MODE_LOOP_BACKWARDS)
	{
		startFrame = frames - 1;
	}
	if (S->testFlag(W3D_ACF_RANDOMSTART))
	{
		startFrame = randomStartFrame(frames);
	}
	else if (S->testFlag(W3D_ACF_START_FRAME_FIRST))
	{
		startFrame = 0;
	}
	else if (S->testFlag(W3D_ACF_START_FRAME_LAST))
	{
		startFrame = frames - 1;
	}
	else if (anyMaintainFrameFlagSet(S->flags) && prevState && prevState != S && anyMaintainFrameFlagSet(prevState->flags) &&
		commonMaintainFrameFlagSet(S->flags, prevState->flags) && fraction >= 0.0f)
	{
		startFrame = (int)((float)frames * fraction); // RW 0x4BEDBA
	}

	// RW 0x4BEDD1-0x4BEEA7: the speed factor, then UseWeaponTiming
	float speed = 1.0f;
	if (info.speedFactorMin != 1.0f || info.speedFactorMax != 1.0f)
	{
		speed = drawReal(info.speedFactorMin, info.speedFactorMax);
	}
	if (info.useWeaponTiming)
	{
		if (m_options.weaponTimingFrames)
		{
			// lane ANIM-1: < 0 is "no current weapon" (RW 0x4BEE3F: the speed factor is left alone); 0 would be retail's division by zero (cvtsi2ss / divss:
			// an infinite speed), not reproduced: reported as stop S-1580 and the speed factor is left alone
			const int weaponFrames = m_options.weaponTimingFrames();
			if (weaponFrames > 0)
			{
				const float naturalMs = (float)frames * 1000.0f / handle->Get_Frame_Rate();
				const int naturalFrames = (int)(naturalMs * 0.005f); // RW 0xD9F610
				speed = (float)naturalFrames / (float)weaponFrames;
			}
			else if (weaponFrames == 0)
			{
				stop("S-1580", "animation " + info.labelOriginal + " of state " + S->stateName + " has UseWeaponTiming and the weapon's cycle is 0 frames (retail divides by zero); played at its own speed");
			}
		}
		else
		{
			stop("S-095", "animation " + info.labelOriginal + " of state " + S->stateName + " has UseWeaponTiming and no weapon timing source was given");
		}
	}
	if (info.distance != 0.0f && !m_options.objectSpeed)
	{
		stop("S-095", "animation " + info.labelOriginal + " of state " + S->stateName + " has Distance and no object speed source was given");
	}
	startAnimation(handle, resolved, info, m_whichAnim, (float)startFrame, speed);
}

// RW 0x4B4272, Rva0075F0E0 (retail 0x0075F0E0): weight = AnimationPriority, minus 1 for the animation to avoid; the draw is
// GetGameClientRandomValue(0, total - 1) (RW 0x6D32E4) over the first min(size, limit) entries.
int W3DScriptedModelDraw::pickAnimation(const AnimationStateInfo &state, int avoidIndex, int limit)
{
	int size = (int)state.animations.size();
	if (size > limit)
	{
		size = limit;
	}
	if (size < 2)
	{
		return 0;
	}
	int total = 0;
	for (int i = 0; i < size; ++i)
	{
		int weight = state.animations[(size_t)i].priority;
		if (i == avoidIndex)
		{
			--weight;
		}
		total += weight;
	}
	int pick = drawValue(0, total - 1);
	for (int i = 0; i < size; ++i)
	{
		int weight = state.animations[(size_t)i].priority;
		if (i == avoidIndex)
		{
			--weight;
		}
		pick -= weight;
		if (pick < 0)
		{
			return i;
		}
	}
	return 0;
}

// RW 0x4B2785: the fraction of the animation the render object currently plays (track 0), -1 when it plays none.
float W3DScriptedModelDraw::currentAnimFraction() const
{
	const W3DDrawTrack &t = m_tracks[0].anim ? m_tracks[0] : m_tracks[1];
	if (!m_curAnimState || !m_model || !t.anim)
	{
		return -1.0f;
	}
	const int numFrames = t.anim->Get_Num_Frames();
	if (t.frame < 0.0f)
	{
		return 0.0f;
	}
	if (t.frame >= (float)numFrames)
	{
		return 1.0f;
	}
	return numFrames > 1 ? t.frame / ((float)numFrames - 1.0f) : 0.0f;
}

const HAnimClass *W3DScriptedModelDraw::resolveAnimation(const W3DAnimationInfo &info, std::string *resolved, std::string *err)
{
	W3DAnimationLookup lookup;
	lookup.skeleton = m_curModelInfo->skeleton;
	lookup.modelAnimationPrefix = m_curModelInfo->modelAnimationPrefix;
	lookup.names = info.animationNames;
	// RW 0x4BEA54-0x4BEA86: StaticModelLODMode (+0x135) and SwitchModelLODMode (+0x134) number the names
	lookup.numbered = m_data.m_staticModelLODMode || m_data.m_switchModelLODMode;
	lookup.number = m_options.animationLodNumber;
	return m_assets.animation(lookup, resolved, err);
}

// ---------------------------------------------------------------------------------------------------------------------
// The three tracks (RW 0x4B55D5 startAnimation, 0x4B349F set, 0x4B3531 clear, 0x4B359F move)
// ---------------------------------------------------------------------------------------------------------------------
void W3DScriptedModelDraw::setTrack(int slot, const W3DDrawTrack &track)
{
	m_tracks[slot] = track;
}

void W3DScriptedModelDraw::clearTrack(int slot)
{
	m_tracks[slot] = W3DDrawTrack();
}

// RW 0x4B57F8 / 0x4B5732 / 0x4BF66F: the countdown starts at max(1, min(blend, frames - 1)), blend being the incoming track's
// AnimationBlendTime (5 when that is not positive).
void W3DScriptedModelDraw::initBlend(const W3DDrawTrack &incoming)
{
	const float blend = incoming.blendTime > 0.0f ? incoming.blendTime : kDefaultBlendFrames;
	m_blendInitial = m_blendCountdown = std::max(1.0f, std::min(blend, (float)incoming.anim->Get_Num_Frames() - 1.0f));
}

// ESC(0,1), then ESC(1,2) with a restarted blend when a third track waits (RW 0x4BF92D-0x4BF9D1).
void W3DScriptedModelDraw::promote()
{
	m_tracks[0] = m_tracks[1];
	clearTrack(1);
	if (m_tracks[2].anim)
	{
		m_tracks[1] = m_tracks[2];
		clearTrack(2);
		initBlend(m_tracks[1]);
	}
	else
	{
		m_blendCountdown = 0.0f; // RW 0x4BF9D1
	}
}

void W3DScriptedModelDraw::startAnimation(const HAnimClass *anim, const std::string &clipName, const W3DAnimationInfo &info, int animationIndex, float startFrame,
	float speedFactor)
{
	m_speedFactor = speedFactor; // RW 0x4B55E9
	W3DDrawTrack t;
	t.anim = anim;
	t.clipName = clipName;
	t.frame = startFrame;
	t.prevFrame = startFrame + 1.0f * -0.00001f; // RW 0x4BEEE5: startFrame + direction * -1e-5
	t.blendTime = info.blendTime;
	t.mode = info.mode;
	t.direction = 1;
	t.completed = false;
	t.mustCompleteBlend = info.mustCompleteBlend;
	t.label = info.label;
	t.animationIndex = animationIndex;

	// RW 0x4B55FB-0x4B5633: blending needs every playing track to be on the same hierarchy (HAnim vtable +0x34, the hierarchy)
	bool sameHierarchy = true;
	for (int i = 0; i < 2; ++i)
	{
		if (m_tracks[i].anim && AsciiStringUtil::compareNoCase(m_tracks[i].anim->Get_HName(), anim->Get_HName()) != 0)
		{
			sameHierarchy = false;
		}
	}
	if (t.blendTime != 0.0f && sameHierarchy)
	{
		if (!m_tracks[0].anim)
		{
			if (!m_tracks[1].anim)
			{
				setTrack(0, t); // nothing playing: no blend (RW 0x4B5664)
				m_blendInitial = 1.0f;
				m_blendCountdown = 0.0f;
				return;
			}
			// RW 0x4B56FC: the incoming track moves to track 0 first
			m_tracks[0] = m_tracks[1];
			clearTrack(1);
		}
		else if (m_tracks[1].anim)
		{
			if (m_tracks[1].mustCompleteBlend)
			{
				setTrack(2, t); // RW 0x4B56A0: AnimationMustCompleteBlend queues the request in track 2
				return;
			}
			if (m_blendCountdown / m_blendInitial < 0.5f)
			{
				// RW 0x4B56D9-0x4B56F6: a blend more than half done is finished first (ESC(0,1)); otherwise the incoming
				// track is replaced
				m_tracks[0] = m_tracks[1];
				clearTrack(1);
			}
		}
		setTrack(1, t);
		initBlend(t);
		return;
	}
	// RW 0x4B57A9: no blend (AnimationBlendTime 0, or another hierarchy): the animation replaces track 0 and track 1 is cleared
	setTrack(0, t);
	clearTrack(1);
	m_blendInitial = 1.0f;
	m_blendCountdown = 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Stepping (RW 0x4BF560 advanceAnimation)
// ---------------------------------------------------------------------------------------------------------------------
// RW 0x4B271C, complete0075B980 (Rva0076C080AdvanceAnimation.cpp:178-190) with flag = the pending state flag
bool W3DScriptedModelDraw::trackComplete(const W3DDrawTrack &t, bool flag) const
{
	if (!m_model || !t.anim)
	{
		return true;
	}
	switch (t.mode)
	{
	case 0:
	case 1:
	case 3:
	case 5:
		return flag ? t.completed : false;
	case 2:
		return t.frame >= (float)t.anim->Get_Num_Frames() - 1.0f;
	case 6:
		return t.frame <= 0.0f;
	default:
		return true;
	}
}

void W3DScriptedModelDraw::stepTrack(W3DDrawTrack &t, float elapsedMs)
{
	// RW 0x4BF6FD-0x4BF8B4 (the donor Rva0076C080AdvanceAnimation.cpp:378-412 is the same code)
	const float delta = t.anim->Get_Frame_Rate() * m_speedSync * m_speedFactor * elapsedMs * 0.001f;
	const float last = (float)t.anim->Get_Num_Frames() - 1.0f;
	t.prevFrame = t.frame;
	t.completed = false;
	if (last <= 0.0f)
	{
		// a one frame animation: nothing to advance; looping modes count as having wrapped
		t.frame = 0.0f;
		t.completed = (t.mode == 1 || t.mode == 2 || t.mode == 3 || t.mode == 5 || t.mode == 6);
		return;
	}
	switch (t.mode)
	{
	case 1:
		t.frame += delta;
		if (t.frame > last)
		{
			const int loops = (int)(t.frame / last);
			t.frame -= (float)loops * last;
			t.completed = true;
		}
		break;
	case 2:
		t.frame += delta;
		if (t.frame > last)
		{
			t.frame = last;
			t.completed = true;
		}
		break;
	case 3:
		t.frame += (float)t.direction * delta;
		if (t.direction == 1)
		{
			if (t.frame > last)
			{
				const int loops = (int)(t.frame / last);
				const float wrapped = (float)loops * last;
				t.frame = (wrapped + last) - t.frame;
				t.direction = -1;
				t.completed = true;
			}
		}
		else if (t.frame < 0.0f)
		{
			const int loops = (int)(std::fabs(t.frame) / last);
			if (loops > 0)
			{
				t.frame = (float)loops * last + t.frame;
			}
			t.direction = 1;
			t.frame = -t.frame;
			t.completed = true;
		}
		break;
	case 5:
		t.frame -= delta;
		if (t.frame < 0.0f)
		{
			const int loops = (int)(std::fabs(t.frame) / last);
			const float wrapped = (float)loops * last;
			t.frame = t.frame + (wrapped + last);
			t.completed = true;
		}
		break;
	case 6:
		t.frame -= delta;
		if (t.frame < 0.0f)
		{
			t.frame = 0.0f;
			t.completed = true;
		}
		break;
	default:
		break; // MANUAL (0) and PLAY_TO_FRAME (4) do not advance
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// The construction look (lane RENDER-2): RW 0x4B51B5 and the last block of RW 0x4B686D
// ---------------------------------------------------------------------------------------------------------------------
bool W3DScriptedModelDraw::updateConstructionFrame(float objectPercent, bool refreshPercent, float buildRate, float alpha)
{
	static const int kActivelyBeingConstructed = ModelCondition::indexOf("ACTIVELY_BEING_CONSTRUCTED"); // the binary's name table, bit 0x45
	W3DDrawTrack &t = m_tracks[0];
	if (kActivelyBeingConstructed < 0 || !m_pendingFlags.test(kActivelyBeingConstructed) || !t.anim)
	{
		return false; // RW 0x4B51BE..0x4B51E8
	}
	if (refreshPercent)
	{
		m_constructionPercent = objectPercent; // RW 0x4B51F4..0x4B5203
	}
	const float before = t.frame;
	if (m_constructionPercent == 0.0f)
	{
		// RW 0x4B521D: frame and previous frame 0
		t.prevFrame = 0.0f;
		t.frame = 0.0f;
		return before != 0.0f;
	}
	// RW 0x4B5232..0x4B52A6 (x87 under the 24 bit precision state; one rounding per stored value)
	float u = alpha * buildRate + m_constructionPercent * 0.01f;
	const float one = 1.0f;
	u = (u <= one) ? u : one;
	const float last = (float)(t.anim->Get_Num_Frames() - 1);
	float frame = u * last;
	frame = (frame <= last) ? frame : last;
	if (t.frame <= frame && frame != t.frame)
	{
		stop("S-461", "the build-up frame of a structure follows the construction percent (RW 0x4B51B5); the sub-frame fraction (GameLogic + 0x3C) and the build rate are "
					  "inferred, the construction dust (ParticleSysBone) is not played (S-095)");
		t.prevFrame = t.frame;
		t.frame = frame;
		return true;
	}
	return false;
}

float W3DScriptedModelDraw::constructionHeightOffset(float objectPercent, bool refreshPercent, float buildRate, float alpha, float height)
{
	if (!m_curAnimState || !m_curAnimState->testFlag(W3D_ACF_ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT))
	{
		return 0.0f; // RW 0x4B6BBA..0x4B6BC9
	}
	if (refreshPercent)
	{
		m_constructionPercent = objectPercent; // RW 0x4B6BE0..0x4B6BF5
	}
	if (!(m_constructionPercent >= 0.0f))
	{
		return 0.0f; // RW 0x4B6C05: below 0 (an instant build, -1) nothing moves
	}
	// RW 0x4B6C12..0x4B6C5A
	const float p = alpha * buildRate + m_constructionPercent;
	return height * p * 0.01f - height;
}

// RW 0x4B67D4 / 0x4B2808: f80 = 1, then, for an animation that names the distance one loop covers and an object that moves, the
// ratio of the animation's natural duration to the time the object needs for that distance (200 ms per logic frame).
void W3DScriptedModelDraw::syncSpeedToMovement()
{
	m_speedSync = 1.0f;
	if (!m_curAnimState || m_whichAnim < 0 || m_whichAnim >= (int)m_curAnimState->animations.size() || !m_options.objectSpeed || !m_tracks[0].anim)
	{
		return;
	}
	const float distance = m_curAnimState->animations[(size_t)m_whichAnim].distance;
	if (!(distance > 0.0f))
	{
		return;
	}
	const float speed = m_options.objectSpeed();
	if (!(speed > 0.01f)) // RW 0xBE5600
	{
		return;
	}
	const float desiredMs = distance / speed * 200.0f; // RW 0xD9F614
	const float naturalMs = (float)m_tracks[0].anim->Get_Num_Frames() * 1000.0f / m_tracks[0].anim->Get_Frame_Rate();
	if (naturalMs > 0.0f && desiredMs > 0.0f)
	{
		m_speedSync = naturalMs / desiredMs;
	}
}

void W3DScriptedModelDraw::advance(double elapsedMsD)
{
	const float elapsedMs = (float)elapsedMsD;
	if (!m_model || !m_curAnimState)
	{
		return; // RW 0x4BF56C / 0x4BF575
	}
	if (!m_tracks[0].anim && !m_tracks[1].anim)
	{
		if (m_pending)
		{
			// RW 0x4BF59B-0x4BF5D5: nothing is playing: the pending state is resolved with the pending flags; the pending flag is
			// cleared whatever the selection did
			if (const AnimationStateInfo *next = m_data.findBestAnimationState(m_pendingFlags))
			{
				select(next, false);
			}
			m_pending = false;
			m_pendingTarget = nullptr;
		}
		return;
	}
	if (!m_tracks[0].anim && m_tracks[1].anim)
	{
		// RW 0x4BF649: ESC(0,1), and when a third track waits ESC(1,2) with the blend restarted (no zeroing of the countdown here)
		m_tracks[0] = m_tracks[1];
		clearTrack(1);
		if (m_tracks[2].anim)
		{
			m_tracks[1] = m_tracks[2];
			clearTrack(2);
			initBlend(m_tracks[1]);
		}
	}
	for (W3DDrawTrack &t : m_tracks)
	{
		if (t.anim)
		{
			stepTrack(t, elapsedMs);
		}
	}
	if (m_tracks[0].anim && m_tracks[1].anim)
	{
		m_blendCountdown -= m_tracks[1].anim->Get_Frame_Rate() * m_speedSync * m_speedFactor * elapsedMs * 0.001f; // RW 0x4BF8DC
		if (m_blendCountdown < 0.0f)
		{
			promote();
		}
	}
	if (!m_tracks[0].anim || !m_tracks[1].anim)
	{
		if (trackComplete(m_tracks[0], m_pending) && m_curAnimState && m_whichAnim != -1)
		{
			const AnimationStateInfo *state = m_curAnimState;
			if (m_pending)
			{
				// RW 0x4BFA2D-0x4BFA66: the pending flags are re-resolved; the pending flag stays only when the new state re-armed it
				if (const AnimationStateInfo *next = m_data.findBestAnimationState(m_pendingFlags))
				{
					m_pendingTarget = nullptr;
					select(next, false);
					if (!m_pendingTarget)
					{
						m_pending = false;
					}
					return;
				}
				m_pendingTarget = nullptr;
				m_pending = false;
			}
			// RW 0x4BFA77-0x4BFABC: a state without conditions starts again; so does one with RESTART_ANIM_WHEN_COMPLETE (bit 5)
			if (!state->conditions.any() || state->testFlag(W3D_ACF_RESTART_ANIM_WHEN_COMPLETE))
			{
				apply(state, -1.0f, false);
			}
		}
	}

	// RW 0x4B5497: the opacity ramp of FadeBeginFrame / FadeEndFrame / FadingIn (it needs a begin frame above 0)
	if (m_whichAnim >= 0 && m_whichAnim < (int)m_curAnimState->animations.size() && m_tracks[0].anim)
	{
		const W3DAnimationInfo &info = m_curAnimState->animations[(size_t)m_whichAnim];
		if (info.fadeBeginFrame > 0.0f && info.fadeEndFrame > info.fadeBeginFrame)
		{
			const float frame = m_tracks[0].frame;
			const float end = info.fadeEndFrame > (float)m_tracks[0].anim->Get_Num_Frames() ? (float)m_tracks[0].anim->Get_Num_Frames() : info.fadeEndFrame;
			float opacity;
			if (frame < info.fadeBeginFrame)
			{
				opacity = info.fadingIn ? 0.0f : 1.0f;
			}
			else if (frame > end)
			{
				opacity = info.fadingIn ? 1.0f : 0.0f;
			}
			else
			{
				const float ratio = (frame - info.fadeBeginFrame) / (end - info.fadeBeginFrame);
				opacity = info.fadingIn ? ratio : 1.0f - ratio;
			}
			m_opacity = opacity;
			m_hiddenByFade = !(opacity > 0.0f);
		}
	}
	syncSpeedToMovement();
}

// ---------------------------------------------------------------------------------------------------------------------
// Output and the script API
// ---------------------------------------------------------------------------------------------------------------------
W3DDrawFrame W3DScriptedModelDraw::frame() const
{
	W3DDrawFrame f;
	frame(f);
	return f;
}

void W3DScriptedModelDraw::frame(W3DDrawFrame &f) const
{
	if (m_curModelInfo)
	{
		f.modelName = m_curModelInfo->modelName();
	}
	else
	{
		f.modelName.clear();
	}
	f.model = m_model;
	f.modelState = m_curModelInfo;
	f.animationState = m_curAnimState;
	if (m_curAnimState)
	{
		f.animationStateName = m_curAnimState->stateName;
	}
	else
	{
		f.animationStateName.clear();
	}
	f.motion0 = nullptr;
	f.frame0 = 0.0f;
	f.motion1 = nullptr;
	f.frame1 = 0.0f;
	f.blendPercentage = 0.0f;
	f.blending = false;
	f.hiddenSubObjects.clear();
	int n = 0;
	for (int i = 0; i < 3; ++i)
	{
		f.tracks[i] = m_tracks[i];
		if (m_tracks[i].anim)
		{
			n = i + 1;
		}
	}
	f.trackCount = n;
	const W3DDrawTrack *first = m_tracks[0].anim ? &m_tracks[0] : (m_tracks[1].anim ? &m_tracks[1] : nullptr);
	if (first)
	{
		f.motion0 = first->anim;
		f.frame0 = first->frame;
	}
	if (m_tracks[0].anim && m_tracks[1].anim)
	{
		// RW 0x4B33FB: Set_Animation(track0.anim, track0.frame, track1.anim, track1.frame, 1 - countdown / initial)
		f.blending = true;
		f.motion1 = m_tracks[1].anim;
		f.frame1 = m_tracks[1].frame;
		f.blendPercentage = 1.0f - m_blendCountdown / m_blendInitial;
	}
	f.opacity = m_opacity;
	f.hidden = m_hiddenByFade;
	if (m_model)
	{
		for (int i : m_hiddenSubObjects)
		{
			f.hiddenSubObjects.push_back(m_model->SubObjects[(size_t)i].Name);
		}
	}
}

bool W3DScriptedModelDraw::modelCondition(const std::string &name)
{
	const int bit = ModelCondition::indexOf(name);
	if (bit < 0)
	{
		error("script asked for the unknown model condition " + name);
		return false;
	}
	return m_pendingFlags.test(bit); // the flags of the request being applied (RW 0x4BF420 stored them before select)
}

void W3DScriptedModelDraw::setTransitionAnimState(const std::string &name)
{
	if (!m_inScript)
	{
		error("SetTransitionAnimState(" + name + ") called outside a script");
		return;
	}
	m_ctx.transition = name;
}

void W3DScriptedModelDraw::allowToContinue()
{
	if (!m_inScript)
	{
		error("AllowToContinue called outside a script");
		return;
	}
	m_ctx.allowContinue = true;
}

void W3DScriptedModelDraw::hideModule(const std::string &name)
{
	m_hiddenModules.insert(name);
	m_log.push_back("hide module " + name);
	if (knowsModules())
	{
		m_options.setModuleVisible(name, false); // lane BUILD-4: Drawable::showModule (RW 0x6789B4)
	}
}

void W3DScriptedModelDraw::showModule(const std::string &name)
{
	m_hiddenModules.erase(name);
	m_log.push_back("show module " + name);
	if (knowsModules())
	{
		m_options.setModuleVisible(name, true); // lane BUILD-4: Drawable::showModule (RW 0x6789B4)
	}
}

void W3DScriptedModelDraw::playSound(const std::string &name)
{
	m_log.push_back("sound " + name);
	if (m_options.playSound)
	{
		// AUDIO-3: the request log names the state whose script asked
		AudioLog::Scope logScope([&] { return "draw script state " + (m_curAnimState ? m_curAnimState->stateName : std::string("<none>")); });
		m_options.playSound(name);
	}
}

bool W3DScriptedModelDraw::isCurrentTargetKindOf(const std::string &kind)
{
	if (!m_options.targetKindOf)
	{
		error("a script asked about the target's kind (" + kind + ") and no target information was given");
		return false;
	}
	return m_options.targetKindOf(kind);
}

float W3DScriptedModelDraw::currentTargetBearing()
{
	if (!m_options.targetBearing)
	{
		error("a script asked for the target bearing and no target information was given");
		return 0.0f;
	}
	return m_options.targetBearing();
}

void W3DScriptedModelDraw::setSubObjectHidden(const std::string &name, bool hide, bool permanent)
{
	stop("S-098", "scripts hide and show sub objects by name; the name match (full name or the part after the container dot) is inferred");
	if (m_inScript)
	{
		stop("S-830", "a state script's CurDrawableHide/ShowSubObject[Permanently] reaches only the draw module running it; retail (RW 0x73501F -> Drawable "
					  "RW 0x672823) passes it to every draw module of the drawable");
	}
	if (permanent)
	{
		// lane RENDER-3, TARGET RW 0x4C3B25 (vslot 0x88 of the draw's +0xC interface, reached from CurDrawableHide/ShowSubObjectPermanently RW 0x73501F /
		// Drawable RW 0x672823 with permanent = 1): the list at +0x68..+0x6C is searched by name with _strcmpi; a match takes the new hide flag, no match
		// appends a record; the dirty byte +0x1BD makes the next draw apply the list by name. Nothing tests the current render object here.
		bool found = false;
		for (auto &op : m_permanentSubObjectOps)
		{
			if (AsciiStringUtil::compareNoCase(op.first, name) == 0)
			{
				op.second = hide;
				found = true;
			}
		}
		if (!found)
		{
			m_permanentSubObjectOps.emplace_back(name, hide);
		}
		if (m_model)
		{
			applySubObjectRecords();
		}
		return;
	}
	// RW 0x4C3B25's plain branch: the record list at +0x5C is searched by name with _strcmpi; a match takes the new flag, no match appends a record; the
	// dirty byte +0x1BC makes the draw apply both lists (RW 0x4BA74F)
	bool found = false;
	for (auto &op : m_subObjectOps)
	{
		if (AsciiStringUtil::compareNoCase(op.first, name) == 0)
		{
			op.second = hide;
			found = true;
		}
	}
	if (!found)
	{
		m_subObjectOps.emplace_back(name, hide);
	}
	if (!m_model)
	{
		error("hide/show of sub object " + name + " with no model");
		return;
	}
	if (W3DFindSubObjectByName(*m_model, name) < 0)
	{
		error("sub object " + name + " not found in model " + m_modelName);
	}
	applySubObjectRecords();
}

// RW 0x4BA74F
void W3DScriptedModelDraw::applySubObjectRecords()
{
	for (const auto &op : m_subObjectOps)
	{
		applySubObjectHidden(op.first, op.second); // RW 0x4B3155
	}
	for (const auto &op : m_permanentSubObjectOps)
	{
		applySubObjectHidden(op.first, op.second); // RW 0x4B31C9
	}
}

bool W3DScriptedModelDraw::applySubObjectHidden(const std::string &name, bool hide)
{
	const int index = m_model ? W3DFindSubObjectByName(*m_model, name) : -1;
	if (index < 0)
	{
		return false;
	}
	for (int i : W3DSubObjectsUnderSubObject(*m_model, index))
	{
		if (hide)
		{
			m_hiddenSubObjects.insert(i);
		}
		else
		{
			m_hiddenSubObjects.erase(i);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// W3DHordeModelDraw
// ---------------------------------------------------------------------------------------------------------------------
int W3DHordeModelDraw::checkedLevel(int lodLevel)
{
	if (lodLevel < 0 || lodLevel > 2)
	{
		throw std::runtime_error("W3DHordeModelDraw: the LOD level is 0 (LOW), 1 (MEDIUM) or 2 (HIGH)");
	}
	return lodLevel;
}

W3DHordeModelDraw::W3DHordeModelDraw(const W3DHordeModelDrawModuleData &data, int lodLevel, W3DDrawAssets &assets, W3DDrawRandom &random,
	W3DDrawScriptHost *scripts, const Options &options)
	: W3DScriptedModelDraw(data, assets, random, scripts, options, false)
	, m_lod(data.m_lodOptions[checkedLevel(lodLevel)])
	, m_lodLevel(lodLevel)
{
	initializeState(); // the first states are selected with this class's overrides in place
}

// RW 0x478A74: GameClientRandomValue(0, frames - 1) times the row's RandomStartFramePercent, divided by 100
int W3DHordeModelDraw::randomStartFrame(int frames)
{
	const int v = W3DScriptedModelDraw::randomStartFrame(frames);
	return (v * m_lod.randomStartFramePercent) / 100;
}
