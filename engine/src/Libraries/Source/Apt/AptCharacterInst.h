// OpenBFME. GPL-3.0.
//
// EA Apt character instances: the display-list objects a movie places (AptCIH values of the EA library).
//
// Target facts (clean BFME2 1.06 game.dat, virtual addresses; the library is APT 0.19.03, spec menus-apt.md 3.1):
//   instance value types (AptCIH type field, 0x006DBB30 get()): 0x0C shape, 0x0D sprite, 0x0E button, 0x0F edit
//   text, 0x10 static text, 0x11 morph, 0x12 movie (the root instance of a loaded movie); a sprite or movie is a
//   "sprite instance base" (predicate 0x00ACFCD0).  AptDisplayList::place (0x00AF85E0) creates one instance per
//   placed character: the character types 5/4/2/10/1/8 map to 0x0D/0x0E/0x0F/0x10/0x0C/0x11 (0x00AF8734-0x00AF8889).
//   A new sprite instance starts at frame -1 with flags |= 0x3000000 (bit 24 "needs load events", bit 25 "playing";
//   0x00AF871C, sprite base constructor 0x00AED060), a button starts at state frame 0 (0x00AF8770).
//   The instance name is added to the PARENT's own hash (0x00AF89A3 -> 0x00B0B410), so `_root.SoloPlayNav` exists
//   as soon as the place object has run.
//
// Frame update (AptCIH advance 0x00AE2D60), seek (AptCIH gotoFrame 0x00AE2C10), frame controls (AptMovie.cpp
// 0x00B0F040 / 0x00B0F370 / 0x00B0F680), the action pool (AptAnimation.cpp 0x00AE4B80 push back, 0x00AE4C70 push
// front, 0x00AE6540 run, 0x00AE4390 new-instance flush) and clip events (0x00AE2010) are documented where they
// are ported (AptCharacterInst.cpp, Apt.cpp).
//
// The Open-BFME-1 decompile does not hold these bodies (its Apt.cpp / AptCharacterInstSprite.cpp are unrelated
// fragments); every ordering fact below comes from the BFME2 binary.  Anything not read there is marked
// UNVERIFIED and registered in docs/STOPS.md (S-100..S-109).

#pragma once

#include "Libraries/Source/Apt/AptActionDecoder.h"
#include "Libraries/Source/Apt/AptFile.h"
#include "Libraries/Source/Apt/AptObject.h"
#include "Libraries/Source/Apt/AptValue.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Apt;
class AptSpriteInst;
class AptButtonInst;

// Runtime clip-event bits.  The 24-bit mask of a PlaceObject clip action and the instance event flags use the SWF
// ClipEventFlags bits in little-endian order.  Evidence: the event table at 0x00DDC2E8 (mask -> handler-name string
// id, read by 0x00AE22E5 and 0x00AE23BE) and the fire switch at 0x00AE2111 (0x200 / 0x40000 / 0x4 run immediately,
// 0x2 and 0x20000 go to the front of the action pool, 0x4000 and 0x2000 to the back).  Spec menus-apt.md 2.3 gives
// the OpenSAGE bit order, which is wrong for the retail files (retail data: mask 0x1 on 640 place objects, 0x200 on
// 241, 0x40000 on 121; the OpenSAGE order would make those DragOut, Initialize/Load swapped).
enum AptEventMask : std::uint32_t
{
	APT_EVT_LOAD = 0x1,
	APT_EVT_ENTERFRAME = 0x2,
	APT_EVT_UNLOAD = 0x4,
	APT_EVT_MOUSEMOVE = 0x8,
	APT_EVT_MOUSEDOWN = 0x10,
	APT_EVT_MOUSEUP = 0x20,
	APT_EVT_KEYDOWN = 0x40,
	APT_EVT_KEYUP = 0x80,
	APT_EVT_DATA = 0x100,
	APT_EVT_INITIALIZE = 0x200,
	APT_EVT_PRESS = 0x400,
	APT_EVT_RELEASE = 0x800,
	APT_EVT_RELEASEOUTSIDE = 0x1000,
	APT_EVT_ROLLOVER = 0x2000,
	APT_EVT_ROLLOUT = 0x4000,
	APT_EVT_DRAGOVER = 0x8000,
	APT_EVT_DRAGOUT = 0x10000,
	APT_EVT_KEYPRESS = 0x20000,
	APT_EVT_CONSTRUCT = 0x40000,
	APT_EVT_MOUSEWHEEL = 0x80000
};

