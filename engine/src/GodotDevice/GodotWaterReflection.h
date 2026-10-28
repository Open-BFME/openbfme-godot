// OpenBFME. GPL-3.0.
//
// The planar water reflection (lane WATER-REFLECT, stop S-035; WaterReflectionView, the core maths in GameClient/WaterReflection.h): a SubViewport that shares the world, a camera that mirrors the viewing camera in the plane
// z = ReflectionPlaneZ every frame (WaterReflection.h: RW 0x47EA2B), and the texture the standing water samples as WaterReflectionTexture. Retail renders
// ONE such texture, whatever the number of waters, only when some standing water polygon is in the view (RW 0x47F695) and only for WaterLOD >= 2 with
// ReflectionOn (the MapTerrainBuilder decides that and does not make this node otherwise).
//
// What the camera sees: the nodes on W3D_REFLECTION_LAYER_BIT (the terrain; the W3DInstancer draw nodes of models that have a mirror instance), the camera's
// cull mask being that bit alone. Shaders recognise the pass by CAMERA_VISIBLE_LAYERS == W3D_REFLECTION_LAYER_BIT and clip below the plane (global
// w3d_reflect_y). Roads, floors, the water itself, terrain tracks, the selection decals and the behind-building marks are on no reflection layer, as the
// retail pass skips them (BFME2 W3DWaterRender.cpp, W3DTerrainTracksFlush.cpp:257, FlatHeightMap.cpp:810).

#pragma once

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot
{

class WaterReflectionView : public Node3D
{
	GDCLASS(WaterReflectionView, Node3D)

public:
	WaterReflectionView();

	// plane_z: ReflectionPlaneZ (W3D z = Godot y). polygons: one PackedVector3Array per standing water area, in Godot world space (the area's points at its
	// height), the polygons IsReflectionVisible tests against the viewing frustum.
	void configure(double plane_z, const Array &polygons);
	// The viewport's texture (what RW 0x47F952 binds when the reflection is on)
	Ref<Texture2D> get_reflection_texture() const;
	// { plane_y, polygons, size: Vector2i, updating: bool, visible_polygons: int, error: String, frames_updated: int, camera_origin: Vector3, camera_basis: Basis }
	Dictionary get_state() const;
	// Test hook: the transform the mirror camera has now (the last update).
	Transform3D get_reflection_camera_transform() const;
	SubViewport *get_reflection_viewport() const { return m_viewport; }
	Camera3D *get_reflection_camera() const { return m_camera; }

	void _notification(int what);

protected:
	static void _bind_methods();

private:
	void update();
	void stop(const String &why);

	SubViewport *m_viewport = nullptr;
	Camera3D *m_camera = nullptr;
	double m_planeY = 0.0;
	Array m_polygons;
	bool m_updating = false;
	int64_t m_visiblePolygons = 0;
	int64_t m_framesUpdated = 0;
	String m_error;
};

} // namespace godot
