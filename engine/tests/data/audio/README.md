# MP3 test fixtures

The five MP3 cases in `tests/test_audio_decode.cpp` cover golden decoding,
streaming/rewind, gapless/tags/frame walking, container defects, and corrupted
input. They require all 17 synthetic streams, never committed to git.

From the repository root, on the reference host with **LAME 3.100** and
**mpg123** on PATH, generate them outside CMake (including outside a Flatpak
sandbox) with this one-line command:

```sh
python3 tools/audio/gen_mp3_fixtures.py --out workspace/audio_fixtures
```

On Windows use `py -3` instead of `python3`. Generation checks the unchanged
`mp3_golden.tsv` decoded numbers and `mp3_fixtures.tsv` sizes/SHA-256 before
publishing any streams. A non-reference encoder/decoder is an error: do not
rebaseline the goldens to make a host pass. The golden provenance is LAME 3.100 /
mpg123 1.33.1; other tool builds are accepted only if both contracts match.
To check a copied/generated directory without lame or mpg123:

```sh
python3 tools/audio/gen_mp3_fixtures.py --out workspace/audio_fixtures --check-only
```

When `OPENBFME_BUILD_TESTS=ON` (default), CMake first validates existing streams in
`<build>/audio_fixtures`. CMake checks sizes/SHA-256 natively: valid files need
neither Python nor lame/mpg123 and no extra flag;
they are never overwritten, even with LAME 4.0 on PATH. If files are absent or
invalid, generation requires LAME **3.100** and mpg123. An incompatible encoder
reports, for example, `lame 4.0 found, fixtures need 3.100`, before encoding.
The five MP3 test cases validate all 17 streams against the manifest at test time;
there is no fixture-generation build dependency that can block compilation.
CMake passes the resolved host tool
paths to Python, so tool discovery and invocation cannot use different PATHs.
When generation is unavailable, configure emits one warning with the missing tool
or incompatible LAME version and the standalone command, but succeeds. The build
also succeeds; **exactly the five MP3 audio test cases fail**, with the recorded
tool/version reason and generation command. None is skipped or silently passed.
If fixtures become invalid after configure, those same five cases fail with the
size/SHA-256 mismatch and the regeneration command.

For sandboxed CMake or non-reference hosts (including macOS), copy the reference
directory if needed, make it visible inside the sandbox, then configure with:

```sh
cmake -S engine -B engine/build -G Ninja -DOPENBFME_BUILD_GODOT=OFF -DOPENBFME_AUDIO_FIXTURES="$PWD/workspace/audio_fixtures"
```

The legacy `OPENBFME_LAME=OFF` flag is still accepted to prohibit regeneration,
but is not required for pre-generated fixtures and **does not disable any test**. The CMake cache path
`OPENBFME_AUDIO_FIXTURES` defaults to the environment variable of the same name
when first configured, otherwise `<build>/audio_fixtures`. Tests use the
configured path unless the runtime environment variable overrides it.
Keep fixture bytes in ignored directories. Only the numeric/hash contracts
belong in git; `--write-golden` is for deliberately reviewed contract changes,
not routine generation.

## Release-only builds

Use `-DOPENBFME_BUILD_TESTS=OFF` to omit the C++ test target and **all** MP3
fixture/tool checks, including configure checks. This is independent of
`CMAKE_BUILD_TYPE=Release`, which normally still builds tests. Linux packaging,
macOS packaging and the compile-only macOS CI job use this option.
On MSVC, both `build.bat` and `build.bat Release` build the GDExtension,
tests and helpers without needing MP3 tools/fixtures. Running the tests still
requires verified reference streams for the five MP3 cases.

GitHub's clean Linux and Windows test-build jobs receive the verified reference
streams from the dedicated `fixtures` job as a one-day artifact. This is needed
to run the five formerly excluded MP3 cases without committing bytes or requiring
audio tools on those workers. The release-only macOS job does not download or
depend on that artifact. Other CI machines can keep copying their validated
reference files into `<build>/audio_fixtures` before configure, with no flags.
