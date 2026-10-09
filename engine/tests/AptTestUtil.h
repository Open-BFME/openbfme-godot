// OpenBFME unit tests. GPL-3.0.
//
// Builds Apt movies byte by byte so parser and VM tests never need retail files: a movie image
// (.apt + .const), a bytecode assembler with aligned operands, labels and nested functions.
// Layout of the built .apt: header, string pool, code area, then the structure area.

#pragma once

#include "doctest.h"

#include "Libraries/Source/Apt/AptActionDecoder.h"
#include "Libraries/Source/Apt/AptFile.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// doctest streams the operands of CHECK(expr); shared_ptr to our types has no usable operator<<.
namespace doctest
{
template <class T>
struct StringMaker<std::shared_ptr<T>>
{
	static String convert(const std::shared_ptr<T> &p) { return p ? "non-null" : "null"; }
};
} // namespace doctest

namespace apttest
{

inline void put32(std::vector<std::uint8_t> &v, std::uint32_t x)
{
	v.push_back((std::uint8_t)x);
	v.push_back((std::uint8_t)(x >> 8));
	v.push_back((std::uint8_t)(x >> 16));
	v.push_back((std::uint8_t)(x >> 24));
}

inline void patch32(std::vector<std::uint8_t> &v, std::size_t at, std::uint32_t x)
{
	v[at] = (std::uint8_t)x;
	v[at + 1] = (std::uint8_t)(x >> 8);
	v[at + 2] = (std::uint8_t)(x >> 16);
	v[at + 3] = (std::uint8_t)(x >> 24);
}

inline std::uint32_t floatBits(float f)
{
	std::uint32_t b;
	std::memcpy(&b, &f, 4);
	return b;
}

class TestMovie;

// Bytecode assembler.  Emits into the movie's code area; offsets are absolute file offsets so the
// 4-byte operand alignment matches what the decoder expects.
class Asm
{
public:
	Asm(TestMovie &movie, std::uint32_t startOffset) : m_movie(movie), m_start(startOffset) {}

	std::uint32_t offset() const { return m_start + (std::uint32_t)m_bytes.size(); }
	std::uint32_t startOffset() const { return m_start; }
	const std::vector<std::uint8_t> &bytes() const { return m_bytes; }

	Asm &op(std::uint8_t opcode)
	{
		m_bytes.push_back(opcode);
		return *this;
	}

	void align4()
	{
		while (offset() % 4 != 0)
		{
			m_bytes.push_back(0);
		}
	}

	// ---- operand emitters -------------------------------------------------------------
	Asm &opU8(std::uint8_t opcode, std::uint8_t v)
	{
		op(opcode);
		m_bytes.push_back(v);
		return *this;
	}
	Asm &opAlignedI32(std::uint8_t opcode, std::int32_t v)
	{
		op(opcode);
		align4();
		put32(m_bytes, (std::uint32_t)v);
		return *this;
	}
	Asm &opAlignedStr(std::uint8_t opcode, const std::string &s);

	Asm &pushByte(int v) { return opU8(APT_OP_EA_PUSHBYTE, (std::uint8_t)v); }
	Asm &pushShort(int v)
	{
		op(APT_OP_EA_PUSHSHORT);
		m_bytes.push_back((std::uint8_t)v);
		m_bytes.push_back((std::uint8_t)(v >> 8));
		return *this;
	}
	Asm &pushLong(std::int32_t v)
	{
		op(APT_OP_EA_PUSHLONG);
		put32(m_bytes, (std::uint32_t)v);
		return *this;
	}
	Asm &pushFloat(float f)
	{
		op(APT_OP_EA_PUSHFLOAT);
		put32(m_bytes, floatBits(f));
		return *this;
	}
	Asm &pushString(const std::string &s) { return opAlignedStr(APT_OP_EA_PUSHSTRING, s); }
	Asm &getStringVar(const std::string &s) { return opAlignedStr(APT_OP_EA_GETSTRINGVAR, s); }
	Asm &getStringMember(const std::string &s) { return opAlignedStr(APT_OP_EA_GETSTRINGMEMBER, s); }
	Asm &setStringVar(const std::string &s) { return opAlignedStr(APT_OP_EA_SETSTRINGVAR, s); }
	Asm &setStringMember(const std::string &s) { return opAlignedStr(APT_OP_EA_SETSTRINGMEMBER, s); }
	Asm &gotoLabel(const std::string &s) { return opAlignedStr(APT_OP_GOTOLABEL, s); }
	Asm &getURL(const std::string &url, const std::string &target);

