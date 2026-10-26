"""ONLINE ROLLOUT 2026-10-21: the relay source bundle that services/online/deploy/vps_relay.sh copies to a relay host must configure and build
openbfme_relay on its own. The staging rehearsal found the old four-file list failing to configure once the relay compiled RelayAuth.cpp and the
ROUTER-1 / P16 relay code; this test builds the bundle exactly as the script assembles it (VPS_RELAY_BUNDLE_ONLY: no host is contacted)."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.skipif(shutil.which("cmake") is None or shutil.which("c++") is None, reason="needs cmake and a C++ compiler")
def test_vps_relay_bundle_builds_the_relay(tmp_path):
    bundle = tmp_path / "bundle"
    bundle.mkdir()
    env = {"PATH": "/usr/bin:/bin:/usr/local/bin:" + str(Path(shutil.which("cmake")).parent), "VPS_RELAY_BUNDLE_ONLY": str(bundle)}
    subprocess.run(["bash", str(ROOT / "services/online/deploy/vps_relay.sh"), "unused-host", "unused-secrets", "http://unused"], env=env,
                   check=True, timeout=120)
    build = tmp_path / "build"
    subprocess.run(["cmake", "-S", str(bundle / "services/online/relay"), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release"], check=True,
                   timeout=300, capture_output=True)
    subprocess.run(["cmake", "--build", str(build), "-j", "4", "--target", "openbfme_relay"], check=True, timeout=1200, capture_output=True)
    assert (build / "openbfme_relay").is_file()
