# CommonAIExport — AI Reference Guide

> **Read this file to understand ALL plugin capabilities in one place.**
> 198 MCP tools across 37 TCP command categories plus client-only tools. UE 5.7, TCP port auto-discovery plus multi-editor routing and native localhost HTTP/MCP probe support.

## Architecture

```
AI Assistant ──MCP stdio──> ai_widget_mcp_client.py ──TCP──> UE Editor (C++ TCP Server)
                                                              │
                                              ┌───────────────┼───────────────┐
                                              │               │               │
                                        Widget Builder  Material Builder  Asset Export
                                        BP Graph Builder  CDO Properties  Asset Import
```

- **MCP Client**: `Plugins/CommonAIExport/MCPClient/ai_widget_mcp_client.py`
- **TCP Server**: Runs inside UE Editor, port 55560-55600 (auto-discovery via `Intermediate/AIExport_port.txt`)
- **Multi-editor registry**: each editor writes identity to `%LOCALAPPDATA%/CommonAIExport/Editors/*.json`
- **All tools use `mcp__widget-builder__` prefix** when called from an MCP client
- **Property values use UE ImportText format** (same format the export system produces)
- **Contract validation**: `python Plugins/CommonAIExport/Resources/Scripts/preflight_mcp.py`

---

## 0. Connection / Introspection

| Tool | Purpose |
|------|---------|
| `ping()` | Check if the editor TCP server is responsive |
| `list_commands()` | List registered TCP commands with category, params, mutation, timeout, scope, dry-run, and async metadata |
| `server_status()` | Runtime status: port, command count, scope model, transport summary, and async task counters |
| `editor_identity()` | Identity metadata for the default editor: editor_id, project path, engine/plugin version, port, registry file |
| `command_manifest_export(output_path?)` | Export machine-readable command manifest JSON under the project |
| `project_status()` | Read project/editor workflow status, repo markers, log/build state, world/PIE summary |
| `source_control_status(provider?, repo_path?, path?, no_limit?)` | Read-only Diversion/Git status through the editor process |
| `source_control_log(provider?, repo_path?, path?, limit?, oneline?, since?, until?, ref?)` | Read recent Diversion/Git history from the editor process |
| `source_control_show(provider?, repo_path?, ref?, name_status?)` | Show a Diversion/Git revision summary |
| `source_control_diff(provider?, repo_path?, path?, base?, compare?, name_status?)` | Read read-only Diversion/Git diff output |
| `editors_list(include_stale?)` | List live Unreal Editor instances with CommonAIExport loaded |
| `editor_call(command, params?, editor_id?, project_dir?, port?, scope?, dry_run?)` | Route any CommonAIExport command to a selected editor |
| `asset_transfer_plan(source_asset_path, source_editor_id?, target_editor_id?, ...)` | Read-only cross-project asset transfer plan with dependencies, target existence, and collision analysis |
| `asset_transfer_execute(source_asset_path, ..., scope?, dry_run?, overwrite?)` | Copy planned package files between project Content folders, then scan/verify target |
| `asset_transfer_verify(source_asset_path, source_editor_id?, target_editor_id?, ...)` | Read-only verification that planned packages exist in target |
| `code_transfer_plan(source_paths, source_editor_id?, target_editor_id?, ...)` | Read-only cross-project C++/config file transfer plan |
| `code_transfer_execute(source_paths, ..., scope?, dry_run?, overwrite?)` | Guarded code/config file copy between project folders |
| `code_transfer_verify(source_paths, source_editor_id?, target_editor_id?, ...)` | Hash-based verification for planned code/config transfer |
| `commonai_resources_list()` / `commonai_resource_read(uri)` | Resource-style project status, command manifest, editor registry, and log context |
| `commonai_prompts_list()` / `commonai_prompt_get(name, asset_path?)` | Reusable workflow prompt templates |
| `guarded_build_status(tail_lines?)` | Parse the latest guarded build log |
| `client_scope_policy()` | Inspect process-local client scope and optional approval-hook settings |
| `mcp_server_metadata_export(output_path?)` | Export registry-style server metadata JSON |

---

## 1. Widget Blueprint Lifecycle

