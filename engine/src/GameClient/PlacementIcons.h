// OpenBFME. GPL-3.0.
//
// PlacementIcons (lane BUILD-5 r2): the building placement ghost as RotWK makes it, real drawables of the pending template and, for a castle, of its layout's
// pieces (W3DInGameUI's place icon slots, InGameUI + 0x544: { Drawable *, angle } each, GlobalData + 0xA94 slots). Client only: the drawables live on the
// render side and nothing here changes the logic; the legality answers only colour the icons.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * placeBuildAvailable (RW 0x69C5E6): the drawable of the template (ThingFactory::newDrawable RW 0x6CFE8C, status 8), the builder's player colour as its
//     indicator colour (RW 0x69C731 .. 0x69C74D: Player + 0x2A4, the night colour, when GlobalData + 0x134 (the time of day) is 4 = NIGHT, else Player + 0x2A0;
//     Drawable::setIndicatorColor RW 0x674504), the orientation PlacementViewAngle (template + 0x4E0, RW 0x69C6FC), opacity 0.45 (Drawable + 0xB0 = RW 0xC12408,
//     RW 0x69C714) and the model condition BUILD_PLACEMENT_CURSOR (bit 0x6C, RW 0x69C70D -> RW 0x679512); the draw modules then pick the state's model,
//     animation and BeginScript as for any drawable (W3DModelDraw, the drawable Lua host);
//   * the placement update (RW 0x6A2AE5), each client frame: slot 0 at the ghost's site and angle (RW 0x6A3847 / 0x6A385B); a castle (RW 0x73CD88) gets
//     one drawable per entry of the local faction's layout (RW 0x798F70, RW 0x72D72F) whose template exists (RW 0x6D1305), BUILD_PLACEMENT_CURSOR and 0.45 too
//     but no indicator colour (RW 0x6A3551 .. 0x6A374A), at the entry's offset turned by the ghost's angle (RW 0x6A359E .. 0x6A3690), on the ground (TerrainLogic
//     vslot 0x18, RW 0x6A36A8), turned by the ghost's angle ALONE (RW 0x6A36FB: setOrientation(the ghost's angle); the slot keeps it normalized, RW 0x6A36B7);
//     entries past the layout's end lose their drawables (RW 0x6A3784);
//   * on odd client frames (RW 0x6A3939) every slot is judged (RW 0x6A396A .. 0x6A3B20): a castle's slot only when its template is KindOf BASE_SITE (template
//     + 0x117 & 1: the layout's build plots), any other slot always, by BuildAssistant::isLocationLegalToBuild (slot 0x44) at the drawable's position with the
//     slot's angle, the options 0x48F and the builder; a castle remembers that a plot failed (RW 0x6A39D1). Then, by the code: 0 -> no tint (or, in a
//     castle after a failed plot, IllegalBuildColor) and the faction bib removed (TerrainVisual vslot 0x34); 9 -> the tint (0, 0, 0.5), blue 1.7 on client
//     frames with bit 4 (RW 0x6A3A74 .. 0x6A3AB7), and the bib not highlighted (vslot 0x30(drawable, 0, 0.0)); any other -> IllegalBuildColor (1, 0, 0) and
//     the highlighted bib (vslot 0x30(drawable, 1, 0.0)).
// Not ported (stop S-3302): the restricted-area code 8 of the local player's slots (RW 0x707141), the two status calls on a castle's drawables (RW 0x670AF4(1),
// RW 0x6718FB(0)); the drawables take no client modules (ClientUpdate / ClientBehavior: nothing of the placement uses them).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class CastleTemplateStore;
class Drawable;
struct DrawServices;
class GameLogic;
class InGameUI;
class ThingTemplate;

class PlacementIcons
{
public:
	enum Tint
	{
		TINT_NONE = 0,
		TINT_ILLEGAL = 1, ///< IllegalBuildColor (1, 0, 0), RW 0xDA0B28
		TINT_CODE9 = 2    ///< (0, 0, tintBlue)
	};
	enum Bib
	{
		BIB_NONE = -1,
		BIB_NORMAL = 0,   ///< TBBib.tga
		BIB_HIGHLIGHT = 1 ///< TBRedBib.tga
	};
	struct Icon
	{
		std::unique_ptr<Drawable> drawable;
		const ThingTemplate *tt = nullptr;
		bool piece = false;       ///< a castle layout entry (slot > 0 of a castle)
		bool baseSite = false;    ///< KindOf BASE_SITE: judged in a castle
		float offX = 0.0f, offY = 0.0f; ///< the layout entry's offset from the castle centre (unrotated)
		float angle = 0.0f;       ///< the slot's angle (RW + 4)
		int code = 0;             ///< the last legality answer (LegalBuildCode; 0 until judged)
		Tint tint = TINT_NONE;
		float tintBlue = 0.5f;
		Bib bib = BIB_NONE;
		Icon();
		~Icon();
		Icon(Icon &&) noexcept;
		Icon &operator=(Icon &&) noexcept;
	};

	PlacementIcons(DrawServices &services, GameLogic &logic);
	~PlacementIcons();
	// the castle layouts (null: a castle is drawn as its own drawable alone, reported once)
	void setCastleStore(CastleTemplateStore *store) { m_castles = store; }

	// once per client frame, after the placement translator set the ghost's site (PlaceEventTranslator::handleBuildPlacements): (re)makes the drawables
	// of a new pending template, poses them, steps their animations by `elapsedMs`, and judges them on odd client frames with `ghostOptions`
	// lane BUILD-5 r3: `judge` false on the device's busy-worker frames: the drawables are posed and animated only (no rebuild, no legality: those read the
	// live logic); their draw modules' problems are forwarded to errors() every frame (DrawableManager::collect's rule for the game's drawables)
	void update(const InGameUI &ui, double elapsedMs, unsigned ghostOptions, bool judge = true);
	void clear();

	const std::vector<Icon> &icons() const { return m_icons; }
	std::vector<Icon> &icons() { return m_icons; }
	const std::string &templateName() const { return m_template; }
	bool castle() const { return m_castle; }
	// problems (a draw module that does not build, a layout that does not load), each reported once
	const std::vector<std::string> &errors() const { return m_errors; }
	// the draw modules' counted problems (a hidden sub object the model lacks; an animation that does not resolve is an error, r4), each once
	const std::vector<std::string> &notes() const { return m_notes; }

	// the player colour the ghost's drawable takes (RW 0x69C731: the night colour at night); 0 when there is no builder
	static std::uint32_t indicatorColor(const GameLogic &logic, ObjectID builder);

private:
	void rebuild(const InGameUI &ui);
	std::unique_ptr<Drawable> make(const ThingTemplate &tt);
	void error(const std::string &e);
	void collectProblems();
	DrawServices &m_services;
	GameLogic &m_logic;
	CastleTemplateStore *m_castles = nullptr;
	std::vector<Icon> m_icons;
	std::string m_template;
	ObjectID m_source = INVALID_ID;
	bool m_castle = false;
	std::vector<std::string> m_errors;
	std::vector<std::string> m_notes;
};
