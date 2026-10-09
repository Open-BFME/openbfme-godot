// OpenBFME. GPL-3.0.
// See AptFile.h for the format notes and citations.

#include "Libraries/Source/Apt/AptFile.h"

#include "Libraries/Source/Apt/AptActionDecoder.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <set>

namespace
{

const std::uint32_t kCharacterSignature = 0x09876543;
const std::uint32_t kMaxString = 4096;
const std::uint32_t kMaxConstants = 1u << 20;
const std::uint32_t kMaxCharacters = 1u << 16;
const std::uint32_t kMaxFrames = 1u << 16;
const std::uint32_t kMaxFrameItems = 1u << 16;
const std::uint32_t kMaxImports = 1u << 12;
const std::uint32_t kMaxExports = 1u << 16;
const std::uint32_t kMaxClipEvents = 1u << 16;
const std::uint32_t kMaxButtonItems = 1u << 16;
const std::uint32_t kMaxGlyphs = 1u << 16;
// Aggregate parse budgets: frame tables may alias each other (many frame records pointing at the same item
// table), so the per-table caps above do not bound what a file makes the parser allocate.  These cap the
// total number of frame records and frame items instantiated per file.  They are implementation resource
// bounds, not EA values; the retail corpus peaks 25x (frames) to 100x (frame items) below them
// (test_apt_corpus.cpp measures and asserts a 4x margin).
const std::uint64_t kMaxTotalFrames = 1u << 18;
const std::uint64_t kMaxTotalFrameItems = 1u << 20;
const std::uint64_t kMaxTotalClipEvents = 1u << 20;
const std::uint64_t kMaxTotalButtonElements = 1u << 20;
const std::uint64_t kMaxTotalGlyphs = 1u << 20;
// Aliased records can also repeat a string (a place-object name, a text field's content) once per record.
const std::uint64_t kMaxTotalStringBytes = 1u << 26;

// Bounds-checked little-endian reader.  The first failure is recorded; later reads return zero so
// callers can check ok() at natural checkpoints instead of after every field.
class AptReader
{
public:
	AptReader(const std::uint8_t *data, std::size_t size, std::string label) : m_data(data), m_size(size), m_label(std::move(label)) {}

	bool ok() const { return m_error.empty(); }
	const std::string &error() const { return m_error; }
	std::size_t size() const { return m_size; }

	void fail(const std::string &message)
	{
		if (m_error.empty())
		{
			m_error = m_label + ": " + message;
		}
	}

	bool require(std::uint64_t offset, std::uint64_t size, const char *what)
	{
		if (!ok())
		{
			return false;
		}
		if (offset > m_size || size > m_size - offset)
		{
			fail(std::string(what) + " range " + std::to_string(offset) + "+" + std::to_string(size) + " is out of bounds (file is " +
				std::to_string(m_size) + " bytes)");
			return false;
		}
		return true;
	}

	std::uint8_t u8(std::uint64_t offset, const char *what)
	{
		return require(offset, 1, what) ? m_data[offset] : 0;
	}

	std::uint16_t u16(std::uint64_t offset, const char *what)
	{
		if (!require(offset, 2, what))
		{
			return 0;
		}
		return (std::uint16_t)(m_data[offset] | (m_data[offset + 1] << 8));
	}

	std::uint32_t u32(std::uint64_t offset, const char *what)
	{
		if (!require(offset, 4, what))
		{
			return 0;
		}
		return (std::uint32_t)m_data[offset] | ((std::uint32_t)m_data[offset + 1] << 8) | ((std::uint32_t)m_data[offset + 2] << 16) |
			((std::uint32_t)m_data[offset + 3] << 24);
	}

	std::int32_t i32(std::uint64_t offset, const char *what) { return (std::int32_t)u32(offset, what); }

	// Charges `count` against an aggregate budget; fails the parse when the file exceeds it.
	bool charge(std::uint64_t &total, std::uint64_t count, std::uint64_t cap, const char *what)
	{
		if (!ok())
		{
			return false;
		}
		total += count;
		if (total > cap)
		{
			fail(std::string("file instantiates more than ") + std::to_string(cap) + " " + what + " in total (aliased tables?)");
			return false;
		}
		return true;
	}

	std::uint64_t frameTotal = 0;
	std::uint64_t frameItemTotal = 0;
	std::uint64_t clipEventTotal = 0;
	std::uint64_t buttonElementTotal = 0;
	std::uint64_t glyphTotal = 0;
	std::uint64_t stringByteTotal = 0;

	float f32(std::uint64_t offset, const char *what)
	{
		std::uint32_t bits = u32(offset, what);
		float value;
		std::memcpy(&value, &bits, 4);
		if (ok() && !std::isfinite(value))
		{
			fail(std::string(what) + " at " + std::to_string(offset) + " is not finite");
			return 0;
		}
		return value;
	}

