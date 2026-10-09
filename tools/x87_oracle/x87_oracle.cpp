// OpenBFME. GPL-3.0.
//
// x87 hardware oracle for the numeric facade (engine/src/Common/NumericState.*).
//
// A 32-bit x86 helper: it sets the x87 control word to the retail game's state (precision control
// 24-bit, round-to-nearest, all exceptions masked: CW 0x007F, what RW setFPMode 0x440809 selects)
// and runs the exact instruction sequences retail uses, so the C++ emulation can be tested
// against real hardware. It must be built as 32-bit x86 (MSVC inline __asm); see build.bat.
//
// Protocol: one request per stdin line, one answer per stdout line, all numbers in hex.
//   f32 add|sub|mul|div <aBits> <bBits>
//        fld dword a ; f<op> dword b ; fstp dword r          -> r bits (8 hex digits)
//        This is the math evaluator's step (RW 0x42E28F-0x42E2B2: operate, then store as float).
//   f32i sub <aBits> <int32>
//        fld dword a ; fisub dword b ; fstp dword r             -> r bits (8 hex digits)
//        This is Apt Equals2's float-minus-integer step (BFME2 0x00B03664): the integer is not rounded.
//   f64 add|sub|mul|div|subr <aBits64> <bBits32>
//        fld qword a ; f<op> dword b ; fstp dword r             -> r bits (8 hex digits)
//        `subr` is fsubr: b - a.  These are the Apt handlers' operations on an operand that stays WIDE in ST0
//        (an exact integer or a double) against a float operand stored in memory.
//   dur <ms> <scaleBits>
//        parseDurationUnsignedInt (RW 0x73A429, sequence 0x73A440-0x73A458):
//        fild signed dword ms ; if (ms as int) < 0: fadd dword 2^32 ; fmul dword scale ;
//        fstp qword r                                           -> r bits (16 hex digits)
//   cw
//        prints the control word in effect                      -> 4 hex digits
// An unknown request prints "ERR".

#include <cstdint>
#include <cstdio>
#include <cstring>

static unsigned short g_cw = 0x007F;

static void setControlWord()
{
	unsigned short cw = g_cw;
	__asm { fldcw word ptr [cw] }
}

static float asFloat(std::uint32_t bits)
{
	float f;
	std::memcpy(&f, &bits, sizeof(f));
	return f;
}

static std::uint32_t asBits(float f)
{
	std::uint32_t b;
	std::memcpy(&b, &f, sizeof(b));
	return b;
}

static std::uint32_t op32(const char *op, std::uint32_t aBits, std::uint32_t bBits)
{
	float a = asFloat(aBits);
	float b = asFloat(bBits);
	float r = 0;
	if (!std::strcmp(op, "add"))
	{
		__asm
		{
			fld dword ptr [a]
			fadd dword ptr [b]
			fstp dword ptr [r]
		}
	}
	else if (!std::strcmp(op, "sub"))
	{
		__asm
		{
			fld dword ptr [a]
			fsub dword ptr [b]
			fstp dword ptr [r]
		}
	}
	else if (!std::strcmp(op, "mul"))
	{
		__asm
		{
			fld dword ptr [a]
			fmul dword ptr [b]
			fstp dword ptr [r]
		}
	}
	else
	{
		__asm
		{
			fld dword ptr [a]
			fdiv dword ptr [b]
			fstp dword ptr [r]
		}
	}
	return asBits(r);
}

static std::uint32_t opWide(const char *op, std::uint64_t aBits, std::uint32_t bBits)
{
	double a;
	std::memcpy(&a, &aBits, sizeof(a));
	float b = asFloat(bBits);
	float r = 0;
	if (!std::strcmp(op, "add"))
	{
		__asm
		{
			fld qword ptr [a]
			fadd dword ptr [b]
			fstp dword ptr [r]
		}
	}
	else if (!std::strcmp(op, "sub"))
	{
		__asm
		{
			fld qword ptr [a]
			fsub dword ptr [b]
			fstp dword ptr [r]
		}
	}
	else if (!std::strcmp(op, "subr"))
	{
		__asm
		{
			fld qword ptr [a]
			fsubr dword ptr [b]
			fstp dword ptr [r]
		}
	}
	else if (!std::strcmp(op, "mul"))
	{
		__asm
		{
			fld qword ptr [a]
			fmul dword ptr [b]
			fstp dword ptr [r]
		}
	}
	else
	{
		__asm
		{
			fld qword ptr [a]
			fdiv dword ptr [b]
			fstp dword ptr [r]
		}
	}
	return asBits(r);
}

static std::uint32_t subInt(std::uint32_t aBits, std::int32_t b)
{
	float a = asFloat(aBits);
	float r = 0;
	__asm
	{
		fld dword ptr [a]
		fisub dword ptr [b]
		fstp dword ptr [r]
	}
	return asBits(r);
}

static std::uint64_t duration(std::uint32_t ms, std::uint32_t scaleBits)
{
	float scale = asFloat(scaleBits);
	float two32 = 4294967296.0f;
	double r = 0;
	__asm
	{
		mov eax, dword ptr [ms]
		test eax, eax
		fild dword ptr [ms]
		jns skip
		fadd dword ptr [two32]
	skip:
		fmul dword ptr [scale]
		fstp qword ptr [r]
	}
	std::uint64_t bits;
	std::memcpy(&bits, &r, sizeof(bits));
	return bits;
}

int main()
{
	setControlWord();
	char line[256];
	while (std::fgets(line, sizeof(line), stdin))
	{
		char cmd[16] = {}, op[16] = {};
		unsigned a = 0, b = 0;
		unsigned long long a64 = 0;
		if (std::sscanf(line, "%15s %15s %x %x", cmd, op, &a, &b) == 4 && !std::strcmp(cmd, "f32"))
		{
			if (std::strcmp(op, "add") && std::strcmp(op, "sub") && std::strcmp(op, "mul") && std::strcmp(op, "div"))
			{
				std::puts("ERR");
				continue;
			}
			std::printf("%08x\n", op32(op, a, b));
		}
		else if (std::sscanf(line, "%15s %15s %llx %x", cmd, op, &a64, &b) == 4 && !std::strcmp(cmd, "f64"))
		{
			if (std::strcmp(op, "add") && std::strcmp(op, "sub") && std::strcmp(op, "subr") && std::strcmp(op, "mul") && std::strcmp(op, "div"))
			{
				std::puts("ERR");
				continue;
			}
			std::printf("%08x\n", opWide(op, a64, b));
		}
		else if (std::sscanf(line, "%15s %15s %x %x", cmd, op, &a, &b) == 4 && !std::strcmp(cmd, "f32i") && !std::strcmp(op, "sub"))
		{
			std::printf("%08x\n", subInt(a, (std::int32_t)b));
		}
		else if (std::sscanf(line, "%15s %x %x", cmd, &a, &b) == 3 && !std::strcmp(cmd, "dur"))
		{
			std::printf("%016llx\n", (unsigned long long)duration(a, b));
		}
		else if (std::sscanf(line, "%15s", cmd) == 1 && !std::strcmp(cmd, "cw"))
		{
			unsigned short cw = 0;
			__asm { fnstcw word ptr [cw] }
			std::printf("%04x\n", cw);
		}
		else
		{
			std::puts("ERR");
		}
		std::fflush(stdout);
	}
	return 0;
}
