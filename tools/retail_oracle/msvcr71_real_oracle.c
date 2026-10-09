/* tools/retail_oracle/msvcr71_real_oracle.c (lane WIN-1): runs RotWK's own msvcr71.dll sscanf(text, "%f") and strtod on each line of stdin.
 *
 * Build (32-bit Windows; llvm-mingw from a Linux host):  i686-w64-mingw32-clang -O1 -o msvcr71_real_oracle.exe msvcr71_real_oracle.c
 * Run:  msvcr71_real_oracle.exe <path of the RotWK msvcr71.dll> <hex of the first 32 bytes of sscanf in that FILE>  < texts  > results
 * Under Wine force the native DLL: WINEDLLOVERRIDES=msvcr71=n. Wine ships its own msvcr71 (a different, correctly rounding implementation) and
 * loads it even for an explicit path; the byte check refuses to run on it (that substitution misled lane WIN-1 r1).
 * Output per line: <sscanf count> <float bits, hex: the sentinel 0xC640E400 when nothing was assigned> <strtod chars consumed> <double bits, hex>.
 * The game's setFPMode (RW 0x440809) is applied first, as the game runs it before INI::load.
 * engine/tests/data/win1/msvcr71_scanf_golden.txt was made with it (see the header of that file). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef int (__cdecl *sscanf_t)(const char *, const char *, ...);
typedef double (__cdecl *strtod_t)(const char *, char **);
typedef unsigned (__cdecl *controlfp_t)(unsigned, unsigned);
int main(int argc, char **argv)
{
	HMODULE m = LoadLibraryA(argc > 1 ? argv[1] : "msvcr71.dll");
	if (!m) { fprintf(stderr, "cannot load msvcr71\n"); return 2; }
	sscanf_t ss = (sscanf_t)GetProcAddress(m, "sscanf");
	strtod_t sd = (strtod_t)GetProcAddress(m, "strtod");
	controlfp_t cf = (controlfp_t)GetProcAddress(m, "_controlfp");
	if (!ss || !sd || !cf) return 3;
	/* argv[2]: the hex of the first 32 bytes of sscanf in the DLL FILE; refuse to run on another implementation (Wine's builtin msvcr71) */
	if (argc > 2)
	{
		for (int i = 0; i < 32; ++i)
		{
			unsigned v; sscanf(argv[2] + 2 * i, "%2x", &v);
			if (((unsigned char *)ss)[i] != (unsigned char)v) { fprintf(stderr, "the loaded sscanf is not the retail msvcr71.dll's (byte %d)\n", i); return 4; }
		}
	}
	/* RW setFPMode 0x440809: _fpreset; cw = _controlfp(0,0); _controlfp((cw & 0xFFFEFCFF) | 0x20000, 0x30300) */
	unsigned cw = cf(0, 0);
	cf((cw & 0xFFFEFCFFu) | 0x20000u, 0x30300u);
	char line[4096];
	while (fgets(line, sizeof line, stdin))
	{
		size_t n = strlen(line);
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
		float f = -12345.0f; uint32_t fb; uint64_t db; char *e;
		int c = ss(line, "%f", &f);
		double d = sd(line, &e);
		memcpy(&fb, &f, 4); memcpy(&db, &d, 8);
		printf("%d %08x %d %016llx\n", c, fb, (int)(e - line), (unsigned long long)db);
	}
	return 0;
}
