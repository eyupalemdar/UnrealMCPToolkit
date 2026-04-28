# CommonAIExport — Claude Reference Guide

> **Read this file to understand ALL plugin capabilities in one place.**
> 57 MCP tools across 11 categories. UE 5.7, TCP port auto-discovery.

## Architecture

```
Claude Code ──MCP stdio──> ai_widget_mcp_client.py ──TCP──> UE Editor (C++ TCP Server)
                                                              │
                                              ┌───────────────┼───────────────┐
                                              │               │               │
                                        Widget Builder  Material Builder  Asset Export
                                        BP Graph Builder  CDO Properties  Asset Import
```

- **MCP Client**: `Plugins/CommonAIExport/MCPClient/ai_widget_mcp_client.py`
- **TCP Server**: Runs inside UE Editor, port 55560-55600 (auto-discovery via `Intermediate/AIExport_port.txt`)
- **All tools use `mcp__widget-builder__` prefix** when called from Claude Code
- **Property values use UE ImportText format** (same format the export system produces)

---

## 1. Widget Blueprint Lifecycle

| Tool | Purpose |
|------|---------|
| `create_widget_blueprint(package_path, asset_name, parent_class?)` | Create new WBP. parent_class e.g. `/Script/CommonUI.CommonUserWidget` |
| `compile_and_save(asset_path)` | Compile + save to disk. **Always call after modifications.** |
| `reparent_blueprint(asset_path, new_parent_class)` | Change parent class. e.g. `/Script/LyraGame.LyraActivatableWidget` |
| `get_widget_tree(asset_path)` | Get full widget hierarchy as JSON |
| `list_widget_classes()` | List all available non-abstract widget classes |
| `export_widget(asset_path, both_formats?)` | Export to _raw.txt + _simplified.txt + _stripped.txt |
| `export_blueprint(asset_path, both_formats?)` | Export any BP type (Widget, Blueprint, DataAsset, etc.) |
| `list_supported_types()` | List exportable asset types |

### Example: Create Widget
```
create_widget_blueprint("/Game/UI/Kale/Screens", "W_KalePlayContent", "/Script/CommonUI.CommonUserWidget")
add_widget("/Game/UI/Kale/Screens/W_KalePlayContent", "VerticalBox", "VBox_Root", "")
compile_and_save("/Game/UI/Kale/Screens/W_KalePlayContent")
```

---

## 2. Widget Tree Manipulation

| Tool | Purpose |
|------|---------|
| `add_widget(asset_path, widget_class, widget_name, parent_name?)` | Add widget. parent="" = root |
| `remove_widget(asset_path, widget_name)` | Remove widget |
| `move_widget(asset_path, widget_name, new_parent, index?)` | Move widget. index=-1 = append |

### Widget Classes (common)
`TextBlock`, `Image`, `Button`, `Border`, `CanvasPanel`, `VerticalBox`, `HorizontalBox`, `Overlay`, `SizeBox`, `Spacer`, `WidgetSwitcher`, `ScrollBox`, `CommonTextBlock`, `CommonButtonBase`, `CommonActionWidget`, `CommonActivatableWidget`, `CommonAnimatedSwitcher`

### Custom Widget Blueprint Children
`list_widget_classes()` is a convenience list, not an authority for newly
created Widget Blueprints. `add_widget` can resolve reusable WBP children by
explicit asset/generated-class path:

```
add_widget("/Game/UI/Path/W_Parent", "/Game/UI/Path/W_Component", "GeneralTab", "Switcher")
add_widget("/Game/UI/Path/W_Parent", "/Game/UI/Path/W_Component_C", "GeneralTab", "Switcher")
add_widget("/Game/UI/Path/W_Parent", "/Game/UI/Path/W_Component.W_Component_C", "GeneralTab", "Switcher")
add_widget("/Game/UI/Path/W_Parent", "WidgetBlueprintGeneratedClass'/Game/UI/Path/W_Component.W_Component_C'", "GeneralTab", "Switcher")
```

Do not assign class or asset paths into `WidgetSwitcher` or `NamedSlot` through
slot `Content`; add an actual child widget instance instead.

---

## 3. Widget Properties

| Tool | Purpose |
|------|---------|
| `set_widget_property(asset_path, widget_name, property_name, value)` | Set single property |
| `set_widget_properties(asset_path, widget_name, properties_json)` | Set multiple properties at once |
| `set_slot_property(asset_path, widget_name, property_name, value)` | Set slot property (Padding, Size, Alignment, etc.) |
| `set_canvas_slot_layout(asset_path, widget_name, pos_x, pos_y, size_x, size_y, ...)` | Canvas slot shortcut |

