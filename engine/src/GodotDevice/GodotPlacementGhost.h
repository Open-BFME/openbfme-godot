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
// the ghost casts no shadow (translucent drawables are not shadow-map casters, stop S-3304).

#pragma once

#include "GameClient/PlacementIcons.h"
#include "GameClient/InGameUI.h"

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
class GameTextSource;
class MappedImageCollection;
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
	// the farm placement feedback's sources (community PR #37): the HUD's mapped images (InGameUI PlaceTerrainResourceClaimantDecal names one) and game text
	void setFeedbackSources(const MappedImageCollection *images, const GameTextSource *text)
	{
		m_images = images;
		m_text = text;
	}
	// { template, castle, anchored, arrow, icons: [ { template, x, y, z, angle, code, tint: Color (a 0: none), bib: "" | "TBBib.tga" | "TBRedBib.tga",
	//   instances, piece, base_site } ], errors: [...] }
	Dictionary state() const;
	const std::vector<std::string> &errors() const { return m_errors; }
	double lastIconsMs() const { return m_lastIconsMs; } // lane FOGFX-1: the last sync's PlacementIcons::update and its icons' draw
	double lastDrawMs() const { return m_lastDrawMs; }
	const double *lastDrawParts() const { return m_partMs; } // the instances, update_now, the tint, the bib, the shadow decal

private:
	double m_lastIconsMs = 0.0, m_lastDrawMs = 0.0;
	double m_partMs[5] = {};
	unsigned long long m_modelsBuilt = 0, m_instancersMade = 0;
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
		const ThingTemplate *bibTemplate = nullptr; ///< the template the bib quad was built for (a reused wall slot can change template)
		// lane OWNERFIX-2: the decal shadow of the drawable's current model state (Shadow = SHADOW_ALPHA_DECAL ...: e.g. a well's or a statue's area
		// of effect on its BUILD_PLACEMENT_CURSOR state)
		uint64_t shadowDecalId = 0;
		std::string shadowTexture;
		float shadowSizeX = 0.0f, shadowSizeY = 0.0f;
		Color tint = Color(0, 0, 0, 0);
		bool tintApplied = false;
		int instances = 0;
	};
	void error(const std::string &e);
	void clearTransient(); // the formation preview, the resource decal and label
	void parkViews(const std::string &templateName); // lane FOGFX-1: m_views hidden and kept under the template's name
	static constexpr size_t kMaxParked = 48;         ///< lane FOGFX-1: more parked templates than this and the parked ones are freed
	std::map<std::string, std::vector<IconView>> m_parked;
	W3DInstancer *newInstancer(const char *name);
	void drawIcon(size_t i, PlacementIcons::Icon &icon, IconView &view);
	void applyTint(IconView &view, const Color &tint);
	void updateBib(PlacementIcons::Icon &icon, IconView &view);
	Ref<StandardMaterial3D> bibMaterial(const std::string &tex, const Ref<Texture2D> &t); // lane FOGFX-1: shared per texture
	void updateShadowDecal(PlacementIcons::Icon &icon, IconView &view); // lane OWNERFIX-2
	void updateArrow(const InGameUI &ui);
	void updateFormation(LiveGame &game,const InGameUI &ui,bool idle);
	std::vector<InGameUI::FormationSlot> m_formationSlots;
	std::vector<std::string> m_formationTextures;
	MeshInstance3D *m_formation=nullptr;
	uint64_t m_formationId=0;
	Ref<Texture2D> texture(const std::string &name);

	const MappedImageCollection *m_images = nullptr;
	const GameTextSource *m_text = nullptr;
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
	std::map<std::string, Ref<StandardMaterial3D>> m_bibMaterials; ///< lane FOGFX-1: the bib's material per texture, shared by every bib
	Ref<W3DModelBuilder> m_textureLoader;
	W3DInstancer *m_arrow = nullptr;
	uint64_t m_arrowId = 0;
	int64_t m_arrowModel = -2, m_arrowInstance = -1;
	bool m_arrowShown = false, m_anchored = false;
	std::vector<std::string> m_errors;
};
} // namespace godot