| Tool | Purpose |
|------|---------|
| `create_widget_blueprint(package_path, asset_name, parent_class?, scope?, dry_run?)` | Create new WBP. parent_class e.g. `/Script/CommonUI.CommonUserWidget` |
| `compile_and_save(asset_path, scope?, dry_run?)` | Compile + save to disk. **Always call after modifications.** |
| `reparent_blueprint(asset_path, new_parent_class, scope?, dry_run?)` | Change parent class. e.g. `/Script/LyraGame.LyraActivatableWidget` |
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
| `add_widget(asset_path, widget_class, widget_name, parent_name?, scope?, dry_run?)` | Add widget. parent="" = root |
| `remove_widget(asset_path, widget_name, scope?, dry_run?)` | Remove widget |
| `move_widget(asset_path, widget_name, new_parent, index?, scope?, dry_run?)` | Move widget. index=-1 = append |

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
| `set_widget_property(asset_path, widget_name, property_name, value, scope?, dry_run?)` | Set single property |
| `set_widget_properties(asset_path, widget_name, properties, scope?, dry_run?)` | Set multiple properties at once; accepts JSON string or dict |
| `set_slot_property(asset_path, widget_name, property_name, value, scope?, dry_run?)` | Set slot property (Padding, Size, Alignment, etc.) |
| `set_canvas_slot_layout(asset_path, widget_name, pos_x?, pos_y?, size_x?, size_y?, ..., scope?, dry_run?)` | Canvas slot shortcut |

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
| `set_cdo_property(asset_path, property_name, value, scope?, dry_run?)` | Set CDO property (bSelectable, MinWidth, Style, etc.) |
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
| `add_cdo_array_element(asset_path, array_name, element_values?, class_name?, scope?, dry_run?)` | Add element. element_values = JSON object of sub-properties |
| `set_cdo_array_element_property(asset_path, array_name, index, property_name, value, scope?, dry_run?)` | Set sub-property on element |
| `remove_cdo_array_element(asset_path, array_name, index, scope?, dry_run?)` | Remove by index |
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
| `add_event_node(asset_path, event_name, node_name, pos_x?, pos_y?, graph_name?, scope?, dry_run?)` | Add event override (Construct, Tick, BP_OnSelected, etc.) |
| `add_custom_event(asset_path, event_name, node_name, pos_x?, pos_y?, graph_name?, scope?, dry_run?)` | Add custom event |
| `ensure_function_graph(asset_path, function_name, inputs?, outputs?, entry_node_name?, result_node_name?, scope?, dry_run?)` | Create/update a function graph and tag entry/result nodes |
| `add_function_call(asset_path, function_name, node_name, target_class?, pos_x?, pos_y?, graph_name?, scope?, dry_run?)` | Add function call node |
| `add_variable_get_node(asset_path, var_name, node_name, pos_x?, pos_y?, graph_name?, scope?, dry_run?)` | Add Get node for variable |
| `add_variable_set_node(asset_path, var_name, node_name, pos_x?, pos_y?, graph_name?, scope?, dry_run?)` | Add Set node for variable |
| `add_make_struct_node(asset_path, struct_type, node_name, pos_x?, pos_y?, graph_name?, scope?, dry_run?)` | Add Make Struct node |
| `add_branch_node(asset_path, node_name, pos_x?, pos_y?, graph_name?, scope?, dry_run?)` | Add Branch (if) node |
| `connect_pins(asset_path, source_node, source_pin, target_node, target_pin, graph_name?, scope?, dry_run?)` | Connect two pins |
| `set_pin_default(asset_path, node_name, pin_name, value, graph_name?, scope?, dry_run?)` | Set pin default value |
| `remove_graph_node(asset_path, node_name, graph_name?, scope?, dry_run?)` | Remove a graph node |
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
| `add_variable(asset_path, var_name, var_type, instance_editable?, blueprint_read_only?, category?, scope?, dry_run?)` | Add member variable |
| `set_variable_default(asset_path, var_name, value, scope?, dry_run?)` | Set default value |
| `remove_variable(asset_path, var_name, scope?, dry_run?)` | Remove variable |
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

### Blueprint Components

| Tool | Purpose |
|------|---------|
| `blueprint_component_list(asset_path)` | List SCS components on an Actor Blueprint |
| `blueprint_component_add(asset_path, component_name, component_class, parent_component_name?, compile_blueprint?, save_asset?, scope?, dry_run?)` | Add an ActorComponent/SceneComponent node |
| `blueprint_component_remove(asset_path, component_name, promote_children?, compile_blueprint?, save_asset?, scope?, dry_run?)` | Remove a component node |
| `blueprint_component_set_property(asset_path, component_name, property_path, value, compile_blueprint?, save_asset?, scope?, dry_run?)` | Set a component template property using ImportText |

```python
blueprint_component_add(
    "/Game/Blueprints/BP_DebugActor",
    "Mesh",
    "/Script/Engine.StaticMeshComponent",
    compile_blueprint=True
)
blueprint_component_set_property(
    "/Game/Blueprints/BP_DebugActor",
    "Mesh",
    "RelativeLocation",
    "(X=0,Y=0,Z=80)"
)
blueprint_component_list("/Game/Blueprints/BP_DebugActor")
```

---

## 8. Material System

### Material Creation & Editing

| Tool | Purpose |
|------|---------|
| `create_material(package_path, asset_name, domain?, blend_mode?, shading_model?, two_sided?, scope?, dry_run?)` | Create material |
| `set_material_property(asset_path, property_name, value, scope?, dry_run?)` | Set material property (domain, blend, shading) |
| `add_expression(asset_path, expression_class, node_name, pos_x?, pos_y?, scope?, dry_run?)` | Add expression node |
| `set_expression_property(asset_path, node_name, property_name, value, scope?, dry_run?)` | Set expression property |
| `connect_expressions(asset_path, from_node, from_output, to_node, to_input, scope?, dry_run?)` | Wire nodes |
| `connect_to_material_property(asset_path, from_node, from_output, material_property, scope?, dry_run?)` | Wire to root |
| `disconnect_input(asset_path, node_name, input_name, scope?, dry_run?)` | Disconnect input |
| `remove_expression(asset_path, node_name, scope?, dry_run?)` | Remove expression |
| `compile_material(asset_path, scope?, dry_run?)` | Compile + save |
| `get_material_graph(asset_path)` | Get graph as JSON |
| `list_expression_classes()` | List available expression types |

### Material Instance

| Tool | Purpose |
|------|---------|
| `create_material_instance(package_path, asset_name, parent_material_path, scope?, dry_run?)` | Create MIC |
| `set_instance_parameter(asset_path, param_name, param_type, value, scope?, dry_run?)` | Set scalar/vector/texture param |
| `save_material_instance(asset_path, scope?, dry_run?)` | Save to disk |
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

## 9. Data Asset

| Tool | Purpose |
|------|---------|
| `save_data_asset(asset_path, scope?, dry_run?)` | Save a Data Asset to disk without compiling |

