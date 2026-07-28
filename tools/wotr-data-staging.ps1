# WAR OF THE RING DATA: what a bundle must carry, DERIVED rather than restated.
#
# WHY THIS FILE EXISTS
#   Three times now this project has shipped a feature and left its data behind.
#   First the living-world document (WAR OF THE RING read UNAVAILABLE), then the
#   converted 3D map, then four bundles at once - filled territories, the 3D
#   army/building/plot models, the region portraits and the setup-screen string
#   table - all converted, all sitting on the author's disk, none of them in the
#   release. Each time the fix was to add the missing artefact to a hardcoded
#   list, and each time the list was correct until the next bundle landed.
#
#   So the list is no longer written down. It is COMPUTED, from the two places
#   that cannot lie about it:
#
#     1. THE LOADERS. game/src/wotr/*.gd is the authority on what the running
#        game looks for. Every loader declares the environment variable it reads
#        (BUNDLE_ENV / DOCUMENT_ENV), the schema it accepts (SCHEMA), and - when
#        it searches mounted packs itself - the pack-relative path it searches
#        (PACK_BUNDLE_RELATIVE / PACK_DOCUMENT_RELATIVE). All of that is read out
#        of the source; none of it is copied here.
#
#     2. THE WORKSPACE. Bundles are found BY SCHEMA, not by path. Nothing here
#        knows that the marker models happen to live in
#        .private/retail-work/livingworld-markers; it knows that a loader accepts
#        `openbfme.living-world-markers` and goes looking for a document that
#        declares it.
#
#   What is left to declare by hand is only what neither source can state: for
#   the loaders that are found "beside" another bundle rather than at a path of
#   their own, WHICH bundle hosts them; and, for every bundle, what the player
#   loses without it. Both are checked against the loader census, so:
#
#     * a loader with no rule REFUSES the build (a sixth bundle cannot be missed)
#     * a rule with no loader REFUSES the build (a deleted loader cannot leave a
#       rule behind that quietly stages dead weight)
#
#   That is the difference between this and the three fixes before it. Adding a
#   seventh loader to game/src/wotr does not make this file wrong; it makes the
#   build stop and say so.

Set-StrictMode -Version Latest

# Where the authority lives. Repo-relative, forward slashes.
$script:WotrLoaderDirectory = 'game/src/wotr'

# The workspace subtrees searched for converted bundles, repo-relative. Scanned
# to a depth of two, which is the layout every converter writes.
$script:WotrWorkspaceRelative = '.private/retail-work'

# A .json bigger than this is not a bundle manifest or a living-world document -
# it is a catalog or an asset census - and is skipped rather than read. This can
# only cause a bundle to be reported ABSENT (which refuses a release), never to
# be silently omitted from one.
$script:WotrWorkspaceMaxScanBytes = 8MB

