# Export System

Unreal MCP Toolkit exports Unreal assets into text files that AI assistants can
read, diff, and reason about. Export is read-only: mutation workflows live in
command handlers and Builders, while canonical asset serialization lives in
registered Exporters.

## Entry Points

| Entry point | Use case |
|---|---|
| Content Browser `Export for AI` | Manual export from the editor |
| MCP/TCP `export_blueprint` | Export any supported asset type from an AI assistant |
| MCP/TCP `export_widget` | Widget Blueprint export convenience command |
| `MCTExport` commandlet | Headless export through `UnrealEditor-Cmd.exe` |
| `Resources/Scripts/export_asset.py` | Automation wrapper for commandlet export or local raw-file simplification |
| `Resources/Scripts/simplify_asset.py` | Manual simplification of an existing raw export |

## Output Files

Each normal export can produce three files:

| Format | Suffix | Producer | Best for |
|---|---|---|---|
| Raw | `_raw.txt` | C++ exporter | Debugging export coverage and full UE serialization output |
| Stripped | `_stripped.txt` | C++ exporter with default filtering | Exact non-default UE property and pin text |
| Simplified | `_simplified.txt` | Python simplifier | General AI review and human reading |

Read `_simplified.txt` first. Use `_stripped.txt` when exact ImportText values,
pin types, transition data, or low-level UE serialization details matter. Use
`_raw.txt` only when debugging the export pipeline.

Default output mirrors Content paths under `Dev/AIExports/`.

## Supported Exporters

The exporter registry checks higher priority exporters first.

| Asset type | Exporter | Priority |
|---|---|---:|
| Widget Blueprint | `UMCTWidgetBlueprintExporter` | 100 |
| Animation Blueprint | `UMCTAnimBlueprintExporter` | 90 |
| Blueprint | `UMCTBlueprintExporter` | 50 |
| World/Map | `UMCTWorldExporter` | 50 |
| DataTable | `UMCTDataTableExporter` | 50 |
| Input Action / Input Mapping Context | `UMCTInputExporter` | 50 |
| Audio assets | `UMCTAudioExporter` | 50 |
| Texture | `UMCTTextureExporter` | 50 |
| PhysicalMaterial | `UMCTPhysicalMaterialExporter` | 46 |
| Material / MaterialInstance | `UMCTMaterialExporter` | 45 |
| DataAsset | `UMCTDataAssetExporter` | 40 |

Use `list_supported_types` from MCP/TCP to query the live editor for the exact
supported class paths.

## Simplifiers

Python simplifiers live under `Resources/Scripts/`:

| Script | Scope |
|---|---|
| `simplify_asset.py` | Dispatcher that detects asset type and calls the right simplifier |
| `bp_simplify.py` | Blueprint graph simplification |
| `widget_simplify.py` | Widget tree simplification |
| `animbp_simplify.py` | Animation Blueprint state machine and graph simplification |
| `dataasset_simplify.py` | DataAsset and property-heavy asset simplification |
| `input_simplify.py` | Input action and mapping context simplification |
| `ability_simplify.py` | Gameplay Ability-oriented simplification |
| `material_simplify.py` | Material and MaterialInstance graph/property simplification |

## MCP/TCP Usage

With an Unreal Editor instance open:

```powershell
python Plugins/MCPToolkit/Resources/Scripts/ai_export_client.py ping
python Plugins/MCPToolkit/Resources/Scripts/ai_export_client.py export_blueprint /Game/UI/W_MainMenu
python Plugins/MCPToolkit/Resources/Scripts/ai_export_client.py list_types
```

MCP tools expose the same export surface:

```text
export_blueprint(asset_path="/Game/UI/W_MainMenu", both_formats=true)
export_widget(asset_path="/Game/UI/W_MainMenu", both_formats=true)
list_supported_types()
```

## Commandlet Usage

Run through `UnrealEditor-Cmd.exe`:

```powershell
UnrealEditor-Cmd.exe "Project.uproject" -run=MCTExport -asset="/Game/UI/W_Menu" -both -nullrhi -unattended -nosplash -nopause
```

Supported commandlet parameters:

| Parameter | Purpose |
|---|---|
| `-asset=<path>` | Required Unreal asset path |
| `-raw` | Export raw file only |
| `-simplify` | Export simplified output only |
| `-both` | Export raw, stripped, and simplified outputs |
| `-output=<dir>` | Override output directory |
| `-format=text` | Current text output mode |

Automation wrapper examples:

```powershell
python Resources/Scripts/export_asset.py --asset /Game/UI/W_Menu --project <PROJECT_ROOT>\ExampleProject.uproject
python Resources/Scripts/export_asset.py Dev\AIExports\W_Menu_raw.txt
```

If Unreal is not installed under `C:\Program Files\Epic Games\UE_5.7`, pass
`--engine-dir` or `--editor-cmd`, or set `UE_ENGINE_DIR` / `UE_ROOT`.

## Adding Export Support

1. Create a `UMCTExporterBase` subclass.
2. Implement `CanExport()`, `GetSupportedClasses()`, `Export()`, and priority
   when the default priority is not appropriate.
3. Register it in `UMCTExporterRegistry::RegisterDefaultExporters()`.
4. Add simplifier support when the raw/stripped output needs AI-friendly
   restructuring.
5. Update `Resources/CapabilityLayerMatrix.json` if the new export changes
   capability ownership.
6. Regenerate and validate artifacts:

   ```powershell
   python Resources/Scripts/generate_mcp_artifacts.py
   python Resources/Scripts/validate_mcp_contract.py
   python Resources/Scripts/test_mcp_contract.py
   ```

## Troubleshooting

`Asset type not supported`: Run `list_supported_types` and confirm the live
asset class is covered by a registered exporter.

`Python not found`: Set the plugin Python path in Project Settings or ensure the
Python executable is available on `PATH`.

`Simplified file not created`: Inspect the raw file and run
`python Resources/Scripts/simplify_asset.py <raw-file>` manually to see the
simplifier error.

`Commandlet cannot find UnrealEditor-Cmd.exe`: Pass `--editor-cmd` or
`--engine-dir` to `export_asset.py`, or set `UE_ENGINE_DIR` / `UE_ROOT`.

`Export timed out`: Keep the editor responsive, retry with a smaller asset, or
use the commandlet path for long-running exports.
