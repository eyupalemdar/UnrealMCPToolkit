#!/usr/bin/env python3
"""Runtime smoke tests for a live CommonAIExport editor instance."""

from __future__ import annotations

import argparse
import json
import os
import socket
import time
import urllib.error
import urllib.request
from pathlib import Path


DEFAULT_TCP_PORT = 55560
MCP_PROTOCOL_VERSION = "2025-06-18"


def _project_root() -> Path:
    env_root = os.environ.get("UE_PROJECT_DIR")
    if env_root:
        return Path(env_root).resolve()
    cwd = Path.cwd().resolve()
    if (cwd / "Intermediate" / "AIExport_port.txt").exists():
        return cwd
    for parent in Path(__file__).resolve().parents:
        if (parent / "Intermediate").exists() and any(parent.glob("*.uproject")):
            return parent
    return Path(__file__).resolve().parents[4]


PROJECT_ROOT = _project_root()


def _read_port(filename: str, default: int = 0) -> int:
    path = PROJECT_ROOT / "Intermediate" / filename
    if not path.exists():
        return default
    try:
        return int(path.read_text(encoding="utf-8").strip())
    except (OSError, ValueError):
        return default


def _send_tcp(port: int, payload: dict, timeout: int = 30) -> dict:
    data = json.dumps(payload).encode("utf-8")
    command_type = str(payload.get("type", "<missing>"))
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            sock.sendall(data)
            chunks: list[bytes] = []
            while True:
                chunk = sock.recv(65536)
                if not chunk:
                    break
                chunks.append(chunk)
                if len(chunk) < 65536:
                    break
    except TimeoutError as exc:
        raise RuntimeError(f"TCP command '{command_type}' timed out after {timeout}s") from exc
    except OSError as exc:
        raise RuntimeError(f"TCP command '{command_type}' failed: {exc}") from exc

    raw = b"".join(chunks).decode("utf-8", errors="replace")
    if not raw.strip():
        raise RuntimeError(f"TCP command '{command_type}' returned an empty response")
    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"TCP command '{command_type}' returned non-JSON response: {raw[:500]}") from exc


def _http_request(port: int, path: str, payload: dict | None = None, headers: dict | None = None, method: str | None = None) -> dict:
    url = f"http://127.0.0.1:{port}{path}"
    request_headers = {"MCP-Protocol-Version": MCP_PROTOCOL_VERSION}
    token = os.environ.get("COMMONAI_MCP_HTTP_TOKEN") or os.environ.get("COMMONAIEXPORT_HTTP_TOKEN")
    if token:
        request_headers["Authorization"] = f"Bearer {token.strip()}"
    if headers:
        request_headers.update(headers)

    data = None
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        request_headers["Content-Type"] = "application/json"

    request = urllib.request.Request(url, data=data, headers=request_headers, method=method or ("POST" if payload is not None else "GET"))
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            body = response.read().decode("utf-8")
            parsed_body: dict | str = {}
            if body:
                try:
                    parsed_body = json.loads(body)
                except json.JSONDecodeError:
                    parsed_body = body
            return {
                "success": True,
                "status": response.status,
                "headers": dict(response.headers.items()),
                "body": parsed_body,
            }
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        parsed = None
        if body:
            try:
                parsed = json.loads(body)
            except json.JSONDecodeError:
                parsed = body
        return {"success": False, "status": exc.code, "headers": dict(exc.headers.items()), "body": parsed}


def _mcp(port: int, request_id: int, method: str, params: dict | None = None, session_id: str = "", protocol_version: str = MCP_PROTOCOL_VERSION) -> dict:
    headers = {"MCP-Protocol-Version": protocol_version}
    if session_id:
        headers["Mcp-Session-Id"] = session_id
    return _http_request(
        port,
        "/mcp",
        {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params or {}},
        headers=headers,
    )