---

## 10. Generic Asset Factory

| Tool | Purpose |
|------|---------|
| `create_asset(package_path, asset_name, asset_type, properties?, scope?, dry_run?)` | Create InputAction, InputMappingContext, Sound*, PhysicalMaterial |
| `set_asset_property(asset_path, property_path, value, scope?, dry_run?)` | Set a reflected property on any loaded asset |
| `get_asset_properties(asset_path)` | Get all reflected asset properties as JSON |
| `asset_search(path?, name_filter?, class_filter?, recursive?, offset?, limit?)` | Search Asset Registry with pagination |
| `asset_validate_light(asset_path)` | Lightweight existence/dependency/redirector health check |
| `asset_exists(asset_path)` | Check Asset Registry/on-disk package existence without loading full properties |
| `scan_asset_paths(paths?, path?, force_rescan?)` | Rescan Content paths after external package file copies |
| `save_asset(asset_path, scope?, dry_run?)` | Save any loaded asset to disk |
| `rename_asset(asset_path, new_package_path?, new_asset_name?, scope?, dry_run?)` | Rename or move an asset using AssetTools |
| `get_referencers(asset_path)` | List packages that reference the asset |
| `get_dependencies(asset_path)` | List packages referenced by the asset |
| `delete_asset(asset_path, force?, scope?, dry_run?)` | Delete an asset after reference checks, or force delete when explicitly requested. Requires `scope="destructive"` |
| `list_redirectors(folder_path, recursive?)` | List redirectors under a folder |
| `fixup_redirectors(folder_path, recursive?, scope?, dry_run?)` | Fix redirector referencers under a folder |

---

## 11. Static Mesh

| Tool | Purpose |
|------|---------|
| `static_mesh_info(asset_path, include_lods?, include_sections?, include_materials?, include_sockets?, include_collision?, include_nanite?, lod_limit?, section_limit?, material_limit?, socket_limit?)` | Inspect StaticMesh LODs, sections, material slots, sockets, BodySetup collision, bounds, memory estimate, and Nanite state |

---

## 12. Skeletal Mesh

| Tool | Purpose |
|------|---------|
| `skeletal_mesh_info(asset_path, include_lods?, include_sections?, include_materials?, include_sockets?, include_skeleton?, include_physics_asset?, include_bounds?, include_nanite?, lod_limit?, section_limit?, material_limit?, socket_limit?, bone_limit?, physics_body_limit?, constraint_limit?)` | Inspect SkeletalMesh LODs, sections, material slots, sockets, reference skeletons, PhysicsAsset bodies/constraints, bounds, memory estimate, morph count, and Nanite state |

---

## 13. Input Mapping Context

| Tool | Purpose |
|------|---------|
| `add_input_mapping(asset_path, input_action_path, key, triggers?, modifiers?, scope?, dry_run?)` | Add a key mapping to an IMC |
| `remove_input_mapping(asset_path, mapping_index, scope?, dry_run?)` | Remove a mapping by index |
| `get_input_mappings(asset_path)` | Get all mappings as JSON |

---

## 14. AnimBlueprint

| Tool | Purpose |
|------|---------|
| `create_anim_blueprint(package_path, asset_name, skeleton_path, parent_class?, scope?, dry_run?)` | Create an Animation Blueprint |
| `get_anim_blueprint_info(asset_path)` | Get AnimBlueprint skeleton, parent, graphs, and variables as JSON |

---

## 15. Animation Asset

| Tool | Purpose |
|------|---------|
| `animation_asset_info(asset_path, include_notifies?, include_curves?, include_montage?, include_sequence?, include_skeleton?, include_data_model?, include_markers?, include_references?, notify_limit?, curve_limit?, marker_limit?, track_name_limit?, slot_limit?, section_limit?, segment_limit?, reference_limit?)` | Inspect AnimSequence/AnimMontage timing, skeleton, notifies, curves, source data model, sync markers, referenced animations, montage sections, slots, and segments |

---

## 16. Sequencer

| Tool | Purpose |
|------|---------|
| `sequencer_asset_info(asset_path, include_sections?, binding_limit?, track_limit?, section_limit?)` | Inspect a LevelSequence MovieScene: timing, bindings, tracks, spawnables/possessables, and sections |

---

## 17. Spline

| Tool | Purpose |
|------|---------|
| `spline_actor_create(actor_label?, component_name?, location?, points?, coordinate_space?, point_type?, closed_loop?, set_closed_loop?, on_conflict?, scope?, dry_run?)` | Create an editor actor with a SplineComponent and optional initial points |
| `spline_component_info(actor_path?, actor_label?, actor_name?, component_name?, world?, include_points?, point_limit?)` | Inspect spline components, point types, tangents, rotations, scale, distance, and length |
| `spline_component_set_points(points, actor_path?, actor_label?, actor_name?, component_name?, coordinate_space?, point_type?, closed_loop?, set_closed_loop?, scope?, dry_run?)` | Replace spline control points on an editor actor |

---

## 18. Landscape

| Tool | Purpose |
|------|---------|
| `landscape_info(world?, name_filter?, include_components?, include_layers?, include_splines?, landscape_limit?, component_limit?, layer_limit?, spline_control_point_limit?, spline_segment_limit?, weightmap_allocation_limit?)` | Inspect Landscape proxies, components, target layers, materials, and Landscape splines |
| `landscape_sample_height(x, y, z?, trace_extent?, world?, name_filter?)` | Sample Landscape height and normal at an XY point |

---

## 19. Foliage

