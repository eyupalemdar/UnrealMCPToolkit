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
                "args": ["<ProjectDir>/Plugins/CommonAIExport/MCPClient/ai_widget_mcp_client.py"]
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

Material Builder workflow:
1. create_material - Create a new Material asset (domain, blend mode, shading model)
2. add_expression - Add expression nodes (Multiply, Add, Color, ScalarParam, etc.)
3. set_expression_property - Set node properties via reflection
4. connect_expressions - Wire nodes together
5. connect_to_material_property - Wire to root (EmissiveColor, Opacity, etc.)
6. compile_material - Recompile and save
7. get_material_graph - Verify the result

Material Instance workflow:
1. create_material_instance - Create MIC with parent
2. set_instance_parameter - Set scalar/vector/texture parameters
3. save_material_instance - Save to disk

Generic Asset Factory workflow:
1. create_asset - Create InputAction, InputMappingContext, Sound*, PhysicalMaterial
2. set_asset_property - Set properties via reflection (ImportText format)
3. save_asset - Save to disk
4. get_asset_properties - Inspect properties

Input Mapping Context workflow:
1. create_asset (asset_type="InputMappingContext") - Create IMC
2. add_input_mapping - Add key bindings with triggers/modifiers
3. remove_input_mapping - Remove bindings by index
4. get_input_mappings - Inspect current bindings
5. save_asset - Save to disk

AnimBlueprint workflow:
1. create_anim_blueprint - Create with skeleton and parent class
2. Use Blueprint Graph tools (add_event_node, etc.) for EventGraph
3. compile_and_save - Compile and save
4. get_anim_blueprint_info - Inspect result
""")

DEFAULT_PORT = 55560
TIMEOUT = 60

# Derive project dir from script location: <ProjectDir>/Plugins/CommonAIExport/MCPClient/this_script.py
_SCRIPT_DIR = Path(__file__).resolve().parent
_DEFAULT_PROJECT_DIR = str(_SCRIPT_DIR.parent.parent.parent)
PROJECT_DIR = os.environ.get("UE_PROJECT_DIR", _DEFAULT_PROJECT_DIR)


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
# BLUEPRINT UTILITY
# =============================================================================

@mcp.tool()
def reparent_blueprint(
    asset_path: str,
    new_parent_class: str
) -> str:
    """
    Change the parent class of a Widget Blueprint.

    Args:
        asset_path: Asset path of the Widget Blueprint to reparent,
                   e.g. "/Game/UI/LoadingScreen/W_LoadingScreen_Kale"
        new_parent_class: Full class path of the new parent class,
                         e.g. "/Script/LyraGame.LyraLoadingScreenWidget"

    Returns:
        JSON with old_parent and new_parent on success.
    """
    return _format_response(_send_command("reparent_blueprint", {
        "asset_path": asset_path,
        "new_parent_class": new_parent_class,
    }))


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


# =============================================================================
# MATERIAL BUILDER
# =============================================================================

@mcp.tool()
def create_material(
    package_path: str,
    asset_name: str,
    domain: str = "Surface",
    blend_mode: str = "Opaque",
    shading_model: str = "DefaultLit",
    two_sided: bool = False
) -> str:
    """
    Create a new Material asset.

    Args:
        package_path: Content path, e.g. "/Game/Materials"
        asset_name: Asset name, e.g. "M_Gold"
        domain: "Surface", "PostProcess", "UI", "Volume", "LightFunction", "DeferredDecal"
        blend_mode: "Opaque", "Masked", "Translucent", "Additive", "Modulate"
        shading_model: "DefaultLit", "Unlit", "Subsurface", "TwoSidedFoliage", etc.
        two_sided: Enable two-sided rendering

    Returns:
        JSON with asset_path and asset_name on success.
    """
    params = {
        "package_path": package_path,
        "asset_name": asset_name,
        "domain": domain,
        "blend_mode": blend_mode,
        "shading_model": shading_model,
        "two_sided": two_sided,
    }
    return _format_response(_send_command("create_material", params))


@mcp.tool()
def set_material_property(
    asset_path: str,
    property_name: str,
    value: str
) -> str:
    """
    Set a material-level property.

    Args:
        asset_path: Asset path of the Material
        property_name: UMaterial property name (e.g. "BlendMode", "bTwoSided")
        value: Value in ImportText format

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("set_material_property", {
        "asset_path": asset_path,
        "property_name": property_name,
        "value": value,
    }))


