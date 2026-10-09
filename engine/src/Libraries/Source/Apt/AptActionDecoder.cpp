// OpenBFME. GPL-3.0.
// See AptActionDecoder.h for the format notes and citations.

#include "Libraries/Source/Apt/AptActionDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace
{

struct OpInfo
{
	std::uint8_t opcode;
	const char *name;
	bool aligned;
};

// The 86 opcodes in the RotWK corpus census (spec menus-apt.md 2.4) with their SWF/EA names.
const OpInfo kOps[] = {
	{ 0x00, "End", false },
	{ 0x04, "NextFrame", false },
	{ 0x06, "Play", false },
	{ 0x07, "Stop", false },
	{ 0x0B, "Subtract", false },
	{ 0x0C, "Multiply", false },
	{ 0x0D, "Divide", false },
	{ 0x12, "Not", false },
	{ 0x13, "StringEquals", false },
	{ 0x17, "Pop", false },
	{ 0x18, "ToInteger", false },
	{ 0x1C, "GetVariable", false },
	{ 0x1D, "SetVariable", false },
	{ 0x21, "StringConcat", false },
	{ 0x22, "GetProperty", false },
	{ 0x23, "SetProperty", false },
	{ 0x24, "CloneSprite", false },
	{ 0x25, "RemoveSprite", false },
	{ 0x26, "Trace", false },
	{ 0x30, "Random", false },
	{ 0x3A, "Delete", false },
	{ 0x3B, "Delete2", false },
	{ 0x3C, "DefineLocal", false },
	{ 0x3D, "CallFunction", false },
	{ 0x3E, "Return", false },
	{ 0x3F, "Modulo", false },
	{ 0x40, "NewObject", false },
	{ 0x41, "DefineLocal2", false },
	{ 0x42, "InitArray", false },
	{ 0x43, "InitObject", false },
	{ 0x44, "TypeOf", false },
	{ 0x47, "Add2", false },
	{ 0x48, "Less2", false },
	{ 0x49, "Equals2", false },
	{ 0x4A, "ToNumber", false },
	{ 0x4B, "ToString", false },
	{ 0x4C, "PushDuplicate", false },
	{ 0x4E, "GetMember", false },
	{ 0x4F, "SetMember", false },
	{ 0x50, "Increment", false },
	{ 0x51, "Decrement", false },
	{ 0x52, "CallMethod", false },
	{ 0x55, "Enumerate2", false },
	{ 0x59, "EA_PushZero", false },
	{ 0x5A, "EA_PushOne", false },
	{ 0x5B, "EA_CallFuncPop", false },
	{ 0x5D, "EA_CallMethodPop", false },
	{ 0x5E, "EA_CallMethod", false },
	{ 0x60, "BitAnd", false },
	{ 0x64, "BitRShift", false },
	{ 0x67, "Greater", false },
	{ 0x70, "EA_PushThisVar", false },
	{ 0x71, "EA_PushGlobalVar", false },
	{ 0x72, "EA_ZeroVar", false },
	{ 0x73, "EA_PushTrue", false },
	{ 0x74, "EA_PushFalse", false },
	{ 0x75, "EA_PushNull", false },
	{ 0x76, "EA_PushUndefined", false },
	{ 0x81, "GotoFrame", true },
	{ 0x83, "GetURL", true },
	{ 0x87, "SetRegister", true },
	{ 0x88, "ConstantPool", true },
	{ 0x8C, "GotoLabel", true },
	{ 0x8E, "DefineFunction2", true },
	{ 0x96, "PushData", true },
	{ 0x99, "BranchAlways", true },
	{ 0x9A, "GetURL2", false },
	{ 0x9B, "DefineFunction", true },
	{ 0x9D, "BranchIfTrue", true },
	{ 0x9F, "GotoFrame2", true },
	{ 0xA1, "EA_PushString", true },
	{ 0xA2, "EA_PushConstantByte", false },
	{ 0xA4, "EA_GetStringVar", true },
	{ 0xA5, "EA_GetStringMember", true },
	{ 0xA6, "EA_SetStringVar", true },
	{ 0xA7, "EA_SetStringMember", true },
	{ 0xAE, "EA_PushValueOfVar", false },
	{ 0xAF, "EA_GetNamedMember", false },
	{ 0xB0, "EA_CallNamedFuncPop", false },
	{ 0xB1, "EA_CallNamedFunc", false },
	{ 0xB2, "EA_CallNamedMethodPop", false },
	{ 0xB3, "EA_CallNamedMethod", false },
	{ 0xB4, "EA_PushFloat", false },
	{ 0xB5, "EA_PushByte", false },
	{ 0xB6, "EA_PushShort", false },
	{ 0xB7, "EA_PushLong", false },
};

const OpInfo *findOp(std::uint8_t opcode)
{
	static std::map<std::uint8_t, const OpInfo *> table;
	if (table.empty())
	{
		for (const OpInfo &o : kOps)
		{
			table[o.opcode] = &o;
		}
	}
	auto it = table.find(opcode);
	return it == table.end() ? nullptr : it->second;
}

const std::uint8_t kFunctionTrailer[8] = { 0x32, 0x54, 0x76, 0x98, 0x78, 0x56, 0x34, 0x12 };

class Decoder
{
public:
	Decoder(const AptFile &file) : m_file(file), m_data(*file.data) {}

	// Decode [start, end) when bounded, else up to and including the first End.
	bool decodeBlock(std::uint32_t start, bool bounded, std::uint32_t end, std::shared_ptr<AptCodeBlock> &out, int depth)
	{
		if (depth > 64)
		{
			return fail("function nesting exceeds 64 levels at offset " + std::to_string(start));
		}
		if (start >= m_data.size() || (bounded && (end < start || end > m_data.size())))
		{
			return fail("ActionScript range at " + std::to_string(start) + " is out of bounds");
		}
		auto block = std::make_shared<AptCodeBlock>();
		block->startOffset = start;
		block->swfVersion = m_file.version;
		std::uint32_t pos = start;
		std::map<std::uint32_t, std::int32_t> indexByOffset;
		while (!bounded || pos < end)
		{
			if (block->instructions.size() >= 100000)
			{
				return fail("ActionScript instruction count exceeds bounds at offset " + std::to_string(start));
			}
			if (pos >= m_data.size())
			{
				return fail("ActionScript opcode at " + std::to_string(pos) + " is out of bounds");
			}
			AptInstruction ins;
			ins.offset = pos;
			ins.opcode = m_data[pos];
			++pos;
			const OpInfo *info = findOp(ins.opcode);
			if (!info)
			{
				char buf[96];
				std::snprintf(buf, sizeof(buf), "ActionScript opcode 0x%02x at offset %u is not supported (not in the RotWK corpus census)", ins.opcode, ins.offset);
				return fail(buf);
			}
			if (info->aligned)
			{
				std::uint32_t aligned = (pos + 3u) & ~3u;
				if (aligned > m_data.size())
				{
					return fail("ActionScript alignment at " + std::to_string(ins.offset) + " is out of bounds");
				}
				for (std::uint32_t p = pos; p < aligned; ++p)
				{
					if (m_data[p] != 0)
					{
						return fail("ActionScript alignment bytes at " + std::to_string(ins.offset) + " are not zero");
					}
				}
				pos = aligned;
			}
			if (!readOperand(ins, pos, depth))
			{
				return false;
			}
			ins.nextOffset = pos;
			indexByOffset[ins.offset] = (std::int32_t)block->instructions.size();
			std::uint8_t op = ins.opcode;
			block->instructions.push_back(std::move(ins));
			if (op == APT_OP_END && !bounded)
			{
				break;
			}
		}
		if (bounded && pos != end)
		{
			return fail("ActionScript bounded body ended at " + std::to_string(pos) + ", expected " + std::to_string(end));
		}
		if (!bounded && (block->instructions.empty() || block->instructions.back().opcode != APT_OP_END))
		{
			return fail("ActionScript stream at " + std::to_string(start) + " lacks an End instruction");
		}
		block->endOffset = pos;
		indexByOffset[pos] = (std::int32_t)block->instructions.size();
		for (AptInstruction &ins : block->instructions)
		{
			if (ins.opcode != APT_OP_BRANCHALWAYS && ins.opcode != APT_OP_BRANCHIFTRUE)
			{
				continue;
			}
			std::int64_t target = (std::int64_t)ins.nextOffset + ins.intOperand;
			auto it = target < 0 ? indexByOffset.end() : indexByOffset.find((std::uint32_t)target);
			if (it == indexByOffset.end())
			{
				return fail("ActionScript branch at " + std::to_string(ins.offset) + " targets non-instruction offset " + std::to_string(target));
			}
			ins.branchTarget = it->second;
		}
		out = std::move(block);
		return true;
	}

	std::string error;

private:
	bool fail(const std::string &message)
	{
		if (error.empty())
		{
			error = m_file.name + ".apt: " + message;
		}
		return false;
	}

	bool need(std::uint32_t pos, std::uint32_t size, const char *what)
	{
		if ((std::uint64_t)pos + size > m_data.size())
		{
			return fail(std::string("ActionScript ") + what + " at " + std::to_string(pos) + " is out of bounds");
		}
		return true;
	}

	std::uint32_t rd32(std::uint32_t pos)
	{
		return (std::uint32_t)m_data[pos] | ((std::uint32_t)m_data[pos + 1] << 8) | ((std::uint32_t)m_data[pos + 2] << 16) | ((std::uint32_t)m_data[pos + 3] << 24);
	}

	bool readString(std::uint32_t pointer, const char *what, std::string &out)
	{
		if (pointer >= m_data.size())
		{
			return fail(std::string("ActionScript ") + what + " string pointer " + std::to_string(pointer) + " is out of bounds");
		}
		std::size_t limit = std::min<std::size_t>(m_data.size(), (std::size_t)pointer + 4097);
		for (std::size_t i = pointer; i < limit; ++i)
		{
			if (m_data[i] == 0)
			{
				out.assign((const char *)m_data.data() + pointer, i - pointer);
				return true;
			}
		}
		return fail(std::string("ActionScript ") + what + " string at " + std::to_string(pointer) + " is unterminated");
	}

	bool readConstRefs(AptInstruction &ins, std::uint32_t &pos, const char *what)
	{
		if (!need(pos, 8, what))
		{
			return false;
		}
		std::uint32_t count = rd32(pos);
		std::uint32_t table = rd32(pos + 4);
		pos += 8;
		if (count > 4096)
		{
			return fail(std::string("ActionScript ") + what + " count " + std::to_string(count) + " exceeds bounds");
		}
		if (count && !need(table, count * 4, what))
		{
			return false;
		}
		for (std::uint32_t i = 0; i < count; ++i)
		{
			std::uint32_t idx = rd32(table + i * 4);
			if (idx >= m_file.consts.entries.size())
			{
				return fail(std::string("ActionScript ") + what + " constant index " + std::to_string(idx) + " is out of bounds");
			}
			const AptConstEntry &e = m_file.consts.entries[idx];
			// Each reference copies its string, and the index tables of different instructions may alias one
			// 4096-entry table, so the per-instruction count does not bound the copies: cap the totals per decode.
			// These are implementation resource bounds (docs/STOPS.md S-009), not EA values.
			if (++m_constRefs > kMaxConstRefs)
			{
				return fail(std::string("ActionScript ") + what + " instructions reference more than " + std::to_string(kMaxConstRefs) +
					" constants in total (aliased index tables?)");
			}
			m_constBytes += e.text.size();
			if (m_constBytes > kMaxConstBytes)
			{
				return fail(std::string("ActionScript ") + what + " instructions copy more than " + std::to_string(kMaxConstBytes) +
					" bytes of constant strings in total (aliased index tables?)");
			}
			AptConstRef ref;
			ref.constIndex = idx;
			ref.type = e.type;
			ref.raw = e.raw;
			ref.text = e.text;
			ins.constants.push_back(std::move(ref));
		}
		return true;
	}

	bool readOperand(AptInstruction &ins, std::uint32_t &pos, int depth)
	{
		switch (ins.opcode)
		{
			case APT_OP_EA_PUSHCONSTANTBYTE:
			case APT_OP_EA_PUSHVALUEOFVAR:
			case APT_OP_EA_GETNAMEDMEMBER:
			case APT_OP_EA_CALLNAMEDFUNCPOP:
			case APT_OP_EA_CALLNAMEDFUNC:
			case APT_OP_EA_CALLNAMEDMETHODPOP:
			case APT_OP_EA_CALLNAMEDMETHOD:
				if (!need(pos, 1, "byte operand"))
				{
					return false;
				}
				ins.intOperand = m_data[pos];
				pos += 1;
				return true;
			case APT_OP_EA_PUSHBYTE:
				// signed: BFME1 Rva008CB260AppendToken.cpp reads `char c` and calls AptInteger::Create(c)
				if (!need(pos, 1, "PushByte operand"))
				{
					return false;
				}
				ins.intOperand = (std::int8_t)m_data[pos];
				pos += 1;
				return true;
			case APT_OP_EA_PUSHSHORT:
				// signed: Bfme5PushShort8CB2A0.cpp `short immediate`
				if (!need(pos, 2, "PushShort operand"))
				{
					return false;
				}
				ins.intOperand = (std::int16_t)(m_data[pos] | (m_data[pos + 1] << 8));
				pos += 2;
				return true;
			case APT_OP_EA_PUSHLONG:
				if (!need(pos, 4, "PushLong operand"))
				{
					return false;
				}
				ins.intOperand = (std::int32_t)rd32(pos);
				pos += 4;
				return true;
			case APT_OP_EA_PUSHFLOAT:
			{
				if (!need(pos, 4, "PushFloat operand"))
				{
					return false;
				}
				std::uint32_t bits = rd32(pos);
				std::memcpy(&ins.floatOperand, &bits, 4);
				if (!std::isfinite(ins.floatOperand))
				{
					return fail("ActionScript PushFloat operand at " + std::to_string(pos) + " is not finite");
				}
				pos += 4;
				return true;
			}
			case APT_OP_GOTOFRAME:
			case APT_OP_SETREGISTER:
			case APT_OP_BRANCHALWAYS:
			case APT_OP_BRANCHIFTRUE:
			case APT_OP_GOTOFRAME2:
				if (!need(pos, 4, "operand"))
				{
					return false;
				}
				ins.intOperand = (std::int32_t)rd32(pos);
				pos += 4;
				return true;
			case APT_OP_GOTOLABEL:
			case APT_OP_EA_PUSHSTRING:
			case APT_OP_EA_GETSTRINGVAR:
			case APT_OP_EA_GETSTRINGMEMBER:
			case APT_OP_EA_SETSTRINGVAR:
			case APT_OP_EA_SETSTRINGMEMBER:
				if (!need(pos, 4, "string operand"))
				{
					return false;
				}
				if (!readString(rd32(pos), "operand", ins.text))
				{
					return false;
				}
				pos += 4;
				return true;
			case APT_OP_GETURL:
				if (!need(pos, 8, "GetURL operand"))
				{
					return false;
				}
				if (!readString(rd32(pos), "GetURL url", ins.text) || !readString(rd32(pos + 4), "GetURL target", ins.text2))
				{
					return false;
				}
				pos += 8;
				return true;
			case APT_OP_CONSTANTPOOL:
				return readConstRefs(ins, pos, "ConstantPool");
			case APT_OP_PUSHDATA:
				return readConstRefs(ins, pos, "PushData");
			case APT_OP_DEFINEFUNCTION:
			case APT_OP_DEFINEFUNCTION2:
				return readFunction(ins, pos, depth);
			default:
				return true; // no operand
		}
	}

	bool readFunction(AptInstruction &ins, std::uint32_t &pos, int depth)
	{
		bool v2 = ins.opcode == APT_OP_DEFINEFUNCTION2;
		std::uint32_t headerSize = v2 ? 28u : 24u;
		if (!need(pos, headerSize, "function header"))
		{
			return false;
		}
		auto def = std::make_shared<AptFunctionDef>();
		def->isV2 = v2;
		def->swfVersion = m_file.version;
		std::uint32_t namePtr = rd32(pos);
		std::uint32_t paramCount = rd32(pos + 4);
		std::uint32_t paramTable;
		std::int32_t bodySize;
		std::uint32_t trailerAt;
		if (v2)
		{
			def->registerCount = m_data[pos + 8];
			def->flags = (std::uint32_t)m_data[pos + 9] | ((std::uint32_t)m_data[pos + 10] << 8) | ((std::uint32_t)m_data[pos + 11] << 16);
			paramTable = rd32(pos + 12);
			bodySize = (std::int32_t)rd32(pos + 16);
			trailerAt = pos + 20;
		}
		else
		{
			paramTable = rd32(pos + 8);
			bodySize = (std::int32_t)rd32(pos + 12);
			trailerAt = pos + 16;
		}
		if (paramCount > 256)
		{
			return fail("ActionScript parameter count " + std::to_string(paramCount) + " exceeds bounds");
		}
		if (std::memcmp(m_data.data() + trailerAt, kFunctionTrailer, 8) != 0)
		{
			return fail("ActionScript function trailer at " + std::to_string(trailerAt) + " changed");
		}
		std::uint32_t stride = v2 ? 8u : 4u;
		if (paramCount && !need(paramTable, paramCount * stride, "function parameters"))
		{
			return false;
		}
		for (std::uint32_t i = 0; i < paramCount; ++i)
		{
			AptFunctionParam p;
			std::uint32_t at = paramTable + i * stride;
			if (v2)
			{
				p.reg = (std::int32_t)rd32(at);
				if (!readString(rd32(at + 4), "parameter name", p.name))
				{
					return false;
				}
			}
			else
			{
				p.reg = -1;
				if (!readString(rd32(at), "parameter name", p.name))
				{
					return false;
				}
			}
			def->params.push_back(std::move(p));
		}
		if (!readString(namePtr, "function name", def->name))
		{
			return false;
		}
		pos += headerSize;
		if (bodySize < 0 || (std::uint64_t)pos + (std::uint32_t)bodySize > m_data.size())
		{
			return fail("ActionScript function body at " + std::to_string(pos) + " is out of bounds");
		}
		std::shared_ptr<AptCodeBlock> body;
		if (!decodeBlock(pos, true, pos + (std::uint32_t)bodySize, body, depth + 1))
		{
			return false;
		}
		def->body = body;
		pos += (std::uint32_t)bodySize;
		ins.function = def;
		return true;
	}

	static constexpr std::uint64_t kMaxConstRefs = 1u << 20;
	static constexpr std::uint64_t kMaxConstBytes = 1u << 26;
	std::uint64_t m_constRefs = 0;
	std::uint64_t m_constBytes = 0;
	const AptFile &m_file;
	const std::vector<std::uint8_t> &m_data;
};

void countBlock(const AptCodeBlock &b, std::uint32_t counts[256])
{
	for (const AptInstruction &i : b.instructions)
	{
		++counts[i.opcode];
		if (i.function && i.function->body)
		{
			countBlock(*i.function->body, counts);
		}
	}
}

} // namespace

