# Ring lane publish plan (do not execute in the importer lane)

Run from the repository root only after the in-progress seven-faction publish
has ended. These commands intentionally separate conversion, publication, and
selection. Do not add `--select` to any build command.

```powershell
$python = 'C:\Users\Jonathan\Desktop\open-bfme\workspace\retail-work\tools\python-3.12-env\Scripts\python.exe'
$state = 'C:\Users\Jonathan\Desktop\open-bfme\workspace\retail-work'
$workspace = "$state\editions\rotwk"
$assets = "$workspace\cache\effective-assets"
$content = 'C:\Users\Jonathan\Desktop\open-bfme\workspace\content-packs'

& $python tools\rotwk_faction_convert_batch.py --install F:\RotWK --game rotwk --state-root $state --assets-root $assets --faction men --faction elves --faction dwarves --faction isengard --faction mordor --faction wild --faction angmar
& $python tools\openbfme_import.py --state-root $state compile-cah-system --game rotwk --assets-root $assets
& $python tools\openbfme_import.py --state-root $state compile-ring-system --game rotwk --assets-root $assets
& $python tools\rotwk_faction_pack_proof.py --install F:\RotWK --game rotwk --state-root $state --faction men --faction elves --faction dwarves --faction isengard --faction mordor --faction wild --faction angmar --publish
& $python tools\rotwk_multimap_skirmish.py --install F:\RotWK --game rotwk --state-root $state --effective-assets $assets --full-profile --build --publish

# Read each exact bundleSha256 from:
#   $state\reports\rotwk-faction-pack-proof.json
# Read build.publishedPack and build.packMapsSha256 from:
#   $state\reports\rotwk-multimap-skirmish.json
# The leaf directory name of build.publishedPack is the bundle digest used by
# selection. build.packMapsSha256 independently attests data/maps.json; it is
# not the bundle digest.
# Substitute those receipts below; never guess or reuse an older digest.
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-men-vslice       --bundle-sha256 <men-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-elves-vslice     --bundle-sha256 <elves-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-dwarves-vslice   --bundle-sha256 <dwarves-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-isengard-vslice  --bundle-sha256 <isengard-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-mordor-vslice    --bundle-sha256 <mordor-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-wild-vslice      --bundle-sha256 <wild-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-angmar-vslice    --bundle-sha256 <angmar-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-playable-maps-private --bundle-sha256 <leaf-directory-name-from-build.publishedPack>

& $python tools\check_pack_addresses.py
& powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish-durable-pack.ps1 -Verify
```

The Men host cook owns `data/ring/system.json`, Gollum, TheRing, and fortress
ring art. Elves/Dwarves/Arnor-style Galadriel routes and the evil-faction
Sauron routes remain source-derived in their own unit documents. The map cook
adds `Lib_GollumSpawn` only to maps whose `LibraryMapLists` references it; maps
with inline scripts are left unchanged.
