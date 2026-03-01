#!/usr/bin/env python3
"""
AI Widget Builder MCP Server

MCP server that wraps CommonAIExport TCP commands for Claude Code integration.
Provides tools for creating and manipulating Widget Blueprints in Unreal Editor.

Port Discovery:
    Reads {ProjectDir}/Intermediate/AIExport_port.txt automatically.

Usage:
    Add to Claude Code settings (.claude/settings.json):
    {
        "mcpServers": {
            "widget-builder": {
                "command": "python",
                "args": ["D:/Steamworks/Lyra/Plugins/CommonAIExport/MCPClient/ai_widget_mcp_client.py"]
            }
        }
    }
"""

import socket
import json
import os
from pathlib import Path
from fastmcp import FastMCP

mcp = FastMCP("widget-builder", instructions="""
Widget Blueprint Builder for Unreal Engine.
Creates and manipulates Widget Blueprints via the CommonAIExport TCP server.

Typical workflow:
1. create_widget_blueprint - Create a new WBP asset
2. add_widget - Add widgets (TextBlock, Button, Image, VerticalBox, etc.)
3. set_widget_property / set_widget_properties - Set widget properties
4. set_canvas_slot_layout - Position widgets in CanvasPanel
5. compile_and_save - Compile and save to disk
6. get_widget_tree - Verify the result

Property values use UE ImportText format (same format the export system produces).
""")

DEFAULT_PORT = 55560
TIMEOUT = 60
PROJECT_DIR = os.environ.get("UE_PROJECT_DIR", "D:/Steamworks/Lyra")


def _find_port() -> int:
    """Discover TCP server port from port file."""
    port_file = Path(PROJECT_DIR) / "Intermediate" / "AIExport_port.txt"
    if port_file.exists():
        try:
            return int(port_file.read_text().strip())
        except (ValueError, IOError):
            pass

    # Search upward from current directory
    current = Path.cwd()
    for _ in range(10):
        pf = current / "Intermediate" / "AIExport_port.txt"
        if pf.exists():
            try:
                return int(pf.read_text().strip())
            except (ValueError, IOError):
                pass
        parent = current.parent
        if parent == current:
            break
        current = parent

    return DEFAULT_PORT


def _send_command(cmd_type: str, params: dict | None = None) -> dict:
    """Send a TCP command to the CommonAIExport server."""
    port = _find_port()

    command = {"type": cmd_type}
    if params:
        command["params"] = params

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(TIMEOUT)
        s.connect(("127.0.0.1", port))
        s.sendall(json.dumps(command).encode("utf-8"))

        data = b""
        while True:
            try:
                chunk = s.recv(65536)
                if not chunk:
                    break
                data += chunk
                try:
                    json.loads(data.decode("utf-8"))
                    break
                except json.JSONDecodeError:
                    continue
            except socket.timeout:
                break

        s.close()

        if data:
            return json.loads(data.decode("utf-8"))
        return {"success": False, "error": "No response from server"}

    except ConnectionRefusedError:
        return {"success": False, "error": f"Connection refused on port {port}. Is Unreal Editor running with CommonAIExport?"}
    except socket.timeout:
        return {"success": False, "error": "Connection timed out"}
    except Exception as e:
        return {"success": False, "error": str(e)}


def _format_response(result: dict) -> str:
    """Format a TCP response as a readable string."""
    return json.dumps(result, indent=2, ensure_ascii=False)


# =============================================================================
# CONNECTION
# =============================================================================

@mcp.tool()
def ping() -> str:
    """Check if the Unreal Editor TCP server is running and responsive."""
    return _format_response(_send_command("ping"))


# =============================================================================
# WIDGET BLUEPRINT LIFECYCLE
# =============================================================================

@mcp.tool()
def create_widget_blueprint(
    package_path: str,
    asset_name: str,
    parent_class: str = ""
) -> str:
    """
    Create a new Widget Blueprint asset in the Unreal project.

    Args:
        package_path: Content path, e.g. "/Game/UI/Screens"
        asset_name: Asset name, e.g. "WBP_MyScreen"
        parent_class: Optional parent class path. Defaults to UUserWidget.
                     e.g. "/Script/LyraGame.LyraActivatableWidget"

    Returns:
        JSON with asset_path and asset_name on success.
    """
    params = {"package_path": package_path, "asset_name": asset_name}
    if parent_class:
        params["parent_class"] = parent_class
    return _format_response(_send_command("create_widget_blueprint", params))


@mcp.tool()
def compile_and_save(asset_path: str) -> str:
    """
    Compile and save a Widget Blueprint to disk.

    Args:
        asset_path: Asset path, e.g. "/Game/UI/Screens/WBP_MyScreen"

    Returns:
        JSON with compiled/saved status and any warnings.
    """
    return _format_response(_send_command("compile_and_save", {"asset_path": asset_path}))