	// NUL-terminated string (raw bytes, no transcoding) that must end within kMaxString bytes.
	std::string cstring(std::uint64_t offset, const char *what)
	{
		if (!ok())
		{
			return std::string();
		}
		if (offset >= m_size)
		{
			fail(std::string(what) + " string offset " + std::to_string(offset) + " is out of bounds");
			return std::string();
		}
		std::size_t limit = (std::size_t)std::min<std::uint64_t>(m_size, offset + kMaxString + 1);
		for (std::size_t i = (std::size_t)offset; i < limit; ++i)
		{
			if (m_data[i] == 0)
			{
				if (!charge(stringByteTotal, i - (std::size_t)offset, kMaxTotalStringBytes, "string bytes"))
				{
					return std::string();
				}
				return std::string((const char *)m_data + offset, i - (std::size_t)offset);
			}
		}
		fail(std::string(what) + " string at " + std::to_string(offset) + " lacks a NUL terminator within " + std::to_string(kMaxString) + " bytes");
		return std::string();
	}

	// (count, pointer) list header at `offset`; validates count*stride bytes at the pointer.
	bool listHeader(std::uint64_t offset, std::uint32_t maxCount, std::uint32_t stride, const char *what, std::uint32_t &count, std::uint32_t &pointer)
	{
		std::int32_t signedCount = i32(offset, what);
		pointer = u32(offset + 4, what);
		if (!ok())
		{
			return false;
		}
		if (signedCount < 0 || (std::uint32_t)signedCount > maxCount)
		{
			fail(std::string(what) + " count " + std::to_string(signedCount) + " exceeds bounds");
			return false;
		}
		count = (std::uint32_t)signedCount;
		if (count != 0)
		{
			return require(pointer, (std::uint64_t)count * stride, what);
		}
		if (pointer > m_size)
		{
			fail(std::string("empty ") + what + " pointer is out of bounds");
			return false;
		}
		return true;
	}

private:
	const std::uint8_t *m_data;
	std::size_t m_size;
	std::string m_label;
	std::string m_error;
};

bool boolField(AptReader &r, std::uint64_t offset, const char *what)
{
	std::uint32_t v = r.u32(offset, what);
	if (r.ok() && v > 1)
	{
		r.fail(std::string(what) + " at " + std::to_string(offset) + " is not a boolean (" + std::to_string(v) + ")");
	}
	return v == 1;
}

bool setError(std::string *error, const std::string &message)
{
	if (error)
	{
		*error = message;
	}
	return false;
}

// ---- frame items -------------------------------------------------------------------------

bool parseClipActions(AptReader &r, std::uint32_t offset, std::vector<AptClipEvent> &events)
{
	std::uint32_t count = 0;
	std::uint32_t table = 0;
	if (!r.require(offset, 8, "clip-action list"))
	{
		return false;
	}
	std::int32_t signedCount = r.i32(offset, "clip-action event count");
	table = r.u32(offset + 4, "clip-action event table");
	if (!r.ok())
	{
		return false;
	}
	if (signedCount <= 0 || (std::uint32_t)signedCount > kMaxClipEvents)
	{
		r.fail("clip-action event count " + std::to_string(signedCount) + " is empty or exceeds bounds");
		return false;
	}
	count = (std::uint32_t)signedCount;
	if (!r.require(table, (std::uint64_t)count * 12, "clip-action event table") ||
		!r.charge(r.clipEventTotal, count, kMaxTotalClipEvents, "clip events"))
	{
		return false;
	}
	for (std::uint32_t i = 0; i < count; ++i)
	{
		std::uint32_t at = table + i * 12;
		AptClipEvent e;
		e.mask = (std::uint32_t)r.u8(at, "clip event mask") | ((std::uint32_t)r.u8(at + 1, "clip event mask") << 8) |
			((std::uint32_t)r.u8(at + 2, "clip event mask") << 16);
		e.keyCode = r.u8(at + 3, "clip event key code");
		e.nextEventOffset = r.u32(at + 4, "clip event next pointer");
		e.codeOffset = r.u32(at + 8, "clip event code pointer");
		if (!r.ok())
		{
			return false;
		}
		if (e.mask == 0 || (e.mask & ~(std::uint32_t)APT_CLIP_ALL_MASK) != 0)
		{
			r.fail("clip event mask 0x" + std::to_string(e.mask) + " at " + std::to_string(at) + " is invalid");
			return false;
		}
		if (e.keyCode != 0 && (e.mask & APT_CLIP_KEYPRESS) == 0)
		{
			r.fail("clip event key code at " + std::to_string(at) + " lacks the key-press event");
			return false;
		}
		events.push_back(e);
	}
	return true;
}

bool parsePlaceObject(AptReader &r, std::uint32_t offset, AptPlaceObject &p)
{
	if (!r.require(offset, 60, "place-object"))
	{
		return false;
	}
	p.flags = r.u32(offset + 4, "place-object flags");
	if (r.ok() && (p.flags & ~0xFFu) != 0)
	{
		r.fail("place-object at " + std::to_string(offset) + " has reserved flag bits set");
		return false;
	}
	p.depth = r.i32(offset + 8, "place-object depth");
	p.characterId = r.i32(offset + 12, "place-object character");
	for (int i = 0; i < 4; ++i)
	{
		p.matrix[i] = r.f32(offset + 16 + i * 4, "place-object matrix");
	}
	for (int i = 0; i < 2; ++i)
	{
		p.translation[i] = r.f32(offset + 32 + i * 4, "place-object translation");
	}
	for (int i = 0; i < 4; ++i)
	{
		p.tint[i] = r.u8(offset + 40 + i, "place-object tint");
		p.additive[i] = r.u8(offset + 44 + i, "place-object additive");
	}
	p.ratio = r.f32(offset + 48, "place-object ratio");
	p.clipDepth = r.i32(offset + 56, "place-object clip depth");
	if (!r.ok())
	{
		return false;
	}
	if (p.flags & APT_PLACE_HASNAME)
	{
		p.name = r.cstring(r.u32(offset + 52, "place-object name pointer"), "place-object name");
	}
	if (p.flags & APT_PLACE_HASCLIPACTION)
	{
		p.clipActionsFlagged = true;
		std::uint32_t clipOffset = r.u32(offset + 60, "place-object clip actions pointer");
		if (!r.ok())
		{
			return false;
		}
		if (clipOffset == 0)
		{
			// Retail authors flagged-null clip action pointers (29 in the RotWK corpus); spec 2.3: no events.
			p.clipActionsNull = true;
		}
		else
		{
			parseClipActions(r, clipOffset, p.clipEvents);
		}
	}
	return r.ok();
}

bool parseFrames(AptReader &r, std::uint32_t table, std::uint32_t count, std::vector<AptFrame> &frames)
{
	if (!r.charge(r.frameTotal, count, kMaxTotalFrames, "frames"))
	{
		return false;
	}
	frames.resize(count);
	for (std::uint32_t f = 0; f < count; ++f)
	{
		std::uint32_t itemCount = 0;
		std::uint32_t itemTable = 0;
		if (!r.listHeader(table + f * 8, kMaxFrameItems, 4, "frame item list", itemCount, itemTable))
		{
			return false;
		}
		AptFrame &frame = frames[f];
		if (!r.charge(r.frameItemTotal, itemCount, kMaxTotalFrameItems, "frame items"))
		{
			return false;
		}
		frame.items.resize(itemCount);
		for (std::uint32_t i = 0; i < itemCount; ++i)
		{
			std::uint32_t at = r.u32(itemTable + i * 4, "frame item pointer");
			AptFrameItem &item = frame.items[i];
			item.offset = at;
			item.type = r.u32(at, "frame item type");
			if (!r.ok())
			{
				return false;
			}
			switch (item.type)
			{
				case APT_ITEM_ACTION:
					item.codeOffset = r.u32(at + 4, "action code pointer");
					break;
				case APT_ITEM_FRAMELABEL:
					item.label = r.cstring(r.u32(at + 4, "frame label name pointer"), "frame label name");
					item.labelFlags = r.u32(at + 8, "frame label flags");
					item.labelFrameId = r.u32(at + 12, "frame label id");
					break;
				case APT_ITEM_PLACEOBJECT:
					item.place = std::make_shared<AptPlaceObject>();
					parsePlaceObject(r, at, *item.place);
					break;
				case APT_ITEM_REMOVEOBJECT:
					item.removeDepth = r.i32(at + 4, "remove depth");
					break;
				case APT_ITEM_BACKGROUNDCOLOR:
					for (int c = 0; c < 4; ++c)
					{
						item.color[c] = r.u8(at + 4 + c, "background colour");
					}
					break;
				case APT_ITEM_INITACTION:
					// (type, spriteId, codePtr).  The archived importer read +4 (the sprite id) as the
					// code pointer; OpenSAGE FrameItems/InitAction.cs and the corpus' only two records
					// (GuiTest root f0, OnlineHome root f14) show the code pointer at +8.
					item.spriteId = r.u32(at + 4, "init action sprite id");
					item.codeOffset = r.u32(at + 8, "init action code pointer");
					break;
				default:
					r.fail("frame item at " + std::to_string(at) + " has unknown type " + std::to_string(item.type));
					return false;
			}
			if (!r.ok())
			{
				return false;
			}
		}
	}
	return r.ok();
}

// ---- characters --------------------------------------------------------------------------

bool parseButton(AptReader &r, std::uint32_t at, AptButtonInfo &b)
{
	if (!r.require(at, 60, "button character"))
	{
		return false;
	}
	b.isMenu = boolField(r, at + 8, "button menu flag");
	for (int i = 0; i < 4; ++i)
	{
		b.bounds[i] = r.f32(at + 12 + i * 4, "button bounds");
	}
	std::uint32_t triangleCount = r.u32(at + 28, "button triangle count");
	std::uint32_t vertexCount = r.u32(at + 32, "button vertex count");
	std::uint32_t vertexTable = r.u32(at + 36, "button vertex table");
	std::uint32_t triangleTable = r.u32(at + 40, "button triangle table");
	std::uint32_t recordCount = r.u32(at + 44, "button record count");
	std::uint32_t recordTable = r.u32(at + 48, "button record table");
	std::uint32_t actionCount = r.u32(at + 52, "button action count");
	std::uint32_t actionTable = r.u32(at + 56, "button action table");
	if (!r.ok())
	{
		return false;
	}
	if (vertexCount > kMaxButtonItems || triangleCount > kMaxButtonItems || recordCount > kMaxButtonItems || actionCount > kMaxButtonItems)
	{
		r.fail("button at " + std::to_string(at) + " counts exceed bounds");
		return false;
	}
	if ((vertexCount && vertexTable == 0) || (triangleCount && triangleTable == 0) || (recordCount && recordTable == 0) || (actionCount && actionTable == 0))
	{
		r.fail("button at " + std::to_string(at) + " has a null table with a non-zero count");
		return false;
	}
	r.require(vertexTable, (std::uint64_t)vertexCount * 8, "button vertices");
	r.require(triangleTable, (std::uint64_t)triangleCount * 6, "button triangles");
	r.require(recordTable, (std::uint64_t)recordCount * 68, "button records");
	r.require(actionTable, (std::uint64_t)actionCount * 8, "button actions");
	if (!r.ok() ||
		!r.charge(r.buttonElementTotal, (std::uint64_t)vertexCount * 2 + (std::uint64_t)triangleCount * 3 + recordCount + actionCount,
			kMaxTotalButtonElements, "button elements"))
	{
		return false;
	}
	for (std::uint32_t i = 0; i < vertexCount; ++i)
	{
		b.vertices.push_back(r.f32(vertexTable + i * 8, "button vertex x"));
		b.vertices.push_back(r.f32(vertexTable + i * 8 + 4, "button vertex y"));
	}
	for (std::uint32_t i = 0; i < triangleCount * 3; ++i)
	{
		std::uint16_t idx = r.u16(triangleTable + i * 2, "button triangle index");
		if (r.ok() && idx >= vertexCount)
		{
			r.fail("button triangle references missing vertex " + std::to_string(idx));
			return false;
		}
		b.triangles.push_back(idx);
	}
	for (std::uint32_t i = 0; i < recordCount; ++i)
	{
		std::uint32_t p = recordTable + i * 68;
		AptButtonRecord rec;
		rec.stateMask = r.u8(p, "button record state mask");
		std::uint32_t reserved = (std::uint32_t)r.u8(p + 1, "button record reserved") | ((std::uint32_t)r.u8(p + 2, "button record reserved") << 8) |
			((std::uint32_t)r.u8(p + 3, "button record reserved") << 16);
		if (r.ok() && (rec.stateMask == 0 || (rec.stateMask & ~0x0Fu) != 0 || reserved != 0))
		{
			r.fail("button record flags or reserved bytes at " + std::to_string(p) + " are invalid");
			return false;
		}
		rec.characterId = r.u32(p + 4, "button record character");
		rec.depth = r.i32(p + 8, "button record depth");
		for (int k = 0; k < 4; ++k)
		{
			rec.matrix[k] = r.f32(p + 12 + k * 4, "button record matrix");
			rec.color[k] = r.f32(p + 36 + k * 4, "button record colour");
			rec.unknown[k] = r.f32(p + 52 + k * 4, "button record unknown");
		}
		for (int k = 0; k < 2; ++k)
		{
			rec.translation[k] = r.f32(p + 28 + k * 4, "button record translation");
		}
		b.records.push_back(rec);
	}
	for (std::uint32_t i = 0; i < actionCount; ++i)
	{
		std::uint32_t p = actionTable + i * 8;
		AptButtonAction a;
		a.transitionMask = r.u8(p, "button action transitions");
		a.keyCode = r.u16(p + 1, "button action key code");
		std::uint8_t reserved = r.u8(p + 3, "button action reserved");
		a.codeOffset = r.u32(p + 4, "button action code pointer");
		// A key-only action has transition mask 0 (MainMenu character 103); an action with neither a
		// transition nor a key code has no trigger at all.
		if (r.ok() && ((a.transitionMask == 0 && a.keyCode == 0) || reserved != 0))
		{
			r.fail("button action at " + std::to_string(p) + " has no trigger or a reserved byte set");
			return false;
		}
		b.actions.push_back(a);
	}
	return r.ok();
}

bool parseText(AptReader &r, std::uint32_t at, AptTextInfo &t)
{
	if (!r.require(at, 60, "text character"))
	{
		return false;
	}
	for (int i = 0; i < 4; ++i)
	{
		t.bounds[i] = r.f32(at + 8 + i * 4, "text bounds");
	}
	// The colour is one 32-bit 0xAARRGGBB value, so the file bytes are B, G, R, A.  Target facts (clean BFME2 1.06 game.dat): AptDisplayList::place
	// copies the character's dword at +0x20 verbatim into the text instance at +0x24 (0x00AF87AC / 0x00AF87AF), and the TextField `textColor`
	// getter returns `[instance + 0x24] & 0xFFFFFF` as a number (0x00AEFE89..0x00AEFE92; Flash's textColor is 0xRRGGBB).  OpenSAGE reads
	// the four bytes as R, G, B, A; that order shows the retail UI in swapped red and blue.  `color` holds r, g, b, a.
	t.color[0] = r.u8(at + 34, "text colour red");
	t.color[1] = r.u8(at + 33, "text colour green");
	t.color[2] = r.u8(at + 32, "text colour blue");
	t.color[3] = r.u8(at + 35, "text colour alpha");
	t.fontId = r.u32(at + 24, "text font character");
	t.alignment = r.u32(at + 28, "text alignment");
	t.fontHeight = r.f32(at + 36, "text font height");
	t.readOnly = boolField(r, at + 40, "text read-only");
	t.multiline = boolField(r, at + 44, "text multiline");
	t.wordWrap = boolField(r, at + 48, "text word-wrap");
	t.initialText = r.cstring(r.u32(at + 52, "text content pointer"), "text content");
	t.variableName = r.cstring(r.u32(at + 56, "text variable pointer"), "text variable");
	if (r.ok() && t.alignment > 2)
	{
		r.fail("text alignment code " + std::to_string(t.alignment) + " at " + std::to_string(at) + " is unsupported");
	}
	return r.ok();
}

} // namespace