### Property Format (ImportText)
```
# Simple values
"True", "False", "13.000000", "SelfHitTestInvisible", "Center"

# Struct (dot-notation)
property_name="Font.Size"  value="13.000000"
property_name="Font.TypefaceFontName"  value="Medium"

# Color
property_name="ColorAndOpacity"  value="(R=0.961,G=0.651,B=0.137,A=1.0)"

# SlateColor (for SetColorAndOpacity BP node pins)
"(SpecifiedColor=(R=1.0,G=1.0,B=1.0,A=1.0),ColorUseRule=UseColor_Specified)"

# Brush
property_name="Brush.DrawAs"  value="RoundedBox"
property_name="Brush.OutlineSettings.CornerRadii"  value="(X=3.0,Y=3.0,Z=0.0,W=0.0)"

# Font reference
property_name="Font.FontObject"  value="/Script/Engine.Font'/Game/UI/Fonts/Inter/Inter.Inter'"

# Class reference (TSubclassOf)
"WidgetBlueprintGeneratedClass'/Game/UI/Path/W_Widget.W_Widget_C'"

# Asset reference
"/Script/Engine.Texture2D'/Game/UI/Textures/T_Logo.T_Logo'"
```

### Slot Properties
```
# Padding
set_slot_property: property_name="Padding"  value="(Left=20.0,Top=12.0,Right=20.0,Bottom=12.0)"

# Size (Fill/Auto)
set_slot_property: property_name="Size.SizeRule"  value="Fill"
set_slot_property: property_name="Size.Value"  value="1.0"

# Alignment
set_slot_property: property_name="HorizontalAlignment"  value="HAlign_Center"
set_slot_property: property_name="VerticalAlignment"  value="VAlign_Bottom"
```

---

## 4. CDO (Class Default Object) Properties

| Tool | Purpose |
|------|---------|
| `set_cdo_property(asset_path, property_name, value)` | Set CDO property (bSelectable, MinWidth, Style, etc.) |
| `get_cdo_properties(asset_path)` | Get all CDO properties as JSON |

### Example: Button CDO
```
set_cdo_property("/Game/UI/W_MyButton", "bSelectable", "true")
set_cdo_property("/Game/UI/W_MyButton", "MinWidth", "160")
set_cdo_property("/Game/UI/W_MyButton", "Style", "/Script/Engine.BlueprintGeneratedClass'/Game/UI/Foundation/Buttons/ButtonStyle-Clear.ButtonStyle-Clear_C'")
```

---

## 5. CDO Array Properties

| Tool | Purpose |
|------|---------|
| `add_cdo_array_element(asset_path, array_name, element_values?)` | Add element. element_values = JSON object of sub-properties |
| `set_cdo_array_element_property(asset_path, array_name, index, property_name, value)` | Set sub-property on element |
| `remove_cdo_array_element(asset_path, array_name, index)` | Remove by index |
| `get_cdo_array_length(asset_path, array_name)` | Get array length |

### Example: Tab Registration Array
```python
# Add a tab entry
add_cdo_array_element(
    "/Game/UI/Kale/Components/W_KaleTabList",
    "PreregisteredTabInfoArray",
    '{"TabId": "Play", "TabText": "NSLOCTEXT(\\"Kale\\", \\"Tab_Play\\", \\"PLAY\\")"}'
)

# Set content type on existing element
set_cdo_array_element_property(
    "/Game/UI/Kale/Components/W_KaleTabList",
    "PreregisteredTabInfoArray",
    0,  # index
    "TabContentType",
    "WidgetBlueprintGeneratedClass'/Game/UI/Kale/Screens/W_KalePlayContent.W_KalePlayContent_C'"
)
```

### Gotcha: TSubclassOf Constraints
`TabContentType` is `TSubclassOf<UCommonUserWidget>` — the target widget MUST inherit from `CommonUserWidget`, not plain `UserWidget`. Reparent first if needed.

---

## 6. Blueprint Graph Manipulation

