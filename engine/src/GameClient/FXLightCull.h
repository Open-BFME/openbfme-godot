// OpenBFME. GPL-3.0.
//
// FXLightCull (lane LOCAL-28, #331 port fit): the view cull of the light pulses the object shaders light with. CLIENT ONLY: included by
// GodotDevice/GodotFXPlayer.cpp (and its test); no simulation file includes it.

#pragma once

#include <cstddef>
#include <vector>

// lane LOCAL-28 (#331 port fit): the light pulses' view cull, RW 0x0046FCF9 (RTS3DScene::renderOneObject, Spheres_Intersect against the view
// frustum). The planes are Godot's (Camera3D::get_frustum): the normals point OUT of the frustum and distance = normal . p - d, so a sphere is
// outside when its centre is more than its radius in front of any plane. The first six planes are the main camera's; six more are the
// extra frustum of GodotFXPlayer::set_extra_pose_frustum, and a sphere inside either frustum is kept (the particle visibility test's rule).
// Fewer than six planes (no camera) culls nothing.
struct FXCullPlane
{
	float nx = 0.0f, ny = 0.0f, nz = 0.0f, d = 0.0f;
};

inline bool fxSphereVisible(const std::vector<FXCullPlane> &planes, float x, float y, float z, float radius)
{
	if (planes.size() < 6)
	{
		return true;
	}
	auto inside = [&](size_t first, size_t last) {
		for (size_t k = first; k < last; ++k)
		{
			const FXCullPlane &pl = planes[k];
			if (pl.nx * x + pl.ny * y + pl.nz * z - pl.d > radius)
			{
				return false;
			}
		}
		return true;
	};
	return inside(0, 6) || (planes.size() >= 12 && inside(6, 12));
}
