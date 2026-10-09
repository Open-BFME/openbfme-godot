// OpenBFME. GPL-3.0.
//
// ActionScript bytecode decoder for EA Apt programs.
//
// Operand layouts: OpenSAGE Gui/Apt/ActionScript/Opcodes/Instruction.cs (opcode ids and alignment
// table) and the archived importer retail_hud_apt_convert.py _decode_action_sequence, which decodes
// the whole RotWK corpus.  The EA interpreter reads the same bytes with a cursor
// (BFME1 decomp game/Libraries/Source/EA/Apt/Bfme5PushConstant8CB180.cpp, Bfme5PushShort8CB2A0.cpp,
// Rva008CB2F0DecodeInteger.cpp, Rva008CB200PushFloat, StringLiteralPush008CB050.cpp:
// "aligned string literal": the cursor is rounded up to 4 and a u32 pointer is read).
//
// Alignment: for the opcodes in alignedOpcodes() the operand starts at the next 4-byte boundary of
// the FILE offset; the pad bytes must be zero.
//
// Only the opcodes seen in the RotWK corpus census (86 distinct values) are accepted.  Any other
// opcode - including the 0x1A that the previous importer mis-read out of GuiTest - is rejected with
// a message naming the opcode and its offset.

#pragma once

#include "Libraries/Source/Apt/AptFile.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum AptOp : std::uint8_t
{
	APT_OP_END = 0x00,
	APT_OP_NEXTFRAME = 0x04,
	APT_OP_PLAY = 0x06,
	APT_OP_STOP = 0x07,
	APT_OP_SUBTRACT = 0x0B,
	APT_OP_MULTIPLY = 0x0C,
	APT_OP_DIVIDE = 0x0D,
	APT_OP_NOT = 0x12,
	APT_OP_STRINGEQUALS = 0x13,
	APT_OP_POP = 0x17,
	APT_OP_TOINTEGER = 0x18,
	APT_OP_GETVARIABLE = 0x1C,
	APT_OP_SETVARIABLE = 0x1D,
	APT_OP_STRINGCONCAT = 0x21,
	APT_OP_GETPROPERTY = 0x22,
	APT_OP_SETPROPERTY = 0x23,
	APT_OP_CLONESPRITE = 0x24,
	APT_OP_REMOVESPRITE = 0x25,
	APT_OP_TRACE = 0x26,
	APT_OP_RANDOM = 0x30,
	APT_OP_DELETE = 0x3A,
	APT_OP_DELETE2 = 0x3B,
	APT_OP_DEFINELOCAL = 0x3C,
	APT_OP_CALLFUNCTION = 0x3D,
	APT_OP_RETURN = 0x3E,
	APT_OP_MODULO = 0x3F,
	APT_OP_NEWOBJECT = 0x40,
	APT_OP_DEFINELOCAL2 = 0x41,
	APT_OP_INITARRAY = 0x42,
	APT_OP_INITOBJECT = 0x43,
	APT_OP_TYPEOF = 0x44,
	APT_OP_ADD2 = 0x47,
	APT_OP_LESS2 = 0x48,
	APT_OP_EQUALS2 = 0x49,
	APT_OP_TONUMBER = 0x4A,
	APT_OP_TOSTRING = 0x4B,
	APT_OP_PUSHDUPLICATE = 0x4C,
	APT_OP_GETMEMBER = 0x4E,
	APT_OP_SETMEMBER = 0x4F,
	APT_OP_INCREMENT = 0x50,
	APT_OP_DECREMENT = 0x51,
	APT_OP_CALLMETHOD = 0x52,
	APT_OP_ENUMERATE2 = 0x55,
	APT_OP_EA_PUSHZERO = 0x59,
	APT_OP_EA_PUSHONE = 0x5A,
	APT_OP_EA_CALLFUNCPOP = 0x5B,
	APT_OP_EA_CALLMETHODPOP = 0x5D,
	APT_OP_EA_CALLMETHOD = 0x5E,
	APT_OP_BITAND = 0x60,
	APT_OP_BITRSHIFT = 0x64,
	APT_OP_GREATER = 0x67,
	APT_OP_EA_PUSHTHISVAR = 0x70,
	APT_OP_EA_PUSHGLOBALVAR = 0x71,
	APT_OP_EA_ZEROVAR = 0x72,
	APT_OP_EA_PUSHTRUE = 0x73,
	APT_OP_EA_PUSHFALSE = 0x74,
	APT_OP_EA_PUSHNULL = 0x75,
	APT_OP_EA_PUSHUNDEFINED = 0x76,
	APT_OP_GOTOFRAME = 0x81,
	APT_OP_GETURL = 0x83,
	APT_OP_SETREGISTER = 0x87,
	APT_OP_CONSTANTPOOL = 0x88,
	APT_OP_GOTOLABEL = 0x8C,
	APT_OP_DEFINEFUNCTION2 = 0x8E,
	APT_OP_PUSHDATA = 0x96,
	APT_OP_BRANCHALWAYS = 0x99,
	APT_OP_GETURL2 = 0x9A,
	APT_OP_DEFINEFUNCTION = 0x9B,
	APT_OP_BRANCHIFTRUE = 0x9D,
	APT_OP_GOTOFRAME2 = 0x9F,
	APT_OP_EA_PUSHSTRING = 0xA1,
	APT_OP_EA_PUSHCONSTANTBYTE = 0xA2,
	APT_OP_EA_GETSTRINGVAR = 0xA4,
	APT_OP_EA_GETSTRINGMEMBER = 0xA5,
	APT_OP_EA_SETSTRINGVAR = 0xA6,
	APT_OP_EA_SETSTRINGMEMBER = 0xA7,
	APT_OP_EA_PUSHVALUEOFVAR = 0xAE,
	APT_OP_EA_GETNAMEDMEMBER = 0xAF,
	APT_OP_EA_CALLNAMEDFUNCPOP = 0xB0,
	APT_OP_EA_CALLNAMEDFUNC = 0xB1,
	APT_OP_EA_CALLNAMEDMETHODPOP = 0xB2,
	APT_OP_EA_CALLNAMEDMETHOD = 0xB3,
	APT_OP_EA_PUSHFLOAT = 0xB4,
	APT_OP_EA_PUSHBYTE = 0xB5,
	APT_OP_EA_PUSHSHORT = 0xB6,
	APT_OP_EA_PUSHLONG = 0xB7
};

