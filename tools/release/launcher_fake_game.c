/* Lane LAUNCH-1: a stand-in for OpenBFME.exe in tools/release/launcher_windows_test.py: appends "played game <path>" to the file
   named by OPENBFME_TEST_PLAYED. Build: x86_64-w64-mingw32-clang -O2 -o fakegame.exe launcher_fake_game.c (or cl launcher_fake_game.c). */
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    const char *p = getenv("OPENBFME_TEST_PLAYED");
    FILE *f = p ? fopen(p, "a") : NULL;
    if (!f) return 1;
    fprintf(f, "played game %s\n", argv[0]);
    fclose(f);
    return 0;
}
