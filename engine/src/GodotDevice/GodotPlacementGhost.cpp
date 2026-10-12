// OpenBFME. GPL-3.0.
// See GodotPlacementGhost.h (lane BUILD-5 r2).

#include "GodotDevice/GodotPlacementGhost.h"

#include "Common/AsciiString.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LiveGame.h"
#include "GameClient/PlacementGhost.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameLogic/Map/CastleTemplates.h"
#include "GodotDevice/GodotRetailFileSystem.h"
#include "GodotDevice/GodotW3DInstancer.h"
#include "GodotDevice/GodotW3DModelBuilder.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot
{
namespace
{
String toGodotString(const std::string &s)
{
	return String::utf8(s.c_str());
}

// SAGE (x east, y north, z up) -> Godot (x, z, -y), the drawable's basis and scale (as GameWorld draws its drawables)
Transform3D toTransform(const float *basis, const Coord3D &pos, float scale)
{
	static const float P[3][3] = { { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } };
	float b[3][3];
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			float v = 0.0f;
			for (int a = 0; a < 3; ++a)
			{
				for (int c = 0; c < 3; ++c)
				{
					v += P[i][a] * basis[a * 3 + c] * P[j][c];
				}
			}
			b[i][j] = v * scale;
		}
	}
	Basis basisG(Vector3(b[0][0], b[1][0], b[2][0]), Vector3(b[0][1], b[1][1], b[2][1]), Vector3(b[0][2], b[1][2], b[2][2]));
	return Transform3D(basisG, Vector3(pos.x, pos.z, -pos.y));
}

// a node the renderer made, by the id taken when it was made: freed only while it still exists (the world that holds it may have gone first)
void freeNode(uint64_t id)
{
	if (id == 0)
	{
		return;
	}
	if (Node *n = Object::cast_to<Node>(ObjectDB::get_instance(id)))
	{
		n->queue_free();
	}
}

Color houseColorOf(std::uint32_t argb)
{
	return Color(((argb >> 16) & 255) / 255.0f, ((argb >> 8) & 255) / 255.0f, (argb & 255) / 255.0f, 1.0f);
}
} // namespace

PlacementGhostRenderer::PlacementGhostRenderer(Node3D *parent, const Ref<RetailFileSystem> &fs)
	: m_parent(parent)
	, m_fs(fs)
{
	m_textureLoader.instantiate();
	if (m_fs.is_valid() && m_fs->archive_fs())
	{
		m_castles.reset(new CastleTemplateStore());
		m_castles->setLoader(CastleTemplateStore::fileSystemLoader(*m_fs->archive_fs()));
	}
	else
	{
		error("no mounted archives: the castle layouts cannot be read");
	}
}

PlacementGhostRenderer::~PlacementGhostRenderer()
{
	clear();
	freeNode(m_arrowId);
	m_arrow = nullptr;
}

void PlacementGhostRenderer::error(const std::string &e)
{
	for (const std::string &x : m_errors)
	{
		if (x == e)
		{
			return;
		}
	}
	m_errors.push_back(e);
	UtilityFunctions::push_error(toGodotString("[placement ghost] " + e));
}

void PlacementGhostRenderer::clear()
{
	for (IconView &v : m_views)
	{
		freeNode(v.instId);
		freeNode(v.bibId);
	}
	m_views.clear();
}

W3DInstancer *PlacementGhostRenderer::newInstancer(const char *name)
{
	W3DInstancer *inst = memnew(W3DInstancer);
	inst->set_name(name);
	m_parent->add_child(inst);
	const Dictionary s = inst->setup(m_fs);
	if (!(bool)s["ok"])
	{
		const Array e = s["errors"];
		for (int64_t i = 0; i < e.size(); ++i)
		{
			error(String(e[i]).utf8().get_data());
		}
	}
	inst->set_house_colors_enabled(true); // before the first add_model (W3DInstancer.h)
	inst->set_playing(false);             // the drawable's draw module poses it
	return inst;
}