// The 17 table entries of 0x00DDC2E8: the runtime mask and the member function name the script may assign.
struct AptEventNameEntry
{
	std::uint32_t mask;
	const char *name;
};
const std::vector<AptEventNameEntry> &AptEventNames();
// Index of `mask` in the table, or 0 when absent: the EA search loop (0x00AE22E3..0x00AE22F6) leaves the index at 0
// ("onEnterFrame") for a mask without an entry.
std::size_t AptEventTableIndex(std::uint32_t mask, bool *found);

struct AptMatrix
{
	float a = 1, b = 0, c = 0, d = 1; // x' = a*x + c*y + tx ; y' = b*x + d*y + ty (the SWF matrix, file order m[0..3])
	float tx = 0, ty = 0;

	void apply(float x, float y, float &ox, float &oy) const
	{
		ox = a * x + c * y + tx;
		oy = b * x + d * y + ty;
	}
	AptMatrix concat(const AptMatrix &inner) const; // this * inner: inner applied first
	AptMatrix inverse(bool *ok) const;
};

struct AptColorTransform
{
	float mul[4] = { 1, 1, 1, 1 };  // r g b a, 0..1
	float add[4] = { 0, 0, 0, 0 };  // r g b a, 0..255 units
	AptColorTransform concat(const AptColorTransform &inner) const; // mul = mul * inner.mul, add = add + inner.add (RW 0xB225B0)
};

// A character with the movie that owns its tables (an imported character belongs to the exporting movie).
struct AptCharRef
{
	std::shared_ptr<const AptFile> file;
	const AptCharacter *character = nullptr;
};

// ---------------------------------------------------------------------------------------------------------------
// AptCharacterInst: one placed character.  Script-visible: the members and `_x`-style properties below.
// ---------------------------------------------------------------------------------------------------------------
// lane UI-1: RotWK's scale / rotation record of a clip's matrix (RW 0xAF4890): signed scale percents and the rotation in degrees
struct AptScaleRotation
{
	float xscale = 100.0f;
	float yscale = 100.0f;
	float rotationDegrees = 0.0f;
};
AptScaleRotation AptDecomposeMatrix(const AptMatrix &m);

class AptCharacterInst : public AptObject
{
public:
	enum class Type : std::uint8_t
	{
		Shape = 0x0C,
		Sprite = 0x0D,
		Button = 0x0E,
		EditText = 0x0F,
		StaticText = 0x10,
		Morph = 0x11,
		Movie = 0x12,
		Unsupported = 0xFF // a character the parser has no layout for (Video, Sound, ...): placed as a no-op instance
	};

	AptCharacterInst(Apt &apt, Type type);

	Type type() const { return m_type; }
	bool isSpriteBase() const { return m_type == Type::Sprite || m_type == Type::Movie; }
	AptSpriteInst *asSprite();
	const AptSpriteInst *asSprite() const;
	AptButtonInst *asButton();
	Apt &apt() { return m_apt; }

	AptCharacterInst *parent() const { return m_parent; }
	int depth() const { return m_depth; }
	const std::string &instName() const { return m_instName; }
	bool defined() const { return m_defined; }
	// True from the start of the teardown (before the Unload callbacks run) on: such an instance is never torn down twice.
	bool destroying() const { return m_destroying; }
	const AptCharRef &charRef() const { return m_char; }
	// The nearest ancestor (or self) that is a loaded movie root: what `_root` means for this instance.
	AptSpriteInst *movieRoot();

	// placement state
	AptMatrix matrix;
	AptColorTransform color;
	float ratio = 0;
	float placeRatio = 0; // lane FB7-1: the ratio of the place object that created the instance (the seek's same-placement test, BFME2 0x00AF9589)
	std::int32_t clipDepth = -1; // -1 = not a clip layer (the file stores -1 when the flag is absent)
	bool visible = true;
	bool enabled = true;
	bool useHandCursor = true;
	bool focusRect = true;

