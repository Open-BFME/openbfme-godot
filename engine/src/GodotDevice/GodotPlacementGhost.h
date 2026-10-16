// OpenBFME. GPL-3.0.
//
// The building placement ghost on screen (lane BUILD-5 r2): GameClient/PlacementIcons' drawables (RotWK's place icons: the template's drawable with
// BUILD_PLACEMENT_CURSOR, 0.45 opacity and the builder's colour, a castle's layout pieces) drawn as the game draws its drawables (W3DInstancer: the draw
// module's model, animation clips, hidden sub objects, house colour and opacity), each icon on an instancer of its own so it takes its own tint; the faction
// bib under a refused icon; W_Arrow on the ghost while the placement is anchored (W3DInGameUI::drawPlaceAngle RW 0x48EAD8: the anchor and the arrow are both
// W_Arrow, RW 0xBDDBB0, set to the ghost's transform). Run by the HUD after the displayed camera is applied (InGameHudNode::_process).
//
// DONOR (ZH W3DScene.cpp:662-698): Drawable::colorTint (RW 0x6757BB) adds the tint to every global light's diffuse and to the ambient (the W3D shader's
// w3d_tint, set on the icon's own instancer's nodes); INFERENCE (stop S-3302): RotWK's scene lighting of the tint was not read; the bib is ZH's W3DBibBuffer
// quad (alpha blended, the corners at the drawable's height). Every problem (a model, a texture, a layout) is an error reported once (push_error) and listed
// in state().errors, never a silent stand-in. r3: the instancers draw in the alpha-blended pass (W3DInstancer::set_alpha_override, DONOR ZH dx8renderer.cpp:1884-1916);
// the ghost casts no shadow (no drawable does yet, stop S-3304).

#pragma once

#include "GameClient/PlacementIcons.h"

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/decal.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

class CastleTemplateStore;
class InGameUI;
class LiveGame;

namespace godot
{
class RetailFileSystem;
class W3DInstancer;
class W3DModelBuilder;

class PlacementGhostRenderer
{
public:
	PlacementGhostRenderer(Node3D *parent, const Ref<RetailFileSystem> &fs);
	~PlacementGhostRenderer();
	// one rendered frame: PlacementIcons::update, then the instancers, tints, bibs and the arrow
	// `judge` false on a frame the logic worker runs (InGameUI's pose only: PlacementIcons::update)
	void sync(LiveGame &game, const InGameUI &ui, double deltaSeconds, unsigned ghostOptions, bool judge = true);
	void clear();
	// { template, castle, anchored, arrow, icons: [ { template, x, y, z, angle, code, tint: Color (a 0: none), bib: "" | "TBBib.tga" | "TBRedBib.tga",
	//   instances, piece, base_site } ], errors: [...] }
	Dictionary state() const;
	const std::vector<std::string> &errors() const { return m_errors; }

private:
	struct EntryView
	{
		int64_t model = -1, instance = -1;
		std::string modelName;
		const void *hiddenModel = nullptr;
		std::uint32_t hiddenGeneration = ~0u;
		std::vector<std::string> hidden;
	};
	struct IconView
	{
		const ThingTemplate *tt = nullptr;
		W3DInstancer *inst = nullptr;
		std::map<std::string, int64_t> models;
		std::vector<EntryView> entries;
		MeshInstance3D *bib = nullptr;
		uint64_t instId = 0, bibId = 0; ///< the nodes' ids (freed by id: GodotPlacementGhost.cpp freeNode)
		std::string bibTexture;
		Color tint = Color(0, 0, 0, 0);
		bool tintApplied = false;
		int instances = 0;
	};
	void error(const std::string &e);
	W3DInstancer *newInstancer(const char *name);
	void drawIcon(size_t i, PlacementIcons::Icon &icon, IconView &view);
	void applyTint(IconView &view, const Color &tint);
	void updateBib(PlacementIcons::Icon &icon, IconView &view);
	void updateArrow(const InGameUI &ui);
	void updateResources(const InGameUI &ui);
	Ref<Texture2D> texture(const std::string &name);

	Decal *m_resourceDecal = nullptr;
	Label *m_resourceLabel = nullptr;
	uint64_t m_resourceDecalId = 0, m_resourceLabelId = 0;
	Node3D *m_parent = nullptr;
	Ref<RetailFileSystem> m_fs;
	std::unique_ptr<CastleTemplateStore> m_castles;
	std::unique_ptr<PlacementIcons> m_icons;
	LiveGame *m_game = nullptr;
	std::vector<IconView> m_views;
	std::map<std::string, Ref<Texture2D>> m_textures;
	Ref<W3DModelBuilder> m_textureLoader;
	W3DInstancer *m_arrow = nullptr;
	uint64_t m_arrowId = 0;
	int64_t m_arrowModel = -2, m_arrowInstance = -1;
	bool m_arrowShown = false, m_anchored = false;
	std::vector<std::string> m_errors;
};
} // namespace godot