@mcp.tool()
def add_expression(
    asset_path: str,
    expression_class: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0
) -> str:
    """
    Add a material expression node to the material graph.

    Args:
        asset_path: Asset path of the Material
        expression_class: Short class name or alias:
            Math: Multiply, Add, Subtract, Divide, Lerp, Clamp, OneMinus, Power, Abs, Saturate
            Constants: Constant, Constant3 (Color), Constant4
            Parameters: ScalarParam, VectorParam, TextureSample (Texture)
            Coords: TexCoord (UV), Panner
            Vector: Append, Mask (ComponentMask), Dot, Cross, Normalize, Length, Distance
            Logic: If, StaticSwitch
            Utility: Time, VertexColor, Fresnel, WorldPos, Comment
        node_name: Logical name for the node (used for lookup in other commands)
        pos_x: X position in graph (negative = left of root inputs)
        pos_y: Y position in graph

    Returns:
        JSON with node_name, expression_class, and position.
    """
    return _format_response(_send_command("add_expression", {
        "asset_path": asset_path,
        "expression_class": expression_class,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
    }))


@mcp.tool()
def set_expression_property(
    asset_path: str,
    node_name: str,
    property_name: str,
    value: str
) -> str:
    """
    Set a property on a material expression node via UE reflection.

    Args:
        asset_path: Asset path of the Material
        node_name: Node name (as set by add_expression)
        property_name: Property on the expression class (supports dot-notation)
        value: Value in ImportText format. Examples:
               - Constant3Vector color: "(R=1.0,G=0.843,B=0.0,A=1.0)"
               - ScalarParameter default: "0.5"
               - TextureSample texture: "Texture2D'/Game/Textures/T_Gold.T_Gold'"
               - ComponentMask R: "true"

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("set_expression_property", {
        "asset_path": asset_path,
        "node_name": node_name,
        "property_name": property_name,
        "value": value,
    }))


@mcp.tool()
def connect_expressions(
    asset_path: str,
    from_node: str,
    from_output: str,
    to_node: str,
    to_input: str
) -> str:
    """
    Connect two material expression nodes.

    Args:
        asset_path: Asset path of the Material
        from_node: Source node name
        from_output: Output pin name on source (e.g. "RGB", "R", "G", "" for default)
        to_node: Target node name
        to_input: Input pin name on target (e.g. "A", "B", "Alpha", "Base")

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("connect_expressions", {
        "asset_path": asset_path,
        "from_node": from_node,
        "from_output": from_output,
        "to_node": to_node,
        "to_input": to_input,
    }))


@mcp.tool()
def connect_to_material_property(
    asset_path: str,
    from_node: str,
    from_output: str,
    material_property: str
) -> str:
    """
    Connect an expression output to a material root input property.

    Args:
        asset_path: Asset path of the Material
        from_node: Source node name
        from_output: Output pin (e.g. "RGB", "" for default)
        material_property: Root property name:
            "BaseColor", "Metallic", "Roughness", "Specular", "Normal",
            "EmissiveColor" (or "Emissive"), "Opacity", "OpacityMask",
            "WorldPositionOffset" (or "WPO"), "AmbientOcclusion" (or "AO"),
            "SubsurfaceColor", "Refraction", "PixelDepthOffset" (or "PDO")

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("connect_to_material_property", {
        "asset_path": asset_path,
        "from_node": from_node,
        "from_output": from_output,
        "material_property": material_property,
    }))


@mcp.tool()
def disconnect_input(
    asset_path: str,
    node_name: str,
    input_name: str
) -> str:
    """
    Disconnect a specific input on an expression node.

    Args:
        asset_path: Asset path of the Material
        node_name: Target expression node name
        input_name: Input pin name to disconnect (e.g. "A", "B", "Alpha")

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("disconnect_input", {
        "asset_path": asset_path,
        "node_name": node_name,
        "input_name": input_name,
    }))