| Tool | Purpose |
|------|---------|
| `add_event_node(asset_path, event_name, node_name, pos_x?, pos_y?, graph_name?)` | Add event override (Construct, Tick, BP_OnSelected, etc.) |
| `add_custom_event(asset_path, event_name, node_name, pos_x?, pos_y?, graph_name?)` | Add custom event |
| `ensure_function_graph(asset_path, function_name, inputs?, outputs?, entry_node_name?, result_node_name?)` | Create/update a function graph and tag entry/result nodes |
| `add_function_call(asset_path, function_name, node_name, target_class?, pos_x?, pos_y?, graph_name?)` | Add function call node |
| `add_variable_get_node(asset_path, var_name, node_name, pos_x?, pos_y?, graph_name?)` | Add Get node for variable |
| `add_variable_set_node(asset_path, var_name, node_name, pos_x?, pos_y?, graph_name?)` | Add Set node for variable |
| `add_make_struct_node(asset_path, struct_type, node_name, pos_x?, pos_y?, graph_name?)` | Add Make Struct node |
| `add_branch_node(asset_path, node_name, pos_x?, pos_y?, graph_name?)` | Add Branch (if) node |
| `connect_pins(asset_path, source_node, source_pin, target_node, target_pin, graph_name?)` | Connect two pins |
| `set_pin_default(asset_path, node_name, pin_name, value, graph_name?)` | Set pin default value |
| `remove_graph_node(asset_path, node_name, graph_name?)` | Remove a graph node |
| `get_graph(asset_path, graph_name?)` | Get graph as JSON (default: "EventGraph") |
| `list_graphs(asset_path)` | List all graphs |

### Named Function Graphs
```python
ensure_function_graph(
    path,
    "UpdateTextStyle",
    entry_node_name="UpdateTextStyle_Entry"
)
add_variable_get_node(path, "Text", "GetText", graph_name="UpdateTextStyle")
add_function_call(path, "SetStyle", "SetTextStyle",
    target_class="/Script/CommonUI.CommonTextBlock",
    graph_name="UpdateTextStyle")
connect_pins(path, "UpdateTextStyle_Entry", "then", "SetTextStyle", "execute",
    graph_name="UpdateTextStyle")
```

Function pin specs use the same `type` strings as `add_variable`:
`{"name": "InputType", "type": "enum:/Script/CommonInput.ECommonInputType",
"default_value": "MouseAndKeyboard"}`.

### Example: Selected/Deselected Events
```python
# Add event
add_event_node(path, "BP_OnSelected", "K2Node_Event_Selected", 0, 0)

# Add function call
add_function_call(path, "SetColorAndOpacity", "SetColor_Selected", "UMG.TextBlock", 300, 0)

# Add variable get
add_variable_get_node(path, "ButtonTextBlock", "GetTextBlock", 100, 50)

# Connect exec pin
connect_pins(path, "K2Node_Event_Selected", "then", "SetColor_Selected", "execute")

# Connect object pin
connect_pins(path, "GetTextBlock", "ButtonTextBlock", "SetColor_Selected", "self")

# Set pin default (SlateColor)
set_pin_default(path, "SetColor_Selected", "InColorAndOpacity",
    "(SpecifiedColor=(R=1.0,G=1.0,B=1.0,A=1.0),ColorUseRule=UseColor_Specified)")
```

### Pin Name Conventions
- Exec: `then` (out), `execute` (in)
- Return: `ReturnValue`
- Self: `self`
- Function params: exact parameter name (e.g., `InColorAndOpacity`, `InVisibility`, `InText`, `Content`)
- Variable Get: outputs the variable name (e.g., `ButtonTextBlock`)
- Struct Make: field names as pins

### Important: target_class for Functions
Some functions exist on specific classes and require `target_class`:
- `SetColorAndOpacity` on TextBlock: `target_class="/Script/UMG.TextBlock"`
- `SetVisibility` on Widget: `target_class="/Script/UMG.Widget"`
- `SetText` on TextBlock: `target_class="/Script/UMG.TextBlock"`
- `AddChildToHorizontalBox`: `target_class="/Script/UMG.HorizontalBox"`

Without `target_class`, the function may resolve to wrong overload or fail.

---

## 7. Blueprint Variables

| Tool | Purpose |
|------|---------|
| `add_variable(asset_path, var_name, var_type, instance_editable?, blueprint_read_only?, category?)` | Add member variable |
| `set_variable_default(asset_path, var_name, value)` | Set default value |
| `remove_variable(asset_path, var_name)` | Remove variable |
| `get_variables(asset_path)` | Get all variables as JSON |

### Variable Types
```
"bool", "int", "float", "string", "text", "name",
"vector", "rotator", "transform", "color", "linearcolor",
"/Script/UMG.Widget", "/Script/CommonUI.CommonTextBlock",  # Object types
"class:/Script/CommonUI.CommonButtonStyle",                # TSubclassOf/class refs
"enum:/Script/SlateCore.EHorizontalAlignment",             # Enum byte vars
"enum:/Script/CommonInput.ECommonInputType"
```

