# Unreal MCP Toolkit Documentation

This directory is the main documentation home for Unreal MCP Toolkit. The root
README is intentionally short for GitHub visitors; detailed workflow,
architecture, reference, and validation material lives here.

Turkish overview: [README.tr.md](README.tr.md).

## Current Surface

- Server name: `MCPToolkit`
- Native TCP commands: 179
- MCP tools: 198
- Client-only MCP tools: 19
- Command categories: 38
- Capability entries: 42
- Primary MCP client: `MCPClient/ai_widget_mcp_client.py`
- Generated artifacts: `Resources/Generated/`
- Current validation target: Unreal Engine 5.7

## Start Here

| Goal | Document |
|---|---|
| Configure an AI assistant and run first commands | [Usage/AI_QUICKSTART.md](Usage/AI_QUICKSTART.md) |
| Understand asset export, formats, simplifiers, and commandlet usage | [Usage/EXPORT_SYSTEM.md](Usage/EXPORT_SYSTEM.md) |
| See every tool and workflow gotcha in one place | [Reference/AI_REFERENCE.md](Reference/AI_REFERENCE.md) |
| Review generated command and schema coverage | [../Resources/Generated/MCPToolkit_ToolCatalog.md](../Resources/Generated/MCPToolkit_ToolCatalog.md) |
| Decide where new features belong | [Reference/CAPABILITY_MATRIX.md](Reference/CAPABILITY_MATRIX.md) |
| Transfer reference UI into Unreal safely | [AI_UI_Transfer/README.md](AI_UI_Transfer/README.md) |
| Author or validate UI transfer specs | [UI_TSpec/README.md](UI_TSpec/README.md) |
| Review CommonUI architecture notes | [CommonUI_Architecture.md](CommonUI_Architecture.md) |
| Resume an AI handoff | [AI_SESSION_HANDOFF.md](AI_SESSION_HANDOFF.md) |

## Documentation Types

Maintained docs:

- `Usage/` contains practical runbooks for humans and AI assistants.
- `Reference/` contains curated reference material and capability ownership.
- `AI_UI_Transfer/` contains the TSpec-first UI transfer workflow and component
  recipes.
- `UI_TSpec/` contains the JSON schema, templates, and examples for portable UI
  specifications.

Generated docs:

- `Resources/Generated/MCPToolkit_ToolCatalog.md`
- `Resources/Generated/MCPToolkit_CommandManifest.json`
- `Resources/Generated/MCPToolkit_ToolSchemas.json`
- `Resources/Generated/MCPToolkit_WrapperSpec.json`
- `Resources/Generated/MCPToolkit_server.json`
- `Resources/Generated/MCPToolkit_MCPWrapperStubs.py`
- `Resources/Generated/MCPToolkit_MCPWrapperRuntime.py`

Do not edit generated files by hand. Regenerate them with:

```powershell
python Resources/Scripts/generate_mcp_artifacts.py
```

## Validation

Run the MCP contract checks after changing native commands, Python wrappers,
capability matrices, generated docs, or schema metadata:

```powershell
python Resources/Scripts/validate_mcp_contract.py
python Resources/Scripts/test_mcp_contract.py
```

Use the preflight script when you want regeneration and validation in one pass:

```powershell
python Resources/Scripts/preflight_mcp.py
```

For UI transfer specs:

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/ValidateUITSpecs.ps1
```

From a host project with the plugin installed:

```powershell
powershell -ExecutionPolicy Bypass -File Plugins/MCPToolkit/Resources/Scripts/ValidateUITSpecs.ps1 -Root . -SpecDirectory Docs/UI_TSpecs
```

## Naming and Compatibility

The plugin identity is `Unreal MCP Toolkit`, with `MCPToolkit` as the module
name and `MCT` as the C++ type prefix.

Some externally visible compatibility names still contain `commonai`:

- MCP resource URIs such as `commonai://commands/manifest`
- Client helper tools such as `commonai_resources_list`
- Legacy environment variables such as `COMMONAI_MCP_HTTP_TOKEN`
- Native HTTP compatibility routes under `/commonai/...`

New documentation should use `MCPToolkit` for the plugin and module name, and
only mention `commonai` when documenting compatibility surfaces that still exist.