// =============================================================================================
// .const
// =============================================================================================

bool AptConstFile::parse(const std::vector<std::uint8_t> &bytes, AptConstFile &out, std::string *error)
{
	out = AptConstFile();
	AptReader r(bytes.data(), bytes.size(), ".const");
	static const char kMagic[] = "Apt constant file";
	if (bytes.size() < 32 || std::memcmp(bytes.data(), kMagic, 17) != 0)
	{
		return setError(error, ".const: bad magic (expected \"Apt constant file\")");
	}
	if (bytes[17] != 0x1A || bytes[18] != 0 || bytes[19] != 0)
	{
		return setError(error, ".const: bad magic sentinel (expected 1A 00 00)");
	}
	out.aptDataEntryOffset = r.u32(20, "apt entry offset");
	std::uint32_t count = r.u32(24, "constant count");
	std::uint32_t headerSize = r.u32(28, "header size");
	if (!r.ok())
	{
		return setError(error, r.error());
	}
	if (headerSize != 32)
	{
		return setError(error, ".const: header size is " + std::to_string(headerSize) + ", expected 32");
	}
	if (count > kMaxConstants || !r.require(32, (std::uint64_t)count * 8, "constant records"))
	{
		return setError(error, r.ok() ? ".const: constant count " + std::to_string(count) + " exceeds bounds" : r.error());
	}
	out.entries.resize(count);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		AptConstEntry &e = out.entries[i];
		e.type = r.u32(32 + i * 8, "constant type");
		e.raw = r.u32(36 + i * 8, "constant value");
		if (!r.ok())
		{
			return setError(error, r.error());
		}
		switch (e.type)
		{
			case APT_CONST_STRING:
				e.text = r.cstring(e.raw, "constant string");
				break;
			case APT_CONST_NONE:
				if (e.raw != 0)
				{
					r.fail("None constant " + std::to_string(i) + " is non-zero");
				}
				break;
			case APT_CONST_BOOLEAN:
				if (e.raw > 1)
				{
					r.fail("Boolean constant " + std::to_string(i) + " is not 0/1");
				}
				break;
			case APT_CONST_FLOAT:
			{
				float v;
				std::memcpy(&v, &e.raw, 4);
				if (!std::isfinite(v))
				{
					r.fail("Float constant " + std::to_string(i) + " is not finite");
				}
				break;
			}
			case APT_CONST_UNDEFINED:
			case APT_CONST_PROPERTY:
			case APT_CONST_REGISTER:
			case APT_CONST_INTEGER:
			case APT_CONST_LOOKUP:
				break;
			default:
				r.fail("constant " + std::to_string(i) + " has unknown type " + std::to_string(e.type));
				break;
		}
		if (!r.ok())
		{
			return setError(error, r.error());
		}
	}
	return true;
}

