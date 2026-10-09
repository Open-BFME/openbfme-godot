// OpenBFME. GPL-3.0.
//
// EA Apt movie data model and file parsers: .const, .apt, .dat (image map) and .ru (geometry).
//
// File layouts (all little-endian, pointers are absolute file offsets):
//   .const  - 17-byte magic "Apt constant file", 1A 00 00, u32 aptDataEntryOffset, u32 count,
//             u32 headerSize (32), then (u32 type, u32 value) records.
//             Cross-checks: OpenSAGE OpenSage.Game/Data/Apt/ConstantData.cs,
//             archived importer sage_apt.py parse_apt_constants.
//   .apt    - "Apt Data:" + version digit + 1A 00; root Character (type 9, signature 0x09876543)
//             at aptDataEntryOffset.  Movie body, characters, frame items, place objects, clip
//             actions and buttons follow OpenSAGE Data/Apt/{Characters,FrameItems}/*.cs and the
//             archived importer retail_hud_apt_convert.py (_parse_* functions) which were checked
//             against the retail corpus.  The EA library maps these structures in place after
//             pointer fixup (BFME1 decomp game/Libraries/Source/Apt/AptLoad.cpp), so the file
//             layout IS the runtime layout.
//   .dat    - ASCII "N->tex" / "N=x y w h" image map lines, ';' comments (AptToBigc output).
//   .ru     - CRLF ASCII geometry: c / s s:r:g:b:a / s l:w:r:g:b:a / s tc:r:g:b:a:img:m11:m12:m21:m22:tx:ty
//             / t x0:y0:x1:y1:x2:y2 / l x0:y0:x1:y1.
//
// Every malformed input is an error with a message; nothing is defaulted.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct AptCodeBlock;

// ---------------------------------------------------------------------------------------------
// .const
// ---------------------------------------------------------------------------------------------

enum AptConstType : std::uint32_t
{
	APT_CONST_UNDEFINED = 0,
	APT_CONST_STRING = 1,
	APT_CONST_PROPERTY = 2,
	APT_CONST_NONE = 3,
	APT_CONST_REGISTER = 4,
	APT_CONST_BOOLEAN = 5,
	APT_CONST_FLOAT = 6,
	APT_CONST_INTEGER = 7,
	APT_CONST_LOOKUP = 8
};

struct AptConstEntry
{
	std::uint32_t type = 0;
	std::uint32_t raw = 0;  // string offset, register number, bool, float bits, int bits, lookup id
	std::string text;       // valid when type == APT_CONST_STRING (raw bytes, no transcoding)
};

class AptConstFile
{
public:
	std::uint32_t aptDataEntryOffset = 0;
	std::vector<AptConstEntry> entries;

	static bool parse(const std::vector<std::uint8_t> &bytes, AptConstFile &out, std::string *error);
};

// ---------------------------------------------------------------------------------------------
// .apt
// ---------------------------------------------------------------------------------------------

enum AptCharacterType : std::uint32_t
{
	APT_CHAR_NULL = 0, // empty slot: an import fills it
	APT_CHAR_SHAPE = 1,
	APT_CHAR_EDITTEXT = 2,
	APT_CHAR_FONT = 3,
	APT_CHAR_BUTTON = 4,
	APT_CHAR_SPRITE = 5,
	APT_CHAR_SOUND = 6,
	APT_CHAR_IMAGE = 7,
	APT_CHAR_MORPH = 8,
	APT_CHAR_MOVIE = 9,
	APT_CHAR_STATICTEXT = 10,
	APT_CHAR_NONE = 11,
	APT_CHAR_VIDEO = 12
};

enum AptFrameItemType : std::uint32_t
{
	APT_ITEM_ACTION = 1,
	APT_ITEM_FRAMELABEL = 2,
	APT_ITEM_PLACEOBJECT = 3,
	APT_ITEM_REMOVEOBJECT = 4,
	APT_ITEM_BACKGROUNDCOLOR = 5,
	APT_ITEM_INITACTION = 8
};

enum AptPlaceFlags : std::uint32_t
{
	APT_PLACE_MOVE = 0x01,
	APT_PLACE_HASCHARACTER = 0x02,
	APT_PLACE_HASMATRIX = 0x04,
	APT_PLACE_HASCOLORTRANSFORM = 0x08,
	APT_PLACE_HASRATIO = 0x10,
	APT_PLACE_HASNAME = 0x20,
	APT_PLACE_HASCLIPDEPTH = 0x40,
	APT_PLACE_HASCLIPACTION = 0x80
};

