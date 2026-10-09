// OpenBFME. GPL-3.0. See DrawableLaunchBones.h (lane RENDER-2).

#include "GameClient/DrawableLaunchBones.h"

#include "Common/AsciiString.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PristinePose.h"
#include "GameLogic/SimMath.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
std::uint32_t floatBits(float f)
{
	std::uint32_t u;
	std::memcpy(&u, &f, sizeof(u));
	return u;
}

void toRows(const Matrix3D &m, float out[12])
{
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			out[r * 4 + c] = m.Row[r][c];
		}
	}
}

int indexOf(const ModelConditionInfo &s, const W3DModelDrawModuleData &data)
{
	for (size_t i = 0; i < data.m_conditionStates.size(); ++i)
	{
		if (&data.m_conditionStates[i] == &s)
		{
			return (int)i;
		}
	}
	return -2; // not one of the module's states (a test's own state): keyed apart
}

int indexOf(const AnimationStateInfo *s, const W3DModelDrawModuleData &data)
{
	if (!s)
	{
		return -1;
	}
	for (size_t i = 0; i < data.m_animationStates.size(); ++i)
	{
		if (&data.m_animationStates[i] == s)
		{
			return (int)i;
		}
	}
	return -2;
}
} // namespace

DrawableLaunchBones::DrawableLaunchBones(W3DDrawAssets &assets, std::vector<std::string> standardPublicBones, int animationLodNumber)
	: m_assets(assets)
	, m_standardPublicBones(std::move(standardPublicBones))
	, m_animationLodNumber(animationLodNumber)
{
}

void DrawableLaunchBones::reset()
{
	m_cache.clear();
	m_problems.clear();
	m_queries = 0;
	m_answered = 0;
	++m_generation;
}

void DrawableLaunchBones::problem(const std::string &p)
{
	if (std::find(m_problems.begin(), m_problems.end(), p) == m_problems.end())
	{
		m_problems.push_back(p);
	}
}

std::vector<std::string> DrawableLaunchBones::stops() const
{
	if (m_queries == 0)
	{
		return {};
	}
	return { "[S-460] launch bones: " + std::to_string(m_answered) + " of " + std::to_string(m_queries) +
		" launch queries named a bone; the pristine pose is evaluated deterministically (PristinePose, numeric facade, canonical environment) from the stored "
		"W3D values, but its bits are not proven equal to retail's: the adaptive delta filter table and the attach turn use sinDet / cosDet for the MSVCR71 "
		"sin / cos, the scale is applied after the pose, raw animations use the classic summation order (S-028), a state without an animation takes the bind "
		"pose (retail may take a live object's), the first Model name is used; " + std::to_string(m_problems.size()) + " launch-bone data problems" };
}

ModelConditionFlags DrawableLaunchBones::launchFlags(const Object &launcher)
{
	return launchFlags(launcher, launcher.getModelConditionBits());
}

ModelConditionFlags DrawableLaunchBones::launchFlags(const Object &launcher, const std::array<std::uint32_t, 19> &bits)
{
	ModelConditionFlags f;
	const Object::ModelConditionBits &placement = launcher.getPlacementConditionBits();
	bool placed = false;
	for (std::uint32_t w : placement)
	{
		placed = placed || w != 0u;
	}
	for (int b = 0; b < MODELCONDITION_COUNT; ++b)
	{
		const std::uint32_t mask = 1u << (b & 31);
		if ((bits[(size_t)b >> 5] & mask) || (placement[(size_t)b >> 5] & mask))
		{
			f.set(b);
		}
	}
	if (!placed)
	{
		// DrawableManager::objectCreated (ZH Object::friend_bindToDrawable): a new drawable follows the map's time of day and weather; a placement's flags
		// replace them (DrawableManager::applyPlacement)
		const GameLogicSettings &settings = launcher.logic().settings();
		if (settings.forceModelsToFollowTimeOfDay && settings.night)
		{
			f.set(ModelCondition::indexOf("NIGHT"));
		}
		if (settings.forceModelsToFollowWeather && settings.snowy)
		{
			f.set(ModelCondition::indexOf("SNOW"));
		}
	}
	return f;
}

