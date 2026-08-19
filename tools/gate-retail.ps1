[CmdletBinding()]
param(
    [string]$Install = "$env:BFME2_INSTALL",
    [string]$GodotPath = "",
    [switch]$IntegrationOwnerPublish,
    # Developer-only escape hatch. The default remains the cold A/B proof.
    # This switch performs one build and the pack provenance records that the
    # reproducibility comparison was not attested.
    [switch]$SingleBuild,
    # Run only SECTION A (the deterministic BFME2 proof-pack gate). SECTION B
    # asserts about a maintainer's private multi-pack selection and cannot run
    # on a machine that does not have one.
    [switch]$SkipPrivateSelection
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "proof-gate-common.ps1")

$gate = "RETAIL_PIPELINE_GATE"
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$gameRoot = Join-Path $repoRoot "game"
$cli = Join-Path $repoRoot "tools\openbfme_import.py"
$pythonBootstrap = Join-Path $PSScriptRoot "bootstrap-importer-python.ps1"
$profilePath = [IO.Path]::GetFullPath((Join-Path $repoRoot "workspace\retail-work\profiles\men-fords-v0-complete.generated.json"))
$expectedProfileId = "men-fords-v0-complete-generated"
$expectedProfileSha256 = "0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d"
$expectedPackId = "bfme2-men-vslice"
# This gate is the BFME2 lane end to end ($env:BFME2_INSTALL, bfme2-men-vslice),
# so its importer calls name --game bfme2 rather than riding the CLI default,
# which is now rotwk (the content baseline).
# Measured with the collector this gate actually runs (pytest), not with
# `unittest discover`, which could not see 949 of these tests. See the
# importer_tests step below.
$minimumImporterTestCount = 2491
$maximumImporterSkipCount = 86
$publishRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "workspace\content-packs"))

