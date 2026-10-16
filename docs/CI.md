# GitHub Actions CI

[The workflow](../.github/workflows/ci.yml) runs on pushes, pull requests, and
manual dispatch from the repository's Actions tab. No secrets are required.
Older runs for the same branch or pull request are cancelled when a new one starts.

The jobs are:

- **Generate synthetic audio fixtures**: Ubuntu generates MP3 test inputs using
  LAME and mpg123 and checks them against the committed golden values. Both
  platform jobs download the same fixtures.
- **Windows x64 (MSVC)**: Windows Server 2022 runs `build.bat Release`, including
  the extension, unit tests, and Windows oracle helpers, then runs the unit suite.
- **macOS arm64 (Apple Clang)**: macOS 26 builds the extension and unit tests
  with a macOS 26 deployment target, then runs the unit suite.

Both platform jobs upload test logs even when tests fail. Successful jobs also
upload native build artifacts, retained for seven days. The macOS archive
preserves executable permissions. Download these from the workflow run's
Artifacts section.

These are native libraries and test executables, not exported playable apps.
Use the libraries with the checked-out Godot project and Godot 4.7. Packaging,
code signing, release publishing, GPU tests, and retail gameplay tests remain
separate. Retail-dependent tests report skips without the two game installations;
CI never downloads or uploads retail game assets or local generated map packs.

To reproduce the unit stage, run `openbfme_tests` from the build directory after
building and generating the synthetic audio fixtures. Do not filter out failing
tests to make CI pass; inspect the uploaded logs. A complete Windows runner
execution and a hosted macOS execution are needed to confirm platform coverage.
