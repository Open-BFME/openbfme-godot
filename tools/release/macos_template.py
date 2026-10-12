#!/usr/bin/env python3
"""Lane MAC-1: the arm64-only Godot macOS export template, made from Godot's official universal one.

    python3 tools/release/macos_template.py --out <zip> [--templates <dir of Godot's export templates>]
    python3 tools/release/macos_template.py --thin <fat Mach-O> --out <file>      (one binary's arm64 slice)

Godot ships one macOS template archive, macos.zip, whose two binaries (godot_macos_debug.universal, godot_macos_release.universal) are
fat Mach-O files holding an x86_64 and an arm64 slice; an export with binary_format/architecture="arm64" asks the archive for
godot_macos_debug.arm64 and fails with the official archive ("Requested template binary ... not found"). The OpenBFME GDExtension is
arm64 only (engine/, built on the Mac), so an OpenBFME.app with a universal Godot inside would start on an Intel Mac and fail to load its
extension, and carry 90 MB of x86_64 code that can never run. This script writes a template archive that holds the official files
unchanged plus each binary's arm64 slice under the name Godot asks for; package.sh and build_launcher.sh point the macOS presets'
custom_template at it (godot/export_presets.cfg, launcher/export_presets.cfg: "macOS arm64").

The slice is taken from the fat header (struct fat_header / fat_arch, big-endian; CPU_TYPE_ARM64 = 0x0100000C) exactly as `lipo -thin
arm64` writes it (checked byte for byte against lipo on macOS 26), so no Apple tool is needed and the output is the same on any host.
The archive's entries carry a fixed date (1980-01-01) and are stored in the archive's own order, so the same official archive gives
the same bytes; the official archive is read from --templates (default: Godot's export template folder of this OS, version 4.7.2.stable)
and must be the 4.7.2.stable one (its version.txt).
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
import zipfile
from pathlib import Path

GODOT_VERSION = "4.7.2.stable"
FAT_MAGIC = 0xCAFEBABE
CPU_TYPE_ARM64 = 0x0100000C
MH_MAGIC_64 = 0xFEEDFACF
BINARIES = ("macos_template.app/Contents/MacOS/godot_macos_debug.universal",
            "macos_template.app/Contents/MacOS/godot_macos_release.universal")


def default_templates_dir() -> Path:
    if sys.platform == "darwin":
        return Path.home() / "Library/Application Support/Godot/export_templates" / GODOT_VERSION
    if sys.platform == "win32":
        return Path(os.environ.get("APPDATA", "")) / "Godot/export_templates" / GODOT_VERSION
    base = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")
    return base / "godot/export_templates" / GODOT_VERSION


def arm64_slice(data: bytes) -> bytes:
    """The arm64 slice of a fat Mach-O (what `lipo -thin arm64` writes); a thin arm64 file is returned as it is."""
    if len(data) >= 4 and struct.unpack(">I", data[:4])[0] != FAT_MAGIC:
        if len(data) >= 8 and struct.unpack("<II", data[:8]) == (MH_MAGIC_64, CPU_TYPE_ARM64):
            return data
        raise ValueError("not a fat Mach-O file and not a thin arm64 Mach-O file")
    magic, count = struct.unpack(">II", data[:8])
    if count == 0 or count > 16:
        raise ValueError(f"a fat header with {count} architectures")
    for i in range(count):
        cputype, _subtype, offset, size, _align = struct.unpack_from(">IIIII", data, 8 + 20 * i)
        if cputype == CPU_TYPE_ARM64:
            if offset + size > len(data):
                raise ValueError("the arm64 slice runs past the end of the file")
            piece = data[offset:offset + size]
            if struct.unpack("<II", piece[:8]) != (MH_MAGIC_64, CPU_TYPE_ARM64):
                raise ValueError("the arm64 slice does not start with a 64-bit arm64 Mach-O header")
            return piece
    raise ValueError("no arm64 slice in the fat file")


def check_templates(d: Path) -> Path:
    version = d / "version.txt"
    if not version.is_file() or version.read_text(encoding="utf-8").strip() != GODOT_VERSION:
        raise SystemExit(f"macos_template: {d} does not hold Godot's {GODOT_VERSION} export templates (version.txt)")
    z = d / "macos.zip"
    if not z.is_file():
        raise SystemExit(f"macos_template: {z} does not exist: install Godot {GODOT_VERSION}'s export templates")
    return z


def write_template(official: Path, out: Path) -> dict[str, int]:
    """The arm64 template archive; {binary name: slice size} for the report."""
    sizes = {}
    with zipfile.ZipFile(official) as src, zipfile.ZipFile(out, "w") as dst:
        names = src.namelist()
        for want in BINARIES:
            if want not in names:
                raise SystemExit(f"macos_template: {official} holds no {want}")
        for info in src.infolist():
            data = src.read(info)
            entry = zipfile.ZipInfo(info.filename, date_time=(1980, 1, 1, 0, 0, 0))
            entry.external_attr = info.external_attr
            entry.compress_type = zipfile.ZIP_DEFLATED
            dst.writestr(entry, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
            if info.filename in BINARIES:
                try:
                    thin = arm64_slice(data)
                except ValueError as e:
                    raise SystemExit(f"macos_template: {info.filename}: {e}")
                name = info.filename[:-len("universal")] + "arm64"
                entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                entry.external_attr = info.external_attr
                entry.compress_type = zipfile.ZIP_DEFLATED
                dst.writestr(entry, thin, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
                sizes[name.rsplit("/", 1)[1]] = len(thin)
    return sizes


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, required=True, help="the template archive to write (or, with --thin, the thin binary)")
    ap.add_argument("--templates", type=Path, default=None, help=f"Godot's export template folder (default: this OS's, {GODOT_VERSION})")
    ap.add_argument("--thin", type=Path, help="write the arm64 slice of this fat Mach-O file instead of a template archive")
    a = ap.parse_args(argv)
    if a.thin:
        try:
            a.out.write_bytes(arm64_slice(a.thin.read_bytes()))
        except ValueError as e:
            raise SystemExit(f"macos_template: {a.thin}: {e}")
        print(f"MACOS TEMPLATE thin {a.out} ({a.out.stat().st_size} bytes)")
        return 0
    official = check_templates(a.templates or default_templates_dir())
    a.out.parent.mkdir(parents=True, exist_ok=True)
    tmp = a.out.with_name(a.out.name + ".tmp")
    sizes = write_template(official, tmp)
    os.replace(tmp, a.out)
    print(f"MACOS TEMPLATE {a.out}: " + ", ".join(f"{n} {s} bytes" for n, s in sizes.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