// =============================================================================================
// .apt
// =============================================================================================

bool AptFile::parse(const std::string &movieName, std::shared_ptr<const std::vector<std::uint8_t>> aptBytes, const AptConstFile &consts, AptFile &out,
	std::string *error)
{
	out = AptFile();
	if (!aptBytes)
	{
		return setError(error, movieName + ".apt: no data");
	}
	AptReader r(aptBytes->data(), aptBytes->size(), movieName + ".apt");
	const std::vector<std::uint8_t> &d = *aptBytes;
	if (d.size() < 12 || std::memcmp(d.data(), "Apt Data", 8) != 0 || d[8] != ':' || d[10] != 0x1A || d[11] != 0)
	{
		return setError(error, movieName + ".apt: bad magic (expected \"Apt Data:N\" 1A 00)");
	}
	if (d[9] != '6' && d[9] != '7')
	{
		return setError(error, movieName + ".apt: unsupported Apt version '" + std::string(1, (char)d[9]) + "' (expected 6 or 7)");
	}
	out.name = movieName;
	out.version = (std::uint32_t)(d[9] - '0');
	out.consts = consts;
	out.data = aptBytes;

	std::uint32_t entry = consts.aptDataEntryOffset;
	if (!r.require(entry, 60, "root movie header"))
	{
		return setError(error, r.error());
	}
	if (r.u32(entry, "root type") != APT_CHAR_MOVIE || r.u32(entry + 4, "root signature") != kCharacterSignature)
	{
		return setError(error, movieName + ".apt: root character is not a movie with the Apt signature");
	}
	std::uint32_t movie = entry + 8;

	std::uint32_t frameCount = 0, frameTable = 0, charCount = 0, charTable = 0, importCount = 0, importTable = 0, exportCount = 0, exportTable = 0;
	r.listHeader(movie, kMaxFrames, 8, "root frames", frameCount, frameTable);
	out.unknownField = r.u32(movie + 8, "movie unknown field");
	r.listHeader(movie + 12, kMaxCharacters, 4, "characters", charCount, charTable);
	out.width = r.u32(movie + 20, "movie width");
	out.height = r.u32(movie + 24, "movie height");
	out.msPerFrame = r.u32(movie + 28, "movie ms per frame");
	r.listHeader(movie + 32, kMaxImports, 16, "imports", importCount, importTable);
	r.listHeader(movie + 40, kMaxExports, 8, "exports", exportCount, exportTable);
	if (!r.ok())
	{
		return setError(error, r.error());
	}
	if (out.width < 1 || out.width > 8192 || out.height < 1 || out.height > 8192 || out.msPerFrame < 1 || out.msPerFrame > 60000)
	{
		return setError(error, movieName + ".apt: movie dimensions or frame rate out of range");
	}

	for (std::uint32_t i = 0; i < importCount; ++i)
	{
		std::uint32_t at = importTable + i * 16;
		AptImport im;
		im.movie = r.cstring(r.u32(at, "import movie pointer"), "import movie");
		im.name = r.cstring(r.u32(at + 4, "import name pointer"), "import name");
		im.characterId = r.u32(at + 8, "import character");
		im.pointer = r.u32(at + 12, "import pointer");
		out.imports.push_back(im);
	}
	for (std::uint32_t i = 0; i < exportCount; ++i)
	{
		std::uint32_t at = exportTable + i * 8;
		AptExport ex;
		ex.name = r.cstring(r.u32(at, "export name pointer"), "export name");
		ex.characterId = r.u32(at + 4, "export character");
		out.exports.push_back(ex);
	}
	if (!r.ok())
	{
		return setError(error, r.error());
	}

	if (!parseFrames(r, frameTable, frameCount, out.frames))
	{
		return setError(error, r.error());
	}

	out.characters.resize(charCount);
	for (std::uint32_t id = 0; id < charCount; ++id)
	{
		AptCharacter &c = out.characters[id];
		c.id = id;
		std::uint32_t at = r.u32(charTable + id * 4, "character pointer");
		if (!r.ok())
		{
			return setError(error, r.error());
		}
		if (at == 0)
		{
			c.type = APT_CHAR_NULL;
			continue;
		}
		c.offset = at;
		c.type = r.u32(at, "character type");
		std::uint32_t signature = r.u32(at + 4, "character signature");
		if (!r.ok())
		{
			return setError(error, r.error());
		}
		if (signature != kCharacterSignature)
		{
			return setError(error, movieName + ".apt: character " + std::to_string(id) + " signature is 0x" + std::to_string(signature));
		}
		switch (c.type)
		{
			case APT_CHAR_SHAPE:
				for (int k = 0; k < 4; ++k)
				{
					c.bounds[k] = r.f32(at + 8 + k * 4, "shape bounds");
				}
				c.geometryId = r.u32(at + 24, "shape geometry id");
				break;
			case APT_CHAR_IMAGE:
				c.textureId = r.u32(at + 8, "image texture id");
				break;
			case APT_CHAR_SPRITE:
			{
				std::uint32_t n = 0, tbl = 0;
				if (r.listHeader(at + 8, kMaxFrames, 8, "sprite frames", n, tbl))
				{
					parseFrames(r, tbl, n, c.frames);
				}
				break;
			}
			case APT_CHAR_FONT:
			{
				c.fontName = r.cstring(r.u32(at + 8, "font name pointer"), "font name");
				std::uint32_t glyphCount = r.u32(at + 12, "font glyph count");
				std::uint32_t glyphTable = r.u32(at + 16, "font glyph table");
				if (r.ok() && glyphCount > kMaxGlyphs)
				{
					r.fail("font glyph count exceeds bounds");
				}
				if (r.ok() && glyphCount != 0)
				{
					if (glyphTable == 0)
					{
						r.fail("font glyph table is null");
					}
					else if (r.require(glyphTable, (std::uint64_t)glyphCount * 4, "font glyph table") &&
						 r.charge(r.glyphTotal, glyphCount, kMaxTotalGlyphs, "font glyphs"))
					{
						for (std::uint32_t g = 0; g < glyphCount; ++g)
						{
							c.glyphs.push_back(r.u32(glyphTable + g * 4, "font glyph"));
						}
					}
				}
				break;
			}
			case APT_CHAR_EDITTEXT:
				c.text = std::make_shared<AptTextInfo>();
				parseText(r, at, *c.text);
				break;
			case APT_CHAR_BUTTON:
				c.button = std::make_shared<AptButtonInfo>();
				parseButton(r, at, *c.button);
				break;
			case APT_CHAR_MOVIE:
				c.opaque = (at != entry); // a non-root movie character has no decoded layout
				break;
			case APT_CHAR_SOUND:
			case APT_CHAR_MORPH:
			case APT_CHAR_STATICTEXT:
			case APT_CHAR_NONE:
			case APT_CHAR_VIDEO:
				c.opaque = true;
				break;
			default:
				r.fail("character " + std::to_string(id) + " has unknown type " + std::to_string(c.type));
				break;
		}
		if (!r.ok())
		{
			return setError(error, r.error());
		}
	}
	return true;
}

