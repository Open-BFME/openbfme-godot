// OpenBFME. GPL-3.0.
//
// ParticleBoneFollow: how the ParticleSysBone systems of a model draw follow the ANIMATED bones.
//
// RETAIL. A W3DModelDraw keeps one tracker list per draw - the draw object's +0x58, nodes whose +8 is a ParticleSystem id and
// whose +0xC points at the record the creation copies at RW 0x4B6CDD ({AsciiString bone +0, dword boneIndex +4, byte
// followBone +0x10}) - and walks it once per draw from W3DModelDraw::doDrawModule RW 0x4C72BE:
//
//   RW 0x4C7795  `cmp byte [edi + 0x108], 0; je 0x4C77A8` - the module data's ParticlesAttachedToAnimatedBones (+0x108, INI row
//                of the draw table RW 0xBE1320) gates the whole walk. edi is the module data ([ebx + 4] of doDrawModule).
//   RW 0x4C779E  `lea ecx, [ebx + 0xC]; mov eax, [ecx]; push 0; call [eax + 0x6C]` (through 0x4C77A5): the draw module's other
//                half at +0xC, interface slot 27, with the argument 0 = the per-frame call. Slot 27 of vtable RW 0xBE3B78 is RW
//                0x4B6D5F, the body ported here. The module's clean end calls the same slot with 1 (RW 0x4C6D73 .. 0x4C6D7A)
//                and then RW 0x4C5CC5.
//
//   RW 0x4B6D5F, per node of the tracker list (head/tail at +0x58; the walk is 0x4B6DA1 .. 0x4B6EEF):
//     0x4B6DB3  RW 0x5F530A findParticleSystemByID(TheParticleSystemManager, &handle, node+8): a system that is gone is
//               skipped and its node stays (the node leaves with the state, RW 0x4C6514's clean path).
//     0x4B6DCC  `cmp byte [sys + 0x1A7], 0; jne 0x4B6DDF` - the record's followBone, stored by the creation at RW 0x4C6B49
//               (`mov byte [eax + 0x1a7], cl`);
//     0x4B6DD5  `cmp byte [ebp + 8], 0; jne 0x4B6EF7`: a record whose FollowBone is off and a non-zero argument leaves the walk
//               through 0x4B6EF7 (the handle released, al = 1). The per-frame call passes 0 (RW 0x4C77A3), so it moves EVERY
//               tracked system; the end-of-draw call (1, RW 0x4C6D78) stops at the first FollowBone = No record.
//     0x4B6DDF  `mov eax, [esi - 8]; cmp byte [eax + 0x12A], 0` - the module data's ParticleBonesCheckDrawable picks the branch:
//       set  -> 0x4B6DF2 RW 0x4B6CDD copies the record, 0x4B6E14 RW 0x672B5B asks the DRAWABLE for the bone BY NAME (it walks
//               the drawable's draw-module list at +0x14C, takes each module's render object through slot +0xA4 and its bone
//               through slot +0x14) and on success 0x4B6E2F RW 0x5F2F55 setLocalTransform + 0x4B6E44 RW 0x5F32A7
//               setSkipParentXfrm(1). No visibility test and no start call on this branch.
//       clear-> 0x4B6E57 `test ebx, ebx; je 0x4B6ED2`: a record whose creation-time bone index (+4) is 0 is left alone; else
//               0x4B6E5B the render object's slot +0xD4 decides - 0x4B6E79 RW 0x5F2EDE stop when it answers false, 0x4B6E8B RW
//               0x5F2EE6 start when it answers true - and 0x4B6E9D the render object's slot +0xCC (Get_Bone_Transform(int), the
//               index) gives the transform those two calls then apply.
//
// Both transforms are the render object's bone transform: the drawable's world transform times the pivot's transform of the
// pose the draw currently shows. That is why both branches call setSkipParentXfrm(1): ParticleSystem::update (RW 0x5FA2CE,
// 0x5FA8A9) takes a skipped local transform as the world one, while the creation's position + Z rotation (RW 0x4C6514) has
// no such call and is still multiplied by the drawable.
//
// PORT. LiveFX::createSystems writes one Record per created system (RW 0x4C6BAD .. 0x4C6C39: the record copy plus the new
// system's id pushed to the module's list) and LiveFX::followAnimatedBones builds one Pose per draw entry and calls
// followFrame - from updateAttachedSystems, the port's per-client-frame place for this call (retail makes it from the draw
// module's doDrawModule).
//
// INFERENCE (reported in the PR and in stop S-682):
//   (a) the pose: retail reads the render object's live pose, exactly what the renderer shows. The port evaluates it from the
//       draw module's pose request (W3DDrawPoseView: track 0's animation at its frame) through PristinePose, the deterministic
//       transcription of HTreeClass::Anim_Pose / Anim_Pose_Raw that GodotW3DInstancer::evaluate_animation_pose runs for the
//       drawn pose. While an AnimationBlendTime countdown runs the RW pose request 0x4B33FB also blends track 1 in; this reads
//       track 0 alone, as the pick path does (GameClient/DrawablePick).
//   (b) the render object's slot +0xD4 (the by-index branch's visibility test) is not identified. It is read for the pivot
//       the record names and drives the stop/start pair, so it is taken as the pose's own pivot visibility (HTreePose::
//       IsVisible, the animation's visibility channel) - the only per-pivot visibility the draw has.
//   (c) the two branches (by name through the drawable, by index through the render object) resolve to the same pose here;
//       what stays different is what RW reads from the record: the by-name branch re-resolves the name every frame and
//       ignores a zero creation index, the by-index one uses the creation-time index and skips a record whose index is 0.
//   (d) a frame the animation cannot define (time-coded channel before its first key, S-003; motion stream frame below 0,
//       S-004) throws in htree.h; the record is counted as missed for that frame and keeps last frame's transform, never a
//       guessed one.

