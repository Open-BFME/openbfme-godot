# Open BFME mods

Drop a folder here (or in `user://mods`) with:

```
my_mod/
  pack.json          # { "id": "my_mod", "priority": 50 }
  units/*.json       # same schema as data/base/units
  buildings/*.json
  factions/*.json
  abilities/*.json
  maps/*.json
  globals.json       # optional overrides
  assets/...         # optional meshes/icons
```

Later packs (higher `priority`) override earlier definitions by `id`.

External content root (absolute path) can also be set via env:

```
OPENBFME_CONTENT=C:\path\to\content_root
```

where `content_root` contains one or more pack folders with `pack.json`.
