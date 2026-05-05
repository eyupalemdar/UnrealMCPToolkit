param(
    [Parameter(Mandatory=$false)]
    [string]$PluginPath = "",

    [Parameter(Mandatory=$false)]
    [string]$EngineVersion = "5.7",

    [Parameter(Mandatory=$false)]
    [string]$TargetPlatforms = "Win64 Linux",

    [Parameter(Mandatory=$false)]
    [string]$PackageRoot = "",

    [Parameter(Mandatory=$false)]
    [string]$EngineRoot = "",

    [Parameter(Mandatory=$false)]
    [string]$LinuxToolchain = "",

    [Parameter(Mandatory=$false)]
    [switch]$NoEngineVersionStamp
)

$ErrorActionPreference = "Stop"

function Resolve-FullPath {
    param([string]$Path)

    return [System.IO.Path]::GetFullPath($Path)
}

function Get-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return Resolve-FullPath (Join-Path $scriptDir "..\..")
}

function Get-RunUatPath {
    param(
        [string]$Version,
        [string]$Root
    )

    $candidates = @()

    if (-not [string]::IsNullOrWhiteSpace($Root)) {
        $candidates += Join-Path $Root "Engine\Build\BatchFiles\RunUAT.bat"
        $candidates += Join-Path $Root "Build\BatchFiles\RunUAT.bat"
    }

    $candidates += "C:\Program Files\Epic Games\UE_$Version\Engine\Build\BatchFiles\RunUAT.bat"

    if ($env:UNREAL_ENGINE_ROOT) {
        $candidates += Join-Path $env:UNREAL_ENGINE_ROOT "Engine\Build\BatchFiles\RunUAT.bat"
        $candidates += Join-Path $env:UNREAL_ENGINE_ROOT "Build\BatchFiles\RunUAT.bat"
    }

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate -PathType Leaf) {
            return Resolve-FullPath $candidate
        }
    }

    throw "RunUAT.bat not found for UE $Version. Pass -EngineRoot or install UE_$Version under C:\Program Files\Epic Games."
}

function Get-EngineVersionString {
    param([string]$Version)

    if ($Version -match '^\d+\.\d+\.\d+$') {
        return $Version
    }

    if ($Version -match '^\d+\.\d+$') {
        return "$Version.0"
    }

    return $Version
}

function Get-LinuxToolchainPath {
    param(
        [string]$Version,
        [string]$ExplicitPath
    )

    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        return $ExplicitPath
    }

    if ($env:LINUX_MULTIARCH_ROOT) {
        return $env:LINUX_MULTIARCH_ROOT
    }

    $toolchainMap = @{
        "5.7" = "C:\UnrealToolchains\v26_clang-20.1.8-rockylinux8\"
        "5.6" = "C:\UnrealToolchains\v25_clang-18.1.0-rockylinux8\"
        "5.5" = "C:\UnrealToolchains\v23_clang-18.1.0-rockylinux8\"
        "5.4" = "C:\UnrealToolchains\v22_clang-16.0.6-centos7\"
        "5.3" = "C:\UnrealToolchains\v22_clang-16.0.6-centos7\"
        "5.2" = "C:\UnrealToolchains\v21_clang-15.0.1-centos7\"
        "5.1" = "C:\UnrealToolchains\v20_clang-13.0.1-centos7\"
    }

    if ($toolchainMap.ContainsKey($Version)) {
        return $toolchainMap[$Version]
    }

    return ""
}

function Set-PackagedEngineVersion {
    param(
        [string]$PackagedPluginPath,
        [string]$EngineVersionValue
    )

    if (-not (Test-Path $PackagedPluginPath -PathType Leaf)) {
        throw "Packaged plugin descriptor not found: $PackagedPluginPath"
    }

    $pluginObject = Get-Content $PackagedPluginPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $ordered = [ordered]@{}

    foreach ($property in $pluginObject.PSObject.Properties) {
        $ordered[$property.Name] = $property.Value
        if ($property.Name -eq "VersionName" -and -not $ordered.Contains("EngineVersion")) {
            $ordered["EngineVersion"] = $EngineVersionValue
        }
    }

    if (-not $ordered.Contains("EngineVersion")) {
        $ordered["EngineVersion"] = $EngineVersionValue
    }
    else {
        $ordered["EngineVersion"] = $EngineVersionValue
    }

    $json = [PSCustomObject]$ordered | ConvertTo-Json -Depth 20
    $json = $json -replace '\\u0026', '&'
    $json = $json -replace '\\u003c', '<'
    $json = $json -replace '\\u003e', '>'
    $json = $json -replace '\\u0027', "'"
    $json = $json -replace '\\u002b', '+'
    $json = $json -replace '\\u003d', '='

    Set-Content -Path $PackagedPluginPath -Value $json -NoNewline -Encoding UTF8
}