#pragma once

#include "GameClient/ParticleSys.h"
#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <cstddef>
#include <string>
#include <vector>

class W3DScriptedModelDraw;
class W3DModelDrawModuleData;

namespace ParticleBoneFollow
{

// One node of the draw module's tracker (the list at +0x58 of RW 0x4B6D5F): the system id and the record RW 0x4B6CDD copies.
struct Record
{
	FXParticleSystem::ParticleSystemID system = FXParticleSystem::INVALID_PARTICLE_SYSTEM_ID;
	std::size_t entry = 0;      ///< the draw entry that created it: its pose is the one that moves the system
	std::string boneName;       ///< the bone as ParticleSysBone wrote it (the parse lower-cases it)
	int boneIndex = 0;          ///< the record's +4: the model's pivot index at creation, 0 when the model has no such bone
	bool followBone = false;    ///< the record's +0x10, the system's +0x1A7 (RW 0x4C6B49)
	bool checkDrawable = false; ///< the module data's ParticleBonesCheckDrawable (+0x12A) when the record was made
};

// The pose source of one draw entry: RETAIL's render object.
struct Pose
{
	const W3DScriptedModelDraw *draw = nullptr; ///< the draw module (its pose request and its current model)
	float scale = 1.0f;                         ///< the drawable's instance scale, the render object's scale
	Matrix3D world;                             ///< the drawable's world transform (position and basis)
	bool follow = false;                        ///< RW 0x4C7795: the module data's ParticlesAttachedToAnimatedBones
};

// What a walk did, for the report and the tests.
struct Counters
{
	unsigned followed = 0; ///< transforms applied (RW 0x5F2F55 + 0x5F32A7)
	unsigned missed = 0;   ///< a record whose system is gone or whose bone this frame's pose does not have
	unsigned started = 0;  ///< RW 0x5F2EE6
	unsigned stopped = 0;  ///< RW 0x5F2EDE
	unsigned gated = 0;    ///< records of a draw entry whose module data does not ask for the follow (RW 0x4C7795)
};

// RW 0x4C7795: `cmp byte [edi + 0x108], 0` - does the module data ask for the follow?
bool wantsFollow(const W3DModelDrawModuleData &data);

// The draw's CURRENT pose pivot `pivot` in the model's space, every element multiplied by `scale` - the value the
// creation of a ParticleSysBone system reads from the render object (RW 0x4C6514: slot +0xCC Get_Bone_Transform at RW 0x4C678F for a
// FollowBone record and at RW 0x4C69BE for the position-only branch; the render object holds the pose the draw shows). False when the
// pose or the pivot is not there (a state without an animation - the render object then shows the bind pose -, S-003 / S-004).
bool poseBone(const W3DScriptedModelDraw &draw, float scale, int pivot, Matrix3D &out);

// RW 0x4B6D5F. With the per-frame argument (0, the call RW 0x4C779E makes) every record's system takes its bone's transform,
// and the by-index branch also starts or stops the system with the bone's visibility; one pose is evaluated per draw entry
// that has records. With the end-of-draw argument (1, RW 0x4C6D78) the walk returns at the first record whose FollowBone is
// off. A record of an entry whose Pose::follow is false is not touched (RW 0x4C7795 never calls the walk for such a module).
void followFrame(std::vector<Record> &records, FXParticleSystem::ParticleSystemManager &systems, const Pose *poses, std::size_t poseCount,
	Counters *counters, int callArg = 0);

} // namespace ParticleBoneFollow