# THE ONLY HAND-WRITTEN TABLE, and it deliberately carries no paths.
#
#   env    the loader constant this row answers for. Must match a loader in
#          game/src/wotr; the census refuses in both directions.
#   schema only for a loader that declares none of its own (wotr_session.gd
#          reads the document through wotr_world.gd). Otherwise left empty and
#          taken from the loader.
#   host   for a loader that is found BESIDE another bundle rather than at a
#          pack-relative path of its own: the env of the bundle that hosts it.
#          This is the one fact the sources cannot state - wotr_screen.gd builds
#          those roots at runtime - so it is declared, and a loader that
#          declares neither a pack-relative path nor a host refuses.
#   loses  what the player gets instead, in their words. Printed when the bundle
#          is absent, so "missing" is never just a filename.
$script:WotrStagingRules = @(
    [pscustomobject]@{
        env    = 'OPENBFME_LIVING_WORLD_DOC'
        schema = 'openbfme.living-world'
        host   = ''
        loses  = 'War of the Ring does not open at all - the menu entry reads (UNAVAILABLE)'
    }
    [pscustomobject]@{
        env    = 'OPENBFME_LIVING_MAP'
        schema = ''
        host   = ''
        loses  = "retail's 3D Middle-earth - the strategic layer falls back to the flat 2D graph"
    }
    [pscustomobject]@{
        env    = 'OPENBFME_LIVING_MAP_REGIONS'
        schema = ''
        host   = ''
        loses  = "filled territories - regions are drawn as markers instead of retail's own lmr_fill/lmr_border shapes"
    }
    [pscustomobject]@{
        env    = 'OPENBFME_LIVING_WORLD_MARKERS'
        schema = ''
        host   = 'OPENBFME_LIVING_MAP_REGIONS'
        loses  = "retail's 3D army, building and build-plot models - army stacks become flat portrait plates and plots flat rings"
    }
    [pscustomobject]@{
        # THIS RULE EXISTS BECAUSE THE GUARD CAUGHT ITS ABSENCE IN PRODUCTION,
        # one commit after the guard was written: the release refused by name
        # the moment wotr_autoresolve.gd landed. Adding the rule is the intended
        # response, not a workaround.
        #
        # Note the honest `loses`: the auto-resolve model is deliberately wired
        # to NOTHING (56ffb5c), so its absence costs the player nothing TODAY.
        # It ships anyway because the loader is real and the data is 655 KB -
        # when auto-resolve is authorised, the tables are already in the field
        # rather than needing a re-release. Overstating the loss here would be
        # the same dishonesty as understating one.
        env    = 'OPENBFME_LIVING_WORLD_AUTORESOLVE'
        schema = 'openbfme.living-world-autoresolve'
        host   = 'OPENBFME_LIVING_MAP_REGIONS'
        loses  = "nothing visible yet - retail's auto-resolve tables are converted but deliberately not wired into battle resolution, so this ships ready rather than needed"
    }
    [pscustomobject]@{
        env    = 'OPENBFME_LIVING_WORLD_REGION_IMAGES'
        schema = ''
        host   = 'OPENBFME_LIVING_MAP_REGIONS'
        loses  = "the region and fortress portraits on the region card - the card names a region without showing it"
    }
    [pscustomobject]@{
        env    = 'OPENBFME_LIVING_WORLD_UI'
        schema = ''
        host   = 'OPENBFME_LIVING_MAP_REGIONS'
        loses  = "retail's living-world UI art - faction banners, building and build-plot icons and the strategic chrome"
    }
    [pscustomobject]@{
        env    = 'OPENBFME_LIVING_WORLD_STRINGS'
        schema = ''
        host   = 'OPENBFME_LIVING_MAP_REGIONS'
        loses  = "retail's names for regions, armies and scenarios - the map shows raw ids like LW:DisplayName"
    }
    [pscustomobject]@{
        env    = 'OPENBFME_WOTR_SETUP_STRINGS'
        schema = ''
        host   = 'OPENBFME_LIVING_MAP_REGIONS'
        loses  = 'the GAME SETUP screen headings, faction names and button captions - it shows retail keys instead'
    }
    [pscustomobject]@{
        env    = 'OPENBFME_LIVING_WORLD_MACROS'
        schema = ''
        host   = 'OPENBFME_LIVING_MAP_REGIONS'
        loses  = "retail's gamedata #define table - numbers the strategic rules quote resolve to their macro names"
    }
)