// DefineFunction2 flag bits (OpenSAGE Function.cs; EA adds PreloadExtern).
enum AptFunction2Flags : std::uint32_t
{
	APT_FN2_PRELOAD_GLOBAL = 0x000001,
	APT_FN2_PRELOAD_THIS = 0x000100,
	APT_FN2_SUPPRESS_THIS = 0x000200,
	APT_FN2_PRELOAD_ARGUMENTS = 0x000400,
	APT_FN2_SUPPRESS_ARGUMENTS = 0x000800,
	APT_FN2_PRELOAD_SUPER = 0x001000,
	APT_FN2_SUPPRESS_SUPER = 0x002000,
	APT_FN2_PRELOAD_ROOT = 0x004000,
	APT_FN2_PRELOAD_PARENT = 0x008000,
	APT_FN2_PRELOAD_EXTERN = 0x010000
};

// One typed PushData / ConstantPool entry, resolved from the .const table at decode time.
struct AptConstRef
{
	std::uint32_t constIndex = 0; // index into the .const table
	std::uint32_t type = 0;       // AptConstType
	std::uint32_t raw = 0;        // register number / bool / float bits / int bits / lookup id
	std::string text;             // string entries
};

struct AptFunctionDef;

struct AptInstruction
{
	std::uint8_t opcode = 0;
	std::uint32_t offset = 0;     // file offset of the opcode byte
	std::uint32_t nextOffset = 0; // file offset just past the instruction (incl. a function body)
	std::int32_t intOperand = 0;  // byte/short/long/register/frame/branch/pool-index operand
	float floatOperand = 0;       // PushFloat
	std::string text;             // string operand (PushString, Get/SetString*, GotoLabel, GetURL url)
	std::string text2;            // GetURL target
	std::vector<AptConstRef> constants; // PushData / ConstantPool
	std::int32_t branchTarget = -1;     // instruction index of the target (== size() when it is the end)
	std::shared_ptr<const AptFunctionDef> function; // DefineFunction / DefineFunction2
};

struct AptCodeBlock
{
	std::uint32_t startOffset = 0;
	std::uint32_t endOffset = 0;
	std::uint32_t swfVersion = 0; // the owning movie's Apt version digit (6 or 7)
	std::vector<AptInstruction> instructions;

	// Instruction count including nested function bodies (what the corpus census counts).
	std::size_t flatInstructionCount() const;
	// Adds the opcode of every instruction, nested bodies included, to counts[256].
	void countOpcodes(std::uint32_t counts[256]) const;
};

struct AptFunctionParam
{
	std::int32_t reg = -1; // register that receives the argument, -1/0 = named local only
	std::string name;
};

struct AptFunctionDef
{
	std::string name;
	bool isV2 = false;           // DefineFunction2
	std::uint32_t registerCount = 0;
	std::uint32_t flags = 0;
	std::vector<AptFunctionParam> params;
	std::shared_ptr<const AptCodeBlock> body;
	std::uint32_t swfVersion = 0;
};

class AptActionDecoder
{
public:
	// Decode the program at `offset`: instructions up to and including the first End opcode.
	static bool decodeProgram(const AptFile &file, std::uint32_t offset, std::shared_ptr<const AptCodeBlock> &out, std::string *error);

	// The 86 opcode values the decoder accepts, ascending.
	static const std::vector<std::uint8_t> &supportedOpcodes();
	static bool isSupportedOpcode(std::uint8_t opcode);
	static bool isAlignedOpcode(std::uint8_t opcode);
	static const char *opcodeName(std::uint8_t opcode);
};