| Tool | Purpose |
|------|---------|
| `foliage_info(world?, type_filter?, include_settings?, foliage_actor_limit?, foliage_type_limit?)` | Inspect InstancedFoliageActors, FoliageTypes, settings, HISM components, and counts |
| `foliage_sample_instances(x, y, z?, radius?, limit?, scan_limit?, world?, type_filter?)` | Sample Foliage instance transforms around a world-space point with bounded scanning |
| `foliage_type_settings(foliage_type_path)` | Inspect a FoliageType asset's placement, rendering, scalability, and procedural settings |

---

## 20. PCG

| Tool | Purpose |
|------|---------|
| `pcg_graph_info(asset_path, include_nodes?, include_pins?, include_edges?, include_settings?, node_limit?, pin_limit?, edge_limit?, setting_property_limit?)` | Inspect PCGGraph nodes, pins, edges, and settings |
| `pcg_component_info(world?, actor_path?, actor_label?, actor_name?, name_filter?, component_name?, include_graph?, include_resources?, component_limit?, resource_limit?)` | Inspect PCGComponents and generation state in a world |

---

## 21. Asset Import

| Tool | Purpose |
|------|---------|
| `import_texture(source_path, package_path, asset_name?, compression?, srgb?, mip_gen?, lod_group?, scope?, dry_run?)` | Import texture from disk |
| `import_font(package_path, font_name, faces, hinting?, scope?, dry_run?)` | Import TTF/OTF into a Composite Font |

---

## 22. Widget Preview Capture (IFTP verify loop)

Renders a Widget Blueprint to a PNG file (or files, one per ratio) so an AI assistant can
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

### AI assistant workflow
1. `capture_widget_preview(...)` returns JSON with `png_path` entries.
2. Use the `Read` tool on each `png_path` — the assistant reads PNG as an image (multimodal).
3. Compare visually with the Pencil reference (from `mcp__pencil__get_screenshot`).
4. Write findings to `Docs/AI_UI_Transfer/ui-fidelity-log.md`.

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

## 23. Asset Lifecycle

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

---

## 24. Editor, Level, Actor and PIE

These tools provide the first general editor automation package outside
Widget/Material-specific workflows. They operate on the current editor world and
inherit the same scope/dry-run model exposed through `list_commands`.

