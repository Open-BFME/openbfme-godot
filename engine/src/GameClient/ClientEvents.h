// OpenBFME. GPL-3.0.
//
// ClientEvents (lane SMOOTH-1, stop S-810): what the simulation tells its client, as ordered, data-only events.
//
// The simulation calls ObjectClientHooks (GameLogic/GameLogic.h) at the points where ZH / RotWK call the Drawable directly: the object's creation
// (RW 0x628882 sendObjectCreated: the drawable is made and bound), its destruction, every changed model condition bit (ZH Object::setModelConditionFlags
// -> Drawable), the module / sub object visibility requests of upgrades and Lua (RW 0x6789B4 / 0x672823), and the map placement of a map object.
// ClientEventRecorder implements the hooks by appending one event per call, numbered (logic frame, sequence) in call order, holding only owned data
// (object ids, template pointers - immutable for the loaded game -, flags, names), never an Object pointer. The render side applies the events in
// sequence, each exactly once (DrawableManager::applyEvents), on its own thread and at its own time.
//
// The one hook that answers the simulation, showModule, answers from the template's draw module tags: the drawable makes every draw module of its
// template (RW 0x679FD7, S-114) and RW 0x6789B4 returns true when one of them has the tag, so the template gives the same answer the drawable would,
// independent of when (or whether) a renderer consumed the creation event.
//
// INFERENCE (port structure, not a retail fact): retail calls the drawable synchronously on one thread; the event order here is that call order, so a
// renderer that applies every event in sequence reaches the state retail's drawable had after the same calls.
//
// Lane ANIM-1, TARGET: a model condition change does NOT reach the draw modules at once. RW 0x68D607 hands the object's whole new flag set to
// Drawable::replaceModelConditionFlags (RW 0x679512), which stores it (+0x258) and sets the dirty byte +0x443; the draw modules' replaceModelConditionState
// runs when the dirty flags are flushed: in Drawable::updateDrawable on the first client frame of a logic frame (RW 0x6759C4 .. 0x675A11, gated by
// RW 0x63252F), before a bone query (RW 0x674673 / 0x6756A1 / 0x674B1F) and where the logic calls RW 0x67449C (the pre-fire RW 0x69213E, object creation
// paths). So MODEL_CONDITION only updates the drawable's flags and MODEL_FLUSH applies them: one draw state selection per logic frame with the frame's
// net flags, not one per changed bit (a cleared FIRING bit and a set RELOADING bit used to select the idle state in between).

#pragma once

#include "Common/GameCommon.h"
#include "Common/INIDataTypes.h"
#include "Common/ModelState.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectTypes.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameClient/DrawableScriptTarget.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

class ThingTemplate;
struct MapObjectDrawable;

