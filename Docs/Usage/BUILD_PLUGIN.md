# Build Plugin

Use `Resources/Scripts/BuildPlugin.ps1` to package Unreal MCP Toolkit with
Unreal AutomationTool. The script is scoped to this repository by default and
keeps build products under `Saved/BuildPlugin/`.

## UE 5.7 Windows And Linux

Default behavior targets UE 5.7 for both Win64 and Linux:

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/BuildPlugin.ps1
```

Equivalent explicit command:

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/BuildPlugin.ps1 -EngineVersion 5.7 -TargetPlatforms "Win64 Linux"
```

Expected output folders:

```text
Saved/BuildPlugin/MCPToolkit_UE5.7_Win64
Saved/BuildPlugin/MCPToolkit_UE5.7_Linux
```

The source `MCPToolkit.uplugin` file is not modified. After packaging, the
script stamps each packaged descriptor with `EngineVersion: "5.7.0"` unless
`-NoEngineVersionStamp` is passed.

Unreal MCP Toolkit is an Editor plugin. When this script runs on Windows,
`BuildPlugin` produces a precompiled Win64 editor binary. The Linux package is
still produced and stamped for UE 5.7, but it is a source package from the
Windows host; Unreal AutomationTool does not cross-compile Linux editor host
modules from Windows. To produce `Binaries/Linux/libUnrealEditor-MCPToolkit.so`,
run the same script on a Linux host with UE 5.7 installed.

## Linux Toolchain

For UE 5.7 Linux packaging, the script uses the first available option:

1. `-LinuxToolchain <path>`
2. `LINUX_MULTIARCH_ROOT`
3. `C:\UnrealToolchains\v26_clang-20.1.8-rockylinux8\`

Example:

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/BuildPlugin.ps1 -TargetPlatforms "Linux" -LinuxToolchain "C:\UnrealToolchains\v26_clang-20.1.8-rockylinux8\"
```

## Parameters

| Parameter | Default | Purpose |
|---|---|---|
| `-PluginPath` | `MCPToolkit.uplugin` in this repo | Plugin descriptor to package |
| `-EngineVersion` | `5.7` | Unreal Engine install suffix, such as `5.7` |
| `-TargetPlatforms` | `Win64 Linux` | Space-separated platform list |
| `-PackageRoot` | `Saved/BuildPlugin` | Parent directory for package outputs |
| `-EngineRoot` | auto-detect | Optional Unreal Engine root override |
| `-LinuxToolchain` | auto-detect | Optional Linux cross-toolchain override |
| `-NoEngineVersionStamp` | false | Leave packaged `.uplugin` descriptor unchanged |

## Single-Platform Builds

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/BuildPlugin.ps1 -TargetPlatforms "Win64"
powershell -ExecutionPolicy Bypass -File Resources/Scripts/BuildPlugin.ps1 -TargetPlatforms "Linux"
```

## Validation

Run contract checks before release packaging:

```powershell
python Resources/Scripts/validate_mcp_contract.py
python Resources/Scripts/test_mcp_contract.py
powershell -ExecutionPolicy Bypass -File Resources/Scripts/ValidateUITSpecs.ps1
```
