> Promoted from `workspace/scratch/phase2-report-20260816.md` on 2026-08-17 (stage-2a triage); absolute machine paths relativized to current workspace/ layout.

# Phase 2 missing physical asset cook report (2026-08-16)

## Outcome

- Completed batches: 79 (RotWK 40, BFME2 39).
- Physical converter output files: 38,507.
- Publication destinations verified: 158 (79 bundles in each of two roots).
- Selection SHA-256 stayed unchanged: `e3cf65197b36fc855f852f18ce7e53a698fca34ff004d29e0b25d31f0326285c`.
- Current selected-pack address gate: `PACK_ADDRESS_CHECK PASS packs=42 roots=2`.

## Output counts

| Edition | W3D GLB | Texture PNG | Audio WAV | Standalone animation | Effects | UI | Misc MAP | Total physical outputs |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| rotwk | 2,645 | 2,966 | 13,728 | 0 | 0 | 0 | 1 | 19,340 |
| bfme2 | 2,284 | 3,524 | 13,358 | 0 | 0 | 0 | 1 | 19,167 |

Animations converted through authored W3D recipes are contained in GLB outputs; they are not separately countable files. The existing audio converter emits WAV and texture converter emits PNG, so the packs preserve those official lane outputs rather than renaming them to OGG/GLB placeholders.

## Honest cut line

| Edition | Catalog-backed missing paths | Safe profile coverage | Unconvertible physical paths | Unresolved semantic tokens |
|---|---:|---:|---:|---:|
| RotWK | 19,475 | 16,652 | 2,823 | 9,512 |
| BFME2 | 22,965 | 18,588 | 4,377 | 10,123 |

Full receipt rows, tokens, reference types, and converter reasons: `workspace/scratch/phase2-unconvertible-assets-20260816.json` (SHA-256 `1ca7bf7510633b0e4de1d0de53a34f0ac3170462e92984a3108915921bee1593`).

## New pack addresses

