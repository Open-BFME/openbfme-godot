# Ring lane publish plan (do not execute in the importer lane)

Run from the repository root only after the in-progress seven-faction publish
has ended. These commands intentionally separate conversion, publication, and
selection. Do not add `--select` to any build command.

```powershell
$python = 'C:\Users\Jonathan\Desktop\open-bfme\.private\retail-work\tools\python-3.12-env\Scripts\python.exe'
$state = 'C:\Users\Jonathan\Desktop\open-bfme\.private\retail-work\editions\rotwk'
$assets = "$state\cache\effective-assets"
$content = 'C:\Users\Jonathan\Desktop\open-bfme\.private\content-packs'

& $python tools\rotwk_faction_convert_batch.py --install F:\RotWK --game rotwk --state-root $state --assets-root $assets --faction men --faction elves --faction dwarves --faction isengard --faction mordor --faction wild --faction angmar
& $python tools\openbfme_import.py compile-cah-system --game rotwk --assets-root $assets
& $python tools\rotwk_faction_pack_proof.py --install F:\RotWK --game rotwk --state-root $state --faction men --faction elves --faction dwarves --faction isengard --faction mordor --faction wild --faction angmar --publish
& $python tools\rotwk_multimap_skirmish.py --install F:\RotWK --game rotwk --state-root $state --effective-assets $assets --full-profile --build --publish

# Read each exact bundleSha256 from:
#   $state\reports\rotwk-faction-pack-proof.json
# Read the map packId and published directory digest from:
#   $state\reports\rotwk-multimap-skirmish.json
# Substitute those receipts below; never guess or reuse an older digest.
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-men-vslice       --bundle-sha256 <men-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-elves-vslice     --bundle-sha256 <elves-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-dwarves-vslice   --bundle-sha256 <dwarves-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-isengard-vslice  --bundle-sha256 <isengard-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-mordor-vslice    --bundle-sha256 <mordor-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-wild-vslice      --bundle-sha256 <wild-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-angmar-vslice    --bundle-sha256 <angmar-bundleSha256>
& $python tools\openbfme_import.py update-selection-entry --godot-content-root $content --pack-id rotwk-playable-maps-private --bundle-sha256 <maps-bundleSha256>

& $python tools\check_pack_addresses.py
& powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish-durable-pack.ps1 -Verify
```

The Men host cook owns `data/ring/system.json`, Gollum, TheRing, and fortress
ring art. Elves/Dwarves/Arnor-style Galadriel routes and the evil-faction
Sauron routes remain source-derived in their own unit documents. The map cook
adds `Lib_GollumSpawn` only to maps whose `LibraryMapLists` references it; maps
with inline scripts are left unchanged.
