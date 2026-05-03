#!/usr/bin/env python3
"""Static tests for CommonAIExport MCP descriptors and generated artifacts."""

from __future__ import annotations

import json
import sys

from generate_mcp_artifacts import (
    AI_REFERENCE_PATH,
    CLIENT_ONLY_TOOLS,
    COMMAND_MANIFEST_PATH,
    TOOL_SCHEMAS_PATH,
    WRAPPER_SPEC_PATH,
    WRAPPER_RUNTIME_PATH,
    WRAPPER_STUBS_PATH,
    build_ai_reference_tool_summary,
    build_command_manifest,
    build_wrapper_runtime,
    build_wrapper_spec,
    build_wrapper_stubs,
    build_tool_schemas,
    read_mcp_tool_signatures,
)


SCOPE_RANK = {"read": 0, "write": 1, "destructive": 2}


def _failures() -> list[str]:
    failures: list[str] = []
    manifest = build_command_manifest()
    schemas = build_tool_schemas(manifest)
    wrapper_spec = build_wrapper_spec(manifest, schemas)
    commands = manifest["commands"]
    signatures = read_mcp_tool_signatures()

    seen_names: set[str] = set()
    for command in commands:
        name = command["name"]
        if name in seen_names:
            failures.append(f"duplicate command name: {name}")
        seen_names.add(name)

        scope = command["required_scope"]
        if scope not in SCOPE_RANK:
            failures.append(f"{name}: invalid scope {scope}")
        if command["mutating"] and scope == "read":
            failures.append(f"{name}: mutating command cannot require read scope")
        if scope == "destructive" and not command["supports_dry_run"]:
            failures.append(f"{name}: destructive command must support dry_run")
        if command["supports_dry_run"] and not command["mutating"]:
            failures.append(f"{name}: read-only command should not advertise dry_run")
        if command["async_candidate"] and command["timeout_seconds"] < 120 and scope != "destructive":
            failures.append(f"{name}: async_candidate should be reserved for long or destructive commands")

        schema = schemas["tools"].get(name)
        if not schema:
            failures.append(f"{name}: missing generated tool schema")
            continue
        if name not in signatures:
            failures.append(f"{name}: missing Python MCP wrapper signature")
        if schema.get("additionalProperties") is not False:
            failures.append(f"{name}: generated schema must be strict with additionalProperties=false")
        if schema.get("properties") != signatures.get(name, {}).get("properties"):
            failures.append(f"{name}: generated schema properties do not match Python wrapper signature")
        if schema.get("required") != signatures.get(name, {}).get("required"):
            failures.append(f"{name}: generated schema required list does not match Python wrapper signature")
        annotations = schema.get("annotations", {})
        if annotations.get("readOnlyHint") != (not command["mutating"]):
            failures.append(f"{name}: readOnlyHint does not match mutating flag")
        if annotations.get("destructiveHint") != (scope == "destructive"):
            failures.append(f"{name}: destructiveHint does not match scope")
        commonai = schema.get("x-commonai", {})
        if commonai.get("required_scope") != scope:
            failures.append(f"{name}: schema required_scope mismatch")
        wrapper_item = wrapper_spec.get("tools", {}).get(name, {})
        if not wrapper_item.get("wrapper_present"):
            failures.append(f"{name}: missing Python MCP wrapper metadata")
        if not wrapper_item.get("calls_expected_tcp_command"):
            failures.append(f"{name}: Python MCP wrapper does not call the matching TCP command")

    for tool_name in CLIENT_ONLY_TOOLS:
        schema = schemas["tools"].get(tool_name)
        if not schema:
            failures.append(f"{tool_name}: missing client-only generated schema")
            continue
        if tool_name not in signatures:
            failures.append(f"{tool_name}: missing Python MCP wrapper signature")
        if schema.get("x-commonai", {}).get("source") != "python_client_only":
            failures.append(f"{tool_name}: client-only schema source mismatch")
        if schema.get("additionalProperties") is not False:
            failures.append(f"{tool_name}: generated schema must be strict with additionalProperties=false")

    if COMMAND_MANIFEST_PATH.exists():
        generated_manifest = json.loads(COMMAND_MANIFEST_PATH.read_text(encoding="utf-8"))
        generated_names = [command["name"] for command in generated_manifest.get("commands", [])]
        current_names = [command["name"] for command in commands]
        if generated_names != current_names:
            failures.append("generated command manifest is stale")
    else:
        failures.append(f"missing generated manifest: {COMMAND_MANIFEST_PATH}")

    if TOOL_SCHEMAS_PATH.exists():
        generated_schemas = json.loads(TOOL_SCHEMAS_PATH.read_text(encoding="utf-8"))
        if generated_schemas.get("tool_count") != len(commands) + len(CLIENT_ONLY_TOOLS):
            failures.append("generated tool schema count mismatch")
        broad_tools = [
            name
            for name, schema in generated_schemas.get("tools", {}).items()
            if schema.get("additionalProperties") is not False
        ]
        if broad_tools:
            failures.append("generated tool schemas are not strict: " + ", ".join(sorted(broad_tools)[:10]))
    else:
        failures.append(f"missing generated schemas: {TOOL_SCHEMAS_PATH}")

    conditional_payload_rules = []
    for tool_name, item in wrapper_spec.get("tools", {}).items():
        payload_rules = item.get("wrapper", {}).get("payload_params", {})
        for param_name, rule in payload_rules.items():
            if rule.get("include") == "conditional":
                conditional_payload_rules.append(f"{tool_name}.{param_name}")
    if conditional_payload_rules:
        failures.append("generated wrapper payload rules contain unresolved conditionals: " + ", ".join(sorted(conditional_payload_rules)[:10]))

    move_widget_index_rule = (
        wrapper_spec.get("tools", {})
        .get("move_widget", {})
        .get("wrapper", {})
        .get("payload_params", {})
        .get("index", {})
        .get("include")
    )
    if move_widget_index_rule != "when_ge_zero":
        failures.append("move_widget.index payload rule should be encoded as when_ge_zero")

    if WRAPPER_SPEC_PATH.exists():
        generated_wrapper_spec = json.loads(WRAPPER_SPEC_PATH.read_text(encoding="utf-8"))
        stable_generated = dict(generated_wrapper_spec)
        stable_generated.pop("generated_at_utc", None)
        stable_current = dict(wrapper_spec)
        stable_current.pop("generated_at_utc", None)
        if stable_generated != stable_current:
            failures.append("generated wrapper spec is stale")
        if generated_wrapper_spec.get("wrapper_drift"):
            failures.append("generated wrapper spec reports wrapper drift")
    else:
        failures.append(f"missing generated wrapper spec: {WRAPPER_SPEC_PATH}")

    if WRAPPER_STUBS_PATH.exists():
        expected_stubs = build_wrapper_stubs(manifest, wrapper_spec)
        if WRAPPER_STUBS_PATH.read_text(encoding="utf-8") != expected_stubs:
            failures.append("generated wrapper stubs are stale")
    else:
        failures.append(f"missing generated wrapper stubs: {WRAPPER_STUBS_PATH}")

    if WRAPPER_RUNTIME_PATH.exists():
        expected_runtime = build_wrapper_runtime(manifest, wrapper_spec)
        if WRAPPER_RUNTIME_PATH.read_text(encoding="utf-8") != expected_runtime:
            failures.append("generated wrapper runtime is stale")
        runtime_namespace: dict = {}
        exec(expected_runtime, runtime_namespace)
        generated_wrappers = runtime_namespace.get("GENERATED_TCP_WRAPPERS", {})
        parameterized_wrappers = [
            name for name, spec in generated_wrappers.items() if spec.get("params")
        ]
        if len(parameterized_wrappers) < 20:
            failures.append("generated wrapper runtime did not promote parameterized read-only wrappers")
        for expected_name in ("asset_exists", "get_widget_tree", "task_events"):
            if expected_name not in generated_wrappers:
                failures.append(f"generated wrapper runtime missing {expected_name}")
        for expected_name in (
            "actor_set_transform",
            "actor_spawn",
            "add_cdo_array_element",
            "add_expression",
            "add_input_mapping",
            "compile_material",
            "connect_expressions",
            "connect_to_material_property",
            "create_anim_blueprint",
            "create_asset",
            "create_material",
            "create_material_instance",
            "disconnect_input",
            "fixup_redirectors",
            "import_font",
            "import_texture",
            "level_open",
            "level_save_current",
            "pie_start",
            "pie_stop",
            "remove_input_mapping",
            "rename_asset",
            "remove_cdo_array_element",
            "remove_expression",
            "save_asset",
            "save_data_asset",
            "save_material_instance",
            "set_cdo_array_element_property",
            "set_cdo_property",
            "set_expression_property",
            "set_instance_parameter",
            "set_asset_property",
            "set_material_property",
            "viewport_capture",
        ):
            spec = generated_wrappers.get(expected_name)
            if not spec:
                failures.append(f"generated wrapper runtime missing write-scope {expected_name}")
            elif not spec.get("meta_params"):
                failures.append(f"generated wrapper runtime missing meta params for {expected_name}")
        for expected_name in (
            "actor_delete",
            "delete_asset",
            "editor_console_command",
        ):
            spec = generated_wrappers.get(expected_name)
            if not spec:
                failures.append(f"generated wrapper runtime missing destructive {expected_name}")
            elif spec.get("required_scope") != "destructive" or not spec.get("supports_dry_run"):
                failures.append(f"generated wrapper runtime has wrong destructive metadata for {expected_name}")
            elif not spec.get("meta_params"):
                failures.append(f"generated wrapper runtime missing destructive meta params for {expected_name}")
        build_tcp_call = runtime_namespace.get("build_tcp_call")
        if callable(build_tcp_call):
            asset_call = build_tcp_call("asset_exists", {"asset_path": "/Game/Example"})
            if asset_call.get("params") != {"asset_path": "/Game/Example"}:
                failures.append("generated wrapper runtime failed required parameter payload mapping")
            task_call = build_tcp_call("task_events", {"task_id": "", "after_sequence": 0, "limit": 100})
            if task_call.get("params") != {"limit": 100}:
                failures.append("generated wrapper runtime failed optional parameter omission mapping")
            missing_call = build_tcp_call("asset_exists", {})
            if missing_call.get("success") is not False:
                failures.append("generated wrapper runtime did not reject missing required parameters")
            unexpected_call = build_tcp_call("asset_exists", {"asset_path": "/Game/Example", "extra": True})
            if unexpected_call.get("success") is not False:
                failures.append("generated wrapper runtime did not reject unexpected parameters")
            pie_call = build_tcp_call("pie_start", {"scope": "write", "dry_run": True})
            if pie_call.get("params") is not None or pie_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed write-scope meta-only mapping")
            level_call = build_tcp_call("level_open", {"map_path": "/Game/Maps/L_Main", "scope": "", "dry_run": True})
            if level_call.get("params") != {"map_path": "/Game/Maps/L_Main"} or level_call.get("meta") != {"dry_run": True}:
                failures.append("generated wrapper runtime failed write-scope payload/meta mapping")
            viewport_call = build_tcp_call(
                "viewport_capture",
                {
                    "output_path": "",
                    "show_ui": False,
                    "add_filename_suffix": True,
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if viewport_call.get("params") != {"show_ui": False, "add_filename_suffix": True} or viewport_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed write-scope optional payload mapping")
            create_asset_call = build_tcp_call(
                "create_asset",
                {
                    "package_path": "/Game/Input",
                    "asset_name": "IA_Test",
                    "asset_type": "InputAction",
                    "properties": None,
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if create_asset_call.get("params") != {"package_path": "/Game/Input", "asset_name": "IA_Test", "asset_type": "InputAction"} or create_asset_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed create_asset payload/meta mapping")
            rename_asset_call = build_tcp_call(
                "rename_asset",
                {
                    "asset_path": "/Game/Input/IA_Test",
                    "new_package_path": "",
                    "new_asset_name": "IA_TestRenamed",
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if rename_asset_call.get("params") != {"asset_path": "/Game/Input/IA_Test", "new_asset_name": "IA_TestRenamed"} or rename_asset_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed rename_asset optional payload/meta mapping")
            fixup_call = build_tcp_call(
                "fixup_redirectors",
                {
                    "folder_path": "/Game/Input",
                    "recursive": False,
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if fixup_call.get("params") != {"folder_path": "/Game/Input", "recursive": False} or fixup_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed fixup_redirectors payload/meta mapping")
            input_mapping_call = build_tcp_call(
                "add_input_mapping",
                {
                    "asset_path": "/Game/Input/IMC_Default",
                    "input_action_path": "/Game/Input/IA_Test",
                    "key": "SpaceBar",
                    "triggers": None,
                    "modifiers": None,
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if input_mapping_call.get("params") != {"asset_path": "/Game/Input/IMC_Default", "input_action_path": "/Game/Input/IA_Test", "key": "SpaceBar"} or input_mapping_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed add_input_mapping optional payload/meta mapping")
            remove_mapping_call = build_tcp_call(
                "remove_input_mapping",
                {
                    "asset_path": "/Game/Input/IMC_Default",
                    "mapping_index": 0,
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if remove_mapping_call.get("params") != {"asset_path": "/Game/Input/IMC_Default", "mapping_index": 0} or remove_mapping_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed remove_input_mapping payload/meta mapping")
            import_texture_call = build_tcp_call(
                "import_texture",
                {
                    "source_path": "D:/Temp/T_Test.png",
                    "package_path": "/Game/UI",
                    "asset_name": "",
                    "compression": "UserInterface2D",
                    "srgb": True,
                    "mip_gen": "NoMipmaps",
                    "lod_group": "UI",
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if import_texture_call.get("params") != {"source_path": "D:/Temp/T_Test.png", "package_path": "/Game/UI"} or import_texture_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed import_texture default payload/meta mapping")
            import_font_call = build_tcp_call(
                "import_font",
                {
                    "package_path": "/Game/UI/Fonts",
                    "font_name": "Inter",
                    "faces": [{"source_path": "D:/Temp/Inter-Regular.ttf", "name": "Regular"}],
                    "hinting": "AutoLight",
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if import_font_call.get("params") != {"package_path": "/Game/UI/Fonts", "font_name": "Inter", "faces": [{"source_path": "D:/Temp/Inter-Regular.ttf", "name": "Regular"}], "hinting": "AutoLight"} or import_font_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed import_font payload/meta mapping")
            anim_blueprint_call = build_tcp_call(
                "create_anim_blueprint",
                {
                    "package_path": "/Game/Characters/Animations",
                    "asset_name": "ABP_Test",
                    "skeleton_path": "/Game/Characters/SK_Test",
                    "parent_class": "AnimInstance",
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if anim_blueprint_call.get("params") != {"package_path": "/Game/Characters/Animations", "asset_name": "ABP_Test", "skeleton_path": "/Game/Characters/SK_Test"} or anim_blueprint_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed create_anim_blueprint default payload/meta mapping")
            set_cdo_call = build_tcp_call(
                "set_cdo_property",
                {
                    "asset_path": "/Game/UI/W_Test",
                    "property_name": "bIsFocusable",
                    "value": "true",
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if set_cdo_call.get("params") != {"asset_path": "/Game/UI/W_Test", "property_name": "bIsFocusable", "value": "true"} or set_cdo_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed set_cdo_property payload/meta mapping")
            add_cdo_array_call = build_tcp_call(
                "add_cdo_array_element",
                {
                    "asset_path": "/Game/UI/W_Test",
                    "array_name": "PreregisteredTabInfoArray",
                    "element_values": "{}",
                    "class_name": "",
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if add_cdo_array_call.get("params") != {"asset_path": "/Game/UI/W_Test", "array_name": "PreregisteredTabInfoArray"} or add_cdo_array_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed add_cdo_array_element default payload/meta mapping")
            set_cdo_array_call = build_tcp_call(
                "set_cdo_array_element_property",
                {
                    "asset_path": "/Game/UI/W_Test",
                    "array_name": "PreregisteredTabInfoArray",
                    "index": 0,
                    "property_name": "TabNameID",
                    "value": "Play",
                    "scope": "write",
                    "dry_run": True,
                },
            )
            expected_cdo_array_params = {
                "asset_path": "/Game/UI/W_Test",
                "array_name": "PreregisteredTabInfoArray",
                "index": 0,
                "property_name": "TabNameID",
                "value": "Play",
            }
            if set_cdo_array_call.get("params") != expected_cdo_array_params or set_cdo_array_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed set_cdo_array_element_property payload/meta mapping")
            remove_cdo_array_call = build_tcp_call(
                "remove_cdo_array_element",
                {
                    "asset_path": "/Game/UI/W_Test",
                    "array_name": "PreregisteredTabInfoArray",
                    "index": 0,
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if remove_cdo_array_call.get("params") != {"asset_path": "/Game/UI/W_Test", "array_name": "PreregisteredTabInfoArray", "index": 0} or remove_cdo_array_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed remove_cdo_array_element payload/meta mapping")
            create_material_call = build_tcp_call(
                "create_material",
                {
                    "package_path": "/Game/Materials",
                    "asset_name": "M_Test",
                    "domain": "Surface",
                    "blend_mode": "Opaque",
                    "shading_model": "DefaultLit",
                    "two_sided": False,
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if create_material_call.get("params") != {"package_path": "/Game/Materials", "asset_name": "M_Test"} or create_material_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed create_material default payload/meta mapping")
            add_expression_call = build_tcp_call(
                "add_expression",
                {
                    "asset_path": "/Game/Materials/M_Test",
                    "expression_class": "ScalarParam",
                    "node_name": "Roughness",
                    "pos_x": -200,
                    "pos_y": 0,
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if add_expression_call.get("params") != {"asset_path": "/Game/Materials/M_Test", "expression_class": "ScalarParam", "node_name": "Roughness", "pos_x": -200} or add_expression_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed add_expression optional payload/meta mapping")
            connect_material_call = build_tcp_call(
                "connect_to_material_property",
                {
                    "asset_path": "/Game/Materials/M_Test",
                    "from_node": "Color",
                    "from_output": "",
                    "material_property": "BaseColor",
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if connect_material_call.get("params") != {"asset_path": "/Game/Materials/M_Test", "from_node": "Color", "from_output": "", "material_property": "BaseColor"} or connect_material_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed connect_to_material_property payload/meta mapping")
            material_instance_call = build_tcp_call(
                "create_material_instance",
                {
                    "package_path": "/Game/Materials",
                    "asset_name": "MI_Test",
                    "parent_material_path": "/Game/Materials/M_Test",
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if material_instance_call.get("params") != {"package_path": "/Game/Materials", "asset_name": "MI_Test", "parent_material_path": "/Game/Materials/M_Test"} or material_instance_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed create_material_instance payload/meta mapping")
            set_instance_call = build_tcp_call(
                "set_instance_parameter",
                {
                    "asset_path": "/Game/Materials/MI_Test",
                    "param_name": "Roughness",
                    "param_type": "scalar",
                    "value": "0.5",
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if set_instance_call.get("params") != {"asset_path": "/Game/Materials/MI_Test", "param_name": "Roughness", "param_type": "scalar", "value": "0.5"} or set_instance_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed set_instance_parameter payload/meta mapping")
            compile_material_call = build_tcp_call(
                "compile_material",
                {
                    "asset_path": "/Game/Materials/M_Test",
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if compile_material_call.get("params") != {"asset_path": "/Game/Materials/M_Test"} or compile_material_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed compile_material payload/meta mapping")
            spawn_call = build_tcp_call(
                "actor_spawn",
                {
                    "class_path": "/Script/Engine.StaticMeshActor",
                    "actor_label": "",
                    "location": None,
                    "rotation": {"yaw": 90},
                    "scale": None,
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if spawn_call.get("params") != {"class_path": "/Script/Engine.StaticMeshActor", "rotation": {"yaw": 90}} or spawn_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed actor_spawn optional transform mapping")
            set_transform_call = build_tcp_call(
                "actor_set_transform",
                {
                    "actor_path": "",
                    "actor_label": "BP_TestActor",
                    "actor_name": "",
                    "location": None,
                    "rotation": None,
                    "scale": {"x": 1, "y": 1, "z": 1},
                    "scope": "write",
                    "dry_run": True,
                },
            )
            if set_transform_call.get("params") != {"actor_label": "BP_TestActor", "scale": {"x": 1, "y": 1, "z": 1}} or set_transform_call.get("meta") != {"scope": "write", "dry_run": True}:
                failures.append("generated wrapper runtime failed actor_set_transform optional transform mapping")
            delete_call = build_tcp_call(
                "delete_asset",
                {
                    "asset_path": "/Game/Missing",
                    "force": False,
                    "scope": "destructive",
                    "dry_run": True,
                },
            )
            if delete_call.get("params") != {"asset_path": "/Game/Missing"} or delete_call.get("meta") != {"scope": "destructive", "dry_run": True}:
                failures.append("generated wrapper runtime failed destructive delete_asset payload/meta mapping")
            actor_delete_call = build_tcp_call(
                "actor_delete",
                {
                    "actor_path": "",
                    "actor_label": "BP_TestActor",
                    "actor_name": "",
                    "scope": "destructive",
                    "dry_run": True,
                },
            )
            if actor_delete_call.get("params") != {"actor_label": "BP_TestActor"} or actor_delete_call.get("meta") != {"scope": "destructive", "dry_run": True}:
                failures.append("generated wrapper runtime failed destructive actor_delete payload/meta mapping")
            console_call = build_tcp_call(
                "editor_console_command",
                {
                    "command": "stat fps",
                    "scope": "destructive",
                    "dry_run": True,
                },
            )
            if console_call.get("params") != {"command": "stat fps"} or console_call.get("meta") != {"scope": "destructive", "dry_run": True}:
                failures.append("generated wrapper runtime failed destructive editor_console_command payload/meta mapping")
        else:
            failures.append("generated wrapper runtime missing build_tcp_call")
    else:
        failures.append(f"missing generated wrapper runtime: {WRAPPER_RUNTIME_PATH}")

    if AI_REFERENCE_PATH.exists():
        ai_reference = AI_REFERENCE_PATH.read_text(encoding="utf-8")
        expected_summary = build_ai_reference_tool_summary(manifest, schemas)
        if expected_summary not in ai_reference:
            failures.append("AI_REFERENCE.md generated tool summary is stale")
    else:
        failures.append(f"missing AI_REFERENCE.md: {AI_REFERENCE_PATH}")

    return failures


def main() -> int:
    failures = _failures()
    if failures:
        print("CommonAIExport MCP static tests failed:")
        for failure in failures:
            print(f"  - {failure}")
        return 1

    print("CommonAIExport MCP static tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