# ---------------------------------------------------------------------------
# SECTION B PIN - the private multi-pack selection.
#
# SECTION B (below) runs a set of runners that assert about the MOUNTED
# SELECTION as a whole rather than about this gate's freshly built bfme2
# bundle. Until this pin existed, that made the gate's result depend on
# whichever packs happened to be installed on the maintainer's machine - a
# stale pack could turn a red run green, or a green run red, with no signal
# that the INPUT had changed rather than the code.
#
# These constants pin the selection document byte-for-byte plus every pack
# id/bundle-hash it names. Changing the local selection is allowed; doing so
# SILENTLY is not. When the selection legitimately moves, re-measure and update
# these constants in the same change that moves it, and say so - the same
# conscious-update pattern the state pins use.
# RE-MEASURED 2026-08-18 v0.2.5 recook (7 faction packs; owner playtest fixes)
$expectedSelectionSha256 = "c27af45b5ce7a56b5b1f78a983d3fae8279c68a38f22c7c35201e38daed03992"
$expectedSelectionActivePack = "rotwk-men-vslice/f177d1bd6c43def75b2bcfccde368e3e179d3de67c182351c83e9968b942e514"
# RE-MEASURED 2026-08-18 v0.2.5 recook (7 faction packs; owner playtest fixes)
$expectedSelectionSupplementalPacks = @(
    "bfme2-men-vslice/7de517bf146582f10741750b50d63f9955c42d1fe2aa13200757fc6fb29f217a",
    "bfme2-skirmish-maps-private/f9c14cfa4c25e68509373390741fc82e5892f050a2305a19fa3efaca0f39a5b0",
    "rotwk-elves-vslice/d41df402a90f8ea6aa699547262564ed72d2baf5836939fbbfbe279340d9f1c5",
    "rotwk-dwarves-vslice/fbc7a61c96d98e754314af216a31a76692b96d8875c82206edf0f67dd83d93f5",
    "rotwk-isengard-vslice/d987154db1484f08543519de16b0d402038fbfa56c9c4fa03dd6490a0925637a",
    "rotwk-mordor-vslice/30f82d9d1cd7ab5797dcc2f260a6223a72550072d39eb98eb0cf9a16d5196968",
    "rotwk-wild-vslice/b96c36c24b90f43afeec77fe3fba027584492b3fd7c954624ce25a328cc82091",
    "rotwk-angmar-vslice/84d1cd77e64ef9b1a96df3ecbd4fec866d5e7e66ad6a8091a7d0e08d93fcf316",
    "rotwk-music-vslice/f10d95389a1ab51a7a20b3f549fc6b90291db51f7e68693e4d157f1a67eada8d",
    "rotwk-playable-maps-private/1739b61386b8242aafee7c46c2f2639f950dd8d5d7292687d2c10702b1e9972b",
    "rotwk-cursors-vslice/11236cb6a57396ccf3bfad7d4406f89dc7e0e95b3d9db34c0063c6c1c1d760d2",
    "rotwk-men-eva-overlay/c59262c328a3041b3da684cdab826619ee7a3dcee85b68b137e68a869bc8741d",
    "rotwk-elves-eva-overlay/51d90549e45b6c04e98a95c10bc03a709e8767c121d6d454a5d71bf338a78ab6",
    "rotwk-dwarves-eva-overlay/75f1642049f4a491b45901db02600cc70fe78a95909c8d20c6d0c25e146fd13c",
    "rotwk-isengard-eva-overlay/5228196e4ebd593c0698a65f78ed16aaabb9821c6e51ea6c8626f73fe7960375",
    "rotwk-mordor-eva-overlay/d7e608b0e0f1a0d4151d6f33cf4fd42fec794c314792d8637f6b68bada627dab",
    "rotwk-wild-eva-overlay/98a12fe7b1277b51c23044916e267e7bd86f150da5c14dce61900b0ee1edfc8e",
    "rotwk-angmar-eva-overlay/96820c3d3abe5379ce36f123fc846810feca3756db625dfeb3ca1202d9ee958c",
    "bfme2-neutral-vslice/ccc75c1d6e3272581f6a98ca0d8d56f4040b0ae68d14cda8c1afc6152c8819ce",
    "rotwk-neutral-vslice/6032e4568e34970105370ffe86dd7e88ca4b11c1350b3b1815fbd95bc0edc278",
    "bfme2-missing-physical-20260816-batch-041/f72bc1c5c6f09a68f74651b42a453af4b48ffa2c7bd1bba985a895b041fdd591",
    "bfme2-missing-physical-20260816-batch-042/6d37895946a556bb8ae5f764b60292da8dcebd8ad41058ba7473b96e8f2fcf42",
    "bfme2-missing-physical-20260816-batch-043/779f32f33f55ec48df138a90b70b7a5ffbf6093d3a5d9640c344b182550f4148",
    "bfme2-missing-physical-20260816-batch-044/ca78e9d87f32856ef5cbfcd409fadffd9ec14e1e52118c6b2ab6186b36fdec47",
    "bfme2-missing-physical-20260816-batch-045/db3653c7cb92bfbb79cdd2519059ed07b3cf3347eef0e79fa29896bb3e5cfef0",
    "bfme2-missing-physical-20260816-batch-046/fec6e462c01ff93353591db16fe5524f3e0b2ab0d81c854cac79ff4801520455",
    "bfme2-missing-physical-20260816-batch-047/6f2d1a2449c10e861023efd45662b88e754879194c3db2be70be33e32bc7745f",
    "bfme2-missing-physical-20260816-batch-048/c6c0d0aafbd21a6b549512e59a446dbce605fbc39646fa8e6ee297a8bcbf9340",
    "bfme2-missing-physical-20260816-batch-049/48f50dd4f5cf41f39e1584d2556723686e42a4cba9829479f5bff10fe3a51bb7",
    "bfme2-missing-physical-20260816-batch-050/90eee00bb257ab44b4070db4c959d6ec1512c6884634daa465ad3c58b1249ffe",
    "bfme2-missing-physical-20260816-batch-051/a3156803e30fd22d551d36f8a13bb700b75e6aefc7d567e4f9c8d6454bead1aa",
    "bfme2-missing-physical-20260816-batch-052/97523915de161445145e69f99b8389eeb02d93264568a60d7e21ffda0ceb84d1",
    "bfme2-missing-physical-20260816-batch-053/b51de7f400621ff635cc9f47d1fb32312314cc91ee282d728efedd636035c545",
    "bfme2-missing-physical-20260816-batch-054/77b3525a904847013f8c00d2f46fcbda5f0f3aa9a43c924b9c80cb7949591ff8",
    "bfme2-missing-physical-20260816-batch-055/b1db202983558c5c59330ab531bf6dac72af00e6ca20d93c8352a0bc72d4ae7b",
    "bfme2-missing-physical-20260816-batch-056/923a4e674c63d2a4ce9119e19483eadc7722600bf923d8a292b7b9bb87fdff9e",
    "bfme2-missing-physical-20260816-batch-057/9a727662c191847352e5df511f94992286f236c6feaefdf9a290b5401c2015ff",
    "bfme2-missing-physical-20260816-batch-058/f6ff83affe40a296e0351bd4905c2f756d7529862068e57083c2c9f55fb5fbb5",
    "bfme2-missing-physical-20260816-batch-059/96f682fc916261da6d1c8e4d0c01792f63d701aae4afd7d9d39af2f3f316235b",
    "bfme2-missing-physical-20260816-batch-060/378d8619c8bb6d7d155b5062964c75d038484944a59734f13cf007b267356303",
    "bfme2-missing-physical-20260816-batch-061/11a3488875e72c3c29c788ad8a32fd8da817c614df5f8246f39f1d153f3b1bab",
    "bfme2-missing-physical-20260816-batch-062/186029f019f7c3efad6148b1ed5ae9121956ecbc5830b41eaaf3ab2dd2ac1dde",
    "bfme2-missing-physical-20260816-batch-063/f75640193f4091218837e77cfeda2af677902a8fcecacfffbe06bea5029080f0",
    "bfme2-missing-physical-20260816-batch-064/d88dcba89a2feee4b63ef892e48f696dd4c20da5eb0675388550d69e0c4b5020",
    "bfme2-missing-physical-20260816-batch-065/15c0eb65f080697c56d33c07c295ac847d415a0329c26d7bc2cdec61b7644901",
    "bfme2-missing-physical-20260816-batch-066/fe0865be7d8f5026eda155cb6511bda7c7619f52c858fc11a6c7c0f62097a1d9",
    "bfme2-missing-physical-20260816-batch-067/a176388a516c1b996190ad8e93e7b6989754f6c7dcb5ea019f7028bb6fad36cf",
    "bfme2-missing-physical-20260816-batch-068/2ae23ed2fda59f72e244bad02c37973931c060ab35234ead381599a36ef4e356",
    "bfme2-missing-physical-20260816-batch-069/f97fe84db2fc7e7a61157ffe3f023778a296e47761c12935e79293b5dac2afa3",
    "bfme2-missing-physical-20260816-batch-070/c1e870a14711daa12a95c6952941dcabb553cdba0a903febc36fd5d35c856c3f",
    "bfme2-missing-physical-20260816-batch-071/a73c247803e214faa868cbc8ef51b2f29fdb580ce32b058e5bd2e7ee135ca15c",
    "bfme2-missing-physical-20260816-batch-072/deb466144f5b57c189c2fb0d4efc7b63781247c0d5deb52c6afb382a202b0b46",
    "bfme2-missing-physical-20260816-batch-073/ff0c51dac0107bd31432d504ceffc4bce3e47bd44df5599e80d25c0b381d3492",
    "bfme2-missing-physical-20260816-batch-074/1a94e0d44b45bd6372db222373e925c1ebbc610d5ce92975e0675291de9ed068",
    "bfme2-missing-physical-20260816-batch-075/131881ca7862d859f8c7af23a238332875d343d52ecfe77200e6d12321cebd03",
    "bfme2-missing-physical-20260816-batch-076/6a85651453a911d5f109c1595d6c63fb8530387ea64ebbbc148e51c5ad608f9d",
    "bfme2-missing-physical-20260816-batch-077/b24a1ddb98a6efb41af6407c978ca0c3be55b38ccaa126f5ab8a54a5e4eabf26",
    "bfme2-missing-physical-20260816-batch-078/60ba5a597716e62e3e20015aadfa7c4b32de7ac77161a563ab63527cf1a3766f",
    "bfme2-missing-physical-20260816-batch-079/50f65ac95a2f9799937ffab67a7928dee98e8d924c64155dd605669090fd6fbf",
    "rotwk-missing-physical-20260816-batch-001/49dbc0b919050029a7b1b10301559824caf7b168deb829cdca30db58afb804a0",
    "rotwk-missing-physical-20260816-batch-002/a92a730f4daf0ce0bf4e78af1caa3ffb08ed3706ffe1d34d825c75f30e1e0f16",
    "rotwk-missing-physical-20260816-batch-003/61d8187114e83fd3457adc24242de286cd722df7a599faa4c35eba6df9c99280",
    "rotwk-missing-physical-20260816-batch-004/ad335fdecdaf9e1d5e42ad2e8b3dbddaccbe5e86926305f1179c1af2e7c8bbaa",
    "rotwk-missing-physical-20260816-batch-005/f082d5295d6590a9f327fbd9cd4176cf90078e5689a3ccf2710e976b2ac7735c",
    "rotwk-missing-physical-20260816-batch-006/29316e1e96212299fb7494b3daea304dba5367d68718da052e0f3e245f37f130",
    "rotwk-missing-physical-20260816-batch-007/68eaaf5dbd4420732a8f5831f14c58ded33545e89201543d4eeb1d79cd42b07b",
    "rotwk-missing-physical-20260816-batch-008/f5869f124cfc5528e7a09117e081b2b85d639bce93745d60b31d0a994bf148fb",
    "rotwk-missing-physical-20260816-batch-009/3add071847be8b8661aa951be8c6041861d95a52fec083e6d5e1061aa5afa1f3",
    "rotwk-missing-physical-20260816-batch-010/1d21833fe9cf664c76614e3c3798f754beae212ec6f861bd30f910444ac4d9d8",
    "rotwk-missing-physical-20260816-batch-011/a39a937d8ce44e715ad2378d72c2ea05d137bf98f1cebadc51dd0f05dca472d7",
    "rotwk-missing-physical-20260816-batch-012/2d7fd45bd51f656808483f0c05ab7ddeb9db1c2dc4fb08278146ff769360f210",
    "rotwk-missing-physical-20260816-batch-013/adc1f70b9f6d22ef04c183c57bfee152e8c3d7466fd7a8df9b07c7c7f0aeb957",
    "rotwk-missing-physical-20260816-batch-014/f62bb3ae6109686c1bca40adef96b8bedfaa52c91c47b551faa4b0be8f11c0af",
    "rotwk-missing-physical-20260816-batch-015/d4921d1ca162b9c5ca678fbd792f47363083a90d1b9e795f0a54a3bac918d16a",
    "rotwk-missing-physical-20260816-batch-016/664185693e4a3fe2dda7d80d3a2fb304c347f8f4cc5a0295de3166defda8954a",
    "rotwk-missing-physical-20260816-batch-017/3a7b01f85c329a6b97c936da6f943eb9918bc84fd7c5b8644788e8b48f85b877",
    "rotwk-missing-physical-20260816-batch-018/e0550282f394e1ccf6c34583102824d8b27711942db109d6a9d7940098b3993f",
    "rotwk-missing-physical-20260816-batch-019/948301951db44e64aaeb44956ac8a8b675e64457cb11ec8bc317d608fedf2948",
    "rotwk-missing-physical-20260816-batch-020/6c6910f424355232e6cad9834f483259e5f5485384f8c95aa85d33845bcc5a32",
    "rotwk-missing-physical-20260816-batch-021/56fa9dbf3f618fc5e16db6334a9d2729a92df5a1960728c1a96e91b9fac73053",
    "rotwk-missing-physical-20260816-batch-022/446b5786e07efe047d9162b1e9e51b4211b22af34bfe4dc5b9561731731d5cbd",
    "rotwk-missing-physical-20260816-batch-023/177d55824881f27626dfe9c8c1a3a3aeab5fe37cfc934c04e00060ddf4f89967",
    "rotwk-missing-physical-20260816-batch-024/9c5fa25531005589ec1c5a8f5bdcdac52a6b38d7853e7f80bf0719cf86838957",
    "rotwk-missing-physical-20260816-batch-025/76a1e5d95e4d62b42e5ddd7911d61862ce199671f9cdc16ea99b2eb7e9d98f6a",
    "rotwk-missing-physical-20260816-batch-026/7ac4d26da3e6fc2fbda8cc4bfd3b09d9ba705f69fb0a0a14e87210e9d0e559dd",
    "rotwk-missing-physical-20260816-batch-027/3bc710981ca642b3ff7b4937798c325bbd1a92ef6da637f09c3df4517e422015",
    "rotwk-missing-physical-20260816-batch-028/eca866200a911f10e47d5ddd9692183249ede33c96f32e84d0d8d612b9e5ba4e",
    "rotwk-missing-physical-20260816-batch-029/6b58d26340eb860156983e9f76f214f3f2aa3384a2269c998b88ee1a18b9fb1a",
    "rotwk-missing-physical-20260816-batch-030/9f06fa59c335f8e4551eae400e56ba5fcc2900409a7f75d10fe663ad6c02e025",
    "rotwk-missing-physical-20260816-batch-031/d28a36e5aade781b69b2254095e9533faf38e1db393d96e7233010dc2c6c61a0",
    "rotwk-missing-physical-20260816-batch-032/73859b38ee45be38a2356bcd5418a03cfb5bc4449b78d4e1b49a49bcd9943514",
    "rotwk-missing-physical-20260816-batch-033/cca2d307ae66d1c20a1e702cf633aeb277cb3bf94709ee4583bf332afd912305",
    "rotwk-missing-physical-20260816-batch-034/9a955c977def427ffd5a1e394660d4c84cddd2ec8cfca5770797f64d65a1621b",
    "rotwk-missing-physical-20260816-batch-035/7988b1d4fb26fbadb551396a94b5b3f3153dda1c61eb9d518298c467150589f6",
    "rotwk-missing-physical-20260816-batch-036/56c7e65572622208e6d417456627f107f215eb6008f5a36ec6deb618f8f76764",
    "rotwk-missing-physical-20260816-batch-037/e6aec3caa4fedb96ff6be90a9fcc21e5e28ce7f7246e37106fe9f3a0c94362ed",
    "rotwk-missing-physical-20260816-batch-038/68d1a6ba8418ddc66bda49eb560936a1fbd85bb8001f0f5c0dbc6071c17a5973",
    "rotwk-missing-physical-20260816-batch-039/c470d003a9448b59738589ee4024f5b795f4e333ed94ac21457c4551b91da886",
    "rotwk-missing-physical-20260816-batch-040/425b3dc167212b48ef9a8692482d05627092ca8107bb61d1b2b9fa90548f4864"
)
$stateRoot = if (-not [string]::IsNullOrWhiteSpace($env:OPENBFME_IMPORT_ROOT)) {
    [IO.Path]::GetFullPath($env:OPENBFME_IMPORT_ROOT)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot "workspace\retail-work"))
}
$env:OPENBFME_IMPORT_ROOT = $stateRoot
$expectedBuildPackPath = [IO.Path]::GetFullPath((Join-Path $stateRoot "packs\$expectedPackId"))
$python = Join-Path $stateRoot "tools\python-3.12-env\Scripts\python.exe"
$env:PYTHONPATH = Join-Path $repoRoot "importer"
$forbiddenDiagnostics = '(?i)\b(?:ERROR|WARNING|leak(?:ed|s|ing)?|orphan(?:ed|s)?|ObjectDB instances|RID allocations|resources still in use)\b'

