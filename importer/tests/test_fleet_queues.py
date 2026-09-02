from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

from importer.tests.test_big import make_big
from importer.tests.test_sage_apt import _apt, _const
from importer.tests.test_sage_map import _synthetic_map
from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.fleet_queues import KINDS, generate_queue_documents, main
from openbfme_importer.sage_apt import parse_wnd_layout
from openbfme_importer.sage_map import convert_sage_map
from openbfme_importer.util import write_json_atomic
from openbfme_importer.verify_item import ItemVerificationError, verify_item


_WND = (
    b"FILE_VERSION = 2;\nWINDOW\n"
    b"WINDOWTYPE = USER;\n"
    b"SCREENRECT = UPPERLEFT: 0 0, BOTTOMRIGHT: 800 600, "
    b"CREATIONRESOLUTION: 800 600;\n"
    b'NAME = "Shell.wnd:Root";\nSTATUS = ENABLED;\nSTYLE = USER;\n'
    b'SYSTEMCALLBACK = "[None]";\nINPUTCALLBACK = "[None]";\n'
    b'TOOLTIPCALLBACK = "[None]";\nDRAWCALLBACK = "[None]";\nEND\n'
)


def _receipt(path: Path, pack: Path) -> dict[str, object]:
    return {
        "path": path.relative_to(pack).as_posix(),
        "size": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def _fixture(tmp_path: Path) -> tuple[Path, Path, Path]:
    install = tmp_path / "install"
    install.mkdir()
    map_source, _height = _synthetic_map()
    files = {
        "art/w3d/hero.w3d": b"",
        "art/compiledtextures/open.png": b"fixture-png",
        "data/audio/sounds/open.wav": b"fixture-wav",
        "maps/map mp arena/map mp arena.map": map_source,
        "maps/campaign/one.map": map_source,
        "maps/cinematic/two.map": map_source,
        "apt/shell.wnd": _WND,
        "apt/open.apt": _apt(),
        "apt/other.wnd": _WND,
        "apt/open.const": _const(),
        "data/ini/object.ini": (
            b"Object FixtureHero\n  KindOf = SELECTABLE INFANTRY\n"
            b"  Model = hero.w3d\nEnd\n"
        ),
    }
    make_big(install / "ini.big", files)

    content_root = tmp_path / "content-packs"
    pack_relative = "fixture-pack/one"
    pack = content_root / "fixture-pack" / "one"
    (pack / "assets").mkdir(parents=True)
    model = pack / "assets" / "hero.glb"
    model.write_bytes(b"fixture-glb")
    proof = pack / "assets" / "hero-proof.json"
    write_json_atomic(
        proof,
        {
            "schema": "openbfme.w3d-presentation-capabilities",
            "schemaVersion": 0,
            "metrics": {"meshCount": 0, "boneCount": 0, "animationCount": 0},
        },
    )

    source_path = tmp_path / "arena.map"
    source_path.write_bytes(map_source)
    map_root = pack / "maps" / "arena"
    convert_sage_map(source_path, map_root)

    screen = pack / "screens" / "shell.json"
    write_json_atomic(screen, parse_wnd_layout(_WND, "apt/shell.wnd"))
    map_outputs = [
        _receipt(path, pack)
        for path in sorted(map_root.iterdir(), key=lambda item: item.name.casefold())
    ]
    manifest = {
        "format": 1,
        "entries": [
            {
                "kind": "model",
                "converter": "w3d-bundle",
                "source": {
                    "virtual_path": "art/w3d/hero.w3d",
                    "sha256": hashlib.sha256(files["art/w3d/hero.w3d"]).hexdigest(),
                },
                "outputs": [
                    _receipt(model, pack),
                    _receipt(proof, pack),
                ],
            },
            {
                "kind": "map",
                "converter": "sage-map",
                "source": {
                    "virtual_path": "maps/map mp arena/map mp arena.map",
                    "sha256": hashlib.sha256(map_source).hexdigest(),
                },
                "outputs": map_outputs,
            },
            {
                "kind": "screen",
                "converter": "sage-apt-screen-runtime",
                "source": {
                    "virtual_path": "apt/shell.wnd",
                    "sha256": hashlib.sha256(_WND).hexdigest(),
                },
                "outputs": [_receipt(screen, pack)],
            },
        ],
    }
    write_json_atomic(pack / "provenance" / "manifest.json", manifest)
    write_json_atomic(
        content_root / "selection.json",
        {
            "schema": "openbfme.pack-selection",
            "schemaVersion": 0,
            "activePack": pack_relative,
            "supplementalPacks": [],
        },
    )
    return install, content_root, proof


def test_queue_documents_are_whole_corpus_and_byte_identical(tmp_path: Path) -> None:
    install, content_root, _proof = _fixture(tmp_path)
    first = tmp_path / "first"
    second = tmp_path / "second"
    args = ["--install", str(install), "--content-root", str(content_root)]
    assert main([*args, "--out-dir", str(first)]) == 0
    assert main([*args, "--out-dir", str(second)]) == 0
    for kind in KINDS:
        one = (first / f"rotwk-{kind}-queue.json").read_bytes()
        two = (second / f"rotwk-{kind}-queue.json").read_bytes()
        assert one == two
        document = json.loads(one)
        assert document["total"] == 3
        assert document["done"] == 1
        assert len(document["open"]) == 2
        assert document["kind"] == kind
        assert all(
            {"id", "title", "rank", "detail", "oracle"} <= set(row)
            for row in document["open"]
        )
        assert "denominators" in document


def test_asset_priority_uses_playable_object_reference(tmp_path: Path) -> None:
    install, content_root, _proof = _fixture(tmp_path)
    documents = generate_queue_documents(
        InstallCatalog.build(install),
        install=install,
        content_root=content_root,
        kinds=("assets",),
    )
    rows = {row["id"]: row for row in documents["assets"]["open"]}
    assert rows["art/compiledtextures/open.png"]["rank"] == 100
    assert all("openbfme_importer.verify_item" in row["oracle"] for row in rows.values())


def test_verify_item_accepts_converted_fixtures_and_names_corruption(tmp_path: Path) -> None:
    install, content_root, proof = _fixture(tmp_path)
    for kind, item_id in (
        ("assets", "art/w3d/hero.w3d"),
        ("maps", "maps/map mp arena/map mp arena.map"),
        ("screens", "apt/shell.wnd"),
    ):
        result = verify_item(
            kind=kind,
            item_id=item_id,
            install=install,
            content_root=content_root,
        )
        assert result["status"] == "verified"
        assert result["id"] == item_id

    proof.write_bytes(b"corrupt")
    with pytest.raises(ItemVerificationError, match="converted-output-corrupt.*output-size-mismatch"):
        verify_item(
            kind="assets",
            item_id="art/w3d/hero.w3d",
            install=install,
            content_root=content_root,
        )