@mcp.tool()
def remove_expression(asset_path: str, node_name: str) -> str:
    """
    Remove an expression node from the material graph.

    Args:
        asset_path: Asset path of the Material
        node_name: Node name to remove

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("remove_expression", {
        "asset_path": asset_path,
        "node_name": node_name,
    }))


@mcp.tool()
def compile_material(asset_path: str) -> str:
    """
    Recompile a material and save to disk.

    Args:
        asset_path: Asset path of the Material

    Returns:
        JSON with compiled/saved status and any warnings.
    """
    return _format_response(_send_command("compile_material", {"asset_path": asset_path}))


@mcp.tool()
def get_material_graph(asset_path: str) -> str:
    """
    Get the material graph as JSON (expressions + connections).

    Args:
        asset_path: Asset path of the Material

    Returns:
        JSON with domain, blend_mode, expressions array, and connections array.
    """
    return _format_response(_send_command("get_material_graph", {"asset_path": asset_path}))


@mcp.tool()
def list_expression_classes() -> str:
    """
    List all available material expression classes.

    Returns:
        JSON with array of class names.
    """
    return _format_response(_send_command("list_expression_classes"))


# =============================================================================
# MATERIAL INSTANCE
# =============================================================================

@mcp.tool()
def create_material_instance(
    package_path: str,
    asset_name: str,
    parent_material_path: str
) -> str:
    """
    Create a Material Instance Constant (MIC).

    Args:
        package_path: Content path, e.g. "/Game/Materials/Instances"
        asset_name: Asset name, e.g. "MI_GoldVariant"
        parent_material_path: Path to parent material, e.g. "/Game/Materials/M_Gold"

    Returns:
        JSON with asset_path and asset_name on success.
    """
    return _format_response(_send_command("create_material_instance", {
        "package_path": package_path,
        "asset_name": asset_name,
        "parent_material_path": parent_material_path,
    }))


@mcp.tool()
def set_instance_parameter(
    asset_path: str,
    param_name: str,
    param_type: str,
    value: str
) -> str:
    """
    Set a parameter on a Material Instance Constant.

    Args:
        asset_path: Asset path of the MIC
        param_name: Parameter name (must match parameter in parent material)
        param_type: "scalar", "vector", or "texture"
        value: Value string:
               scalar: "0.5"
               vector: "(R=1.0,G=0.0,B=0.0,A=1.0)"
               texture: "/Game/Textures/T_MyTex"

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("set_instance_parameter", {
        "asset_path": asset_path,
        "param_name": param_name,
        "param_type": param_type,
        "value": value,
    }))


