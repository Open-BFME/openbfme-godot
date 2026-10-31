#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

#ifndef OPENBFME_TEST_MODE
#define OPENBFME_TEST_MODE 0
#endif

static const char *env_or_empty(const char *name) {
    const char *value = getenv(name);
    return value ? value : "";
}

static const char *version_from_path(const char *path) {
    static char version[256];
    const char *p = strstr(path, "\\versions\\");
    if (!p) p = strstr(path, "/versions/");
    if (!p) {
        char file[1024];
        size_t n = strlen(path);
        if (n >= sizeof(file)) n = sizeof(file) - 1;
        memcpy(file, path, n);
        file[n] = '\0';
        char *slash = strrchr(file, '\\');
        char *forward = strrchr(file, '/');
        if (forward && (!slash || forward > slash)) slash = forward;
        if (slash) {
            slash[1] = '\0';
            strncat(file, "VERSION", sizeof(file) - strlen(file) - 1);
            FILE *input = fopen(file, "r");
            if (input && fscanf(input, "OpenBFME %255s", version) == 1) {
                fclose(input);
                return version;
            }
            if (input) fclose(input);
        }
        strcpy(version, "unknown");
        return version;
    }
    p += 10;
    size_t len = 0;
    while (p[len] && p[len] != '\\' && p[len] != '/') ++len;
    if (len >= sizeof(version)) len = sizeof(version) - 1;
    memcpy(version, p, len);
    version[len] = '\0';
    return version;
}

#ifdef _WIN32
static void detach_stdio(void) {
    fflush(stdout);
    fflush(stderr);
    if (_fileno(stdin) >= 0) _close(_fileno(stdin));
    if (_fileno(stdout) >= 0) _close(_fileno(stdout));
    if (_fileno(stderr) >= 0) _close(_fileno(stderr));
    SetStdHandle(STD_INPUT_HANDLE, INVALID_HANDLE_VALUE);
    SetStdHandle(STD_OUTPUT_HANDLE, INVALID_HANDLE_VALUE);
    SetStdHandle(STD_ERROR_HANDLE, INVALID_HANDLE_VALUE);
}

static int wait_for_exit(void) {
    char name[128];
    snprintf(name, sizeof(name), "Local\\OpenBFME-test-game-%lu", (unsigned long)GetCurrentProcessId());
    HANDLE event = CreateEventA(NULL, TRUE, FALSE, name);
    if (!event) return 1;
    detach_stdio();
    DWORD result = WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
    return result == WAIT_OBJECT_0 ? 0 : 1;
}
#endif

static void write_args(FILE *out, int argc, char **argv) {
    for (int i = 1; i < argc; ++i) fprintf(out, "%s\n", argv[i]);
}

static const char *game_directory(const char *path) {
    static char directory[1024];
    size_t n = strlen(path);
    while (n && path[n - 1] != '\\' && path[n - 1] != '/') --n;
    if (n) --n;
    if (n >= sizeof(directory)) n = sizeof(directory) - 1;
    memcpy(directory, path, n);
    directory[n] = '\0';
    for (size_t i = 0; i < n; ++i) if (directory[i] == '\\') directory[i] = '/';
    return directory;
}

int main(int argc, char **argv) {
    const char *played = env_or_empty("OPENBFME_TEST_PLAYED");
    const char *marker = env_or_empty("OPENBFME_TEST_MARKER");
    const char *args_file = env_or_empty("OPENBFME_TEST_ARGS");
    const char *sleep_game = env_or_empty("OPENBFME_TEST_SLEEP");
    FILE *out = played[0] ? fopen(played, "a") : NULL;
    if (OPENBFME_TEST_MODE == 2) {
#ifdef _WIN32
        if (out) fclose(out);
        return wait_for_exit();
#else
        return 0;
#endif
        return 0;
    }
    if (OPENBFME_TEST_MODE == 4) {
        FILE *m = marker[0] ? fopen(marker, "a") : NULL;
        if (m) fclose(m);
        return m ? 0 : 1;
    }
    if (OPENBFME_TEST_MODE == 3) {
        FILE *a = args_file[0] ? fopen(args_file, "w") : NULL;
        if (!a) return 1;
        write_args(a, argc, argv);
        fclose(a);
        return 0;
    }
    if (!out) return 1;
    const char *version = env_or_empty("OPENBFME_TEST_VERSION");
    if (!version[0]) version = version_from_path(argv[0]);
    if (OPENBFME_TEST_MODE == 1) {
        fprintf(out, "played %s from %s args:", version, game_directory(argv[0]));
    } else {
        fprintf(out, "played game %s args:", version);
    }
    for (int i = 1; i < argc; ++i) fprintf(out, " %s", argv[i]);
    fputc('\n', out);
    fclose(out);
    if (sleep_game[0]) {
#ifdef _WIN32
        return wait_for_exit();
#endif
    }
    return 0;
}
