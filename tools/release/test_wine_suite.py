"""Lane WIN-1 r2 (Sol r1): tools/release/wine_suite.sh exits non-zero when a batch fails or is lost.

`wine` is a stub on PATH that runs a fake openbfme_tests.exe (a shell script printing a doctest summary), so the gate logic is tested without Wine.
"""
import os
import stat
import subprocess

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "wine_suite.sh")

FAKE_TESTS = r'''#!/bin/sh
# the fake suite: STUB_MODE good (every batch passes), fail (the batch holding test_b fails), lost (the batch holding test_b prints no summary)
case "$*" in
  *test_b.cpp*) b=1 ;;
  *) b=0 ;;
esac
if [ "$b" = 1 ] && [ "$STUB_MODE" = lost ]; then echo "crashed"; exit 3; fi
if [ "$b" = 1 ] && [ "$STUB_MODE" = fail ]; then
  echo "x.cpp:1: ERROR: CHECK( 1 == 2 ) is NOT correct!"
  echo "[doctest] test cases:  2 |  1 passed | 1 failed | 0 skipped"; exit 1
fi
echo "[doctest] test cases:  2 |  2 passed | 0 failed | 0 skipped"
'''

STUB_WINE = "#!/bin/sh\nexec \"$@\"\n"


def write_exec(path, text):
    with open(path, "w") as f:
        f.write(text)
    os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC)


def run(tmp_path, mode):
    tree = tmp_path / "tree"
    (tree / "engine" / "tests").mkdir(parents=True)
    for i in range(10):  # two batches: test_a + 7 more, then test_b + one more
        name = "test_b.cpp" if i == 8 else "test_%02d.cpp" % i
        (tree / "engine" / "tests" / name).write_text("")
    build = tmp_path / "build"
    build.mkdir()
    write_exec(str(build / "openbfme_tests.exe"), FAKE_TESTS)
    bindir = tmp_path / "bin"
    bindir.mkdir()
    write_exec(str(bindir / "wine"), STUB_WINE)
    env = dict(os.environ, PATH=str(bindir) + os.pathsep + os.environ["PATH"], STUB_MODE=mode, WINEPREFIX=str(tmp_path / "prefix"),
               WINE_SUITE_RETRY_SLEEP="0")
    log = tmp_path / "suite.log"
    r = subprocess.run(["bash", SCRIPT, str(build), str(log), str(tree)], env=env, capture_output=True, text=True, timeout=120)
    return r.returncode, log.read_text()


def test_every_batch_passing_exits_0(tmp_path):
    rc, log = run(tmp_path, "good")
    assert rc == 0, log
    assert log.strip().splitlines()[-1] == "CHUNKED test cases: 4 | 4 passed | 0 failed | 0 batches lost"


@pytest.mark.parametrize("mode,last", [("fail", "CHUNKED test cases: 4 | 3 passed | 1 failed | 0 batches lost"),
                                       ("lost", "CHUNKED test cases: 2 | 2 passed | 0 failed | 1 batches lost")])
def test_a_failed_or_lost_batch_exits_non_zero(tmp_path, mode, last):
    rc, log = run(tmp_path, mode)
    assert rc != 0, log
    assert log.strip().splitlines()[-1] == last