def _assert(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def _tcp_command(tcp_port: int, command_type: str, params: dict | None = None, meta: dict | None = None, timeout: int = 30) -> dict:
    payload = {"type": command_type}
    if params is not None:
        payload["params"] = params
    if meta is not None:
        payload["meta"] = meta
    return _send_tcp(tcp_port, payload, timeout=timeout)


def _assert_tcp_success(response: dict, label: str) -> dict:
    _assert(bool(response.get("success")), f"{label} failed: {response.get('error', response)}")
    data = response.get("data", {})
    return data if isinstance(data, dict) else {}


def _asset_exists(tcp_port: int, asset_path: str) -> bool:
    data = _assert_tcp_success(
        _tcp_command(tcp_port, "asset_exists", {"asset_path": asset_path}, timeout=30),
        f"asset_exists {asset_path}",
    )
    return bool(data.get("exists"))


def _scan_smoke_asset_path(tcp_port: int) -> None:
    _assert_tcp_success(
        _tcp_command(tcp_port, "scan_asset_paths", {"path": "/Game/CommonAIExport/_Smoke", "force_rescan": True}, timeout=60),
        "scan_asset_paths /Game/CommonAIExport/_Smoke",
    )


def _delete_smoke_asset(tcp_port: int, asset_path: str, *, force: bool = False, allow_missing: bool = True) -> dict:
    if allow_missing and not _asset_exists(tcp_port, asset_path):
        return {"success": True, "data": {"asset_path": asset_path, "deleted": False, "missing": True}}

    response = _tcp_command(
        tcp_port,
        "delete_asset",
        {"asset_path": asset_path, "force": force},
        {"scope": "destructive"},
        timeout=60,
    )
    if not response.get("success"):
        return response

    _scan_smoke_asset_path(tcp_port)
    time.sleep(0.1)
    if _asset_exists(tcp_port, asset_path):
        return {"success": False, "error": f"smoke asset still exists after delete: {asset_path}", "data": response.get("data", {})}

    return response


def _cleanup_smoke_asset(tcp_port: int, asset_path: str, *, allow_missing: bool = True) -> dict:
    cleanup = _delete_smoke_asset(tcp_port, asset_path, allow_missing=allow_missing)
    if cleanup.get("success"):
        return cleanup
    if not _asset_exists(tcp_port, asset_path):
        return {"success": True, "data": {"asset_path": asset_path, "deleted": False, "missing_after_failed_delete": True}}
    return _delete_smoke_asset(tcp_port, asset_path, force=True, allow_missing=False)


def _run_mutating_widget_smoke(tcp_port: int) -> dict:
    """Create, mutate, compile, inspect, and delete an isolated smoke WBP."""
    package_path = "/Game/CommonAIExport/_Smoke"
    asset_name = "W_CommonAIExportRuntimeSmoke"
    asset_path = f"{package_path}/{asset_name}"

    cleanup = _cleanup_smoke_asset(tcp_port, asset_path)
    _assert(bool(cleanup.get("success")), f"smoke asset pre-cleanup failed: {cleanup.get('error', cleanup)}")
    created = False
    try:
        create_data = _assert_tcp_success(
            _tcp_command(
                tcp_port,
                "create_widget_blueprint",
                {
                    "package_path": package_path,
                    "asset_name": asset_name,
                    "parent_class": "/Script/UMG.UserWidget",
                },
                timeout=60,
            ),
            "create_widget_blueprint",
        )
        created = True
        smoke_asset_path = str(create_data.get("asset_path") or asset_path)

        _assert_tcp_success(
            _tcp_command(
                tcp_port,
                "add_widget",
                {"asset_path": smoke_asset_path, "widget_class": "TextBlock", "widget_name": "SmokeText", "parent_name": "RootCanvas"},
            ),
            "add_widget SmokeText",
        )
        _assert_tcp_success(
            _tcp_command(
                tcp_port,
                "set_widget_property",
                {
                    "asset_path": smoke_asset_path,
                    "widget_name": "SmokeText",
                    "property_name": "Text",
                    "value": 'NSLOCTEXT("CommonAIExport", "RuntimeSmoke", "Runtime Smoke")',
                },
            ),
            "set_widget_property Text",
        )
        _assert_tcp_success(
            _tcp_command(
                tcp_port,
                "set_widget_property",
                {
                    "asset_path": smoke_asset_path,
                    "widget_name": "SmokeText",
                    "property_name": "Font.Size",
                    "value": "24",
                },
            ),
            "set_widget_property Font.Size",
        )
        _assert_tcp_success(
            _tcp_command(
                tcp_port,
                "set_canvas_slot_layout",
                {
                    "asset_path": smoke_asset_path,
                    "widget_name": "SmokeText",
                    "position_x": 32,
                    "position_y": 24,
                    "size_x": 320,
                    "size_y": 60,
                    "anchor_min_x": 0,
                    "anchor_min_y": 0,
                    "anchor_max_x": 0,
                    "anchor_max_y": 0,
                    "alignment_x": 0,
                    "alignment_y": 0,
                },
            ),
            "set_canvas_slot_layout",
        )
        _assert_tcp_success(
            _tcp_command(
                tcp_port,
                "ensure_function_graph",
                {
                    "asset_path": smoke_asset_path,
                    "function_name": "RuntimeSmokeFunction",
                    "inputs": [{"name": "Enabled", "type": "bool", "default_value": "true"}],
                    "outputs": [{"name": "ReturnValue", "type": "bool", "default_value": "true"}],
                    "entry_node_name": "RuntimeSmokeFunction_Entry",
                    "result_node_name": "RuntimeSmokeFunction_Result",
                },
                timeout=60,
            ),
            "ensure_function_graph RuntimeSmokeFunction",
        )
        graphs_data = _assert_tcp_success(
            _tcp_command(tcp_port, "list_graphs", {"asset_path": smoke_asset_path}, timeout=60),
            "list_graphs",
        )
        _assert("RuntimeSmokeFunction" in json.dumps(graphs_data, ensure_ascii=False), "mutating graph smoke missing RuntimeSmokeFunction")
        _assert_tcp_success(_tcp_command(tcp_port, "compile_and_save", {"asset_path": smoke_asset_path}, timeout=60), "compile_and_save")
        tree_data = _assert_tcp_success(_tcp_command(tcp_port, "get_widget_tree", {"asset_path": smoke_asset_path}, timeout=60), "get_widget_tree")
        tree_text = json.dumps(tree_data, ensure_ascii=False)
        _assert("RootCanvas" in tree_text and "SmokeText" in tree_text, "mutating widget smoke tree missing expected widgets")
        return {
            "success": True,
            "asset_path": smoke_asset_path,
            "created": True,
            "graph_checked": True,
            "tree_checked": True,
        }
    finally:
        if created:
            cleanup = _cleanup_smoke_asset(tcp_port, asset_path, allow_missing=False)
            _assert(bool(cleanup.get("success")), f"smoke asset cleanup failed: {cleanup.get('error', cleanup)}")


def _run_mutating_material_smoke(tcp_port: int) -> dict:
    """Create, mutate, inspect, and delete an isolated smoke Material."""
    package_path = "/Game/CommonAIExport/_Smoke"
    asset_name = "M_CommonAIExportRuntimeSmoke"
    asset_path = f"{package_path}/{asset_name}"

    cleanup = _cleanup_smoke_asset(tcp_port, asset_path)
    _assert(bool(cleanup.get("success")), f"smoke material pre-cleanup failed: {cleanup.get('error', cleanup)}")
    created = False
    try:
        create_data = _assert_tcp_success(
            _tcp_command(
                tcp_port,
                "create_material",
                {
                    "package_path": package_path,
                    "asset_name": asset_name,
                    "domain": "Surface",
                    "blend_mode": "Opaque",
                    "shading_model": "DefaultLit",
                    "two_sided": True,
                },
                timeout=60,
            ),
            "create_material",
        )
        created = True
        smoke_asset_path = str(create_data.get("asset_path") or asset_path)

        _assert_tcp_success(
            _tcp_command(
                tcp_port,
                "add_expression",
                {
                    "asset_path": smoke_asset_path,
                    "expression_class": "Constant3",
                    "node_name": "SmokeColor",
                    "pos_x": -320,
                    "pos_y": 0,
                },
                timeout=60,
            ),
            "add_expression SmokeColor",
        )
        _assert_tcp_success(
            _tcp_command(
                tcp_port,
                "set_expression_property",
                {
                    "asset_path": smoke_asset_path,
                    "node_name": "SmokeColor",
                    "property_name": "Constant",
                    "value": "(R=0.050000,G=0.350000,B=0.900000,A=1.000000)",
                },
                timeout=60,
            ),
            "set_expression_property SmokeColor.Constant",
        )
        _assert_tcp_success(_tcp_command(tcp_port, "compile_material", {"asset_path": smoke_asset_path}, timeout=120), "compile_material")
        graph_data = _assert_tcp_success(_tcp_command(tcp_port, "get_material_graph", {"asset_path": smoke_asset_path}, timeout=60), "get_material_graph")
        graph_text = json.dumps(graph_data, ensure_ascii=False)
        _assert("SmokeColor" in graph_text and "MaterialExpressionConstant3Vector" in graph_text, "material smoke graph missing expected expression")
        return {
            "success": True,
            "asset_path": smoke_asset_path,
            "created": True,
            "graph_checked": True,
        }
    finally:
        if created:
            cleanup = _cleanup_smoke_asset(tcp_port, asset_path, allow_missing=False)
            _assert(bool(cleanup.get("success")), f"smoke material cleanup failed: {cleanup.get('error', cleanup)}")


def _run_mutating_asset_smoke(tcp_port: int) -> dict:
    """Create, save, inspect, and delete an isolated generic asset."""
    package_path = "/Game/CommonAIExport/_Smoke"
    asset_name = "IA_CommonAIExportRuntimeSmoke"
    asset_path = f"{package_path}/{asset_name}"

    cleanup = _cleanup_smoke_asset(tcp_port, asset_path)
    _assert(bool(cleanup.get("success")), f"smoke generic asset pre-cleanup failed: {cleanup.get('error', cleanup)}")
    created = False
    try:
        create_data = _assert_tcp_success(
            _tcp_command(
                tcp_port,
                "create_asset",
                {
                    "package_path": package_path,
                    "asset_name": asset_name,
                    "asset_type": "InputAction",
                },
                timeout=60,
            ),
            "create_asset InputAction",
        )
        created = True
        smoke_asset_path = str(create_data.get("asset_path") or asset_path)

        _assert_tcp_success(_tcp_command(tcp_port, "save_asset", {"asset_path": smoke_asset_path}, timeout=60), "save_asset InputAction")
        props_data = _assert_tcp_success(
            _tcp_command(tcp_port, "get_asset_properties", {"asset_path": smoke_asset_path}, timeout=60),
            "get_asset_properties InputAction",
        )
        _assert("InputAction" in json.dumps(props_data, ensure_ascii=False), "generic asset smoke missing InputAction properties")
        return {
            "success": True,
            "asset_path": smoke_asset_path,
            "created": True,
            "properties_checked": True,
        }
    finally:
        if created:
            cleanup = _cleanup_smoke_asset(tcp_port, asset_path, allow_missing=False)
            _assert(bool(cleanup.get("success")), f"smoke generic asset cleanup failed: {cleanup.get('error', cleanup)}")


def run_smoke(mutating_smoke: bool = False) -> dict:
    tcp_port = _read_port("AIExport_port.txt", DEFAULT_TCP_PORT)
    http_port = _read_port("AIExport_http_port.txt", 0)
    _assert(http_port > 0, "native HTTP port file missing")

    ping = _send_tcp(tcp_port, {"type": "ping"})
    _assert(bool(ping.get("success")), "TCP ping failed")
    server_status = _assert_tcp_success(_tcp_command(tcp_port, "server_status"), "server_status")
    expected_tool_count = int(server_status.get("command_count") or 0)
    _assert(expected_tool_count > 0, "server_status did not report command_count")

    health = _http_request(http_port, "/commonai/health")
    _assert(bool(health.get("success") and health.get("body", {}).get("success")), "HTTP health failed")

    initialize = _mcp(
        http_port,
        1,
        "initialize",
        {
            "protocolVersion": MCP_PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": {"name": "CommonAIExport runtime smoke", "version": "1.0"},
        },
    )
    _assert(initialize["success"], "MCP initialize failed")
    session_id = (
        initialize.get("headers", {}).get("Mcp-Session-Id")
        or initialize.get("headers", {}).get("mcp-session-id")
        or initialize.get("body", {}).get("result", {}).get("sessionId")
        or ""
    )
    _assert(bool(session_id), "MCP initialize did not return a session id")

    tool_count = 0
    page_count = 0
    cursor = None
    request_id = 2
    while True:
        params = {"cursor": cursor} if cursor else {}
        page = _mcp(http_port, request_id, "tools/list", params, session_id=session_id)
        _assert(page["success"], "MCP tools/list page failed")
        result = page["body"]["result"]
        tool_count += len(result.get("tools", []))
        page_count += 1
        cursor = result.get("nextCursor")
        if not cursor:
            break
        request_id += 1
    _assert(tool_count == expected_tool_count, f"unexpected native MCP tool count: {tool_count} != {expected_tool_count}")

    bad_protocol = _mcp(http_port, 90, "tools/list", {}, session_id=session_id, protocol_version="1900-01-01")
    _assert(bad_protocol.get("body", {}).get("error", {}).get("code") == -32002, "unsupported protocol did not return -32002")

    dry_delete = _send_tcp(
        tcp_port,
        {
            "type": "actor_delete",
            "meta": {"scope": "destructive", "dry_run": True},
            "params": {"actor_name": "CommonAIExport_RuntimeSmoke_NoActor"},
        },
    )
    _assert(bool(dry_delete.get("success") and dry_delete.get("data", {}).get("dry_run")), "destructive dry-run failed")

    no_scope_delete = _send_tcp(tcp_port, {"type": "delete_asset", "params": {"asset_path": "/Game/CommonAIExport/RuntimeSmokeMissing"}})
    _assert(not no_scope_delete.get("success") and "destructive" in no_scope_delete.get("error", ""), "destructive scope gate failed")

    runtime_diagnostics = _assert_tcp_success(
        _tcp_command(tcp_port, "runtime_diagnostics", {"world": "auto", "component_limit": 5}),
        "runtime_diagnostics",
    )
    _assert(runtime_diagnostics.get("world_available") is True, "runtime diagnostics did not report an available world")
    _assert(isinstance(runtime_diagnostics.get("pie"), dict), "runtime diagnostics missing PIE state")
    _assert(isinstance(runtime_diagnostics.get("players"), dict), "runtime diagnostics missing player summary")

    input_routing = _assert_tcp_success(
        _tcp_command(tcp_port, "runtime_input_routing", {"world": "auto"}),
        "runtime_input_routing",
    )
    _assert(input_routing.get("world_available") is True, "runtime input routing did not report an available world")
    _assert(isinstance(input_routing.get("pie"), dict), "runtime input routing missing PIE state")
    _assert(isinstance(input_routing.get("controllers"), list), "runtime input routing missing controller array")
    _assert(isinstance(input_routing.get("local_players"), list), "runtime input routing missing local player array")

    replication_diagnostics = _assert_tcp_success(
        _tcp_command(
            tcp_port,
            "runtime_replication_diagnostics",
            {"world": "auto", "actor_limit": 10, "component_limit": 5},
        ),
        "runtime_replication_diagnostics",
    )
    _assert(replication_diagnostics.get("world_available") is True, "runtime replication diagnostics did not report an available world")
    _assert(isinstance(replication_diagnostics.get("world"), dict), "runtime replication diagnostics missing world summary")
    _assert(isinstance(replication_diagnostics.get("actors"), list), "runtime replication diagnostics missing actor array")
    if replication_diagnostics.get("actors"):
        first_actor = replication_diagnostics["actors"][0]
        _assert(isinstance(first_actor, dict) and isinstance(first_actor.get("replication"), dict), "runtime replication diagnostics missing actor replication object")

    ability_system_diagnostics = _assert_tcp_success(
        _tcp_command(
            tcp_port,
            "runtime_ability_system_diagnostics",
            {"world": "auto", "actor_limit": 10, "ability_limit": 5, "effect_limit": 5, "attribute_limit": 5},
        ),
        "runtime_ability_system_diagnostics",
    )
    _assert(ability_system_diagnostics.get("world_available") is True, "runtime ability system diagnostics did not report an available world")
    _assert(isinstance(ability_system_diagnostics.get("world"), dict), "runtime ability system diagnostics missing world summary")
    _assert(isinstance(ability_system_diagnostics.get("actors"), list), "runtime ability system diagnostics missing actor array")
    _assert(isinstance(ability_system_diagnostics.get("ability_system_component_count"), int), "runtime ability system diagnostics missing component count")
    if ability_system_diagnostics.get("actors"):
        first_actor = ability_system_diagnostics["actors"][0]
        _assert(isinstance(first_actor, dict) and isinstance(first_actor.get("ability_system_components"), list), "runtime ability system diagnostics missing component array")

    ai_perception_diagnostics = _assert_tcp_success(
        _tcp_command(
            tcp_port,
            "runtime_ai_perception_diagnostics",
            {"world": "auto", "listener_limit": 10, "target_limit": 5, "stimulus_limit": 5},
        ),
        "runtime_ai_perception_diagnostics",
    )
    _assert(ai_perception_diagnostics.get("world_available") is True, "runtime AI perception diagnostics did not report an available world")
    _assert(isinstance(ai_perception_diagnostics.get("world"), dict), "runtime AI perception diagnostics missing world summary")
    _assert(isinstance(ai_perception_diagnostics.get("listeners"), list), "runtime AI perception diagnostics missing listener array")
    _assert(isinstance(ai_perception_diagnostics.get("matched_listener_count"), int), "runtime AI perception diagnostics missing listener count")
    if ai_perception_diagnostics.get("listeners"):
        first_listener = ai_perception_diagnostics["listeners"][0]
        _assert(isinstance(first_listener, dict) and isinstance(first_listener.get("targets"), list), "runtime AI perception diagnostics missing target array")

    commonui_diagnostics = _assert_tcp_success(
        _tcp_command(
            tcp_port,
            "runtime_commonui_diagnostics",
            {"world": "auto", "local_player_limit": 4, "widget_limit": 5, "container_limit": 5, "binding_limit": 5},
        ),
        "runtime_commonui_diagnostics",
    )
    _assert(commonui_diagnostics.get("world_available") is True, "runtime CommonUI diagnostics did not report an available world")
    _assert(isinstance(commonui_diagnostics.get("world"), dict), "runtime CommonUI diagnostics missing world summary")
    _assert(isinstance(commonui_diagnostics.get("local_players"), list), "runtime CommonUI diagnostics missing local player array")
    _assert(isinstance(commonui_diagnostics.get("matched_activatable_widget_count"), int), "runtime CommonUI diagnostics missing activatable widget count")
    if commonui_diagnostics.get("local_players"):
        first_local_player = commonui_diagnostics["local_players"][0]
        _assert(isinstance(first_local_player, dict) and isinstance(first_local_player.get("action_router"), dict), "runtime CommonUI diagnostics missing action router object")

    asset_streaming_diagnostics = _assert_tcp_success(
        _tcp_command(
            tcp_port,
            "runtime_asset_streaming_diagnostics",
            {"world": "auto", "include_levels": True, "level_limit": 10},
        ),
        "runtime_asset_streaming_diagnostics",
    )
    _assert(asset_streaming_diagnostics.get("world_available") is True, "runtime asset streaming diagnostics did not report an available world")
    _assert(isinstance(asset_streaming_diagnostics.get("world"), dict), "runtime asset streaming diagnostics missing world summary")
    _assert(isinstance(asset_streaming_diagnostics.get("streaming_manager"), dict), "runtime asset streaming diagnostics missing streaming manager object")
    _assert(isinstance(asset_streaming_diagnostics.get("streaming_level_count"), int), "runtime asset streaming diagnostics missing streaming level count")
    _assert(isinstance(asset_streaming_diagnostics.get("streaming_levels"), list), "runtime asset streaming diagnostics missing streaming level array")

    game_instance_diagnostics = _assert_tcp_success(
        _tcp_command(
            tcp_port,
            "runtime_game_instance_diagnostics",
            {
                "world": "auto",
                "include_local_players": True,
                "include_subsystems": True,
                "include_save_names": False,
                "local_player_limit": 4,
                "subsystem_limit": 20,
            },
        ),
        "runtime_game_instance_diagnostics",
    )
    _assert(game_instance_diagnostics.get("world_available") is True, "runtime game instance diagnostics did not report an available world")
    _assert(isinstance(game_instance_diagnostics.get("game_instance_available"), bool), "runtime game instance diagnostics missing availability flag")
    _assert(isinstance(game_instance_diagnostics.get("save_game_system"), dict), "runtime game instance diagnostics missing save game system object")
    if game_instance_diagnostics.get("game_instance_available"):
        _assert(isinstance(game_instance_diagnostics.get("game_instance"), dict), "runtime game instance diagnostics missing GameInstance object")

    level_travel_diagnostics = _assert_tcp_success(
        _tcp_command(
            tcp_port,
            "runtime_level_travel_diagnostics",
            {
                "world": "auto",
                "include_url_options": True,
                "include_preparing_levels": True,
                "url_option_limit": 20,
                "preparing_level_limit": 20,
            },
        ),
        "runtime_level_travel_diagnostics",
    )
    _assert(level_travel_diagnostics.get("world_available") is True, "runtime level travel diagnostics did not report an available world")
    _assert(isinstance(level_travel_diagnostics.get("travel"), dict), "runtime level travel diagnostics missing travel object")
    _assert(isinstance(level_travel_diagnostics["travel"].get("current_url"), dict), "runtime level travel diagnostics missing current URL object")
    _assert(isinstance(level_travel_diagnostics.get("net_driver"), dict), "runtime level travel diagnostics missing NetDriver object")

    multiplayer_diagnostics = _assert_tcp_success(
        _tcp_command(
            tcp_port,
            "runtime_multiplayer_connection_diagnostics",
            {
                "world": "auto",
                "include_connections": True,
                "include_player_controllers": True,
                "include_world_context": True,
                "include_url_options": True,
                "connection_limit": 8,
                "player_controller_limit": 8,
                "url_option_limit": 20,
            },
        ),
        "runtime_multiplayer_connection_diagnostics",
    )
    _assert(multiplayer_diagnostics.get("world_available") is True, "runtime multiplayer diagnostics did not report an available world")
    _assert(isinstance(multiplayer_diagnostics.get("online_session"), dict), "runtime multiplayer diagnostics missing online session object")
    _assert(isinstance(multiplayer_diagnostics.get("net_driver"), dict), "runtime multiplayer diagnostics missing NetDriver object")
    _assert(isinstance(multiplayer_diagnostics.get("world_context"), dict), "runtime multiplayer diagnostics missing world context object")
    _assert(isinstance(multiplayer_diagnostics.get("player_controllers"), list), "runtime multiplayer diagnostics missing player controller array")

    tick_timer_latent_diagnostics = _assert_tcp_success(
        _tcp_command(
            tcp_port,
            "runtime_tick_timer_latent_diagnostics",
            {
                "world": "auto",
                "include_latent_actions": True,
                "latent_action_limit": 10,
            },
        ),
        "runtime_tick_timer_latent_diagnostics",
    )
    _assert(tick_timer_latent_diagnostics.get("world_available") is True, "runtime tick/timer/latent diagnostics did not report an available world")
    _assert(isinstance(tick_timer_latent_diagnostics.get("time"), dict), "runtime tick/timer/latent diagnostics missing time object")
    _assert(isinstance(tick_timer_latent_diagnostics.get("world_settings"), dict), "runtime tick/timer/latent diagnostics missing world settings object")
    _assert(isinstance(tick_timer_latent_diagnostics.get("timer_manager"), dict), "runtime tick/timer/latent diagnostics missing TimerManager object")
    _assert(isinstance(tick_timer_latent_diagnostics.get("latent_actions"), dict), "runtime tick/timer/latent diagnostics missing latent actions object")

    scheduler_performance_diagnostics = _assert_tcp_success(
        _tcp_command(
            tcp_port,
            "runtime_scheduler_performance_diagnostics",
            {
                "world": "auto",
                "include_actor_ticks": True,
                "include_component_ticks": True,
                "actor_limit": 8,
                "component_limit": 16,
                "hitch_threshold_ms": 33.333,
            },
        ),
        "runtime_scheduler_performance_diagnostics",
    )
    _assert(scheduler_performance_diagnostics.get("world_available") is True, "runtime scheduler/performance diagnostics did not report an available world")
    _assert(isinstance(scheduler_performance_diagnostics.get("app_frame"), dict), "runtime scheduler/performance diagnostics missing app frame object")
    _assert(isinstance(scheduler_performance_diagnostics.get("task_graph"), dict), "runtime scheduler/performance diagnostics missing TaskGraph object")
    _assert(isinstance(scheduler_performance_diagnostics.get("threading"), dict), "runtime scheduler/performance diagnostics missing threading object")
    _assert(isinstance(scheduler_performance_diagnostics.get("tick_summary"), dict), "runtime scheduler/performance diagnostics missing tick summary object")

    task = _send_tcp(tcp_port, {"type": "task_submit", "params": {"command": "ping"}})
    _assert(bool(task.get("success")), "task_submit failed")
    task_id = task["data"]["task_id"]
    task_result = None
    for _ in range(20):
        time.sleep(0.2)
        task_result = _send_tcp(tcp_port, {"type": "task_result", "params": {"task_id": task_id}})
        if task_result.get("data", {}).get("status") in {"completed", "failed", "cancelled"}:
            break
    _assert(bool(task_result and task_result.get("success") and task_result.get("data", {}).get("status") == "completed"), "async ping task did not complete")
    task_events = _assert_tcp_success(
        _tcp_command(tcp_port, "task_events", {"task_id": task_id, "limit": 20}),
        "task_events",
    )
    event_text = json.dumps(task_events, ensure_ascii=False)
    _assert(task_events.get("returned_count", 0) >= 2 and "queued" in event_text and "completed" in event_text, "async task events missing queued/completed lifecycle")
    sse_events = _http_request(http_port, f"/commonai/tasks/events/sse?task_id={task_id}&limit=20")
    sse_body = str(sse_events.get("body", ""))
    sse_headers = {str(key).lower(): str(value) for key, value in sse_events.get("headers", {}).items()}
    sse_content_type = sse_headers.get("content-type", "")
    _assert(bool(sse_events.get("success") and "text/event-stream" in sse_content_type), "task events SSE endpoint failed")
    _assert("event: queued" in sse_body and "event: completed" in sse_body and "data:" in sse_body, "task events SSE body missing lifecycle events")

    delete_session = _http_request(http_port, "/mcp", headers={"Mcp-Session-Id": session_id}, method="DELETE")
    _assert(bool(delete_session.get("success") and delete_session.get("body", {}).get("success")), "MCP session delete failed")
    after_delete = _mcp(http_port, 91, "tools/list", {}, session_id=session_id)
    _assert(after_delete.get("body", {}).get("error", {}).get("code") == -32001, "deleted session did not return -32001")

    mutating_widget_result = None
    mutating_material_result = None
    mutating_asset_result = None
    if mutating_smoke:
        mutating_widget_result = _run_mutating_widget_smoke(tcp_port)
        mutating_material_result = _run_mutating_material_smoke(tcp_port)
        mutating_asset_result = _run_mutating_asset_smoke(tcp_port)

    identity = _send_tcp(tcp_port, {"type": "editor_identity"})
    audit_path = Path(identity.get("data", {}).get("http_audit_log_path", ""))
    audit_lines = []
    if audit_path.exists():
        audit_lines = [line for line in audit_path.read_text(encoding="utf-8").splitlines() if line.strip()]
        for line in audit_lines[-20:]:
            json.loads(line)

    return {
        "success": True,
        "editor_id": ping.get("data", {}).get("editor_id"),
        "tcp_port": tcp_port,
        "http_port": http_port,
        "native_mcp_tool_count": tool_count,
        "expected_native_mcp_tool_count": expected_tool_count,
        "native_mcp_page_count": page_count,
        "unsupported_protocol_code": bad_protocol.get("body", {}).get("error", {}).get("code"),
        "dry_run_scope_gate": True,
        "destructive_scope_gate": True,
        "runtime_diagnostics_checked": True,
        "runtime_input_routing_checked": True,
        "runtime_replication_diagnostics_checked": True,
        "runtime_ability_system_diagnostics_checked": True,
        "runtime_ai_perception_diagnostics_checked": True,
        "runtime_commonui_diagnostics_checked": True,
        "runtime_asset_streaming_diagnostics_checked": True,
        "runtime_game_instance_diagnostics_checked": True,
        "runtime_level_travel_diagnostics_checked": True,
        "runtime_multiplayer_connection_diagnostics_checked": True,
        "runtime_tick_timer_latent_diagnostics_checked": True,
        "runtime_scheduler_performance_diagnostics_checked": True,
        "async_task_status": task_result.get("data", {}).get("status") if task_result else "",
        "async_task_event_count": task_events.get("returned_count", 0),
        "latest_task_event_sequence": task_events.get("latest_sequence", 0),
        "task_events_sse_checked": True,
        "session_delete_success": True,
        "mutating_widget_smoke": mutating_widget_result,
        "mutating_material_smoke": mutating_material_result,
        "mutating_asset_smoke": mutating_asset_result,
        "audit_log_path": str(audit_path),
        "audit_line_count": len(audit_lines),
        "audit_parse_checked": min(len(audit_lines), 20),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="Print compact JSON instead of pretty JSON")
    parser.add_argument(
        "--mutating-smoke",
        action="store_true",
        help="Create, compile, inspect, and delete an isolated /Game/CommonAIExport/_Smoke test WBP.",
    )
    args = parser.parse_args()

    try:
        result = run_smoke(mutating_smoke=args.mutating_smoke)
    except Exception as exc:
        result = {"success": False, "error": str(exc)}
        print(json.dumps(result, indent=None if args.json else 2, ensure_ascii=False))
        return 1

    print(json.dumps(result, indent=None if args.json else 2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
