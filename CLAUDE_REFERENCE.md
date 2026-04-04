# CommonAIExport — Claude Reference Guide

> **Read this file to understand ALL plugin capabilities in one place.**
> 59 MCP tools across 10 categories. UE 5.7, TCP port auto-discovery.

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
| `create_blueprint(package_path, asset_name, parent_class)` | Create any BP type (ButtonStyle, TextStyle, DataAsset, etc.) |
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

### Example: Create ButtonStyle DataAsset
```
create_blueprint("/Game/UI/Foundation/Buttons", "ButtonStyle-Kale-Primary", "/Script/CommonUI.CommonButtonStyle")
set_cdo_property("/Game/UI/Foundation/Buttons/ButtonStyle-Kale-Primary", "NormalBase",
    "(TintColor=(SpecifiedColor=(R=1.0,G=1.0,B=1.0,A=1.0)),DrawAs=Box,ImageSize=(X=16.0,Y=16.0),Margin=(Left=0.5,Top=0.5,Right=0.5,Bottom=0.5),ResourceObject=\"/Script/Engine.MaterialInstanceConstant'/Game/UI/Menu/Art/MI_UI_MenuButton_Base.MI_UI_MenuButton_Base'\")")
compile_and_save("/Game/UI/Foundation/Buttons/ButtonStyle-Kale-Primary")
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
| `add_event_node(asset_path, event_name, node_name, pos_x?, pos_y?)` | Add event override (Construct, Tick, BP_OnSelected, etc.) |
| `add_custom_event(asset_path, event_name, node_name, pos_x?, pos_y?)` | Add custom event |
| `add_function_call(asset_path, function_name, node_name, target_class?, pos_x?, pos_y?)` | Add function call node |
| `add_variable_get_node(asset_path, var_name, node_name, pos_x?, pos_y?)` | Add Get node for variable |
| `add_variable_set_node(asset_path, var_name, node_name, pos_x?, pos_y?)` | Add Set node for variable |
| `add_make_struct_node(asset_path, struct_type, node_name, pos_x?, pos_y?)` | Add Make Struct node |
| `add_branch_node(asset_path, node_name, pos_x?, pos_y?)` | Add Branch (if) node |
| `add_call_parent_function(asset_path, function_name, node_name, pos_x?, pos_y?)` | Add Super::Function call node |
| `connect_pins(asset_path, source_node, source_pin, target_node, target_pin)` | Connect two pins |
| `set_pin_default(asset_path, node_name, pin_name, value)` | Set pin default value |
| `remove_graph_node(asset_path, node_name)` | Remove a graph node |
| `get_graph(asset_path, graph_name?)` | Get graph as JSON (default: "EventGraph") |
| `list_graphs(asset_path)` | List all graphs |

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
| `add_variable(asset_path, var_name, var_type, is_exposed?, category?)` | Add member variable |
| `set_variable_default(asset_path, var_name, value)` | Set default value |
| `remove_variable(asset_path, var_name)` | Remove variable |
| `get_variables(asset_path)` | Get all variables as JSON |

### Variable Types
```
"bool", "int", "float", "string", "text", "name",
"vector", "rotator", "transform", "color", "linearcolor",
"/Script/UMG.Widget", "/Script/CommonUI.CommonTextBlock",  # Object types
"class'/Script/UMG.Widget'"  # Class reference
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
| Blueprint Graph | 12 | add_event, add_custom_event, add_function_call, add_var_get, add_var_set, add_make_struct, add_branch, connect_pins, set_pin_default, remove_node, get_graph, list_graphs |
| Blueprint Variables | 4 | add, set_default, remove, get_variables |
| Material System | 14 | create, set_property, add_expr, set_expr_property, connect_exprs, connect_to_root, disconnect, remove_expr, compile, get_graph, list_classes, create_instance, set_param, save_instance, get_info |
| Asset Import | 2 | import_texture, import_font |
| Utility | 1 | ping |
| **Total** | **55** | |

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

