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
import time
import hashlib
import subprocess
import urllib.request
import urllib.error
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
2. Use Blueprint Graph tools (add_event_node, ensure_function_graph, etc.)
3. compile_and_save - Compile and save
4. get_anim_blueprint_info - Inspect result
""")

DEFAULT_PORT = 55560
TIMEOUT = 60

# Derive project dir from script location: <ProjectDir>/Plugins/CommonAIExport/MCPClient/this_script.py
_SCRIPT_DIR = Path(__file__).resolve().parent
_DEFAULT_PROJECT_DIR = str(_SCRIPT_DIR.parent.parent.parent)
PROJECT_DIR = os.environ.get("UE_PROJECT_DIR", _DEFAULT_PROJECT_DIR)
CLIENT_ONLY_TOOLS = {
    "editors_list",
    "editor_call",
    "asset_transfer_plan",
    "asset_transfer_execute",
    "asset_transfer_verify",
    "code_transfer_plan",
    "code_transfer_execute",
    "code_transfer_verify",
    "commonai_resources_list",
    "commonai_resource_read",
    "commonai_prompts_list",
    "commonai_prompt_get",
    "guarded_build_status",
    "mcp_server_metadata_export",
    "native_http_status",
    "native_mcp_probe",
}


def _registry_dirs() -> list[Path]:
    """Return candidate global CommonAIExport editor registry directories."""
    candidates: list[Path] = []

    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        candidates.append(Path(local_app_data) / "CommonAIExport" / "Editors")

    app_data = os.environ.get("APPDATA")
    if app_data:
        candidates.append(Path(app_data) / "CommonAIExport" / "Editors")

    candidates.append(Path.home() / "AppData" / "Local" / "CommonAIExport" / "Editors")

    seen: set[Path] = set()
    result: list[Path] = []
    for candidate in candidates:
        resolved = candidate.expanduser()
        if resolved not in seen:
            seen.add(resolved)
            result.append(resolved)
    return result


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


def _find_http_port() -> int:
    """Discover native HTTP/MCP server port from port file."""
    port_file = Path(PROJECT_DIR) / "Intermediate" / "AIExport_http_port.txt"
    if port_file.exists():
        try:
            return int(port_file.read_text().strip())
        except (ValueError, IOError):
            pass
    return 0


def _http_headers(extra: dict | None = None) -> dict:
    """Build headers for native localhost HTTP/MCP calls."""
    headers = {"MCP-Protocol-Version": "2025-06-18"}
    token = os.environ.get("COMMONAI_MCP_HTTP_TOKEN") or os.environ.get("COMMONAIEXPORT_HTTP_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token.strip()}"
    if extra:
        headers.update(extra)
    return headers


def _http_json_request(path: str, payload: dict | None = None, timeout: int = 30, headers: dict | None = None, method: str | None = None) -> dict:
    """Call the native localhost HTTP/MCP endpoint."""
    port = _find_http_port()
    if port <= 0:
        return {"success": False, "error": "Native HTTP port file not found", "port": port}
    url = f"http://127.0.0.1:{port}{path}"
    try:
        request_headers = _http_headers(headers)
        if payload is None:
            request = urllib.request.Request(url, headers=request_headers, method=method or "GET")
            with urllib.request.urlopen(request, timeout=timeout) as response:
                body = response.read().decode("utf-8")
                response_headers = dict(response.headers.items())
        else:
            data = json.dumps(payload).encode("utf-8")
            request = urllib.request.Request(
                url,
                data=data,
                headers=_http_headers({"Content-Type": "application/json", **(headers or {})}),
                method=method or "POST",
            )
            with urllib.request.urlopen(request, timeout=timeout) as response:
                body = response.read().decode("utf-8")
                response_headers = dict(response.headers.items())
        parsed = json.loads(body) if body else {}
        return {"success": True, "url": url, "port": port, "response": parsed, "headers": response_headers}
    except urllib.error.HTTPError as exc:
        text = exc.read().decode("utf-8", errors="replace")
        return {"success": False, "url": url, "port": port, "status": exc.code, "error": text}
    except Exception as exc:
        return {"success": False, "url": url, "port": port, "error": str(exc)}


def _build_command(cmd_type: str, params: dict | None = None, meta: dict | None = None) -> dict:
    """Build a CommonAIExport TCP command envelope."""
    command = {"type": cmd_type}
    if params is not None:
        command["params"] = params
    if meta is not None:
        command["meta"] = meta
    return command


def _send_command_to_port(port: int, cmd_type: str, params: dict | None = None, meta: dict | None = None) -> dict:
    """Send a TCP command to a specific CommonAIExport editor port."""
    command = _build_command(cmd_type, params, meta)

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


def _send_command(cmd_type: str, params: dict | None = None, meta: dict | None = None) -> dict:
    """Send a TCP command to the default CommonAIExport server."""
    return _send_command_to_port(_find_port(), cmd_type, params, meta)


def _format_response(result: dict) -> str:
    """Format a TCP response as a readable string."""
    return json.dumps(result, indent=2, ensure_ascii=False)


def _request_meta(scope: str = "", dry_run: bool = False) -> dict | None:
    """Build optional TCP request metadata for scope-gated and dry-run calls."""
    meta: dict = {}
    if scope:
        meta["scope"] = scope
    if dry_run:
        meta["dry_run"] = True
    return meta or None


def _default_editor_entry() -> dict:
    """Build a fallback entry from this MCP server's configured project."""
    project_dir = str(Path(PROJECT_DIR).resolve())
    port = _find_port()
    return {
        "schema_version": 1,
        "editor_id": f"default-{Path(project_dir).name}-{port}",
        "server": "CommonAIExport",
        "host": "127.0.0.1",
        "port": port,
        "project_name": Path(project_dir).name,
        "project_dir": project_dir,
        "project_file": "",
        "registry_file": "",
        "source": "project_port_file",
    }


