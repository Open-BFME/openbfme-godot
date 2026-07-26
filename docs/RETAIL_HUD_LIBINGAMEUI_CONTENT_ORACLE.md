# Retail HUD `libingameui:37332` content oracle

`retail_hud_libingameui_content_oracle.py` closes the concrete `contentType`
question left by the typed `CreateContent(contentType, contentName)` adapter. It
is a static, payload-free BFME2 1.06 oracle. It does not convert assets, add a
generic ActionScript VM, modify profiles, or add runtime support.

## Men/Fords result

The exact allowlist is one row:

| Movie | Export source index | Symbol | Character | Kind |
| --- | ---: | --- | ---: | --- |
| `libInGameUI` | 647 | `CommandButton` | 49 | sprite |

The export record is at APT offset `5912` with SHA-256
`1781bccfd68f6ab4a96a49b97b5ae596568ec44c9eeced147e8212b87619700e`.
Character 49 begins at offset `18020`; its 16-byte header SHA-256 is
`f2bf1b6b03e4fb18fd13bdffdc6184aaec0b285772f73bca1eb359b44c0dbfbb`.

Only two movies in the nine-movie Men/Fords closure import the
`libInGameUI::MovieClipFrame` host:

- `InGameSideCommandBar` imports it as character 1. Sprite 18 places it as
  `Button`, and sprite 21 creates `Button0` through `Button11` on root frame 0.
- `Palantir` imports it as character 108. Sprite 114 places six instances in
  `_show` frame 9, in source order `1,2,3,4,5,0`.

The native command paths at `0x009286D2` and `0x009295AE` converge through
`0x009C3697` and use `contentType="CommandButton"` with
`contentName="Button"`. The generic create dispatch at `0x009C329B` has exactly
three direct callers:

| Caller | `contentType` | Men/Fords classification |
| --- | --- | --- |
| `0x009C7DDC` | `CommandButton` | reachable |
| `0x009E1402` | `StrategicCommandButton` | excluded StrategicHUD path |
| `0x009FCBBB` | `icon` | excluded unit-icon path |

`StrategicHUD` is pinned only as an excluded comparison. Its sole export is
`StrategicCommandButton`, source index 0, character 12. It is not in the current
nine-movie closure. No exact `icon` export exists in the current closure or that
comparison movie.

The only authored `CreateContent` call is in `palantir:95872`:

```text
_root.CommandButtons["0"].CreateContent("CommandButton", "Bttn")
```

That entire test setup is guarded by `Boolean(_global.InGame)` and branches to
the end when true. Therefore it is skipped in the actual in-game Men/Fords
scene; it is not an additional live consumer and does not change the native
`contentName="Button"` contract.

If an exact export is undefined, retail `attachMovie` leaves
`this[contentName]` undefined. `CreateContent` then skips placeholder geometry
copies and extern registration and renders no concrete child. The integration
must preserve that no-op and must never substitute generic art.

No runtime trace is needed to integrate this allowlist. The one remaining gate
is implementation: bind the converted `libInGameUI` export registry so it can
instantiate character 49 and its converted timeline/visual closure. This oracle
does not perform that binding and does not authorize profile blocker removal.

## Pinned movie triplets

All values are `byte length / SHA-256` in `.apt`, `.const`, `.dat` order.

| Scope | Movie | `.apt` | `.const` | `.dat` |
| --- | --- | --- | --- | --- |
| current | `InGameHelpBox` | `5512 / 520e5a1ff4aac288d7957a8c76818a3ceaff72b395167ccb660fa301447178e7` | `2176 / 2e6e635242e77d2bdd392001f7136c8dea61fa7a13e554850be7c702a97c71de` | `50 / 892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1` |
| current | `InGameHeroSelect` | `174324 / dc155d39f7b8dde5c2ca7ec09407918b3e914d61a5adc59a194e0a33268e3cbd` | `3146 / 46343633da353aa7fdcde60e6e2b61304ef2084324083dccf4c5e3dcbc433f93` | `178 / 29e57bc7bf05b9b21970b10834c9493a0d9ddaee4a184538f50cfdf614c8a70b` |
| current | `InGamePlanningMode` | `29998 / 20003cc09ef9b209bdb4c25a0ec3da9842abbc41a7cb5229e69a7b3f4b01330e` | `1633 / e180509285f59f53484bf28a5351d2b26047a4b4b3501660415318e74b766731` | `195 / d51ddc3707f8a47eb91d66dd1025b014515775a97701e7c24fceb8e043cf515d` |
| current | `InGameSideCommandBar` | `14082 / 84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef` | `3364 / 5f21b405a8121edb689365441177b38b32386f101a1bb06418336dbac815975a` | `50 / 892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1` |
| current | `InGameSpellBook` | `27966 / 24f82808dadd151ffed47284ee92800af18db22894cac4f2479e32b90913f1f4` | `2406 / 40fa111c2cc8bbd05e979ea9b8b5c7fce34654c9c8c8d2b1deaf0399368b1639` | `50 / 892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1` |
| current | `libInGameImagesMain` | `9068 / ad5bb65d3ae84a85934c931764c4ed2a24cefce4db1a996fdd73add388897d24` | `32 / b1fb2aca40af93325888ee9077825df275627f55cb1e1d29938e103290228703` | `414 / fbcb53e6acc3be69461fa8066743dcd179abde8cfee22899f30aa1ce9258da0f` |
| current | `libInGameUI` | `58462 / 305bdfabca3a815f8c373419978ca080a7f28561b2ca9d36eeeb7f35992ba392` | `2876 / 717a03669f47944f9933e829e8d5d1193e375cadbbdfc5804ee131631a7176cd` | `50 / 892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1` |
| current | `Palantir` | `378173 / c1f500847f0c77d4c6504edf79113b5723300165bebd42b4dafda479516f5140` | `10260 / f07e24e3b70e286d491652cc827aef904a2ccabf54107d4f1bfc3030beee8fd9` | `586 / d8e8964711e4061b0643dd0dd3de1876b7326cee6d60e11214793b5d483f3ae4` |
| current | `PalantirExport` | `1716 / 2c35dc2671e316d6d2101b3d8790bea7f9f7b06a597abe6937862396f188391c` | `32 / 708c329be95e34edd70c1a13a82ccc58f8bad534f86ecf2c268b51467dcb21bf` | `224 / 6a45a2b1445b034f369fed28d2f29791abb82e026b0a32b45718788636433b4a` |
| excluded comparison | `StrategicHUD` | `19115 / 9b1bf4f832db1925ff3a4ee1eff49a2f11db87bf12d6394211f19c4f6570221a` | `2465 / d575e2b1e8e542b620ee7bb58d20d6edfb7b1123b079245823579c101977e8c5` | `50 / 892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1` |

## Run and acceptance

```powershell
$env:PYTHONPATH = "importer"
python -m openbfme_importer.retail_hud_libingameui_content_oracle `
  .private/retail-work/cache/effective-assets `
  <BFME2>/game.dat `
  --output .private/scratch/hud-libingameui-content-oracle/contract.json
python -m pytest -q importer/tests/test_retail_hud_libingameui_content_oracle.py
python -m ruff check `
  importer/openbfme_importer/retail_hud_libingameui_content_oracle.py `
  importer/tests/test_retail_hud_libingameui_content_oracle.py
```

For deterministic A/B acceptance, write a second contract to another file in
the same `.private/scratch/hud-libingameui-content-oracle` directory and compare
the files byte-for-byte.
