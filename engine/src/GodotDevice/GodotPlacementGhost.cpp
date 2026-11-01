// OpenBFME. GPL-3.0.
// See GodotPlacementGhost.h (lane BUILD-5 r2).

#include "GodotDevice/GodotPlacementGhost.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/FormationAssistant.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include "GameClient/GUI/Image.h"
#include <godot_cpp/classes/viewport.hpp>
#include <cmath>

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
#include <godot_cpp/classes/decal.hpp>
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

void PlacementGhostRenderer::clearTransient()
{
	freeNode(m_formationId); m_formationId=0; m_formation=nullptr;
	m_formationSlots.clear(); m_formationTextures.clear();
	freeNode(m_resourceDecalId);
	freeNode(m_resourceLabelId);
	m_resourceDecalId = m_resourceLabelId = 0;
	m_resourceDecal = nullptr;
	m_resourceLabel = nullptr;
}

void PlacementGhostRenderer::clear()
{
	clearTransient();
	for (IconView &v : m_views)
	{
		freeNode(v.instId);
		freeNode(v.bibId);
		freeNode(v.shadowDecalId); // lane OWNERFIX-2
	}
	m_views.clear();
	for (auto &kv : m_parked)
	{
		for (IconView &v : kv.second)
		{
			freeNode(v.instId);
			freeNode(v.bibId);
		}
	}
	m_parked.clear();
}

void PlacementGhostRenderer::parkViews(const std::string &templateName)
{
	// lane FOGFX-1 (FB-0055, "a brief 30-40% FPS drop when selecting a building"): the icons' instancers keep their models, textures and materials when
	// the placement ends or changes template (hidden, kept by template name); the next placement of that template takes them back instead of loading the
	// model again (9 - 14 ms on the frame the ghost appeared, measured on a GTX 1060). DONOR (ZH InGameUI::placeBuildAvailable, W3DAssetManager): RotWK's
	// place icons are new drawables whose render objects come from the asset manager's prototypes, which keep a model once loaded
	for (IconView &v : m_views)
	{
		if (v.inst)
		{
			v.inst->set_visible(false);
		}
		if (v.bib)
		{
			v.bib->set_visible(false); // kept with its template's quad (updateBib shows it again)
		}
		freeNode(v.shadowDecalId); // rebuilt on demand
		v.shadowDecalId = 0;
		v.shadowTexture.clear();
	}
	if (!templateName.empty() && !m_views.empty())
	{
		if (m_parked.size() >= kMaxParked)
		{
			for (auto &kv : m_parked)
			{
				for (IconView &v : kv.second)
				{
					freeNode(v.instId);
					freeNode(v.bibId);
				}
			}
			m_parked.clear();
		}
		std::vector<IconView> &slot = m_parked[templateName];
		for (IconView &v : slot)
		{
			freeNode(v.instId);
			freeNode(v.bibId);
		}
		slot = std::move(m_views);
	}
	else
	{
		for (IconView &v : m_views)
		{
			freeNode(v.instId);
			freeNode(v.bibId);
		}
	}
	m_views.clear();
}

W3DInstancer *PlacementGhostRenderer::newInstancer(const char *name)
{
	W3DInstancer *inst = memnew(W3DInstancer);
	++m_instancersMade;
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
	const uint64_t tIcon = Time::get_singleton()->get_ticks_usec();
	if (view.tt != icon.tt)
	{
		// The destination hub moves between slots as a wall span changes length.
		// Keep each slot's loaded models instead of rebuilding GPU resources every
		// time that slot alternates between a segment and the destination hub.
		for (const EntryView &entry : view.entries)
		{
			if (view.inst && entry.instance >= 0) view.inst->remove_instance(entry.instance);
		}
		view.entries.clear();
		view.tt = icon.tt;
	}
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
				m_modelsBuilt += it == view.models.end() ? 1 : 0;
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
	const uint64_t tu = Time::get_singleton()->get_ticks_usec();
	view.inst->update_now();
	const uint64_t tt = Time::get_singleton()->get_ticks_usec();
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
	const uint64_t tb = Time::get_singleton()->get_ticks_usec();
	updateBib(icon, view);
	const uint64_t ts = Time::get_singleton()->get_ticks_usec();
	updateShadowDecal(icon, view);
	const uint64_t te = Time::get_singleton()->get_ticks_usec();
	m_partMs[0] += (double)(tu - tIcon) / 1000.0; // lane FOGFX-1: the draw's parts (the HUD's frame timings)
	m_partMs[1] += (double)(tt - tu) / 1000.0;
	m_partMs[2] += (double)(tb - tt) / 1000.0;
	m_partMs[3] += (double)(ts - tb) / 1000.0;
	m_partMs[4] += (double)(te - ts) / 1000.0;
}