	// ConstantPool / PushData over .const table indices.
	Asm &constantPool(const std::vector<std::string> &strings);
	Asm &constantPoolIdx(const std::vector<std::uint32_t> &constIndices);
	// ConstantPool whose (count, table pointer) operand points at a table the caller built elsewhere in the file,
	// so several instructions can share one index table.
	Asm &constantPoolAt(std::uint32_t count, std::uint32_t tableOffset);
	Asm &pushDataIdx(const std::vector<std::uint32_t> &constIndices);
	Asm &pushConstByte(int poolIndex) { return opU8(APT_OP_EA_PUSHCONSTANTBYTE, (std::uint8_t)poolIndex); }
	Asm &pushValueOfVar(int poolIndex) { return opU8(APT_OP_EA_PUSHVALUEOFVAR, (std::uint8_t)poolIndex); }
	Asm &getNamedMember(int poolIndex) { return opU8(APT_OP_EA_GETNAMEDMEMBER, (std::uint8_t)poolIndex); }
	Asm &setRegister(int reg) { return opAlignedI32(APT_OP_SETREGISTER, reg); }

	// ---- branches ---------------------------------------------------------------------
	Asm &branchAlways(const std::string &label) { return branch(APT_OP_BRANCHALWAYS, label); }
	Asm &branchIfTrue(const std::string &label) { return branch(APT_OP_BRANCHIFTRUE, label); }
	Asm &label(const std::string &name)
	{
		m_labels[name] = offset();
		return *this;
	}
	Asm &rawBranch(std::uint8_t opcode, std::int32_t relative) { return opAlignedI32(opcode, relative); }

	// ---- functions --------------------------------------------------------------------
	Asm &defineFunction(const std::string &name, const std::vector<std::string> &params, const std::function<void(Asm &)> &body);
	Asm &defineFunction2(const std::string &name, std::uint32_t registerCount, std::uint32_t flags,
		const std::vector<std::pair<int, std::string>> &params, const std::function<void(Asm &)> &body);

	// Resolve forward branches; call once when the program is complete.
	void finish()
	{
		for (const auto &f : m_fixups)
		{
			auto it = m_labels.find(f.label);
			assert(it != m_labels.end());
			std::int32_t rel = (std::int32_t)it->second - (std::int32_t)(f.operandOffset + 4);
			patch32(m_bytes, f.operandOffset - m_start, (std::uint32_t)rel);
		}
		m_fixups.clear();
	}

private:
	friend class TestMovie;
	struct Fixup
	{
		std::string label;
		std::uint32_t operandOffset;
	};
	Asm &branch(std::uint8_t opcode, const std::string &label)
	{
		op(opcode);
		align4();
		m_fixups.push_back({ label, offset() });
		put32(m_bytes, 0);
		return *this;
	}

	TestMovie &m_movie;
	std::uint32_t m_start;
	std::vector<std::uint8_t> m_bytes;
	std::map<std::string, std::uint32_t> m_labels;
	std::vector<Fixup> m_fixups;
};

class TestMovie
{
public:
	static const std::uint32_t kStrBase = 0x100;
	static const std::uint32_t kStrSize = 0x4000;
	static const std::uint32_t kCodeBase = 0x4100;
	static const std::uint32_t kCodeSize = 0x8000;
	static const std::uint32_t kStructBase = 0xC100;

	explicit TestMovie(int version = 7) : m_version(version) {}