---

## 8. Material System

### Material Creation & Editing

| Tool | Purpose |
|------|---------|
| `create_material(package_path, asset_name, domain?, blend_mode?, shading_model?)` | Create material |
| `set_material_property(asset_path, property_name, value)` | Set material property (domain, blend, shading) |
| `add_expression(asset_path, expression_class, node_name, pos_x?, pos_y?)` | Add expression node |
| `set_expression_property(asset_path, node_name, property_name, value)` | Set expression property |
| `connect_expressions(asset_path, source_node, source_index, target_node, target_index)` | Wire nodes |
| `connect_to_material_property(asset_path, source_node, source_index, material_property)` | Wire to root |
| `disconnect_input(asset_path, node_name, input_index)` | Disconnect input |
| `remove_expression(asset_path, node_name)` | Remove expression |
| `compile_material(asset_path)` | Compile + save |
| `get_material_graph(asset_path)` | Get graph as JSON |
| `list_expression_classes()` | List available expression types |

### Material Instance

| Tool | Purpose |
|------|---------|
| `create_material_instance(package_path, asset_name, parent_material)` | Create MIC |
| `set_instance_parameter(asset_path, param_name, param_type, value)` | Set scalar/vector/texture param |
| `save_material_instance(asset_path)` | Save to disk |
| `get_material_instance_info(asset_path)` | Get MIC info |

### Material Property Values
```
# Domain
"Surface", "DeferredDecal", "LightFunction", "PostProcess", "UserInterface"

# Blend Mode
"Opaque", "Masked", "Translucent", "Additive", "Modulate", "AlphaComposite"

# Shading Model
"DefaultLit", "Unlit", "SubsurfaceProfile"

# Material Root Properties (connect_to_material_property)
"EmissiveColor", "Opacity", "OpacityMask", "BaseColor", "Metallic", "Roughness", "Normal"

# Instance Parameters
param_type="scalar"  value="0.5"
param_type="vector"  value="(R=1.0,G=0.5,B=0.0,A=1.0)"
param_type="texture" value="/Game/UI/Textures/T_Logo.T_Logo"
```

### Example: Material Instance for Tab Button
```python
create_material_instance("/Game/UI/Kale/Materials", "MI_KaleTabButton_BG",
    "/Game/UI/Foundation/Materials/MI_UI_MenuButton_Base")
set_instance_parameter("/Game/UI/Kale/Materials/MI_KaleTabButton_BG",
    "Base_RGBA", "vector", "(R=0.0,G=0.0,B=0.0,A=0.0)")
save_material_instance("/Game/UI/Kale/Materials/MI_KaleTabButton_BG")
```

---

## 9. Asset Import

| Tool | Purpose |
|------|---------|
| `import_texture(source_path, destination_path, asset_name, compression?)` | Import texture from disk |
| `import_font(font_paths_json, destination_path, asset_name, default_size?)` | Import TTF/OTF → Composite Font |

---

## 10. Widget Preview Capture (IFTP verify loop)

Renders a Widget Blueprint to a PNG file (or files, one per ratio) so Claude can
visually compare UE output with the Pencil source across multiple screen ratios.

| Tool | Purpose |
|------|---------|
| `capture_widget_preview(asset_path, width?, height?, output_path?, warmup_frames?, transparent_bg?, return_base64?, dpi_scale?, preview_mode?, preview_function_calls?, ratios?)` | Render WBP to PNG at one or more resolutions |

### Single-shot example
```python
capture_widget_preview(
    asset_path="/Game/UI/Menu/W_Splash_IFTP_Test",
    width=1920, height=1080,
    preview_mode="runtime"
)
# → {"pngs": [{"png_path": "<Project>/Intermediate/WidgetCaptures/W_Splash_IFTP_Test_1920x1080.png", "width": 1920, "height": 1080, "size_bytes": 42817}], "count": 1}
```

### Multi-ratio example (IFTP runbook adım 14)
```python
capture_widget_preview(
    asset_path="/Game/UI/Menu/W_Splash_IFTP_Test",
    ratios=[
        {"width": 1920, "height": 1080, "label": "16x9"},
        {"width": 2560, "height": 1080, "label": "21x9"},
        {"width": 1080, "height": 1920, "label": "9x16"},
        {"width": 1440, "height": 1080, "label": "4x3"},
    ]
)
# → 4 PNG files written, one per ratio. Each includes label suffix in filename.
```

