// OpenBFME infrastructure (no ZH/BFME counterpart): the acceptance stops of the W3D render path, reported at run time.
//
// docs/STOPS.md registers every behaviour the render path takes without a recovered source. Registering is not enough: each such
// behaviour must show up in the model report the moment a model uses it, so a consumer (and a test) can see which meshes are
// affected. These collectors read the render data and say, in one message per stop hit, which stop applies. The messages start
// with "[S-0xx]" and the id is also carried separately; tests assert on ids and counts, never on the guessed arithmetic.
//
//   S-020 skin vertices whose two weights are both 0 are given weight 100 (OpenSAGE's reading)
//   S-021 BFME dual-bone blend formula is the spec's reading of the file
//   S-022 house colour is not applied (combine unrecovered) although housecolor.ini names a house texture for the base texture
//   S-023 BFME2 FX material drawn with an approximation
//   S-025 stage with a texture and no texture coordinates at all (zeros assumed)
//   S-026 blend / depth / gradient combinations Godot cannot express (drawn with the nearest state)
//   S-027 texture mapper phase shared by every instance of a material; RANDOM mapper uses a non-retail generator
//   (S-024, S-028 and S-029 are raised where animation names are resolved, poses are evaluated and fades are drawn.)

#pragma once

#include "Libraries/WWVegas/WW3D2/meshrender.h"

#include <string>
#include <vector>

class HouseColorTable;

struct W3DStopHit
{
	std::string Id;      // "S-020"
	std::string Message; // "[S-020] ..."
	size_t Count = 1;    // vertices / surfaces the message covers
};

// Builds "[Id] detail".
std::string W3D_Stop_Message(const std::string &id, const std::string &detail);

// Per mesh: S-020, S-021, S-025.
void W3D_Collect_Mesh_Stops(const MeshRenderData &mesh, std::vector<W3DStopHit> &out);

// Per draw surface: S-022 (when houseColors is not null), S-023, S-026, S-027.
void W3D_Collect_Surface_Stops(const MeshRenderData &mesh, const MeshDrawSurface &surf, const MeshModelClass &source, const HouseColorTable *houseColors,
	std::vector<W3DStopHit> &out);

// Raised where a clip is looked up by a bare name (no "HIERARCHY." part): S-024. `resolved` is the name that was used.
W3DStopHit W3D_Bare_Clip_Stop(const std::string &clipName, const std::string &hierarchy, const std::string &resolved);

// S-028: poses evaluated by an arm whose float summation order is not retail's (raw / blended animations).
// `instances` is how many poses of the update were affected.
W3DStopHit W3D_Pose_Order_Stop(size_t instances);

// S-1730 (lane PERF-3): the instancer leaves the poses of instances outside the camera's frustum pending (W3DInstancer::set_pose_culling); the bound it
// tests is its own (W3D_Pose_Cull_Radius), not retail's render-object bounds. Reported once per instancer, when the first pose is left pending.
W3DStopHit W3D_Pose_Cull_Stop();
// The cull radius of a model, in model units: 1.5 * (the farthest pivot of the bind pose + the largest mesh extent) + 30.
float W3D_Pose_Cull_Radius(float farthestPivot, float largestMeshExtent);

// S-029: an opaque surface whose pivot fade is below 1 is drawn with a screen-door dither instead of retail's alpha-blended pass.
W3DStopHit W3D_Fade_Dither_Stop(size_t instances);

// S-029 (second condition): blended draw batches whose instances interleave in depth are drawn batch by batch, not back to front.
// `pairs` is the number of batch pairs affected this frame.
W3DStopHit W3D_Batch_Order_Stop(size_t pairs);