@mcp.tool()
def save_material_instance(asset_path: str) -> str:
    """
    Save a Material Instance Constant to disk.

    Args:
        asset_path: Asset path of the MIC

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("save_material_instance", {"asset_path": asset_path}))


@mcp.tool()
def get_material_instance_info(asset_path: str) -> str:
    """
    Get info about a Material Instance Constant as JSON.

    Args:
        asset_path: Asset path of the MIC

    Returns:
        JSON with parent material, scalar/vector/texture parameters.
    """
    return _format_response(_send_command("get_material_instance_info", {"asset_path": asset_path}))


# =============================================================================
# ASSET IMPORT
# =============================================================================

@mcp.tool()
def import_texture(
    source_path: str,
    package_path: str,
    asset_name: str = "",
    compression: str = "UserInterface2D",
    srgb: bool = True,
    mip_gen: str = "NoMipmaps",
    lod_group: str = "UI"
) -> str:
    """
    Import a texture file from disk into the Unreal project.

    Args:
        source_path: Absolute path to the source image file (PNG, TGA, JPG, BMP, EXR)
        package_path: Content path to import into, e.g. "/Game/UI/Kale/Textures"
        asset_name: Asset name. If empty, derived from source filename.
        compression: Compression setting:
                    "Default" - General purpose
                    "UserInterface2D" or "UI" - Best for UI textures (crisp, no artifacts)
                    "NormalMap" - For normal maps
                    "Masks" - For mask textures (no sRGB)
                    "HDR" - For HDR textures
                    "Grayscale" - For grayscale/displacement
                    "Alpha" - For alpha channels
        srgb: Enable sRGB color space (True for color textures, False for data textures)
        mip_gen: Mipmap generation setting:
                "NoMipmaps" - No mipmaps (best for UI)
                "FromTextureGroup" - Use texture group default
                "Sharpen" - Sharpen mipmaps
                "Blur" - Blur mipmaps
        lod_group: LOD/Texture group:
                  "UI" - User Interface textures
                  "World" - World textures
                  "Character" - Character textures
                  "Effects" - Effect textures
                  "Lightmap" - Lightmaps
                  "Shadowmap" - Shadowmaps

    Returns:
        JSON with asset_path, asset_name, width, height, format, and saved status.
    """
    params = {
        "source_path": source_path,
        "package_path": package_path,
        "asset_name": asset_name,
        "compression": compression,
        "srgb": srgb,
        "mip_gen": mip_gen,
        "lod_group": lod_group,
    }
    return _format_response(_send_command("import_texture", params))


@mcp.tool()
def import_font(
    package_path: str,
    font_name: str,
    faces: list[dict],
    hinting: str = "Auto"
) -> str:
    """
    Import font files (TTF/OTF) and create a Composite Font asset.

    Creates individual UFontFace assets for each weight/style, then
    creates a composite UFont that ties them all together.

    Args:
        package_path: Content path for font assets, e.g. "/Game/UI/Kale/Fonts/Inter"
        font_name: Name for the composite font, e.g. "Inter"
        faces: List of font face entries. Each entry is a dict with:
              - "source_path": Absolute path to TTF/OTF file
              - "name": Weight/style name (e.g. "Regular", "Bold", "Medium", "SemiBold")
              Example: [
                  {"source_path": "/path/to/Inter-Regular.ttf", "name": "Regular"},
                  {"source_path": "/path/to/Inter-Bold.ttf", "name": "Bold"}
              ]
        hinting: Font hinting mode: "Auto" (default), "AutoLight", "None"

    Returns:
        JSON with font_asset_path, font_name, face_count, and individual face asset paths.
    """
    params = {
        "package_path": package_path,
        "font_name": font_name,
        "faces": faces,
        "hinting": hinting,
    }
    return _format_response(_send_command("import_font", params))


# =============================================================================
# CDO (CLASS DEFAULT OBJECT) PROPERTIES
# =============================================================================

@mcp.tool()
def set_cdo_property(
    asset_path: str,
    property_name: str,
    value: str
) -> str:
    """
    Set a Class Default Object property on a Blueprint.

    Works on the Blueprint's generated class CDO, not widget instances in the tree.
    Use for properties like bSelectable, bIsFocusable, ClickMethod, MinWidth, etc.

    Args:
        asset_path: Asset path of the Widget Blueprint
        property_name: CDO property name (supports dot-notation for structs)
        value: Value in ImportText format

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("set_cdo_property", {
        "asset_path": asset_path,
        "property_name": property_name,
        "value": value,
    }))


@mcp.tool()
def get_cdo_properties(asset_path: str) -> str:
    """
    Get CDO properties of a Blueprint as JSON.

    Returns:
        JSON with property name -> value pairs.
    """
    return _format_response(_send_command("get_cdo_properties", {
        "asset_path": asset_path,
    }))


# =============================================================================
# CDO ARRAY PROPERTIES
# =============================================================================

@mcp.tool()
def add_cdo_array_element(
    asset_path: str,
    array_name: str,
    element_values: str = "{}",
    class_name: str = ""
) -> str:
    """
    Add an element to a CDO array property.

    For struct arrays (e.g. PreregisteredTabInfoArray), pass element_values as
    a JSON object with sub-property names and their ImportText values.

    For instanced UObject arrays (e.g. TArray<TObjectPtr<UGameFeatureAction>> with
    UPROPERTY(Instanced)), pass class_name with the full class path to instantiate.

    Works with Widget Blueprints, Blueprint data assets (BPTYPE_Const), and
    plain data assets.

    Args:
        asset_path: Asset path of the Blueprint or data asset
        array_name: Name of the TArray property on the CDO
        element_values: JSON object string of sub-property name -> value pairs.
                       Example: '{"TabNameID": "Play", "TabText": "NSLOCTEXT(\\"\\", \\"\\", \\"PLAY\\")"}'
        class_name: Full class path for instanced UObject arrays (e.g. "/Script/OkeyGame.OkeyAction_ConfigureGameInstance").
                   Leave empty for struct/simple type arrays.

    Returns:
        JSON with the index of the newly added element.
    """
    return _format_response(_send_command("add_cdo_array_element", {
        "asset_path": asset_path,
        "array_name": array_name,
        "element_values": element_values,
        "class_name": class_name,
    }))