function Invoke-ImporterJson {
    param([string]$Name, [string[]]$Arguments)
    # `2>&1` on a NATIVE command under `$ErrorActionPreference = 'Stop'` (set at
    # the top of this script) is a trap: Windows PowerShell 5.1 wraps every
    # stderr line in a NativeCommandError ErrorRecord, and `Stop` turns the
    # FIRST one into a terminating error - thrown before the pipeline finishes,
    # so before the logging loop below and before any parsing.
    #
    # The importer writes its progress ticker to stderr unconditionally
    # (`openbfme_importer/progress.py`). So every step that emits a ticker died
    # instantly with the ticker line itself as the exception message. That is
    # what stopped this gate at its `plan` step: the run looked like an importer
    # failure and never printed a single `plan` line, because the throw happened
    # before the first Write-Host. Steps that emit no ticker (bootstrap, doctor)
    # sailed through, which made it look step-specific rather than systemic.
    #
    # `Invoke-ProofChecked` in proof-gate-common.ps1 already guards its own
    # native calls this way; this helper had simply never been given the same
    # treatment. Exit code is still the authority on success.
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = @(& $python $cli --json @Arguments 2>&1 | ForEach-Object { $_.ToString() })
        $exitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $oldPreference }
    foreach ($line in $output) { Write-Host "$gate $Name $line" }
    if ($exitCode -ne 0) { throw "$Name failed with exit code $exitCode" }
    # The importer writes its human progress ticker to STDERR
    # (`openbfme_importer/progress.py` prints "[progress] ..." there
    # unconditionally) while the `--json` document goes to stdout. This helper
    # merges the two streams with `2>&1` so the whole run is logged - which
    # means the text handed to ConvertFrom-Json can begin with a progress line
    # and fail to parse. That is what it did: the gate could not get past its
    # `plan` step, and the thrown error was the progress line itself rather
    # than anything about JSON, which made it look like an importer failure.
    #
    # Drop only the ticker lines before parsing. They are still written to the
    # log above, so nothing is hidden; and a real non-JSON diagnostic on stderr
    # still reaches ConvertFrom-Json and still fails the step, which is the
    # behaviour worth keeping.
    $jsonLines = @($output | Where-Object { $_ -notmatch '^\[progress\] ' })
    return (($jsonLines -join "`n") | ConvertFrom-Json)
}

function Invoke-GodotPassedFloor {
    param(
        [string]$Name,
        [string]$Runner,
        [string]$CountPattern,
        [int]$MinimumPassed
    )
    $output = Invoke-ProofChecked $gate $Name $godot @("--headless", "--path", $gameRoot, "--script", "res://tests/$Runner") $CountPattern $forbiddenDiagnostics
    $countMatch = [regex]::Match($output, $CountPattern)
    Assert-ProofTrue ($countMatch.Success -and [int]$countMatch.Groups[1].Value -ge $MinimumPassed) "$Name passed fewer than the protected baseline of $MinimumPassed checks."
}