void PlacementGhostRenderer::drawIcon(size_t i, PlacementIcons::Icon &icon, IconView &view)
{
	Drawable &d = *icon.drawable;
	if (!view.inst)
	{
		view.inst = newInstancer(i == 0 ? "PlacementGhost" : "PlacementGhostPiece");
		view.inst->set_alpha_override(true); // the drawables' 0.45: ZH's translucent pass (W3DShaderKey::AlphaOverride)
		view.instId = view.inst->get_instance_id();
	}
	std::vector<DrawEntry> &entries = d.entries();
	view.entries.resize(entries.size());
	const Transform3D xf = toTransform(d.getBasis(), *d.getPosition(), d.getInstanceScale());
	int instances = 0;
	for (size_t k = 0; k < entries.size(); ++k)
	{
		DrawEntry &e = entries[k];
		EntryView &ev = view.entries[k];
		if (e.kind != W3D_DRAWKIND_MODEL || !e.draw)
		{
			continue;
		}
		W3DDrawPoseView f;
		e.draw->poseView(f);
		if (!f.model && f.modelName && !f.modelName->empty() && AsciiStringUtil::compareNoCase(*f.modelName, "None") != 0)
		{
			error(icon.tt->getName() + " " + e.className + ": the model " + *f.modelName + " does not load: the ghost does not draw it");
		}
		const std::string model = (f.model && !e.moduleHidden) ? AsciiStringUtil::lowered(*f.modelName) : std::string();
		if (model != ev.modelName)
		{
			if (ev.instance >= 0)
			{
				view.inst->remove_instance(ev.instance);
				ev.instance = -1;
			}
			ev.modelName = model;
			ev.hiddenModel = nullptr;
			ev.hidden.clear();
			if (!model.empty())
			{
				auto it = view.models.find(model);
				int64_t mid = it == view.models.end() ? view.inst->add_model(toGodotString(*f.modelName)) : it->second;
				view.models[model] = mid;
				if (mid < 0)
				{
					error(icon.tt->getName() + ": the model " + *f.modelName + " does not build: " + String(Variant(view.inst->get_errors())).utf8().get_data());
				}
				else
				{
					ev.instance = view.inst->add_instance(mid, xf, String(), 0.0, 1.0);
					if (ev.instance >= 0 && d.getHouseColor() != 0)
					{
						view.inst->set_instance_house_color(ev.instance, houseColorOf(d.getHouseColor())); // RW 0x674504 (slot 0 only)
					}
				}
			}
		}
		if (ev.instance < 0)
		{
			continue;
		}
		++instances;
		view.inst->set_instance_transform(ev.instance, xf);
		view.inst->set_instance_pose_keyed(ev.instance, *f.clip0, f.clipKey0, f.frame0, *f.clip1, f.clipKey1, f.frame1, f.blendPercentage);
		if (f.model != ev.hiddenModel || f.hiddenGeneration != ev.hiddenGeneration)
		{
			ev.hiddenModel = f.model;
			ev.hiddenGeneration = f.hiddenGeneration;
			std::vector<std::string> names;
			if (f.model)
			{
				for (int h : *f.hiddenSubObjects)
				{
					names.push_back(f.model->SubObjects[(size_t)h].Name);
				}
			}
			if (names != ev.hidden)
			{
				PackedStringArray packed;
				for (const std::string &n : names)
				{
					packed.push_back(toGodotString(n));
				}
				if (!view.inst->set_instance_hidden_subobjects(ev.instance, packed))
				{
					error(icon.tt->getName() + ": the draw script hides a sub object the model lacks");
				}
				ev.hidden = std::move(names);
			}
		}
		view.inst->set_instance_opacity(ev.instance, d.drawOpacity()); // 0.45 (RW 0xC12408)
	}
	view.instances = instances;
	view.inst->update_now();
	// the tint (Drawable::colorTint RW 0x6757BB): IllegalBuildColor (1, 0, 0) or (0, 0, 0.5 / 1.7) for code 9
	Color tint(0, 0, 0, 0);
	if (icon.tint == PlacementIcons::TINT_ILLEGAL)
	{
		tint = Color(1, 0, 0, 1);
	}
	else if (icon.tint == PlacementIcons::TINT_CODE9)
	{
		tint = Color(0, 0, icon.tintBlue, 1);
	}
	applyTint(view, tint);
	updateBib(icon, view);
}

void PlacementGhostRenderer::applyTint(IconView &view, const Color &tint)
{
	// Drawable::colorTint (RW 0x6757BB): the W3D shader's per node tint (w3dshadergen: added to the lights and the ambient, as ZH's W3DScene does); every draw
	// item node of the icon's own instancer takes it (a model added later takes it on the next frame)
	const Vector3 t = tint.a > 0.0f ? Vector3(tint.r, tint.g, tint.b) : Vector3();
	view.tint = tint;
	view.tintApplied = true;
	const TypedArray<Node> nodes = view.inst->find_children("*", "MultiMeshInstance3D", true, false);
	for (int64_t n = 0; n < nodes.size(); ++n)
	{
		if (GeometryInstance3D *g = Object::cast_to<GeometryInstance3D>(nodes[n]))
		{
			if (Variant(g->get_instance_shader_parameter("w3d_tint")) != Variant(t))
			{
				g->set_instance_shader_parameter("w3d_tint", t);
			}
		}
	}
}

