// OpenBFME. GPL-3.0.
//
// PristinePose (lane RENDER-2, review r1): the pose a projectile launch bone is read from, evaluated as SIMULATION code. Retail's logic asks the
// launcher's drawable for its launch bone (RW 0x6CAB85 -> 0x6756A1 -> 0x4C34A2), and that bone comes from a pristine pose (RW 0x4BD9A7: the animation
// state's first animation at an integer frame, FrameForPristineBonePositions). The value therefore enters the lockstep state, so it is computed
// here from the values the W3D loaders store AS READ (pivot quaternions and translations, raw channel floats, time-coded keys, the packed adaptive
// delta bytes) with the numeric facade only, in the operation order of the retail routines the render path ports:
//   * the hierarchy composition: HTreeClass Base_Pose / Anim_Pose / Anim_Pose_Raw (BFME2 0x5628A0, 0x562BB0 both arms, 0x563A80), the pivot
//     matrix of HLodClass::Update_Sub_Object_Transforms (0x59D640) - transcribed from htree.cpp term by term;
//   * the channel getters at an integer frame: raw (identity outside the range), classic time-coded (hold / lerp / BFME2 nlerp 0x717550), classic
//     adaptive delta (decoded from the packed bytes, frame truncation of 0x190CCB), the BFME2 motion channel encodings 0 / 1 / 2 (0x1B2EFC,
//     0x1B2FEF, 0x1B3313, 0x1B2450) - transcribed from motchan.cpp;
//   * the adaptive delta filter table (ZH motchan.cpp: 1 - sin(90 degrees * i / 240) for entries 16..255) with SimMath::sinDet.
// Everything runs under the canonical floating-point environment (CanonicalEnvironment saves the caller's and restores it), so a caller in another
// rounding mode gets the same bits.
//
// INFERENCE / stop S-460: retail's filter table comes from the MSVCR71 sin (sinDet is not proven bit equal); retail applies the drawable scale as
// the root of the pose (this port multiplies the posed matrices element by element, exact for scale 1); the raw-animation arm uses the classic
// arm's summation order (S-028).

#pragma once

#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <cfenv>
#include <string>
#include <vector>

class HTreeClass;
class HAnimClass;

namespace PristinePose
{

// The adaptive delta filter table entry `index & 0xFF` (shared with the drawn decode, motchan.cpp AdaptiveDelta_Filter).
float adaptiveDeltaFilter(unsigned index);
// The whole table computed afresh (under the canonical environment, whatever the caller's): what adaptiveDeltaFilter caches. For tests.
void computeFilterTable(float out[256]);

// Sets the canonical floating-point environment (NumericState::normalizeFloatingPointEnvironment) for its lifetime and restores the caller's.
class CanonicalEnvironment
{
public:
	CanonicalEnvironment();
	~CanonicalEnvironment();
	CanonicalEnvironment(const CanonicalEnvironment &) = delete;
	CanonicalEnvironment &operator=(const CanonicalEnvironment &) = delete;

private:
	std::fenv_t m_saved;
};

// The pivot transforms of `tree` posed with `anim` at `frame` (nullptr: the bind pose), each element then multiplied by `scale` (when it is not 1).
// false + *error when a channel has no value at the frame (a classic time-coded channel before its first key, S-003; a motion stream frame
// below 0, S-004) or the animation is of a class this evaluator does not know.
bool evaluate(const HTreeClass &tree, const HAnimClass *anim, int frame, float scale, std::vector<Matrix3D> &out, std::string *error);

} // namespace PristinePose