// Clip event bit flags of a PlaceObject clip action (24-bit mask).  Target facts (clean BFME2 1.06 game.dat): the mask is
// compared bit for bit with the runtime event masks (AptCIH::fire 0x00AE2010 `test [action], mask`, event table 0x00DDC2E8,
// fire switch 0x00AE2111), which are the SWF ClipEventFlags in little-endian bit order.  The OpenSAGE enumeration quoted by
// spec menus-apt.md 2.3 (Initialize=1 ... KeyUp=0x800000) is wrong for the retail files: the retail masks are 0x1 (640
// place objects), 0x200 (241), 0x40000 (121), 0x2 (3), 0x4 (4), 0x400 (3), 0x800, 0x8000, 0x10000 (1 each), i.e. Load,
// Initialize, Construct, EnterFrame, Unload, Press, Release, DragOver, DragOut.
enum AptClipEventFlags : std::uint32_t
{
	APT_CLIP_LOAD = 0x000001,
	APT_CLIP_ENTERFRAME = 0x000002,
	APT_CLIP_UNLOAD = 0x000004,
	APT_CLIP_MOUSEMOVE = 0x000008,
	APT_CLIP_MOUSEDOWN = 0x000010,
	APT_CLIP_MOUSEUP = 0x000020,
	APT_CLIP_KEYDOWN = 0x000040,
	APT_CLIP_KEYUP = 0x000080,
	APT_CLIP_DATA = 0x000100,
	APT_CLIP_INITIALIZE = 0x000200,
	APT_CLIP_PRESS = 0x000400,
	APT_CLIP_RELEASE = 0x000800,
	APT_CLIP_RELEASEOUTSIDE = 0x001000,
	APT_CLIP_ROLLOVER = 0x002000,
	APT_CLIP_ROLLOUT = 0x004000,
	APT_CLIP_DRAGOVER = 0x008000,
	APT_CLIP_DRAGOUT = 0x010000,
	APT_CLIP_KEYPRESS = 0x020000,
	APT_CLIP_CONSTRUCT = 0x040000,
	APT_CLIP_MOUSEWHEEL = 0x080000,
	APT_CLIP_ALL_MASK = 0xFFFFFF
};

struct AptClipEvent
{
	std::uint32_t mask = 0;
	std::uint8_t keyCode = 0;
	std::uint32_t nextEventOffset = 0; // raw u32 at +4 of the 12-byte record
	std::uint32_t codeOffset = 0;      // action program
};

struct AptPlaceObject
{
	std::uint32_t flags = 0;
	std::int32_t depth = 0;
	std::int32_t characterId = 0; // meaningful when APT_PLACE_HASCHARACTER
	float matrix[4] = { 1, 0, 0, 1 };
	float translation[2] = { 0, 0 };
	std::uint8_t tint[4] = { 255, 255, 255, 255 };
	std::uint8_t additive[4] = { 0, 0, 0, 0 };
	float ratio = 0;
	std::string name; // meaningful when APT_PLACE_HASNAME
	std::int32_t clipDepth = 0;
	bool clipActionsFlagged = false; // flag 0x80 set
	bool clipActionsNull = false;    // flagged but the pointer is null: "no events" (spec 2.3)
	std::vector<AptClipEvent> clipEvents;
};

struct AptFrameItem
{
	std::uint32_t type = 0;
	std::uint32_t offset = 0;         // file offset of the item record
	std::uint32_t codeOffset = 0;     // Action, InitAction
	std::uint32_t spriteId = 0;       // InitAction
	std::string label;                // FrameLabel
	std::uint32_t labelFlags = 0;     // FrameLabel
	std::uint32_t labelFrameId = 0;   // FrameLabel
	std::int32_t removeDepth = 0;     // RemoveObject
	std::uint8_t color[4] = { 0, 0, 0, 0 }; // BackgroundColor
	std::shared_ptr<AptPlaceObject> place; // PlaceObject
};

struct AptFrame
{
	std::vector<AptFrameItem> items;
};

struct AptButtonRecord
{
	std::uint32_t stateMask = 0; // Up=1 Over=2 Down=4 Hit=8
	std::uint32_t characterId = 0;
	std::int32_t depth = 0;
	float matrix[4] = { 1, 0, 0, 1 };
	float translation[2] = { 0, 0 };
	float color[4] = { 1, 1, 1, 1 };
	float unknown[4] = { 0, 0, 0, 0 };
};

struct AptButtonAction
{
	std::uint8_t transitionMask = 0; // IdleToOverUp=1 .. OverDownToIdle=128
	std::uint16_t keyCode = 0;
	std::uint32_t codeOffset = 0;
};

struct AptButtonInfo
{
	bool isMenu = false;
	float bounds[4] = { 0, 0, 0, 0 };
	std::vector<float> vertices;                // x,y pairs
	std::vector<std::uint16_t> triangles;       // index triples
	std::vector<AptButtonRecord> records;
	std::vector<AptButtonAction> actions;
};

struct AptTextInfo
{
	float bounds[4] = { 0, 0, 0, 0 };
	std::uint32_t fontId = 0;
	std::uint32_t alignment = 0;
	std::uint8_t color[4] = { 0, 0, 0, 0 }; // r, g, b, a (the file stores the dword 0xAARRGGBB: bytes B, G, R, A; see parseText)
	float fontHeight = 0;
	bool readOnly = false;
	bool multiline = false;
	bool wordWrap = false;
	std::string initialText;
	std::string variableName;
};

