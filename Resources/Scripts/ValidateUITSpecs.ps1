param(
    [string]$Root = "",
    [string]$SpecDirectory = "Docs/UI_TSpec"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $Root = Join-Path $scriptDir "..\.."
}

$rootPath = (Resolve-Path -LiteralPath $Root).Path
$dir = Join-Path $rootPath $SpecDirectory

if (-not (Test-Path -LiteralPath $dir)) {
    throw "TSpec directory not found: $dir"
}

$allowedModes = @("letterbox", "hybrid", "adaptive")
$rootShellByMode = @{
    letterbox = "letterbox-default"
    hybrid = "hybrid-adaptive"
    adaptive = "adaptive-default"
}
$requiredNodesByMode = @{
    letterbox = @("Root_Overlay", "BG_Layer", "BG_Image", "Content_Scale", "Content_SafeZone", "Content_Box")
    hybrid = @("Root_Overlay", "BG_Image", "Content_SafeZone", "MainCanvas")
    adaptive = @("Root_Overlay", "BG_Layer", "BG_Image", "Content_SafeZone", "Adaptive_Canvas")
}

$failures = New-Object System.Collections.Generic.List[string]
$specs = Get-ChildItem -LiteralPath $dir -Filter "*.tspec.json" -File

foreach ($file in $specs) {
    $relative = $file.FullName.Substring($rootPath.Length).TrimStart('\', '/')
    try {
        $spec = Get-Content -Raw -LiteralPath $file.FullName | ConvertFrom-Json
    }
    catch {
        $failures.Add("${relative}: invalid JSON - $($_.Exception.Message)")
        continue
    }

    foreach ($field in @('$schema', 'screen', 'pencilFile', 'pencilFrameId', 'mode', 'wbpPath', 'parentClass', 'rootShell', 'nodes')) {
        if (-not $spec.PSObject.Properties.Name.Contains($field)) {
            $failures.Add("${relative}: missing required field '$field'")
        }
    }

    if ($spec.'$schema' -ne "tspec-v1") {
        $failures.Add("${relative}: `$schema must be 'tspec-v1'")
    }

    if ($allowedModes -notcontains $spec.mode) {
        $failures.Add("${relative}: unsupported mode '$($spec.mode)'")
    }
    elseif ($spec.rootShell.type -ne $rootShellByMode[$spec.mode]) {
        $failures.Add("${relative}: mode '$($spec.mode)' requires rootShell.type '$($rootShellByMode[$spec.mode])', got '$($spec.rootShell.type)'")
    }

    if (-not ($spec.rootShell.referenceSize -is [System.Array]) -or $spec.rootShell.referenceSize.Count -ne 2) {
        $failures.Add("${relative}: rootShell.referenceSize must be [width,height]")
    }

    if (-not ($spec.nodes -is [System.Array]) -or $spec.nodes.Count -eq 0) {
        $failures.Add("${relative}: nodes must be a non-empty array")
        continue
    }

    $names = New-Object System.Collections.Generic.HashSet[string]
    foreach ($node in $spec.nodes) {
        if ([string]::IsNullOrWhiteSpace($node.name)) {
            $failures.Add("${relative}: node with empty name")
            continue
        }
        if (-not $names.Add([string]$node.name)) {
            $failures.Add("${relative}: duplicate node name '$($node.name)'")
        }
        if ([string]::IsNullOrWhiteSpace($node.widgetClass)) {
            $failures.Add("${relative}: node '$($node.name)' missing widgetClass")
        }
        if (-not $node.PSObject.Properties.Name.Contains("parentName")) {
            $failures.Add("${relative}: node '$($node.name)' missing parentName")
        }
    }

    foreach ($requiredNode in $requiredNodesByMode[$spec.mode]) {
        $found = $names.Contains($requiredNode)
        if (-not $found -and $requiredNode -eq "Root_Overlay") {
            $found = $names.Contains("RootOverlay")
        }
        if (-not $found) {
            $failures.Add("${relative}: mode '$($spec.mode)' missing required root node '$requiredNode'")
        }
    }

    foreach ($node in $spec.nodes) {
        $parentName = [string]$node.parentName
        $parentFound = $names.Contains($parentName)
        if (-not $parentFound -and $parentName -eq "RootOverlay") {
            $parentFound = $names.Contains("Root_Overlay")
        }
        if (-not $parentFound -and $parentName -eq "Root_Overlay") {
            $parentFound = $names.Contains("RootOverlay")
        }
        if ($node.parentName -and -not $parentFound) {
            $failures.Add("${relative}: node '$($node.name)' references missing parent '$($node.parentName)'")
        }

        if ($node.children) {
            foreach ($child in $node.children) {
                if (-not $names.Contains([string]$child)) {
                    $failures.Add("${relative}: node '$($node.name)' references missing child '$child'")
                }
            }
        }

        if ($spec.mode -eq "letterbox" -and $node.slot -and $node.slot.canvas) {
            $failures.Add("${relative}: letterbox node '$($node.name)' must not use CanvasPanel slot")
        }

        if ($node.slot -and $node.slot.padding) {
            if (-not ($node.slot.padding -is [System.Array]) -or $node.slot.padding.Count -ne 4) {
                $failures.Add("${relative}: node '$($node.name)' slot.padding must be [L,T,R,B]")
            }
        }

        if ($node.slot -and $node.slot.canvas) {
            foreach ($canvasField in @("anchors", "offsets")) {
                $value = $node.slot.canvas.$canvasField
                if ($value -and ((-not ($value -is [System.Array])) -or $value.Count -ne 4)) {
                    $failures.Add("${relative}: node '$($node.name)' canvas.$canvasField must have 4 numbers")
                }
            }
            $alignment = $node.slot.canvas.alignment
            if ($alignment -and ((-not ($alignment -is [System.Array])) -or $alignment.Count -ne 2)) {
                $failures.Add("${relative}: node '$($node.name)' canvas.alignment must have 2 numbers")
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "TSpec validation failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Validated $($specs.Count) TSpec file(s)." -ForegroundColor Green

