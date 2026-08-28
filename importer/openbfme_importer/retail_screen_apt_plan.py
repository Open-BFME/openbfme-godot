"""Build the APT plan for ANY retail screen movie, not just the Men HUD.

`retail_hud_apt_profile.build_retail_hud_apt_plan` is a Men-HUD instrument: it
pins `_GROUPS`, asserts oracle markers ("188 virtual files", "SGCommandBar",
"window/controlbar.wnd") and validates the palantir external-movie closure. None
of that is wrong - it is the attestation for THAT scene - but it is why a screen
cannot simply be appended to it (queue Q117).

The parser underneath is already movie-agnostic: `sage_apt.parse_apt_movie` emits
exactly the `apt` summary that `retail_hud_apt_convert._movie_from_plan`
consumes, and all 81 screens ship as their own `apt/<movie>.big`, the same shape
the HUD plan already handles. So this module is deliberately thin - it assembles
the plan and attests every byte it reads, and it invents nothing.

Every file is hashed as it is read and the digest goes into the plan, so the
converter's `_verified_source` re-check is a real second opinion rather than a
restatement of the same number: the plan says what the tree held at plan time,
and the convert fails closed if the tree has since moved.
"""

from __future__ import annotations

import hashlib
import re
from pathlib import Path
from typing import Any

from .sage_apt import (
    AptParseError,
    parse_apt_constants,
    parse_apt_dat,
    parse_apt_geometry,
    parse_apt_movie,
    parse_tga_identity,
)

#: A screen is one movie plus its constants, image assignments and geometry.
#: RotWK's largest shell movie is `LivingWorldUI.apt` at 799,258 bytes, so the
#: default APT bound is raised to admit the whole shell rather than only the
#: HUD-sized ones. This is a MEASURED edition fact, not slack.
MAX_SCREEN_APT_BYTES = 1_048_576
MAX_SCREEN_EXPORTS = 8192
#: `MenuExport.apt` exports 1,334 symbols in BFME2 and 4,574 in RotWK.
_GEOMETRY_NAME = re.compile(r"^(\d+)\.ru$")
#: Every screen keeps its atlases where the retail tree puts them:
#: ``art/Textures/apt_<Movie>_<textureId>.tga``.  That is the SAME naming the
#: Men-HUD plan feeds the converter (`_movie_from_plan` keys atlases off the
#: trailing ``_<n>.tga``), so nothing about the shape is screen-specific.
_ATLAS_DIRECTORY = "art/Textures"
#: TGA bound is the one `parse_tga_identity` already enforces (2048x2048x4).
MAX_SCREEN_ATLAS_BYTES = 8 * 1024 * 1024
MAX_SCREEN_ATLASES = 64
#: Measured: the widest screen closure in the tree is 4 movies deep-and-wide.
MAX_SCREEN_CLOSURE = 32