function Get-WotrLoaderCensus {
    <#
    .SYNOPSIS
        Every War of the Ring loader in this checkout, read out of its source.
    .DESCRIPTION
        A loader is a .gd in game/src/wotr that declares BUNDLE_ENV or
        DOCUMENT_ENV: that constant is the loader saying "there is a converted
        artefact I need". Everything else - the schema it will accept and the
        pack-relative path it searches - is read from the same file, so the
        bundler cannot hold a different opinion than the running game.
    #>
    param([Parameter(Mandatory)][string]$RepoRoot)

    $directory = Join-Path $RepoRoot ($script:WotrLoaderDirectory -replace '/', '\')
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        throw (New-BundleRefusal -Problem "The War of the Ring loader directory is missing: $directory" -Remedy 'The bundler derives what to stage from these files. Without them it cannot know, and guessing is the defect this replaced.')
    }
    $loaders = New-Object 'System.Collections.Generic.List[object]'
    foreach ($file in @(Get-ChildItem -LiteralPath $directory -Filter '*.gd' -File | Sort-Object Name)) {
        $text = [IO.File]::ReadAllText($file.FullName)
        $envName = ''
        foreach ($constant in @('BUNDLE_ENV', 'DOCUMENT_ENV')) {
            if ($text -cmatch ('(?m)^const\s+' + $constant + '\s*:=\s*"(?<v>[^"]+)"')) {
                $envName = $Matches['v']
                break
            }
        }
        if ($envName -eq '') { continue }

        $schema = ''
        if ($text -cmatch '(?m)^const\s+SCHEMA\s*:=\s*"(?<v>[^"]+)"') { $schema = $Matches['v'] }
        $packRelative = ''
        foreach ($constant in @('PACK_BUNDLE_RELATIVE', 'PACK_DOCUMENT_RELATIVE')) {
            if ($text -cmatch ('(?m)^const\s+' + $constant + '\s*:=\s*"(?<v>[^"]+)"')) {
                $packRelative = $Matches['v']
                break
            }
        }
        $fileName = ''
        if ($text -cmatch '(?m)^const\s+FILE_NAME\s*:=\s*"(?<v>[^"]+)"') { $fileName = $Matches['v'] }
        # A loader that names a pack-relative FILE (the living-world document) is
        # staged as one file; everything else is a directory bundle.
        $kind = 'bundle'
        if ($packRelative -ne '' -and $packRelative -cmatch '\.json$') { $kind = 'document' }

        $loaders.Add([pscustomobject]@{
            source       = "$($script:WotrLoaderDirectory)/$($file.Name)"
            env          = $envName
            schema       = $schema
            packRelative = $packRelative
            fileName     = $fileName
            kind         = $kind
        })
    }
    if ($loaders.Count -eq 0) {
        throw (New-BundleRefusal -Problem "No loader in $directory declares BUNDLE_ENV or DOCUMENT_ENV, so the bundler derived an EMPTY staging plan." -Remedy 'An empty plan would ship a bundle with no War of the Ring data and no complaint. Either the loaders moved or their constants were renamed; fix the census in tools/wotr-data-staging.ps1 to match.')
    }
    return $loaders.ToArray()
}


function Get-WotrWorkspaceSchemaIndex {
    <#
    .SYNOPSIS
        Every converted artefact in the private workspace, indexed BY SCHEMA.
    .DESCRIPTION
        No path is hardcoded. A converter that writes its output somewhere new is
        still found, and a converter that starts declaring a new schema shows up
        here as a schema no loader claims - which the plan reports.
    #>
    param([Parameter(Mandatory)][string[]]$Roots)

    $index = @{}
    # Which root each schema was first found in. Roots are searched IN ORDER and
    # the first one that has a schema owns it: a worktree with its own workspace
    # shadows the main checkout's, exactly the way -ContentRoot resolution
    # already works. Without this, having a workspace in two checkouts would
    # look like "two copies of this bundle" and refuse a build that is fine.
    $ownerRoot = @{}
    $skipped = New-Object 'System.Collections.Generic.List[string]'
    $scanned = 0
    $seen = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($root in $Roots) {
        if ($root -eq '' -or -not (Test-Path -LiteralPath $root -PathType Container)) { continue }
        $files = New-Object 'System.Collections.Generic.List[object]'
        foreach ($file in @(Get-ChildItem -LiteralPath $root -Filter '*.json' -File -ErrorAction SilentlyContinue)) { $files.Add($file) }
        foreach ($sub in @(Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue)) {
            foreach ($file in @(Get-ChildItem -LiteralPath $sub.FullName -Filter '*.json' -File -ErrorAction SilentlyContinue)) { $files.Add($file) }
        }
        foreach ($file in $files) {
            if (-not $seen.Add($file.FullName.ToLowerInvariant())) { continue }
            if ($file.Length -gt $script:WotrWorkspaceMaxScanBytes) { $skipped.Add($file.FullName); continue }
            $scanned++
            $text = [IO.File]::ReadAllText($file.FullName)
            if ($text -cmatch '"schema"\s*:\s*"(?<s>openbfme\.[^"]+)"') {
                $schema = $Matches['s']
                if (-not $index.ContainsKey($schema)) {
                    $index[$schema] = New-Object 'System.Collections.Generic.List[string]'
                    $ownerRoot[$schema] = $root
                }
                if ($ownerRoot[$schema] -ceq $root) { $index[$schema].Add($file.FullName) }
            }
        }
    }
    return [pscustomobject]@{
        bySchema = $index
        scanned  = $scanned
        skipped  = $skipped.ToArray()
    }
}