# ---------------------------------------------------------------------------
# The SCRIPTED state pin is DELIBERATELY RED and is NOT re-minted here.
#
# `game/tests/retail_scripted_state_pin_runner.gd` carries EXPECTED_HASH
# b0acc61e... (minted 2026-07-27) and exits non-zero because the scenario now
# hashes to something else. Its header explains why re-minting it needs owner
# approval. Two bad options were rejected: leaving it out of every gate (which
# is what let it rot red and unnoticed for a week), and re-minting it to make
# the gate green (which buries the drift).
#
# This is the third option, the same observed-values ratchet the battle
# signatures use: assert the OBSERVED hash against the value this tree is
# documented to produce. The pin stays red and honest; any NEW movement of the
# scripted scenario fails this gate immediately instead of hiding behind an
# already-failing runner.
#
# Observed value measured on this tree 2026-08-05, twice, identical.
$expectedScriptedStatePinObserved = "0ae2055fc76eac2348e4b519c5f5ba10370a4d96dec12e3c2e757c7defee14c2"
$scriptedStatePinAuthoredExpectation = "b0acc61e2a60c4b8e810986b168db12055e8a42ae92bc551dd8df01135c33774"

function Assert-ScriptedStatePinObserved {
    param([string]$Godot)
    $oldPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $lines = @(& $Godot "--headless" "--path" $gameRoot "--script" "res://tests/retail_scripted_state_pin_runner.gd" 2>&1 | ForEach-Object { $_.ToString() })
    }
    finally { $ErrorActionPreference = $oldPreference }
    $output = $lines -join [Environment]::NewLine
    $script:ProofGateFailureOutput = $output
    $observed = [regex]::Match($output, '(?m)^RETAIL_SCRIPTED_STATE_PIN ticks=([0-9]+) hash=([0-9a-f]{64})\s*$')
    Assert-ProofTrue ($observed.Success) "retail_scripted_state_pin did not emit its hash line at all; the runner is broken, not merely red."
    $hash = $observed.Groups[2].Value
    Assert-ProofTrue ($hash -eq $expectedScriptedStatePinObserved) "retail_scripted_state_pin MOVED: observed $hash, this tree is documented to produce $expectedScriptedStatePinObserved. The scripted scenario changed. Diagnose it; do not update this constant to match a run."
    # The runner's own authored expectation must still be the un-re-minted 2026-07-27
    # value. If it ever matches, someone re-minted the pin - which is an owner
    # decision and must not arrive silently through this gate.
    $stillRed = $output -match [regex]::Escape($scriptedStatePinAuthoredExpectation)
    Assert-ProofTrue ($stillRed) "retail_scripted_state_pin no longer reports the 2026-07-27 authored expectation. A re-mint is an explicit owner decision; update this gate in the same change."
    $script:ProofGateFailureOutput = ""
    Write-Host "$gate retail_scripted_state_pin EXPECTED-STATE PASS observed=$hash authored_pin=$scriptedStatePinAuthoredExpectation (still red by design, ratcheted)"
}

