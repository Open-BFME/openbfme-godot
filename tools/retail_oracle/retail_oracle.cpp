// OpenBFME. GPL-3.0.
//
// retail_oracle: a 32-bit host that maps a retail game.dat into its own address space and calls
// chosen functions by address, on real hardware, so ports get external expected values instead of
// hand-read disassembly. See README.md for the protocol and for how to add a function.
//
// What it does NOT do: run the entry point, TLS callbacks or global constructors. Only the
// functions you name run, with the memory you prepared (alloc/poke), under the game's FPU state
// (x87 control word 0x007F = precision 24-bit, round to nearest, what RW setFPMode 0x440809 selects).
//
// Imports: kernel32 and msvcr71 (and any DLL named with `bind`) bind to the real DLLs, msvcr71
// from the folder that holds game.dat. Every other import binds to a per-import stub that raises
// a reported error naming the DLL and function when called ("fail loudly").
//
// Build: build.bat (MSVC x86). Linked at a fixed high base so the game's preferred base 0x400000 is free.

#pragma warning(disable : 4731) // doCall restores ebp with popad on purpose
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>

typedef std::uint8_t u8;
typedef std::uint16_t u16;
typedef std::uint32_t u32;

static const DWORD kStubException = 0xE0524F31; // raised by an unbound-import stub; info[0] = stub index
static const u32 kReserveBase = 0x00400000;
static const u32 kReserveSize = 0x00C00000; // 0x400000..0x1000000 covers both games (SizeOfImage 0xADA000)

// ---------------------------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------------------------
struct Section
{
	char name[9];
	u32 rva, vsize, rsize, flags;
};

struct Image
{
	std::string tag, path;
	u32 base = 0, size = 0, entry = 0;
	std::vector<Section> sections;
	std::vector<std::string> realBound; // "dll!name" bound to a real DLL
	std::vector<std::string> stubbed;   // "dll!name" bound to a failing stub
};

static std::vector<Image *> g_images;
static std::vector<std::string> g_stubNames; // index -> "tag:dll!name"
static std::set<std::string> g_realDlls = {"kernel32.dll", "msvcr71.dll", "msvcp71.dll"};
static void *g_reservation = nullptr;

static std::string lower(std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
	return s;
}

static std::string dirOf(const std::string &p)
{
	size_t i = p.find_last_of("\\/");
	return i == std::string::npos ? std::string(".") : p.substr(0, i);
}

static std::string hex8(u32 v)
{
	char b[16];
	std::snprintf(b, sizeof(b), "%08x", v);
	return b;
}

// ---------------------------------------------------------------------------------------------
// Stubs for unbound imports: push idx ; call stubHit.  stubHit never returns.
// ---------------------------------------------------------------------------------------------
static void __cdecl stubHit(u32 idx)
{
	// Stack here: [ret into thunk][idx][return address into the game's caller].
	ULONG_PTR info[2] = {idx, (ULONG_PTR)(&idx)[1]};
	RaiseException(kStubException, EXCEPTION_NONCONTINUABLE, 2, info);
}

static u8 *g_thunks = nullptr;
static size_t g_thunkUsed = 0, g_thunkCap = 0;