void PlacementGhostRenderer::updateShadowDecal(PlacementIcons::Icon &icon, IconView &view)
{
	// lane OWNERFIX-2: a model state's decal shadow (W3DModelDraw's Shadow = SHADOW_ALPHA_DECAL 0x20 / SHADOW_ADDITIVE_DECAL 0x40 / their _DYNAMIC forms 0x400 /
	// 0x800, bits of RW 0xD99EB0; ShadowTexture, ShadowSizeX / Y, ShadowOffsetX / Y; RW 0x4B92D3 parses them) is a projected decal under the drawable: the
	// retail data gives the well's / statue's BUILD_PLACEMENT_CURSOR state its area of effect (decal_hero_good, GONDOR_WELL_AOE_RADIUS_DECAL = 440).
	// INFERENCE (stop S-5032): drawn as the selection decals are (a Godot Decal on the terrain layer, S-2522) at the texture's own alpha, not faded by the
	// drawable's 0.45 opacity (the shadow's ShadowOpacity* fade is not run); only the placement ghost draws them (every other drawable's shadow is S-3304)
	const ModelShadowInfo *sh = nullptr;
	for (DrawEntry &e : icon.drawable->entries())
	{
		if (e.kind == W3D_DRAWKIND_MODEL && e.draw && !e.moduleHidden)
		{
			const ModelConditionInfo *info = e.draw->currentModelState();
			if (info && info->hasShadow && (info->shadow.type & 0xC60) && !info->shadow.texture.empty())
			{
				sh = &info->shadow;
				break;
			}
		}
	}
	Decal *decal = view.shadowDecalId ? Object::cast_to<Decal>(ObjectDB::get_instance(godot::ObjectID(view.shadowDecalId))) : nullptr;
	if (!sh)
	{
		if (decal)
		{
			decal->set_visible(false);
		}
		view.shadowTexture.clear();
		return;
	}
	const std::string file = sh->texture.find('.') == std::string::npos ? sh->texture + ".tga" : sh->texture;
	Ref<Texture2D> tex = texture(file);
	if (tex.is_null())
	{
		return; // texture() reported it
	}
	if (!decal)
	{
		decal = memnew(Decal);
		decal->set_name("PlacementShadowDecal");
		m_parent->add_child(decal);
		view.shadowDecalId = decal->get_instance_id();
	}
	if (view.shadowTexture != file)
	{
		decal->set_texture(Decal::TEXTURE_ALBEDO, tex);
		view.shadowTexture = file;
	}
	const Coord3D &p = *icon.drawable->getPosition();
	// SAGE (x, y, z up) -> Godot (x, z, -y); projected down over a box 200 units deep
	decal->set_position(Vector3(p.x + sh->offsetX, p.z, -(p.y + sh->offsetY)));
	decal->set_size(Vector3(sh->sizeX, 200.0f, sh->sizeY));
	decal->set_albedo_mix(1.0f);
	decal->set_cull_mask(1u << 19); // the terrain's layer (GodotMapTerrain)
	decal->set_upper_fade(0.0f);
	decal->set_lower_fade(0.0f);
	decal->set_visible(true);
	view.shadowSizeX = sh->sizeX;
	view.shadowSizeY = sh->sizeY;
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

Ref<StandardMaterial3D> PlacementGhostRenderer::bibMaterial(const std::string &tex, const Ref<Texture2D> &t)
{
	Ref<StandardMaterial3D> &m = m_bibMaterials[tex];
	if (m.is_null())
	{
		m.instantiate();
		m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA); // ZH W3DBibBuffer: SRCBLEND_SRC_ALPHA, DSTBLEND_ONE_MINUS_SRC_ALPHA, no depth write
		m->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
		m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, t);
	}
	return m;
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
	if (view.bib && view.bibTemplate != icon.tt)
	{
		// Sol review r1 (community PR #37): the quad is the template's footprint (PlacementGhost::bibOf); a reused wall slot that now shows another
		// template (a hub, a segment) gets a new one
		freeNode(view.bibId);
		view.bib = nullptr;
		view.bibId = 0;
		view.bibTexture.clear();
	}
	if (!view.bib)
	{
		view.bibTemplate = icon.tt;
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
		m_parent->add_child(view.bib);
		view.bibId = view.bib->get_instance_id();
	}
	if (view.bibTexture != tex)
	{
		// lane FOGFX-1 (FB-0055): one material per bib texture for the renderer's life (a new StandardMaterial3D and its texture cost about 9 ms of the
		// main thread on the first frame of every placement, measured on a GTX 1060)
		view.bib->set_material_override(bibMaterial(tex, t));
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
		// lane FOGFX-1: the two bib materials made with the game (its first idle frame), not on the first placement: Godot builds a material's shader
		// when a node first takes it, so a hidden node takes each once (measured: 12 ms of the main thread otherwise)
		for (const char *tex : { "TBBib.tga", "TBRedBib.tga" })
		{
			const Ref<Texture2D> t = texture(tex);
			if (t.is_valid())
			{
				MeshInstance3D *warm = memnew(MeshInstance3D);
				warm->set_visible(false);
				m_parent->add_child(warm);
				warm->set_material_override(bibMaterial(tex, t));
				warm->queue_free();
			}
		}
	}
	const std::string before = m_icons->templateName();
	const uint64_t t0 = Time::get_singleton()->get_ticks_usec();
	m_icons->update(ui, deltaSeconds * 1000.0, ghostOptions, judge);
	const uint64_t t1 = Time::get_singleton()->get_ticks_usec();
	m_lastIconsMs = (double)(t1 - t0) / 1000.0; // lane FOGFX-1: the icons' update apart from their draw (the HUD's frame timings)
	for (double &p : m_partMs)
	{
		p = 0.0;
	}
	for (const std::string &e : m_icons->errors())
	{
		error(e);
	}
	if (m_icons->templateName() != before)
	{
		// a new template (or none): the previous template's instancers are parked, the new one's taken back when it was placed before (lane FOGFX-1)
		clearTransient();
		parkViews(before);
		auto parked = m_parked.find(m_icons->templateName());
		if (parked != m_parked.end())
		{
			m_views = std::move(parked->second);
			m_parked.erase(parked);
		}
	}
	if (m_icons->icons().size() != m_views.size())
	{
		// community PR #37: a wall span's tile count changes as the cursor moves; the slots that stay keep their instancers and models (drawIcon swaps a
		// slot's template), only the slots past the new count are freed
		for (size_t i = m_icons->icons().size(); i < m_views.size(); ++i)
		{
			freeNode(m_views[i].instId);
			freeNode(m_views[i].bibId);
		}
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
	m_lastDrawMs = (double)(Time::get_singleton()->get_ticks_usec() - t1) / 1000.0; // the icons' instancers, tints, bibs
	updateArrow(ui);
	updateFormation(game,ui,judge);
	m_formationSlots=ui.formationSlots;
	// merge queue q21: the farm's claimable share while it is placed is drawn by OWNERFIX-2's TerrainResourceFeedback (GodotInGameHud: RW 0x75B561's
	// share, the claimant decal with the claimed ground cut out, the placement text); PR #37's own decal and label here drew it a second time
}