	// string pool (NUL-terminated, deduped); returns the absolute file offset
	std::uint32_t str(const std::string &s)
	{
		auto it = m_strOffsets.find(s);
		if (it != m_strOffsets.end())
		{
			return it->second;
		}
		std::uint32_t off = kStrBase + (std::uint32_t)m_strPool.size();
		m_strPool.insert(m_strPool.end(), s.begin(), s.end());
		m_strPool.push_back(0);
		assert(m_strPool.size() < kStrSize);
		m_strOffsets[s] = off;
		return off;
	}

	// .const table; returns the constant index
	std::uint32_t constString(const std::string &s)
	{
		for (std::size_t i = 0; i < m_consts.size(); ++i)
		{
			if (m_consts[i].type == APT_CONST_STRING && m_consts[i].text == s)
			{
				return (std::uint32_t)i;
			}
		}
		AptConstEntry e;
		e.type = APT_CONST_STRING;
		e.text = s;
		m_consts.push_back(e);
		return (std::uint32_t)m_consts.size() - 1;
	}
	std::uint32_t constTyped(std::uint32_t type, std::uint32_t raw)
	{
		AptConstEntry e;
		e.type = type;
		e.raw = raw;
		m_consts.push_back(e);
		return (std::uint32_t)m_consts.size() - 1;
	}
	std::uint32_t constRegister(std::uint32_t reg) { return constTyped(APT_CONST_REGISTER, reg); }
	std::uint32_t constInt(std::int32_t v) { return constTyped(APT_CONST_INTEGER, (std::uint32_t)v); }
	std::uint32_t constFloat(float f) { return constTyped(APT_CONST_FLOAT, floatBits(f)); }
	std::uint32_t constBool(bool b) { return constTyped(APT_CONST_BOOLEAN, b ? 1 : 0); }
	std::uint32_t constNone() { return constTyped(APT_CONST_NONE, 0); }

	// New program assembler positioned at the next 4-aligned spot of the code area.
	Asm program()
	{
		while ((kCodeBase + m_code.size()) % 4 != 0)
		{
			m_code.push_back(0);
		}
		return Asm(*this, kCodeBase + (std::uint32_t)m_code.size());
	}
	// Append a finished program; returns its file offset.
	std::uint32_t commit(Asm &a)
	{
		a.finish();
		std::uint32_t off = a.startOffset();
		assert(off == kCodeBase + m_code.size());
		m_code.insert(m_code.end(), a.bytes().begin(), a.bytes().end());
		assert(m_code.size() < kCodeSize);
		return off;
	}

	// ---- structures ---------------------------------------------------------------------
	std::uint32_t structAt() const { return kStructBase + (std::uint32_t)m_struct.size(); }
	std::uint32_t emit32(std::uint32_t x)
	{
		std::uint32_t at = structAt();
		put32(m_struct, x);
		return at;
	}

