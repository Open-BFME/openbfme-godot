// OpenBFME. GPL-3.0. See GodotWaterReflection.h.

#include "GodotDevice/GodotWaterReflection.h"

#include "GameClient/WaterReflection.h"
#include "GodotDevice/GodotGammaComposite.h"
#include "GodotDevice/GodotW3DMaterial.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/plane.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

namespace godot
{
namespace
{
// Godot (x, y up, -z north) <-> W3D (x, y, z up): G = (x, z, -y)
::Vector3 toW3D(const Vector3 &g)
{
	return ::Vector3(g.x, -g.z, g.y);
}
Vector3 toGodotSpace(const ::Vector3 &w)
{
	return Vector3(w.X, w.Z, -w.Y);
}

::Matrix3D toMatrix3D(const Transform3D &t)
{
	::Matrix3D m;
	for (int c = 0; c < 3; ++c)
	{
		const ::Vector3 axis = toW3D(t.basis.get_column(c));
		m.Row[0][c] = axis.X;
		m.Row[1][c] = axis.Y;
		m.Row[2][c] = axis.Z;
	}
	const ::Vector3 origin = toW3D(t.origin);
	m.Row[0][3] = origin.X;
	m.Row[1][3] = origin.Y;
	m.Row[2][3] = origin.Z;
	return m;
}

Transform3D fromMatrix3D(const ::Matrix3D &m)
{
	Transform3D t;
	for (int c = 0; c < 3; ++c)
	{
		t.basis.set_column(c, toGodotSpace(::Vector3(m.Row[0][c], m.Row[1][c], m.Row[2][c])));
	}
	t.origin = toGodotSpace(::Vector3(m.Row[0][3], m.Row[1][3], m.Row[2][3]));
	return t;
}
} // namespace

WaterReflectionView::WaterReflectionView()
{
	// the texture of RW 0x47E1BD: Create_Render_Target(0x200, 0x200, 0); the width follows the viewing camera's aspect (see update)
	m_viewport = memnew(SubViewport);
	m_viewport->set_name("Viewport");
	m_viewport->set_size(Vector2i(WaterReflection::kTextureSize, WaterReflection::kTextureSize));
	m_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	m_viewport->set_msaa_3d(Viewport::MSAA_DISABLED);
	m_viewport->set_screen_space_aa(Viewport::SCREEN_SPACE_AA_DISABLED);
	m_viewport->set_use_taa(false);
	m_viewport->set_use_debanding(false);
	m_viewport->set_transparent_background(false);
	m_viewport->set_disable_input(true);
	add_child(m_viewport);
	m_camera = memnew(Camera3D);
	m_camera->set_name("Camera");
	m_camera->set_cull_mask(W3D_REFLECTION_LAYER_BIT);
	// WW3D::Begin_Render(true, true, Vector3(0, 0, 0), 0): cleared to black
	Ref<Environment> env;
	env.instantiate();
	env->set_background(Environment::BG_COLOR);
	env->set_bg_color(Color(0.0f, 0.0f, 0.0f, 1.0f));
	m_camera->set_environment(env);
	m_viewport->add_child(m_camera);
	m_camera->set_current(true);
	set_process_priority(100000); // after every node that moves the viewing camera
}

void WaterReflectionView::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("configure", "plane_z", "polygons"), &WaterReflectionView::configure);
	ClassDB::bind_method(D_METHOD("get_reflection_texture"), &WaterReflectionView::get_reflection_texture);
	ClassDB::bind_method(D_METHOD("get_state"), &WaterReflectionView::get_state);
	ClassDB::bind_method(D_METHOD("get_reflection_camera_transform"), &WaterReflectionView::get_reflection_camera_transform);
	ClassDB::bind_method(D_METHOD("get_reflection_viewport"), &WaterReflectionView::get_reflection_viewport);
	ClassDB::bind_method(D_METHOD("get_reflection_camera"), &WaterReflectionView::get_reflection_camera);
}

void WaterReflectionView::configure(double plane_z, const Array &polygons)
{
	m_planeY = plane_z;
	m_polygons = polygons;
}

Ref<Texture2D> WaterReflectionView::get_reflection_texture() const
{
	return m_viewport->get_texture();
}

Transform3D WaterReflectionView::get_reflection_camera_transform() const
{
	return m_camera->get_global_transform();
}

