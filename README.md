# Unreal MCP Toolkit

AI/MCP-powered Unreal Editor toolkit for automation, asset export, diagnostics,
Blueprint tooling, and TSpec-first UI mutation.

Primary language: English. Turkish overview: [Docs/README.tr.md](Docs/README.tr.md).

## What It Provides

- 198 MCP tools backed by 179 native TCP commands and 19 client-only tools.
- Local Unreal Editor automation over TCP, Python MCP stdio, and native localhost
  HTTP/MCP.
- Asset export for Widget Blueprints, Blueprints, Animation Blueprints,
  DataAssets, DataTables, Materials, PhysicalMaterials, Worlds, Input assets,
  Audio, and Textures.
- Editor/runtime diagnostics for projects, logs, source control, tests, actors,
  levels, PIE, runtime systems, StaticMesh, SkeletalMesh, Sequencer, landscape,
  foliage, PCG, Niagara, animation assets, CommonUI, navigation, physics, audio,
  streaming, and more.
- Builder-backed mutation for reusable asset authoring domains such as Widget
  Blueprints, Blueprint graphs/components, DataTables, DataAssets, Materials,
  imports, and AnimBlueprint creation.
- Guarded cross-project asset/code transfer, multi-editor routing, async jobs,
  generated schemas, capability matrices, and validation scripts.

Current validation target: Unreal Engine 5.7.

## Quick Start

1. Copy the `MCPToolkit` folder into your Unreal project's `Plugins/` directory.
2. Rebuild the project and launch Unreal Editor. The editor must stay open while
   tools run because the automation server lives inside the editor process.
3. Configure your MCP-capable assistant to run:

   ```text
   python Plugins/MCPToolkit/MCPClient/ai_widget_mcp_client.py
   ```

4. Read the AI quick start:
   [Docs/Usage/AI_QUICKSTART.md](Docs/Usage/AI_QUICKSTART.md).

For direct TCP testing from a host project:

```powershell
python Plugins/MCPToolkit/Resources/Scripts/ai_export_client.py ping
python Plugins/MCPToolkit/Resources/Scripts/ai_export_client.py list_commands
```

From this plugin repository:

```powershell
python Resources/Scripts/preflight_mcp.py
```

## Documentation

Start with [Docs/README.md](Docs/README.md). The root README intentionally stays
small; detailed usage and reference material lives under `Docs/` and generated
artifacts live under `Resources/Generated/`.

| Need | Read |
|---|---|
| Setup and first tool calls | [Docs/Usage/AI_QUICKSTART.md](Docs/Usage/AI_QUICKSTART.md) |
| Build UE 5.7 Win64/Linux packages | [Docs/Usage/BUILD_PLUGIN.md](Docs/Usage/BUILD_PLUGIN.md) |
| Export architecture and formats | [Docs/Usage/EXPORT_SYSTEM.md](Docs/Usage/EXPORT_SYSTEM.md) |
| Full AI tool reference | [Docs/Reference/AI_REFERENCE.md](Docs/Reference/AI_REFERENCE.md) |
| Generated tool catalog | [Resources/Generated/MCPToolkit_ToolCatalog.md](Resources/Generated/MCPToolkit_ToolCatalog.md) |
| Capability and layer ownership | [Docs/Reference/CAPABILITY_MATRIX.md](Docs/Reference/CAPABILITY_MATRIX.md) |
| UI transfer workflow | [Docs/AI_UI_Transfer/README.md](Docs/AI_UI_Transfer/README.md) |
| UI TSpec schema and validation | [Docs/UI_TSpec/README.md](Docs/UI_TSpec/README.md) |
| CommonUI notes | [Docs/CommonUI_Architecture.md](Docs/CommonUI_Architecture.md) |
| Turkish overview | [Docs/README.tr.md](Docs/README.tr.md) |

## Safe UI Automation

Widget Blueprint mutation must start from a valid TSpec. Before mutating a
production WBP, validate specs with:

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/ValidateUITSpecs.ps1
```

For an installed plugin inside a host project:

```powershell
powershell -ExecutionPolicy Bypass -File Plugins/MCPToolkit/Resources/Scripts/ValidateUITSpecs.ps1 -Root . -SpecDirectory Docs/Tasarim/UI_TSpecs
```

If a component behavior is uncertain, use the component recipes in
[Docs/AI_UI_Transfer/component_recipes](Docs/AI_UI_Transfer/component_recipes)
or create a minimal probe WBP before touching production assets.

## Manual Export

In the Content Browser, right-click a supported asset and select
**Export for AI**. Output is written under `Dev/AIExports/`.

![Export for AI Context Menu](Resources/Images/ExportForAI.png)

Headless export:

```powershell
UnrealEditor-Cmd.exe "Project.uproject" -run=MCTExport -asset="/Game/UI/W_Menu" -both -nullrhi -unattended -nosplash -nopause
```

More export details: [Docs/Usage/EXPORT_SYSTEM.md](Docs/Usage/EXPORT_SYSTEM.md).

## Validation

Package UE 5.7 Win64 and Linux builds:

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/BuildPlugin.ps1
```

Run these after command, wrapper, matrix, or documentation-generation changes:

```powershell
python Resources/Scripts/generate_mcp_artifacts.py
python Resources/Scripts/validate_mcp_contract.py
python Resources/Scripts/test_mcp_contract.py
```

For a quick all-in-one check:

```powershell
python Resources/Scripts/preflight_mcp.py
```

## License

See [LICENSE](LICENSE).