$repoRoot = Get-RepoRoot

if ([string]::IsNullOrWhiteSpace($PluginPath)) {
    $PluginPath = Join-Path $repoRoot "MCPToolkit.uplugin"
}

if (-not (Test-Path $PluginPath -PathType Leaf)) {
    throw "Plugin file not found: $PluginPath"
}

$PluginPath = Resolve-FullPath $PluginPath
$pluginInfo = Get-Item $PluginPath
$pluginName = $pluginInfo.BaseName

if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
    $PackageRoot = Join-Path $repoRoot "Saved\BuildPlugin"
}

$PackageRoot = Resolve-FullPath $PackageRoot
New-Item -ItemType Directory -Path $PackageRoot -Force | Out-Null

$platforms = $TargetPlatforms -split '\s+' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
if ($platforms.Count -eq 0) {
    throw "No target platforms specified."
}

$runUat = Get-RunUatPath -Version $EngineVersion -Root $EngineRoot
$engineVersionStamp = Get-EngineVersionString -Version $EngineVersion
$failures = @()

Write-Host "[INFO] Plugin: $PluginPath" -ForegroundColor Cyan
Write-Host "[INFO] Engine: UE_$EngineVersion" -ForegroundColor Cyan
Write-Host "[INFO] RunUAT: $runUat" -ForegroundColor Cyan
Write-Host "[INFO] Platforms: $($platforms -join ', ')" -ForegroundColor Cyan
Write-Host "[INFO] Package root: $PackageRoot" -ForegroundColor Cyan

foreach ($platform in $platforms) {
    $outputFolder = Join-Path $PackageRoot "${pluginName}_UE${EngineVersion}_${platform}"
    New-Item -ItemType Directory -Path $outputFolder -Force | Out-Null

    $arguments = @(
        "BuildPlugin",
        "-Plugin=$PluginPath",
        "-Package=$outputFolder",
        "-TargetPlatforms=$platform"
    )

    if ($platform -eq "Win64") {
        $arguments += "-Rocket"
    }
    elseif ($platform -eq "Linux") {
        $toolchain = Get-LinuxToolchainPath -Version $EngineVersion -ExplicitPath $LinuxToolchain
        if ([string]::IsNullOrWhiteSpace($toolchain) -or -not (Test-Path $toolchain -PathType Container)) {
            $failures += "${platform}: Linux toolchain not found. Pass -LinuxToolchain or set LINUX_MULTIARCH_ROOT."
            Write-Host "[ERROR] Linux toolchain not found: $toolchain" -ForegroundColor Red
            continue
        }

        Write-Host "[INFO] Linux toolchain: $toolchain" -ForegroundColor Cyan
        $arguments += "-Architecture=x86_64-unknown-linux-gnu"
        $arguments += "-LinuxToolchain=$toolchain"
    }

    Write-Host "`n[INFO] Building $pluginName for UE_$EngineVersion $platform..." -ForegroundColor Green
    Write-Host "[INFO] Output: $outputFolder" -ForegroundColor Cyan

    & $runUat @arguments
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        $failures += "${platform}: BuildPlugin failed with exit code $exitCode"
        Write-Host "[ERROR] Build failed for $platform" -ForegroundColor Red
        continue
    }

    if (-not $NoEngineVersionStamp) {
        $packagedPluginPath = Join-Path $outputFolder "$pluginName.uplugin"
        Set-PackagedEngineVersion -PackagedPluginPath $packagedPluginPath -EngineVersionValue $engineVersionStamp
        Write-Host "[INFO] Stamped packaged descriptor EngineVersion=$engineVersionStamp" -ForegroundColor Cyan
    }

    Write-Host "[SUCCESS] Built $pluginName for UE_$EngineVersion $platform" -ForegroundColor Green
}

if ($failures.Count -gt 0) {
    Write-Host "`n[ERROR] Build completed with failures:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "`n[SUCCESS] Build process completed." -ForegroundColor Green
exit 0