@mcp.tool()
def set_cdo_array_element_property(
    asset_path: str,
    array_name: str,
    index: int,
    property_name: str,
    value: str
) -> str:
    """
    Set a sub-property on a specific CDO array element.

    Args:
        asset_path: Asset path of the Widget Blueprint
        array_name: Name of the TArray property
        index: Element index (0-based)
        property_name: Sub-property name within the element (supports dot-notation)
        value: Value in ImportText format

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("set_cdo_array_element_property", {
        "asset_path": asset_path,
        "array_name": array_name,
        "index": index,
        "property_name": property_name,
        "value": value,
    }))


@mcp.tool()
def remove_cdo_array_element(
    asset_path: str,
    array_name: str,
    index: int
) -> str:
    """
    Remove an element from a CDO array property by index.

    Args:
        asset_path: Asset path of the Widget Blueprint
        array_name: Name of the TArray property
        index: Element index to remove (0-based)

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("remove_cdo_array_element", {
        "asset_path": asset_path,
        "array_name": array_name,
        "index": index,
    }))


@mcp.tool()
def get_cdo_array_length(
    asset_path: str,
    array_name: str
) -> str:
    """
    Get the length of a CDO array property.

    Args:
        asset_path: Asset path of the Widget Blueprint
        array_name: Name of the TArray property

    Returns:
        JSON with array_name and length.
    """
    return _format_response(_send_command("get_cdo_array_length", {
        "asset_path": asset_path,
        "array_name": array_name,
    }))


# =============================================================================
# BLUEPRINT GRAPH MANIPULATION
# =============================================================================

@mcp.tool()
def add_event_node(
    asset_path: str,
    event_name: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0
) -> str:
    """
    Add an event override node to the Blueprint's event graph.

    Creates a node that overrides a parent class event (e.g. BP_OnSelected,
    BP_OnDeselected, BP_OnHovered, ReceiveBeginPlay, HandleTabCreation).

    Args:
        asset_path: Asset path of the Blueprint
        event_name: Function name to override (e.g. "BP_OnSelected", "ReceiveBeginPlay")
        node_name: Logical name for later reference in connect_pins etc.
        pos_x: X position in graph
        pos_y: Y position in graph

    Returns:
        JSON with node_name and event_name on success.
    """
    return _format_response(_send_command("add_event_node", {
        "asset_path": asset_path,
        "event_name": event_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
    }))


@mcp.tool()
def add_custom_event(
    asset_path: str,
    event_name: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0
) -> str:
    """
    Add a Custom Event node to the Blueprint's event graph.

    Args:
        asset_path: Asset path of the Blueprint
        event_name: Custom event display name
        node_name: Logical name for later reference
        pos_x: X position in graph
        pos_y: Y position in graph

    Returns:
        JSON with node_name and event_name.
    """
    return _format_response(_send_command("add_custom_event", {
        "asset_path": asset_path,
        "event_name": event_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
    }))


@mcp.tool()
def add_function_call(
    asset_path: str,
    function_name: str,
    node_name: str,
    target_class: str = "",
    pos_x: int = 0,
    pos_y: int = 0
) -> str:
    """
    Add a function call node to the Blueprint's event graph.

    Searches for the function in: target_class (if given), Blueprint parent class
    hierarchy, and common UMG/CommonUI classes.

    Args:
        asset_path: Asset path of the Blueprint
        function_name: Function to call (e.g. "SetText", "SetVisibility",
                       "SetColorAndOpacity", "AddChildToHorizontalBox")
        node_name: Logical name for later reference
        target_class: Optional class path to search (e.g. "/Script/UMG.TextBlock")
        pos_x: X position in graph
        pos_y: Y position in graph

    Returns:
        JSON with node_name and function_name.
    """
    params = {
        "asset_path": asset_path,
        "function_name": function_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
    }
    if target_class:
        params["target_class"] = target_class
    return _format_response(_send_command("add_function_call", params))


@mcp.tool()
def add_variable_get_node(
    asset_path: str,
    variable_name: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0
) -> str:
    """
    Add a Variable Get node to the graph.

    Args:
        asset_path: Asset path of the Blueprint
        variable_name: Blueprint variable name (e.g. "ButtonTextBlock")
        node_name: Logical name for later reference
        pos_x: X position in graph
        pos_y: Y position in graph

    Returns:
        JSON with node_name and variable_name.
    """
    return _format_response(_send_command("add_variable_get_node", {
        "asset_path": asset_path,
        "variable_name": variable_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
    }))