# =============================================================================
# WIDGET TREE MANIPULATION
# =============================================================================

@mcp.tool()
def add_widget(
    asset_path: str,
    widget_class: str,
    widget_name: str,
    parent_name: str = ""
) -> str:
    """
    Add a widget to the widget tree of a Widget Blueprint.

    Args:
        asset_path: Asset path of the Widget Blueprint
        widget_class: Widget class short name. Common types:
                     Panels: CanvasPanel, VerticalBox (VBox), HorizontalBox (HBox),
                             Overlay, GridPanel, WrapBox, ScrollBox, WidgetSwitcher
                     Content: TextBlock (Text), Image, Button, CheckBox, Slider,
                             ProgressBar, EditableTextBox, ComboBoxString
                     Lyra: LyraButtonBase, CommonTextBlock
        widget_name: Name for the new widget (e.g. "TitleText", "ActionButton")
        parent_name: Name of parent panel widget. Empty = set as root widget.

    Returns:
        JSON with widget_name and widget_class of the created widget.
    """
    params = {
        "asset_path": asset_path,
        "widget_class": widget_class,
        "widget_name": widget_name,
    }
    if parent_name:
        params["parent_name"] = parent_name
    return _format_response(_send_command("add_widget", params))


@mcp.tool()
def remove_widget(asset_path: str, widget_name: str) -> str:
    """
    Remove a widget from the widget tree.

    Args:
        asset_path: Asset path of the Widget Blueprint
        widget_name: Name of the widget to remove

    Returns:
        JSON with removal confirmation.
    """
    return _format_response(_send_command("remove_widget", {
        "asset_path": asset_path,
        "widget_name": widget_name,
    }))


@mcp.tool()
def move_widget(
    asset_path: str,
    widget_name: str,
    new_parent_name: str,
    index: int = -1
) -> str:
    """
    Move a widget to a new parent in the widget tree.

    Args:
        asset_path: Asset path of the Widget Blueprint
        widget_name: Name of the widget to move
        new_parent_name: Name of the new parent panel. Empty = make root.
        index: Position among siblings (-1 = append at end)

    Returns:
        JSON confirming the move.
    """
    params = {
        "asset_path": asset_path,
        "widget_name": widget_name,
        "new_parent_name": new_parent_name,
    }
    if index >= 0:
        params["index"] = index
    return _format_response(_send_command("move_widget", params))


# =============================================================================
# PROPERTY SETTING
# =============================================================================

@mcp.tool()
def set_widget_property(
    asset_path: str,
    widget_name: str,
    property_name: str,
    value: str
) -> str:
    """
    Set a property on a widget using UE reflection.

    Supports dot-notation for struct properties: "Font.Size", "ColorAndOpacity.A"
    Values use UE ImportText format.

    Args:
        asset_path: Asset path of the Widget Blueprint
        widget_name: Name of the target widget
        property_name: Property name (e.g. "Text", "Font.Size", "Visibility")
        value: Value in ImportText format.
              Examples:
              - Text: 'NSLOCTEXT("NS", "Key", "Display Text")'
              - Font size: "24"
              - Color: "(R=1.0,G=0.5,B=0.0,A=1.0)"
              - Enum: "ESlateVisibility::Collapsed"

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("set_widget_property", {
        "asset_path": asset_path,
        "widget_name": widget_name,
        "property_name": property_name,
        "value": value,
    }))


@mcp.tool()
def set_slot_property(
    asset_path: str,
    widget_name: str,
    property_name: str,
    value: str
) -> str:
    """
    Set a slot property on a widget (the parent panel's slot).

    Args:
        asset_path: Asset path of the Widget Blueprint
        widget_name: Name of the widget whose slot to modify
        property_name: Slot property name (e.g. "Padding", "HorizontalAlignment")
        value: Value in ImportText format.
              Examples:
              - Padding: "(Left=10,Top=5,Right=10,Bottom=5)"
              - HorizontalAlignment: "EHorizontalAlignment::Center"
              - VerticalAlignment: "EVerticalAlignment::Fill"
              - Size (for SizeBox slot): "(SizeRule=Fill,Value=1.0)"

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("set_slot_property", {
        "asset_path": asset_path,
        "widget_name": widget_name,
        "property_name": property_name,
        "value": value,
    }))