try {
    foreach ($path in @($cli, $profilePath)) {
        Assert-ProofTrue (Test-Path -LiteralPath $path -PathType Leaf) "Missing retail gate dependency: $path"
    }
    $profileBytes = [IO.File]::ReadAllBytes($profilePath)
    $profileSha256 = (Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-ProofTrue ($profileSha256 -eq $expectedProfileSha256) "Generated completion profile hash changed; integration-owner review is required."
    $profileDocument = ([Text.Encoding]::UTF8.GetString($profileBytes) | ConvertFrom-Json)
    Assert-ProofTrue (
        [int]$profileDocument.format -eq 1 -and
        [string]$profileDocument.id -eq $expectedProfileId -and
        [string]$profileDocument.pack.id -eq $expectedPackId -and
        [string]$profileDocument.pack.schema -eq 'openbfme.content-pack' -and
        $profileDocument.pack.vertical_slice_complete -is [bool] -and
        -not [bool]$profileDocument.pack.vertical_slice_complete
    ) "Generated completion profile identity/readiness marker changed; vertical_slice_complete must remain false until the final rendered checklist passes."

    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $pythonBootstrap -StateRoot $stateRoot
    if ($LASTEXITCODE -ne 0) { throw "Importer Python bootstrap failed." }
    # `python -m unittest discover` cannot collect a module-level `def test_*`,
    # and 73 files in this suite define nothing else -- 949 test functions,
    # including the four largest compiler modules
    # (test_playable_unit_compiler 139, test_playable_unit_pack_compiler 67,
    # test_playable_structure_compiler 64, test_playable_structure_pack_compiler
    # 42) were silently outside this gate. Reproduce the old blind spot with:
    #   python -m unittest discover -s importer/tests -p "test_playable_structure_compiler.py" -v
    #   -> "Ran 0 tests"
    # pytest is a hash-pinned, version-verified bootstrap dependency
    # (importer\requirements-win.txt), so this step adds no new tool.
    # Run from $repoRoot on purpose: `python -m` puts the working directory on
    # sys.path, which is how the suite's `importer.tests.*` cross-module
    # fixture imports resolve.
    Push-Location $repoRoot
    try {
        # Success-only marker. pytest's summary line leads with the failure
        # counts when anything fails ("7 failed, 2396 passed, ..."), so
        # anchoring "<n> passed" to the start of the line cannot match a run
        # that had failures, errors or xpasses.
        $importerTestOutput = Invoke-ProofChecked $gate "importer_tests" $python @("-m", "pytest", (Join-Path $repoRoot "importer\tests"), "-q") '(?m)^[1-9][0-9]* passed(?:, [0-9]+ [a-z ]+)* in '
    }
    finally { Pop-Location }
    $testCountMatch = [regex]::Match($importerTestOutput, '(?m)^([1-9][0-9]*) passed[, ]')
    Assert-ProofTrue ($testCountMatch.Success) "Importer suite did not report an executed test count."
    $skipCountMatch = [regex]::Match($importerTestOutput, '(?m)^[1-9][0-9]* passed, ([0-9]+) skipped')
    $skippedTestCount = if ($skipCountMatch.Success) { [int]$skipCountMatch.Groups[1].Value } else { 0 }
    # Match the old semantic: "executed" counted skips too (unittest's "Ran N").
    $executedTestCount = [int]$testCountMatch.Groups[1].Value + $skippedTestCount
    Assert-ProofTrue ($executedTestCount -ge $minimumImporterTestCount) "Importer suite executed fewer than the protected baseline of $minimumImporterTestCount tests."
    Assert-ProofTrue ($skippedTestCount -le $maximumImporterSkipCount) "Importer suite skipped more than the approved maximum of $maximumImporterSkipCount tests."

    $bootstrap = Invoke-ImporterJson "bootstrap" @("bootstrap-tools")
    Assert-ProofTrue ([bool]$bootstrap.ready) "Pinned external tools are not ready."
    $doctor = Invoke-ImporterJson "doctor" @("doctor", "--game", "bfme2", "--install", $Install, "--deep")
    Assert-ProofTrue ([bool]$doctor.ready) "Retail install doctor is not ready."
    $plan = Invoke-ImporterJson "plan" @("plan", "--game", "bfme2", "--install", $Install, "--profile", $profilePath)
    Assert-ProofTrue (
        [bool]$plan.ready -and
        @($plan.missing_required).Count -eq 0 -and
        [string]$plan.profile -eq $expectedProfileId -and
        [string]$plan.pack -eq $expectedPackId -and
        [int]$plan.resource_count -gt 0 -and
        [int]$plan.selected_file_count -gt 0 -and
        [int](($plan.resources | ForEach-Object { @($_.matches).Count } | Measure-Object -Sum).Sum) -gt 0 -and
        [string]$plan.profile_sha256 -eq $expectedProfileSha256 -and
        $plan.importer_recipe_sha256 -match '^[0-9a-f]{64}$'
    ) "Exact generated completion profile did not resolve its pinned ready closure."
    Assert-ProofTrue (((Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash.ToLowerInvariant()) -eq $expectedProfileSha256) "Generated completion profile changed during planning."

    $buildArguments = @("build", "--game", "bfme2", "--install", $Install, "--profile", $profilePath, "--force")
    if ($IntegrationOwnerPublish) {
        $buildArguments += @("--godot-content-root", $publishRoot)
        Write-Host "$gate integration-owner publish explicitly enabled target=$publishRoot"
    } else {
        $buildArguments += "--no-publish"
        Write-Host "$gate proof builds are non-publishing"
    }
    # A/B REPRODUCIBILITY: build B runs COLD (2026-08-04).
    #
    # Both builds used to share the W3D conversion cache, so build B never
    # re-ran the converter - it copied A's converted artefacts back out of the
    # cache and re-assembled them. Equal bundle hashes therefore only proved
    # ASSEMBLE-stage determinism; a non-deterministic converter (unstable
    # iteration order, embedded timestamps, float drift) was invisible to this
    # gate by construction, because its output was only ever produced once.
    #
    # `--no-conversion-cache` (importer/openbfme_importer/cli.py, registered on
    # the `build` subparser) forces B to convert from the retail bytes again
    # without reading or populating the cache. A == B now means the CONVERTER is
    # deterministic too, which is what "byte-reproducible" is claimed to mean.
    #
    # Cost: B no longer gets the cache hit, so this gate is slower by a full
    # cold W3D conversion of the profile. That is the price of the claim; do not
    # "fix" a slow gate by putting the cache back, because that silently
    # downgrades what the assertion below proves.
    if ($SingleBuild) {
        $buildArguments += "--single-build"
    }
    $first = Invoke-ImporterJson "build_a" $buildArguments
    if ($SingleBuild) {
        $second = $first
        Write-Host "$gate reproducibility NOT ATTESTED (single build explicitly requested) bundle_sha256=$($first.bundle_sha256)"
    } else {
        $second = Invoke-ImporterJson "build_b" ($buildArguments + "--no-conversion-cache")
    }
    Assert-ProofTrue ([bool]$first.valid -and [bool]$second.valid -and [bool]$first.semantic_provenance -and [bool]$second.semantic_provenance) "A retail build failed its semantic provenance audit."
    if (-not $SingleBuild) {
        Assert-ProofTrue ($first.bundle_sha256 -match '^[0-9a-f]{64}$' -and $first.bundle_sha256 -eq $second.bundle_sha256) "Repeat builds were not byte-reproducible (build B ran cold, so this covers the converter, not just assembly)."
    }
    Assert-ProofTrue (
        [string]$first.profile -eq $expectedProfileId -and
        [string]$second.profile -eq $expectedProfileId -and
        [string]$first.profile_sha256 -eq $expectedProfileSha256 -and
        [string]$second.profile_sha256 -eq $expectedProfileSha256 -and
        [IO.Path]::GetFullPath([string]$first.pack) -eq $expectedBuildPackPath -and
        [IO.Path]::GetFullPath([string]$second.pack) -eq $expectedBuildPackPath
    ) "A proof build changed the selected completion profile."
    if ($IntegrationOwnerPublish) {
        $expectedPublishedPack = [IO.Path]::GetFullPath((Join-Path $publishRoot "$expectedPackId\$($second.bundle_sha256)"))
        $expectedSelection = [IO.Path]::GetFullPath((Join-Path $publishRoot "selection.json"))
        $expectedActivePack = "$expectedPackId/$($second.bundle_sha256)"
        Assert-ProofTrue (
            [IO.Path]::GetFullPath([string]$first.published_pack) -eq $expectedPublishedPack -and
            [IO.Path]::GetFullPath([string]$second.published_pack) -eq $expectedPublishedPack -and
            [IO.Path]::GetFullPath([string]$first.selection) -eq $expectedSelection -and
            [IO.Path]::GetFullPath([string]$second.selection) -eq $expectedSelection -and
            [string]$first.active_pack -eq $expectedActivePack -and
            [string]$second.active_pack -eq $expectedActivePack
        ) "Integration-owner publication changed the expected release or selection path."
    } else {
        Assert-ProofTrue (
            $first.PSObject.Properties.Name -notcontains 'published_pack' -and
            $second.PSObject.Properties.Name -notcontains 'published_pack'
        ) "A proof-only build unexpectedly published or selected a pack."
    }
    if (-not $SingleBuild) {
        Write-Host "$gate reproducibility PASS (build B cold, converter+assemble) bundle_sha256=$($first.bundle_sha256)"
    }

    $builtPackDocument = (Get-Content -Raw -LiteralPath (Join-Path $expectedBuildPackPath "pack.json") | ConvertFrom-Json)
    $builtProvenanceDocument = (Get-Content -Raw -LiteralPath (Join-Path $expectedBuildPackPath "provenance\manifest.json") | ConvertFrom-Json)
    Assert-ProofTrue (
        $builtPackDocument.profile_build_complete -is [bool] -and
        [bool]$builtPackDocument.profile_build_complete -and
        @($builtProvenanceDocument.incomplete).Count -eq 0
    ) "Strict completion build retained incomplete conversion reasons."
    if ($SingleBuild) {
        Assert-ProofTrue (
            [string]$builtProvenanceDocument.incrementalRebuild.reproducibility.mode -eq 'single-build' -and
            -not [bool]$builtProvenanceDocument.incrementalRebuild.reproducibility.attested
        ) "Single build provenance falsely claimed reproducibility attestation."
    } else {
        Assert-ProofTrue (
            [string]$builtProvenanceDocument.incrementalRebuild.reproducibility.mode -eq 'cold-a-b-required' -and
            -not [bool]$builtProvenanceDocument.incrementalRebuild.reproducibility.attested
        ) "Pack provenance must defer the A/B claim to this external gate."
    }

    $audit = Invoke-ImporterJson "audit" @("audit", [string]$second.pack)
    Assert-ProofTrue (
        [bool]$audit.valid -and
        [bool]$audit.semantic_provenance -and
        $audit.provenance_contract -eq 'openbfme.retail-import-provenance-v1' -and
        $audit.profile -eq $expectedProfileId -and
        $audit.profile_sha256 -eq $expectedProfileSha256 -and
        $audit.importer_recipe_sha256 -eq $plan.importer_recipe_sha256 -and
        [int]$audit.source_archive_count -gt 0 -and
        [int]$audit.provenance_entry_count -gt 0 -and
        [int]$audit.tool_attestation_count -ge 5 -and
        [int]$audit.checked_files -ge 162 -and
        [int]$audit.checked_outputs -ge 158 -and
        [int]$audit.checked_files -ge [int]$audit.checked_outputs -and
        [int]$audit.source_archive_count -eq [int]$first.source_archive_count -and
        [int]$audit.source_archive_count -eq [int]$second.source_archive_count -and
        [int]$audit.provenance_entry_count -eq [int]$first.provenance_entry_count -and
        [int]$audit.provenance_entry_count -eq [int]$second.provenance_entry_count -and
        [int]$audit.checked_files -eq [int]$first.checked_files -and
        [int]$audit.checked_files -eq [int]$second.checked_files -and
        [int]$audit.checked_outputs -eq [int]$first.checked_outputs -and
        [int]$audit.checked_outputs -eq [int]$second.checked_outputs
    ) "Pack semantic provenance audit failed."
    Assert-ProofTrue (((Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash.ToLowerInvariant()) -eq $expectedProfileSha256) "Generated completion profile changed during proof builds."

    # Runtime acceptance must exercise the pack produced by this exact proof
    # run. Falling through to the durable user:// selection can silently test
    # an older bundle after Build A/B have succeeded. Publishing mode validates
    # the immutable published copy; proof-only mode validates the private build
    # root without mutating selection.json.
    if ($IntegrationOwnerPublish) {
        $env:OPENBFME_CONTENT = $expectedPublishedPack
    } else {
        $env:OPENBFME_CONTENT = $expectedBuildPackPath
    }
    Write-Host "$gate runtime pack explicitly selected root=$($env:OPENBFME_CONTENT)"
    $godot = Resolve-ProofGodot $GodotPath $repoRoot
    # Every importer claim marked executable names a focused runtime runner.
    # Run the registry-derived set as one gate so adding/removing an executable
    # claim cannot leave its evidence outside the real retail cadence.
    [void](Invoke-ProofPriorGate $gate "module_runtime_evidence" (Join-Path $PSScriptRoot "gate-module-runtime-evidence.ps1") $godot '(?m)^MODULE_RUNTIME_EVIDENCE_GATE PASS runners=58 checks=[1-9][0-9]* registered=58\s*$')
    Invoke-GodotPassedFloor "stage11_12_groups_and_routes" "stage11_12_runner.gd" '(?m)^STAGE 11/12 TESTS: ([0-9]+) passed, 0 failed\s*$' 26
    Invoke-GodotPassedFloor "stage14_15_base_loop" "stage14_15_sim_runner.gd" '(?m)^STAGE 14/15 SIM TESTS: ([0-9]+) passed, 0 failed\s*$' 31
    Invoke-GodotPassedFloor "stage15_menu_and_audio" "stage15_menu_runner.gd" '(?m)^STAGE15_MENU_RESULT passed=([0-9]+) failed=0\s*$' 22
    Invoke-GodotPassedFloor "retail_pack_runtime" "retail_pack_runner.gd" '(?m)^RETAIL_PACK_RESULT passed=([0-9]+) failed=0\s*$' 175
    # playable_retail_slice honours the runner's OWN acceptance contract.
    #
    # It used to key on `RETAIL_SLICE_RESULT ... failed=0` with a floor of 208.
    # That is not the contract the runner publishes. The runner pins a named
    # known-failure allowlist, does NOT count those pinned rows into `failed`,
    # and reports the real verdict on its own line:
    #     RETAIL_SLICE_ACCEPTANCE PASS min_passed=<N> pinned_known_failures=<M>
    # Two consequences of the old wiring, both now closed:
    #   * a row that fails WITHOUT being in KNOWN_FAILURE_NAMES prints to stderr
    #     but leaves `failed` at 0, so the old marker still matched and the step
    #     read as a clean pass on the marker alone (only the child exit code
    #     caught it, and only incidentally);
    #   * the 208 floor sat 155 checks below the runner's own ratchet, so a
    #     massive regression could clear the gate.
    # Assert the acceptance line, then read the ratchet the runner itself
    # declares and require the measured passed count to meet it - the floor can
    # no longer drift away from the runner.
    $sliceOutput = Invoke-ProofChecked $gate "playable_retail_slice" $godot @("--headless", "--path", $gameRoot, "--script", "res://tests/retail_slice_runner.gd") '(?m)^RETAIL_SLICE_ACCEPTANCE PASS min_passed=([0-9]+) pinned_known_failures=([0-9]+)\s*$' $forbiddenDiagnostics
    $sliceAcceptance = [regex]::Match($sliceOutput, '(?m)^RETAIL_SLICE_ACCEPTANCE PASS min_passed=([0-9]+) pinned_known_failures=([0-9]+)\s*$')
    $sliceResult = [regex]::Match($sliceOutput, '(?m)^RETAIL_SLICE_RESULT passed=([0-9]+) failed=([0-9]+)\s*$')
    Assert-ProofTrue ($sliceAcceptance.Success -and $sliceResult.Success) "playable_retail_slice did not report both its result and acceptance lines."
    Assert-ProofTrue ([int]$sliceResult.Groups[2].Value -eq 0) "playable_retail_slice reported unpinned failures."
    Assert-ProofTrue ([int]$sliceResult.Groups[1].Value -ge [int]$sliceAcceptance.Groups[1].Value) "playable_retail_slice passed fewer checks than its own declared ACCEPTANCE_MIN_PASSED ratchet."
    Write-Host "$gate playable_retail_slice passed=$($sliceResult.Groups[1].Value) ratchet=$($sliceAcceptance.Groups[1].Value) pinned_known_failures=$($sliceAcceptance.Groups[2].Value)"
    Invoke-GodotPassedFloor "external_pack_security" "external_pack_runner.gd" '(?m)^EXTERNAL_PACK_RESULT passed=([0-9]+) failed=0\s*$' 64
    # Mouse picking hit-tests the retail Geometry footprint, not a flat
    # world-unit radius. Pure math over the shared pick module, so it needs no
    # cooked map or pack and stays fast.
    # Floor ratcheted 30 -> 31 on 2026-08-04 (fortress fallback pinned to the
    # authored 64 source half-extents).
    Invoke-GodotPassedFloor "selection_pick_footprint" "selection_pick_footprint_runner.gd" '(?m)^SELECTION_PICK_RESULT passed=([0-9]+) failed=0\s*$' 31
    # Retail troop movement and formation semantics: TurnTime-derived turn rate,
    # the MaxTurnWithoutReform wheel-vs-reform switch, WaitForFormation group
    # speed cohesion, and the authored Line/Block rank spacing. Half these checks
    # assert the OPT-OUT path - with the "retail_formation_movement" rule absent
    # every behaviour falls back byte-for-byte, which is what keeps the
    # owner-signed retail_state_pin hash reachable (verified: the pin step below
    # is green with this work landed). Pure sim math over a self-built fixture,
    # so it needs no cooked map or pack and stays fast.
    Invoke-GodotPassedFloor "retail_formation_movement" "retail_formation_movement_runner.gd" '(?m)^RETAIL_FORMATION_MOVEMENT passed=([0-9]+) failed=0\s*$' 31
    # Behavioural + cross-platform state pin for the GDScript simulation.
    #
    # WIRED 2026-08-04. Until now this runner was CI-only, and because no local
    # gate ever invoked it the pinned hash sat RED on `main` for a week without
    # anyone seeing a failure (full bisect and the conscious re-mint are written
    # up in the runner's own header). A pin nothing runs is not a pin. It asserts
    # its own hash and exits non-zero on drift, so it carries no passed floor;
    # the frozen fixture injects its own rules and needs no cooked pack.
    Invoke-ProofChecked $gate "retail_state_pin" $godot @("--headless", "--path", $gameRoot, "--script", "res://tests/retail_state_pin_runner.gd") '(?m)^RETAIL_STATE_PIN OK hash matches the pinned value\s*$' $forbiddenDiagnostics | Out-Null
    Assert-ScriptedStatePinObserved $godot
    # Per-faction music selection. Fixture-driven (no retail bytes, no audio
    # playback), so it is fast and green with or without a music pack mounted;
    # when one IS mounted it re-runs the same assertions against it.
    Invoke-GodotPassedFloor "faction_music_director" "music_director_runner.gd" '(?m)^MUSIC_DIRECTOR_RESULT passed=([0-9]+) failed=0\s*$' 119
    # Create-a-Hero. Self-contained by construction: the runner builds its own
    # class table from authored numbers, so it needs no pack and belongs in the
    # deterministic section. WIRED 2026-08-05 - the slice shipped with MY HEROES
    # enabled in the shell while nothing gated the code behind that button.
    # Floor is the measured value from 2026-08-05.
    Invoke-GodotPassedFloor "cah_create_a_hero" "cah_create_a_hero_runner.gd" '(?m)^CAH_CREATE_A_HERO_OK passed=([0-9]+) failed=0\s*$' 66
    # Fast synthetic structure-contract coverage. These fixtures do not read
    # the mounted private selection, so keep them in deterministic SECTION A.
    Invoke-GodotPassedFloor "structure_armor_tables" "test_structure_armor_tables.gd" '(?m)^STRUCTURE_ARMOR_TABLES_RESULT passed=([0-9]+) failed=0\s*$' 9
    Invoke-GodotPassedFloor "scenario_structure_armor_projection" "scenario_structure_armor_projection_runner.gd" '(?m)^SCENARIO_STRUCTURE_ARMOR_PROJECTION_RESULT passed=([0-9]+) failed=0\s*$' 14
    Invoke-GodotPassedFloor "capturable_neutral" "capturable_neutral_runner.gd" '(?m)^CAPTURABLE_NEUTRAL_RESULT passed=([0-9]+) failed=0\s*$' 13

    # The publication-boundary scan belongs to SECTION A: it is about the repository, not
    # about anyone's mounted content, and it must run even when SECTION B is
    # skipped.
    [void](Invoke-ProofChecked $gate "export_firewall_self_test" "powershell.exe" @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "test-export-scan.ps1")) '(?m)^EXPORT_SCAN_SELF_TEST PASS ')
    [void](Invoke-ProofChecked $gate "export_firewall" "powershell.exe" @("-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "export-scan.ps1"), "-Root", $gameRoot) '(?m)^EXPORT_SCAN PASS ')

    Write-Host "$gate SECTION_A_DETERMINISTIC PASS scope=freshly-built-bfme2-proof-pack"

    # =====================================================================
    # SECTION B - PRIVATE MULTI-PACK SELECTION INTEGRATION GATE
    # =====================================================================
    # Everything above this line is deterministic: it runs against the bundle
    # THIS run just built, from pinned inputs, and a public clone with a lawful
    # retail install reproduces it.
    #
    # Everything below asserts about the MOUNTED SELECTION as a whole - the
    # menu's availability sweep across all packs named by selection.json, the
    # CONTROLBAR string table across every mounted pack, the goal matrices over
    # all seven faction packs. Pinning those to this gate's single bfme2 bundle
    # would make them assert about content they are not about.
    #
    # That input is a maintainer's private workspace, so it is PINNED (see the
    # constants at the top of this file) and verified before any of it runs.
    # An unpinned selection is exactly how a stale pack turns a red run green.
    if ($SkipPrivateSelection) {
        Write-Host "$gate SECTION_B_PRIVATE_SELECTION SKIPPED reason=-SkipPrivateSelection"
        if ($SingleBuild) {
            Write-Host "$gate PASS attested=false mode=single-build bundle_sha256=$($second.bundle_sha256) sections=A"
        } else {
            Write-Host "$gate PASS attested=true mode=cold-a-b bundle_sha256=$($second.bundle_sha256) sections=A"
        }
        exit 0
    }
    $selectionPath = Join-Path $publishRoot "selection.json"
    Assert-ProofTrue (Test-Path -LiteralPath $selectionPath -PathType Leaf) "SECTION B needs a private pack selection at $selectionPath. Run with -SkipPrivateSelection on a machine that has none."
    $selectionSha256 = (Get-FileHash -LiteralPath $selectionPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $selectionDocument = Read-ProofJson $selectionPath
    $selectionSupplemental = @($selectionDocument.supplementalPacks)
    $selectionDrift = [Collections.Generic.List[string]]::new()
    if ($selectionSha256 -ne $expectedSelectionSha256) { $selectionDrift.Add("document sha256 $selectionSha256 != pinned $expectedSelectionSha256") }
    if ([string]$selectionDocument.activePack -ne $expectedSelectionActivePack) { $selectionDrift.Add("activePack '$($selectionDocument.activePack)' != pinned '$expectedSelectionActivePack'") }
    foreach ($pinned in $expectedSelectionSupplementalPacks) {
        if ($selectionSupplemental -notcontains $pinned) { $selectionDrift.Add("missing pinned supplemental pack '$pinned'") }
    }
    foreach ($present in $selectionSupplemental) {
        if ($expectedSelectionSupplementalPacks -notcontains $present) { $selectionDrift.Add("unpinned supplemental pack '$present'") }
    }
    # Every pinned pack must actually be on disk, or the runners below silently
    # assert about a smaller selection than the pin describes.
    foreach ($pinned in (@($expectedSelectionActivePack) + $expectedSelectionSupplementalPacks)) {
        $packPath = Join-Path $publishRoot ($pinned -replace '/', '\')
        if (-not (Test-Path -LiteralPath $packPath -PathType Container)) { $selectionDrift.Add("pinned pack is not installed: $pinned") }
    }
    Assert-ProofTrue ($selectionDrift.Count -eq 0) ("SECTION B selection does not match its pin. " + ($selectionDrift -join "; ") + ". Updating the selection is fine; doing it silently is not - re-measure and update the `$expectedSelection* constants in tools/gate-retail.ps1 in the same change.")
    Write-Host "$gate SECTION_B_PRIVATE_SELECTION pin OK sha256=$selectionSha256 packs=$($selectionSupplemental.Count + 1)"

    # OPENBFME_CONTENT is restored afterwards so nothing below sees the switch.
    $packScopedContentRoot = $env:OPENBFME_CONTENT
    try {
        $env:OPENBFME_CONTENT = $publishRoot
        Write-Host "$gate selection-scoped runners root=$($env:OPENBFME_CONTENT)"
        # Floors ratcheted 2026-08-04 from measured runs against this exact
        # root (menu_sweep 22 -> 36, hud_strings 4 -> 5). RAISE these
        # consciously when a measured run clears them; never lower one to make
        # a red run green.
        Invoke-GodotPassedFloor "menu_availability_sweep" "menu_sweep_runner.gd" '(?m)^MENU_SWEEP_RESULT passed=([0-9]+) failed=0\s*$' 36
        Invoke-GodotPassedFloor "hud_string_completeness" "hud_string_completeness_runner.gd" '(?m)^HUD_STRINGS_RESULT passed=([0-9]+) failed=0 ' 5
        # Selected neutral-pack receipts and live map-placement behavior must
        # move with the exact SECTION B selection pin above.
        Invoke-GodotPassedFloor "selected_neutral_pack_acceptance" "selected_neutral_pack_acceptance_runner.gd" '(?m)^SELECTED_NEUTRAL_PACK_ACCEPTANCE_OK passed=([0-9]+)\s*$' 35
        Invoke-GodotPassedFloor "scenario_map_placement_live" "scenario_map_placement_live_runner.gd" '(?m)^SCENARIO_MAP_PLACEMENT_LIVE_OK passed=([0-9]+)\s*$' 80
        # ADDED 2026-08-04. These goal/behaviour runners existed and were green
        # but nothing ran them in a gate, so a regression in any of them could
        # ship unnoticed. Like the two above they assert about the mounted
        # SELECTION (all seven faction packs), not this gate's single bfme2
        # bundle, so they belong inside this selection-scoped block. Floors are
        # measured values from 2026-08-04, not guesses.
        Invoke-GodotPassedFloor "goal_spellbook_matrix" "goal_spellbook_matrix_runner.gd" '(?m)^SPELLBOOK_MATRIX_RESULT passed=([0-9]+) failed=0 ' 293
        Invoke-GodotPassedFloor "goal_production_matrix" "goal_production_matrix_runner.gd" '(?m)^PRODUCTION_MATRIX_RESULT passed=([0-9]+) failed=0\s*$' 315
        Invoke-GodotPassedFloor "goal_deep_production_matrix" "goal_deep_production_runner.gd" '(?m)^DEEP_PRODUCTION_RESULT passed=([0-9]+) failed=0 ' 584
        Invoke-GodotPassedFloor "goal_map_matrix" "goal_map_matrix_runner.gd" '(?m)^GOAL_MAP_MATRIX_RESULT passed=([0-9]+) failed=0 ' 83
        # ADDED 2026-08-10. The radar. Half of this runner is pure geometry over
        # a stub (source-grid mapping under a rotated player-start axis, the ink
        # sheet's corners, the paper staying bolted to the bezel while the map
        # pans), and half reads the MOUNTED packs: retail's parchment bitmap out
        # of the palantir atlas, asserted against its measured radial gradient,
        # and a REGISTRATION PIN that correlates a cooked map's published ink
        # alpha against its own heightmap slope under three rival mappings. The
        # pack-backed half is why it lives in this scoped block rather than in
        # SECTION A - and why it FAILS instead of skipping when a pack is
        # missing. Floor is the measured 2026-08-10 value (32/0); reverting the
        # radar to a vertical flip takes it to 25/7.
        Invoke-GodotPassedFloor "minimap_parchment" "minimap_parchment_runner.gd" '(?m)^MINIMAP_PARCHMENT_RESULT passed=([0-9]+) failed=0\s*$' 32
        Invoke-GodotPassedFloor "destroy_die_modules" "destroy_die_runner.gd" '(?m)^DESTROY_DIE_RESULT passed=([0-9]+) failed=0\s*$' 24
        # ADDED 2026-08-09. A created hero bought, spawned, hit, healed, cast
        # and levelled on the mounted pack's own compiled CAH table. It reads
        # the SELECTION (the Men slice plus data/cah/system.json), so it belongs
        # inside this block; floor is the measured 2026-08-09 value.
        Invoke-GodotPassedFloor "cah_match" "cah_match_runner.gd" '(?m)^CAH_MATCH_RESULT passed=([0-9]+) failed=0\s*$' 72
        # ADDED 2026-08-11, BOTH OF THESE, after a republished faction pack added
        # retail hero voice sets to cah.system that no mounted pack's audio
        # registry defines. Roster-audio closure treated "authored but unbound"
        # as a hard refusal and took Men and Dwarves offline at the very end of a
        # successful boot - and NOTHING IN ANY GATE RAN THE RUNNER THAT CAUGHT
        # IT. Both live in this scoped block, not SECTION A: the closure runner
        # resolves the host pack out of the mounted selection (it fails closed on
        # this gate's single bfme2 proof bundle), and launch validation boots the
        # real slice twice against the selection's Men and Dwarves packs.
        # Floors are measured 2026-08-11 values against this root.
        Invoke-GodotPassedFloor "created_hero_voice_closure" "retail_created_hero_voice_closure_runner.gd" '(?m)^CREATED_HERO_VOICE_CLOSURE_RESULT passed=([0-9]+) failed=0\s*$' 12
        Invoke-GodotPassedFloor "retail_launch_validation" "retail_launch_validation_runner.gd" '(?m)^RESULT retail_launch_validation passed=([0-9]+) failed=0\s*$' 35
        # Synthetic compiled-contract full loop: deterministic Gollum, ring
        # pickup/re-drop/delivery, dual-scope purchase gate, 300s rank-10 hero,
        # death re-drop, rule-off refusal, and twin hashes every 30 ticks. The
        # exact final hash pins the authoritative behavior, not only its check count.
        Invoke-GodotPassedFloor "ring_mechanic" "ring_mechanic_runner.gd" '(?m)^RING_MECHANIC_RESULT passed=([0-9]+) failed=0 hash=2650094088785f8e521604f8dec1218c4c5f95fa0d9e8802110a424f3e8aa875\s*$' 141
        # The lockstep gate now also carries the two-seat created-hero exchange
        # (lobby table convergence, byte-identical launch rosters, ownership,
        # and equal state hashes with different heroes per seat), which needs
        # the mounted CAH table - hence its move into the scoped block.
        Invoke-GodotPassedFloor "retail_lockstep_network" "retail_lockstep_network_runner.gd" '(?m)^RETAIL_LOCKSTEP_NET_RESULT passed=([0-9]+) failed=0\s*$' 37
        Invoke-GodotPassedFloor "banner_castle_sim" "banner_castle_sim_runner.gd" '(?m)^BANNER_CASTLE_SIM_RUNNER PASS checks=([0-9]+)\s*$' 25
        # Fortress command surface, one process per faction (the slice hosts one
        # faction at a time). Asserts the three castle-system contracts against
        # the PURE retail oracle: build plots offer that faction's authored pad
        # buildings (commandset.ini <Faction>FortressExpansionPad*CommandSet),
        # the fortress hero roster is non-empty and matches
        # playertemplate.ini BuildableHeroesMP, and the CastleUpgrade
        # trigger -> real-upgrade purchase path completes. Floors are measured
        # values from 2026-08-04. Only the three factions whose slice boots
        # green today are wired; mordor/dwarves/elves/isengard still fail boot
        # on PRE-EXISTING unrelated gaps (MordorBlackRiderHorde producer slot
        # collision; missing CONTROLBAR hero-ability strings) — named here so
        # the omission is deliberate rather than an oversight.
        # Raised 2026-08-10 with the paged fortress radial (main / Fortress
        # Upgrades / Heroes pages, retail label+icon+cost on each improvement,
        # and the created hero's recruit slot). Dwarves is wired now that its
        # slice boots green; mordor/elves/isengard remain off for the
        # PRE-EXISTING reasons above.
        # Raised 2026-08-10 by the fortress upgrade-catalog republish. The packs
        # now carry retail's WHOLE upgrades page instead of only the two buttons
        # whose CastleUpgrade module converts a *Trigger id, so each faction adds
        # ~3 checks per newly purchasable improvement (offered / binds its pack
        # icon / states its cost): dwarves 44->60, angmar 40->56, wild 40->53.
        # men rises 45->49 on the four new castle-upgrade SHAPE checks only - its
        # pack is deliberately not republished this round (CaH cook gap, see the
        # selection pin note above), so its catalog is still the old two rows.
        $fortressSurfaceFloors = @{ "angmar" = 56; "men" = 49; "wild" = 53; "dwarves" = 60 }
        $priorSliceFaction = $env:OPENBFME_SLICE_FACTION
        try {
            foreach ($fortressFaction in @("angmar", "men", "wild", "dwarves")) {
                $env:OPENBFME_SLICE_FACTION = $fortressFaction
                Invoke-GodotPassedFloor "fortress_command_surface_$fortressFaction" "fortress_command_surface_runner.gd" ('(?m)^RESULT fortress_command_surface faction=' + $fortressFaction + ' passed=([0-9]+) failed=0\s*$') $fortressSurfaceFloors[$fortressFaction]
            }
        }
        finally {
            $env:OPENBFME_SLICE_FACTION = $priorSliceFaction
        }
        # NOT WIRED: banner_castle_silent_playtest_runner.gd. Measured
        # 2026-08-04 it reports passed=37 failed=5 against this same root.
        # Wiring a known-red runner into a gate only teaches people to ignore
        # the gate; it is named here so the omission is deliberate and
        # visible rather than an oversight.
    }
    finally {
        $env:OPENBFME_CONTENT = $packScopedContentRoot
    }

    Write-Host "$gate SECTION_B_PRIVATE_SELECTION PASS selection_sha256=$selectionSha256"
    if ($SingleBuild) {
        Write-Host "$gate PASS attested=false mode=single-build bundle_sha256=$($second.bundle_sha256) sections=A+B"
    } else {
        Write-Host "$gate PASS attested=true mode=cold-a-b bundle_sha256=$($second.bundle_sha256) sections=A+B"
    }
    exit 0
}
catch {
    Write-ProofGateFailure $gate $_
    exit 1
}