#: FROZEN flagged-null clip-action identities, one row per authored record.
#:
#: `retail_hud_apt_convert` fails closed on a PlaceObject whose clip-action
#: FLAG is set while its pointer is null, admitting only exact identities.  An
#: EARLIER revision of this module enumerated those identities from the same
#: file the parser was about to read and registered whatever it found, which
#: made the guard vacuous: the very records it exists to refuse were
#: pre-admitted by construction.  Counting them agreeing with a hand
#: measurement proved the WALK was right, not that the records were authored.
#:
#: So the walk is now a VERIFIER against this frozen table, the same shape
#: `retail_strategic_apt_convert.EXPECTED_FLAGGED_NULL_CLIP_ACTIONS` already
#: uses.  A record that is not here - or whose 64 bytes no longer hash to the
#: frozen digest - is a REFUSAL naming it, so a patched .apt cannot slip a new
#: null pointer past the guard.  The digest is what makes an offset collision
#: between editions harmless.
#:
#: Both editions are covered because the lane must stay edition-agnostic, and
#: their offsets genuinely differ.  The cross-check worth naming: RotWK's eight
#: `timeline.apt` records at 72,228..72,676 are EXACTLY the eight the strategic
#: lane hand-measured independently, and BFME2's are the 120,344..120,792 that
#: same lane records as the ones it must NOT be.
FROZEN_FLAGGED_NULL_CLIP_ACTIONS: tuple[tuple[str, int, int, str], ...] = (
    ("cahappearance.apt", 13892, 0xAE, "e3def56fe06a34ef09230a474c18cb4443b084bceaf1e7aa81d0256f37b79dcc"),  # rotwk
    ("cahappearance.apt", 22052, 0xAE, "6883d458d127008e3464f948896d79f959f8810b9d9cfa92962e70994beb82dd"),  # bfme2
    ("cahmanager.apt", 41320, 0xA6, "f8364ba1b8e1b22482f9990f08ce3afcacd116445a0417a08aedeff41f0a1949"),  # rotwk
    ("cahmanager.apt", 41384, 0xA6, "17136fcf829e6d9360ffcf5f79d4d821fbd134492d9ac1ce3e8f46ba81450210"),  # rotwk
    ("cahmanager.apt", 53440, 0xA6, "4c518cfdbc44076a914dabae3e507000004f0adad42d25a755ff0250c2017558"),  # bfme2
    ("cahmanager.apt", 53504, 0xA6, "d40760a8430161a9aae0aa8a68cb9d801590f9bfbd6b4ca1cc198fe76010da75"),  # bfme2
    ("cahmanager.apt", 110624, 0xA6, "6bd453a6510c05416a22aee0e39f0639d50fcbc13a85b9224be4d82c5c44c940"),  # rotwk
    ("cahmanager.apt", 110688, 0xA6, "5eda9885dbc04b364441fad00a3d9045207861e9beeebdc0f31b32c2146e8218"),  # rotwk
    ("cahmanager.apt", 122216, 0xA6, "e6183c26abbd1c092bc7af0a425d89ece74a370fee8701a5cd65315fe6882045"),  # bfme2
    ("cahmanager.apt", 122280, 0xA6, "9cac0cdec759aede8201521d7ddb4986881ccf5eef9b67a98a65ca51e9863683"),  # bfme2
    ("ingamechat.apt", 25316, 0x86, "7b2dbb32547df7aa95dea7428566eb5085fefbbebbb4fdf550c3226db3950124"),  # bfme2
    ("ingameheroselect.apt", 166756, 0xB6, "7cf6432cbd91629acd5252c69aa957a08cadffd61214ae49ed0e078dec99a135"),  # bfme2
    ("ingameheroselect.apt", 167600, 0xB6, "daee0adf4ffee4c89c035a0d49a641793029abdf03fa8187c049f7cd9deab7fa"),  # rotwk
    ("lanopenplay.apt", 58800, 0xA6, "92a2ae6b4484b1e48ae7004639785aff69ba8d2442d9a30c2587a9ae945c9628"),  # rotwk
    ("lanopenplay.apt", 58864, 0xA6, "a36516f65c2dfe76820309bf60c4733d2337562e611f55e94b12f2e29c3c6c57"),  # rotwk
    ("lanopenplay.apt", 76916, 0xA6, "28b9cd187db731825980fd13ee2621bf971b6b05b3c772d3d62818b75fe4480d"),  # bfme2
    ("lanopenplay.apt", 76980, 0xA6, "e202e0d46295c8339c46c60ec5093b2c4e1ec3e092db653d4df0cd4ea35ad6fa"),  # bfme2
    ("lanstrategic.apt", 64568, 0xA6, "f3094e42896541669a7ed7b5d9ec6de93e51abe2c95051c8b69508458902a99a"),  # rotwk
    ("lanstrategic.apt", 64696, 0xA6, "038d65848e0154ea6f984fa79ee42fa4a36af06a66c6539086c31f6c5d1b01a0"),  # rotwk
    ("lanstrategic.apt", 72592, 0xA6, "1f01e77d0660c4ac24c91c2eb1852e49a3cf686068f8ab4ea9333925a50eabca"),  # bfme2
    ("lanstrategic.apt", 72720, 0xA6, "a3f6e44795bdf143d470c14022acb9bdcbb64e3a693333e257456e974e7ebbd8"),  # bfme2
    ("objectives.apt", 22184, 0x9E, "12142ce6fdf18ec3f1dbdcbd5fc3c07dcf7971638d25922d8bce99f13359e814"),  # rotwk
    ("objectives.apt", 22648, 0x96, "0a1f341f777c8b3a33e834f9e58703cacfa3eef2ae68e4f3e135e69cb8e83cac"),  # rotwk
    ("objectives.apt", 23712, 0x9E, "4fbe3cfd2a7d84a44c4a73703f0a2f8c6da435514c73e0ec4ab3d82f9da1c7ee"),  # rotwk
    ("objectives.apt", 35124, 0x9E, "b066a71b2a51e99b56d7900bc081b62b917d4316581e793fea74faa535f3e0cf"),  # bfme2
    ("objectives.apt", 35588, 0x96, "2599cf5b6c3ec313ceeeb17ae87965c5c11f7bf13329a8164fd8797a5939ac76"),  # bfme2
    ("objectives.apt", 36652, 0x9E, "51240e025599ba53ae7e274194aa38d39a7e0153cdbe54266d95e58125aa8a49"),  # bfme2
    ("onlinelogin.apt", 56820, 0xA6, "d1fe899a3e745376aad82048b6546f21c3ecb8f1c99fc250195e1de76dd18329"),  # bfme2
    ("onlineopenplay.apt", 81616, 0x86, "ed3ccef856e3d67b2895f03b3842eb07f1e2866df39e5a38cff13fe2193fdf0a"),  # bfme2
    ("onlineopenplay.apt", 116540, 0xA6, "25425792548d8062b516f5fab2d46f72afa23101fbe1fd06fdef4c0c5c5d85b1"),  # rotwk
    ("onlineopenplay.apt", 116668, 0xA6, "5278b7a74a5d9b6abc31b1680406ba551b7f1b6b338209f5ad290a40550d4999"),  # rotwk
    ("onlineopenplay.apt", 116732, 0xA6, "79005ddab2f22c734e7e76ed09bdb2b052c1564189edd417e7f15bc766b9f7ce"),  # rotwk
    ("onlineopenplay.apt", 116796, 0xA6, "cfc96d4d33179838692cb5c68a6d9b86d22b9c8987506fde7a9b51d3ba185615"),  # rotwk
    ("onlineshell.apt", 32612, 0xB6, "c009826f0494408c518bdd5ad409bd38fb1c6b6eec97a0e036860f8548478acf"),  # bfme2
    ("onlinestrategic.apt", 11908, 0xA6, "82637731ee7668833846a003d0f510c88fddad4dd4aa79f9a26f723d1a1913d1"),  # rotwk
    ("onlinestrategic.apt", 12036, 0xA6, "901b64a0970f49486a75411bfde1a8030c66f9d0e270dd050888e10689261b6c"),  # rotwk
    ("onlinestrategic.apt", 12100, 0xA6, "bd9d6bb493138c232f59b49224b63b2c554fc3432c89ad70a3ed175772ff15a7"),  # rotwk
    ("onlinestrategic.apt", 12164, 0xA6, "002a9939a8f1702b9f14a2c04a074c45ba0711d5ae31bd7aca8c0fb3b852700e"),  # rotwk
    ("timeline.apt", 72228, 0xA6, "195c8ba0c9811f72fb71325dec7666cd0b77727287a5f001776272959d3a7bb3"),  # rotwk
    ("timeline.apt", 72292, 0xA6, "56ac7e1d773be7517e0637925bcf3363d87a41827c00ffc7d5a8186346bd2706"),  # rotwk
    ("timeline.apt", 72356, 0xA6, "cdb6b3c6b8126e5988df8ed8fe683b00baaa1173e183f5fcedd73d39ddf87250"),  # rotwk
    ("timeline.apt", 72420, 0xA6, "43807f4cccbeb291cc4f869e21a7380bc2d4b0f4ab7c5214bcc321ce4fde1366"),  # rotwk
    ("timeline.apt", 72484, 0xA6, "024ae3912da96a1d8d35f59d19735401012cd0715b5d5d73033aec5c39fd3704"),  # rotwk
    ("timeline.apt", 72548, 0xA6, "f9301e801dd31bf0f8c2dfcd5cc331572a04f436ae382a3527b76e08421464c1"),  # rotwk
    ("timeline.apt", 72612, 0xA6, "81abc46dcf6cfb54cda8aef8412520be501c9d0616bc3a00d44c4a38f534027e"),  # rotwk
    ("timeline.apt", 72676, 0xA6, "4bb3cf96921c65a2b584a8e1681710b641ccb68425b32df95bb3aaa4107c35f6"),  # rotwk
    ("timeline.apt", 120344, 0xA6, "9e7c4c55a69ddb89d1774018b8e9f1b641d4bb38cc2d2cc4a3a91b190238a5c5"),  # bfme2
    ("timeline.apt", 120408, 0xA6, "6fb9a938bcc053ebb41c836d655b9437e33b3426d61544e799d34ef144bddd34"),  # bfme2
    ("timeline.apt", 120472, 0xA6, "d6fefaf4055adccd9f45bac9b0fa3114f82c310756c81745af7b7fb156a27669"),  # bfme2
    ("timeline.apt", 120536, 0xA6, "13f5faf0ea2026d6a09baeb02ce12e4242a2fe49cc08320e2d71552a2dbaa4d1"),  # bfme2
    ("timeline.apt", 120600, 0xA6, "9a90e24ec83e597f8edcb40c2180d72448960324dc2152b3e468844e012b16cb"),  # bfme2
    ("timeline.apt", 120664, 0xA6, "7530a99f11effd485a9a56f10a382dd4448fda0fde3ab626f308a59287c98cdb"),  # bfme2
    ("timeline.apt", 120728, 0xA6, "4063a6259224ebf267de971bd6ccb7eea216eb9cf03bfff40e6345f693090a7c"),  # bfme2
    ("timeline.apt", 120792, 0xA6, "9f61904c8a2d9c03e493a797a6347ae6d29842ac417bb3cfc28cbfa3e65be854"),  # bfme2
)
_FROZEN_FLAGGED_NULL_INDEX = {
    (path, offset, flags): digest
    for path, offset, flags, digest in FROZEN_FLAGGED_NULL_CLIP_ACTIONS
}