### Create Widget Animation
1. `create_widget_animation` → 2. `bind_animation_widget` (for each widget) → 3. `add_animation_track` (for each property) → 4. `add_animation_keyframe` (for each keyframe) → 5. `compile_and_save`

---

## 10. Widget Animations

| Tool | Purpose |
|------|---------|
| `create_widget_animation(asset_path, animation_name, length_seconds?)` | Create UWidgetAnimation on a WBP. Default 1.0s. |
| `bind_animation_widget(asset_path, animation_name, widget_name)` | Bind a widget to an animation (creates possessable + binding). |
| `add_animation_track(asset_path, animation_name, widget_name, property_type, property_path)` | Add a property track. Types: `float`, `color`, `transform2d`. |
| `add_animation_keyframe(asset_path, animation_name, widget_name, property_path, time, value, interpolation?)` | Add keyframe. Interpolation: `Linear`, `Cubic` (default), `Constant`. |

### Track Types

| Type | UE Track | Value Format | Common Properties |
|------|----------|-------------|-------------------|
| `float` | `UMovieSceneFloatTrack` | `"0.5"` | `RenderOpacity` |
| `color` | `UMovieSceneColorTrack` | `"(R=1.0,G=0.5,B=0.0,A=1.0)"` | `ColorAndOpacity`, `BrushColor`, `TintColor` |
| `transform2d` | `UMovieScene2DTransformTrack` | `"0,0,1,1,0,0,0"` (TX,TY,SX,SY,ShX,ShY,Angle) | `RenderTransform` |

### Example: Fade-in Animation
```
create_widget_animation("/Game/UI/W_MyWidget", "FadeIn", 0.5)
bind_animation_widget("/Game/UI/W_MyWidget", "FadeIn", "ContentBox")
add_animation_track("/Game/UI/W_MyWidget", "FadeIn", "ContentBox", "float", "RenderOpacity")
add_animation_keyframe("/Game/UI/W_MyWidget", "FadeIn", "ContentBox", "RenderOpacity", 0.0, "0.0", "Cubic")
add_animation_keyframe("/Game/UI/W_MyWidget", "FadeIn", "ContentBox", "RenderOpacity", 0.5, "1.0", "Cubic")
compile_and_save("/Game/UI/W_MyWidget")
```

### Example: Color Pulse Animation
```
create_widget_animation("/Game/UI/W_MyWidget", "Pulse", 1.0)
bind_animation_widget("/Game/UI/W_MyWidget", "Pulse", "Indicator")
add_animation_track("/Game/UI/W_MyWidget", "Pulse", "Indicator", "color", "ColorAndOpacity")
add_animation_keyframe("/Game/UI/W_MyWidget", "Pulse", "Indicator", "ColorAndOpacity", 0.0, "(R=1.0,G=1.0,B=1.0,A=1.0)")
add_animation_keyframe("/Game/UI/W_MyWidget", "Pulse", "Indicator", "ColorAndOpacity", 0.5, "(R=0.916,G=0.389,B=0.013,A=1.0)")
add_animation_keyframe("/Game/UI/W_MyWidget", "Pulse", "Indicator", "ColorAndOpacity", 1.0, "(R=1.0,G=1.0,B=1.0,A=1.0)")
compile_and_save("/Game/UI/W_MyWidget")
```

### Gotchas
- **Bind before track**: Widget must be bound (`bind_animation_widget`) before adding tracks
- **Track before keyframe**: Track must exist (`add_animation_track`) before adding keyframes
- **Compile to persist**: Call `compile_and_save` after all animation changes
- **Property path**: Must match the UE property name exactly (case-sensitive)
- **Color format**: Use UE FLinearColor ImportText format: `(R=0.5,G=0.5,B=0.5,A=1.0)`

---

*Version: 6.0.0 — Last Updated: 2026-03-08*
*59 MCP tools, covering Widget Builder, Material Builder, BP Graph, CDO, Array, Widget Animation, Import, Export*