| Tool | Purpose |
|------|---------|
| `editor_world_info()` | Inspect current editor world, package/map filename, level list, actor count, and PIE state |
| `runtime_world_info(world?)` | Inspect PIE/runtime world metadata, falling back to editor world in auto mode |
| `runtime_player_list(world?)` | List runtime player controllers, local players, and possessed pawns |
| `runtime_component_list(world?, actor_path?, actor_label?, actor_name?, name_filter?, actor_class_filter?, component_class_filter?, limit?, include_scene_details?, include_hierarchy?, hierarchy_depth?, hierarchy_actor_limit?, hierarchy_component_limit?)` | List actor components in PIE/runtime or editor worlds, optionally with scene transforms, mesh refs, ISM counts, and hierarchy |
| `runtime_diagnostics(world?, actor_path?, actor_label?, actor_name?, include_components?, component_limit?)` | Capture a focused PIE/runtime diagnostics snapshot with world, player, warning, and optional actor/component details |
| `runtime_input_routing(world?)` | Inspect player controller input components, EnhancedInput subsystem state, and CommonInput local-player state |
| `runtime_replication_diagnostics(world?, actor_path?, actor_label?, actor_name?, name_filter?, class_filter?, include_components?, actor_limit?, component_limit?)` | Inspect actor replication roles, dormancy, relevancy flags, movement replication, and replicated component state |
| `runtime_ability_system_diagnostics(world?, actor_path?, actor_label?, actor_name?, name_filter?, class_filter?, include_abilities?, include_effects?, include_attributes?, actor_limit?, ability_limit?, effect_limit?, attribute_limit?)` | Inspect runtime AbilitySystemComponent tags, granted abilities, active gameplay effects, attributes, and ownership state |
| `runtime_ai_perception_diagnostics(world?, actor_path?, actor_label?, actor_name?, name_filter?, class_filter?, target_name_filter?, include_stimuli?, listener_limit?, target_limit?, stimulus_limit?)` | Inspect runtime AIPerceptionComponent listeners, sense configs, known/current targets, hostile targets, and stimuli |
| `runtime_ai_controller_diagnostics(world?, actor_path?, actor_label?, actor_name?, name_filter?, class_filter?, pawn_filter?, include_blackboard_values?, include_behavior_debug?, include_path_points?, include_perception?, controller_limit?, blackboard_key_limit?, path_point_limit?, debug_string_limit?)` | Inspect runtime AIController focus/move state, possessed pawn, Brain/BehaviorTree, Blackboard keys, and PathFollowing path summaries |
| `runtime_eqs_diagnostics(world?, name_filter?, class_filter?, include_registered_item_types?, include_wrappers?, registered_item_type_limit?, wrapper_limit?, debug_string_limit?)` | Inspect runtime EQS manager/config availability, registered item types, and query wrapper summaries |
| `runtime_gameplay_tags_diagnostics(world?, actor_path?, actor_label?, actor_name?, name_filter?, class_filter?, component_class_filter?, tag_filter?, include_dictionary?, include_components?, actor_limit?, component_limit?, tag_limit?)` | Inspect Gameplay Tags dictionary metadata, replication flags, source paths, and runtime tagged actor/component state |
| `runtime_commonui_diagnostics(world?, name_filter?, class_filter?, include_widgets?, include_bindings?, local_player_limit?, widget_limit?, container_limit?, binding_limit?)` | Inspect runtime CommonUI action router state, active bindings, activatable widgets, and containers |
| `runtime_audio_diagnostics(world?, actor_path?, actor_label?, actor_name?, name_filter?, class_filter?, component_class_filter?, sound_filter?, include_inactive?, component_limit?)` | Inspect runtime audio device state and AudioComponent playback metadata |
| `runtime_navigation_diagnostics(world?, nav_data_filter?, include_nav_data?, include_bounds?, include_supported_agents?, include_invokers?, nav_data_limit?, bounds_limit?, invoker_limit?, tile_sample_limit?)` | Inspect NavigationSystem build state, supported agents, nav data, bounds, invokers, and Recast NavMesh tile/config summaries |
| `runtime_asset_streaming_diagnostics(world?, include_levels?, level_limit?)` | Inspect streaming manager counters, render asset streaming pool status, and per-level streaming state |
| `runtime_async_load_diagnostics(world?, asset_path?, package_name?, asset_paths?, package_names?, include_package_probes?, include_streamable_handles?, include_streaming_manager?, probe_limit?, asset_data_limit?, handle_limit?, requested_asset_limit?)` | Inspect async package loading, AssetRegistry scan state, StreamableManager handles, and per-package load progress probes |
| `runtime_game_instance_diagnostics(world?, include_local_players?, include_subsystems?, include_save_names?, save_slot_name?, save_user_index?, local_player_limit?, subsystem_limit?, save_name_limit?)` | Inspect GameInstance, LocalPlayer, subsystem, and save-game system state |
| `runtime_level_travel_diagnostics(world?, include_url_options?, include_preparing_levels?, url_option_limit?, preparing_level_limit?)` | Inspect current URL, pending server travel, seamless travel, preparing levels, and NetDriver state |
| `runtime_multiplayer_connection_diagnostics(world?, include_connections?, include_player_controllers?, include_world_context?, include_url_options?, connection_limit?, player_controller_limit?, url_option_limit?)` | Inspect multiplayer NetDriver, connection, PlayerController, world-context, PendingNetGame, and online-session shell state |
| `runtime_tick_timer_latent_diagnostics(world?, object_path?, actor_path?, actor_label?, actor_name?, include_latent_actions?, latent_action_limit?)` | Inspect world tick/time state, time dilation, timer manager tick state, and targeted latent actions |
| `runtime_scheduler_performance_diagnostics(world?, name_filter?, class_filter?, component_class_filter?, include_actor_ticks?, include_component_ticks?, actor_limit?, component_limit?, hitch_threshold_ms?)` | Inspect app frame timing, TaskGraph/threading state, hitch flags, and actor/component tick scheduling state |
| `runtime_physics_collision_diagnostics(world?, name_filter?, class_filter?, component_class_filter?, include_components?, include_responses?, component_limit?)` | Inspect physics settings/world gravity and primitive component collision, body, and response state without running queries |
| `actor_list(name_filter?, class_filter?, limit?)` | List actors with class path and transform metadata |
| `actor_spawn(class_path, actor_label?, location?, rotation?, scale?, scope?, dry_run?)` | Spawn an actor with transaction support |
| `actor_set_transform(actor_path?, actor_label?, actor_name?, location?, rotation?, scale?, scope?, dry_run?)` | Move/rotate/scale an actor identified by path, label, or name |
| `actor_delete(actor_path?, actor_label?, actor_name?, scope?, dry_run?)` | Delete an actor; requires `scope="destructive"` |
| `level_open(map_path, scope?, dry_run?)` | Open a map by long package path or filename |
| `level_save_current(scope?, dry_run?)` | Save the current persistent level |
| `level_structure_info(world?, include_streaming_levels?, include_world_partition?, include_data_layers?, include_level_instances?, include_hlod?, level_limit?, data_layer_limit?, level_instance_limit?, hlod_limit?)` | Inspect streaming levels, World Partition, Data Layers, Level Instances, and HLOD without mutating the world |
| `pie_status()` | Read Play-In-Editor active/simulating state |
| `pie_start(scope?, dry_run?)` | Request PIE start |
| `pie_stop(scope?, dry_run?)` | Request PIE stop |
| `editor_console_command(command, scope?, dry_run?)` | Execute editor console command; requires `scope="destructive"` |
| `viewport_capture(output_path?, show_ui?, add_filename_suffix?, scope?, dry_run?)` | Request an editor viewport screenshot |

### Example: Inspect and dry-run editor actions

```python
editor_world_info()
runtime_world_info()
runtime_player_list()
runtime_component_list(limit=25, include_scene_details=True)
runtime_component_list(actor_label="BP_DebugPawn", include_hierarchy=True, include_scene_details=True)
runtime_diagnostics(component_limit=25)
actor_list(limit=10)

actor_spawn(
    class_path="/Script/Engine.StaticMeshActor",
    actor_label="AI_TestActor",
    location={"x": 0, "y": 0, "z": 120},
    scope="write",
    dry_run=True
)

actor_delete(
    actor_label="AI_TestActor",
    scope="destructive",
    dry_run=True
)

pie_status()
editor_console_command(command="stat fps", scope="destructive", dry_run=True)
viewport_capture(scope="write", dry_run=True)
```

Actor spawn, transform, and delete handlers wrap editor changes in
`FScopedTransaction`, so successful mutations participate in Undo. Dry-run is
generic at this stage: it verifies command identity/scope and exits before the
handler touches the editor.

---

## 25. Multi-Editor Routing