class ScreenAptPlanError(ValueError):
    """A screen movie is absent, malformed, or violates a stated bound."""


def _read(path: Path, limit: int) -> bytes:
    if not path.is_file():
        raise ScreenAptPlanError(f"screen source is missing: {path.name}")
    size = path.stat().st_size
    if size > limit:
        raise ScreenAptPlanError(f"{path.name} exceeds its stated byte bound")
    return path.read_bytes()


def _digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def screen_movie_names(effective_assets_root: Path | str) -> tuple[str, ...]:
    """Every movie in the tree that carries the full .apt/.const/.dat trio."""

    root = Path(effective_assets_root)
    names = []
    for apt in sorted(root.glob("*.apt")):
        stem = apt.stem
        if (root / f"{stem}.const").is_file() and (root / f"{stem}.dat").is_file():
            names.append(stem)
    return tuple(names)


def build_screen_apt_plan(
    effective_assets_root: Path | str, movie: str
) -> dict[str, Any]:
    """Assemble the exact plan `_movie_from_plan` consumes for one screen.

    Raises rather than guessing: a missing constants file, an unparsable
    geometry row or an oversize movie is a refusal, never a partial plan.
    """

    if not movie or not re.fullmatch(r"[A-Za-z0-9_]{1,64}", movie):
        raise ScreenAptPlanError("screen movie name is not a bare identifier")
    root = Path(effective_assets_root)
    apt_path = root / f"{movie}.apt"
    const_path = root / f"{movie}.const"
    dat_path = root / f"{movie}.dat"

    constants_bytes = _read(const_path, MAX_SCREEN_APT_BYTES)
    constants = parse_apt_constants(constants_bytes, const_path.name)
    apt_bytes = _read(apt_path, MAX_SCREEN_APT_BYTES)
    try:
        summary = parse_apt_movie(
            apt_bytes,
            constants,
            apt_path.name,
            max_bytes=MAX_SCREEN_APT_BYTES,
            max_exports=MAX_SCREEN_EXPORTS,
        )
    except AptParseError as error:
        raise ScreenAptPlanError(f"{movie}: {error}") from error
    dat_bytes = _read(dat_path, MAX_SCREEN_APT_BYTES)
    image_map = parse_apt_dat(dat_bytes, dat_path.name)

    geometry: list[dict[str, Any]] = []
    geometry_dir = root / f"{movie}_geometry"
    if geometry_dir.is_dir():
        rows = []
        for entry in geometry_dir.iterdir():
            match = _GEOMETRY_NAME.match(entry.name)
            if match is None:
                raise ScreenAptPlanError(
                    f"{movie} geometry holds a non-RU file: {entry.name}"
                )
            rows.append((int(match.group(1)), entry))
        for geometry_id, entry in sorted(rows):
            data = _read(entry, MAX_SCREEN_APT_BYTES)
            shape = parse_apt_geometry(data, f"{geometry_dir.name}/{entry.name}")
            geometry.append(
                {
                    "virtualPath": f"{geometry_dir.name}/{entry.name}",
                    "geometryId": geometry_id,
                    "sha256": _digest(data),
                    "byteLength": len(data),
                    "shape": shape,
                }
            )

    return {
        "schema": "openbfme.retail-screen-apt-plan",
        "schemaVersion": 0,
        "movie": movie,
        "apt": summary,
        "constants": constants,
        "imageMap": image_map,
        "geometry": geometry,
        "atlases": _screen_atlases(root, movie),
        "flaggedNullClipActions": _flagged_null_clip_actions(
            apt_bytes, summary, apt_path.name
        ),
    }


