# Retired ambient mod folder

`game/mods/` and `user://mods` are diagnostic-only ambient locations. Packs
placed there are diagnosed and are **not mounted**.

Use only an explicit `OPENBFME_CONTENT` pack root or an explicit
`selection.json` load set. The loader does not scan sibling pack directories.

See the [modding guide](../../docs/MODDING.md) and the
[non-shipping example mod](../../examples/mods/example_hard_orcs/).