Dictionary WaterReflectionView::get_state() const
{
	Dictionary d;
	d["plane_y"] = m_planeY;
	d["polygons"] = (int64_t)m_polygons.size();
	d["size"] = m_viewport->get_size();
	d["updating"] = m_updating;
	d["visible_polygons"] = m_visiblePolygons;
	d["error"] = m_error;
	d["frames_updated"] = m_framesUpdated;
	return d;
}

void WaterReflectionView::_notification(int what)
{
	switch (what)
	{
	case NOTIFICATION_READY:
		set_process_internal(true);
		break;
	case NOTIFICATION_INTERNAL_PROCESS:
		update();
		break;
	case NOTIFICATION_EXIT_TREE:
		W3D_Set_Reflect_Plane(W3D_REFLECT_PLANE_NONE);
		break;
	default:
		break;
	}
}

void WaterReflectionView::stop(const String &why)
{
	if (m_updating)
	{
		m_viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
		m_updating = false;
		W3D_Set_Reflect_Plane(W3D_REFLECT_PLANE_NONE);
	}
	m_error = why;
}

void WaterReflectionView::update()
{
	Viewport *vp = get_viewport();
	Camera3D *main = vp ? vp->get_camera_3d() : nullptr;
	m_visiblePolygons = 0;
	if (!main)
	{
		stop("no viewing camera");
		return;
	}
	if (main->get_projection() != Camera3D::PROJECTION_PERSPECTIVE)
	{
		// RW 0x47EA2B copies the camera and its projection; an orthographic camera's oblique clip is not ported
		stop("[S-035] the viewing camera is not a perspective camera; the reflection is not rendered");
		return;
	}
	const Transform3D source = main->get_global_transform();
	::Matrix3D reflected;
	if (!WaterReflection::mirrorCameraTransform(toMatrix3D(source), (float)m_planeY, reflected))
	{
		stop("the viewing camera's transform is not finite");
		return;
	}
	// RW 0x47F695: the pass runs only when some standing water polygon clips to three or more vertices against the viewing frustum
	std::vector<WaterReflection::Plane> frustum;
	{
		const Array planes = main->get_frustum();
		for (int64_t i = 0; i < planes.size(); ++i)
		{
			const Plane p = planes[i]; // outward normals: a point is inside when n . p - d <= 0
			WaterReflection::Plane q;
			q.n[0] = -p.normal.x;
			q.n[1] = -p.normal.y;
			q.n[2] = -p.normal.z;
			q.d = p.d;
			frustum.push_back(q);
		}
	}
	for (int64_t i = 0; i < m_polygons.size(); ++i)
	{
		const PackedVector3Array points = m_polygons[i];
		std::vector<::Vector3> polygon;
		polygon.reserve((size_t)points.size());
		for (int64_t k = 0; k < points.size(); ++k)
		{
			polygon.push_back(::Vector3(points[k].x, points[k].y, points[k].z));
		}
		if (WaterReflection::polygonInFrustum(polygon, frustum))
		{
			++m_visiblePolygons;
		}
	}
	if (m_visiblePolygons == 0)
	{
		if (m_updating)
		{
			m_viewport->set_update_mode(SubViewport::UPDATE_DISABLED); // the texture stays as it was
			m_updating = false;
			W3D_Set_Reflect_Plane(W3D_REFLECT_PLANE_NONE);
		}
		m_error = String();
		return;
	}
	m_error = String();
	// The texture is 512 x 512 in retail, the camera's own aspect stretched into it. A Godot camera takes its aspect from the viewport, so the viewport has the
	// viewing camera's aspect and retail's 512 rows (S-035: the horizontal resolution is 512 * aspect, not 512).
	const Vector2 view = vp->get_visible_rect().size;
	const double aspect = view.y > 0.0 ? view.x / view.y : 1.0;
	const Vector2i size((int32_t)std::lround(WaterReflection::kTextureSize * aspect), WaterReflection::kTextureSize);
	if (m_viewport->get_size() != size)
	{
		m_viewport->set_size(size);
	}
	m_camera->set_perspective(main->get_fov(), main->get_near(), main->get_far());
	m_camera->set_keep_aspect_mode(main->get_keep_aspect_mode());
	m_camera->set_h_offset(main->get_h_offset());
	m_camera->set_v_offset(main->get_v_offset());
	m_camera->set_global_transform(fromMatrix3D(reflected));
	if (!m_updating)
	{
		m_viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
		m_updating = true;
	}
	W3D_Set_Reflect_Plane((float)m_planeY);
	GammaComposite::ensure(m_camera); // the transparent W3D materials hand over gamma-space values
	++m_framesUpdated;
}

} // namespace godot