static u32 makeStub(const std::string &name)
{
	if (!g_thunks || g_thunkUsed + 16 > g_thunkCap)
	{
		g_thunkCap = 16 * 4096;
		g_thunks = (u8 *)VirtualAlloc(nullptr, g_thunkCap, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		g_thunkUsed = 0;
		if (!g_thunks)
			return 0;
	}
	u8 *t = g_thunks + g_thunkUsed;
	g_thunkUsed += 16;
	u32 idx = (u32)g_stubNames.size();
	g_stubNames.push_back(name);
	t[0] = 0x68; // push imm32
	std::memcpy(t + 1, &idx, 4);
	t[5] = 0xE8; // call rel32
	u32 rel = (u32)((u8 *)&stubHit - (t + 10));
	std::memcpy(t + 6, &rel, 4);
	return (u32)(size_t)t;
}

// ---------------------------------------------------------------------------------------------
// Mapping
// ---------------------------------------------------------------------------------------------
static std::string fail(const std::string &m) { return "err " + m; }

static DWORD protFor(u32 f)
{
	bool x = (f & 0x20000000) != 0, r = (f & 0x40000000) != 0, w = (f & 0x80000000) != 0;
	if (x)
		return w ? PAGE_EXECUTE_READWRITE : (r ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
	if (w)
		return PAGE_READWRITE;
	return r ? PAGE_READONLY : PAGE_NOACCESS;
}

static bool readFile(const std::string &path, std::vector<u8> &out)
{
	HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER sz;
	GetFileSizeEx(h, &sz);
	out.resize((size_t)sz.QuadPart);
	size_t got = 0;
	while (got < out.size())
	{
		DWORD n = 0;
		if (!ReadFile(h, out.data() + got, (DWORD)std::min<size_t>(out.size() - got, 1u << 24), &n, nullptr) || n == 0)
			break;
		got += n;
	}
	CloseHandle(h);
	return got == out.size();
}

template <class T>
static const T *at(const std::vector<u8> &f, size_t off)
{
	return off + sizeof(T) <= f.size() ? (const T *)(f.data() + off) : nullptr;
}

static std::string loadImage(const std::string &tag, const std::string &path)
{
	for (Image *i : g_images)
		if (i->tag == tag)
			return fail("tag already loaded: " + tag);
	std::vector<u8> file;
	if (!readFile(path, file))
		return fail("cannot read " + path);
	const IMAGE_DOS_HEADER *dos = at<IMAGE_DOS_HEADER>(file, 0);
	if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
		return fail("not a PE file (no MZ)");
	const IMAGE_NT_HEADERS32 *nt = at<IMAGE_NT_HEADERS32>(file, (size_t)dos->e_lfanew);
	if (!nt || nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
		return fail("not a 32-bit PE image");
	const IMAGE_DATA_DIRECTORY &rel = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
	(void)rel; // game.dat has none; the image is mapped at its preferred base and never relocated

	Image *im = new Image;
	im->tag = tag;
	im->path = path;
	im->base = nt->OptionalHeader.ImageBase;
	im->size = nt->OptionalHeader.SizeOfImage;
	im->entry = im->base + nt->OptionalHeader.AddressOfEntryPoint;
	for (Image *o : g_images)
		if (im->base < o->base + o->size && o->base < im->base + im->size)
		{
			delete im;
			return fail("image range overlaps already-loaded image '" + o->tag + "' (one image per process; start another helper)");
		}

	if (g_reservation && im->base >= kReserveBase && im->base + im->size <= kReserveBase + kReserveSize)
	{
		VirtualFree(g_reservation, 0, MEM_RELEASE);
		g_reservation = nullptr;
	}
	u8 *mem = (u8 *)VirtualAlloc((void *)(size_t)im->base, im->size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (mem != (u8 *)(size_t)im->base)
	{
		MEMORY_BASIC_INFORMATION mbi = {};
		std::string why = "VirtualAlloc at preferred base failed";
		for (size_t a = im->base; a < (size_t)im->base + im->size;)
		{
			if (!VirtualQuery((void *)a, &mbi, sizeof(mbi)))
				break;
			if (mbi.State != MEM_FREE)
			{
				char b[96];
				std::snprintf(b, sizeof(b), ": range already used at %p size %p (restart the helper)", mbi.BaseAddress, (void *)mbi.RegionSize);
				why += b;
				break;
			}
			a = (size_t)mbi.BaseAddress + mbi.RegionSize;
		}
		delete im;
		return fail(why);
	}

	size_t hdr = nt->OptionalHeader.SizeOfHeaders;
	if (hdr > file.size() || hdr > im->size)
	{
		delete im;
		return fail("bad SizeOfHeaders");
	}
	std::memcpy(mem, file.data(), hdr);
	const IMAGE_SECTION_HEADER *sh = IMAGE_FIRST_SECTION(nt);
	for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
	{
		Section s = {};
		std::memcpy(s.name, sh[i].Name, 8);
		for (int k = 0; k < 8; k++)
			if (s.name[k] && (s.name[k] <= 32 || s.name[k] > 126))
				s.name[k] = '_'; // keep `info` tokens whitespace-free
		s.rva = sh[i].VirtualAddress;
		s.vsize = sh[i].Misc.VirtualSize;
		s.rsize = sh[i].SizeOfRawData;
		s.flags = sh[i].Characteristics;
		u32 span = s.vsize > s.rsize ? s.vsize : s.rsize;
		if ((size_t)s.rva + span > im->size)
		{
			delete im;
			return fail(std::string("section ") + s.name + " exceeds SizeOfImage");
		}
		u32 copy = s.rsize < s.vsize ? s.rsize : s.vsize;
		if (copy)
		{
			if ((size_t)sh[i].PointerToRawData + copy > file.size())
			{
				delete im;
				return fail(std::string("section ") + s.name + " raw data beyond end of file");
			}
			std::memcpy(mem + s.rva, file.data() + sh[i].PointerToRawData, copy);
		}
		im->sections.push_back(s);
	}

	// Imports.
	const IMAGE_DATA_DIRECTORY &impd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	std::string gamedir = dirOf(path);
	if (impd.VirtualAddress)
	{
		const IMAGE_IMPORT_DESCRIPTOR *d = (const IMAGE_IMPORT_DESCRIPTOR *)(mem + impd.VirtualAddress);
		for (; d->Name; d++)
		{
			std::string dll = (const char *)(mem + d->Name);
			std::string key = lower(dll);
			HMODULE mod = nullptr;
			std::string modErr;
			if (g_realDlls.count(key))
			{
				std::string full = gamedir + "\\" + dll;
				if (GetFileAttributesA(full.c_str()) != INVALID_FILE_ATTRIBUTES)
					mod = LoadLibraryExA(full.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
				else if (key == "kernel32.dll")
					mod = LoadLibraryA(dll.c_str());
				if (!mod)
				{
					delete im;
					return fail("real-bound DLL not loadable: " + dll + " (looked next to game.dat, then the system for kernel32)");
				}
			}
			const u32 *oft = (const u32 *)(mem + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
			u32 *iat = (u32 *)(mem + d->FirstThunk);
			for (; *oft; oft++, iat++)
			{
				std::string fn;
				LPCSTR proc;
				if (*oft & 0x80000000)
				{
					proc = (LPCSTR)(size_t)(*oft & 0xFFFF);
					char b[16];
					std::snprintf(b, sizeof(b), "#%u", *oft & 0xFFFF);
					fn = b;
				}
				else
				{
					proc = (LPCSTR)(mem + *oft + 2);
					fn = proc;
				}
				FARPROC p = mod ? GetProcAddress(mod, proc) : nullptr;
				if (p)
				{
					*iat = (u32)(size_t)p;
					im->realBound.push_back(dll + "!" + fn);
				}
				else
				{
					u32 t = makeStub(tag + ":" + dll + "!" + fn);
					if (!t)
					{
						delete im;
						return fail("cannot allocate import stubs");
					}
					*iat = t;
					im->stubbed.push_back(dll + "!" + fn);
				}
			}
		}
	}

	for (const Section &s : im->sections)
	{
		u32 span = s.vsize > s.rsize ? s.vsize : s.rsize;
		DWORD old;
		VirtualProtect(mem + s.rva, span, protFor(s.flags), &old);
	}
	g_images.push_back(im);
	char b[200];
	std::snprintf(b, sizeof(b), "ok base=%08x size=%08x entry=%08x sections=%u real_imports=%u stub_imports=%u", im->base, im->size, im->entry,
				  (unsigned)im->sections.size(), (unsigned)im->realBound.size(), (unsigned)im->stubbed.size());
	return b;
}

// ---------------------------------------------------------------------------------------------
// Calling
// ---------------------------------------------------------------------------------------------
struct CallRegs
{
	u32 r_eax, r_ecx, r_edx, r_ebx, r_esi, r_edi, r_ebp, r_esp;
};

static u32 g_target, g_nargs, g_args[64], g_savedEsp, g_recoverEip;
static u32 g_cw = 0x007F;    // x87 control word: PC24, round to nearest, all exceptions masked
static u32 g_mxcsr = 0x1F80; // SSE control: round to nearest, all exceptions masked
static CallRegs g_in, g_out;
static u16 g_outCw;
static u32 g_outMxcsr;
static u8 g_outXmm0[16];
static u8 g_outEnv[28];
static u8 g_outSt0[10];

static volatile LONG g_inCall = 0;
static volatile LONG g_faulted = 0;
static EXCEPTION_RECORD g_exc;
static CONTEXT g_ctx;

static bool hardFault(DWORD c)
{
	switch (c)
	{
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_INT_OVERFLOW:
	case EXCEPTION_BREAKPOINT:
	case EXCEPTION_STACK_OVERFLOW:
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
	case EXCEPTION_FLT_INVALID_OPERATION:
	case EXCEPTION_FLT_OVERFLOW:
	case kStubException:
		return true;
	}
	return false;
}

// The vectored handler sees faults before any SEH frame, including the game's own registered
// frames (whose handlers live outside any module and would be refused by the dispatcher). A
// reported fault diverts execution to the recovery label inside doCall.
static LONG CALLBACK vectoredHandler(EXCEPTION_POINTERS *ep)
{
	if (!g_inCall || !hardFault(ep->ExceptionRecord->ExceptionCode))
		return EXCEPTION_CONTINUE_SEARCH;
	g_exc = *ep->ExceptionRecord;
	g_ctx = *ep->ContextRecord;
	g_faulted = 1;
	ep->ContextRecord->Eip = g_recoverEip;
	return EXCEPTION_CONTINUE_EXECUTION;
}

static void doCall()
{
	__asm
	{
		pushfd
		pushad
		mov g_savedEsp, esp
		mov g_recoverEip, offset Recover
		fninit
		fldcw word ptr [g_cw]
		ldmxcsr dword ptr [g_mxcsr]
		mov ecx, g_nargs
	PushLoop:
		test ecx, ecx
		jz PushDone
		push dword ptr g_args[ecx*4-4]
		dec ecx
		jmp PushLoop
	PushDone:
		mov eax, g_in.r_eax
		mov ecx, g_in.r_ecx
		mov edx, g_in.r_edx
		mov ebx, g_in.r_ebx
		mov esi, g_in.r_esi
		mov edi, g_in.r_edi
		mov ebp, g_in.r_ebp
		call dword ptr [g_target]
		mov g_out.r_eax, eax
		mov g_out.r_ecx, ecx
		mov g_out.r_edx, edx
		mov g_out.r_ebx, ebx
		mov g_out.r_esi, esi
		mov g_out.r_edi, edi
		mov g_out.r_ebp, ebp
		mov g_out.r_esp, esp
		fnstcw word ptr [g_outCw]
		stmxcsr dword ptr [g_outMxcsr]
		movups xmmword ptr [g_outXmm0], xmm0
		fnstenv [g_outEnv]
	Recover:
		mov esp, g_savedEsp
		popad
		popfd
	}
}

static void captureSt0(int &depth, bool &have)
{
	u16 sw = *(u16 *)(g_outEnv + 4), tw = *(u16 *)(g_outEnv + 8);
	unsigned top = (sw >> 11) & 7;
	depth = 0;
	for (int i = 0; i < 8; i++)
		if (((tw >> (2 * i)) & 3) != 3)
			depth++;
	have = ((tw >> (2 * top)) & 3) != 3;
	if (have)
		__asm fstp tbyte ptr [g_outSt0]
	__asm fninit
}

static std::string toHex(const u8 *p, size_t n)
{
	static const char *d = "0123456789abcdef";
	std::string s;
	for (size_t i = 0; i < n; i++)
	{
		s += d[p[i] >> 4];
		s += d[p[i] & 15];
	}
	return s;
}

static std::string describeFault()
{
	char b[512];
	DWORD c = g_exc.ExceptionCode;
	if (c == kStubException)
	{
		u32 idx = (u32)g_exc.ExceptionInformation[0];
		std::string n = idx < g_stubNames.size() ? g_stubNames[idx] : "?";
		std::snprintf(b, sizeof(b), "err stub import called: %s return_to=%08x", n.c_str(), (u32)g_exc.ExceptionInformation[1]);
		return b;
	}
	const char *kind = "";
	char extra[96] = "";
	if (c == EXCEPTION_ACCESS_VIOLATION || c == EXCEPTION_IN_PAGE_ERROR)
	{
		ULONG_PTR k = g_exc.ExceptionInformation[0];
		kind = k == 0 ? " access=read" : (k == 1 ? " access=write" : " access=exec");
		std::snprintf(extra, sizeof(extra), " addr=%08x", (u32)g_exc.ExceptionInformation[1]);
	}
	std::snprintf(b, sizeof(b), "err fault code=%08x eip=%08x%s%s eax=%08x ecx=%08x edx=%08x ebx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x", (unsigned)c,
				  (u32)(size_t)g_exc.ExceptionAddress, kind, extra, g_ctx.Eax, g_ctx.Ecx, g_ctx.Edx, g_ctx.Ebx, g_ctx.Esi, g_ctx.Edi, g_ctx.Ebp, g_ctx.Esp);
	return b;
}

static std::string runCall(u32 target, const std::string &conv, std::vector<u32> args, CallRegs in, bool *ok)
{
	*ok = false;
	if (args.size() > 60)
		return fail("too many arguments (max 60 dwords)");
	std::memset(&g_in, 0, sizeof(g_in));
	g_in = in;
	size_t first = 0;
	if (conv == "thiscall" || conv == "fastcall")
	{
		if (args.empty())
			return fail(conv + " needs at least one argument (ecx)");
		g_in.r_ecx = args[0];
		first = 1;
		if (conv == "fastcall")
		{
			if (args.size() > 1)
			{
				g_in.r_edx = args[1];
				first = 2;
			}
		}
	}
	else if (conv != "cdecl" && conv != "stdcall" && conv != "regs")
		return fail("unknown calling convention (cdecl|stdcall|thiscall|fastcall|regs): " + conv);
	g_nargs = (u32)(args.size() - first);
	for (u32 i = 0; i < g_nargs; i++)
		g_args[i] = args[first + i];
	g_target = target;
	std::memset(&g_out, 0, sizeof(g_out));
	std::memset(g_outSt0, 0, sizeof(g_outSt0));
	std::memset(g_outEnv, 0, sizeof(g_outEnv));
	g_faulted = 0;
	g_inCall = 1;
	doCall();
	g_inCall = 0;
	if (g_faulted)
	{
		__asm fninit
		return describeFault();
	}
	int depth;
	bool have;
	captureSt0(depth, have);
	// Net bytes the callee popped beyond the return address: 0 for cdecl, 4*n for stdcall/thiscall.
	u32 expectedEsp = g_savedEsp - 4 * g_nargs;
	char b[600];
	std::snprintf(b, sizeof(b), "ok eax=%08x ecx=%08x edx=%08x ebx=%08x esi=%08x edi=%08x ebp=%08x callee_pop=%x cw=%04x mxcsr=%08x fpdepth=%d st0=%s xmm0=%s", g_out.r_eax,
						  g_out.r_ecx, g_out.r_edx, g_out.r_ebx, g_out.r_esi, g_out.r_edi, g_out.r_ebp, g_out.r_esp - expectedEsp, g_outCw, g_outMxcsr, depth,
						  have ? toHex(g_outSt0, 10).c_str() : "-", toHex(g_outXmm0, 16).c_str());
	*ok = true;
	return b;
}

// ---------------------------------------------------------------------------------------------
// Memory helpers (SEH guarded)
// ---------------------------------------------------------------------------------------------
static bool guardedRead(u32 addr, u8 *dst, size_t n)
{
	__try
	{
		std::memcpy(dst, (const void *)(size_t)addr, n);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

static bool guardedWrite(u32 addr, const u8 *src, size_t n)
{
	__try
	{
		DWORD old = 0;
		bool reprotect = VirtualProtect((void *)(size_t)addr, n, PAGE_EXECUTE_READWRITE, &old) != 0;
		std::memcpy((void *)(size_t)addr, src, n);
		if (reprotect)
		{
			DWORD tmp;
			VirtualProtect((void *)(size_t)addr, n, old, &tmp);
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

// ---------------------------------------------------------------------------------------------
// Protocol
// ---------------------------------------------------------------------------------------------
static std::vector<std::string> split(const std::string &s)
{
	std::vector<std::string> t;
	size_t i = 0;
	while (i < s.size())
	{
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
			i++;
		size_t j = i;
		while (j < s.size() && !(s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n'))
			j++;
		if (j > i)
			t.push_back(s.substr(i, j - i));
		i = j;
	}
	return t;
}

static bool parseHex(const std::string &s0, unsigned long long &v)
{
	std::string s = s0;
	if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s = s.substr(2);
	if (s.empty() || s.size() > 16)
		return false;
	v = 0;
	for (char c : s)
	{
		int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
		if (d < 0)
			return false;
		v = (v << 4) | (unsigned)d;
	}
	return true;
}

static bool parseU32(const std::string &s, u32 &out)
{
	unsigned long long v;
	if (!parseHex(s, v) || v > 0xFFFFFFFFull)
		return false;
	out = (u32)v;
	return true;
}

static void *__cdecl hostAlloc(u32 size, u32, u32) { return std::malloc(size ? size : 1); }
static void __cdecl hostFree(void *p, u32) { std::free(p); }

static const char *kHelp =
	"commands: load <tag> <path> | bind <dll> | call <tag> <va> <conv> [args] | poke <addr> <hex> | pokes <addr> <text> | peek <addr> <len> | "
	"alloc <size> [addr] | hostfn alloc|free | sym <dll> <fn> | query <addr> [end] | setcw <hex> | setmxcsr <hex> | info <tag> | imports <tag> | quit. Numbers are hex. "
	"call args: <hex32> | d:<hex64> (two dwords) | r:<eax|ecx|edx|ebx|esi|edi|ebp>=<hex>";

static std::string handle(const std::string &line, bool &quit)
{
	std::vector<std::string> t = split(line);
	if (t.empty())
		return "";
	const std::string &cmd = t[0];
	if (cmd == "quit")
	{
		quit = true;
		return "ok bye";
	}
	if (cmd == "help")
		return std::string("ok ") + kHelp;
	if (cmd == "bind")
	{
		if (t.size() != 2)
			return fail("usage: bind <dll>");
		if (!g_images.empty())
			return fail("bind must come before load");
		g_realDlls.insert(lower(t[1]));
		return "ok";
	}
	if (cmd == "load")
	{
		if (t.size() < 3)
			return fail("usage: load <tag> <path>");
		size_t p = line.find(t[2]);
		std::string path = line.substr(p);
		while (!path.empty() && (path.back() == '\r' || path.back() == '\n' || path.back() == ' '))
			path.pop_back();
		return loadImage(t[1], path);
	}
	if (cmd == "info" || cmd == "imports")
	{
		if (t.size() != 2)
			return fail("usage: " + cmd + " <tag>");
		for (Image *i : g_images)
			if (i->tag == t[1])
			{
				std::string s = "ok";
				if (cmd == "info")
				{
					for (const Section &sec : i->sections)
					{
						char b[96];
						std::snprintf(b, sizeof(b), " %s@%08x+%x", sec.name, i->base + sec.rva, sec.vsize);
						s += b;
					}
				}
				else
				{
					for (const std::string &n : i->stubbed)
						s += " stub:" + n;
					for (const std::string &n : i->realBound)
						s += " real:" + n;
				}
				return s;
			}
		return fail("no such image: " + t[1]);
	}
	if (cmd == "setcw" || cmd == "setmxcsr")
	{
		u32 v;
		if (t.size() != 2 || !parseU32(t[1], v))
			return fail("usage: " + cmd + " <hex>");
		(cmd == "setcw" ? g_cw : g_mxcsr) = v;
		return "ok";
	}
	if (cmd == "query")
	{
		// Debug aid: list memory regions from <addr> up to <end> (at most 16).
		u32 a = 0, hi = 0xFFFFFFFF;
		if (t.size() < 2 || !parseU32(t[1], a) || (t.size() > 2 && !parseU32(t[2], hi)))
			return fail("usage: query <addr> [end]");
		std::string s = "ok";
		for (int n = 0; n < 16 && a < hi; n++)
		{
			MEMORY_BASIC_INFORMATION mbi;
			if (!VirtualQuery((void *)(size_t)a, &mbi, sizeof(mbi)))
				break;
			char b[128];
			std::snprintf(b, sizeof(b), " [%p+%p state=%lx type=%lx prot=%lx]", mbi.BaseAddress, (void *)mbi.RegionSize, mbi.State, mbi.Type, mbi.Protect);
			s += b;
			a = (u32)((size_t)mbi.BaseAddress + mbi.RegionSize);
		}
		return s;
	}
	if (cmd == "hostfn")
	{
		// Addresses of host-provided cdecl helpers a caller can poke into a retail function-pointer
		// global (for example the game's allocator hooks): alloc(size, a, b) -> ptr, free(ptr, a).
		if (t.size() != 2)
			return fail("usage: hostfn alloc|free");
		if (t[1] == "alloc")
			return "ok " + hex8((u32)(size_t)&hostAlloc);
		if (t[1] == "free")
			return "ok " + hex8((u32)(size_t)&hostFree);
		return fail("unknown host function: " + t[1]);
	}
	if (cmd == "sym")
	{
		// Address of an export of a DLL already loaded into the process (after `load`).
		if (t.size() != 3)
			return fail("usage: sym <dll> <function>");
		HMODULE m = GetModuleHandleA(t[1].c_str());
		FARPROC p = m ? GetProcAddress(m, t[2].c_str()) : nullptr;
		if (!p)
			return fail("not found: " + t[1] + "!" + t[2]);
		return "ok " + hex8((u32)(size_t)p);
	}
	if (cmd == "alloc")
	{
		u32 size, addr = 0;
		if (t.size() < 2 || !parseU32(t[1], size) || size == 0 || (t.size() > 2 && !parseU32(t[2], addr)))
			return fail("usage: alloc <size> [addr]");
		void *p = VirtualAlloc((void *)(size_t)addr, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		if (!p)
			return fail("VirtualAlloc failed");
		return "ok " + hex8((u32)(size_t)p);
	}
	if (cmd == "peek")
	{
		u32 addr, n;
		if (t.size() != 3 || !parseU32(t[1], addr) || !parseU32(t[2], n) || n > (1u << 24))
			return fail("usage: peek <addr> <len>");
		std::vector<u8> buf(n);
		if (!guardedRead(addr, buf.data(), n))
			return fail("read fault in " + t[1] + "+" + t[2]);
		return "ok " + toHex(buf.data(), n);
	}
	if (cmd == "poke")
	{
		u32 addr;
		if (t.size() < 3 || !parseU32(t[1], addr))
			return fail("usage: poke <addr> <hexbytes>");
		std::string h;
		for (size_t i = 2; i < t.size(); i++)
			h += t[i];
		if (h.size() % 2)
			return fail("odd number of hex digits");
		std::vector<u8> buf(h.size() / 2);
		for (size_t i = 0; i < buf.size(); i++)
		{
			unsigned long long v;
			if (!parseHex(h.substr(2 * i, 2), v))
				return fail("bad hex byte");
			buf[i] = (u8)v;
		}
		if (!guardedWrite(addr, buf.data(), buf.size()))
			return fail("write fault in " + t[1]);
		return "ok";
	}
	if (cmd == "pokes")
	{
		u32 addr;
		if (t.size() < 3 || !parseU32(t[1], addr))
			return fail("usage: pokes <addr> <text>");
		size_t p = line.find(t[1]) + t[1].size();
		while (p < line.size() && line[p] == ' ')
			p++;
		std::string text = line.substr(p);
		while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
			text.pop_back();
		if (!guardedWrite(addr, (const u8 *)text.c_str(), text.size() + 1))
			return fail("write fault in " + t[1]);
		return "ok";
	}
	if (cmd == "call")
	{
		if (t.size() < 4)
			return fail("usage: call <tag> <va> <conv> [args]");
		bool known = false;
		for (Image *i : g_images)
			known |= i->tag == t[1];
		if (!known)
			return fail("no such image: " + t[1]);
		u32 va;
		if (!parseU32(t[2], va))
			return fail("bad address");
		std::vector<u32> args;
		CallRegs in = {};
		for (size_t i = 4; i < t.size(); i++)
		{
			const std::string &a = t[i];
			if (a.compare(0, 2, "d:") == 0)
			{
				unsigned long long v;
				if (!parseHex(a.substr(2), v))
					return fail("bad d: argument " + a);
				args.push_back((u32)v);
				args.push_back((u32)(v >> 32));
			}
			else if (a.compare(0, 2, "r:") == 0)
			{
				size_t eq = a.find('=');
				u32 v;
				if (eq == std::string::npos || !parseU32(a.substr(eq + 1), v))
					return fail("bad r: argument " + a);
				std::string r = a.substr(2, eq - 2);
				u32 *slot = r == "eax" ? &in.r_eax : r == "ecx" ? &in.r_ecx : r == "edx" ? &in.r_edx : r == "ebx" ? &in.r_ebx : r == "esi" ? &in.r_esi : r == "edi" ? &in.r_edi : r == "ebp" ? &in.r_ebp : nullptr;
				if (!slot)
					return fail("bad register in " + a);
				*slot = v;
			}
			else
			{
				u32 v;
				if (!parseU32(a, v))
					return fail("bad argument " + a);
				args.push_back(v);
			}
		}
		bool ok;
		return runCall(va, t[3], args, in, &ok);
	}
	return fail("unknown command: " + cmd);
}

int main()
{
	// The loader maps system data at 0x400000 and nearby shortly after process start (the range is
	// not free by the time main runs). So the first process is a supervisor: it creates this same
	// exe suspended, reserves the game's range inside it with VirtualAllocEx before the loader has
	// run, then resumes it. The child inherits stdin/stdout and finds the reservation in place.
	char marker[8];
	if (!GetEnvironmentVariableA("RETAIL_ORACLE_CHILD", marker, sizeof(marker)))
	{
		SetEnvironmentVariableA("RETAIL_ORACLE_CHILD", "1");
		STARTUPINFOA si = {sizeof(si)};
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
		PROCESS_INFORMATION pi = {};
		char exe[MAX_PATH], cmdline[MAX_PATH + 4];
		GetModuleFileNameA(nullptr, exe, MAX_PATH);
		std::snprintf(cmdline, sizeof(cmdline), "\"%s\"", exe);
		if (!CreateProcessA(nullptr, cmdline, nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi))
		{
			std::fprintf(stderr, "retail_oracle: cannot create the worker process (error %lu)\n", GetLastError());
			return 2;
		}
		if (!VirtualAllocEx(pi.hProcess, (void *)(size_t)kReserveBase, kReserveSize, MEM_RESERVE, PAGE_NOACCESS))
			std::fprintf(stderr, "retail_oracle: cannot reserve the game range in the worker (error %lu); `load` will report what holds it\n", GetLastError());
		// If this supervisor is killed (a caller's watchdog), the worker must not outlive it.
		HANDLE job = CreateJobObjectA(nullptr, nullptr);
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim = {};
		lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof(lim)) || !AssignProcessToJobObject(job, pi.hProcess))
			std::fprintf(stderr, "retail_oracle: warning: worker is not tied to a job object (error %lu)\n", GetLastError());
		ResumeThread(pi.hThread);
		WaitForSingleObject(pi.hProcess, INFINITE);
		DWORD code = 1;
		GetExitCodeProcess(pi.hProcess, &code);
		return (int)code;
	}
	MEMORY_BASIC_INFORMATION rsv;
	if (VirtualQuery((void *)(size_t)kReserveBase, &rsv, sizeof(rsv)) && rsv.State == MEM_RESERVE && rsv.AllocationBase == (void *)(size_t)kReserveBase)
		g_reservation = rsv.AllocationBase;
	AddVectoredExceptionHandler(1, vectoredHandler);
	std::string line;
	bool quit = false;
	while (!quit && std::getline(std::cin, line))
	{
		std::string out = handle(line, quit);
		if (out.empty())
			continue;
		std::fputs(out.c_str(), stdout);
		std::fputc('\n', stdout);
		std::fflush(stdout);
	}
	return 0;
}