function Resolve-WotrDocumentProvenance {
    <#
    .SYNOPSIS
        Which living-world document the converted region bundles were built
        against - read out of the bundles, not chosen here.
    .DESCRIPTION
        THIS IS WHY THE RELEASE SHIPS THE ROTWK DOCUMENT AND NOT THE BFME2 ONE,
        and it is not an opinion. The region-geometry converter is given a
        document and records which one in its manifest ("document.path", with
        the regionsMatched count it produced). The region-image converter records
        the same. A build that ships a DIFFERENT document than the one those
        meshes were matched against is shipping geometry keyed to region ids the
        document may not use - the exact silent-degradation class this whole
        lane exists to stop.
    .OUTPUTS
        $null when no bundle states a provenance, otherwise
        { fileName, declaredBy[], regionsMatched }.
    #>
    param([Parameter(Mandatory)]$SchemaIndex)

    $claims = New-Object 'System.Collections.Generic.List[object]'
    foreach ($schema in @('openbfme.living-map-regions', 'openbfme.living-world-region-images')) {
        if (-not $SchemaIndex.bySchema.ContainsKey($schema)) { continue }
        foreach ($path in $SchemaIndex.bySchema[$schema]) {
            try { $parsed = [IO.File]::ReadAllText($path) | ConvertFrom-Json } catch { continue }
            $names = @($parsed.PSObject.Properties.Name)
            if ($names -notcontains 'document') { continue }
            $documentValue = $parsed.document
            $documentPath = ''
            $matched = -1
            if ($documentValue -is [string]) {
                $documentPath = [string]$documentValue
            } elseif ($null -ne $documentValue) {
                $documentNames = @($documentValue.PSObject.Properties.Name)
                if ($documentNames -contains 'path') { $documentPath = [string]$documentValue.path }
                if ($documentNames -contains 'regionsMatched') { $matched = [int]$documentValue.regionsMatched }
            }
            if ($documentPath -eq '') { continue }
            $claims.Add([pscustomobject]@{
                fileName       = [IO.Path]::GetFileName($documentPath)
                declaredBy     = $path
                regionsMatched = $matched
            })
        }
    }
    if ($claims.Count -eq 0) { return $null }
    $distinct = @($claims | ForEach-Object { $_.fileName } | Sort-Object -Unique)
    if ($distinct.Count -gt 1) {
        throw (New-BundleRefusal -Problem ("The converted bundles disagree about which living-world document they were built against: " + ($distinct -join ', ') + ". Shipping either one would put geometry and portraits next to a document they were not matched to.") -Remedy 'Re-run the region-geometry and region-image converters against the same document, then rebuild.')
    }
    $best = $claims[0]
    foreach ($claim in $claims) { if ($claim.regionsMatched -gt $best.regionsMatched) { $best = $claim } }
    return [pscustomobject]@{
        fileName       = $best.fileName
        declaredBy     = @($claims | ForEach-Object { $_.declaredBy })
        regionsMatched = $best.regionsMatched
    }
}