@mcp.tool()
def add_variable_set_node(
    asset_path: str,
    variable_name: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0
) -> str:
    """
    Add a Variable Set node to the graph.

    Args:
        asset_path: Asset path of the Blueprint
        variable_name: Blueprint variable name
        node_name: Logical name for later reference
        pos_x: X position in graph
        pos_y: Y position in graph

    Returns:
        JSON with node_name and variable_name.
    """
    return _format_response(_send_command("add_variable_set_node", {
        "asset_path": asset_path,
        "variable_name": variable_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
    }))


@mcp.tool()
def add_make_struct_node(
    asset_path: str,
    struct_name: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0
) -> str:
    """
    Add a Make Struct node to the graph.

    Args:
        asset_path: Asset path of the Blueprint
        struct_name: Struct type name (e.g. "LinearColor", "Vector", "SlateFontInfo")
        node_name: Logical name for later reference
        pos_x: X position in graph
        pos_y: Y position in graph

    Returns:
        JSON with node_name and struct_name.
    """
    return _format_response(_send_command("add_make_struct_node", {
        "asset_path": asset_path,
        "struct_name": struct_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
    }))


@mcp.tool()
def add_branch_node(
    asset_path: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0
) -> str:
    """
    Add a Branch (if/else) node to the graph.

    Args:
        asset_path: Asset path of the Blueprint
        node_name: Logical name for later reference
        pos_x: X position in graph
        pos_y: Y position in graph

    Returns:
        JSON with node_name.
    """
    return _format_response(_send_command("add_branch_node", {
        "asset_path": asset_path,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
    }))


@mcp.tool()
def connect_pins(
    asset_path: str,
    from_node: str,
    from_pin: str,
    to_node: str,
    to_pin: str
) -> str:
    """
    Connect an output pin to an input pin between two nodes.

    Uses logical node names (set when creating nodes). Pin names support
    aliases: "exec"/"execute"/"then" for execution pins, "self"/"target" for self pins.

    Args:
        asset_path: Asset path of the Blueprint
        from_node: Source node logical name
        from_pin: Output pin name (e.g. "then", "ReturnValue", "output")
        to_node: Target node logical name
        to_pin: Input pin name (e.g. "execute", "self", "InText", "NewVisibility")

    Returns:
        JSON with connection details.
    """
    return _format_response(_send_command("connect_pins", {
        "asset_path": asset_path,
        "from_node": from_node,
        "from_pin": from_pin,
        "to_node": to_node,
        "to_pin": to_pin,
    }))


