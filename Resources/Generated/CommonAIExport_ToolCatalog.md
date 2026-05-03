# CommonAIExport Generated Tool Catalog

> Generated from `FAIExportTCPServer::GetCommandDescriptors`; do not edit by hand.

- TCP commands: 125
- MCP tools: 142
- Categories: 24
- Parameter schemas: strict top-level JSON Schema from Python MCP wrapper signatures
- Wrapper registry: `Resources/Generated/CommonAIExport_WrapperSpec.json`
- Wrapper stubs: `Resources/Generated/CommonAIExport_MCPWrapperStubs.py`
- Wrapper runtime: `Resources/Generated/CommonAIExport_MCPWrapperRuntime.py`

## Categories

| Category | Count |
|---|---:|
| `AnimBlueprint` | 2 |
| `Asset` | 14 |
| `AssetLifecycle` | 1 |
| `AsyncJob` | 6 |
| `BlueprintGraph` | 14 |
| `BlueprintUtility` | 1 |
| `BlueprintVariable` | 4 |
| `CDO` | 2 |
| `CDOArray` | 4 |
| `DataAsset` | 1 |
| `Editor` | 2 |
| `EditorActor` | 4 |
| `EditorLevel` | 2 |
| `EditorViewport` | 1 |
| `Export` | 3 |
| `Import` | 2 |
| `Input` | 3 |
| `Material` | 15 |
| `PIE` | 3 |
| `RuntimeInspector` | 18 |
| `Utility` | 5 |
| `Widget` | 11 |
| `WidgetPreview` | 1 |
| `Workflow` | 6 |

## TCP Commands