def _flagged_null_clip_actions(
    data: bytes, summary: dict[str, Any], virtual_path: str
) -> list[dict[str, Any]]:
    """Enumerate the screen's flagged-null PlaceObject records, by identity.

    Retail authors PlaceObject records whose clip-action FLAG is set while the
    pointer is zero.  `retail_hud_apt_convert` fails closed on those unless the
    exact (path, record offset, flags) identity is registered, which is right:
    a null pointer must never be read as "no clip actions" by accident.  Eleven
    screens hit it, so the screen lane measures its own identities the same way
    the strategic closure measured TimeLine's eight - by walking the SAME frame
    tables the converter walks, never by pattern-matching bytes.

    The identity list is evidence, not permission: the plan also hashes the
    .apt, and the converter re-verifies that digest, so a tree that has moved
    is refused before any of these offsets is trusted.
    """

    def u32(offset: int) -> int:
        if not 0 <= offset <= len(data) - 4:
            raise ScreenAptPlanError(f"{virtual_path} pointer is out of bounds")
        return int.from_bytes(data[offset : offset + 4], "little")

    def i32(offset: int) -> int:
        return int.from_bytes(
            data[offset : offset + 4], "little", signed=True
        ) if 0 <= offset <= len(data) - 4 else _out_of_bounds(virtual_path)

    def frame_table_records(table: int, count: int) -> list[dict[str, Any]]:
        rows: list[dict[str, Any]] = []
        for frame_index in range(count):
            header = table + frame_index * 8
            item_count = i32(header)
            item_table = u32(header + 4)
            if not 0 <= item_count <= 16_384:
                raise ScreenAptPlanError(f"{virtual_path} frame item count is out of bounds")
            for item_index in range(item_count):
                pointer = u32(item_table + item_index * 4)
                if u32(pointer) != 3:
                    continue
                flags = u32(pointer + 4)
                if not flags & 0x80 or u32(pointer + 60) != 0:
                    continue
                digest = _digest(data[pointer : pointer + 64])
                identity = (virtual_path.casefold(), pointer, flags)
                frozen = _FROZEN_FLAGGED_NULL_INDEX.get(identity)
                if frozen is None:
                    raise ScreenAptPlanError(
                        "unfrozen flagged-null clip-action record: "
                        f"{virtual_path.casefold()} @{pointer} "
                        f"flags 0x{flags:02X} sha256 {digest}"
                    )
                if frozen != digest:
                    raise ScreenAptPlanError(
                        "flagged-null clip-action record changed: "
                        f"{virtual_path.casefold()} @{pointer} "
                        f"expected {frozen} found {digest}"
                    )
                rows.append(
                    {
                        "virtualPath": virtual_path.casefold(),
                        "recordOffset": pointer,
                        "flags": flags,
                        "sha256": digest,
                    }
                )
        return rows

    root = summary["root"]
    movie_offset = int(root["entryOffset"]) + 8
    records = frame_table_records(u32(movie_offset + 4), i32(movie_offset))
    character_count = i32(movie_offset + 12)
    character_table = u32(movie_offset + 16)
    for character_id in range(character_count):
        entry = summary["characters"][character_id]
        if str(entry.get("kind")) != "sprite":
            continue
        pointer = u32(character_table + character_id * 4)
        if not pointer:
            continue
        records.extend(frame_table_records(u32(pointer + 12), i32(pointer + 8)))
    return sorted(records, key=lambda row: int(row["recordOffset"]))