struct ClientEvent
{
	enum Kind : std::uint8_t
	{
		CREATED,         ///< tmpl, flags (time of day / weather), house colour, position, angle
		DESTROYED,
		MODEL_CONDITION, ///< bit, on
		SHOW_MODULE,     ///< name (the module tag), visible, permanent
		SHOW_SUB_OBJECT, ///< name, visible, permanent
		PLACEMENT,       ///< flags (when setFlags), scale (when setScale), mirror
		REPLACED,        ///< lane STEALTH-2 (RW 0x776F03): the drawable made again as CREATED's fields say (tmpl: the look, flags: the object's conditions)
		MODEL_FLUSH,     ///< lane ANIM-1 (RW 0x67449C / 0x6759C4): the drawable's changed flags reach its draw modules; scriptTarget, weapon timing
		FADE_IN,         ///< lane BUILD-4 (RW 0x670AA2): fadeFrames client frames
		CUSTOM_COLORS,   ///< lane CAH-2 (RW 0x80AF0B -> 0x6727B0): customKind and customColors, the drawable's house colour set
	};
	Kind kind = CREATED;
	UnsignedInt frame = 0;   ///< the logic frame that made it (0 during the load)
	std::uint32_t seq = 0;   ///< call order over the whole game
	ObjectID object = INVALID_ID;
	const ThingTemplate *tmpl = nullptr;
	ModelConditionFlags flags;
	bool hasHouseColor = false;
	std::uint32_t houseColor = 0;
	Coord3D position;
	float angle = 0.0f;
	KindOfMaskType kindOf{};   ///< CREATED: the object's KindOf bits (what the device layer asks of a drawable, e.g. INFANTRY's light set)
	// CREATED: the drawable's ClientUpdate / ClientBehavior modules in creation order, resolved on the logic owner (ModuleFactory::resolveClientModule)
	struct ClientModule
	{
		std::string name;
		const ModuleData *data = nullptr;
		ModuleFactory::ResolvedModule resolved;
	};
	std::vector<ClientModule> clientModules;
	int bit = -1;
	bool on = false;
	DrawableScriptTarget scriptTarget; ///< MODEL_FLUSH (lane FX-3 / ANIM-1): the object's target record when the flags reach the draw, for the state scripts they start
	bool hasWeaponTiming = false;      ///< MODEL_FLUSH (lane ANIM-1): the object has a current weapon (RW 0x68B58C(0)); weaponTimingFrames is its cycle
	int weaponTimingFrames = 0;        ///< ObjectWeapons::drawWeaponTimingFrames (RW 0x4BEE31 .. 0x4BEE91), what UseWeaponTiming animations divide by
	std::string name;
	bool visible = false, permanent = false;
	bool setFlags = false, setScale = false, mirror = false;
	float scale = 1.0f;
	UnsignedInt fadeFrames = 0;
	int customKind = 0;                             ///< CUSTOM_COLORS (lane CAH-2)
	std::uint32_t customColors[3] = { 0u, 0u, 0u }; ///< CUSTOM_COLORS: ARGB
};

class ClientEventRecorder : public ObjectClientHooks
{
public:
	explicit ClientEventRecorder(GameLogic &logic) : m_logic(logic) {}

	void objectCreated(Object &obj) override;
	void objectDestroyed(Object &obj) override;
	void modelConditionChanged(Object &obj, int bit, bool on) override;
	void flushModelConditions(Object &obj) override;
	bool showModule(Object &obj, const std::string &tag, bool visible, bool permanent) override;
	void showSubObject(Object &obj, const std::string &name, bool visible, bool permanent) override;
	void replaceDrawable(Object &obj, const ThingTemplate *tmpl, bool hasColor, std::uint32_t color) override;
	void fadeIn(Object &obj, UnsignedInt frames) override;
	void setCustomColors(Object &obj, int kind, std::uint32_t c0, std::uint32_t c1, std::uint32_t c2) override; // lane CAH-2
	// MapObjectLoop's afterCreate for a map object (DrawableManager::applyPlacement): the client part of the placement
	void placement(Object &obj, const MapObjectDrawable &placement);

	// the events since the last take, in order (called by the simulation owner when it publishes a frame). Lane ANIM-1: with `endOfFrame` the objects whose
	// flags changed since their last flush get their MODEL_FLUSH first, in the order of their first change (the end of the logic frame: RW 0x6759C4); the
	// render side's take of what the main thread caused between frames passes false, so the flushes do not depend on when (or whether) it ran
	std::vector<ClientEvent> take(bool endOfFrame = true);
	size_t pending() const { return m_events.size(); }
	std::uint32_t recorded() const { return m_seq; }

	// RW 0x6789B4's answer from the template: a draw module of `tt` has the tag
	static bool templateHasDrawModuleTag(const ThingTemplate &tt, const std::string &tag);

private:
	ClientEvent &push(ClientEvent::Kind kind, const Object &obj);
	void pushFlush(const Object &obj);

	// lane ANIM-1: the objects whose MODEL_CONDITION events have not been flushed (RW Drawable + 0x443), in the order of their first change
	std::vector<ObjectID> m_dirty;
	std::set<ObjectID> m_dirtySet;

	GameLogic &m_logic;
	std::vector<ClientEvent> m_events;
	std::uint32_t m_seq = 0;
};