@mcp.tool()
def set_pin_default(
    asset_path: str,
    node_name: str,
    pin_name: str,
    default_value: str
) -> str:
    """
    Set a pin's default value (for unconnected input pins).

    Args:
        asset_path: Asset path of the Blueprint
        node_name: Target node logical name
        pin_name: Pin name on the node
        default_value: Value as string (ImportText format)

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("set_pin_default", {
        "asset_path": asset_path,
        "node_name": node_name,
        "pin_name": pin_name,
        "default_value": default_value,
    }))


@mcp.tool()
def remove_graph_node(asset_path: str, node_name: str) -> str:
    """
    Remove a node from the Blueprint graph by its logical name.

    Args:
        asset_path: Asset path of the Blueprint
        node_name: Logical name of the node to remove

    Returns:
        JSON with removal confirmation.
    """
    return _format_response(_send_command("remove_graph_node", {
        "asset_path": asset_path,
        "node_name": node_name,
    }))


@mcp.tool()
def get_graph(asset_path: str, graph_name: str = "EventGraph") -> str:
    """
    Get a Blueprint graph as JSON (nodes + connections).

    Returns all nodes with their pins, connections, positions, and AI names.

    Args:
        asset_path: Asset path of the Blueprint
        graph_name: Graph name (default: "EventGraph")

    Returns:
        JSON with nodes array, each containing pins and connections.
    """
    return _format_response(_send_command("get_graph", {
        "asset_path": asset_path,
        "graph_name": graph_name,
    }))


@mcp.tool()
def list_graphs(asset_path: str) -> str:
    """
    List all graphs in a Blueprint.

    Args:
        asset_path: Asset path of the Blueprint

    Returns:
        JSON with graphs array of names.
    """
    return _format_response(_send_command("list_graphs", {
        "asset_path": asset_path,
    }))


# =============================================================================
# BLUEPRINT VARIABLES
# =============================================================================

@mcp.tool()
def add_variable(
    asset_path: str,
    var_name: str,
    var_type: str,
    instance_editable: bool = False,
    category: str = ""
) -> str:
    """
    Add a member variable to a Blueprint.

    Args:
        asset_path: Asset path of the Blueprint
        var_name: Variable name
        var_type: Type string. Supported types:
                 - Primitives: "bool", "int", "float", "double", "byte"
                 - Strings: "String", "Name", "Text"
                 - Structs: "Vector", "Rotator", "Transform", "LinearColor"
                 - Objects: "/Script/UMG.TextBlock" (class path for object refs)
        instance_editable: Expose to Details panel in editor (default: False)
        category: Optional variable category name

    Returns:
        JSON with var_name and var_type.
    """
    params = {
        "asset_path": asset_path,
        "var_name": var_name,
        "var_type": var_type,
        "instance_editable": instance_editable,
    }
    if category:
        params["category"] = category
    return _format_response(_send_command("add_variable", params))


@mcp.tool()
def set_variable_default(
    asset_path: str,
    var_name: str,
    default_value: str
) -> str:
    """
    Set a Blueprint variable's default value.

    Args:
        asset_path: Asset path of the Blueprint
        var_name: Variable name
        default_value: Default value as string

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("set_variable_default", {
        "asset_path": asset_path,
        "var_name": var_name,
        "default_value": default_value,
    }))


@mcp.tool()
def remove_variable(asset_path: str, var_name: str) -> str:
    """
    Remove a variable from a Blueprint.

    Args:
        asset_path: Asset path of the Blueprint
        var_name: Variable name to remove

    Returns:
        JSON with removal confirmation.
    """
    return _format_response(_send_command("remove_variable", {
        "asset_path": asset_path,
        "var_name": var_name,
    }))


@mcp.tool()
def get_variables(asset_path: str) -> str:
    """
    Get all Blueprint variables as JSON.

    Args:
        asset_path: Asset path of the Blueprint

    Returns:
        JSON with variables array containing name, type, default_value, category.
    """
    return _format_response(_send_command("get_variables", {
        "asset_path": asset_path,
    }))


# =============================================================================
# GENERIC ASSET FACTORY
# =============================================================================

@mcp.tool()
def create_asset(
    package_path: str,
    asset_name: str,
    asset_type: str,
    properties: dict | None = None
) -> str:
    """
    Create a new asset of the given type.

    Args:
        package_path: Content path, e.g. "/Game/Input"
        asset_name: Asset name, e.g. "IA_Jump"
        asset_type: One of: InputAction, InputMappingContext,
                   SoundClass, SoundSubmix, SoundConcurrency, SoundAttenuation,
                   SoundControlBus, SoundControlBusMix, SoundModulationPatch,
                   PhysicalMaterial
        properties: Optional dict of initial property values (ImportText format)

    Returns:
        JSON with asset_path, asset_name, asset_type, class.
    """
    params = {
        "package_path": package_path,
        "asset_name": asset_name,
        "asset_type": asset_type,
    }
    if properties:
        params["properties"] = properties
    return _format_response(_send_command("create_asset", params))