// RW 0x4C2F4E validateStuff -> RW 0x4BD9A7 validateCachedBones -> RW 0x4BDED7 validateWeaponBarrelInfo, with robj = null (the launch path)
DrawableLaunchBones::Entry &DrawableLaunchBones::entry(const W3DModelDrawModuleData &data, const ModuleKey &mk, const ModelConditionInfo &modelState,
	const AnimationStateInfo *animState, float scale)
{
	const Key key(mk.templateName, mk.module, indexOf(modelState, data), indexOf(animState, data), floatBits(scale));
	auto it = m_cache.find(key);
	if (it != m_cache.end())
	{
		return it->second;
	}
	Entry &e = m_cache[key];
	// RW 0x4BDA1B..0x4BDA2F: a model state without a model has no bones
	if (modelState.modelName().empty())
	{
		return e;
	}
	std::string err;
	const RenderObjPrototype *proto = m_assets.model(modelState.modelName(), &err);
	if (!proto)
	{
		// RW 0x4BDA6C: "ASSET ERROR: Model %s not found!" and no bones
		problem("model " + modelState.modelName() + " not found: " + err);
		return e;
	}
	// RW 0x4BDBBE..0x4BDC27: the animation state's first animation, resolved plain, then numbered with the LOD number; neither: the bind pose (RW 0x4BDC2D logs)
	const HAnimClass *anim = nullptr;
	if (animState && !animState->animations.empty())
	{
		W3DAnimationLookup lookup;
		lookup.skeleton = modelState.skeleton;
		lookup.modelAnimationPrefix = modelState.modelAnimationPrefix;
		lookup.names = animState->animations[0].animationNames;
		std::string resolved, aerr;
		anim = m_assets.animation(lookup, &resolved, &aerr);
		if (!anim)
		{
			lookup.numbered = true;
			lookup.number = m_animationLodNumber;
			anim = m_assets.animation(lookup, &resolved, &aerr);
		}
		if (anim)
		{
			// RW 0x4BDCDF..0x4BDCFB: frame = FrameForPristineBonePositions unless frames - 1 <= it, then frames - 1
			const int last = anim->Get_Num_Frames() - 1;
			e.pose.frame = last <= animState->frameForPristineBonePositions ? last : animState->frameForPristineBonePositions;
			e.pose.animation = resolved;
		}
		else
		{
			problem("animation state " + animState->stateName + ": pose animation " + animState->animations[0].clipName() + " does not resolve (" + aerr +
				"); bones from the bind pose");
		}
	}
	if (!proto->Tree)
	{
		problem("model " + modelState.modelName() + " has no hierarchy: no bones");
		return e;
	}
	std::vector<Matrix3D> pose;
	std::string perr;
	if (!PristinePose::evaluate(*proto->Tree, anim, e.pose.frame, scale, pose, &perr))
	{
		// an undefined animation frame (S-003 / S-004) is reported, never guessed
		problem("bones of " + modelState.modelName() + ": " + perr);
		return e;
	}
	e.bones.reset(new W3DModelBones());
	e.bones->buildFromPose(modelState, proto, data.m_extraPublicBones, m_standardPublicBones, pose);
	for (const std::string &b : e.bones->missingBones)
	{
		problem("bone " + b + " not found in model " + modelState.modelName());
	}
	e.ok = e.bones->valid;
	return e;
}

const W3DModelBones *DrawableLaunchBones::bones(const W3DModelDrawModuleData &data, const ModuleKey &key, const ModelConditionInfo &modelState,
	const AnimationStateInfo *animState, float scale)
{
	Entry &e = entry(data, key, modelState, animState, scale);
	return e.ok ? e.bones.get() : nullptr;
}

DrawableLaunchBones::PoseInfo DrawableLaunchBones::poseOf(const W3DModelDrawModuleData &data, const ModuleKey &key, const ModelConditionInfo &modelState,
	const AnimationStateInfo *animState, float scale)
{
	return entry(data, key, modelState, animState, scale).pose;
}

// RW 0x4C34A2
bool DrawableLaunchBones::getProjectileLaunchOffset(const W3DModelDrawModuleData &data, const ModuleKey &key, const ModelConditionFlags &flags, float scale,
	float angle, const ThingTemplate *tt, int wslot, int barrel, float launch[12])
{
	const ModelConditionInfo *modelState = data.findBestInfo(flags);          // RW 0x4B4379
	const AnimationStateInfo *animState = data.findBestAnimationState(flags); // RW 0x4B4443
	if (!modelState)
	{
		return false; // RW 0x4C34CB
	}
	const W3DModelBones *b = bones(data, key, *modelState, animState, scale);
	// RW 0x4C34F6..0x4C3637: the AttachToBoneInAnotherModule offset, turned by the drawable's angle
	float off[3] = { 0.0f, 0.0f, 0.0f };
	if (!data.m_attachToDrawableBone.empty() && tt)
	{
		Matrix3D attach;
		if (getPristineBonePositions(*tt, flags, scale, data.m_attachToDrawableBone, 0, &attach, 1) == 1)
		{
			turnAttachOffset(attach, angle, off);
		}
	}
	// RW 0x4C3643..0x4C36F3: the animation state's barrels of the slot
	if (!b || wslot < 0 || wslot >= W3D_WEAPONSLOT_COUNT || b->barrels[wslot].empty())
	{
		return false;
	}
	const std::vector<W3DWeaponBarrel> &v = b->barrels[wslot];
	const size_t i = (barrel < 0 || (size_t)barrel >= v.size()) ? 0 : (size_t)barrel;
	toRows(v[i].projectileOffset, launch);
	launch[3] = SimMath::sseAdd(launch[3], off[0]);
	launch[7] = SimMath::sseAdd(launch[7], off[1]);
	launch[11] = SimMath::sseAdd(launch[11], off[2]);
	return true;
}