def _out_of_bounds(virtual_path: str) -> int:
    raise ScreenAptPlanError(f"{virtual_path} pointer is out of bounds")


def _screen_atlases(root: Path, movie: str) -> list[dict[str, Any]]:
    """The screen's own ``apt_<Movie>_<n>.tga`` atlases, hashed and bounded.

    A screen with no atlas is a screen that draws only solid geometry, not a
    broken plan - but a screen whose geometry names an image id the atlases do
    not cover is left to fail loudly downstream as
    ``texture-assignment-unresolved``.  Nothing here fills a gap by guessing.
    """

    directory = root / _ATLAS_DIRECTORY
    if not directory.is_dir():
        raise ScreenAptPlanError(f"atlas directory is missing: {_ATLAS_DIRECTORY}")
    pattern = re.compile(rf"^apt_{re.escape(movie)}_(\d+)\.tga$", re.IGNORECASE)
    found: list[tuple[int, Path]] = []
    for entry in directory.iterdir():
        match = pattern.match(entry.name)
        if match is not None:
            found.append((int(match.group(1)), entry))
    if len(found) > MAX_SCREEN_ATLASES:
        raise ScreenAptPlanError(f"{movie} exceeds its stated atlas count bound")
    atlases: list[dict[str, Any]] = []
    for texture_id, entry in sorted(found):
        data = _read(entry, MAX_SCREEN_ATLAS_BYTES)
        virtual_path = f"{_ATLAS_DIRECTORY}/{entry.name}"
        try:
            parsed = parse_tga_identity(data, virtual_path)
        except AptParseError as error:
            raise ScreenAptPlanError(f"{movie}: {error}") from error
        digest = str(parsed["sha256"])
        stem = entry.stem.casefold().replace("_", "-")
        parsed["textureId"] = texture_id
        parsed["cookedPng"] = (
            f"assets/ui/screens/{movie.casefold()}/{stem}-{digest[:12]}.png"
        )
        atlases.append(parsed)
    return atlases


