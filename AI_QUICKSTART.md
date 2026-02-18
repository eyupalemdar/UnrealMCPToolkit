# CommonAIExport - AI Quick Start

## THIS IS NOT MCP

**CommonAIExport is a TCP-based system, NOT an MCP server.**

You cannot use MCP tools to interact with this system. You must connect via TCP socket.

---

## How It Works

1. **Unreal Editor must be running** - The TCP server runs inside the Editor
2. **Read port from file**: `{ProjectDir}/Intermediate/AIExport_port.txt`
3. **Connect via TCP socket** to `127.0.0.1` on that port
4. **Send JSON commands**, receive JSON responses

---

## Critical: Path Mirroring

When exporting assets, the output directory **MUST mirror** the asset's Content folder structure:

| Asset Path | Output Directory |
|------------|------------------|
| `/Game/UI/W_Menu` | `Dev/AIExports/Game/UI/` |
| `/Game/Blueprints/BP_Actor` | `Dev/AIExports/Game/Blueprints/` |
| `/Game/Input/IA_Jump` | `Dev/AIExports/Game/Input/` |

**Python helper function:**

```python
def get_output_path(asset_path, project_dir):
    """Convert asset path to mirrored output directory."""
    # "/Game/UI/W_Menu" -> "Game/UI"
    relative = asset_path.lstrip("/").rsplit("/", 1)[0]
    return f"{project_dir}/Dev/AIExports/{relative}/"
```

---

## Quick Commands

```bash
cd {ProjectDir}

# 1. Check connection
python Plugins/CommonAIExport/Resources/Scripts/ai_export_client.py ping

# 2. Export any asset (Blueprint, Widget, DataAsset, etc.)
python Plugins/CommonAIExport/Resources/Scripts/ai_export_client.py export_blueprint '/Game/UI/W_Menu'

# 3. List supported types
python Plugins/CommonAIExport/Resources/Scripts/ai_export_client.py list_types
```

---

## Common Mistakes

| Mistake | Solution |
|---------|----------|
| Using MCP tools | This is TCP, not MCP. Use TCP socket or Python client |
| Editor not running | Start Unreal Editor first, then connect |
| Wrong output path | Must mirror `/Game/...` structure (see table above) |
| Git Bash path mangling | Use PowerShell: `powershell -Command "python script.py '/Game/...'"` |
| Port connection refused | Check `Intermediate/AIExport_port.txt` exists and Editor is running |

---

## TCP Protocol

**Request** (minimal — `output_directory` is optional, omitting it auto-mirrors):
```json
{
  "type": "export_blueprint",
  "params": {
    "asset_path": "/Game/UI/W_Menu",
    "both_formats": true
  }
}
```

**Request** (explicit override):
```json
{
  "type": "export_blueprint",
  "params": {
    "asset_path": "/Game/UI/W_Menu",
    "output_directory": "D:/Project/Dev/AIExports/Game/UI/",
    "both_formats": true
  }
}
```

**Response:**
```json
{
  "success": true,
  "data": {
    "asset_name": "W_Menu",
    "asset_type": "WidgetBlueprint",
    "simplified_file": "D:/Project/Dev/AIExports/Game/UI/W_Menu_simplified.txt"
  }
}
```

---

For detailed documentation, see [README.md](README.md).