bool AptFile::findExport(const std::string &exportName, std::uint32_t &characterId, bool *ambiguous) const
{
	auto lower = [](const std::string &s) {
		std::string o = s;
		for (char &ch : o)
		{
			if (ch >= 'A' && ch <= 'Z')
			{
				ch = (char)(ch - 'A' + 'a');
			}
		}
		return o;
	};
	std::string want = lower(exportName);
	bool found = false;
	if (ambiguous)
	{
		*ambiguous = false;
	}
	for (const AptExport &e : exports)
	{
		if (lower(e.name) != want)
		{
			continue;
		}
		if (!found)
		{
			characterId = e.characterId;
			found = true;
		}
		else if (e.characterId != characterId && ambiguous)
		{
			*ambiguous = true;
		}
	}
	return found;
}

std::shared_ptr<const AptCodeBlock> AptFile::codeAt(std::uint32_t offset, std::string *error) const
{
	auto it = m_codeCache.find(offset);
	if (it != m_codeCache.end())
	{
		return it->second;
	}
	std::shared_ptr<const AptCodeBlock> block;
	if (!AptActionDecoder::decodeProgram(*this, offset, block, error))
	{
		return nullptr;
	}
	m_codeCache[offset] = block;
	return block;
}

std::vector<std::uint32_t> AptFile::programOffsets() const
{
	std::vector<std::uint32_t> out;
	std::set<std::uint32_t> seen;
	auto add = [&](std::uint32_t off) {
		if (seen.insert(off).second)
		{
			out.push_back(off);
		}
	};
	auto walk = [&](const std::vector<AptFrame> &fr) {
		for (const AptFrame &f : fr)
		{
			for (const AptFrameItem &it : f.items)
			{
				if (it.type == APT_ITEM_ACTION || it.type == APT_ITEM_INITACTION)
				{
					add(it.codeOffset);
				}
				else if (it.type == APT_ITEM_PLACEOBJECT && it.place)
				{
					for (const AptClipEvent &e : it.place->clipEvents)
					{
						add(e.codeOffset);
					}
				}
			}
		}
	};
	walk(frames);
	for (const AptCharacter &c : characters)
	{
		if (c.type == APT_CHAR_SPRITE)
		{
			walk(c.frames);
		}
		else if (c.type == APT_CHAR_BUTTON && c.button)
		{
			for (const AptButtonAction &a : c.button->actions)
			{
				add(a.codeOffset);
			}
		}
	}
	return out;
}