def _read_registry_entries() -> list[dict]:
    """Read global CommonAIExport editor registry files."""
    entries: list[dict] = []
    seen_keys: set[tuple[str, int]] = set()

    for registry_dir in _registry_dirs():
        if not registry_dir.exists():
            continue

        for registry_file in registry_dir.glob("*.json"):
            try:
                entry = json.loads(registry_file.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue

            try:
                port = int(entry.get("port", 0))
            except (TypeError, ValueError):
                continue
            if port <= 0:
                continue

            editor_id = str(entry.get("editor_id") or f"{entry.get('project_name', 'editor')}-{port}")
            key = (editor_id, port)
            if key in seen_keys:
                continue
            seen_keys.add(key)

            entry["editor_id"] = editor_id
            entry["port"] = port
            entry["registry_file"] = str(registry_file)
            entry["source"] = "global_registry"
            entries.append(entry)

    fallback = _default_editor_entry()
    fallback_port = int(fallback["port"])
    if all(int(entry.get("port", 0)) != fallback_port for entry in entries):
        entries.append(fallback)

    return entries


def _probe_editor(entry: dict) -> dict:
    """Ping an editor registry entry and merge live identity data when possible."""
    port = int(entry["port"])
    started = time.monotonic()
    response = _send_command_to_port(port, "editor_identity")
    elapsed_ms = int((time.monotonic() - started) * 1000)

    probed = dict(entry)
    probed["probe_elapsed_ms"] = elapsed_ms
    if response.get("success"):
        data = response.get("data") or {}
        if isinstance(data, dict):
            probed.update(data)
        probed["alive"] = True
        probed["status"] = "alive"
    else:
        probed["alive"] = False
        probed["status"] = "stale"
        probed["error"] = response.get("error", "editor_identity failed")
    return probed


def _list_editors(include_stale: bool = False) -> list[dict]:
    """List known editor entries, probing each TCP endpoint."""
    editors: list[dict] = []
    seen_ports: set[int] = set()

    for entry in _read_registry_entries():
        port = int(entry["port"])
        if port in seen_ports:
            continue
        seen_ports.add(port)

        probed = _probe_editor(entry)
        if include_stale or probed.get("alive"):
            editors.append(probed)

    return editors


def _resolve_editor(editor_id: str = "", project_dir: str = "", port: int = 0) -> tuple[dict | None, dict | None]:
    """Resolve an editor target by id, project dir, or explicit port."""
    if port > 0:
        entry = {"editor_id": f"port-{port}", "host": "127.0.0.1", "port": port}
        return _probe_editor(entry), None

    normalized_project_dir = str(Path(project_dir).resolve()).lower() if project_dir else ""
    editors = _list_editors(include_stale=False)

    if not editor_id and not normalized_project_dir:
        default_port = _find_port()
        for editor in editors:
            if int(editor.get("port", 0)) == default_port:
                return editor, None
        return _probe_editor(_default_editor_entry()), None

    matches: list[dict] = []
    for editor in editors:
        if editor_id and editor.get("editor_id") == editor_id:
            matches.append(editor)
            continue
        if normalized_project_dir:
            candidate_dir = str(Path(str(editor.get("project_dir", ""))).resolve()).lower()
            if candidate_dir == normalized_project_dir:
                matches.append(editor)

    if not matches:
        return None, {
            "success": False,
            "error": "No live CommonAIExport editor matched the requested target",
            "requested": {
                "editor_id": editor_id,
                "project_dir": project_dir,
                "port": port,
            },
            "live_editors": editors,
        }

    if len(matches) > 1:
        return None, {
            "success": False,
            "error": "Multiple live CommonAIExport editors matched; pass editor_id or port",
            "matches": matches,
        }

    return matches[0], None


def _normalize_package_name(asset_path: str) -> str:
    """Normalize /Game/Path/Asset.Asset to /Game/Path/Asset."""
    package_name = asset_path.strip()
    if "." in package_name:
        package_name = package_name.split(".", 1)[0]
    return package_name


def _package_category(package_name: str) -> str:
    """Classify an Unreal package path for transfer planning."""
    if package_name.startswith("/Game/"):
        return "project"
    if package_name.startswith("/Engine/"):
        return "engine"
    if package_name.startswith("/Script/"):
        return "script"
    if package_name.startswith("/"):
        return "plugin_or_external"
    return "unknown"


def _target_asset_path(source_package: str, root_package: str, target_root: str) -> str:
    """Map the root package to target_root; keep dependencies at original paths."""
    if source_package == root_package and target_root:
        return _normalize_package_name(target_root)
    return source_package


def _call_editor_raw(editor: dict, command: str, params: dict | None = None) -> dict:
    """Call a selected editor without wrapping in editor_call output."""
    return _send_command_to_port(int(editor["port"]), command, params)


def _get_dependency_closure(source_editor: dict, root_package: str, max_depth: int, max_assets: int) -> dict:
    """Collect direct or bounded transitive source dependencies."""
    graph: dict[str, list[str]] = {}
    errors: list[dict] = []
    project_packages: set[str] = {root_package}
    external_refs: set[str] = set()
    queue: list[tuple[str, int]] = [(root_package, 0)]
    visited: set[str] = set()
    scan_was_incomplete = False
    truncated = False

    while queue:
        package_name, depth = queue.pop(0)
        if package_name in visited:
            continue
        visited.add(package_name)

        if len(visited) > max_assets:
            truncated = True
            break

        response = _call_editor_raw(source_editor, "get_dependencies", {"asset_path": package_name})
        if not response.get("success"):
            errors.append({
                "asset_path": package_name,
                "command": "get_dependencies",
                "error": response.get("error", "get_dependencies failed"),
            })
            graph[package_name] = []
            continue

        data = response.get("data") or {}
        deps = [_normalize_package_name(str(dep)) for dep in data.get("dependencies", [])]
        graph[package_name] = deps
        scan_was_incomplete = scan_was_incomplete or bool(data.get("scan_was_incomplete"))

        for dep in deps:
            category = _package_category(dep)
            if category == "project":
                project_packages.add(dep)
                if depth < max_depth and dep not in visited and len(visited) + len(queue) < max_assets:
                    queue.append((dep, depth + 1))
            else:
                external_refs.add(dep)

    return {
        "root": root_package,
        "max_depth": max_depth,
        "max_assets": max_assets,
        "truncated": truncated,
        "scan_was_incomplete": scan_was_incomplete,
        "graph": graph,
        "project_packages": sorted(project_packages),
        "external_refs": sorted(external_refs),
        "errors": errors,
    }


def _get_asset_state(editor: dict, asset_path: str) -> dict:
    """Return target/source asset state from get_asset_properties."""
    response = _call_editor_raw(editor, "asset_exists", {"asset_path": asset_path})
    if response.get("success"):
        data = response.get("data") or {}
        return {
            "asset_path": asset_path,
            "exists": bool(data.get("exists")),
            "class": data.get("class_path", ""),
            "response": data,
        }

    error = str(response.get("error", "get_asset_properties failed"))
    return {
        "asset_path": asset_path,
        "exists": False,
        "class": "",
        "error": error,
        "missing": "not found" in error.lower(),
    }


def _is_within(child: Path, parent: Path) -> bool:
    """Return True if child is inside parent after resolution."""
    try:
        child.resolve().relative_to(parent.resolve())
        return True
    except ValueError:
        return False


def _content_file_candidates(project_dir: str, package_name: str) -> list[Path]:
    """Return known package file candidates for a /Game package."""
    package_name = _normalize_package_name(package_name)
    if not package_name.startswith("/Game/"):
        return []

    relative = package_name.removeprefix("/Game/")
    base = Path(project_dir) / "Content" / Path(relative)
    return [
        base.with_suffix(".uasset"),
        base.with_suffix(".umap"),
        base.with_suffix(".uexp"),
        base.with_suffix(".ubulk"),
        base.with_suffix(".uptnl"),
    ]


def _existing_package_files(project_dir: str, package_name: str) -> list[Path]:
    """Return existing package files for a /Game package."""
    return [path for path in _content_file_candidates(project_dir, package_name) if path.exists()]


def _copy_package_files(
    source_project_dir: str,
    target_project_dir: str,
    source_package: str,
    target_package: str,
    overwrite: bool,
) -> dict:
    """Copy package sidecar files from source Content to target Content."""
    source_content = (Path(source_project_dir) / "Content").resolve()
    target_content = (Path(target_project_dir) / "Content").resolve()
    source_files = _existing_package_files(source_project_dir, source_package)
    if not source_files:
        return {
            "source_asset_path": source_package,
            "target_asset_path": target_package,
            "copied": False,
            "error": "No source package files found on disk",
        }

    target_candidates = _content_file_candidates(target_project_dir, target_package)
    source_candidates = _content_file_candidates(source_project_dir, source_package)
    candidate_map = {
        source_candidate.suffix.lower(): target_candidate
        for source_candidate, target_candidate in zip(source_candidates, target_candidates)
    }

    copied_files: list[dict] = []
    skipped_files: list[dict] = []
    errors: list[str] = []

    for source_file in source_files:
        target_file = candidate_map.get(source_file.suffix.lower())
        if target_file is None:
            skipped_files.append({"source_file": str(source_file), "reason": "unsupported package sidecar extension"})
            continue

        if not _is_within(source_file, source_content):
            errors.append(f"Source file is outside source Content directory: {source_file}")
            continue
        if not _is_within(target_file, target_content):
            errors.append(f"Target file is outside target Content directory: {target_file}")
            continue
        if target_file.exists() and not overwrite:
            errors.append(f"Target file already exists: {target_file}")
            continue

        target_file.parent.mkdir(parents=True, exist_ok=True)
        target_file.write_bytes(source_file.read_bytes())
        copied_files.append({
            "source_file": str(source_file),
            "target_file": str(target_file),
            "size_bytes": target_file.stat().st_size,
        })

    return {
        "source_asset_path": source_package,
        "target_asset_path": target_package,
        "copied": bool(copied_files) and not errors,
        "copied_files": copied_files,
        "skipped_files": skipped_files,
        "errors": errors,
    }


CODE_TRANSFER_ALLOWED_EXTENSIONS = {
    ".h",
    ".hpp",
    ".inl",
    ".cpp",
    ".c",
    ".cc",
    ".cs",
    ".ini",
    ".json",
    ".uplugin",
    ".uproject",
    ".md",
    ".txt",
    ".usf",
    ".ush",
}
CODE_TRANSFER_BLOCKED_ROOTS = {
    ".git",
    ".vs",
    "Binaries",
    "DerivedDataCache",
    "Intermediate",
    "Saved",
}


def _sha256_file(path: Path) -> str:
    """Return SHA-256 hash for a file."""
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _resolve_project_file(project_dir: str, input_path: str) -> tuple[Path | None, str, str]:
    """Resolve a path against a project root and return path, relative path, error."""
    root = Path(project_dir).resolve()
    raw = Path(input_path)
    path = raw.resolve() if raw.is_absolute() else (root / raw).resolve()
    if not _is_within(path, root):
        return None, "", f"Path is outside project directory: {input_path}"
    try:
        relative = path.relative_to(root).as_posix()
    except ValueError:
        return None, "", f"Path is outside project directory: {input_path}"
    if relative.split("/", 1)[0] in CODE_TRANSFER_BLOCKED_ROOTS:
        return None, relative, f"Path is under blocked project directory: {relative}"
    if path.suffix not in CODE_TRANSFER_ALLOWED_EXTENSIONS:
        return None, relative, f"Unsupported code transfer extension: {path.suffix}"
    return path, relative, ""


def _discover_companion_files(project_dir: str, source_files: list[tuple[Path, str]]) -> list[tuple[Path, str]]:
    """Find simple same-stem C++ companion files under Source."""
    root = Path(project_dir).resolve()
    known = {relative for _, relative in source_files}
    additions: list[tuple[Path, str]] = []
    source_root = root / "Source"
    if not source_root.exists():
        return additions

    for path, _relative in source_files:
        if path.suffix not in {".h", ".hpp", ".cpp", ".cc", ".c"}:
            continue
        for extension in (".h", ".hpp", ".cpp", ".cc", ".c", ".inl"):
            for companion in source_root.rglob(path.stem + extension):
                resolved = companion.resolve()
                if not _is_within(resolved, root):
                    continue
                relative = resolved.relative_to(root).as_posix()
                if relative not in known:
                    known.add(relative)
                    additions.append((resolved, relative))
    return additions


def _target_code_path(target_project_dir: str, relative_path: str, target_subdir: str, preserve_relative_paths: bool) -> Path:
    """Map a source relative path into the target project."""
    root = Path(target_project_dir).resolve()
    if preserve_relative_paths:
        return (root / relative_path).resolve()
    subdir = target_subdir.strip().strip("/\\")
    return (root / subdir / Path(relative_path).name).resolve() if subdir else (root / Path(relative_path).name).resolve()


COMMONAI_RESOURCES = {
    "commonai://project/status": {
        "name": "Project Status",
        "description": "Current CommonAIExport project/editor status.",
        "mime_type": "application/json",
    },
    "commonai://commands/manifest": {
        "name": "Command Manifest",
        "description": "Command descriptor manifest from the active editor.",
        "mime_type": "application/json",
    },
    "commonai://editors/list": {
        "name": "Editor Registry",
        "description": "Live CommonAIExport editor registry entries.",
        "mime_type": "application/json",
    },
    "commonai://logs/latest": {
        "name": "Latest Project Log",
        "description": "Recent project log lines from Saved/Logs.",
        "mime_type": "application/json",
    },
    "commonai://audit/http": {
        "name": "HTTP MCP Audit",
        "description": "Recent CommonAIExport native HTTP/MCP audit JSONL events.",
        "mime_type": "application/json",
    },
}

COMMONAI_PROMPTS = {
    "build_fix_test": {
        "description": "Guarded build-fix-test workflow for this project.",
        "template": (
            "Use the guarded build workflow for ProjectOkey. Do not use Live Coding. "
            "If the editor must be relaunched after a build, open it through the existing "
            "VS2022 OkeyGame.sln Local Windows Debugger/F5 session. Check project_status, "
            "guarded_build_status, editor_log_read(filter='Error'), then make the smallest "
            "code change needed and rerun the contract/build checks."
        ),
    },
    "asset_safety_review": {
        "description": "Review an Unreal asset before copying, deleting, or mutating it.",
        "template": (
            "Before mutating {asset_path}, run asset_validate_light, get_dependencies, "
            "get_referencers, and asset_transfer_plan when moving between projects. "
            "Require scope='destructive' for deletes or overwrites. Prefer dry_run=True "
            "until collisions, external dependencies, and referencers are understood."
        ),
    },
    "multi_editor_transfer": {
        "description": "Plan guarded transfer between two open Unreal projects.",
        "template": (
            "Run editors_list, identify source and target editor_id values, then run "
            "asset_transfer_plan or code_transfer_plan. Execute only after reviewing "
            "collisions and required scopes. Verify with asset_transfer_verify or "
            "code_transfer_verify, then run guarded_build_status/project_status as needed."
        ),
    },
    "ui_transfer_validation": {
        "description": "Validate a UI transfer task before touching production Widget Blueprints.",
        "template": (
            "Before mutating production UI assets, read AI_UI_TRANSFER.md, "
            "Docs/AI_UI_Transfer/START_HERE.md, CommonUI architecture docs, and the relevant "
            "component recipe. Ensure a TSpec exists and passes Scripts/ValidateUITSpecs.ps1. "
            "For uncertain components, use a probe WBP under /Game/UI/_AIProbe first, then update "
            "the recipe/matrix before touching production assets."
        ),
    },
    "blueprint_graph_inspection": {
        "description": "Inspect Blueprint graph structure before graph mutation.",
        "template": (
            "Before editing Blueprint graphs, export or inspect with get_graph/list_graphs, identify "
            "existing events/functions/variables, and prefer adding narrowly scoped nodes. For pins, "
            "use set_pin_default only after confirming the target pin type. Compile/save and inspect "
            "graph state after changes."
        ),
    },
    "runtime_debug_triage": {
        "description": "Triage runtime/editor issues with status, logs, PIE, and MCP audit context.",
        "template": (
            "Start with project_status, server_status, pie_status, editor_log_read(filter='Error'), "
            "commonai_resource_read('commonai://audit/http'), and guarded_build_status. Reproduce in PIE "
            "only when needed. Keep changes small, rerun smoke_mcp_runtime.py, and relaunch through VS2022 "
            "Local Windows Debugger/F5 after guarded builds."
        ),
    },
}


def _resource_payload(uri: str) -> dict:
    """Read a CommonAIExport resource-like payload."""
    if uri == "commonai://project/status":
        return _send_command("project_status")
    if uri == "commonai://commands/manifest":
        return _send_command("list_commands")
    if uri == "commonai://editors/list":
        return {
            "success": True,
            "editors": _list_editors(include_stale=True),
            "registry_dirs": [str(path) for path in _registry_dirs()],
        }
    if uri == "commonai://logs/latest":
        return _send_command("editor_log_read", {"max_lines": 200})
    if uri == "commonai://audit/http":
        audit_path = Path(PROJECT_DIR).resolve() / "Saved" / "Logs" / "CommonAIExport_HTTP_Audit.jsonl"
        if not audit_path.exists():
            return {
                "success": True,
                "log_path": str(audit_path),
                "line_count": 0,
                "events": [],
            }
        lines = audit_path.read_text(encoding="utf-8", errors="replace").splitlines()
        events = []
        for line in lines[-200:]:
            if not line.strip():
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError:
                events.append({"parse_error": True, "raw": line})
        return {
            "success": True,
            "log_path": str(audit_path),
            "line_count": len(lines),
            "returned_count": len(events),
            "events": events,
        }
    return {"success": False, "error": f"Unknown CommonAIExport resource URI: {uri}"}


def _prompt_payload(name: str, asset_path: str = "") -> dict:
    """Read a prompt template payload."""
    descriptor = COMMONAI_PROMPTS.get(name)
    if not descriptor:
        return {"success": False, "error": f"Unknown CommonAIExport prompt: {name}"}
    template = descriptor["template"]
    if "{asset_path}" in template:
        template = template.format(asset_path=asset_path or "<asset_path>")
    return {
        "success": True,
        "name": name,
        "description": descriptor["description"],
        "prompt": template,
    }


def _server_metadata() -> dict:
    """Build MCP registry-style metadata for CommonAIExport."""
    return {
        "name": "commonai-export",
        "display_name": "CommonAIExport",
        "description": "Project-local Unreal Editor automation MCP bridge for ProjectOkey.",
        "version": "0.4.0",
        "protocol": {
            "current_transport": "stdio FastMCP wrapper over localhost TCP JSON",
            "native_http_mcp": {
                "discovery_file": "Intermediate/AIExport_http_port.txt",
                "path": "/mcp",
                "protocol_version": "2025-06-18",
                "supports_sessions": True,
                "supports_pagination": True,
                "optional_bearer_token_env": "COMMONAI_MCP_HTTP_TOKEN",
            },
        },
        "project_dir": str(Path(PROJECT_DIR).resolve()),
        "tools": {
            "tcp_manifest": "commonai://commands/manifest",
            "client_only": sorted(CLIENT_ONLY_TOOLS),
            "generated_schemas": "Plugins/CommonAIExport/Resources/Generated/CommonAIExport_ToolSchemas.json",
        },
        "resources": list(COMMONAI_RESOURCES.keys()),
        "prompts": sorted(COMMONAI_PROMPTS.keys()),
        "security": {
            "bind": "127.0.0.1",
            "scopes": ["read", "write", "destructive"],
            "dry_run": True,
            "destructive_requires_explicit_scope": True,
            "allowed_origins_env": "COMMONAI_MCP_HTTP_ALLOWED_ORIGINS",
        },
    }


@mcp.resource("commonai://project/status", mime_type="application/json")
def commonai_project_status_resource() -> str:
    """CommonAIExport project/editor status resource."""
    return _format_response(_resource_payload("commonai://project/status"))


@mcp.resource("commonai://commands/manifest", mime_type="application/json")
def commonai_command_manifest_resource() -> str:
    """CommonAIExport command manifest resource."""
    return _format_response(_resource_payload("commonai://commands/manifest"))


@mcp.resource("commonai://editors/list", mime_type="application/json")
def commonai_editors_resource() -> str:
    """CommonAIExport editor registry resource."""
    return _format_response(_resource_payload("commonai://editors/list"))


@mcp.resource("commonai://logs/latest", mime_type="application/json")
def commonai_latest_log_resource() -> str:
    """CommonAIExport latest project log resource."""
    return _format_response(_resource_payload("commonai://logs/latest"))


@mcp.resource("commonai://audit/http", mime_type="application/json")
def commonai_http_audit_resource() -> str:
    """CommonAIExport native HTTP/MCP audit resource."""
    return _format_response(_resource_payload("commonai://audit/http"))


@mcp.prompt("build_fix_test")
def build_fix_test_prompt() -> str:
    """Guarded ProjectOkey build/fix/test prompt."""
    return _prompt_payload("build_fix_test")["prompt"]


@mcp.prompt("asset_safety_review")
def asset_safety_review_prompt(asset_path: str = "") -> str:
    """Asset safety review prompt."""
    return _prompt_payload("asset_safety_review", asset_path)["prompt"]


@mcp.prompt("multi_editor_transfer")
def multi_editor_transfer_prompt() -> str:
    """Cross-project transfer planning prompt."""
    return _prompt_payload("multi_editor_transfer")["prompt"]


@mcp.prompt("ui_transfer_validation")
def ui_transfer_validation_prompt() -> str:
    """UI transfer validation prompt."""
    return _prompt_payload("ui_transfer_validation")["prompt"]


@mcp.prompt("blueprint_graph_inspection")
def blueprint_graph_inspection_prompt() -> str:
    """Blueprint graph inspection prompt."""
    return _prompt_payload("blueprint_graph_inspection")["prompt"]


@mcp.prompt("runtime_debug_triage")
def runtime_debug_triage_prompt() -> str:
    """Runtime debug triage prompt."""
    return _prompt_payload("runtime_debug_triage")["prompt"]


# =============================================================================
# CONNECTION
# =============================================================================

@mcp.tool()
def ping() -> str:
    """Check if the Unreal Editor TCP server is running and responsive."""
    return _format_response(_send_command("ping"))


@mcp.tool()
def list_commands() -> str:
    """
    List registered CommonAIExport TCP commands.

    Returns:
        JSON with command names, categories, parameter requirements, mutation
        flags, and nominal timeout seconds.
    """
    return _format_response(_send_command("list_commands"))


@mcp.tool()
def server_status() -> str:
    """
    Get CommonAIExport server runtime status.

    Returns:
        JSON with server identity, port, command count, scope model, transport
        summary, and async task counters.
    """
    return _format_response(_send_command("server_status"))


@mcp.tool()
def editor_identity() -> str:
    """
    Get identity metadata for the default Unreal Editor target.

    Returns:
        JSON with editor_id, project path, engine/plugin version, TCP port,
        registry file, transport summary, and capability flags.
    """
    return _format_response(_send_command("editor_identity"))


@mcp.tool()
def command_manifest_export(output_path: str = "") -> str:
    """
    Export the current TCP command manifest to a JSON file under the project.

    Args:
        output_path: Optional absolute or project-relative path. The editor
                     rejects paths outside the project directory.

    Returns:
        JSON with output_path and command_count.
    """
    params: dict = {}
    if output_path:
        params["output_path"] = output_path
    return _format_response(_send_command("command_manifest_export", params))


@mcp.tool()
def project_status() -> str:
    """
    Get project/editor workflow status.

    Returns:
        JSON with editor identity, command count, repo markers, last build log
        presence, log file count, and current editor world/PIE state.
    """
    return _format_response(_send_command("project_status"))


@mcp.tool()
def source_control_status(
    provider: str = "auto",
    repo_path: str = "",
    path: str = "",
    no_limit: bool = False,
) -> str:
    """
    Read source-control status from the editor process.

    Args:
        provider: "auto", "dv"/"diversion", or "git". Auto prefers Diversion
                  when a .diversion folder exists in repo_path.
        repo_path: Optional project-relative or absolute directory under the
                   project. Use "Plugins/CommonAIExport" for the GitHub repo.
        path: Optional repo-relative file or directory path.
        no_limit: For Diversion, request full status output.

    Returns:
        JSON with provider, return_code, stdout, and stderr. This is read-only.
    """
    params = {
        "provider": provider,
        "repo_path": repo_path,
        "path": path,
        "no_limit": no_limit,
    }
    return _format_response(_send_command("source_control_status", params))


@mcp.tool()
def source_control_log(
    provider: str = "auto",
    repo_path: str = "",
    path: str = "",
    limit: int = 20,
    oneline: bool = True,
    since: str = "",
    until: str = "",
    ref: str = "",
) -> str:
    """
    Read recent source-control history from the editor process.

    Args:
        provider: "auto", "dv"/"diversion", or "git".
        repo_path: Optional project-relative repo directory. Use
                   "Plugins/CommonAIExport" for the plugin GitHub repo.
        path: Optional repo-relative file or directory path.
        limit: Maximum commits returned. The TCP server clamps this value.
        oneline: Request compact one-line history when supported.
        since: Optional date or relative date filter.
        until: Optional date or relative date filter.
        ref: Optional Git revision/range. Not supported by Diversion.

    Returns:
        JSON with provider, command arguments, return_code, stdout, and stderr.
    """
    return _format_response(_send_command("source_control_log", {
        "provider": provider,
        "repo_path": repo_path,
        "path": path,
        "limit": limit,
        "oneline": oneline,
        "since": since,
        "until": until,
        "ref": ref,
    }))


@mcp.tool()
def source_control_show(
    provider: str = "auto",
    repo_path: str = "",
    ref: str = "",
    name_status: bool = True,
) -> str:
    """
    Show a source-control commit or current repo revision from the editor process.

    Args:
        provider: "auto", "dv"/"diversion", or "git".
        repo_path: Optional project-relative repo directory.
        ref: Commit/branch/tag reference. Empty means the provider default.
        name_status: Include changed file names and statuses when supported.

    Returns:
        JSON with provider, command arguments, return_code, stdout, and stderr.
    """
    return _format_response(_send_command("source_control_show", {
        "provider": provider,
        "repo_path": repo_path,
        "ref": ref,
        "name_status": name_status,
    }))


@mcp.tool()
def source_control_diff(
    provider: str = "auto",
    repo_path: str = "",
    path: str = "",
    base: str = "",
    compare: str = "",
    name_status: bool = True,
) -> str:
    """
    Read source-control diff output from the editor process.

    Args:
        provider: "auto", "dv"/"diversion", or "git".
        repo_path: Optional project-relative repo directory.
        path: Optional repo-relative file or directory path.
        base: Optional base revision.
        compare: Optional compare revision.
        name_status: Return names/statuses instead of full patch when supported.

    Returns:
        JSON with provider, command arguments, return_code, stdout, and stderr.
    """
    return _format_response(_send_command("source_control_diff", {
        "provider": provider,
        "repo_path": repo_path,
        "path": path,
        "base": base,
        "compare": compare,
        "name_status": name_status,
    }))


@mcp.tool()
def commonai_resources_list() -> str:
    """
    List CommonAIExport resource URIs exposed by this MCP wrapper.

    Returns:
        JSON with resource uri, name, description, and mime_type.
    """
    resources = []
    for uri, descriptor in COMMONAI_RESOURCES.items():
        item = {"uri": uri}
        item.update(descriptor)
        resources.append(item)
    return _format_response({
        "success": True,
        "count": len(resources),
        "resources": resources,
    })


@mcp.tool()
def commonai_resource_read(uri: str) -> str:
    """
    Read a CommonAIExport resource URI.

    Args:
        uri: One of commonai_resources_list().resources[].uri.

    Returns:
        JSON resource payload.
    """
    return _format_response(_resource_payload(uri))


@mcp.tool()
def commonai_prompts_list() -> str:
    """
    List CommonAIExport reusable prompt templates.

    Returns:
        JSON with prompt names and descriptions.
    """
    prompts = [
        {"name": name, "description": descriptor["description"]}
        for name, descriptor in COMMONAI_PROMPTS.items()
    ]
    return _format_response({
        "success": True,
        "count": len(prompts),
        "prompts": prompts,
    })


@mcp.tool()
def commonai_prompt_get(name: str, asset_path: str = "") -> str:
    """
    Get a CommonAIExport prompt template.

    Args:
        name: Prompt name from commonai_prompts_list().
        asset_path: Optional asset path used by asset_safety_review.

    Returns:
        JSON with prompt text.
    """
    return _format_response(_prompt_payload(name, asset_path))


@mcp.tool()
def guarded_build_status(tail_lines: int = 120) -> str:
    """
    Read the last guarded build log status.

    Args:
        tail_lines: Number of recent LastBuild.log lines to return.

    Returns:
        JSON with LastBuild.log presence, success/failure markers, and tail.
    """
    project_root = Path(PROJECT_DIR).resolve()
    log_path = project_root / "Saved" / "Logs" / "LastBuild.log"
    if not log_path.exists():
        return _format_response({
            "success": True,
            "exists": False,
            "log_path": str(log_path),
            "status": "missing",
        })

    text = log_path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    tail = lines[-max(1, min(tail_lines, 2000)):]
    build_success = "[SUCCESS] Build SUCCESSFUL" in text or "Result: Succeeded" in text
    build_failed = "[ERROR] Build FAILED" in text or "Result: Failed" in text
    return _format_response({
        "success": True,
        "exists": True,
        "log_path": str(log_path),
        "status": "success" if build_success and not build_failed else "failed" if build_failed else "unknown",
        "build_success": build_success,
        "build_failed": build_failed,
        "tail_line_count": len(tail),
        "tail": tail,
    })


@mcp.tool()
def mcp_server_metadata_export(output_path: str = "") -> str:
    """
    Export MCP registry-style metadata for CommonAIExport.

    Args:
        output_path: Optional output JSON path under the project directory.

    Returns:
        JSON with metadata and output_path.
    """
    project_root = Path(PROJECT_DIR).resolve()
    target = (project_root / "Saved" / "AIManifests" / "CommonAIExport_server.json").resolve()
    if output_path:
        candidate = Path(output_path)
        if not candidate.is_absolute():
            candidate = project_root / candidate
        target = candidate.resolve()
    if not _is_within(target, project_root):
        return _format_response({
            "success": False,
            "error": "output_path must be under the project directory",
            "output_path": str(target),
        })

    metadata = _server_metadata()
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(metadata, indent=2, ensure_ascii=False), encoding="utf-8")
    return _format_response({
        "success": True,
        "output_path": str(target),
        "metadata": metadata,
    })