Dictionary PlacementGhostRenderer::state() const
{
	Dictionary s;
	Array formationTextures;for(const auto &name:m_formationTextures)formationTextures.push_back(toGodotString(name));
	s["formation_textures"]=formationTextures;
	s["formation_visible"] = m_formation && m_formation->is_visible();
	Array formation;
	for(const auto &slot:m_formationSlots) {
		Dictionary item;item["id"]=(int64_t)slot.object;item["x"]=slot.position.x;item["y"]=slot.position.y;item["angle"]=slot.angle;
		formation.push_back(item);
	}
	s["formation_slots"]=formation;
	s["template"] = m_icons ? toGodotString(m_icons->templateName()) : String();
	s["resource_radius"] = m_icons ? m_icons->resourceRadius() : 0;
	s["resource_share"] = m_icons ? m_icons->resourceShare() : 0;
	s["resource_visible"] = m_resourceLabel && m_resourceLabel->is_visible();
	s["castle"] = m_icons && m_icons->castle();
	s["parked"] = (int64_t)m_parked.size();       // lane FOGFX-1: templates whose instancers are kept
	s["models_built"] = (int64_t)m_modelsBuilt;   // lane FOGFX-1: add_model calls so far (a placement of a kept template adds none)
	s["instancers_made"] = (int64_t)m_instancersMade;
	s["bib_materials"] = (int64_t)m_bibMaterials.size(); // lane FOGFX-1: shared per bib texture (2: TBBib, TBRedBib)
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
			d["bib_template"] = i < m_views.size() && m_views[i].bib && m_views[i].bibTemplate ? toGodotString(m_views[i].bibTemplate->getName()) : String();
			d["shadow_decal"] = i < m_views.size() ? toGodotString(m_views[i].shadowTexture) : String(); // lane OWNERFIX-2
			d["shadow_decal_size"] = i < m_views.size() ? m_views[i].shadowSizeX : 0.0f;
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

namespace godot {
void PlacementGhostRenderer::updateFormation(LiveGame &game, const InGameUI &ui, bool idle) {
 const float opacity=ui.formationActive || ui.clientFrame()<=ui.formationExpires?1.f:std::max(0.f,1.f-(ui.clientFrame()-ui.formationExpires)/15.f);
 if(opacity<=0 || ui.formationSlots.empty()) { if(m_formation)m_formation->hide();return; }
 if(!idle)return;
 if(!m_formation) {
  m_formation=memnew(MeshInstance3D);m_parent->add_child(m_formation);
  m_formationId=m_formation->get_instance_id();
  m_formation->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
 }
 Ref<ImmediateMesh> mesh;mesh.instantiate();
 const auto *fa=ui.formationData;if(!fa)return;
 struct Sprite {std::string texture;float x,y,width,height,angle;};
 std::vector<Sprite> sprites;
 auto add=[&](const std::string &name,float x,float y,float angle,float width=-1.f){
  const auto *decal=fa->decal(name,true);if(!decal || decal->texture.empty()){if(!name.empty())error("formation preview decal missing: "+name);return;}
  sprites.push_back({decal->texture,x,y,width<0?decal->width:width,decal->height,angle});
 };
 for(const auto &slot:ui.formationSlots) {
  auto *object=game.logic().findObjectByID(slot.object);
  auto *horde=object?dynamic_cast<HordeContain *>(object->getContain()):nullptr;
  if(horde) {
   const auto positions=FormationAssistant::rankOffsets(*object);
   const auto center=FormationAssistant::rankCenter(*object);
   const float c=std::cos(slot.angle),s=std::sin(slot.angle);
   for(const auto &position:positions) {
    const float x=position.x-center.x,y=position.y-center.y;
    add(slot.preview,slot.position.x+x*c-y*s,slot.position.y+x*s+y*c,slot.angle+1.5707964f);
   }
  } else add(slot.preview,slot.position.x,slot.position.y,slot.angle+1.5707964f);
 }
 const float angle=ui.formationAngle,c=std::cos(angle),s=std::sin(angle);
 const auto *head=fa->decal(fa->head),*body=fa->decal(fa->body),*base=fa->decal(fa->base);
 const float baseWidth=base?base->width:0,bodyWidth=body?body->width:0;
 const float side=std::max(ui.formationDragLength,baseWidth*.5f+bodyWidth*.5f);
 const float offset=(body?body->height*.5f:0)+30,front=(body?body->height:0)+30+(head?head->height*.5f:0);
 const auto anchor=ui.formationAnchor;
 add(fa->body,anchor.x+offset*c,anchor.y+offset*s,angle-1.5707964f,side*2-baseWidth);
 add(fa->base,anchor.x+offset*c-(side-10)*s,anchor.y+offset*s+(side-10)*c,angle+1.5707964f);
 add(fa->base,anchor.x+offset*c+(side-10)*s,anchor.y+offset*s-(side-10)*c,angle-1.5707964f);
 // The texture quad axes differ from the native decal: flip directional art.
 add(fa->head,anchor.x+front*c,anchor.y+front*s,angle+1.5707964f);
 std::map<std::string,std::vector<Sprite>> batches;for(const auto &sprite:sprites)batches[sprite.texture].push_back(sprite);
 const Player *player=game.logic().players().getLocalPlayer();
 const auto rgb=player?player->getPlayerColor():0xffffff;
 const Color tint(((rgb>>16)&255)/255.f,((rgb>>8)&255)/255.f,(rgb&255)/255.f,opacity);
 m_formationTextures.clear();
 for(const auto &batch:batches) {
  const Ref<Texture2D> tex=texture(batch.first+".tga");if(tex.is_null())continue;
  m_formationTextures.push_back(batch.first+".tga");
  Ref<StandardMaterial3D> material;material.instantiate();
  material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
  material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
  const bool arrow=(head && batch.first==head->texture)||(body && batch.first==body->texture)||(base && batch.first==base->texture);
  material->set_albedo(arrow?Color(1,1,1,opacity):tint);
  material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO,tex);
  mesh->surface_begin(Mesh::PRIMITIVE_TRIANGLES,material);
  for(const auto &sprite:batch.second) {
   const float c=std::cos(sprite.angle),s=std::sin(sprite.angle);
   const Vector2 uv[4]={{0,0},{1,0},{1,1},{0,1}};
   for(int index:{0,1,2,0,2,3}) {
    const float lx=(uv[index].x-.5f)*sprite.width,ly=(uv[index].y-.5f)*sprite.height;
    const float x=sprite.x+lx*c-ly*s,y=sprite.y+lx*s+ly*c;
    mesh->surface_set_uv(uv[index]);mesh->surface_add_vertex(Vector3(x,game.logic().getGroundHeight(x,y)+2,-y));
   }
  }
  mesh->surface_end();
 }
 m_formation->set_mesh(mesh);m_formation->show();
}

} // namespace godot