def build_screen_closure_plans(
    effective_assets_root: Path | str, movie: str
) -> dict[str, Any]:
    """Plan a screen AND every movie it imports, transitively.

    A screen alone is not a scene.  MainMenu draws 20 primitives on its own and
    32 with `MenuFrameAndBg` loaded; ScoreScreen goes from 184 to 770.  The
    converter already resolves imported characters through `movie.imports`, so
    all it needs is the imported movies present in the same dict - the same
    service `MOVIE_CLOSURE` performs for the Palantir, computed rather than
    hardcoded.

    A movie that cannot be planned is NAMED in ``unplannable`` and its reason
    carried with it.  It is never dropped silently: the caller still sees the
    converter's own `unresolved-import-movie` blocker for whatever it leaves
    out, and now it also knows WHY the movie is missing.
    """

    root = Path(effective_assets_root)
    plans: dict[str, dict[str, Any]] = {}
    unplannable: list[dict[str, str]] = []
    pending = [movie]
    while pending:
        name = pending.pop(0)
        if name.casefold() in {key.casefold() for key in plans}:
            continue
        if any(row["movie"].casefold() == name.casefold() for row in unplannable):
            continue
        if len(plans) >= MAX_SCREEN_CLOSURE:
            raise ScreenAptPlanError(f"{movie} closure exceeds its stated bound")
        try:
            plan = build_screen_apt_plan(root, name)
        except (ScreenAptPlanError, AptParseError) as error:
            if name.casefold() == movie.casefold():
                raise
            unplannable.append({"movie": name, "reason": str(error)})
            continue
        plans[name] = plan
        pending.extend(
            str(item["movie"]) for item in plan["apt"].get("imports", [])
        )
    return {
        "schema": "openbfme.retail-screen-closure-plan",
        "schemaVersion": 0,
        "movie": movie,
        "plans": plans,
        "unplannable": sorted(unplannable, key=lambda row: row["movie"].casefold()),
    }