When the same CommonAIExport plugin is loaded in multiple Unreal Editor
projects, each editor writes a global registry entry. The Python MCP server can
discover those entries and route commands to a selected target without changing
the existing TCP protocol.

| Tool | Purpose |
|------|---------|
| `editors_list(include_stale?)` | Discover live editor instances and their project path, port, PID, engine/plugin version, and capabilities |
| `editor_call(command, params?, editor_id?, project_dir?, port?, scope?, dry_run?)` | Execute any CommonAIExport command against a selected editor |
| `asset_transfer_plan(source_asset_path, source_editor_id?, target_editor_id?, source_project_dir?, target_project_dir?, source_port?, target_port?, target_asset_path?, max_depth?, max_assets?)` | Build a read-only transfer plan before any copy/import work |
| `asset_transfer_execute(source_asset_path, ..., scope?, dry_run?, overwrite?, allow_same_editor?)` | Copy planned package files, scan target asset paths, and run verify |
| `asset_transfer_verify(source_asset_path, source_editor_id?, target_editor_id?, ...)` | Verify planned packages exist in the target project |

### Example: Inspect two open projects

```python
editors_list()
# -> [{"editor_id": "ProjectA-1234-55560", ...}, {"editor_id": "ProjectB-5678-55561", ...}]

editor_call(
    command="get_dependencies",
    editor_id="ProjectA-1234-55560",
    params={"asset_path": "/Game/UI/Hud/W_MainMenu"}
)

editor_call(
    command="get_asset_properties",
    editor_id="ProjectB-5678-55561",
    params={"asset_path": "/Game/UI/Hud/W_MainMenu"}
)

asset_transfer_plan(
    source_asset_path="/Game/UI/Hud/W_MainMenu",
    source_editor_id="ProjectA-1234-55560",
    target_editor_id="ProjectB-5678-55561",
    max_depth=2
)

asset_transfer_execute(
    source_asset_path="/Game/UI/Hud/W_MainMenu",
    source_editor_id="ProjectA-1234-55560",
    target_editor_id="ProjectB-5678-55561",
    scope="write",
    dry_run=False
)
```

This is a routing layer, not a full asset migration engine yet. Cross-project
asset/code transfer begins with read-only inspection and dry-run planning so
dependency closure, missing classes/plugins, path collisions, source-control
state, and build risk are explicit before mutations.
`asset_transfer_execute` copies Unreal package files only (`.uasset`, `.umap`,
`.uexp`, `.ubulk`, `.uptnl`) under project `Content`; it does not migrate C++
modules, plugin binaries, config, source-control changelists, or dependency path
rewrites yet. Actual copy requires `scope="write"`. Overwrite requires
`scope="destructive"`.

---

## 26. Code Transfer

| Tool | Purpose |
|------|---------|
| `code_transfer_plan(source_paths, source_editor_id?, target_editor_id?, target_subdir?, preserve_relative_paths?, include_companions?, max_files?)` | Read-only plan for source/config file transfer |
| `code_transfer_execute(source_paths, ..., scope?, dry_run?, overwrite?, allow_same_editor?)` | Copy planned text/code files between projects |
| `code_transfer_verify(source_paths, source_editor_id?, target_editor_id?, ...)` | Verify target file hashes match the source plan |

Code transfer is intentionally conservative. It blocks project cache/build
folders such as `Binaries`, `Intermediate`, `Saved`, `.vs`, and only allows
text/code-like extensions such as `.h`, `.cpp`, `.cs`, `.ini`, `.json`,
`.uplugin`, `.uproject`, `.usf`, and `.ush`. Actual copy requires
`scope="write"`. Overwrite requires `scope="destructive"`. C++ transfers should
be followed by Build.cs/module/API macro review and the guarded build workflow.

---

## 27. Async Jobs and Safety Metadata

| Tool | Purpose |
|------|---------|
| `task_submit(command, params?, scope?, dry_run?)` | Submit any non-task command for background execution |
| `task_status(task_id?)` | Check one task, or list all known tasks when omitted |
| `task_result(task_id?)` | Read a completed task response, or list completed task summaries when omitted |
| `task_events(task_id?, after_sequence?, limit?)` | Read queued/running/completed/failed/cancel lifecycle events with sequence cursors |
| `task_events_wait(task_id?, after_sequence?, limit?, timeout_ms?, poll_interval_ms?)` | Long-poll briefly for matching lifecycle events before returning the same event payload |
| `task_cancel(task_id?)` | Request cooperative cancellation, or list cancellable tasks when omitted |

### Scope model

CommonAIExport now exposes command safety metadata through `list_commands`.
Scopes are ordered as `read < write < destructive`. Legacy TCP calls without
metadata keep implicit `write` scope for backward compatibility, but
`destructive` commands require an explicit top-level `meta.scope="destructive"`.
The Python MCP wrapper exposes this on destructive tools such as
`delete_asset(..., scope="destructive")` and
`actor_delete(..., scope="destructive")`.

### Dry-run model

Pass `dry_run=True` through the MCP wrapper, or top-level `meta.dry_run=true`
over raw TCP. Mutating commands return a generic dry-run response before any
handler runs. This is intentionally conservative: it validates command identity
and scope but does not attempt tool-specific previews yet.

### Async model