	// Local-to-stage transform and colour (parents applied).
	AptMatrix globalMatrix() const;
	AptColorTransform globalColor() const;
	bool globallyVisible() const;

	// Bounds of the visible content in this instance's parent space (a union over children); false when empty.
	virtual bool contentBounds(float &x0, float &y0, float &x1, float &y1) const;

	// AptObject
	bool getOwn(const std::string &name, AptValue &out) const override;
	void setOwn(const std::string &name, const AptValue &value) override;
	void trace(AptGC &gc) override;
	std::string displayString() const override;

	// Script property by name (_x ...): returns true and fills `out` when `name` is a clip property.
	bool getProperty(const std::string &lowerName, AptValue &out) const;
	void noteTransformUnverified(const std::string &property) const; // lane UI-1: the transform properties' gaps (S-1262)
	bool setProperty(const std::string &lowerName, const AptValue &value);

	virtual void destroy(bool fireUnload);

	std::string targetPath() const; // "_level1.SoloPlayNav" form

protected:
	friend class AptSpriteInst;
	friend class AptButtonInst;
	friend class Apt;
	mutable std::uint8_t m_transformNoted = 0; // lane UI-1: the S-1262 notes already given by this clip (bit per property)
	Apt &m_apt;
	Type m_type;
	AptCharacterInst *m_parent = nullptr; // the sprite or button whose display list holds this instance
	int m_depth = 0;
	std::string m_instName;
	bool m_defined = true;
	bool m_destroying = false;
	// false when the instance is already being or was torn down; otherwise marks it and returns true
	bool beginDestroy()
	{
		if (m_destroying || !m_defined)
		{
			return false;
		}
		m_destroying = true;
		return true;
	}
	AptCharRef m_char;
	AptCharRef m_placedChar; ///< lane MP-2: the character the timeline placed, kept when a loadMovie replaced the instance's content (becomeMovie)
};

// ---------------------------------------------------------------------------------------------------------------
// AptSpriteInst: a sprite or a loaded movie (the "sprite instance base").  Owns the display list and the timeline.
// ---------------------------------------------------------------------------------------------------------------
class AptSpriteInst : public AptCharacterInst
{
public:
	AptSpriteInst(Apt &apt, Type type);

	// timeline (EA fields: frame +0x18, flags +0x1C)
	int frame = -1;
	bool playing = true;     // flag bit 25
	bool needsLoad = true;   // flag bit 24 (events not yet fired)
	int guardFrame = 0;      // +0x28: frame guard written into queued frame actions (negated while queueing)
	std::uint32_t eventFlags = 0; // flags low 24 bits: the clip-event bits the instance handles

	const std::vector<AptFrame> *frames = nullptr; // the timeline
	std::shared_ptr<const AptFile> timelineFile;   // the movie whose bytes hold the frame items' programs
	// Clip actions copied from the place object that created this instance.
	std::vector<AptClipEvent> clipEvents;
	std::shared_ptr<const AptFile> clipEventFile;
	bool hasClipEventList = false;

	// the display list: children sorted by depth, ascending
	const std::vector<AptCharacterInst *> &children() const { return m_children; }
	AptCharacterInst *childAtDepth(int depth) const;
	AptCharacterInst *childByName(const std::string &name) const;
	int totalFrames() const { return frames ? (int)frames->size() : 0; }

	// Timeline entry points (also AptObject::timelineOp).
	void gotoFrame(int target);                  // AptCIH::gotoFrame 0x00AE2C10
	void advance();                              // AptCIH advance 0x00AE2D60
	void advanceChildren();                      // display list sweep 0x00AF7A30
	int labelFrame(const std::string &label) const; // 0x00B0F010: exact (case-sensitive) compare, -1 when absent
	bool timelineOp(const AptTimelineRequest &request) override;

	// place / remove (frame controls) and the script creators
	AptCharacterInst *placeObject(const AptPlaceObject &place, const AptFile &placeFile, const std::shared_ptr<const AptFile> &placeFileShared);
	void removeObject(int depth);
	AptCharacterInst *attachCharacter(const AptCharRef &ref, const std::string &name, int depth, bool fromScript);
	AptSpriteInst *createEmptyClip(const std::string &name, int depth);
	// Removes the occupant of `depth` (if any) before a script-created instance goes there; false (reported) when an Unload callback
	// refilled the depth or destroyed this clip.
	bool vacateDepth(int depth, const char *native);
	void removeAllChildren(bool fireUnload);
	void reorderChild(AptCharacterInst *child, int newDepth); // swapDepths