### Claude workflow
1. `capture_widget_preview(...)` returns JSON with `png_path` entries.
2. Use the `Read` tool on each `png_path` — Claude reads PNG as an image (multimodal).
3. Compare visually with the Pencil reference (from `mcp__pencil__get_screenshot`).
4. Write findings to `.claude/docs/ui-fidelity-log.md`.

### Technical details
- Uses `FWidgetRenderer` + `UTextureRenderTarget2D` → `IImageWrapper` (PNG).
- Game-thread safe: asset load + widget instantiation run via `AsyncTask(GameThread)`.
- `preview_mode="runtime"` is the default and is required for UI acceptance. It does not set `EWidgetDesignFlags::Designing`; CommonActivatable root widgets are activated before rendering.
- `preview_mode="designer"` preserves design-time `PreConstruct` preview data for probes, but it is not a reliable runtime acceptance capture.
- `preview_function_calls` applies optional reflection calls to the live widget instance after runtime activation and before rendering. Use this for stateful runtime captures where activation sets a default state, e.g. `preview_function_calls=[{"function_name":"SwitchToTab","args":{"Tab":"2"}}]` to capture a non-default settings tab without falling back to designer mode.
- The response JSON includes `preview_mode` at the top level and on each PNG entry.
- Warmup frames (default 3) absorb texture streaming delay.
- Widget instance is rooted during render, cleaned up afterwards (no GC leak).
- Default output directory: `<ProjectDir>/Intermediate/WidgetCaptures/`
- Label characters `/\\:` are sanitized to `_` / empty in the filename.
- Timeout: 120 seconds total (game thread work).

### Gotchas
- Widget must have a **valid GeneratedClass** (i.e. compile must have succeeded at least once). Use `compile_and_save` first if the WBP is freshly created.
- Retainer boxes and 3D widgets may not render correctly in the first warmup frame — increase `warmup_frames` to 5+ if you see missing content.
- ScaleBox's `ScaleToFit` behavior requires a valid child hierarchy — empty widgets render as black (Mode 1 letterbox without content).
- `transparent_bg=true` requires the WBP to have no fullscreen background color; otherwise the background is baked into the PNG regardless.

---

## 11. Asset Lifecycle

| Tool | Purpose |
|------|---------|
| `reload_asset(asset_path, reopen_after?)` | Close asset editor, hard reload package, reopen editor |

### Why this exists

After `compile_and_save`, the on-disk asset is updated but any open editor tab keeps a **cached widget instance** showing the old version. The user sees stale output even though `capture_widget_preview` (which reads from disk) shows the fix. Manually the user must do **Asset Actions → Reload** in Content Browser. This tool automates that step.

### Example

```python
# After compile_and_save:
compile_and_save("/Game/UI/Menu/W_MainMenu_HF03d")

# Clear any stale editor tab so the user sees the updated widget:
reload_asset("/Game/UI/Menu/W_MainMenu_HF03d")
# → {"was_open": true, "reloaded": true, "reopened": true}
```

### Technical details

- Uses `UAssetEditorSubsystem::CloseAllEditorsForAsset` to close any open tab.
- Uses `UPackageTools::ReloadPackages` with `AssumePositive` interaction mode (no dialog prompts).
- Reopens the editor tab only if `reopen_after=true` and the tab was open before.
- `capture_widget_preview` does NOT need this — it renders directly from the on-disk asset.

### Response fields

| Field | Meaning |
|---|---|
| `was_open` | Was an editor tab open for this asset before reload? |
| `reloaded` | Did the hard package reload succeed? |
| `reopened` | Was the editor tab reopened after reload? |

### Example: Import Font
```python
import_font(
    '["/path/to/Inter-Regular.ttf", "/path/to/Inter-Bold.ttf", "/path/to/Inter-Medium.ttf"]',
    "/Game/UI/Kale/Fonts/Inter",
    "Inter",
    13
)
```

---

## Critical Gotchas

### 1. Always compile_and_save after changes
Widget tree, CDO, graph, variable changes are in-memory until compiled.

### 2. SlateColor vs LinearColor
- Widget property `ColorAndOpacity`: `(R=1.0,G=1.0,B=1.0,A=1.0)` — LinearColor works
- BP pin default for `SetColorAndOpacity`: Must use full SlateColor struct:
  `(SpecifiedColor=(R=1.0,G=1.0,B=1.0,A=1.0),ColorUseRule=UseColor_Specified)`