// =============================================================================================
// .dat
// =============================================================================================

bool AptImageMap::parse(const std::vector<std::uint8_t> &bytes, AptImageMap &out, std::string *error)
{
	out = AptImageMap();
	std::string text(bytes.begin(), bytes.end());
	for (unsigned char ch : text)
	{
		if (ch >= 0x80)
		{
			return setError(error, ".dat: non-ASCII byte");
		}
	}
	std::set<std::uint32_t> seenIds;
	std::size_t pos = 0;
	int lineNo = 0;
	while (pos <= text.size())
	{
		std::size_t end = text.find('\n', pos);
		if (end == std::string::npos)
		{
			end = text.size();
		}
		std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		++lineNo;
		std::size_t semi = line.find(';');
		if (semi != std::string::npos)
		{
			line.resize(semi);
		}
		// trim
		std::size_t b = line.find_first_not_of(" \t\r");
		if (b == std::string::npos)
		{
			continue;
		}
		std::size_t e2 = line.find_last_not_of(" \t\r");
		line = line.substr(b, e2 - b + 1);

		std::size_t i = 0;
		while (i < line.size() && line[i] >= '0' && line[i] <= '9')
		{
			++i;
		}
		auto bad = [&](const char *why) { return setError(error, ".dat line " + std::to_string(lineNo) + ": " + why + " '" + line + "'"); };
		if (i == 0)
		{
			return bad("unsupported record");
		}
		AptImageMapEntry entry;
		{
			unsigned long long id = 0;
			auto res = std::from_chars(line.data(), line.data() + i, id);
			if (res.ec != std::errc() || id > 0xFFFFFFFFull)
			{
				return bad("bad image id");
			}
			entry.imageId = (std::uint32_t)id;
		}
		std::size_t j = i;
		while (j < line.size() && (line[j] == ' ' || line[j] == '\t'))
		{
			++j;
		}
		bool arrow = false;
		if (line.compare(j, 2, "->") == 0)
		{
			arrow = true;
			j += 2;
		}
		else if (j < line.size() && line[j] == '=')
		{
			++j;
		}
		else
		{
			return bad("missing '=' or '->'");
		}
		std::vector<long long> nums;
		while (j < line.size())
		{
			while (j < line.size() && (line[j] == ' ' || line[j] == '\t'))
			{
				++j;
			}
			if (j >= line.size())
			{
				break;
			}
			std::size_t k = j;
			if (line[k] == '-')
			{
				++k;
			}
			std::size_t digits = k;
			while (k < line.size() && line[k] >= '0' && line[k] <= '9')
			{
				++k;
			}
			if (k == digits)
			{
				return bad("non-numeric value");
			}
			long long v = 0;
			auto res = std::from_chars(line.data() + j, line.data() + k, v);
			if (res.ec != std::errc())
			{
				return bad("bad number");
			}
			nums.push_back(v);
			j = k;
		}
		if (!seenIds.insert(entry.imageId).second)
		{
			return bad("duplicate image id");
		}
		if (arrow)
		{
			if (nums.size() != 1 || nums[0] < 0)
			{
				return bad("'->' needs exactly one texture id");
			}
			entry.isRect = false;
			entry.textureId = (std::uint32_t)nums[0];
		}
		else
		{
			if (nums.size() != 4 || nums[2] <= 0 || nums[3] <= 0 || nums[0] < 0 || nums[1] < 0)
			{
				return bad("'=' needs x y w h with positive size");
			}
			entry.isRect = true;
			entry.textureId = entry.imageId;
			for (int k = 0; k < 4; ++k)
			{
				entry.rect[k] = (std::int32_t)nums[k];
			}
		}
		out.entries.push_back(entry);
	}
	return true;
}

