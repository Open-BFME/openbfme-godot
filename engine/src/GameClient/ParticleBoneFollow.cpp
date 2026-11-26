// OpenBFME. GPL-3.0. See GameClient/ParticleBoneFollow.h.

#include "GameClient/ParticleBoneFollow.h"

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameLogic/Object/PristinePose.h"

#include <cmath>

namespace ParticleBoneFollow
{

// RW 0x4C7795: the gate of doDrawModule (`cmp byte [edi + 0x108], 0`, edi = the module data, [ebx + 4] there).
bool wantsFollow(const W3DModelDrawModuleData &data)
{
	return data.m_particlesAttachedToAnimatedBones;
}

namespace
{

// The pivot index the record names in `model`, or -1.
//   * the by-NAME branch (RW 0x672B5B, ParticleBonesCheckDrawable set) resolves the record's bone name every frame;
//   * the by-INDEX branch (RW 0x4B6E9D, slot +0xCC with the record's +4) uses the creation-time index and skips a record whose
//     index is 0 (0x4B6E57 `test ebx, ebx; je 0x4B6ED2`).
// HTreeClass::Get_Bone_Index answers 0 both for the root and for "not found", so 0 is never a bone.
int pivotOf(const RenderObjPrototype &model, const Record &record)
{
	if (!model.Tree || record.boneName.empty())
	{
		return -1;
	}
	const HTreeClass &tree = *model.Tree;
	if (!record.checkDrawable)
	{
		return record.boneIndex > 0 && record.boneIndex < tree.Num_Pivots() ? record.boneIndex : -1;
	}
	const int index = tree.Get_Bone_Index(record.boneName);
	return index > 0 && index < tree.Num_Pivots() ? index : -1;
}

// The draw entry's current pose in the model's space, as RETAIL's render object holds it, and the pivot visibility of the same
// frame. INFERENCE (a): track 0 alone while an AnimationBlendTime blend runs.
bool evaluatePose(const Pose &pose, const RenderObjPrototype *&model, std::vector<Matrix3D> &out, bool &visibleKnown, HTreePose &visibility)
{
	out.clear();
	visibleKnown = false;
	model = nullptr;
	if (!pose.draw)
	{
		return false;
	}
	model = pose.draw->currentModel();
	if (!model || !model->Tree)
	{
		model = nullptr;
		return false;
	}
	W3DDrawPoseView view;
	pose.draw->poseView(view);
	if (!view.clipKey0)
	{
		return false; // no track in use: nothing the pose could say
	}
	const HAnimClass *anim = static_cast<const HAnimClass *>(view.clipKey0);
	// PristinePose::evaluate is the deterministic transcription of HTreeClass::Anim_Pose / Anim_Pose_Raw (RW 0x562BB0 /
	// 0x563A80) - the evaluator GodotW3DInstancer::evaluate_animation_pose runs for the drawn pose and the pick path reads.
	// The frame is the integer the draw shows (RW 0x4B33FB's request is evaluated the same way, htree.h ROUND).
	std::string error;
	if (!PristinePose::evaluate(*model->Tree, anim, (int)std::lrint(view.frame0), pose.scale, out, &error))
	{
		return false; // S-003 / S-004: a frame the animation cannot define is never guessed
	}
	// INFERENCE (b): the render object's slot +0xD4 (the by-index branch's visibility test) is the pivot's visibility in the
	// pose - the animation's visibility channel, which HTreePose::IsVisible carries.
	try
	{
		model->Tree->Anim_Pose(Matrix3D(), anim, view.frame0, visibility);
		visibleKnown = true;
	}
	catch (...)
	{
		visibleKnown = false;
	}
	return true;
}

// RW 0x4B6E9D / the by-name branch's transform: the render object's Get_Bone_Transform, i.e. its transform times the pivot's.
// The pose carries the drawable's instance scale (PristinePose::evaluate multiplied every element by it); `world` is the
// drawable's own transform.
void worldBone(const Pose &pose, const std::vector<Matrix3D> &poseTransforms, int pivot, Matrix3D &out)
{
	Matrix3D::Multiply(pose.world, poseTransforms[(size_t)pivot], &out);
}

} // namespace

// RW 0x4C6514: the bone transform the CREATION reads. Retail asks the render object - slot +0xCC (Get_Bone_Transform
// by index) - at RW 0x4C678F (the FollowBone branch) and at RW 0x4C69BE (the branch a record without FollowBone takes); the render object holds
// the pose the draw currently shows, so a system is born on the ANIMATED bone, not on the bind pose's. Without an animation in use the render
// object shows the bind pose (RW 0x4BDC2D poses nothing), which is what a null clip gives here.
bool poseBone(const W3DScriptedModelDraw &draw, float scale, int pivot, Matrix3D &out)
{
	const RenderObjPrototype *model = draw.currentModel();
	if (!model || !model->Tree || pivot <= 0)
	{
		return false;
	}
	std::vector<Matrix3D> transforms;
	W3DDrawPoseView view;
	draw.poseView(view);
	const HAnimClass *anim = static_cast<const HAnimClass *>(view.clipKey0);
	std::string error;
	// The frame is the integer the draw shows (RW 0x4B33FB's request is evaluated the same way, htree.h ROUND): the same pose the instancer draws
	if (!PristinePose::evaluate(*model->Tree, anim, anim ? (int)std::lrint(view.frame0) : 0, scale, transforms, &error))
	{
		return false;
	}
	if ((std::size_t)pivot >= transforms.size())
	{
		return false;
	}
	out = transforms[(std::size_t)pivot];
	return true;
}

void followFrame(std::vector<Record> &records, FXParticleSystem::ParticleSystemManager &systems, const Pose *poses, std::size_t poseCount, Counters *counters,
	int callArg)
{
	if (records.empty())
	{
		return;
	}
	std::vector<Matrix3D> poseTransforms;
	HTreePose poseVisibility;
	const RenderObjPrototype *poseModel = nullptr;
	std::size_t poseEntry = (std::size_t)-1;
	bool poseOk = false, poseVisibleKnown = false;
	for (Record &record : records)
	{
		// RW 0x4B6DB3 RW 0x5F530A findParticleSystemByID: a system that is gone is skipped and its record stays (the record
		// leaves with the state - RW 0x4C6514's clean path)
		FXParticleSystem::ParticleSystem *sys = systems.findParticleSystemByID(record.system);
		if (!sys)
		{
			if (counters)
			{
				++counters->missed;
			}
			continue;
		}
		// 0x4B6DCC `cmp byte [sys + 0x1A7], 0; jne 0x4B6DDF` and 0x4B6DD5 `cmp byte [ebp + 8], 0; jne 0x4B6EF7`: a record
		// whose FollowBone is off ends the walk when the argument is not 0. The per-frame call passes 0 (RW 0x4C77A3).
		if (!record.followBone && callArg != 0)
		{
			return;
		}
		if (record.entry >= poseCount)
		{
			if (counters)
			{
				++counters->missed;
			}
			continue;
		}
		const Pose &pose = poses[record.entry];
		if (!pose.follow && callArg == 0)
		{
			// RW 0x4C7795: doDrawModule never makes the per-frame call (argument 0) for a module data whose ParticlesAttachedToAnimatedBones is off.
			// the end-of-draw call (argument 1) has no such gate - RW 0x4C6514 makes it on every frame without a state change
			// (`cmp byte [ebx + 0x48], 0; je 0x4C6D73`, then RW 0x4C6D78 `push 1; call [eax + 0x6C]`), whatever the module data says
			if (counters)
			{
				++counters->gated;
			}
			continue;
		}
		if (record.entry != poseEntry)
		{
			poseEntry = record.entry;
			poseOk = evaluatePose(pose, poseModel, poseTransforms, poseVisibleKnown, poseVisibility);
		}
		// 0x4B6DDF the module data's ParticleBonesCheckDrawable picks the branch; both ask the draw's pose for the bone
		// (INFERENCE (c) of the header)
		const int pivot = poseOk && poseModel ? pivotOf(*poseModel, record) : -1;
		if (pivot <= 0 || (size_t)pivot >= poseTransforms.size())
		{
			if (counters)
			{
				++counters->missed;
			}
			continue;
		}
		Matrix3D xf;
		worldBone(pose, poseTransforms, pivot, xf);
		if (!record.checkDrawable && poseVisibleKnown && (size_t)pivot < poseVisibility.IsVisible.size() && poseVisibility.IsVisible[(size_t)pivot] == 0)
		{
			// RW 0x4B6E79 RW 0x5F2EDE: the bone is not visible, the system stops emitting
			sys->stop();
			if (counters)
			{
				++counters->stopped;
			}
			continue;
		}
		if (!record.checkDrawable)
		{
			// RW 0x4B6E8B RW 0x5F2EE6: the visible case of the by-index branch (the by-name branch has no start call)
			sys->start();
			if (counters)
			{
				++counters->started;
			}
		}
		// RW 0x4B6E2F / 0x4B6EB8 RW 0x5F2F55 setLocalTransform, 0x4B6E44 / 0x4B6ECD RW 0x5F32A7 setSkipParentXfrm(1)
		sys->setLocalTransform(xf);
		sys->setSkipParentXfrm(true);
		if (counters)
		{
			++counters->followed;
		}
	}
}

} // namespace ParticleBoneFollow