| Name | Category | Scope | Mutating | Dry-run | Async | Timeout |
|---|---|---|---:|---:|---:|---:|
| `ping` | `Utility` | `read` | false | false | false | 0 |
| `list_commands` | `Utility` | `read` | false | false | false | 0 |
| `server_status` | `Utility` | `read` | false | false | false | 0 |
| `editor_identity` | `Utility` | `read` | false | false | false | 0 |
| `command_manifest_export` | `Utility` | `read` | false | false | false | 30 |
| `project_status` | `Workflow` | `read` | false | false | false | 0 |
| `source_control_status` | `Workflow` | `read` | false | false | false | 30 |
| `source_control_log` | `Workflow` | `read` | false | false | false | 30 |
| `source_control_show` | `Workflow` | `read` | false | false | false | 30 |
| `source_control_diff` | `Workflow` | `read` | false | false | false | 30 |
| `task_submit` | `AsyncJob` | `read` | false | false | false | 0 |
| `task_status` | `AsyncJob` | `read` | false | false | false | 0 |
| `task_result` | `AsyncJob` | `read` | false | false | false | 0 |
| `task_cancel` | `AsyncJob` | `read` | false | false | false | 0 |
| `task_events` | `AsyncJob` | `read` | false | false | false | 0 |
| `task_events_wait` | `AsyncJob` | `read` | false | false | false | 30 |
| `export_widget` | `Export` | `read` | false | false | false | 60 |
| `export_blueprint` | `Export` | `read` | false | false | false | 60 |
| `list_supported_types` | `Export` | `read` | false | false | false | 0 |
| `editor_world_info` | `Editor` | `read` | false | false | false | 0 |
| `runtime_world_info` | `RuntimeInspector` | `read` | false | false | false | 30 |
| `runtime_player_list` | `RuntimeInspector` | `read` | false | false | false | 30 |
| `runtime_component_list` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_input_routing` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_replication_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_ability_system_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_ai_perception_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_gameplay_tags_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_commonui_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_asset_streaming_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_async_load_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_game_instance_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_level_travel_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_multiplayer_connection_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_tick_timer_latent_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_scheduler_performance_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `runtime_physics_collision_diagnostics` | `RuntimeInspector` | `read` | false | false | false | 60 |
| `actor_list` | `EditorActor` | `read` | false | false | false | 60 |
| `actor_spawn` | `EditorActor` | `write` | true | true | false | 60 |
| `actor_set_transform` | `EditorActor` | `write` | true | true | false | 60 |
| `actor_delete` | `EditorActor` | `destructive` | true | true | false | 60 |
| `level_open` | `EditorLevel` | `write` | true | true | false | 60 |
| `level_save_current` | `EditorLevel` | `write` | true | true | false | 60 |
| `pie_status` | `PIE` | `read` | false | false | false | 0 |
| `pie_start` | `PIE` | `write` | true | true | false | 30 |
| `pie_stop` | `PIE` | `write` | true | true | false | 30 |
| `editor_console_command` | `Editor` | `destructive` | true | true | false | 60 |
| `editor_log_read` | `Workflow` | `read` | false | false | false | 30 |
| `viewport_capture` | `EditorViewport` | `write` | true | true | false | 30 |
| `create_widget_blueprint` | `Widget` | `write` | true | true | false | 60 |
| `add_widget` | `Widget` | `write` | true | true | false | 60 |
| `remove_widget` | `Widget` | `write` | true | true | false | 60 |
| `move_widget` | `Widget` | `write` | true | true | false | 60 |
| `set_widget_property` | `Widget` | `write` | true | true | false | 60 |
| `set_slot_property` | `Widget` | `write` | true | true | false | 60 |
| `set_canvas_slot_layout` | `Widget` | `write` | true | true | false | 60 |
| `set_widget_properties` | `Widget` | `write` | true | true | false | 60 |
| `compile_and_save` | `Widget` | `write` | true | true | false | 60 |
| `get_widget_tree` | `Widget` | `read` | false | false | false | 60 |
| `list_widget_classes` | `Widget` | `read` | false | false | false | 60 |
| `set_cdo_property` | `CDO` | `write` | true | true | true | 120 |
| `get_cdo_properties` | `CDO` | `read` | false | false | false | 60 |
| `add_cdo_array_element` | `CDOArray` | `write` | true | true | false | 60 |
| `set_cdo_array_element_property` | `CDOArray` | `write` | true | true | false | 60 |
| `remove_cdo_array_element` | `CDOArray` | `write` | true | true | false | 60 |
| `get_cdo_array_length` | `CDOArray` | `read` | false | false | false | 60 |
| `add_event_node` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `add_custom_event` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `add_function_call` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `add_variable_get_node` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `add_variable_set_node` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `add_make_struct_node` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `add_branch_node` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `ensure_function_graph` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `add_call_parent_function` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `connect_pins` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `set_pin_default` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `remove_graph_node` | `BlueprintGraph` | `write` | true | true | false | 60 |
| `get_graph` | `BlueprintGraph` | `read` | false | false | false | 60 |
| `list_graphs` | `BlueprintGraph` | `read` | false | false | false | 60 |
| `add_variable` | `BlueprintVariable` | `write` | true | true | false | 60 |
| `set_variable_default` | `BlueprintVariable` | `write` | true | true | false | 60 |
| `remove_variable` | `BlueprintVariable` | `write` | true | true | false | 60 |
| `get_variables` | `BlueprintVariable` | `read` | false | false | false | 60 |
| `reparent_blueprint` | `BlueprintUtility` | `write` | true | true | false | 60 |
| `create_material` | `Material` | `write` | true | true | false | 60 |
| `set_material_property` | `Material` | `write` | true | true | false | 60 |
| `add_expression` | `Material` | `write` | true | true | false | 60 |
| `set_expression_property` | `Material` | `write` | true | true | false | 60 |
| `connect_expressions` | `Material` | `write` | true | true | false | 60 |
| `connect_to_material_property` | `Material` | `write` | true | true | false | 60 |
| `disconnect_input` | `Material` | `write` | true | true | false | 60 |
| `remove_expression` | `Material` | `write` | true | true | false | 60 |
| `compile_material` | `Material` | `write` | true | true | true | 120 |
| `get_material_graph` | `Material` | `read` | false | false | false | 60 |
| `list_expression_classes` | `Material` | `read` | false | false | false | 60 |
| `create_material_instance` | `Material` | `write` | true | true | false | 60 |
| `set_instance_parameter` | `Material` | `write` | true | true | false | 60 |
| `save_material_instance` | `Material` | `write` | true | true | false | 60 |
| `get_material_instance_info` | `Material` | `read` | false | false | false | 60 |
| `save_data_asset` | `DataAsset` | `write` | true | true | false | 60 |
| `create_asset` | `Asset` | `write` | true | true | false | 60 |
| `set_asset_property` | `Asset` | `write` | true | true | false | 60 |
| `get_asset_properties` | `Asset` | `read` | false | false | false | 60 |
| `asset_exists` | `Asset` | `read` | false | false | false | 30 |
| `scan_asset_paths` | `Asset` | `read` | false | false | false | 60 |
| `asset_search` | `Asset` | `read` | false | false | false | 60 |
| `asset_validate_light` | `Asset` | `read` | false | false | false | 60 |
| `save_asset` | `Asset` | `write` | true | true | false | 60 |
| `rename_asset` | `Asset` | `write` | true | true | true | 120 |
| `get_referencers` | `Asset` | `read` | false | false | false | 60 |
| `get_dependencies` | `Asset` | `read` | false | false | false | 60 |
| `delete_asset` | `Asset` | `destructive` | true | true | true | 120 |
| `list_redirectors` | `Asset` | `read` | false | false | false | 60 |
| `fixup_redirectors` | `Asset` | `write` | true | true | true | 120 |
| `add_input_mapping` | `Input` | `write` | true | true | false | 60 |
| `remove_input_mapping` | `Input` | `write` | true | true | false | 60 |
| `get_input_mappings` | `Input` | `read` | false | false | false | 60 |
| `create_anim_blueprint` | `AnimBlueprint` | `write` | true | true | false | 60 |
| `get_anim_blueprint_info` | `AnimBlueprint` | `read` | false | false | false | 60 |
| `import_texture` | `Import` | `write` | true | true | false | 60 |
| `import_font` | `Import` | `write` | true | true | false | 60 |
| `capture_widget_preview` | `WidgetPreview` | `read` | false | false | true | 120 |
| `reload_asset` | `AssetLifecycle` | `read` | false | false | false | 30 |

## Client-Only MCP Tools

- `asset_transfer_execute`
- `asset_transfer_plan`
- `asset_transfer_verify`
- `client_scope_policy`
- `code_transfer_execute`
- `code_transfer_plan`
- `code_transfer_verify`
- `commonai_prompt_get`
- `commonai_prompts_list`
- `commonai_resource_read`
- `commonai_resources_list`
- `editor_call`
- `editors_list`
- `guarded_build_status`
- `mcp_server_metadata_export`
- `native_http_status`
- `native_mcp_probe`