std::size_t AptCodeBlock::flatInstructionCount() const
{
	std::size_t n = instructions.size();
	for (const AptInstruction &i : instructions)
	{
		if (i.function && i.function->body)
		{
			n += i.function->body->flatInstructionCount();
		}
	}
	return n;
}

void AptCodeBlock::countOpcodes(std::uint32_t counts[256]) const
{
	countBlock(*this, counts);
}

bool AptActionDecoder::decodeProgram(const AptFile &file, std::uint32_t offset, std::shared_ptr<const AptCodeBlock> &out, std::string *error)
{
	if (!file.data)
	{
		if (error)
		{
			*error = file.name + ".apt: no data";
		}
		return false;
	}
	Decoder d(file);
	std::shared_ptr<AptCodeBlock> block;
	if (!d.decodeBlock(offset, false, 0, block, 0))
	{
		if (error)
		{
			*error = d.error;
		}
		return false;
	}
	out = block;
	return true;
}

const std::vector<std::uint8_t> &AptActionDecoder::supportedOpcodes()
{
	static const std::vector<std::uint8_t> ops = [] {
		std::vector<std::uint8_t> v;
		for (const OpInfo &o : kOps)
		{
			v.push_back(o.opcode);
		}
		std::sort(v.begin(), v.end());
		return v;
	}();
	return ops;
}

bool AptActionDecoder::isSupportedOpcode(std::uint8_t opcode)
{
	return findOp(opcode) != nullptr;
}

bool AptActionDecoder::isAlignedOpcode(std::uint8_t opcode)
{
	const OpInfo *o = findOp(opcode);
	return o && o->aligned;
}

const char *AptActionDecoder::opcodeName(std::uint8_t opcode)
{
	const OpInfo *o = findOp(opcode);
	return o ? o->name : "?";
}
