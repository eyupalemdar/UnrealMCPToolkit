# Unreal MCP Toolkit - AI Quick Start

## Two Ways to Use

### 1. MCP Tools (Recommended)

MCPToolkit includes a **Python MCP server** that wraps the TCP protocol.
When configured in an MCP-capable AI assistant, the generated tool surface becomes available with the `mcp__widget-builder__` prefix.

**MCP Client**: `Plugins/MCPToolkit/MCPClient/ai_widget_mcp_client.py`

For full tool reference: **[AI_REFERENCE.md](../Reference/AI_REFERENCE.md)**

### 2. Direct TCP (For scripts, other AI tools)

Raw TCP JSON protocol on `127.0.0.1` — port from `{ProjectDir}/Intermediate/MCTExport_port.txt`.

```json
{"type": "command_name", "params": {"key": "value"}}
```

The native C++ HTTP/MCP endpoint is also available on `127.0.0.1`; its port is
written to `{ProjectDir}/Intermediate/MCTExport_http_port.txt`. Set
`MCPTOOLKIT_HTTP_TOKEN` before launching the editor to require bearer auth on
HTTP requests. `/mcp initialize` returns a `Mcp-Session-Id` header and
`tools/list` supports cursor pagination. `DELETE /mcp` releases a session;
`COMMONAI_MCP_SESSION_TTL_SECONDS` controls expiry and
`MCPTOOLKIT_HTTP_ALLOWED_ORIGINS` controls the local CORS allow-list.
HTTP audit events are written to `Saved/Logs/MCPToolkit_HTTP_Audit.jsonl`;
set `MCPTOOLKIT_HTTP_AUDIT=0` to disable that log. Existing `COMMONAI_*`
and `COMMONAIEXPORT_*` HTTP env names remain supported as compatibility
fallbacks.

---

## Prerequisites

1. **Unreal Editor must be running** — TCP server runs inside the Editor
2. **Port file exists**: `{ProjectDir}/Intermediate/MCTExport_port.txt`
3. **Multi-editor discovery**: each editor with the plugin writes `%LOCALAPPDATA%/MCPToolkit/Editors/*.json`

---

## Quick Test

```bash
# Via MCP
ping  # Returns "pong"

# Via TCP (Python)
python Plugins/MCPToolkit/Resources/Scripts/ai_export_client.py ping
python Plugins/MCPToolkit/Resources/Scripts/ai_export_client.py list_commands

# Regenerate and validate manifest/schema/docs artifacts
python Plugins/MCPToolkit/Resources/Scripts/preflight_mcp.py

# Optional live-editor runtime smoke
python Plugins/MCPToolkit/Resources/Scripts/smoke_mcp_runtime.py
python Plugins/MCPToolkit/Resources/Scripts/smoke_mcp_runtime.py --mutating-smoke
```

For UI transfer/TSpec validation:

```powershell
# From the plugin repository
powershell -ExecutionPolicy Bypass -File Resources/Scripts/ValidateUITSpecs.ps1

# From a host Unreal project where the plugin is installed
powershell -ExecutionPolicy Bypass -File Plugins/MCPToolkit/Resources/Scripts/ValidateUITSpecs.ps1 -Root . -SpecDirectory Docs/Tasarim/UI_TSpecs
```

```bash
# Via MCP, when multiple editors are open
editors_list
editor_call(command="server_status", editor_id="<editor_id from editors_list>")
editor_world_info
actor_list(limit=10)
pie_status
project_status
source_control_status
commonai_resources_list
commonai_prompt_get(name="build_fix_test")
commonai_resource_read(uri="commonai://audit/http")
commonai_prompt_get(name="runtime_debug_triage")
native_http_status
native_mcp_probe
asset_search(path="/Game/UI", limit=20)
editor_log_read(max_lines=200, filter="Error")
asset_transfer_plan(source_asset_path="/Game/UI/Hud/W_MainMenu", source_editor_id="<source>", target_editor_id="<target>")
asset_transfer_execute(source_asset_path="/Game/UI/Hud/W_MainMenu", source_editor_id="<source>", target_editor_id="<target>", scope="write", dry_run=False)
code_transfer_plan(source_paths=["Source/OkeyGame/Public/MyClass.h"], source_editor_id="<source>", target_editor_id="<target>")
```

---

## What Can You Do?

| Category | Examples |
|----------|---------|
| **Create Widgets** | Create WBP, add widgets, set properties, build BP graph |
| **Modify CDO** | Set class defaults, manipulate array properties (e.g. tab lists) |
| **Build BP Logic** | Add events, function calls, connect pins, set defaults |
| **Create Materials** | Materials, material instances, expression graphs |
| **Import Assets** | Textures, fonts from disk |
| **Export Assets** | Widget BP, Blueprint, AnimBP, DataAsset, DataTable, Material, World, Audio, Texture |
| **Control Editor Worlds** | Inspect editor world, list/spawn/move/delete actors, open/save levels, PIE status/start/stop |
| **Inspect Project Health** | Search assets, run light asset validation, read editor logs, source-control status, guarded build status |
| **Use Context Resources** | Read CommonAI resources/prompts and export MCP metadata/command manifests |
| **Probe Native HTTP/MCP** | Check C++ localhost HTTP health and JSON-RPC MCP tools/list |
| **Multi-Editor** | Discover open UE projects and route MCPToolkit commands to a selected editor |
| **Transfer Code** | Plan, copy, and verify C++/config files with hash/collision checks |

---

## Reading Exported Files

Each export produces 3 files:

1. **`_simplified.txt`** — Read this first. AI-friendly, clean structure
2. **`_stripped.txt`** — Non-default values in UE format. For exact pin types, transitions
3. **`_raw.txt`** — ALL values. Debugging only, don't read

Output: `Dev/AIExports/` mirroring Content folder structure.

---

## Critical Path Formats

```
Asset path:       /Game/UI/Kale/Components/W_KaleTabButton
C++ class:        /Script/LyraGame.LyraActivatableWidget
Generated class:  WidgetBlueprintGeneratedClass'/Game/UI/Path/W_Widget.W_Widget_C'
```

---

For comprehensive documentation: **[AI_REFERENCE.md](../Reference/AI_REFERENCE.md)** | **[README.md](../../README.md)**