// =============================================================================================
// .ru
// =============================================================================================

namespace
{

bool splitColons(const std::string &s, std::size_t from, std::vector<std::string> &parts)
{
	parts.clear();
	std::size_t start = from;
	while (true)
	{
		std::size_t c = s.find(':', start);
		if (c == std::string::npos)
		{
			parts.push_back(s.substr(start));
			break;
		}
		parts.push_back(s.substr(start, c - start));
		start = c + 1;
	}
	return true;
}

bool parseFloatStrict(const std::string &s, float &out)
{
	if (s.empty())
	{
		return false;
	}
	double v = 0;
	const char *b = s.data();
	if (*b == '+')
	{
		++b;
	}
	auto res = std::from_chars(b, s.data() + s.size(), v);
	if (res.ec != std::errc() || res.ptr != s.data() + s.size() || !std::isfinite(v))
	{
		return false;
	}
	out = (float)v;
	return true;
}

} // namespace

bool AptGeometry::parse(const std::vector<std::uint8_t> &bytes, AptGeometry &out, std::string *error)
{
	out = AptGeometry();
	if (bytes.empty())
	{
		return setError(error, ".ru: empty geometry file");
	}
	std::string text(bytes.begin(), bytes.end());
	for (unsigned char ch : text)
	{
		if (ch >= 0x80)
		{
			return setError(error, ".ru: non-ASCII byte");
		}
	}
	AptGeometryStyle *style = nullptr; // style of the current group; null after 'c'
	std::size_t pos = 0;
	int lineNo = 0;
	while (pos < text.size())
	{
		std::size_t end = text.find('\n', pos);
		if (end == std::string::npos)
		{
			end = text.size();
		}
		std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		++lineNo;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
		{
			line.pop_back();
		}
		std::size_t b = line.find_first_not_of(" \t");
		if (b == std::string::npos)
		{
			continue;
		}
		line = line.substr(b);
		auto bad = [&](const char *why) { return setError(error, ".ru line " + std::to_string(lineNo) + ": " + why + " '" + line + "'"); };
		std::vector<std::string> parts;
		if (line == "c")
		{
			style = nullptr;
			++out.clearCount;
		}
		else if (line[0] == 'c')
		{
			return bad("malformed clear");
		}
		else if (line.compare(0, 2, "s ") == 0)
		{
			splitColons(line, 2, parts);
			AptGeometryStyle s;
			std::size_t expected = 0;
			if (parts[0] == "s")
			{
				s.kind = APT_STYLE_SOLID;
				expected = 5;
			}
			else if (parts[0] == "l")
			{
				s.kind = APT_STYLE_LINE;
				expected = 6;
			}
			else if (parts[0] == "tc")
			{
				s.kind = APT_STYLE_TEXTURED;
				expected = 12;
			}
			else
			{
				return bad("unsupported style");
			}
			if (parts.size() != expected)
			{
				return bad("style has the wrong number of fields");
			}
			std::vector<float> v(parts.size() - 1);
			for (std::size_t i = 1; i < parts.size(); ++i)
			{
				if (!parseFloatStrict(parts[i], v[i - 1]))
				{
					return bad("bad number");
				}
			}
			if (s.kind == APT_STYLE_SOLID)
			{
				for (int i = 0; i < 4; ++i)
				{
					s.rgba[i] = v[i];
				}
			}
			else if (s.kind == APT_STYLE_LINE)
			{
				s.lineWidth = v[0];
				for (int i = 0; i < 4; ++i)
				{
					s.rgba[i] = v[1 + i];
				}
			}
			else
			{
				for (int i = 0; i < 4; ++i)
				{
					s.rgba[i] = v[i];
				}
				if (v[4] < 0 || v[4] != std::floor(v[4]))
				{
					return bad("image id is not a non-negative integer");
				}
				s.imageId = (std::int32_t)v[4];
				for (int i = 0; i < 6; ++i)
				{
					s.uv[i] = v[5 + i];
				}
			}
			out.styles.push_back(std::move(s));
			style = &out.styles.back();
		}
		else if (line.compare(0, 2, "t ") == 0)
		{
			if (!style || style->kind == APT_STYLE_LINE)
			{
				return bad("triangle outside a solid or textured style");
			}
			splitColons(line, 2, parts);
			if (parts.size() != 6)
			{
				return bad("triangle needs 6 coordinates");
			}
			for (const std::string &p : parts)
			{
				float f;
				if (!parseFloatStrict(p, f))
				{
					return bad("bad number");
				}
				style->triangles.push_back(f);
			}
		}
		else if (line.compare(0, 2, "l ") == 0)
		{
			if (!style || style->kind != APT_STYLE_LINE)
			{
				return bad("line outside a line style");
			}
			splitColons(line, 2, parts);
			if (parts.size() != 4)
			{
				return bad("line needs 4 coordinates");
			}
			for (const std::string &p : parts)
			{
				float f;
				if (!parseFloatStrict(p, f))
				{
					return bad("bad number");
				}
				style->lines.push_back(f);
			}
		}
		else
		{
			return bad("unknown geometry record");
		}
	}
	return true;
}