// RW 0x6756A1 over the template's draw modules (the drawable's module list is the template's, RW 0x679FD7)
bool DrawableLaunchBones::getProjectileLaunchOffset(const ThingTemplate &tt, const ModelConditionFlags &flags, float scale, float angle, int wslot, int barrel,
	float launch[12])
{
	const std::vector<ThingTemplate::Nugget> &nuggets = tt.drawModules().nuggets();
	for (size_t k = 0; k < nuggets.size(); ++k)
	{
		const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(nuggets[k].data.get());
		if (data && getProjectileLaunchOffset(*data, ModuleKey{ tt.getName(), (int)k }, flags, scale, angle, &tt, wslot, barrel, launch))
		{
			return true;
		}
	}
	return false;
}

bool DrawableLaunchBones::launchOffset(const Object &launcher, int wslot, int barrel, float launch[12])
{
	++m_queries;
	// the drawable's angle (Thing + 0x44) follows the object's at once in retail: the logic object's angle is the retail value
	const bool found = getProjectileLaunchOffset(*launcher.getTemplate(), launchFlags(launcher), launcher.getInstanceScale(), launcher.getOrientation(), wslot,
		barrel, launch);
	m_answered += found ? 1 : 0;
	return found;
}

bool DrawableLaunchBones::singleLogicalBone(const Object &obj, const std::string &bone, float out[12])
{
	Matrix3D m;
	if (getPristineBonePositions(*obj.getTemplate(), launchFlags(obj), obj.getInstanceScale(), bone, 0, &m, 1) != 1)
	{
		return false;
	}
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			out[r * 4 + c] = m.Row[r][c];
		}
	}
	return true;
}

// lane GARRISON-1: RW 0x68C650 -> RW 0x672A73(prefix, start index 1, ..., maxBones)
int DrawableLaunchBones::multiLogicalBones(const Object &obj, const std::string &prefix, const std::array<std::uint32_t, 19> &bits, int maxBones, float (*out)[12])
{
	++m_queries;
	std::vector<Matrix3D> m((size_t)(maxBones > 0 ? maxBones : 0));
	if (m.empty())
	{
		return 0;
	}
	const int count = getPristineBonePositions(*obj.getTemplate(), launchFlags(obj, bits), obj.getInstanceScale(), prefix, 1, m.data(), maxBones);
	for (int i = 0; i < count; ++i)
	{
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				out[i][r * 4 + c] = m[(size_t)i].Row[r][c];
			}
		}
	}
	m_answered += count > 0 ? 1 : 0;
	return count;
}

void DrawableLaunchBones::turnAttachOffset(const Matrix3D &bone, float angle, float out[3])
{
	const float c = SimMath::cosDet(angle);
	const float s = SimMath::sinDet(angle);
	const float x = bone.Row[0][3], y = bone.Row[1][3];
	out[0] = SimMath::sseSub(SimMath::sseMul(x, c), SimMath::sseMul(y, s));
	out[1] = SimMath::sseAdd(SimMath::sseMul(y, c), SimMath::sseMul(x, s));
	out[2] = bone.Row[2][3];
}

// RW 0x4C3731
int DrawableLaunchBones::getPristineBonePositions(const W3DModelDrawModuleData &data, const ModuleKey &key, const ModelConditionFlags &flags, float scale,
	const std::string &name, int startIndex, Matrix3D *transforms, int maxBones)
{
	const ModelConditionInfo *modelState = data.findBestInfo(flags);
	if (!modelState)
	{
		return 0;
	}
	const W3DModelBones *b = bones(data, key, *modelState, data.findBestAnimationState(flags), scale);
	// at most 64; index 0 is the plain name and stops there, a start index above 0 walks NAME%02d up to 99; the first miss ends the walk
	const int limit = std::min(maxBones, 64);
	const int last = startIndex != 0 ? 99 : 0;
	int count = 0;
	for (int i = startIndex; i <= last && count < limit; ++i)
	{
		char buffer[300];
		if (i == 0)
		{
			std::snprintf(buffer, sizeof(buffer), "%s", name.c_str());
		}
		else
		{
			std::snprintf(buffer, sizeof(buffer), "%s%02d", name.c_str(), i);
		}
		const W3DPristineBone *pb = b ? b->findPristineBone(AsciiStringUtil::lowered(buffer)) : nullptr;
		if (!pb)
		{
			break;
		}
		transforms[count] = pb->mtx;
		++count;
	}
	return count;
}

// RW 0x672A73 over the template's draw modules' RW 0x4C3731
int DrawableLaunchBones::getPristineBonePositions(const ThingTemplate &tt, const ModelConditionFlags &flags, float scale, const std::string &name,
	int startIndex, Matrix3D *transforms, int maxBones)
{
	int total = 0;
	const std::vector<ThingTemplate::Nugget> &nuggets = tt.drawModules().nuggets();
	for (size_t k = 0; k < nuggets.size() && maxBones > 0; ++k)
	{
		const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(nuggets[k].data.get());
		if (!data)
		{
			continue;
		}
		const int count = getPristineBonePositions(*data, ModuleKey{ tt.getName(), (int)k }, flags, scale, name, startIndex, transforms + total, maxBones);
		total += count;
		maxBones -= count;
	}
	return total;
}