@mcp.tool()
def native_http_status() -> str:
    """
    Probe the native C++ HTTP health endpoint.

    Returns:
        JSON with URL, port, and /commonai/health response.
    """
    return _format_response(_http_json_request("/commonai/health"))


@mcp.tool()
def native_mcp_probe() -> str:
    """
    Probe the native C++ MCP JSON-RPC endpoint.

    Runs initialize and tools/list against /mcp and reports tool count.

    Returns:
        JSON with initialize response, tools/list response, and tool count.
    """
    initialize = _http_json_request("/mcp", {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2025-06-18",
            "capabilities": {},
            "clientInfo": {"name": "CommonAIExport MCP Probe", "version": "1.0"},
        },
    })
    session_id = (
        initialize.get("headers", {}).get("Mcp-Session-Id")
        or initialize.get("headers", {}).get("mcp-session-id")
        or initialize.get("response", {}).get("result", {}).get("sessionId")
        or ""
    )

    tools_pages: list[dict] = []
    tool_count = 0
    cursor = None
    request_id = 2
    while True:
        params = {"cursor": cursor} if cursor else {}
        page = _http_json_request(
            "/mcp",
            {
                "jsonrpc": "2.0",
                "id": request_id,
                "method": "tools/list",
                "params": params,
            },
            headers={"Mcp-Session-Id": session_id} if session_id else None,
        )
        tools_pages.append(page)
        if not page.get("success"):
            break
        result = page.get("response", {}).get("result", {})
        tool_count += len(result.get("tools", []))
        cursor = result.get("nextCursor")
        if not cursor:
            break
        request_id += 1
    session_delete = None
    if session_id:
        session_delete = _http_json_request(
            "/mcp",
            headers={"Mcp-Session-Id": session_id},
            method="DELETE",
        )
    delete_success = session_delete is None or bool(session_delete.get("success"))
    return _format_response({
        "success": bool(initialize.get("success") and tools_pages and all(page.get("success") for page in tools_pages) and delete_success),
        "session_id": session_id,
        "initialize": initialize,
        "tools_pages": tools_pages,
        "session_delete": session_delete,
        "page_count": len(tools_pages),
        "tool_count": tool_count,
    })