`task_submit` stores task state in the editor process and runs the selected
command on the server thread pool. The initial call returns immediately with a
`task_id`; use `task_status`, `task_events`, `task_events_wait`, and
`task_result` to poll. `task_events` returns ordered lifecycle events and
`latest_sequence` for cursor-style progress polling. `task_events_wait` adds a
bounded wait (`timeout_ms`, default 5000, clamped to 30000) and returns the same
payload plus `waited_ms` and `timed_out`. Native HTTP also exposes the same data
as JSON at `/commonai/tasks/events`, as long-poll JSON at
`/commonai/tasks/events/wait`, and as an SSE-formatted snapshot at
`/commonai/tasks/events/sse`. UE's built-in HTTPServer response API writes one
complete response body, so the SSE endpoint is compatibility formatting over
the event store rather than an infinite live socket stream. Cancellation is
cooperative: queued work can be cancelled before dispatch, but already-running
game-thread work may finish before cancellation is observed.

### Example: Async Preview Capture
```python
task_submit(
    command="capture_widget_preview",
    params={"asset_path": "/Game/UI/Menu/W_MainMenu", "width": 1920, "height": 1080}
)
# -> {"task_id": "...", "status": "queued"}
task_status("...")
task_events("...")
task_events_wait("...", after_sequence=27)
task_result("...")
```

---

## 28. Workflow and Logs

| Tool | Purpose |
|------|---------|
| `project_status()` | Read editor/project status including repo markers, log count, build-log presence, and world/PIE state |
| `source_control_status(provider?, repo_path?, path?, no_limit?)` | Run read-only Diversion/Git status from the editor process |
| `source_control_log(provider?, repo_path?, path?, limit?, oneline?, since?, until?, ref?)` | Read recent Diversion/Git history; use `repo_path="Plugins/CommonAIExport"` for the plugin Git repo |
| `source_control_show(provider?, repo_path?, ref?, name_status?)` | Show a Diversion/Git commit or current revision summary |
| `source_control_diff(provider?, repo_path?, path?, base?, compare?, name_status?)` | Read read-only Diversion/Git diff output |
| `editor_log_read(max_lines?, filter?, log_name?)` | Read recent project log lines from `Saved/Logs`, optionally filtered |
| `guarded_build_status(tail_lines?)` | Parse latest `Saved/Logs/LastBuild.log` result and return a bounded tail |

---

## 29. MCP Resources, Prompts, and Metadata

| Tool | Purpose |
|------|---------|
| `commonai_resources_list()` | List resource URIs exposed by the MCP wrapper |
| `commonai_resource_read(uri)` | Read `commonai://project/status`, `commonai://commands/manifest`, `commonai://editors/list`, `commonai://tasks/events`, `commonai://logs/latest`, or `commonai://audit/http` |
| `commonai_prompts_list()` | List reusable workflow prompt templates |
| `commonai_prompt_get(name, asset_path?)` | Read a prompt template such as `build_fix_test`, `asset_safety_review`, `multi_editor_transfer`, `ui_transfer_validation`, `blueprint_graph_inspection`, or `runtime_debug_triage` |
| `command_manifest_export(output_path?)` | Write the active command descriptor manifest to JSON under the project |
| `client_scope_policy()` | Inspect client-side max-scope, explicit-scope, and approval-hook policy |
| `mcp_server_metadata_export(output_path?)` | Write registry-style CommonAIExport MCP metadata under the project |
| `native_http_status()` | Probe native C++ HTTP `/commonai/health` endpoint |
| `native_mcp_probe()` | Probe native C++ `/mcp` JSON-RPC initialize and paginated tools/list using MCP session headers |

Generated artifacts are checked by the contract validator and live under
`Plugins/CommonAIExport/Resources/Generated/`:

- `CommonAIExport_CommandManifest.json`
- `CommonAIExport_ToolSchemas.json`
- `CommonAIExport_ToolCatalog.md`
- `CommonAIExport_server.json`
- `CommonAIExport_WrapperSpec.json`
- `CommonAIExport_MCPWrapperStubs.py`
- `CommonAIExport_MCPWrapperRuntime.py`

Capability ownership is checked by `Resources/CapabilityMatrix.json`; see
`Docs/Reference/CAPABILITY_MATRIX.md`. Every native TCP command and every
client-only MCP tool must be assigned to exactly one capability before the
contract validator passes.

`CommonAIExport_WrapperSpec.json` binds each TCP descriptor to the Python MCP
wrapper function, records the wrapper signature and payload inclusion rules,
and fails validation when a wrapper is missing or calls the wrong TCP command.
`CommonAIExport_MCPWrapperStubs.py` is a generated review aid for the next
wrapper-generation pass. `CommonAIExport_MCPWrapperRuntime.py` is imported by
the MCP client for selected pass-through wrappers. Generated runtime metadata
now covers all 161 TCP wrappers: read-only payload wrappers, the safe write-scope set
(editor/level/actor/PIE/viewport, Asset/DataAsset/Input, Import,
AnimBlueprint, CDO/CDOArray, Material graph/MIC, Project/ProjectConfig,
Blueprint graph/variable, Blueprint utility, and Widget wrappers), and the current destructive dry-run set
(`actor_delete`, `delete_asset`, and `editor_console_command`).
Payload fields, optional dict transform values, `scope`, and `dry_run` request
meta are encoded explicitly so destructive tools keep their existing
client/server scope gates. `task_submit` is the payload-scope exception: its
`scope` and `dry_run` fields stay in the TCP payload because they describe the
submitted command, not the submit command itself. Payload inclusion rules now
include explicit numeric predicates such as `when_ge_zero`; static tests fail
if wrapper metadata falls back to unresolved `conditional` rules.

Regenerate them with:

```powershell
python Plugins/CommonAIExport/Resources/Scripts/generate_mcp_artifacts.py
python Plugins/CommonAIExport/Resources/Scripts/validate_mcp_contract.py
python Plugins/CommonAIExport/Resources/Scripts/test_mcp_contract.py
python Plugins/CommonAIExport/Resources/Scripts/smoke_mcp_runtime.py
python Plugins/CommonAIExport/Resources/Scripts/smoke_mcp_runtime.py --mutating-smoke
```

For the non-runtime preflight, use the wrapper:

```powershell
python Plugins/CommonAIExport/Resources/Scripts/preflight_mcp.py
python Plugins/CommonAIExport/Resources/Scripts/preflight_mcp.py --runtime-smoke --mutating-smoke
```

### Example: Read recent errors

```python
project_status()
source_control_status()
source_control_status(provider="git", repo_path="Plugins/CommonAIExport")
guarded_build_status()
editor_log_read(max_lines=500, filter="Error")
commonai_resource_read("commonai://commands/manifest")
commonai_prompt_get("asset_safety_review", asset_path="/Game/UI/Hud/W_Background")
client_scope_policy()
native_http_status()
native_mcp_probe()
```

The native HTTP/MCP endpoint is localhost-only by default. Set
`COMMONAI_MCP_HTTP_TOKEN` before launching the editor to require
`Authorization: Bearer <token>` for HTTP requests. The Python probe tools read
the same environment variable automatically. `/mcp initialize` returns a
`Mcp-Session-Id` response header and `tools/list` supports `nextCursor`
pagination. `DELETE /mcp` with the same session header releases the session;
sessions expire after `COMMONAI_MCP_SESSION_TTL_SECONDS` seconds, defaulting to
3600. Set `COMMONAI_MCP_HTTP_ALLOWED_ORIGINS` to a comma-separated origin list
when a browser-based local client needs something beyond the default localhost
origins. HTTP/MCP requests are written as token-safe JSONL audit events to
`Saved/Logs/CommonAIExport_HTTP_Audit.jsonl`; set `COMMONAI_MCP_HTTP_AUDIT=0`
before launching the editor to disable audit logging.

Client-side scope policy is process-local and preserves existing behavior by
default. Set `COMMONAI_MCP_CLIENT_MAX_SCOPE=read` or `write` to block
higher-scope commands from this MCP client, set
`COMMONAI_MCP_REQUIRE_EXPLICIT_SCOPE=1` to require explicit wrapper scope for
mutations, and set `COMMONAI_MCP_APPROVAL_COMMAND` to a command that accepts a
JSON request on stdin and exits `0` to approve.

`editor_log_read` only resolves `log_name` under the project log directory; arbitrary
filesystem reads are intentionally not exposed through this command.

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

<!-- BEGIN COMMONAI GENERATED TOOL SUMMARY -->
> Generated by `Resources/Scripts/generate_mcp_artifacts.py`; do not edit this block by hand.

- TCP commands: `179`
- Client-only MCP tools: `19`
- Total MCP tools: `198`
- Categories: `38`
- Full generated catalog: `Resources/Generated/CommonAIExport_ToolCatalog.md`

| Category | Count |
|---|---:|
| `AnimBlueprint` | 2 |
| `AnimationAsset` | 1 |
| `Asset` | 14 |
| `AssetLifecycle` | 1 |
| `AsyncJob` | 6 |
| `BlueprintComponent` | 4 |
| `BlueprintGraph` | 14 |
| `BlueprintUtility` | 1 |
| `BlueprintVariable` | 4 |
| `CDO` | 2 |
| `CDOArray` | 4 |
| `DataAsset` | 1 |
| `DataTable` | 6 |
| `Editor` | 2 |
| `EditorActor` | 4 |
| `EditorLevel` | 3 |
| `EditorViewport` | 1 |
| `Export` | 3 |
| `Foliage` | 3 |
| `Import` | 2 |
| `Input` | 3 |
| `Landscape` | 2 |
| `Material` | 15 |
| `Niagara` | 1 |
| `PCG` | 2 |
| `PIE` | 3 |
| `Project` | 4 |
| `ProjectConfig` | 5 |
| `Reflection` | 9 |
| `RuntimeInspector` | 22 |
| `Sequencer` | 1 |
| `SkeletalMesh` | 1 |
| `Spline` | 3 |
| `StaticMesh` | 1 |
| `Utility` | 5 |
| `Widget` | 11 |
| `WidgetPreview` | 1 |
| `Workflow` | 12 |
| **Total** | **198** |

<!-- END COMMONAI GENERATED TOOL SUMMARY -->

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

*Version: 5.2.0 - Last Updated: 2026-05-04*
*198 MCP tools, covering Widget, Material, BP Graph, CDO, Reflection, DataTable, Asset, Import, Preview, Landscape, Foliage, PCG, StaticMesh and level-structure diagnostics, project/config operations, editor/level/actor/PIE/runtime inspector, class/struct/enum reflection, Gameplay Tags, AI Perception, AIController/Brain/Blackboard/PathFollowing diagnostics, EQS manager/query wrapper diagnostics, CommonUI runtime routing, audio device/component diagnostics, NavigationSystem/Recast NavMesh diagnostics, asset streaming, async load/progress diagnostics, async event long-polling, GameInstance/save-game state, level travel/session state, multiplayer connection diagnostics, tick/timer/latent-action state, scheduler/performance diagnostics, physics/collision diagnostics, logs/workflow source-control/build/test/cook status/history/diff, local UE docs lookup, resources/prompts, client scope policy, native HTTP/MCP probe, registry metadata, multi-editor routing, code transfer, async job, and contract introspection workflows*