	bool contentBounds(float &x0, float &y0, float &x1, float &y1) const override;
	void trace(AptGC &gc) override;
	void destroy(bool fireUnload) override;
	bool forInDecoded() const override { return true; }

	// events (AptCIH::fire 0x00AE2010, hasHandler 0x00AE1F90)
	bool hasHandler(std::uint32_t mask) const;
	bool fire(std::uint32_t mask, std::uint32_t arg, bool runMemberHandler);

	// A loaded movie replaces the content of the instance (0x00AD17F0 -> setType 0x12, set data, advance).
	void becomeMovie(const std::shared_ptr<const AptFile> &movie);
	// lane CAH-1: unloadMovie / getURL("", clip): the content goes (Unload fired), the instance stays as an empty, stopped clip with its name and placement
	void unloadContent();
	bool isMovie() const { return m_type == Type::Movie; }

private:
	friend class AptCharacterInst;
	friend class Apt;
	void doFrameControls(int frameNo);           // AptMovie 0x00B0F370
	void queueFrameActions(int frameNo);         // AptMovie 0x00B0F680
	AptCharacterInst *placeCharacter(const AptPlaceObject &place, const AptFile &placeFile, const std::shared_ptr<const AptFile> &placeFileShared);
	void applyPlaceFields(AptCharacterInst &inst, const AptPlaceObject &place, bool isNew);
	void registerName(AptCharacterInst &inst, const std::string &name);
	void insertChild(AptCharacterInst *inst);
	void unlinkChild(AptCharacterInst *inst);
	void firePlacementEvents(AptSpriteInst &inst);

	std::vector<AptCharacterInst *> m_children;
	// Seek reconstruction (0x00B0F040 builds commands, 0x00AF9410 applies them): the placements in effect at a frame.
	struct SeekState;
};

// ---------------------------------------------------------------------------------------------------------------
// Leaf instances.
// ---------------------------------------------------------------------------------------------------------------
class AptShapeInst : public AptCharacterInst
{
public:
	explicit AptShapeInst(Apt &apt) : AptCharacterInst(apt, Type::Shape) {}
	bool contentBounds(float &x0, float &y0, float &x1, float &y1) const override;
};

class AptTextInst : public AptCharacterInst
{
public:
	explicit AptTextInst(Apt &apt, Type type = Type::EditText) : AptCharacterInst(apt, type) {}
	std::string text;       // current text (initial text or script/engine assigned)
	std::string variable;   // bound variable name (resolved on the parent timeline)
	bool textAssigned = false;
	// The current text colour, packed 0xAARRGGBB: the character's colour at creation, then whatever script assigned to `textColor`.
	// Target facts (clean BFME2 1.06 game.dat): AptDisplayList::place copies the character dword into the instance at +0x24
	// (0x00AF87AC); the setter stores `toInteger(value) | 0xFF000000` there (0x00AEEB20..0x00AEEB3B); the getter returns the low
	// 24 bits as a number (0x00AEFE89).  The shared character definition is never touched.
	std::uint32_t colorArgb = 0xFF000000u;
	static std::uint32_t packColor(const std::uint8_t rgba[4]) { return ((std::uint32_t)rgba[3] << 24) | ((std::uint32_t)rgba[0] << 16) | ((std::uint32_t)rgba[1] << 8) | rgba[2]; }
	bool getOwn(const std::string &name, AptValue &out) const override;
	void setOwn(const std::string &name, const AptValue &value) override;
	bool contentBounds(float &x0, float &y0, float &x1, float &y1) const override;
};

// Placeholder for a character the parser has no layout for; drawn as nothing and reported (stop S-100).
class AptOpaqueInst : public AptCharacterInst
{
public:
	AptOpaqueInst(Apt &apt, Type type) : AptCharacterInst(apt, type) {}
};

// Buttons are in AptButtonInst.h (step A3).