@mcp.tool()
def editors_list(include_stale: bool = False) -> str:
    """
    List live Unreal Editor instances running CommonAIExport.

    Args:
        include_stale: If True, include stale registry entries whose TCP port
                       no longer responds.

    Returns:
        JSON with discovered editor entries. Use editor_id or port with
        editor_call to route a command to a non-default project.
    """
    editors = _list_editors(include_stale)
    data = {
        "success": True,
        "count": len(editors),
        "alive_count": sum(1 for editor in editors if editor.get("alive")),
        "registry_dirs": [str(path) for path in _registry_dirs()],
        "default_project_dir": str(Path(PROJECT_DIR).resolve()),
        "client_only_tools": sorted(CLIENT_ONLY_TOOLS),
        "editors": editors,
    }
    return _format_response(data)


@mcp.tool()
def editor_call(
    command: str,
    params: dict | None = None,
    editor_id: str = "",
    project_dir: str = "",
    port: int = 0,
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Route any CommonAIExport TCP command to a selected Unreal Editor instance.

    Args:
        command: CommonAIExport TCP command name, e.g. "server_status".
        params: Optional command params object.
        editor_id: Target editor id from editors_list.
        project_dir: Target project directory from editors_list.
        port: Explicit TCP port. Overrides editor_id/project_dir when > 0.
        scope: Optional command scope: "read", "write", or "destructive".
        dry_run: If True, mutating target commands validate but do not execute.

    Returns:
        JSON with target editor metadata and the target command response.
    """
    target, error = _resolve_editor(editor_id, project_dir, port)
    if error:
        return _format_response(error)
    assert target is not None

    response = _send_command_to_port(
        int(target["port"]),
        command,
        params,
        _request_meta(scope, dry_run),
    )
    data = {
        "success": bool(response.get("success")),
        "target": {
            "editor_id": target.get("editor_id", ""),
            "project_name": target.get("project_name", ""),
            "project_dir": target.get("project_dir", ""),
            "port": target.get("port", 0),
            "pid": target.get("pid", 0),
        },
        "command": command,
        "response": response,
    }
    if not response.get("success"):
        data["error"] = response.get("error", "target command failed")
    return _format_response(data)


@mcp.tool()
def editor_world_info() -> str:
    """
    Get current editor world metadata.

    Returns:
        JSON with current world/map, level list, actor count, world type, and
        PIE activity state.
    """
    return _format_response(_send_command("editor_world_info"))


@mcp.tool()
def runtime_world_info(world: str = "auto") -> str:
    """
    Inspect the active runtime or editor world.

    Args:
        world: "auto", "pie"/"runtime"/"play", or "editor". Auto prefers PIE
               when Play-In-Editor is active and otherwise uses the editor world.

    Returns:
        JSON with world type, net mode, time, actor/level/player counts, PIE
        state, and GameInstance/GameMode/GameState class metadata when present.
    """
    return _format_response(_send_command("runtime_world_info", {
        "world": world,
    }))


@mcp.tool()
def runtime_player_list(world: str = "auto") -> str:
    """
    List runtime player controllers, local players, and possessed pawns.

    Args:
        world: "auto", "pie"/"runtime"/"play", or "editor".

    Returns:
        JSON with world metadata plus controller/local-player arrays.
    """
    return _format_response(_send_command("runtime_player_list", {
        "world": world,
    }))


@mcp.tool()
def runtime_component_list(
    world: str = "auto",
    actor_path: str = "",
    actor_label: str = "",
    actor_name: str = "",
    name_filter: str = "",
    actor_class_filter: str = "",
    component_class_filter: str = "",
    limit: int = 500,
) -> str:
    """
    List actor components from the selected runtime or editor world.

    Args:
        world: "auto", "pie"/"runtime"/"play", or "editor".
        actor_path: Optional exact actor UObject path.
        actor_label: Optional exact actor label.
        actor_name: Optional exact actor object name.
        name_filter: Optional substring matched against actor name or label.
        actor_class_filter: Optional substring matched against actor class path.
        component_class_filter: Optional substring matched against component class path.
        limit: Maximum component records returned. The TCP server clamps this.

    Returns:
        JSON with component records, owner metadata, matched count, and
        truncation state.
    """
    params = {
        "world": world,
        "limit": limit,
    }
    if actor_path:
        params["actor_path"] = actor_path
    if actor_label:
        params["actor_label"] = actor_label
    if actor_name:
        params["actor_name"] = actor_name
    if name_filter:
        params["name_filter"] = name_filter
    if actor_class_filter:
        params["actor_class_filter"] = actor_class_filter
    if component_class_filter:
        params["component_class_filter"] = component_class_filter
    return _format_response(_send_command("runtime_component_list", params))


@mcp.tool()
def actor_list(
    name_filter: str = "",
    class_filter: str = "",
    limit: int = 500,
) -> str:
    """
    List actors in the current editor world.

    Args:
        name_filter: Optional substring matched against actor name or label.
        class_filter: Optional substring matched against actor class path.
        limit: Maximum actors returned. The TCP server clamps to a safe range.

    Returns:
        JSON with actor records containing name, label, path, class, transform,
        matched_count, and truncation metadata.
    """
    params: dict = {"limit": limit}
    if name_filter:
        params["name_filter"] = name_filter
    if class_filter:
        params["class_filter"] = class_filter
    return _format_response(_send_command("actor_list", params))


@mcp.tool()
def actor_spawn(
    class_path: str,
    actor_label: str = "",
    location: dict | None = None,
    rotation: dict | None = None,
    scale: dict | None = None,
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Spawn an actor into the current editor world.

    Args:
        class_path: Actor class path, e.g. "/Script/Engine.StaticMeshActor".
        actor_label: Optional editor label for the new actor.
        location: Optional {"x", "y", "z"} world location.
        rotation: Optional {"pitch", "yaw", "roll"} rotation.
        scale: Optional {"x", "y", "z"} actor scale.
        scope: Optional scope. Mutating execution requires write scope when
               request metadata is provided.
        dry_run: If True, validate scope and return without changing the level.

    Returns:
        JSON with the spawned actor record, or a dry-run response.
    """
    params: dict = {"class_path": class_path}
    if actor_label:
        params["actor_label"] = actor_label
    if location is not None:
        params["location"] = location
    if rotation is not None:
        params["rotation"] = rotation
    if scale is not None:
        params["scale"] = scale
    return _format_response(_send_command("actor_spawn", params, _request_meta(scope, dry_run)))


@mcp.tool()
def actor_set_transform(
    actor_path: str = "",
    actor_label: str = "",
    actor_name: str = "",
    location: dict | None = None,
    rotation: dict | None = None,
    scale: dict | None = None,
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Set transform fields on an actor in the current editor world.

    Identify the actor with one of actor_path, actor_label, or actor_name.
    Omitted transform fields keep their current values.

    Args:
        actor_path: Exact UObject path from actor_list.
        actor_label: Exact editor actor label.
        actor_name: Exact internal actor name.
        location: Optional {"x", "y", "z"} world location.
        rotation: Optional {"pitch", "yaw", "roll"} rotation.
        scale: Optional {"x", "y", "z"} actor scale.
        scope: Optional scope. Mutating execution requires write scope when
               request metadata is provided.
        dry_run: If True, validate scope and return without changing the level.

    Returns:
        JSON with the updated actor record, or a dry-run response.
    """
    params: dict = {}
    if actor_path:
        params["actor_path"] = actor_path
    if actor_label:
        params["actor_label"] = actor_label
    if actor_name:
        params["actor_name"] = actor_name
    if location is not None:
        params["location"] = location
    if rotation is not None:
        params["rotation"] = rotation
    if scale is not None:
        params["scale"] = scale
    return _format_response(_send_command("actor_set_transform", params, _request_meta(scope, dry_run)))


@mcp.tool()
def actor_delete(
    actor_path: str = "",
    actor_label: str = "",
    actor_name: str = "",
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Delete an actor from the current editor world.

    This is a destructive command. It requires destructive scope even for
    dry-run validation.

    Args:
        actor_path: Exact UObject path from actor_list.
        actor_label: Exact editor actor label.
        actor_name: Exact internal actor name.
        scope: Must be "destructive" for execution or dry-run.
        dry_run: If True, validate scope and return without deleting.

    Returns:
        JSON with deletion status, or a dry-run/scope error.
    """
    params: dict = {}
    if actor_path:
        params["actor_path"] = actor_path
    if actor_label:
        params["actor_label"] = actor_label
    if actor_name:
        params["actor_name"] = actor_name
    return _format_response(_send_command("actor_delete", params, _request_meta(scope, dry_run)))


@mcp.tool()
def level_open(
    map_path: str,
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Open a map in the editor.

    Args:
        map_path: Long package map path like "/Game/Maps/L_Main" or a filename.
        scope: Optional scope. Mutating execution requires write scope when
               request metadata is provided.
        dry_run: If True, validate scope and return without opening a map.

    Returns:
        JSON with requested map path and resolved filename.
    """
    return _format_response(_send_command("level_open", {
        "map_path": map_path,
    }, _request_meta(scope, dry_run)))


@mcp.tool()
def level_save_current(
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Save the current persistent editor level.

    Args:
        scope: Optional scope. Mutating execution requires write scope when
               request metadata is provided.
        dry_run: If True, validate scope and return without saving.

    Returns:
        JSON with save status, package name, and filename.
    """
    return _format_response(_send_command("level_save_current", meta=_request_meta(scope, dry_run)))


@mcp.tool()
def pie_status() -> str:
    """
    Get Play-In-Editor runtime state.

    Returns:
        JSON with pie_active, simulating, and play_world fields.
    """
    return _format_response(_send_command("pie_status"))


@mcp.tool()
def pie_start(
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Request Play-In-Editor start.

    Args:
        scope: Optional scope. Mutating execution requires write scope when
               request metadata is provided.
        dry_run: If True, validate scope and return without starting PIE.

    Returns:
        JSON with request status.
    """
    return _format_response(_send_command("pie_start", meta=_request_meta(scope, dry_run)))


@mcp.tool()
def pie_stop(
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Request Play-In-Editor stop.

    Args:
        scope: Optional scope. Mutating execution requires write scope when
               request metadata is provided.
        dry_run: If True, validate scope and return without stopping PIE.

    Returns:
        JSON with request status.
    """
    return _format_response(_send_command("pie_stop", meta=_request_meta(scope, dry_run)))


@mcp.tool()
def editor_console_command(
    command: str,
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Execute an Unreal Editor console command.

    This is intentionally destructive-scope gated because console commands can
    change editor state, load maps, quit, or run project-specific exec hooks.

    Args:
        command: Console command text.
        scope: Must be "destructive" for execution or dry-run.
        dry_run: If True, validate scope and return without executing.

    Returns:
        JSON with handled status and captured command output.
    """
    return _format_response(_send_command("editor_console_command", {
        "command": command,
    }, _request_meta(scope, dry_run)))


@mcp.tool()
def editor_log_read(
    max_lines: int = 200,
    filter: str = "",
    log_name: str = "",
) -> str:
    """
    Read the tail of a project log file under Saved/Logs.

    Args:
        max_lines: Maximum recent lines to read before filtering.
        filter: Optional case-insensitive substring filter.
        log_name: Optional log filename under Saved/Logs. Defaults to the
                  current project log.

    Returns:
        JSON with log path, counts, and matching lines.
    """
    params: dict = {"max_lines": max_lines}
    if filter:
        params["filter"] = filter
    if log_name:
        params["log_name"] = log_name
    return _format_response(_send_command("editor_log_read", params))


@mcp.tool()
def viewport_capture(
    output_path: str = "",
    show_ui: bool = True,
    add_filename_suffix: bool = False,
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Request an editor viewport screenshot.

    The command requests a screenshot on the game/editor thread and returns the
    target output path. The image is written by Unreal's screenshot pipeline on
    a subsequent viewport tick.

    Args:
        output_path: Optional absolute or project-relative output PNG path.
        show_ui: Include Slate/UI in the screenshot when supported.
        add_filename_suffix: Let Unreal add a unique suffix.
        scope: Optional scope. Execution requires write scope when metadata is
               provided.
        dry_run: If True, validate scope and return without requesting capture.

    Returns:
        JSON with screenshot_requested and output_path.
    """
    params: dict = {
        "show_ui": show_ui,
        "add_filename_suffix": add_filename_suffix,
    }
    if output_path:
        params["output_path"] = output_path
    return _format_response(_send_command("viewport_capture", params, _request_meta(scope, dry_run)))


@mcp.tool()
def asset_transfer_plan(
    source_asset_path: str,
    source_editor_id: str = "",
    target_editor_id: str = "",
    source_project_dir: str = "",
    target_project_dir: str = "",
    source_port: int = 0,
    target_port: int = 0,
    target_asset_path: str = "",
    max_depth: int = 2,
    max_assets: int = 200,
) -> str:
    """
    Build a read-only cross-project asset transfer plan.

    Args:
        source_asset_path: Source asset package path, e.g. "/Game/UI/W_Menu".
        source_editor_id: Source editor id from editors_list.
        target_editor_id: Target editor id from editors_list.
        source_project_dir: Source project dir if editor_id is omitted.
        target_project_dir: Target project dir if editor_id is omitted.
        source_port: Explicit source TCP port. Overrides source_editor_id.
        target_port: Explicit target TCP port. Overrides target_editor_id.
        target_asset_path: Optional target path for the root asset. Dependency
                           paths are kept unchanged in this first planning pass.
        max_depth: Dependency recursion depth for /Game packages. Default 2.
        max_assets: Safety cap for dependency traversal and target probes.

    Returns:
        JSON dry-run plan with source dependency closure, external references,
        target existing/missing assets, path collisions, warnings, and next
        actions. This tool does not copy, import, save, or mutate either project.
    """
    source, source_error = _resolve_editor(source_editor_id, source_project_dir, source_port)
    if source_error:
        source_error["phase"] = "resolve_source"
        return _format_response(source_error)

    target, target_error = _resolve_editor(target_editor_id, target_project_dir, target_port)
    if target_error:
        target_error["phase"] = "resolve_target"
        return _format_response(target_error)

    assert source is not None
    assert target is not None

    root_package = _normalize_package_name(source_asset_path)
    target_root = _normalize_package_name(target_asset_path) if target_asset_path else root_package
    max_depth = max(0, min(int(max_depth), 8))
    max_assets = max(1, min(int(max_assets), 1000))

    closure = _get_dependency_closure(source, root_package, max_depth, max_assets)
    source_root_state = _get_asset_state(source, root_package)

    target_assets: list[dict] = []
    existing_assets: list[dict] = []
    missing_assets: list[dict] = []
    collisions: list[dict] = []

    for source_package in closure["project_packages"]:
        planned_target = _target_asset_path(source_package, root_package, target_root)
        source_state = _get_asset_state(source, source_package)
        target_state = _get_asset_state(target, planned_target)

        entry = {
            "source_asset_path": source_package,
            "target_asset_path": planned_target,
            "source_exists": source_state.get("exists", False),
            "source_class": source_state.get("class", ""),
            "target_exists": target_state.get("exists", False),
            "target_class": target_state.get("class", ""),
        }
        if source_state.get("error"):
            entry["source_error"] = source_state["error"]
        if target_state.get("error") and not target_state.get("missing"):
            entry["target_error"] = target_state["error"]

        target_assets.append(entry)
        if target_state.get("exists"):
            existing_assets.append(entry)
            collision = {
                "target_asset_path": planned_target,
                "source_asset_path": source_package,
                "reason": "target asset already exists",
                "source_class": source_state.get("class", ""),
                "target_class": target_state.get("class", ""),
            }
            if source_state.get("class") and target_state.get("class") and source_state.get("class") != target_state.get("class"):
                collision["severity"] = "high"
                collision["reason"] = "target asset exists with a different class"
            else:
                collision["severity"] = "medium"
            collisions.append(collision)
        else:
            missing_assets.append(entry)

    warnings: list[str] = []
    if source.get("editor_id") == target.get("editor_id") or int(source.get("port", 0)) == int(target.get("port", -1)):
        warnings.append("Source and target resolve to the same editor; this is a self-check, not a cross-project transfer.")
    if closure["external_refs"]:
        warnings.append("External /Engine, /Script, or plugin references were found. Verify target project has matching modules/plugins.")
    if closure["scan_was_incomplete"]:
        warnings.append("Source asset registry was still scanning; dependency results may have required blocking completion.")
    if closure["truncated"]:
        warnings.append("Dependency traversal hit max_assets and was truncated.")
    if target_asset_path:
        warnings.append("Only the root asset path is remapped in this first pass; dependency path rewrite is not implemented yet.")

    data = {
        "success": True,
        "dry_run": True,
        "mutates_editor_state": False,
        "source": {
            "editor_id": source.get("editor_id", ""),
            "project_name": source.get("project_name", ""),
            "project_dir": source.get("project_dir", ""),
            "port": source.get("port", 0),
            "pid": source.get("pid", 0),
        },
        "target": {
            "editor_id": target.get("editor_id", ""),
            "project_name": target.get("project_name", ""),
            "project_dir": target.get("project_dir", ""),
            "port": target.get("port", 0),
            "pid": target.get("pid", 0),
        },
        "root_asset": {
            "source_asset_path": root_package,
            "target_asset_path": target_root,
            "source_exists": source_root_state.get("exists", False),
            "source_class": source_root_state.get("class", ""),
        },
        "dependency_closure": {
            "max_depth": closure["max_depth"],
            "max_assets": closure["max_assets"],
            "truncated": closure["truncated"],
            "project_asset_count": len(closure["project_packages"]),
            "external_ref_count": len(closure["external_refs"]),
            "graph": closure["graph"],
            "external_refs": closure["external_refs"],
            "errors": closure["errors"],
        },
        "target_analysis": {
            "checked_asset_count": len(target_assets),
            "existing_asset_count": len(existing_assets),
            "missing_asset_count": len(missing_assets),
            "collision_count": len(collisions),
            "assets": target_assets,
            "existing_assets": existing_assets,
            "missing_assets": missing_assets,
            "collisions": collisions,
        },
        "warnings": warnings,
        "next_actions": [
            "Review collisions and missing external module/plugin references.",
            "Run a future asset_transfer_execute only after explicit user approval and destructive/write scope review.",
            "For C++ backed assets, verify target project has the required native classes before copying packages.",
        ],
    }
    return _format_response(data)


@mcp.tool()
def asset_transfer_execute(
    source_asset_path: str,
    source_editor_id: str = "",
    target_editor_id: str = "",
    source_project_dir: str = "",
    target_project_dir: str = "",
    source_port: int = 0,
    target_port: int = 0,
    target_asset_path: str = "",
    max_depth: int = 2,
    max_assets: int = 200,
    scope: str = "",
    dry_run: bool = True,
    overwrite: bool = False,
    allow_same_editor: bool = False,
) -> str:
    """
    Execute a planned package-file asset transfer between two editor projects.

    Args:
        source_asset_path: Source asset package path, e.g. "/Game/UI/W_Menu".
        source_editor_id: Source editor id from editors_list.
        target_editor_id: Target editor id from editors_list.
        source_project_dir: Source project dir if editor_id is omitted.
        target_project_dir: Target project dir if editor_id is omitted.
        source_port: Explicit source TCP port. Overrides source_editor_id.
        target_port: Explicit target TCP port. Overrides target_editor_id.
        target_asset_path: Optional target path for the root asset.
        max_depth: Dependency recursion depth for /Game packages.
        max_assets: Safety cap for dependency traversal and target probes.
        scope: Must be "write" for copy. Must be "destructive" when overwrite=True.
        dry_run: Default True. If True, returns the copy set without writing files.
        overwrite: If True, existing target package files may be replaced.
        allow_same_editor: If False, blocks accidental self-copy execution.

    Returns:
        JSON with copied package files, scan result, verification summary, and
        any skipped/errors. Does not handle C++ source/module migration.
    """
    plan = json.loads(asset_transfer_plan(
        source_asset_path=source_asset_path,
        source_editor_id=source_editor_id,
        target_editor_id=target_editor_id,
        source_project_dir=source_project_dir,
        target_project_dir=target_project_dir,
        source_port=source_port,
        target_port=target_port,
        target_asset_path=target_asset_path,
        max_depth=max_depth,
        max_assets=max_assets,
    ))

    if not plan.get("success"):
        plan["phase"] = "plan"
        return _format_response(plan)

    source = plan["source"]
    target = plan["target"]
    same_editor = (
        source.get("editor_id") == target.get("editor_id")
        or int(source.get("port", 0)) == int(target.get("port", -1))
    )
    if same_editor and not allow_same_editor:
        return _format_response({
            "success": False,
            "error": "Source and target resolve to the same editor. Pass allow_same_editor=True only for controlled self-tests.",
            "plan": plan,
        })

    normalized_scope = scope.lower().strip()
    if not dry_run:
        if overwrite and normalized_scope != "destructive":
            return _format_response({
                "success": False,
                "error": "asset_transfer_execute with overwrite=True requires scope='destructive'",
                "plan": plan,
            })
        if not overwrite and normalized_scope not in {"write", "destructive"}:
            return _format_response({
                "success": False,
                "error": "asset_transfer_execute requires scope='write' for actual copy",
                "plan": plan,
            })

    copy_set = plan["target_analysis"]["assets"]
    would_copy = [
        {
            "source_asset_path": entry["source_asset_path"],
            "target_asset_path": entry["target_asset_path"],
            "source_files": [str(path) for path in _existing_package_files(source["project_dir"], entry["source_asset_path"])],
            "target_candidates": [str(path) for path in _content_file_candidates(target["project_dir"], entry["target_asset_path"])],
        }
        for entry in copy_set
    ]

    if dry_run:
        return _format_response({
            "success": True,
            "dry_run": True,
            "mutates_editor_state": False,
            "would_copy_count": len(would_copy),
            "would_copy": would_copy,
            "collision_count": plan["target_analysis"].get("collision_count", 0),
            "closure_error_count": len(plan["dependency_closure"].get("errors", [])),
            "plan": plan,
            "required_scope_for_execute": "destructive" if overwrite else "write",
        })

    if plan["dependency_closure"].get("errors"):
        return _format_response({
            "success": False,
            "error": "Transfer plan contains dependency errors; refusing execute",
            "plan": plan,
        })

    if plan["target_analysis"].get("collision_count", 0) > 0 and not overwrite:
        return _format_response({
            "success": False,
            "error": "Target collisions exist. Re-run with overwrite=True and scope='destructive' only after explicit review.",
            "plan": plan,
        })

    copy_results: list[dict] = []
    copied_asset_paths: list[str] = []
    errors: list[str] = []

    for entry in copy_set:
        result = _copy_package_files(
            source["project_dir"],
            target["project_dir"],
            entry["source_asset_path"],
            entry["target_asset_path"],
            overwrite=overwrite,
        )
        copy_results.append(result)
        if result.get("copied"):
            copied_asset_paths.append(entry["target_asset_path"])
        for error in result.get("errors", []):
            errors.append(error)
        if result.get("error"):
            errors.append(result["error"])

    target_scan_paths = sorted({
        str(Path(asset_path).parent).replace("\\", "/")
        for asset_path in copied_asset_paths
        if asset_path.startswith("/Game/")
    })
    target_editor, target_error = _resolve_editor(target.get("editor_id", ""), "", int(target.get("port", 0)))
    scan_response: dict = {"success": False, "error": "No assets copied; scan skipped"}
    if target_error:
        scan_response = target_error
    elif target_scan_paths:
        assert target_editor is not None
        scan_response = _call_editor_raw(
            target_editor,
            "scan_asset_paths",
            {"paths": target_scan_paths, "force_rescan": True},
        )

    verify = json.loads(asset_transfer_verify(
        source_asset_path=source_asset_path,
        source_editor_id=source.get("editor_id", ""),
        target_editor_id=target.get("editor_id", ""),
        source_port=int(source.get("port", 0)),
        target_port=int(target.get("port", 0)),
        target_asset_path=target_asset_path,
        max_depth=max_depth,
        max_assets=max_assets,
    ))

    return _format_response({
        "success": not errors and bool(copied_asset_paths),
        "dry_run": False,
        "overwrite": overwrite,
        "copied_asset_count": len(copied_asset_paths),
        "copied_asset_paths": copied_asset_paths,
        "copy_results": copy_results,
        "scan_paths": target_scan_paths,
        "scan_response": scan_response,
        "verify": verify,
        "errors": errors,
        "plan": plan,
    })


@mcp.tool()
def asset_transfer_verify(
    source_asset_path: str,
    source_editor_id: str = "",
    target_editor_id: str = "",
    source_project_dir: str = "",
    target_project_dir: str = "",
    source_port: int = 0,
    target_port: int = 0,
    target_asset_path: str = "",
    max_depth: int = 2,
    max_assets: int = 200,
) -> str:
    """
    Verify target project state for a cross-project asset transfer.

    Args mirror asset_transfer_plan. The tool is read-only and checks whether
    the planned /Game packages exist in the target editor after scan/import.

    Returns:
        JSON with verification pass/fail, missing assets, existing assets, and
        dependency/external-reference warnings.
    """
    plan = json.loads(asset_transfer_plan(
        source_asset_path=source_asset_path,
        source_editor_id=source_editor_id,
        target_editor_id=target_editor_id,
        source_project_dir=source_project_dir,
        target_project_dir=target_project_dir,
        source_port=source_port,
        target_port=target_port,
        target_asset_path=target_asset_path,
        max_depth=max_depth,
        max_assets=max_assets,
    ))
    if not plan.get("success"):
        plan["phase"] = "verify_plan"
        return _format_response(plan)

    missing_assets = plan["target_analysis"].get("missing_assets", [])
    existing_assets = plan["target_analysis"].get("existing_assets", [])
    closure_errors = plan["dependency_closure"].get("errors", [])
    passed = not missing_assets and not closure_errors

    return _format_response({
        "success": True,
        "verified": passed,
        "missing_asset_count": len(missing_assets),
        "existing_asset_count": len(existing_assets),
        "closure_error_count": len(closure_errors),
        "missing_assets": missing_assets,
        "existing_assets": existing_assets,
        "external_refs": plan["dependency_closure"].get("external_refs", []),
        "warnings": plan.get("warnings", []),
        "source": plan.get("source", {}),
        "target": plan.get("target", {}),
        "root_asset": plan.get("root_asset", {}),
    })


@mcp.tool()
def code_transfer_plan(
    source_paths: list[str],
    source_editor_id: str = "",
    target_editor_id: str = "",
    source_project_dir: str = "",
    target_project_dir: str = "",
    source_port: int = 0,
    target_port: int = 0,
    target_subdir: str = "",
    preserve_relative_paths: bool = True,
    include_companions: bool = True,
    max_files: int = 100,
) -> str:
    """
    Build a read-only cross-project code/config file transfer plan.

    Args:
        source_paths: Source project-relative or absolute paths to text/code files.
        source_editor_id: Source editor id from editors_list.
        target_editor_id: Target editor id from editors_list.
        source_project_dir: Source project dir if editor_id is omitted.
        target_project_dir: Target project dir if editor_id is omitted.
        source_port: Explicit source TCP port.
        target_port: Explicit target TCP port.
        target_subdir: Target subdir when preserve_relative_paths=False.
        preserve_relative_paths: Keep project-relative paths by default.
        include_companions: Auto-include same-stem C++ companions under Source.
        max_files: Safety cap.

    Returns:
        JSON read-only plan with source files, target paths, hashes, collisions,
        blocked files, and warnings. No files are copied.
    """
    source, source_error = _resolve_editor(source_editor_id, source_project_dir, source_port)
    if source_error:
        source_error["phase"] = "resolve_source"
        return _format_response(source_error)
    target, target_error = _resolve_editor(target_editor_id, target_project_dir, target_port)
    if target_error:
        target_error["phase"] = "resolve_target"
        return _format_response(target_error)

    assert source is not None
    assert target is not None
    source_root = str(Path(source["project_dir"]).resolve())
    target_root = str(Path(target["project_dir"]).resolve())
    max_files = max(1, min(int(max_files), 1000))

    resolved_files: list[tuple[Path, str]] = []
    blocked: list[dict] = []
    seen: set[str] = set()
    for input_path in source_paths:
        path, relative, error = _resolve_project_file(source_root, input_path)
        if error:
            blocked.append({"input_path": input_path, "relative_path": relative, "error": error})
            continue
        assert path is not None
        if not path.exists() or not path.is_file():
            blocked.append({"input_path": input_path, "relative_path": relative, "error": "Source file not found"})
            continue
        if relative not in seen:
            seen.add(relative)
            resolved_files.append((path, relative))

    if include_companions:
        for path, relative in _discover_companion_files(source_root, resolved_files):
            if relative not in seen:
                seen.add(relative)
                resolved_files.append((path, relative))

    truncated = len(resolved_files) > max_files
    resolved_files = resolved_files[:max_files]

    files: list[dict] = []
    collisions: list[dict] = []
    for source_file, relative in resolved_files:
        target_file = _target_code_path(target_root, relative, target_subdir, preserve_relative_paths)
        entry: dict = {
            "source_file": str(source_file),
            "relative_path": relative,
            "target_file": str(target_file),
            "extension": source_file.suffix,
            "size_bytes": source_file.stat().st_size,
            "sha256": _sha256_file(source_file),
            "target_exists": target_file.exists(),
        }
        if not _is_within(target_file, Path(target_root)):
            entry["blocked"] = True
            entry["error"] = "Target path resolves outside target project"
            blocked.append({"input_path": relative, "target_file": str(target_file), "error": entry["error"]})
        elif target_file.exists():
            entry["target_sha256"] = _sha256_file(target_file) if target_file.is_file() else ""
            severity = "medium" if entry["target_sha256"] == entry["sha256"] else "high"
            collisions.append({
                "relative_path": relative,
                "target_file": str(target_file),
                "severity": severity,
                "reason": "target file already exists with same content" if severity == "medium" else "target file already exists with different content",
            })
        files.append(entry)

    same_editor = (
        source.get("editor_id") == target.get("editor_id")
        or int(source.get("port", 0)) == int(target.get("port", -1))
    )
    warnings: list[str] = []
    if same_editor:
        warnings.append("Source and target resolve to the same editor; this is a self-check, not a cross-project transfer.")
    if truncated:
        warnings.append("Source file list hit max_files and was truncated.")
    if any(file["relative_path"].startswith("Source/") for file in files):
        warnings.append("C++ source transfer changes may require Build.cs/module/API macro review and a guarded rebuild.")

    return _format_response({
        "success": True,
        "dry_run": True,
        "mutates_editor_state": False,
        "source": {
            "editor_id": source.get("editor_id", ""),
            "project_name": source.get("project_name", ""),
            "project_dir": source_root,
            "port": source.get("port", 0),
            "pid": source.get("pid", 0),
        },
        "target": {
            "editor_id": target.get("editor_id", ""),
            "project_name": target.get("project_name", ""),
            "project_dir": target_root,
            "port": target.get("port", 0),
            "pid": target.get("pid", 0),
        },
        "preserve_relative_paths": preserve_relative_paths,
        "target_subdir": target_subdir,
        "include_companions": include_companions,
        "file_count": len(files),
        "collision_count": len(collisions),
        "blocked_count": len(blocked),
        "truncated": truncated,
        "files": files,
        "collisions": collisions,
        "blocked": blocked,
        "warnings": warnings,
    })


@mcp.tool()
def code_transfer_execute(
    source_paths: list[str],
    source_editor_id: str = "",
    target_editor_id: str = "",
    source_project_dir: str = "",
    target_project_dir: str = "",
    source_port: int = 0,
    target_port: int = 0,
    target_subdir: str = "",
    preserve_relative_paths: bool = True,
    include_companions: bool = True,
    max_files: int = 100,
    scope: str = "",
    dry_run: bool = True,
    overwrite: bool = False,
    allow_same_editor: bool = False,
) -> str:
    """
    Execute a planned code/config file transfer between projects.

    Actual copy requires scope="write". Existing target overwrite requires
    scope="destructive". The tool blocks project cache/build directories and
    only allows text/code-like extensions.
    """
    plan = json.loads(code_transfer_plan(
        source_paths=source_paths,
        source_editor_id=source_editor_id,
        target_editor_id=target_editor_id,
        source_project_dir=source_project_dir,
        target_project_dir=target_project_dir,
        source_port=source_port,
        target_port=target_port,
        target_subdir=target_subdir,
        preserve_relative_paths=preserve_relative_paths,
        include_companions=include_companions,
        max_files=max_files,
    ))
    if not plan.get("success"):
        plan["phase"] = "plan"
        return _format_response(plan)

    same_editor = (
        plan["source"].get("editor_id") == plan["target"].get("editor_id")
        or int(plan["source"].get("port", 0)) == int(plan["target"].get("port", -1))
    )
    if same_editor and not allow_same_editor:
        return _format_response({
            "success": False,
            "error": "Source and target resolve to the same editor. Pass allow_same_editor=True only for controlled self-tests.",
            "plan": plan,
        })

    normalized_scope = scope.lower().strip()
    if dry_run:
        return _format_response({
            "success": True,
            "dry_run": True,
            "mutates_editor_state": False,
            "would_copy_count": plan.get("file_count", 0),
            "collision_count": plan.get("collision_count", 0),
            "blocked_count": plan.get("blocked_count", 0),
            "required_scope_for_execute": "destructive" if overwrite else "write",
            "plan": plan,
        })

    if overwrite and normalized_scope != "destructive":
        return _format_response({
            "success": False,
            "error": "code_transfer_execute with overwrite=True requires scope='destructive'",
            "plan": plan,
        })
    if not overwrite and normalized_scope not in {"write", "destructive"}:
        return _format_response({
            "success": False,
            "error": "code_transfer_execute requires scope='write' for actual copy",
            "plan": plan,
        })
    if plan.get("blocked_count", 0) > 0:
        return _format_response({
            "success": False,
            "error": "Plan contains blocked files; refusing execute",
            "plan": plan,
        })
    if plan.get("collision_count", 0) > 0 and not overwrite:
        return _format_response({
            "success": False,
            "error": "Target file collisions exist. Re-run with overwrite=True and scope='destructive' only after review.",
            "plan": plan,
        })

    copied: list[dict] = []
    errors: list[str] = []
    target_root = Path(plan["target"]["project_dir"]).resolve()
    for entry in plan["files"]:
        source_file = Path(entry["source_file"]).resolve()
        target_file = Path(entry["target_file"]).resolve()
        if not _is_within(target_file, target_root):
            errors.append(f"Target file outside target project: {target_file}")
            continue
        if target_file.exists() and not overwrite:
            errors.append(f"Target file already exists: {target_file}")
            continue
        target_file.parent.mkdir(parents=True, exist_ok=True)
        target_file.write_bytes(source_file.read_bytes())
        copied.append({
            "source_file": str(source_file),
            "target_file": str(target_file),
            "size_bytes": target_file.stat().st_size,
            "sha256": _sha256_file(target_file),
        })

    verify = json.loads(code_transfer_verify(
        source_paths=source_paths,
        source_editor_id=plan["source"].get("editor_id", ""),
        target_editor_id=plan["target"].get("editor_id", ""),
        source_port=int(plan["source"].get("port", 0)),
        target_port=int(plan["target"].get("port", 0)),
        target_subdir=target_subdir,
        preserve_relative_paths=preserve_relative_paths,
        include_companions=include_companions,
        max_files=max_files,
    ))

    return _format_response({
        "success": not errors and len(copied) == plan.get("file_count", 0),
        "dry_run": False,
        "overwrite": overwrite,
        "copied_count": len(copied),
        "copied": copied,
        "errors": errors,
        "verify": verify,
        "plan": plan,
    })


@mcp.tool()
def code_transfer_verify(
    source_paths: list[str],
    source_editor_id: str = "",
    target_editor_id: str = "",
    source_project_dir: str = "",
    target_project_dir: str = "",
    source_port: int = 0,
    target_port: int = 0,
    target_subdir: str = "",
    preserve_relative_paths: bool = True,
    include_companions: bool = True,
    max_files: int = 100,
) -> str:
    """
    Verify target code/config files against the source transfer plan.

    Returns hashes for target files and reports missing or content mismatches.
    """
    plan = json.loads(code_transfer_plan(
        source_paths=source_paths,
        source_editor_id=source_editor_id,
        target_editor_id=target_editor_id,
        source_project_dir=source_project_dir,
        target_project_dir=target_project_dir,
        source_port=source_port,
        target_port=target_port,
        target_subdir=target_subdir,
        preserve_relative_paths=preserve_relative_paths,
        include_companions=include_companions,
        max_files=max_files,
    ))
    if not plan.get("success"):
        plan["phase"] = "verify_plan"
        return _format_response(plan)

    missing: list[dict] = []
    mismatched: list[dict] = []
    matched: list[dict] = []
    for entry in plan["files"]:
        target_file = Path(entry["target_file"])
        if not target_file.exists() or not target_file.is_file():
            missing.append(entry)
            continue
        target_hash = _sha256_file(target_file)
        result = {
            "relative_path": entry["relative_path"],
            "source_sha256": entry["sha256"],
            "target_sha256": target_hash,
            "target_file": str(target_file),
        }
        if target_hash == entry["sha256"]:
            matched.append(result)
        else:
            mismatched.append(result)

    return _format_response({
        "success": True,
        "verified": not missing and not mismatched and plan.get("blocked_count", 0) == 0,
        "matched_count": len(matched),
        "missing_count": len(missing),
        "mismatched_count": len(mismatched),
        "blocked_count": plan.get("blocked_count", 0),
        "matched": matched,
        "missing": missing,
        "mismatched": mismatched,
        "blocked": plan.get("blocked", []),
        "warnings": plan.get("warnings", []),
        "source": plan.get("source", {}),
        "target": plan.get("target", {}),
    })


@mcp.tool()
def task_submit(
    command: str,
    params: dict | None = None,
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Submit a CommonAIExport command for async background execution.

    Args:
        command: TCP/MCP command name to run, e.g. "capture_widget_preview".
        params: Optional command parameters object for the target command.
        scope: Optional scope for gated commands: "read", "write", or
               "destructive". Required for destructive commands.
        dry_run: If True, mutating target commands return a generic dry-run
                 response and do not change Unreal Editor state.

    Returns:
        JSON with task_id, command, status, and target timeout metadata.
    """
    payload: dict = {"command": command}
    if params is not None:
        payload["params"] = params
    if scope:
        payload["scope"] = scope
    if dry_run:
        payload["dry_run"] = True
    return _format_response(_send_command("task_submit", payload))


@mcp.tool()
def task_status(task_id: str = "") -> str:
    """
    Check async task status.

    Args:
        task_id: Optional task id. If omitted, returns all known tasks.

    Returns:
        JSON with task status or a task list.
    """
    if task_id:
        return _format_response(_send_command("task_status", {"task_id": task_id}))
    return _format_response(_send_command("task_status"))


@mcp.tool()
def task_result(task_id: str = "") -> str:
    """
    Get async task result.

    Args:
        task_id: Optional task id. If omitted, returns completed task summaries.

    Returns:
        JSON with stored response_json for the requested task.
    """
    if task_id:
        return _format_response(_send_command("task_result", {"task_id": task_id}))
    return _format_response(_send_command("task_result"))


@mcp.tool()
def task_cancel(task_id: str = "") -> str:
    """
    Request cooperative cancellation for an async task.

    Args:
        task_id: Optional task id. If omitted, returns currently cancellable
                 task summaries.

    Returns:
        JSON with cancellation request status.
    """
    if task_id:
        return _format_response(_send_command("task_cancel", {"task_id": task_id}))
    return _format_response(_send_command("task_cancel"))


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
        widget_class: Widget class short name or explicit custom Widget Blueprint
                     asset/generated-class path. Custom WBP forms:
                     /Game/UI/Path/W_Component
                     /Game/UI/Path/W_Component_C
                     /Game/UI/Path/W_Component.W_Component_C
                     WidgetBlueprintGeneratedClass'/Game/UI/Path/W_Component.W_Component_C'
                     Common types:
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
    List currently discoverable widget classes.

    This is a convenience list, not the authority for freshly created Widget
    Blueprint classes. add_widget can still resolve explicit WBP asset/generated
    class paths.

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
# DATA ASSET
# =============================================================================

@mcp.tool()
def save_data_asset(asset_path: str) -> str:
    """
    Save a Data Asset to disk without compiling.

    Args:
        asset_path: Asset path, e.g. "/Game/Data/DA_MyData"

    Returns:
        JSON with saved status.
    """
    return _format_response(_send_command("save_data_asset", {"asset_path": asset_path}))


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
    pos_y: int = 0,
    graph_name: str = "EventGraph"
) -> str:
    """
    Add an event override node to a Blueprint graph.

    Creates a node that overrides a parent class event (e.g. BP_OnSelected,
    BP_OnDeselected, BP_OnHovered, ReceiveBeginPlay, HandleTabCreation).

    Args:
        asset_path: Asset path of the Blueprint
        event_name: Function name to override (e.g. "BP_OnSelected", "ReceiveBeginPlay")
        node_name: Logical name for later reference in connect_pins etc.
        pos_x: X position in graph
        pos_y: Y position in graph
        graph_name: Target graph name. Defaults to "EventGraph".

    Returns:
        JSON with node_name and event_name on success.
    """
    return _format_response(_send_command("add_event_node", {
        "asset_path": asset_path,
        "event_name": event_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
        "graph_name": graph_name,
    }))


@mcp.tool()
def add_custom_event(
    asset_path: str,
    event_name: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0,
    graph_name: str = "EventGraph"
) -> str:
    """
    Add a Custom Event node to a Blueprint graph.

    Args:
        asset_path: Asset path of the Blueprint
        event_name: Custom event display name
        node_name: Logical name for later reference
        pos_x: X position in graph
        pos_y: Y position in graph
        graph_name: Target graph name. Defaults to "EventGraph".

    Returns:
        JSON with node_name and event_name.
    """
    return _format_response(_send_command("add_custom_event", {
        "asset_path": asset_path,
        "event_name": event_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
        "graph_name": graph_name,
    }))


@mcp.tool()
def add_function_call(
    asset_path: str,
    function_name: str,
    node_name: str,
    target_class: str = "",
    pos_x: int = 0,
    pos_y: int = 0,
    graph_name: str = "EventGraph"
) -> str:
    """
    Add a function call node to a Blueprint graph.

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
        graph_name: Target graph name. Defaults to "EventGraph".

    Returns:
        JSON with node_name and function_name.
    """
    params = {
        "asset_path": asset_path,
        "function_name": function_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
        "graph_name": graph_name,
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
    pos_y: int = 0,
    graph_name: str = "EventGraph"
) -> str:
    """
    Add a Variable Get node to the graph.

    Args:
        asset_path: Asset path of the Blueprint
        variable_name: Blueprint variable name (e.g. "ButtonTextBlock")
        node_name: Logical name for later reference
        pos_x: X position in graph
        pos_y: Y position in graph
        graph_name: Target graph name. Defaults to "EventGraph".

    Returns:
        JSON with node_name and variable_name.
    """
    return _format_response(_send_command("add_variable_get_node", {
        "asset_path": asset_path,
        "variable_name": variable_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
        "graph_name": graph_name,
    }))


@mcp.tool()
def add_variable_set_node(
    asset_path: str,
    variable_name: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0,
    graph_name: str = "EventGraph"
) -> str:
    """
    Add a Variable Set node to the graph.

    Args:
        asset_path: Asset path of the Blueprint
        variable_name: Blueprint variable name
        node_name: Logical name for later reference
        pos_x: X position in graph
        pos_y: Y position in graph
        graph_name: Target graph name. Defaults to "EventGraph".

    Returns:
        JSON with node_name and variable_name.
    """
    return _format_response(_send_command("add_variable_set_node", {
        "asset_path": asset_path,
        "variable_name": variable_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
        "graph_name": graph_name,
    }))


@mcp.tool()
def add_make_struct_node(
    asset_path: str,
    struct_name: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0,
    graph_name: str = "EventGraph"
) -> str:
    """
    Add a Make Struct node to the graph.

    Args:
        asset_path: Asset path of the Blueprint
        struct_name: Struct type name (e.g. "LinearColor", "Vector", "SlateFontInfo")
        node_name: Logical name for later reference
        pos_x: X position in graph
        pos_y: Y position in graph
        graph_name: Target graph name. Defaults to "EventGraph".

    Returns:
        JSON with node_name and struct_name.
    """
    return _format_response(_send_command("add_make_struct_node", {
        "asset_path": asset_path,
        "struct_name": struct_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
        "graph_name": graph_name,
    }))


@mcp.tool()
def add_branch_node(
    asset_path: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0,
    graph_name: str = "EventGraph"
) -> str:
    """
    Add a Branch (if/else) node to the graph.

    Args:
        asset_path: Asset path of the Blueprint
        node_name: Logical name for later reference
        pos_x: X position in graph
        pos_y: Y position in graph
        graph_name: Target graph name. Defaults to "EventGraph".

    Returns:
        JSON with node_name.
    """
    return _format_response(_send_command("add_branch_node", {
        "asset_path": asset_path,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
        "graph_name": graph_name,
    }))


@mcp.tool()
def add_call_parent_function(
    asset_path: str,
    function_name: str,
    node_name: str,
    pos_x: int = 0,
    pos_y: int = 0,
    graph_name: str = "EventGraph"
) -> str:
    """
    Add a Call Parent Function node to a Blueprint graph.

    Args:
        asset_path: Asset path of the Blueprint
        function_name: Parent function to call
        node_name: Logical name for later reference
        pos_x: X position in graph
        pos_y: Y position in graph
        graph_name: Target graph name. Defaults to "EventGraph".

    Returns:
        JSON with node_name and function_name.
    """
    return _format_response(_send_command("add_call_parent_function", {
        "asset_path": asset_path,
        "function_name": function_name,
        "node_name": node_name,
        "pos_x": pos_x,
        "pos_y": pos_y,
        "graph_name": graph_name,
    }))


@mcp.tool()
def ensure_function_graph(
    asset_path: str,
    function_name: str,
    inputs: list[dict] | None = None,
    outputs: list[dict] | None = None,
    entry_node_name: str = "",
    result_node_name: str = ""
) -> str:
    """
    Create or update a Blueprint function graph and tag its entry/result nodes.

    Use this before adding nodes to a named function graph. Pin specs are dicts:
    {"name": "InputType", "type": "enum:/Script/CommonInput.ECommonInputType",
     "default_value": "MouseAndKeyboard"}. Supported `type` strings include
    primitives, structs, object class paths, "class:/Script/...", and "enum:/Script/...".

    Args:
        asset_path: Asset path of the Blueprint
        function_name: Function graph name
        inputs: Optional input pins exposed as output pins on the entry node
        outputs: Optional return pins exposed as input pins on a result node
        entry_node_name: Optional AI logical name for the function entry node
        result_node_name: Optional AI logical name for the function result node

    Returns:
        JSON with graph_name, function_name, entry_node_name, and pin counts.
    """
    params = {
        "asset_path": asset_path,
        "function_name": function_name,
    }
    if inputs:
        params["inputs"] = inputs
    if outputs:
        params["outputs"] = outputs
    if entry_node_name:
        params["entry_node_name"] = entry_node_name
    if result_node_name:
        params["result_node_name"] = result_node_name
    return _format_response(_send_command("ensure_function_graph", params))


@mcp.tool()
def connect_pins(
    asset_path: str,
    from_node: str,
    from_pin: str,
    to_node: str,
    to_pin: str,
    graph_name: str = ""
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
        graph_name: Optional graph name to restrict node lookup.

    Returns:
        JSON with connection details.
    """
    return _format_response(_send_command("connect_pins", {
        "asset_path": asset_path,
        "from_node": from_node,
        "from_pin": from_pin,
        "to_node": to_node,
        "to_pin": to_pin,
        "graph_name": graph_name,
    }))


@mcp.tool()
def set_pin_default(
    asset_path: str,
    node_name: str,
    pin_name: str,
    default_value: str,
    graph_name: str = ""
) -> str:
    """
    Set a pin's default value (for unconnected input pins).

    Args:
        asset_path: Asset path of the Blueprint
        node_name: Target node logical name
        pin_name: Pin name on the node
        default_value: Value as string (ImportText format)
        graph_name: Optional graph name to restrict node lookup.

    Returns:
        JSON with success status.
    """
    return _format_response(_send_command("set_pin_default", {
        "asset_path": asset_path,
        "node_name": node_name,
        "pin_name": pin_name,
        "default_value": default_value,
        "graph_name": graph_name,
    }))


@mcp.tool()
def remove_graph_node(asset_path: str, node_name: str, graph_name: str = "") -> str:
    """
    Remove a node from the Blueprint graph by its logical name.

    Args:
        asset_path: Asset path of the Blueprint
        node_name: Logical name of the node to remove
        graph_name: Optional graph name to restrict node lookup.

    Returns:
        JSON with removal confirmation.
    """
    return _format_response(_send_command("remove_graph_node", {
        "asset_path": asset_path,
        "node_name": node_name,
        "graph_name": graph_name,
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
    blueprint_read_only: bool = False,
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
                 - Class refs: "class:/Script/CommonUI.CommonButtonStyle"
                 - Enums: "enum:/Script/SlateCore.EHorizontalAlignment"
        instance_editable: Expose to Details panel in editor (default: False)
        blueprint_read_only: Mark the variable read-only to child Blueprints
        category: Optional variable category name

    Returns:
        JSON with var_name and var_type.
    """
    params = {
        "asset_path": asset_path,
        "var_name": var_name,
        "var_type": var_type,
        "instance_editable": instance_editable,
        "blueprint_read_only": blueprint_read_only,
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
def asset_search(
    path: str = "/Game",
    name_filter: str = "",
    class_filter: str = "",
    recursive: bool = True,
    offset: int = 0,
    limit: int = 100,
) -> str:
    """
    Search Asset Registry assets with pagination.

    Args:
        path: Content path to search, e.g. "/Game/UI".
        name_filter: Optional substring matched against asset/package name.
        class_filter: Optional substring matched against class path.
        recursive: Include child paths.
        offset: Number of matched assets to skip.
        limit: Maximum assets returned.

    Returns:
        JSON with assets, returned_count, matched_count, and truncation state.
    """
    return _format_response(_send_command("asset_search", {
        "path": path,
        "name_filter": name_filter,
        "class_filter": class_filter,
        "recursive": recursive,
        "offset": offset,
        "limit": limit,
    }))


@mcp.tool()
def asset_validate_light(asset_path: str) -> str:
    """
    Run a lightweight Asset Registry health check.

    Args:
        asset_path: Asset package path, e.g. "/Game/UI/W_Menu".

    Returns:
        JSON with existence, package file, dependency/referencer counts,
        redirector/missing warnings, and external dependency hints.
    """
    return _format_response(_send_command("asset_validate_light", {
        "asset_path": asset_path,
    }))


@mcp.tool()
def asset_exists(asset_path: str) -> str:
    """
    Check whether an asset/package exists in the Asset Registry or on disk.

    Args:
        asset_path: Asset package path, e.g. "/Game/UI/W_Menu".

    Returns:
        JSON with exists, package_name, package filename, class path when known,
        and scan state.
    """
    return _format_response(_send_command("asset_exists", {
        "asset_path": asset_path,
    }))


@mcp.tool()
def scan_asset_paths(
    paths: list[str] | None = None,
    path: str = "",
    force_rescan: bool = False,
) -> str:
    """
    Ask the target editor Asset Registry to scan one or more Content paths.

    Use after package files are copied into a project while the editor is open.

    Args:
        paths: Content package paths, e.g. ["/Game/UI", "/Game/Materials"].
        path: Single path convenience parameter when paths is omitted.
        force_rescan: Force rescan even if the registry has seen the path.

    Returns:
        JSON with scanned paths and count.
    """
    params: dict = {"force_rescan": force_rescan}
    if paths:
        params["paths"] = paths
    elif path:
        params["path"] = path
    return _format_response(_send_command("scan_asset_paths", params))


@mcp.tool()
def get_referencers(asset_path: str) -> str:
    """
    Find all packages that reference this asset (UE Reference Viewer "Referencers" direction).

    Use before deleting an asset to confirm nothing depends on it. Empty list means
    the asset is safe to delete from a hard-reference standpoint.

    Args:
        asset_path: Asset path, e.g. "/Game/UI/Hud/Textures/Tiles/T_Tile_Red_5"
                   Accepts both "/Game/Path/Asset" and "/Game/Path/Asset.Asset" forms.

    Returns:
        JSON with:
          - asset_path        : input path (as given)
          - package_name      : normalized package name used for query
          - count             : number of referencers
          - scan_was_incomplete: true if AssetRegistry was still scanning (waited)
          - referencers       : array of package names that reference this asset
    """
    return _format_response(_send_command("get_referencers", {
        "asset_path": asset_path,
    }))


@mcp.tool()
def get_dependencies(asset_path: str) -> str:
    """
    Find all packages that this asset references (UE Reference Viewer "Dependencies" direction).

    Mirror of get_referencers — shows what this asset pulls in (textures referenced by
    a material, materials used by a widget, etc.).

    Args:
        asset_path: Asset path, e.g. "/Game/UI/Hud/Materials/M_Tile_Base"
                   Accepts both "/Game/Path/Asset" and "/Game/Path/Asset.Asset" forms.

    Returns:
        JSON with:
          - asset_path        : input path (as given)
          - package_name      : normalized package name used for query
          - count             : number of dependencies
          - scan_was_incomplete: true if AssetRegistry was still scanning (waited)
          - dependencies      : array of package names this asset references
    """
    return _format_response(_send_command("get_dependencies", {
        "asset_path": asset_path,
    }))


@mcp.tool()
def list_redirectors(folder_path: str, recursive: bool = True) -> str:
    """
    List UObjectRedirector assets under a folder (Content Browser "Show Redirectors" equivalent).

    Redirectors are left behind by legacy rename/move operations and may exist even though
    rename_asset performs inline fixup, because older workflows (source control sync, manual
    renames outside AssetTools) can still create them. Call this before fixup_redirectors to
    see what will be touched.

    Args:
        folder_path: e.g. "/Game/UI/Hud/Textures/Tiles"
        recursive: recurse into subfolders (default True)

    Returns:
        JSON with:
          - folder_path       : input path
          - recursive         : bool echoed back
          - count             : number of redirectors found
          - scan_was_incomplete: true if AssetRegistry was still scanning (waited)
          - redirectors       : array of { redirector_path, destination_path, stale }
                                stale=true means DestinationObject is null (broken redirector)
    """
    return _format_response(_send_command("list_redirectors", {
        "folder_path": folder_path,
        "recursive": recursive,
    }))


@mcp.tool()
def fixup_redirectors(folder_path: str, recursive: bool = True) -> str:
    """
    Update all references to redirectors under a folder and delete the redirectors
    (UE "Fix Up Redirectors in Folder" equivalent).

    Uses IAssetTools::FixupReferencers which rewrites hard references from referencing
    packages to the redirector's destination, then deletes the now-unused redirector
    packages. Stale redirectors (DestinationObject = null) are skipped.

    Run this after bulk rename/delete operations to clean up the package tree.

    Args:
        folder_path: e.g. "/Game/UI/Hud/Textures/Tiles"
        recursive: recurse into subfolders (default True)

    Returns:
        JSON with:
          - folder_path        : input path
          - recursive          : bool echoed back
          - redirectors_found  : total redirectors discovered
          - redirectors_fixed  : redirectors passed to FixupReferencers
          - skipped_count      : number skipped (stale/load-failed)
          - scan_was_incomplete: true if AssetRegistry was still scanning (waited)
          - skipped            : array of { redirector_path, reason }
                                 reason in { "load_failed", "stale_no_destination" }
    """
    return _format_response(_send_command("fixup_redirectors", {
        "folder_path": folder_path,
        "recursive": recursive,
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


@mcp.tool()
def rename_asset(
    asset_path: str,
    new_package_path: str = "",
    new_asset_name: str = ""
) -> str:
    """
    Rename and/or move an asset. Uses UE AssetTools::RenameAssets which automatically
    creates a redirector and fixes references in dependent assets.

    At least one of new_package_path or new_asset_name must be provided.
    Empty values mean "keep current".

    Args:
        asset_path: Current asset path, e.g. "/Game/UI/Foundation/Buttons/W_CircleBtn"
        new_package_path: New package folder path (optional), e.g. "/Game/UI/Foundation/Buttons"
                         If empty, keeps current folder.
        new_asset_name: New asset name (optional), e.g. "W_IconCircleButton"
                       If empty, keeps current name.

    Returns:
        JSON with renamed status, old_path, new_path, new_package_path, new_asset_name.

    Notes:
        - A redirector is created at the old path so existing references continue to work.
        - Dependent assets are auto-updated by AssetTools (no manual reference fixup needed).
        - Operation runs on Game Thread; up to 120s timeout.
        - For Blueprint/Widget Blueprint assets, pass the BP path (not the _C generated class).
    """
    params = {"asset_path": asset_path}
    if new_package_path:
        params["new_package_path"] = new_package_path
    if new_asset_name:
        params["new_asset_name"] = new_asset_name
    return _format_response(_send_command("rename_asset", params))


@mcp.tool()
def delete_asset(
    asset_path: str,
    force: bool = False,
    scope: str = "",
    dry_run: bool = False,
) -> str:
    """
    Delete an asset from disk via UE ObjectTools (no Content Browser UI required).

    Default path (force=False) uses ObjectTools::DeleteAssets which refuses to delete
    assets that still have referencers — returns count=0 in that case. Inspect with
    get_referencers first, or pass force=True to use ForceDeleteObjects which bypasses
    the reference check (leaves dangling refs; use only when you've verified externally).

    Args:
        asset_path: Asset path, e.g. "/Game/UI/Hud/Textures/Tiles/T_Tile_Red_5"
                   Accepts both "/Game/Path/Asset" and "/Game/Path/Asset.Asset" forms.
        force: If True, bypass reference check and force-delete. Default False.
        scope: Must be "destructive" for this command to execute.
        dry_run: If True with scope="destructive", validates the request and
                 returns without deleting.

    Returns:
        JSON with:
          - deleted       : true on success
          - asset_path    : input path
          - package_name  : package that was deleted
          - num_deleted   : int returned by ObjectTools (1 on success)
          - force         : which code path was used

    Notes:
        - Runs on Game Thread; up to 120s timeout.
        - Prefer get_referencers + default (non-force) delete — safest against dangling refs.
        - For Blueprint/Widget Blueprint assets, pass the BP path (not the _C generated class).
    """
    params = {"asset_path": asset_path}
    if force:
        params["force"] = True
    return _format_response(_send_command("delete_asset", params, _request_meta(scope, dry_run)))


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


# =============================================================================
# WIDGET PREVIEW CAPTURE (IFTP verify loop — multi-ratio fidelity testing)
# =============================================================================

@mcp.tool()
def capture_widget_preview(
    asset_path: str,
    width: int = 1920,
    height: int = 1080,
    output_path: str = "",
    warmup_frames: int = 3,
    transparent_bg: bool = False,
    return_base64: bool = False,
    dpi_scale: float = 1.0,
    preview_mode: str = "runtime",
    preview_function_calls: list[dict] | None = None,
    ratios: list[dict] | None = None,
) -> str:
    """
    Render a Widget Blueprint to PNG file(s) at one or more resolutions.

    Primary use case: IFTP verify loop — compare UE rendering with the Pencil source
    across multiple screen ratios (16:9, 21:9, 9:16, 4:3). Claude Code can read the
    resulting PNG via the Read tool and visually compare with Pencil get_screenshot.

    Args:
        asset_path: Widget Blueprint asset path, e.g. "/Game/UI/Menu/W_Splash"
        width: Single-shot width (used only when `ratios` is empty). Default 1920.
        height: Single-shot height. Default 1080.
        output_path: Explicit output PNG path (single-shot mode only).
                    If empty, PNG is written to
                    <ProjectIntermediate>/WidgetCaptures/<asset><suffix>.png
        warmup_frames: Number of render passes before capture (absorbs texture
                      streaming delay). Default 3, range 1-10.
        transparent_bg: If True, clear color is transparent (useful for UI overlays
                       where a background image isn't baked in). Default False (black).
        return_base64: If True, each PNG entry also includes a base64 "png_base64"
                      field. Normally False — prefer reading the file directly.
        dpi_scale: DPI scale passed to widget renderer. Default 1.0.
        preview_mode: "runtime" (default) follows the runtime widget lifecycle.
                      "designer" enables design-time PreConstruct preview data.
        preview_function_calls: Optional function calls applied to the live widget
                      instance after runtime activation and before rendering.
                      Example: [{"function_name": "SwitchToTab", "args": {"Tab": "2"}}]
        ratios: Optional list of multi-ratio capture entries. Each entry is a dict:
                {"width": int, "height": int, "label": str (optional)}
                Example: [
                    {"width": 1920, "height": 1080, "label": "16x9"},
                    {"width": 2560, "height": 1080, "label": "21x9"},
                    {"width": 1080, "height": 1920, "label": "9x16"},
                    {"width": 1440, "height": 1080, "label": "4x3"},
                ]
                When provided, `width`/`height`/`output_path` are ignored and one
                PNG is written per ratio, with the label baked into the filename.

    Returns:
        JSON with `pngs` array. Each entry: `{png_path, width, height, size_bytes,
        preview_mode, label?, png_base64?}`. Also `count`, `asset_path`, and
        `preview_mode`.
        After the call, read a png_path with the Read tool to visually inspect it.
    """
    params = {
        "asset_path": asset_path,
        "width": width,
        "height": height,
        "warmup_frames": warmup_frames,
        "transparent_bg": transparent_bg,
        "return_base64": return_base64,
        "dpi_scale": dpi_scale,
        "preview_mode": preview_mode,
    }
    if output_path:
        params["output_path"] = output_path
    if preview_function_calls:
        params["preview_function_calls"] = preview_function_calls
    if ratios:
        params["ratios"] = ratios
    return _format_response(_send_command("capture_widget_preview", params))


# =============================================================================
# ASSET LIFECYCLE
# =============================================================================

@mcp.tool()
def reload_asset(
    asset_path: str,
    reopen_after: bool = True,
) -> str:
    """
    Reload an asset from disk, clearing any cached editor tab state.

    Use this after `compile_and_save` when the user has the asset open in an
    editor tab. The editor keeps a cached widget instance that does not auto-
    refresh after backend modifications, so the user sees the old version even
    though the on-disk asset is up to date. This tool closes the editor,
    hard-reloads the package, and (optionally) reopens the editor.

    `capture_widget_preview` renders directly from disk, so it does NOT need
    reload — only the user-facing editor tab needs this.

    Args:
        asset_path: Content path of the asset, e.g. "/Game/UI/Menu/W_MainMenu_HF03d"
        reopen_after: If True (default), reopen the editor tab after reload if
                      it was previously open.

    Returns:
        JSON with `was_open` (was it open before reload), `reloaded` (did the
        hard reload succeed), and `reopened` (was it reopened after).
    """
    return _format_response(_send_command("reload_asset", {
        "asset_path": asset_path,
        "reopen_after": reopen_after,
    }))


if __name__ == "__main__":
    mcp.run()