@mcp.tool()
def set_canvas_slot_layout(
    asset_path: str,
    widget_name: str,
    position_x: float = 0,
    position_y: float = 0,
    size_x: float = 100,
    size_y: float = 30,
    anchor_min_x: float = 0,
    anchor_min_y: float = 0,
    anchor_max_x: float = 0,
    anchor_max_y: float = 0,
    alignment_x: float = 0,
    alignment_y: float = 0
) -> str:
    """
    Set canvas slot layout for a widget inside a CanvasPanel.

    Args:
        asset_path: Asset path of the Widget Blueprint
        widget_name: Name of the widget to position
        position_x: X position offset
        position_y: Y position offset
        size_x: Width
        size_y: Height
        anchor_min_x: Minimum anchor X (0-1). 0=left, 0.5=center, 1=right
        anchor_min_y: Minimum anchor Y (0-1). 0=top, 0.5=center, 1=bottom
        anchor_max_x: Maximum anchor X. Same as min for point anchor, different for stretch.
        anchor_max_y: Maximum anchor Y.
        alignment_x: Pivot X (0-1). 0=left edge, 0.5=center, 1=right edge
        alignment_y: Pivot Y (0-1). 0=top edge, 0.5=center, 1=bottom edge

    Returns:
        JSON with success status and layout summary.
    """
    return _format_response(_send_command("set_canvas_slot_layout", {
        "asset_path": asset_path,
        "widget_name": widget_name,
        "position_x": position_x,
        "position_y": position_y,
        "size_x": size_x,
        "size_y": size_y,
        "anchor_min_x": anchor_min_x,
        "anchor_min_y": anchor_min_y,
        "anchor_max_x": anchor_max_x,
        "anchor_max_y": anchor_max_y,
        "alignment_x": alignment_x,
        "alignment_y": alignment_y,
    }))


@mcp.tool()
def set_widget_properties(
    asset_path: str,
    widget_name: str,
    properties: str
) -> str:
    """
    Set multiple properties on a widget in one call.

    Args:
        asset_path: Asset path of the Widget Blueprint
        widget_name: Name of the target widget
        properties: JSON string of property name->value pairs.
                   Example: '{"Text": "Hello", "Font.Size": "24", "ColorAndOpacity": "(R=1,G=1,B=1,A=1)"}'

    Returns:
        JSON with set_count and any failed property names.
    """
    try:
        props_dict = json.loads(properties)
    except json.JSONDecodeError:
        return json.dumps({"success": False, "error": "Invalid JSON in properties parameter"})

    return _format_response(_send_command("set_widget_properties", {
        "asset_path": asset_path,
        "widget_name": widget_name,
        "properties": props_dict,
    }))


# =============================================================================
# QUERY / INSPECTION
# =============================================================================

@mcp.tool()
def get_widget_tree(asset_path: str) -> str:
    """
    Get the widget tree of a Widget Blueprint as JSON.

    Useful for verifying the current state of a widget blueprint
    after making changes.

    Args:
        asset_path: Asset path of the Widget Blueprint

    Returns:
        JSON tree with name, type, slot info, children for each widget.
    """
    return _format_response(_send_command("get_widget_tree", {"asset_path": asset_path}))


@mcp.tool()
def list_widget_classes() -> str:
    """
    List all available widget classes that can be used with add_widget.

    Returns:
        JSON with array of classes (name and is_panel flag).
    """
    return _format_response(_send_command("list_widget_classes"))


@mcp.tool()
def list_supported_types() -> str:
    """List all asset types supported for export."""
    return _format_response(_send_command("list_supported_types"))


# =============================================================================
# EXPORT (existing functionality)
# =============================================================================

@mcp.tool()
def export_widget(
    asset_path: str,
    output_directory: str = "",
    both_formats: bool = True
) -> str:
    """
    Export a Widget Blueprint to text format for analysis.

    Args:
        asset_path: Asset path, e.g. "/Game/UI/W_MainMenu"
        output_directory: Output directory. Empty = auto-mirrored path.
        both_formats: Export both raw and simplified formats.

    Returns:
        JSON with file paths of exported files.
    """
    params = {"asset_path": asset_path, "both_formats": both_formats}
    if output_directory:
        params["output_directory"] = output_directory
    return _format_response(_send_command("export_widget", params))


@mcp.tool()
def export_blueprint(
    asset_path: str,
    output_directory: str = "",
    both_formats: bool = True
) -> str:
    """
    Export a Blueprint to text format for analysis.

    Args:
        asset_path: Asset path, e.g. "/Game/Blueprints/BP_Player"
        output_directory: Output directory. Empty = auto-mirrored path.
        both_formats: Export both raw and simplified formats.

    Returns:
        JSON with file paths of exported files.
    """
    params = {"asset_path": asset_path, "both_formats": both_formats}
    if output_directory:
        params["output_directory"] = output_directory
    return _format_response(_send_command("export_blueprint", params))


if __name__ == "__main__":
    mcp.run()