### 3. Event Override vs Custom Event
`add_event_node` = override existing event (BP_OnSelected, Construct, HandleTabCreation)
`add_custom_event` = new custom event
Using `add_custom_event` for an existing event name causes compile conflict.

### 4. Export Truncation
CDO properties with very long values (e.g., PreregisteredTabInfoArray) get truncated in export files. Use `get_cdo_array_length` + `set_cdo_array_element_property` to inspect/modify array elements directly.

### 5. TSubclassOf Constraints
When setting class references via `set_cdo_array_element_property`, the target class must satisfy the TSubclassOf constraint. E.g., `TabContentType` requires `UCommonUserWidget` subclass — a plain `UUserWidget` subclass will fail silently.

### 6. Widget Name Uniqueness
UE may rename widgets if the name already exists. Check the return value of `add_widget` for the actual assigned name.

### 7. Root Widget
First `add_widget` with empty `parent_name` becomes the root. Subsequent widgets need a parent.

### 8. Slot Properties Depend on Parent
`set_slot_property` only works if the widget has a slot (i.e., has a parent panel). Setting slot on root widget fails.

---

## Tool Count by Category

| Category | Count | Tools |
|----------|-------|-------|
| Widget Lifecycle | 8 | create, compile, reparent, get_tree, list_classes, export_widget, export_blueprint, list_types |
| Widget Tree | 3 | add, remove, move |
| Widget Properties | 4 | set_property, set_properties, set_slot, set_canvas_layout |
| CDO Properties | 2 | set_cdo_property, get_cdo_properties |
| CDO Arrays | 4 | add_element, set_element_property, remove_element, get_length |
| Blueprint Graph | 14 | add_event, add_custom_event, ensure_function_graph, add_function_call, add_var_get, add_var_set, add_make_struct, add_branch, add_call_parent_function, connect_pins, set_pin_default, remove_node, get_graph, list_graphs |
| Blueprint Variables | 4 | add, set_default, remove, get_variables |
| Material System | 14 | create, set_property, add_expr, set_expr_property, connect_exprs, connect_to_root, disconnect, remove_expr, compile, get_graph, list_classes, create_instance, set_param, save_instance, get_info |
| Asset Import | 2 | import_texture, import_font |
| Widget Preview Capture | 1 | capture_widget_preview (IFTP verify loop) |
| Asset Lifecycle | 1 | reload_asset (clear cached editor tab after compile) |
| Utility | 1 | ping |
| **Total** | **57** | |

---

## Export System (Read-Only)

Exports UE assets to 3 formats for AI analysis:

| Format | Suffix | Use |
|--------|--------|-----|
| Simplified | `_simplified.txt` | **Read this first.** AI-friendly, clean structure |
| Stripped | `_stripped.txt` | Non-default values in UE format. For pin types, exact values |
| Raw | `_raw.txt` | ALL values including defaults. Debugging only |

### Supported Export Types
Widget Blueprint, Blueprint, AnimBlueprint, DataAsset, InputAction, InputMappingContext, Material, MaterialInstance, PhysicalMaterial, Texture, Audio (SoundClass, SoundSubmix, etc.), World/Map

### Output Path
Exports go to `Dev/AIExports/` mirroring the Content folder structure:
`/Game/UI/Kale/W_Widget` → `Dev/AIExports/Game/UI/Kale/W_Widget_simplified.txt`

---

## Quick Workflow Reference

### Create Widget Blueprint from scratch
1. `create_widget_blueprint` → 2. `add_widget` (root) → 3. `add_widget` (children) → 4. `set_widget_property` / `set_widget_properties` → 5. `set_slot_property` → 6. `set_cdo_property` → 7. BP graph: `add_event_node` + `add_function_call` + `connect_pins` + `set_pin_default` → 8. `compile_and_save`

### Create Material Instance
1. `create_material_instance` → 2. `set_instance_parameter` (repeat) → 3. `save_material_instance`

### Modify existing Widget Blueprint
1. `get_widget_tree` (understand structure) → 2. `export_widget` + read simplified (understand CDO + graph) → 3. make changes → 4. `compile_and_save`

### Fix CDO array entry
1. `get_cdo_array_length` → 2. `set_cdo_array_element_property` → 3. `compile_and_save`

---

*Version: 5.0.0 — Last Updated: 2026-03-05*
*55 MCP tools, covering Widget Builder, Material Builder, BP Graph, CDO, Array, Import, Export*
