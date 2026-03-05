# CommonAIExport — AI Quick Start

## Two Ways to Use

### 1. MCP Tools (Recommended for Claude Code)

CommonAIExport includes a **Python MCP server** that wraps the TCP protocol.
When configured in Claude Code's MCP settings, 55 tools become available with `mcp__widget-builder__` prefix.

**MCP Client**: `Plugins/CommonAIExport/MCPClient/ai_widget_mcp_client.py`

For full tool reference: **[CLAUDE_REFERENCE.md](CLAUDE_REFERENCE.md)**

### 2. Direct TCP (For scripts, other AI tools)

Raw TCP JSON protocol on `127.0.0.1` — port from `{ProjectDir}/Intermediate/AIExport_port.txt`.

```json
{"type": "command_name", "params": {"key": "value"}}
```

---

## Prerequisites

1. **Unreal Editor must be running** — TCP server runs inside the Editor
2. **Port file exists**: `{ProjectDir}/Intermediate/AIExport_port.txt`

---

## Quick Test

```bash
# Via MCP
ping  # Returns "pong"

# Via TCP (Python)
python Plugins/CommonAIExport/Resources/Scripts/ai_export_client.py ping
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
| **Export Assets** | Widget BP, Blueprint, AnimBP, DataAsset, Material, World, Audio, Texture |

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

For comprehensive documentation: **[CLAUDE_REFERENCE.md](CLAUDE_REFERENCE.md)** | **[README.md](README.md)**
