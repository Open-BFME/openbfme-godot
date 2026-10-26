"""Godot subprocesses, including windowed menu walks, must never open other apps."""
import os
import shlex
import shutil

import pytest


@pytest.fixture(scope="session", autouse=True)
def _record_external_opens(tmp_path_factory):
    root = tmp_path_factory.mktemp("external-open-guard")
    marker = root / "attempted"
    if os.name != "nt":
        real_gio = shutil.which("gio")
        for name in ("xdg-open", "gio", "open", "explorer", "rundll32"):
            canary = root / name
            record = f"printf attempted >> {shlex.quote(str(marker))}\nexit 99\n"
            if name == "gio":
                # only `gio open` opens anything; xdg-mime's MIME queries (`gio help info`, `gio info`) run the real gio
                passthrough = f"exec {shlex.quote(real_gio)} \"$@\"\n" if real_gio else "exit 1\n"
                canary.write_text(f"#!/bin/sh\nif [ \"$1\" = open ]; then\n{record}fi\n{passthrough}")
            else:
                canary.write_text(f"#!/bin/sh\n{record}")
            canary.chmod(0o755)
    with pytest.MonkeyPatch.context() as env:
        env.setenv("OPENBFME_TEST", "1")
        env.setenv("PATH", str(root) + os.pathsep + os.environ.get("PATH", ""))
        yield
        assert not marker.exists(), "a test subprocess attempted a real external open"