struct AptCharacter
{
	std::uint32_t id = 0;
	std::uint32_t type = APT_CHAR_NULL;
	std::uint32_t offset = 0; // 0 for null slots

	// APT_CHAR_SHAPE
	float bounds[4] = { 0, 0, 0, 0 };
	std::uint32_t geometryId = 0;
	// APT_CHAR_IMAGE
	std::uint32_t textureId = 0;
	// APT_CHAR_SPRITE
	std::vector<AptFrame> frames;
	// APT_CHAR_FONT
	std::string fontName;
	std::vector<std::uint32_t> glyphs;
	// APT_CHAR_EDITTEXT
	std::shared_ptr<AptTextInfo> text;
	// APT_CHAR_BUTTON
	std::shared_ptr<AptButtonInfo> button;
	// Morph / StaticText / Sound / Video / None / Movie: layout not yet decoded.  The type and
	// offset are recorded and `opaque` makes that explicit; consumers must not instantiate them
	// without first adding a parser.
	bool opaque = false;
};

struct AptImport
{
	std::string movie;
	std::string name;
	std::uint32_t characterId = 0;
	std::uint32_t pointer = 0;
};

struct AptExport
{
	std::string name;
	std::uint32_t characterId = 0;
};

class AptFile
{
public:
	std::string name;              // movie name as requested (e.g. "MainMenu")
	std::uint32_t version = 0;     // the digit in "Apt Data:N" (6 or 7)
	std::uint32_t unknownField = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint32_t msPerFrame = 0;
	std::vector<AptFrame> frames;  // root timeline
	std::vector<AptCharacter> characters; // index == character id
	std::vector<AptImport> imports;
	std::vector<AptExport> exports;
	AptConstFile consts;
	std::shared_ptr<const std::vector<std::uint8_t>> data; // raw .apt bytes (bytecode lives here)

	static bool parse(const std::string &movieName, std::shared_ptr<const std::vector<std::uint8_t>> aptBytes,
		const AptConstFile &consts, AptFile &out, std::string *error);

	// Export lookup is ASCII case-insensitive (retail uses _strcmpi tables).  Returns the character
	// id of the first export with that name; *ambiguous is set when another export of the same
	// name has a different character id.  False when the name is not exported.
	bool findExport(const std::string &exportName, std::uint32_t &characterId, bool *ambiguous) const;

	// Decode (and cache) the action program at a file offset.  Null + *error on failure.
	std::shared_ptr<const AptCodeBlock> codeAt(std::uint32_t offset, std::string *error) const;

	// Every distinct program entry offset in this movie, in discovery order: root and sprite frame
	// actions, init actions, place-object clip events, button actions (the set the corpus census
	// counts).  Nested function bodies are not separate programs.
	std::vector<std::uint32_t> programOffsets() const;

private:
	mutable std::map<std::uint32_t, std::shared_ptr<const AptCodeBlock>> m_codeCache;
};

// ---------------------------------------------------------------------------------------------
// .dat image map and .ru geometry
// ---------------------------------------------------------------------------------------------

struct AptImageMapEntry
{
	std::uint32_t imageId = 0;
	bool isRect = false;        // "N=x y w h": image is its own texture apt_X_<N>.tga with this rect
	std::uint32_t textureId = 0; // "N->T": atlas texture apt_X_<T>.tga
	std::int32_t rect[4] = { 0, 0, 0, 0 }; // x y w h when isRect
};

class AptImageMap
{
public:
	std::vector<AptImageMapEntry> entries;
	static bool parse(const std::vector<std::uint8_t> &bytes, AptImageMap &out, std::string *error);
};

enum AptStyleKind
{
	APT_STYLE_SOLID,     // s s:r:g:b:a
	APT_STYLE_LINE,      // s l:w:r:g:b:a
	APT_STYLE_TEXTURED   // s tc:r:g:b:a:img:m11:m12:m21:m22:tx:ty
};

struct AptGeometryStyle
{
	AptStyleKind kind = APT_STYLE_SOLID;
	float rgba[4] = { 0, 0, 0, 0 };
	float lineWidth = 0;
	std::int32_t imageId = 0;
	float uv[6] = { 1, 0, 0, 1, 0, 0 }; // m11 m12 m21 m22 tx ty
	std::vector<float> triangles;       // 6 floats per triangle (solid/textured)
	std::vector<float> lines;           // 4 floats per line (line style)
};

class AptGeometry
{
public:
	std::vector<AptGeometryStyle> styles;
	std::uint32_t clearCount = 0; // number of 'c' records
	static bool parse(const std::vector<std::uint8_t> &bytes, AptGeometry &out, std::string *error);
};