@mcp.tool()
def set_asset_property(
    asset_path: str,
    property_path: str,
    value: str
) -> str:
    """
    Set a property on any loaded asset using reflection.

    Args:
        asset_path: Asset path, e.g. "/Game/Input/IA_Jump"
        property_path: Property path with dot/bracket notation,
                      e.g. "ValueType", "Triggers[0].ActuationThreshold"
        value: Value in UE ImportText format

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("set_asset_property", {
        "asset_path": asset_path,
        "property_path": property_path,
        "value": value,
    }))


@mcp.tool()
def get_asset_properties(asset_path: str) -> str:
    """
    Get all properties of any loaded asset as JSON.

    Args:
        asset_path: Asset path, e.g. "/Game/Input/IA_Jump"

    Returns:
        JSON with asset_path, class, and properties object.
    """
    return _format_response(_send_command("get_asset_properties", {
        "asset_path": asset_path,
    }))


@mcp.tool()
def save_asset(asset_path: str) -> str:
    """
    Save any loaded asset to disk.

    Args:
        asset_path: Asset path, e.g. "/Game/Input/IA_Jump"

    Returns:
        JSON with saved status.
    """
    return _format_response(_send_command("save_asset", {
        "asset_path": asset_path,
    }))


# =============================================================================
# INPUT MAPPING CONTEXT
# =============================================================================

@mcp.tool()
def add_input_mapping(
    asset_path: str,
    input_action_path: str,
    key: str,
    triggers: list[str] | None = None,
    modifiers: list[str] | None = None
) -> str:
    """
    Add a key mapping to an InputMappingContext.

    Args:
        asset_path: Path to InputMappingContext, e.g. "/Game/Input/IMC_Default"
        input_action_path: Path to InputAction, e.g. "/Game/Input/IA_Jump"
        key: FKey name, e.g. "SpaceBar", "Gamepad_FaceButton_Bottom",
             "LeftMouseButton", "W", "Gamepad_LeftStick_Up"
        triggers: Optional trigger class short names, e.g. ["Pressed", "Hold"]
                 Available: Pressed, Released, Down, Hold, HoldAndRelease,
                           Tap, Pulse, ChordAction, ChordBlocker, Combo
        modifiers: Optional modifier class short names, e.g. ["Negate", "SwizzleAxis"]
                  Available: Negate, DeadZone, Scalar, ScaleByDeltaTime,
                            FOVScaling, ResponseCurve, Smooth, SwizzleAxis, ToWorldSpace

    Returns:
        JSON with success status, action and key.
    """
    params = {
        "asset_path": asset_path,
        "input_action_path": input_action_path,
        "key": key,
    }
    if triggers:
        params["triggers"] = triggers
    if modifiers:
        params["modifiers"] = modifiers
    return _format_response(_send_command("add_input_mapping", params))


@mcp.tool()
def remove_input_mapping(
    asset_path: str,
    mapping_index: int
) -> str:
    """
    Remove a key mapping from an InputMappingContext by index.

    Args:
        asset_path: Path to InputMappingContext
        mapping_index: Index of the mapping to remove (0-based)

    Returns:
        JSON with removal confirmation.
    """
    return _format_response(_send_command("remove_input_mapping", {
        "asset_path": asset_path,
        "mapping_index": mapping_index,
    }))


@mcp.tool()
def get_input_mappings(asset_path: str) -> str:
    """
    Get all key mappings from an InputMappingContext as JSON.

    Args:
        asset_path: Path to InputMappingContext

    Returns:
        JSON with mappings array containing index, action, key, triggers, modifiers.
    """
    return _format_response(_send_command("get_input_mappings", {
        "asset_path": asset_path,
    }))


# =============================================================================
# ANIM BLUEPRINT BUILDER
# =============================================================================

@mcp.tool()
def create_anim_blueprint(
    package_path: str,
    asset_name: str,
    skeleton_path: str,
    parent_class: str = "AnimInstance"
) -> str:
    """
    Create a new AnimBlueprint asset.

    Args:
        package_path: Content path, e.g. "/Game/Characters/Animations"
        asset_name: Asset name, e.g. "ABP_Character"
        skeleton_path: Path to USkeleton, e.g. "/Game/Characters/SK_Mannequin"
        parent_class: Optional parent class. Defaults to "AnimInstance".
                     Can also be a full class path.

    Returns:
        JSON with asset_path, asset_name, skeleton, parent_class.
    """
    return _format_response(_send_command("create_anim_blueprint", {
        "package_path": package_path,
        "asset_name": asset_name,
        "skeleton_path": skeleton_path,
        "parent_class": parent_class,
    }))


@mcp.tool()
def get_anim_blueprint_info(asset_path: str) -> str:
    """
    Get AnimBlueprint info as JSON (skeleton, parent class, graphs, variables).

    Args:
        asset_path: Path to AnimBlueprint, e.g. "/Game/Characters/ABP_Character"

    Returns:
        JSON with asset info including skeleton, parent_class, status, graphs, variables.
    """
    return _format_response(_send_command("get_anim_blueprint_info", {
        "asset_path": asset_path,
    }))


if __name__ == "__main__":
    mcp.run()