	std::uint32_t addActionItem(std::uint32_t codeOffset)
	{
		std::uint32_t at = emit32(APT_ITEM_ACTION);
		emit32(codeOffset);
		return at;
	}
	std::uint32_t addInitActionItem(std::uint32_t spriteId, std::uint32_t codeOffset)
	{
		std::uint32_t at = emit32(APT_ITEM_INITACTION);
		emit32(spriteId);
		emit32(codeOffset);
		return at;
	}
	std::uint32_t addLabelItem(const std::string &name, std::uint32_t flags, std::uint32_t frameId)
	{
		std::uint32_t at = emit32(APT_ITEM_FRAMELABEL);
		emit32(str(name));
		emit32(flags);
		emit32(frameId);
		return at;
	}
	std::uint32_t addRemoveItem(std::int32_t depth)
	{
		std::uint32_t at = emit32(APT_ITEM_REMOVEOBJECT);
		emit32((std::uint32_t)depth);
		return at;
	}
	std::uint32_t addBackgroundItem(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
	{
		std::uint32_t at = emit32(APT_ITEM_BACKGROUNDCOLOR);
		emit32((std::uint32_t)r | ((std::uint32_t)g << 8) | ((std::uint32_t)b << 16) | ((std::uint32_t)a << 24));
		return at;
	}

	struct Place
	{
		std::uint32_t flags = APT_PLACE_HASCHARACTER;
		std::int32_t depth = 1;
		std::int32_t characterId = 0;
		float matrix[4] = { 1, 0, 0, 1 };
		float translation[2] = { 0, 0 };
		std::uint8_t tint[4] = { 255, 255, 255, 255 };
		std::uint8_t additive[4] = { 0, 0, 0, 0 };
		float ratio = 0;
		std::string name;
		std::int32_t clipDepth = 0;
		// clip events: (mask, keyCode, codeOffset); flagged when flags has APT_PLACE_HASCLIPACTION
		struct Ev
		{
			std::uint32_t mask;
			std::uint8_t key;
			std::uint32_t code;
		};
		std::vector<Ev> events;
		bool nullClipActions = false;
	};
	std::uint32_t addPlaceItem(const Place &p)
	{
		// clip-action structures first so the item record can point at them
		std::uint32_t clipAt = 0;
		if ((p.flags & APT_PLACE_HASCLIPACTION) && !p.nullClipActions)
		{
			std::uint32_t evAt = structAt();
			for (const Place::Ev &e : p.events)
			{
				emit32((e.mask & 0xFFFFFF) | ((std::uint32_t)e.key << 24));
				emit32(0);
				emit32(e.code);
			}
			clipAt = emit32((std::uint32_t)p.events.size());
			emit32(evAt);
		}
		std::uint32_t nameOff = (p.flags & APT_PLACE_HASNAME) ? str(p.name) : 0;
		std::uint32_t at = emit32(APT_ITEM_PLACEOBJECT);
		emit32(p.flags);
		emit32((std::uint32_t)p.depth);
		emit32((std::uint32_t)p.characterId);
		for (int i = 0; i < 4; ++i)
		{
			emit32(floatBits(p.matrix[i]));
		}
		for (int i = 0; i < 2; ++i)
		{
			emit32(floatBits(p.translation[i]));
		}
		emit32((std::uint32_t)p.tint[0] | ((std::uint32_t)p.tint[1] << 8) | ((std::uint32_t)p.tint[2] << 16) | ((std::uint32_t)p.tint[3] << 24));
		emit32((std::uint32_t)p.additive[0] | ((std::uint32_t)p.additive[1] << 8) | ((std::uint32_t)p.additive[2] << 16) | ((std::uint32_t)p.additive[3] << 24));
		emit32(floatBits(p.ratio));
		emit32(nameOff);
		emit32((std::uint32_t)p.clipDepth);
		if (p.flags & APT_PLACE_HASCLIPACTION)
		{
			emit32(clipAt);
		}
		return at;
	}

	// frames: each frame is a list of item record pointers; returns (count, table pointer)
	struct FrameTable
	{
		std::uint32_t count;
		std::uint32_t pointer;
	};
	FrameTable addFrames(const std::vector<std::vector<std::uint32_t>> &frames)
	{
		std::vector<std::uint32_t> lists;
		for (const auto &f : frames)
		{
			std::uint32_t at = structAt();
			for (std::uint32_t item : f)
			{
				emit32(item);
			}
			lists.push_back(at);
		}
		std::uint32_t table = structAt();
		for (std::size_t i = 0; i < frames.size(); ++i)
		{
			emit32((std::uint32_t)frames[i].size());
			emit32(lists[i]);
		}
		return { (std::uint32_t)frames.size(), table };
	}

	std::uint32_t addSprite(const std::vector<std::vector<std::uint32_t>> &frames)
	{
		FrameTable ft = addFrames(frames);
		std::uint32_t at = emit32(APT_CHAR_SPRITE);
		emit32(0x09876543);
		emit32(ft.count);
		emit32(ft.pointer);
		return at;
	}
	std::uint32_t addShape(float x0, float y0, float x1, float y1, std::uint32_t geometryId)
	{
		std::uint32_t at = emit32(APT_CHAR_SHAPE);
		emit32(0x09876543);
		emit32(floatBits(x0));
		emit32(floatBits(y0));
		emit32(floatBits(x1));
		emit32(floatBits(y1));
		emit32(geometryId);
		return at;
	}
	std::uint32_t addImage(std::uint32_t textureId)
	{
		std::uint32_t at = emit32(APT_CHAR_IMAGE);
		emit32(0x09876543);
		emit32(textureId);
		return at;
	}
	std::uint32_t addFont(const std::string &name, const std::vector<std::uint32_t> &glyphs)
	{
		std::uint32_t tbl = structAt();
		for (std::uint32_t g : glyphs)
		{
			emit32(g);
		}
		std::uint32_t at = emit32(APT_CHAR_FONT);
		emit32(0x09876543);
		emit32(str(name));
		emit32((std::uint32_t)glyphs.size());
		emit32(glyphs.empty() ? 0 : tbl);
		return at;
	}
	std::uint32_t addEditText(const std::string &initial, const std::string &variable, std::uint32_t alignment = 0, float height = 12.0f, std::uint32_t fontId = 0)
	{
		std::uint32_t at = emit32(APT_CHAR_EDITTEXT);
		emit32(0x09876543);
		emit32(floatBits(0));
		emit32(floatBits(0));
		emit32(floatBits(100));
		emit32(floatBits(20));
		emit32(fontId);                     // font id
		emit32(alignment);                  // alignment
		emit32(0xFF0000FFu);                // colour
		emit32(floatBits(height));
		emit32(1);                          // read only
		emit32(0);                          // multiline
		emit32(0);                          // word wrap
		emit32(str(initial));
		emit32(str(variable));
		return at;
	}

	struct ButtonAction
	{
		std::uint8_t transitions;
		std::uint16_t key;
		std::uint32_t code;
	};
	std::uint32_t addButton(const std::vector<std::pair<std::uint32_t, std::uint32_t>> &recordsMaskChar, const std::vector<ButtonAction> &actions)
	{
		std::uint32_t vtx = structAt();
		float v[8] = { 0, 0, 10, 0, 10, 10, 0, 10 };
		for (float f : v)
		{
			emit32(floatBits(f));
		}
		std::uint32_t tri = structAt();
		std::uint16_t t[6] = { 0, 1, 2, 0, 2, 3 };
		for (int i = 0; i < 6; i += 2)
		{
			emit32((std::uint32_t)t[i] | ((std::uint32_t)t[i + 1] << 16));
		}
		std::uint32_t recs = structAt();
		for (const auto &r : recordsMaskChar)
		{
			emit32(r.first & 0xFF); // state mask + 3 reserved zero bytes
			emit32(r.second);       // character
			emit32(1);              // depth
			emit32(floatBits(1));
			emit32(floatBits(0));
			emit32(floatBits(0));
			emit32(floatBits(1));
			emit32(floatBits(0));
			emit32(floatBits(0));
			for (int i = 0; i < 4; ++i)
			{
				emit32(floatBits(1)); // colour
			}
			for (int i = 0; i < 4; ++i)
			{
				emit32(floatBits(0)); // unknown
			}
		}
		std::uint32_t acts = structAt();
		for (const ButtonAction &a : actions)
		{
			emit32((std::uint32_t)a.transitions | ((std::uint32_t)a.key << 8));
			emit32(a.code);
		}
		std::uint32_t at = emit32(APT_CHAR_BUTTON);
		emit32(0x09876543);
		emit32(0); // isMenu
		for (float f : { 0.0f, 0.0f, 10.0f, 10.0f })
		{
			emit32(floatBits(f));
		}
		emit32(2);   // triangle count
		emit32(4);   // vertex count
		emit32(vtx);
		emit32(tri);
		emit32((std::uint32_t)recordsMaskChar.size());
		emit32(recs);
		emit32((std::uint32_t)actions.size());
		emit32(acts);
		return at;
	}

	std::uint32_t addOpaqueCharacter(std::uint32_t type)
	{
		std::uint32_t at = emit32(type);
		emit32(0x09876543);
		emit32(0);
		return at;
	}

	// Place a character record pointer (or 0 for an import slot) at the next character id.
	std::uint32_t addCharacter(std::uint32_t recordOffset)
	{
		m_charPtrs.push_back(recordOffset);
		return (std::uint32_t)m_charPtrs.size() - 1;
	}
	void addImport(const std::string &movie, const std::string &name, std::uint32_t slot) { m_imports.push_back({ movie, name, slot }); }
	void addExport(const std::string &name, std::uint32_t id) { m_exports.push_back({ name, id }); }
	void setRootFrames(const std::vector<std::vector<std::uint32_t>> &frames) { m_rootFrames = frames; }
	void setSize(std::uint32_t w, std::uint32_t h, std::uint32_t ms)
	{
		m_w = w;
		m_h = h;
		m_ms = ms;
	}

	// Serialise.  `.apt` bytes in aptOut, `.const` bytes in constOut.
	void build(std::vector<std::uint8_t> &aptOut, std::vector<std::uint8_t> &constOut)
	{
		FrameTable ft = addFrames(m_rootFrames);
		// character pointer table
		std::uint32_t charTable = structAt();
		for (std::uint32_t p : m_charPtrs)
		{
			emit32(p);
		}
		std::uint32_t importTable = structAt();
		for (const Imp &i : m_imports)
		{
			emit32(str(i.movie));
			emit32(str(i.name));
			emit32(i.slot);
			emit32(0);
		}
		std::uint32_t exportTable = structAt();
		for (const Exp &e : m_exports)
		{
			emit32(str(e.name));
			emit32(e.id);
		}
		std::uint32_t entry = structAt();
		emit32(APT_CHAR_MOVIE);
		emit32(0x09876543);
		emit32(ft.count);
		emit32(ft.pointer);
		emit32(0);
		emit32((std::uint32_t)m_charPtrs.size());
		emit32(charTable);
		emit32(m_w);
		emit32(m_h);
		emit32(m_ms);
		emit32((std::uint32_t)m_imports.size());
		emit32(importTable);
		emit32((std::uint32_t)m_exports.size());
		emit32(exportTable);
		emit32(0);
		emit32(0);

		aptOut.assign(kStructBase, 0);
		const char magic[] = "Apt Data:";
		std::memcpy(aptOut.data(), magic, 9);
		aptOut[9] = (std::uint8_t)('0' + m_version);
		aptOut[10] = 0x1A;
		aptOut[11] = 0;
		if (!m_strPool.empty())
		{
			std::memcpy(aptOut.data() + kStrBase, m_strPool.data(), m_strPool.size());
		}
		if (!m_code.empty())
		{
			std::memcpy(aptOut.data() + kCodeBase, m_code.data(), m_code.size());
		}
		aptOut.insert(aptOut.end(), m_struct.begin(), m_struct.end());

		// .const
		constOut.assign(32, 0);
		std::memcpy(constOut.data(), "Apt constant file", 17);
		constOut[17] = 0x1A;
		patch32(constOut, 20, entry);
		patch32(constOut, 24, (std::uint32_t)m_consts.size());
		patch32(constOut, 28, 32);
		std::vector<std::uint8_t> records;
		std::vector<std::uint8_t> strings;
		std::uint32_t stringBase = 32 + (std::uint32_t)m_consts.size() * 8;
		for (const AptConstEntry &c : m_consts)
		{
			put32(records, c.type);
			if (c.type == APT_CONST_STRING)
			{
				put32(records, stringBase + (std::uint32_t)strings.size());
				strings.insert(strings.end(), c.text.begin(), c.text.end());
				strings.push_back(0);
			}
			else
			{
				put32(records, c.raw);
			}
		}
		constOut.insert(constOut.end(), records.begin(), records.end());
		constOut.insert(constOut.end(), strings.begin(), strings.end());
	}

	// Parse the built movie.  Fails the calling test by returning false.
	bool parse(AptFile &out, std::string *error)
	{
		std::vector<std::uint8_t> a, c;
		build(a, c);
		AptConstFile cf;
		if (!AptConstFile::parse(c, cf, error))
		{
			return false;
		}
		return AptFile::parse("Test", std::make_shared<const std::vector<std::uint8_t>>(a), cf, out, error);
	}

private:
	friend class Asm;
	struct Imp
	{
		std::string movie, name;
		std::uint32_t slot;
	};
	struct Exp
	{
		std::string name;
		std::uint32_t id;
	};
	int m_version;
	std::vector<std::uint8_t> m_strPool;
	std::map<std::string, std::uint32_t> m_strOffsets;
	std::vector<AptConstEntry> m_consts;
	std::vector<std::uint8_t> m_code;
	std::vector<std::uint8_t> m_struct;
	std::vector<std::uint32_t> m_charPtrs;
	std::vector<Imp> m_imports;
	std::vector<Exp> m_exports;
	std::vector<std::vector<std::uint32_t>> m_rootFrames;
	std::uint32_t m_w = 1024, m_h = 768, m_ms = 33;
};

inline Asm &Asm::opAlignedStr(std::uint8_t opcode, const std::string &s)
{
	std::uint32_t p = m_movie.str(s);
	op(opcode);
	align4();
	put32(m_bytes, p);
	return *this;
}

inline Asm &Asm::getURL(const std::string &url, const std::string &target)
{
	std::uint32_t u = m_movie.str(url);
	std::uint32_t t = m_movie.str(target);
	op(APT_OP_GETURL);
	align4();
	put32(m_bytes, u);
	put32(m_bytes, t);
	return *this;
}

inline Asm &Asm::constantPool(const std::vector<std::string> &strings)
{
	std::vector<std::uint32_t> idx;
	for (const std::string &s : strings)
	{
		idx.push_back(m_movie.constString(s));
	}
	return constantPoolIdx(idx);
}

inline Asm &Asm::constantPoolIdx(const std::vector<std::uint32_t> &constIndices)
{
	// index table lives in the string pool region (raw u32 array, deduped by content is not needed)
	std::uint32_t table = m_movie.m_strOffsets.size() ? 0 : 0;
	(void)table;
	std::string blob;
	for (std::uint32_t i : constIndices)
	{
		blob.append(reinterpret_cast<const char *>(&i), 4);
	}
	// store the table bytes in the string pool (word aligned) and reference it
	while ((TestMovie::kStrBase + m_movie.m_strPool.size()) % 4 != 0)
	{
		m_movie.m_strPool.push_back(0);
	}
	std::uint32_t at = TestMovie::kStrBase + (std::uint32_t)m_movie.m_strPool.size();
	m_movie.m_strPool.insert(m_movie.m_strPool.end(), blob.begin(), blob.end());
	op(APT_OP_CONSTANTPOOL);
	align4();
	put32(m_bytes, (std::uint32_t)constIndices.size());
	put32(m_bytes, at);
	return *this;
}

inline Asm &Asm::constantPoolAt(std::uint32_t count, std::uint32_t tableOffset)
{
	op(APT_OP_CONSTANTPOOL);
	align4();
	put32(m_bytes, count);
	put32(m_bytes, tableOffset);
	return *this;
}

inline Asm &Asm::pushDataIdx(const std::vector<std::uint32_t> &constIndices)
{
	std::string blob;
	for (std::uint32_t i : constIndices)
	{
		blob.append(reinterpret_cast<const char *>(&i), 4);
	}
	while ((TestMovie::kStrBase + m_movie.m_strPool.size()) % 4 != 0)
	{
		m_movie.m_strPool.push_back(0);
	}
	std::uint32_t at = TestMovie::kStrBase + (std::uint32_t)m_movie.m_strPool.size();
	m_movie.m_strPool.insert(m_movie.m_strPool.end(), blob.begin(), blob.end());
	op(APT_OP_PUSHDATA);
	align4();
	put32(m_bytes, (std::uint32_t)constIndices.size());
	put32(m_bytes, at);
	return *this;
}

inline Asm &Asm::defineFunction(const std::string &name, const std::vector<std::string> &params, const std::function<void(Asm &)> &body)
{
	// parameter table (u32 string pointers) lives in the string pool
	while ((TestMovie::kStrBase + m_movie.m_strPool.size()) % 4 != 0)
	{
		m_movie.m_strPool.push_back(0);
	}
	std::uint32_t nameOff = m_movie.str(name);
	std::vector<std::uint32_t> ptrs;
	for (const std::string &p : params)
	{
		ptrs.push_back(m_movie.str(p));
	}
	while ((TestMovie::kStrBase + m_movie.m_strPool.size()) % 4 != 0)
	{
		m_movie.m_strPool.push_back(0);
	}
	std::uint32_t table = TestMovie::kStrBase + (std::uint32_t)m_movie.m_strPool.size();
	for (std::uint32_t p : ptrs)
	{
		m_movie.m_strPool.insert(m_movie.m_strPool.end(), reinterpret_cast<const std::uint8_t *>(&p), reinterpret_cast<const std::uint8_t *>(&p) + 4);
	}
	op(APT_OP_DEFINEFUNCTION);
	align4();
	put32(m_bytes, nameOff);
	put32(m_bytes, (std::uint32_t)params.size());
	put32(m_bytes, table);
	std::size_t sizeAt = m_bytes.size();
	put32(m_bytes, 0);
	static const std::uint8_t trailer[8] = { 0x32, 0x54, 0x76, 0x98, 0x78, 0x56, 0x34, 0x12 };
	m_bytes.insert(m_bytes.end(), trailer, trailer + 8);
	std::size_t bodyStart = m_bytes.size();
	body(*this);
	patch32(m_bytes, sizeAt, (std::uint32_t)(m_bytes.size() - bodyStart));
	return *this;
}

inline Asm &Asm::defineFunction2(const std::string &name, std::uint32_t registerCount, std::uint32_t flags,
	const std::vector<std::pair<int, std::string>> &params, const std::function<void(Asm &)> &body)
{
	std::uint32_t nameOff = m_movie.str(name);
	std::vector<std::uint32_t> table32;
	for (const auto &p : params)
	{
		table32.push_back((std::uint32_t)p.first);
		table32.push_back(m_movie.str(p.second));
	}
	while ((TestMovie::kStrBase + m_movie.m_strPool.size()) % 4 != 0)
	{
		m_movie.m_strPool.push_back(0);
	}
	std::uint32_t table = TestMovie::kStrBase + (std::uint32_t)m_movie.m_strPool.size();
	for (std::uint32_t w : table32)
	{
		m_movie.m_strPool.insert(m_movie.m_strPool.end(), reinterpret_cast<const std::uint8_t *>(&w), reinterpret_cast<const std::uint8_t *>(&w) + 4);
	}
	op(APT_OP_DEFINEFUNCTION2);
	align4();
	put32(m_bytes, nameOff);
	put32(m_bytes, (std::uint32_t)params.size());
	m_bytes.push_back((std::uint8_t)registerCount);
	m_bytes.push_back((std::uint8_t)flags);
	m_bytes.push_back((std::uint8_t)(flags >> 8));
	m_bytes.push_back((std::uint8_t)(flags >> 16));
	put32(m_bytes, table);
	std::size_t sizeAt = m_bytes.size();
	put32(m_bytes, 0);
	static const std::uint8_t trailer[8] = { 0x32, 0x54, 0x76, 0x98, 0x78, 0x56, 0x34, 0x12 };
	m_bytes.insert(m_bytes.end(), trailer, trailer + 8);
	std::size_t bodyStart = m_bytes.size();
	body(*this);
	patch32(m_bytes, sizeAt, (std::uint32_t)(m_bytes.size() - bodyStart));
	return *this;
}

} // namespace apttest