Ref<Texture2D> PlacementGhostRenderer::texture(const std::string &name)
{
	auto it = m_textures.find(name);
	if (it != m_textures.end())
	{
		return it->second;
	}
	Ref<Texture2D> t = m_textureLoader->load_texture(m_fs, toGodotString(name));
	if (t.is_null())
	{
		error("the bib texture " + name + ": " + String(Variant(m_textureLoader->get_report())).utf8().get_data());
	}
	m_textures[name] = t;
	return t;
}

void PlacementGhostRenderer::updateBib(PlacementIcons::Icon &icon, IconView &view)
{
	// TerrainVisual vslot 0x30 (drawable, highlight, 0.0) / vslot 0x34 (remove), W3DTerrainVisual::addFactionBibDrawable RW 0x49249D
	const std::string tex = icon.bib == PlacementIcons::BIB_NONE ? std::string() : (icon.bib == PlacementIcons::BIB_NORMAL ? "TBBib.tga" : "TBRedBib.tga");
	if (tex.empty())
	{
		if (view.bib)
		{
			view.bib->set_visible(false);
		}
		view.bibTexture.clear();
		return;
	}
	const PlacementGhost::Bib bib = PlacementGhost::bibOf(*icon.tt);
	if (!bib.valid)
	{
		error(icon.tt->getName() + ": no bib: " + bib.error);
		return;
	}
	const Ref<Texture2D> t = texture(tex);
	if (t.is_null())
	{
		return;
	}
	if (!view.bib)
	{
		Ref<ArrayMesh> mesh;
		mesh.instantiate();
		PackedVector3Array v;
		PackedVector2Array uv;
		PackedVector3Array n;
		// the corners in the drawable's frame, SAGE (x, y) -> Godot (x, -y); uv 0 (x0, y0) (0, 1), 1 (x1, y0) (1, 1), 2 (x1, y1) (1, 0), 3 (x0, y1) (0, 0)
		const Vector3 c[4] = { Vector3(bib.x0, 0.05f, -bib.y0), Vector3(bib.x1, 0.05f, -bib.y0), Vector3(bib.x1, 0.05f, -bib.y1), Vector3(bib.x0, 0.05f, -bib.y1) };
		const Vector2 u[4] = { Vector2(0, 1), Vector2(1, 1), Vector2(1, 0), Vector2(0, 0) };
		for (int k : { 0, 2, 1, 0, 3, 2 })
		{
			v.push_back(c[k]);
			uv.push_back(u[k]);
			n.push_back(Vector3(0, 1, 0));
		}
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = v;
		arrays[Mesh::ARRAY_TEX_UV] = uv;
		arrays[Mesh::ARRAY_NORMAL] = n;
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		view.bib = memnew(MeshInstance3D);
		view.bib->set_name("PlacementBib");
		view.bib->set_mesh(mesh);
		view.bib->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		Ref<StandardMaterial3D> m;
		m.instantiate();
		m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA); // ZH W3DBibBuffer: SRCBLEND_SRC_ALPHA, DSTBLEND_ONE_MINUS_SRC_ALPHA, no depth write
		m->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
		m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		view.bib->set_material_override(m);
		m_parent->add_child(view.bib);
		view.bibId = view.bib->get_instance_id();
	}
	if (view.bibTexture != tex)
	{
		Ref<StandardMaterial3D> m = view.bib->get_material_override();
		m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, t);
		view.bibTexture = tex;
	}
	const Drawable &d = *icon.drawable;
	view.bib->set_transform(toTransform(d.getBasis(), *d.getPosition(), 1.0f));
	view.bib->set_visible(true);
}

void PlacementGhostRenderer::updateArrow(const InGameUI &ui)
{
	// W3DInGameUI::drawPlaceAngle (RW 0x48EAD8): not anchored -> both out of the scene (RW 0x48EB53); anchored -> W_Arrow (the anchor below 5 pixels, the
	// arrow at or above: both W_Arrow, RW 0xBDDBB0) with the transform of slot 0's drawable (RW 0x48EC4E -> RW 0x6765B9)
	m_anchored = ui.isPlacementAnchored();
	const bool want = m_anchored && m_icons && !m_icons->icons().empty();
	if (!want)
	{
		if (m_arrow && m_arrowInstance >= 0)
		{
			m_arrow->remove_instance(m_arrowInstance);
			m_arrowInstance = -1;
			m_arrow->update_now();
		}
		m_arrowShown = false;
		return;
	}
	if (!m_arrow)
	{
		m_arrow = newInstancer("PlacementArrow");
		m_arrowId = m_arrow->get_instance_id();
	}
	if (m_arrowModel == -2)
	{
		m_arrowModel = m_arrow->add_model("W_Arrow");
		if (m_arrowModel < 0)
		{
			error("the placement arrow W_Arrow does not build: " + std::string(String(Variant(m_arrow->get_errors())).utf8().get_data()));
		}
	}
	if (m_arrowModel < 0)
	{
		m_arrowShown = false;
		return;
	}
	const Drawable &d = *m_icons->icons()[0].drawable;
	const Transform3D xf = toTransform(d.getBasis(), *d.getPosition(), d.getInstanceScale());
	if (m_arrowInstance < 0)
	{
		m_arrowInstance = m_arrow->add_instance(m_arrowModel, xf, String(), 0.0, 1.0);
	}
	else
	{
		m_arrow->set_instance_transform(m_arrowInstance, xf);
	}
	m_arrow->update_now();
	m_arrowShown = m_arrowInstance >= 0;
}