def screen_source_virtual_paths(
    effective_assets_root: Path | str, movie: str
) -> tuple[str, ...]:
    """Every virtual path ONE screen needs, in a stable order.

    This is the screen equivalent of `retail_shell_apt_convert
    .shell_source_virtual_paths`, and it exists for the pipeline: a converter
    resolves its inputs from extracted archive entries by virtual path, so the
    lane that cooks screens into a content pack has to be able to state exactly
    which paths a screen consumes before any of them is read.

    It enumerates from the tree rather than guessing a pattern - a screen with
    no geometry directory or no atlas simply contributes fewer paths - and it
    refuses a movie whose required trio is absent, because a screen missing its
    own constants is not a screen with fewer sources.
    """

    if not movie or not re.fullmatch(r"[A-Za-z0-9_]{1,64}", movie):
        raise ScreenAptPlanError("screen movie name is not a bare identifier")
    root = Path(effective_assets_root)
    paths: list[str] = []
    for suffix in (".apt", ".const", ".dat"):
        if not (root / f"{movie}{suffix}").is_file():
            raise ScreenAptPlanError(f"screen source is missing: {movie}{suffix}")
        paths.append(f"{movie}{suffix}")

    geometry_dir = root / f"{movie}_geometry"
    if geometry_dir.is_dir():
        rows = []
        for entry in geometry_dir.iterdir():
            match = _GEOMETRY_NAME.match(entry.name)
            if match is None:
                raise ScreenAptPlanError(
                    f"{movie} geometry holds a non-RU file: {entry.name}"
                )
            rows.append((int(match.group(1)), entry.name))
        paths.extend(
            f"{geometry_dir.name}/{name}" for _index, name in sorted(rows)
        )

    directory = root / _ATLAS_DIRECTORY
    if not directory.is_dir():
        raise ScreenAptPlanError(f"atlas directory is missing: {_ATLAS_DIRECTORY}")
    pattern = re.compile(rf"^apt_{re.escape(movie)}_(\d+)\.tga$", re.IGNORECASE)
    atlases = []
    for entry in directory.iterdir():
        match = pattern.match(entry.name)
        if match is not None:
            atlases.append((int(match.group(1)), entry.name))
    paths.extend(f"{_ATLAS_DIRECTORY}/{name}" for _index, name in sorted(atlases))
    return tuple(paths)