function New-WotrStagingPlan {
    <#
    .SYNOPSIS
        The complete list of what must be staged and where, computed.
    .OUTPUTS
        {
          rules[]      one row per loader: env, schema, source, destination, present, loses
          groups[]     the copy operations, source directory/file -> pack-relative destination
          missing[]    rules with nothing behind them, with what the player loses
          document     the chosen living-world document and why it was chosen
          unclaimed[]  schemas in the workspace no loader accepts (informational)
          scanned      how many workspace .json files were read
        }
    #>
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string[]]$WorkspaceRoots,
        [string]$DocumentOverride = '',
        [string]$MapOverride = '',
        [switch]$AllowMismatchedDocument
    )

    $loaders = Get-WotrLoaderCensus -RepoRoot $RepoRoot

    # ---- the census must agree with the table, in BOTH directions -----------
    $ruleByEnv = @{}
    foreach ($rule in $script:WotrStagingRules) {
        if ($ruleByEnv.ContainsKey($rule.env)) {
            throw (New-BundleRefusal -Problem "tools/wotr-data-staging.ps1 declares two staging rules for $($rule.env).")
        }
        $ruleByEnv[$rule.env] = $rule
    }
    $loaderByEnv = @{}
    foreach ($loader in $loaders) { $loaderByEnv[$loader.env] = $loader }

    foreach ($loader in $loaders) {
        if (-not $ruleByEnv.ContainsKey($loader.env)) {
            throw (New-BundleRefusal -Problem "$($loader.source) declares $($loader.env), and tools/wotr-data-staging.ps1 has no staging rule for it. This build would therefore ship a bundle that loader cannot find." -Remedy "Add a row to `$script:WotrStagingRules for $($loader.env): its host (if it is found beside another bundle) and what the player loses without it. This refusal exists because the same class of omission has shipped three times.")
        }
    }
    foreach ($declaredEnv in @($ruleByEnv.Keys)) {
        if (-not $loaderByEnv.ContainsKey($declaredEnv)) {
            throw (New-BundleRefusal -Problem "tools/wotr-data-staging.ps1 has a staging rule for $declaredEnv, and no loader in $($script:WotrLoaderDirectory) reads it." -Remedy "Either the loader was renamed or removed. Remove the rule, or point it at the constant the loader now declares - a rule with no reader stages weight nothing loads.")
        }
    }

    # ---- destination, derived from the loader wherever the loader states it --
    $destinationByEnv = @{}
    foreach ($loader in $loaders) {
        if ($loader.packRelative -ne '') { $destinationByEnv[$loader.env] = $loader.packRelative }
    }
    foreach ($loader in $loaders) {
        if ($destinationByEnv.ContainsKey($loader.env)) { continue }
        # NOT $host: that is a PowerShell automatic variable and assigning it
        # fails under Set-StrictMode.
        $hostEnv = [string]$ruleByEnv[$loader.env].host
        if ($hostEnv -eq '') {
            throw (New-BundleRefusal -Problem "$($loader.source) declares neither PACK_BUNDLE_RELATIVE nor PACK_DOCUMENT_RELATIVE, and its rule names no host bundle, so there is no path inside a content pack where the running game would find it." -Remedy "Either give the loader a pack-relative constant, or set host on its rule to the env of the bundle it is searched beside (wotr_screen.gd builds those roots).")
        }
        if (-not $destinationByEnv.ContainsKey($hostEnv)) {
            throw (New-BundleRefusal -Problem "$($loader.env) is declared to be hosted by $hostEnv, which itself has no pack-relative destination." -Remedy 'A host must be a bundle whose loader declares PACK_BUNDLE_RELATIVE. Chained hosts are not supported on purpose: one indirection is already the limit of what can be checked.')
        }
        $destinationByEnv[$loader.env] = $destinationByEnv[$hostEnv]
    }

    # ---- what is actually on disk, found by schema --------------------------
    $index = Get-WotrWorkspaceSchemaIndex -Roots $WorkspaceRoots
    $provenance = Resolve-WotrDocumentProvenance -SchemaIndex $index

    $rows = New-Object 'System.Collections.Generic.List[object]'
    $missing = New-Object 'System.Collections.Generic.List[object]'
    $documentRecord = [ordered]@{
        chosen = ''; fileName = ''; basis = ''; provenanceFileName = ''
        provenanceDeclaredBy = @(); regionsMatched = -1
    }

    foreach ($loader in $loaders) {
        $rule = $ruleByEnv[$loader.env]
        $schema = $loader.schema
        if ($schema -eq '') { $schema = [string]$rule.schema }
        if ($schema -eq '') {
            throw (New-BundleRefusal -Problem "$($loader.source) declares no SCHEMA and its rule supplies none, so the workspace cannot be searched for what it needs." -Remedy 'Set schema on the rule to the schema string the loader accepts.')
        }

        $candidates = @()
        if ($index.bySchema.ContainsKey($schema)) { $candidates = @($index.bySchema[$schema]) }

        $chosen = ''
        $basis = ''
        if ($loader.kind -ceq 'document') {
            # The document is the one case with more than one legitimate
            # candidate on disk (BFME2 and RotWK). It is NOT chosen by taste:
            # the converted region bundles record which one they were matched
            # against, and that is what ships.
            if ($DocumentOverride -ne '') {
                if (-not (Test-Path -LiteralPath $DocumentOverride -PathType Leaf)) {
                    throw (New-BundleRefusal -Problem "-LivingWorldDocument does not exist: $DocumentOverride")
                }
                $chosen = [IO.Path]::GetFullPath($DocumentOverride)
                $basis = 'explicit -LivingWorldDocument'
            } elseif ($null -ne $provenance) {
                $matchedCandidates = @($candidates | Where-Object { [IO.Path]::GetFileName($_) -ceq $provenance.fileName })
                if ($matchedCandidates.Count -eq 0) {
                    throw (New-BundleRefusal -Problem "The converted region bundles were built against '$($provenance.fileName)', and no living-world document with that name is in the workspace. Candidates found: $(if ($candidates.Count -gt 0) { $candidates -join ', ' } else { 'none' })." -Remedy 'Regenerate that document, or re-run the region converters against a document that is present. Shipping a different one would put retail geometry beside a document it was never matched to.')
                }
                $chosen = $matchedCandidates[0]
                $basis = "the document the converted region bundles record being built against ($($provenance.regionsMatched) of their regions matched it)"
            } elseif ($candidates.Count -eq 1) {
                $chosen = $candidates[0]
                $basis = 'the only living-world document in the workspace'
            } elseif ($candidates.Count -gt 1) {
                throw (New-BundleRefusal -Problem ("The workspace holds $($candidates.Count) living-world documents and no converted bundle records which one it was built against, so this build cannot know which to ship: " + ($candidates -join ', ')) -Remedy 'Pass -LivingWorldDocument <path>, or produce the region bundles (their manifest records the document and settles it).')
            }
            if ($chosen -ne '') {
                $documentRecord.chosen = $chosen
                $documentRecord.fileName = [IO.Path]::GetFileName($chosen)
                $documentRecord.basis = $basis
                if ($null -ne $provenance) {
                    $documentRecord.provenanceFileName = $provenance.fileName
                    $documentRecord.provenanceDeclaredBy = @($provenance.declaredBy)
                    $documentRecord.regionsMatched = $provenance.regionsMatched
                    if (([IO.Path]::GetFileName($chosen)) -cne $provenance.fileName -and -not $AllowMismatchedDocument) {
                        throw (New-BundleRefusal -Problem "This build would ship '$([IO.Path]::GetFileName($chosen))', but the converted region geometry and portraits were built against '$($provenance.fileName)' ($($provenance.declaredBy -join ', ')). Territories and region art are keyed to the region ids in that document." -Remedy 'Drop -LivingWorldDocument to ship the document the bundles were matched to, or pass -AllowMismatchedWotrDocument to ship them knowingly mismatched (BUILD-INFO records that you did).')
                    }
                }
            }
        } else {
            if ($MapOverride -ne '' -and $loader.env -ceq 'OPENBFME_LIVING_MAP') {
                if (-not (Test-Path -LiteralPath $MapOverride -PathType Container)) {
                    throw (New-BundleRefusal -Problem "-LivingMapBundle does not exist: $MapOverride")
                }
                $chosen = (Join-Path ([IO.Path]::GetFullPath($MapOverride)) 'manifest.json')
                $basis = 'explicit -LivingMapBundle'
            } elseif ($candidates.Count -eq 1) {
                $chosen = $candidates[0]
                $basis = "the only $schema bundle in the workspace"
            } elseif ($candidates.Count -gt 1) {
                throw (New-BundleRefusal -Problem ("The workspace holds $($candidates.Count) artefacts declaring '$schema', so this build cannot know which one the release should carry: " + ($candidates -join ', ')) -Remedy 'Leave exactly one in the workspace. Two converted copies of the same bundle is how a release ships the older one and nobody notices.')
            }
        }

        # The file the loader opens once the bundle is staged - its own FILE_NAME
        # where it declares one, otherwise the manifest.json its locate_and_load
        # probes for. This is what the verifier re-checks in a finished bundle,
        # so "BUILD-INFO says this shipped" and "the loader can open it" are the
        # same claim.
        $marker = ''
        $landedRelative = $destinationByEnv[$loader.env]
        if ($loader.kind -ceq 'document') {
            $marker = [IO.Path]::GetFileName($loader.packRelative)
        } else {
            $marker = $(if ($loader.fileName -ne '') { $loader.fileName } else { 'manifest.json' })
            $landedRelative = $landedRelative + '/' + $marker
        }

        $row = [pscustomobject]@{
            env          = $loader.env
            schema       = $schema
            loader       = $loader.source
            kind         = $loader.kind
            destination  = $destinationByEnv[$loader.env]
            marker       = $marker
            landedRelative = $landedRelative
            hostedBy     = [string]$rule.host
            source       = $chosen
            sourceDirectory = $(if ($chosen -eq '' -or $loader.kind -ceq 'document') { '' } else { [IO.Path]::GetDirectoryName($chosen) })
            basis        = $basis
            present      = ($chosen -ne '')
            loses        = [string]$rule.loses
        }
        $rows.Add($row)
        if (-not $row.present) {
            $missing.Add($row)
        }
    }

    # ---- the copy operations ------------------------------------------------
    # Grouped by SOURCE DIRECTORY, because several loaders legitimately read
    # different files out of one converted bundle (the region bundle carries the
    # strings, the UI atlases, the macros and the setup strings). One directory
    # is one copy; the loaders it satisfies are recorded against it.
    $groups = New-Object 'System.Collections.Generic.List[object]'
    $byDirectory = [ordered]@{}
    foreach ($row in $rows) {
        if (-not $row.present) { continue }
        if ($row.kind -ceq 'document') {
            $groups.Add([pscustomobject]@{
                kind = 'document'; source = $row.source; destination = $row.destination
                envs = @($row.env); primary = $true
            })
            continue
        }
        $key = ([string]$row.sourceDirectory).ToLowerInvariant()
        if (-not $byDirectory.Contains($key)) {
            $byDirectory[$key] = [pscustomobject]@{
                kind = 'bundle'; source = $row.sourceDirectory; destination = $row.destination
                envs = New-Object 'System.Collections.Generic.List[string]'
                primary = $false
            }
        }
        $group = $byDirectory[$key]
        if ($group.destination -cne $row.destination) {
            throw (New-BundleRefusal -Problem "The converted bundle at $($row.sourceDirectory) satisfies loaders that look in two different places inside a content pack ('$($group.destination)' and '$($row.destination)')." -Remedy 'Split the converted output, or correct the host declarations. A single directory cannot be staged to two destinations.')
        }
        [void]$group.envs.Add($row.env)
        # The group that OWNS the destination is the one whose loader declared
        # that pack-relative path itself. It is mirrored; the others merge into
        # it, which is exactly how wotr_screen.gd searches them (beside the
        # region geometry).
        if ($row.hostedBy -eq '') { $group.primary = $true }
    }
    foreach ($key in @($byDirectory.Keys)) {
        $group = $byDirectory[$key]
        $groups.Add([pscustomobject]@{
            kind = 'bundle'; source = $group.source; destination = $group.destination
            envs = @($group.envs.ToArray()); primary = [bool]$group.primary
        })
    }

    # Mirrors before merges: a mirrored copy deletes what it does not own, so a
    # merge that ran first would be wiped without a word.
    $ordered = @(@($groups | Where-Object { $_.kind -ceq 'document' }) +
                 @($groups | Where-Object { $_.kind -ceq 'bundle' -and $_.primary }) +
                 @($groups | Where-Object { $_.kind -ceq 'bundle' -and -not $_.primary }))

    # Two bundles merging into one destination must not carry the same relative
    # path. Checked BEFORE anything is copied, so a collision is a refusal and
    # never a half-overwritten destination.
    $claimed = @{}
    foreach ($group in $ordered) {
        if ($group.kind -cne 'bundle') { continue }
        $rootFull = [IO.Path]::GetFullPath($group.source).TrimEnd('\', '/')
        foreach ($file in [IO.Directory]::EnumerateFiles($rootFull, '*', [IO.SearchOption]::AllDirectories)) {
            $relative = ($group.destination + '/' + $file.Substring($rootFull.Length + 1).Replace('\', '/'))
            $key = $relative.ToLowerInvariant()
            if ($claimed.ContainsKey($key)) {
                throw (New-BundleRefusal -Problem "Two converted bundles would land on the same file inside the content pack: '$relative' comes from both $($claimed[$key]) and $($group.source)." -Remedy 'One of them would silently win. Rename the colliding output in the converter, or stage them to separate destinations.')
            }
            $claimed[$key] = $group.source
        }
    }

    $unclaimed = New-Object 'System.Collections.Generic.List[string]'
    $wanted = @($rows | ForEach-Object { $_.schema })
    foreach ($schema in @($index.bySchema.Keys | Sort-Object)) {
        if ($wanted -cnotcontains $schema) { $unclaimed.Add($schema) }
    }

    return [pscustomobject]@{
        rules     = $rows.ToArray()
        groups    = $ordered
        missing   = $missing.ToArray()
        document  = $documentRecord
        unclaimed = $unclaimed.ToArray()
        scanned   = $index.scanned
        skipped   = $index.skipped
    }
}


function Test-BundleWotrArtifact {
    <#
    .SYNOPSIS
        Refuse an artefact the shipped game would reject anyway.
    .DESCRIPTION
        Generalises the two checks that already existed for the document and the
        3D map to every bundle in the plan, using the schema the LOADER declares
        rather than a copy of it. Run against the source before the build spends
        six minutes, and again against what actually LANDED - a copy that
        silently dropped a manifest would otherwise produce a bundle that falls
        back with nothing saying why.
    #>
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Schema,
        [Parameter(Mandatory)][string]$Label,
        # Deserialize the whole document. Used for the living-world document,
        # which is the one artefact where this has always been done and where
        # removing it would be removing a refusal.
        #
        # NOT used for the rest, and the reason is measured rather than assumed:
        # retail's own string tables legitimately carry keys that differ only in
        # case ("APT:" and "Apt:"), Godot's JSON parser keeps both, and Windows
        # PowerShell 5.1's ConvertFrom-Json throws on them. Deserializing here
        # would refuse a bundle the shipped game reads perfectly - a false
        # refusal, which is its own way of shipping the wrong thing.
        [switch]$Strict
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw (New-BundleRefusal -Problem "$Label declares schema '$Schema' but the file is not there: $Path")
    }
    $info = New-Object IO.FileInfo $Path
    if ($info.Length -eq 0) {
        throw (New-BundleRefusal -Problem "$Label is empty: $Path")
    }
    if ($info.Length -gt $script:WotrDocumentMaxBytes) {
        throw (New-BundleRefusal -Problem "$Label is $($info.Length) bytes, over the $($script:WotrDocumentMaxBytes)-byte limit the game enforces." -Remedy 'The shipped build would refuse to read it, so shipping it would produce a feature that is present and broken.')
    }
    $text = [IO.File]::ReadAllText($Path)
    if ($Strict) {
        try { $parsed = $text | ConvertFrom-Json } catch {
            throw (New-BundleRefusal -Problem "$Label is not valid JSON: $Path" -Remedy 'Re-run the converter that produced it; a document that does not parse disables the feature at runtime.')
        }
        if ($null -eq $parsed -or $parsed -isnot [psobject]) {
            throw (New-BundleRefusal -Problem "$Label is not a JSON object: $Path")
        }
        # Deserialized: ask the object, not the text, so a nested "schema" key
        # deep inside a big document cannot answer for the top-level one.
        $names = @($parsed.PSObject.Properties.Name)
        if ($names -notcontains 'schema' -or ([string]$parsed.schema) -cne $Schema) {
            throw (New-BundleRefusal -Problem "$Label declares schema '$(if ($names -contains 'schema') { $parsed.schema } else { '<none>' })', and the loader accepts only '$Schema': $Path" -Remedy 'The shipped build checks this and would fall back silently. Re-run the converter that produced it.')
        }
        return $info.Length
    } elseif ($text.TrimStart() -cnotlike '{*') {
        throw (New-BundleRefusal -Problem "$Label is not a JSON object: $Path" -Remedy 'Every loader here opens a JSON object and would refuse this.')
    }
    # The schema string is read the same way in both modes, so the check that
    # decides whether the shipped build would accept this file is identical.
    if ($text -cmatch '"schema"\s*:\s*"(?<s>[^"]*)"') {
        if ($Matches['s'] -cne $Schema) {
            throw (New-BundleRefusal -Problem "$Label declares schema '$($Matches['s'])', and the loader accepts only '$Schema': $Path" -Remedy 'The shipped build checks this and would fall back silently. Re-run the converter that produced it.')
        }
    } else {
        throw (New-BundleRefusal -Problem "$Label declares no schema at all, so the loader that needs it would refuse it: $Path")
    }
    return $info.Length
}