void PlacementGhostRenderer::sync(LiveGame &game, const InGameUI &ui, double deltaSeconds, unsigned ghostOptions, bool judge)
{
	if ((&game != m_game || !m_icons) && !judge)
	{
		return; // the drawables are made on an idle frame
	}
	if (&game != m_game || !m_icons)
	{
		clear();
		m_game = &game;
		m_icons.reset(new PlacementIcons(game.drawables().services(), game.logic()));
		m_icons->setCastleStore(m_castles.get());
	}
	const std::string before = m_icons->templateName();
	m_icons->update(ui, deltaSeconds * 1000.0, ghostOptions, judge);
	for (const std::string &e : m_icons->errors())
	{
		error(e);
	}
	if (m_icons->templateName() != before || m_icons->icons().size() != m_views.size())
	{
		clear(); // a new template: new instancers (the models differ)
		m_views.resize(m_icons->icons().size());
	}
	if (ui.isPlacing() && ui.placeHasGhost())
	{
		for (size_t i = 0; i < m_icons->icons().size(); ++i)
		{
			drawIcon(i, m_icons->icons()[i], m_views[i]);
		}
	}
	else
	{
		for (IconView &v : m_views)
		{
			if (v.inst)
			{
				v.inst->set_visible(false);
			}
		}
	}
	for (IconView &v : m_views)
	{
		if (v.inst)
		{
			v.inst->set_visible(ui.isPlacing() && ui.placeHasGhost());
		}
	}
	updateArrow(ui);
}

Dictionary PlacementGhostRenderer::state() const
{
	Dictionary s;
	s["template"] = m_icons ? toGodotString(m_icons->templateName()) : String();
	s["castle"] = m_icons && m_icons->castle();
	s["anchored"] = m_anchored;
	s["arrow"] = m_arrowShown;
	Array icons;
	if (m_icons)
	{
		for (size_t i = 0; i < m_icons->icons().size(); ++i)
		{
			const PlacementIcons::Icon &icon = m_icons->icons()[i];
			Dictionary d;
			d["template"] = icon.tt ? toGodotString(icon.tt->getName()) : String();
			const Coord3D p = icon.drawable ? *icon.drawable->getPosition() : Coord3D{};
			d["x"] = p.x;
			d["y"] = p.y;
			d["z"] = p.z;
			d["angle"] = icon.angle;
			d["code"] = icon.code;
			Color tint(0, 0, 0, 0);
			if (icon.tint == PlacementIcons::TINT_ILLEGAL)
			{
				tint = Color(1, 0, 0, 1);
			}
			else if (icon.tint == PlacementIcons::TINT_CODE9)
			{
				tint = Color(0, 0, icon.tintBlue, 1);
			}
			d["tint"] = tint;
			d["bib"] = icon.bib == PlacementIcons::BIB_NONE ? String() : (icon.bib == PlacementIcons::BIB_NORMAL ? String("TBBib.tga") : String("TBRedBib.tga"));
			d["piece"] = icon.piece;
			d["base_site"] = icon.baseSite;
			d["house_color"] = icon.drawable ? (int64_t)icon.drawable->getHouseColor() : (int64_t)0;
			d["opacity"] = icon.drawable ? icon.drawable->drawOpacity() : 0.0f;
			d["instances"] = i < m_views.size() ? m_views[i].instances : 0;
			icons.push_back(d);
		}
	}
	s["icons"] = icons;
	Array errors;
	for (const std::string &e : m_errors)
	{
		errors.push_back(toGodotString(e));
	}
	s["errors"] = errors;
	Array notes;
	if (m_icons)
	{
		for (const std::string &n : m_icons->notes())
		{
			notes.push_back(toGodotString(n));
		}
	}
	s["notes"] = notes; // lane BUILD-5 r3: the counted draw problems (hide misses, retail data defects)
	return s;
}
} // namespace godot