- `rotwk-missing-physical-20260816-batch-001/49dbc0b919050029a7b1b10301559824caf7b168deb829cdca30db58afb804a0`
- `rotwk-missing-physical-20260816-batch-002/a92a730f4daf0ce0bf4e78af1caa3ffb08ed3706ffe1d34d825c75f30e1e0f16`
- `rotwk-missing-physical-20260816-batch-003/61d8187114e83fd3457adc24242de286cd722df7a599faa4c35eba6df9c99280`
- `rotwk-missing-physical-20260816-batch-004/ad335fdecdaf9e1d5e42ad2e8b3dbddaccbe5e86926305f1179c1af2e7c8bbaa`
- `rotwk-missing-physical-20260816-batch-005/f082d5295d6590a9f327fbd9cd4176cf90078e5689a3ccf2710e976b2ac7735c`
- `rotwk-missing-physical-20260816-batch-006/29316e1e96212299fb7494b3daea304dba5367d68718da052e0f3e245f37f130`
- `rotwk-missing-physical-20260816-batch-007/68eaaf5dbd4420732a8f5831f14c58ded33545e89201543d4eeb1d79cd42b07b`
- `rotwk-missing-physical-20260816-batch-008/f5869f124cfc5528e7a09117e081b2b85d639bce93745d60b31d0a994bf148fb`
- `rotwk-missing-physical-20260816-batch-009/3add071847be8b8661aa951be8c6041861d95a52fec083e6d5e1061aa5afa1f3`
- `rotwk-missing-physical-20260816-batch-010/1d21833fe9cf664c76614e3c3798f754beae212ec6f861bd30f910444ac4d9d8`
- `rotwk-missing-physical-20260816-batch-011/a39a937d8ce44e715ad2378d72c2ea05d137bf98f1cebadc51dd0f05dca472d7`
- `rotwk-missing-physical-20260816-batch-012/2d7fd45bd51f656808483f0c05ab7ddeb9db1c2dc4fb08278146ff769360f210`
- `rotwk-missing-physical-20260816-batch-013/adc1f70b9f6d22ef04c183c57bfee152e8c3d7466fd7a8df9b07c7c7f0aeb957`
- `rotwk-missing-physical-20260816-batch-014/f62bb3ae6109686c1bca40adef96b8bedfaa52c91c47b551faa4b0be8f11c0af`
- `rotwk-missing-physical-20260816-batch-015/d4921d1ca162b9c5ca678fbd792f47363083a90d1b9e795f0a54a3bac918d16a`
- `rotwk-missing-physical-20260816-batch-016/664185693e4a3fe2dda7d80d3a2fb304c347f8f4cc5a0295de3166defda8954a`
- `rotwk-missing-physical-20260816-batch-017/3a7b01f85c329a6b97c936da6f943eb9918bc84fd7c5b8644788e8b48f85b877`
- `rotwk-missing-physical-20260816-batch-018/e0550282f394e1ccf6c34583102824d8b27711942db109d6a9d7940098b3993f`
- `rotwk-missing-physical-20260816-batch-019/948301951db44e64aaeb44956ac8a8b675e64457cb11ec8bc317d608fedf2948`
- `rotwk-missing-physical-20260816-batch-020/6c6910f424355232e6cad9834f483259e5f5485384f8c95aa85d33845bcc5a32`
- `rotwk-missing-physical-20260816-batch-021/56fa9dbf3f618fc5e16db6334a9d2729a92df5a1960728c1a96e91b9fac73053`
- `rotwk-missing-physical-20260816-batch-022/446b5786e07efe047d9162b1e9e51b4211b22af34bfe4dc5b9561731731d5cbd`
- `rotwk-missing-physical-20260816-batch-023/177d55824881f27626dfe9c8c1a3a3aeab5fe37cfc934c04e00060ddf4f89967`
- `rotwk-missing-physical-20260816-batch-024/9c5fa25531005589ec1c5a8f5bdcdac52a6b38d7853e7f80bf0719cf86838957`
- `rotwk-missing-physical-20260816-batch-025/76a1e5d95e4d62b42e5ddd7911d61862ce199671f9cdc16ea99b2eb7e9d98f6a`
- `rotwk-missing-physical-20260816-batch-026/7ac4d26da3e6fc2fbda8cc4bfd3b09d9ba705f69fb0a0a14e87210e9d0e559dd`
- `rotwk-missing-physical-20260816-batch-027/3bc710981ca642b3ff7b4937798c325bbd1a92ef6da637f09c3df4517e422015`
- `rotwk-missing-physical-20260816-batch-028/eca866200a911f10e47d5ddd9692183249ede33c96f32e84d0d8d612b9e5ba4e`
- `rotwk-missing-physical-20260816-batch-029/6b58d26340eb860156983e9f76f214f3f2aa3384a2269c998b88ee1a18b9fb1a`
- `rotwk-missing-physical-20260816-batch-030/9f06fa59c335f8e4551eae400e56ba5fcc2900409a7f75d10fe663ad6c02e025`
- `rotwk-missing-physical-20260816-batch-031/d28a36e5aade781b69b2254095e9533faf38e1db393d96e7233010dc2c6c61a0`
- `rotwk-missing-physical-20260816-batch-032/73859b38ee45be38a2356bcd5418a03cfb5bc4449b78d4e1b49a49bcd9943514`
- `rotwk-missing-physical-20260816-batch-033/cca2d307ae66d1c20a1e702cf633aeb277cb3bf94709ee4583bf332afd912305`
- `rotwk-missing-physical-20260816-batch-034/9a955c977def427ffd5a1e394660d4c84cddd2ec8cfca5770797f64d65a1621b`
- `rotwk-missing-physical-20260816-batch-035/7988b1d4fb26fbadb551396a94b5b3f3153dda1c61eb9d518298c467150589f6`
- `rotwk-missing-physical-20260816-batch-036/56c7e65572622208e6d417456627f107f215eb6008f5a36ec6deb618f8f76764`
- `rotwk-missing-physical-20260816-batch-037/e6aec3caa4fedb96ff6be90a9fcc21e5e28ce7f7246e37106fe9f3a0c94362ed`
- `rotwk-missing-physical-20260816-batch-038/68d1a6ba8418ddc66bda49eb560936a1fbd85bb8001f0f5c0dbc6071c17a5973`
- `rotwk-missing-physical-20260816-batch-039/c470d003a9448b59738589ee4024f5b795f4e333ed94ac21457c4551b91da886`
- `rotwk-missing-physical-20260816-batch-040/425b3dc167212b48ef9a8692482d05627092ca8107bb61d1b2b9fa90548f4864`
- `bfme2-missing-physical-20260816-batch-041/f72bc1c5c6f09a68f74651b42a453af4b48ffa2c7bd1bba985a895b041fdd591`
- `bfme2-missing-physical-20260816-batch-042/6d37895946a556bb8ae5f764b60292da8dcebd8ad41058ba7473b96e8f2fcf42`
- `bfme2-missing-physical-20260816-batch-043/779f32f33f55ec48df138a90b70b7a5ffbf6093d3a5d9640c344b182550f4148`
- `bfme2-missing-physical-20260816-batch-044/ca78e9d87f32856ef5cbfcd409fadffd9ec14e1e52118c6b2ab6186b36fdec47`
- `bfme2-missing-physical-20260816-batch-045/db3653c7cb92bfbb79cdd2519059ed07b3cf3347eef0e79fa29896bb3e5cfef0`
- `bfme2-missing-physical-20260816-batch-046/fec6e462c01ff93353591db16fe5524f3e0b2ab0d81c854cac79ff4801520455`
- `bfme2-missing-physical-20260816-batch-047/6f2d1a2449c10e861023efd45662b88e754879194c3db2be70be33e32bc7745f`
- `bfme2-missing-physical-20260816-batch-048/c6c0d0aafbd21a6b549512e59a446dbce605fbc39646fa8e6ee297a8bcbf9340`
- `bfme2-missing-physical-20260816-batch-049/48f50dd4f5cf41f39e1584d2556723686e42a4cba9829479f5bff10fe3a51bb7`
- `bfme2-missing-physical-20260816-batch-050/90eee00bb257ab44b4070db4c959d6ec1512c6884634daa465ad3c58b1249ffe`
- `bfme2-missing-physical-20260816-batch-051/a3156803e30fd22d551d36f8a13bb700b75e6aefc7d567e4f9c8d6454bead1aa`
- `bfme2-missing-physical-20260816-batch-052/97523915de161445145e69f99b8389eeb02d93264568a60d7e21ffda0ceb84d1`
- `bfme2-missing-physical-20260816-batch-053/b51de7f400621ff635cc9f47d1fb32312314cc91ee282d728efedd636035c545`
- `bfme2-missing-physical-20260816-batch-054/77b3525a904847013f8c00d2f46fcbda5f0f3aa9a43c924b9c80cb7949591ff8`
- `bfme2-missing-physical-20260816-batch-055/b1db202983558c5c59330ab531bf6dac72af00e6ca20d93c8352a0bc72d4ae7b`
- `bfme2-missing-physical-20260816-batch-056/923a4e674c63d2a4ce9119e19483eadc7722600bf923d8a292b7b9bb87fdff9e`
- `bfme2-missing-physical-20260816-batch-057/9a727662c191847352e5df511f94992286f236c6feaefdf9a290b5401c2015ff`
- `bfme2-missing-physical-20260816-batch-058/f6ff83affe40a296e0351bd4905c2f756d7529862068e57083c2c9f55fb5fbb5`
- `bfme2-missing-physical-20260816-batch-059/96f682fc916261da6d1c8e4d0c01792f63d701aae4afd7d9d39af2f3f316235b`
- `bfme2-missing-physical-20260816-batch-060/378d8619c8bb6d7d155b5062964c75d038484944a59734f13cf007b267356303`
- `bfme2-missing-physical-20260816-batch-061/11a3488875e72c3c29c788ad8a32fd8da817c614df5f8246f39f1d153f3b1bab`
- `bfme2-missing-physical-20260816-batch-062/186029f019f7c3efad6148b1ed5ae9121956ecbc5830b41eaaf3ab2dd2ac1dde`
- `bfme2-missing-physical-20260816-batch-063/f75640193f4091218837e77cfeda2af677902a8fcecacfffbe06bea5029080f0`
- `bfme2-missing-physical-20260816-batch-064/d88dcba89a2feee4b63ef892e48f696dd4c20da5eb0675388550d69e0c4b5020`
- `bfme2-missing-physical-20260816-batch-065/15c0eb65f080697c56d33c07c295ac847d415a0329c26d7bc2cdec61b7644901`
- `bfme2-missing-physical-20260816-batch-066/fe0865be7d8f5026eda155cb6511bda7c7619f52c858fc11a6c7c0f62097a1d9`
- `bfme2-missing-physical-20260816-batch-067/a176388a516c1b996190ad8e93e7b6989754f6c7dcb5ea019f7028bb6fad36cf`
- `bfme2-missing-physical-20260816-batch-068/2ae23ed2fda59f72e244bad02c37973931c060ab35234ead381599a36ef4e356`
- `bfme2-missing-physical-20260816-batch-069/f97fe84db2fc7e7a61157ffe3f023778a296e47761c12935e79293b5dac2afa3`
- `bfme2-missing-physical-20260816-batch-070/c1e870a14711daa12a95c6952941dcabb553cdba0a903febc36fd5d35c856c3f`
- `bfme2-missing-physical-20260816-batch-071/a73c247803e214faa868cbc8ef51b2f29fdb580ce32b058e5bd2e7ee135ca15c`
- `bfme2-missing-physical-20260816-batch-072/deb466144f5b57c189c2fb0d4efc7b63781247c0d5deb52c6afb382a202b0b46`
- `bfme2-missing-physical-20260816-batch-073/ff0c51dac0107bd31432d504ceffc4bce3e47bd44df5599e80d25c0b381d3492`
- `bfme2-missing-physical-20260816-batch-074/1a94e0d44b45bd6372db222373e925c1ebbc610d5ce92975e0675291de9ed068`
- `bfme2-missing-physical-20260816-batch-075/131881ca7862d859f8c7af23a238332875d343d52ecfe77200e6d12321cebd03`
- `bfme2-missing-physical-20260816-batch-076/6a85651453a911d5f109c1595d6c63fb8530387ea64ebbbc148e51c5ad608f9d`
- `bfme2-missing-physical-20260816-batch-077/b24a1ddb98a6efb41af6407c978ca0c3be55b38ccaa126f5ab8a54a5e4eabf26`
- `bfme2-missing-physical-20260816-batch-078/60ba5a597716e62e3e20015aadfa7c4b32de7ac77161a563ab63527cf1a3766f`
- `bfme2-missing-physical-20260816-batch-079/50f65ac95a2f9799937ffab67a7928dee98e8d924c64155dd605669090fd6fbf`

## Activation (not executed)

Exact PowerShell invocation preserving the existing active pack and supplements, then appending all 79 batches: `workspace/scratch/phase2-apply-selection-transaction-20260816.ps1`.

The illustrative `tools/apply-selection-transaction.py --add-pack` command does not exist in this checkout. The generated command uses the real `openbfme_import.py apply-selection-transaction` interface and supplies the complete replacement selection document.

## Verification

- Focused converter regression: `15 passed, 1 skipped in 5.49s`.
- Full pinned importer lane: `9 failed, 3686 passed, 17 skipped, 2 warnings, 975 subtests passed in 2126.44s (0:35:26)`.
- Full-lane log: `.private/scratch/phase2-importer-tests.log`.
- The nine full-lane failures are listed in that log and cover current neutral catalogs, module census regeneration, official-map/inheritance corpora, the selected Eowyn pack, and W3D census provenance. The focused media-attestation regression is green.
- Tracked fix commit: `6487e6c` (`Fix media-only converter tool attestation`).
