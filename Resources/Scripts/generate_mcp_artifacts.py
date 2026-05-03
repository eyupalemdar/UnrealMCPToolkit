#!/usr/bin/env python3
"""Generate CommonAIExport MCP manifest, schemas, catalog, and server metadata."""

from __future__ import annotations

import argparse
import ast
import json
import re
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path


PLUGIN_ROOT = Path(__file__).resolve().parents[2]
PROJECT_ROOT = Path(__file__).resolve().parents[4]
CPP_SERVER = PLUGIN_ROOT / "Source" / "CommonAIExport" / "Private" / "AIExportTCPServer.cpp"
MCP_CLIENT = PLUGIN_ROOT / "MCPClient" / "ai_widget_mcp_client.py"
GENERATED_DIR = PLUGIN_ROOT / "Resources" / "Generated"

COMMAND_MANIFEST_PATH = GENERATED_DIR / "CommonAIExport_CommandManifest.json"
TOOL_SCHEMAS_PATH = GENERATED_DIR / "CommonAIExport_ToolSchemas.json"
TOOL_CATALOG_PATH = GENERATED_DIR / "CommonAIExport_ToolCatalog.md"
SERVER_METADATA_PATH = GENERATED_DIR / "CommonAIExport_server.json"
WRAPPER_SPEC_PATH = GENERATED_DIR / "CommonAIExport_WrapperSpec.json"
WRAPPER_STUBS_PATH = GENERATED_DIR / "CommonAIExport_MCPWrapperStubs.py"
AI_REFERENCE_PATH = PLUGIN_ROOT / "AI_REFERENCE.md"

AI_REFERENCE_SUMMARY_BEGIN = "<!-- BEGIN COMMONAI GENERATED TOOL SUMMARY -->"
AI_REFERENCE_SUMMARY_END = "<!-- END COMMONAI GENERATED TOOL SUMMARY -->"

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
    "client_scope_policy",
    "mcp_server_metadata_export",
    "native_http_status",
    "native_mcp_probe",
}

COMMONAI_RESOURCES = [
    "commonai://project/status",
    "commonai://commands/manifest",
    "commonai://editors/list",
    "commonai://logs/latest",
    "commonai://audit/http",
]

COMMONAI_PROMPTS = [
    "asset_safety_review",
    "blueprint_graph_inspection",
    "build_fix_test",
    "multi_editor_transfer",
    "runtime_debug_triage",
    "ui_transfer_validation",
]

MACRO_PATTERNS = (
    (
        "AI_COMMAND_PARAMS_SCOPE",
        re.compile(
            r'AI_COMMAND_PARAMS_SCOPE\("([^"]+)",\s*"([^"]+)",\s*(true|false),\s*(\d+),\s*"([^"]+)",\s*(true|false),\s*(true|false),\s*(\w+)\)'
        ),
    ),
    (
        "AI_COMMAND_NO_PARAMS_SCOPE",
        re.compile(
            r'AI_COMMAND_NO_PARAMS_SCOPE\("([^"]+)",\s*"([^"]+)",\s*(true|false),\s*(\d+),\s*"([^"]+)",\s*(true|false),\s*(true|false),\s*(\w+)\)'
        ),
    ),
    (
        "AI_COMMAND_OPTIONAL_PARAMS",
        re.compile(r'AI_COMMAND_OPTIONAL_PARAMS\("([^"]+)",\s*"([^"]+)",\s*(true|false),\s*(\d+),\s*(\w+)\)'),
    ),
    (
        "AI_COMMAND_PARAMS",
        re.compile(r'AI_COMMAND_PARAMS\("([^"]+)",\s*"([^"]+)",\s*(true|false),\s*(\d+),\s*(\w+)\)'),
    ),
    (
        "AI_COMMAND_NO_PARAMS",
        re.compile(r'AI_COMMAND_NO_PARAMS\("([^"]+)",\s*"([^"]+)",\s*(\d+),\s*(\w+)\)'),
    ),
)

PY_TOOL_RE = re.compile(r"^@mcp\.tool\(\)")
PY_DEF_RE = re.compile(r"^def ([a-zA-Z_][a-zA-Z0-9_]*)\(")


def _project_relative(path: Path) -> str:
    """Return a stable project-relative path, even when executed through a sandbox mirror."""
    resolved = path.resolve()
    try:
        return str(resolved.relative_to(PROJECT_ROOT)).replace("\\", "/")
    except ValueError:
        parts = list(resolved.parts)
        if "Plugins" in parts:
            return "/".join(parts[parts.index("Plugins") :])
        return resolved.name


def _bool(value: str) -> bool:
    return value == "true"


def parse_cpp_descriptors() -> list[dict]:
    """Parse FCommandDescriptor macro calls from AIExportTCPServer.cpp."""
    descriptors: list[dict] = []
    for line_number, line in enumerate(CPP_SERVER.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = line.strip().rstrip(",")
        if not stripped.startswith("AI_COMMAND_"):
            continue

        for macro_name, pattern in MACRO_PATTERNS:
            match = pattern.fullmatch(stripped)
            if not match:
                continue

            values = match.groups()
            if macro_name == "AI_COMMAND_PARAMS_SCOPE":
                name, category, mutating, timeout, scope, dry_run, async_candidate, handler = values
                requires_params = True
            elif macro_name == "AI_COMMAND_NO_PARAMS_SCOPE":
                name, category, mutating, timeout, scope, dry_run, async_candidate, handler = values
                requires_params = False
            elif macro_name == "AI_COMMAND_OPTIONAL_PARAMS":
                name, category, mutating, timeout, handler = values
                mutating_bool = _bool(mutating)
                scope = "write" if mutating_bool else "read"
                dry_run = "true" if mutating_bool else "false"
                async_candidate = "true" if int(timeout) >= 120 else "false"
                requires_params = False
            elif macro_name == "AI_COMMAND_PARAMS":
                name, category, mutating, timeout, handler = values
                mutating_bool = _bool(mutating)
                scope = "write" if mutating_bool else "read"
                dry_run = "true" if mutating_bool else "false"
                async_candidate = "true" if int(timeout) >= 120 else "false"
                requires_params = True
            else:
                name, category, timeout, handler = values
                mutating = "false"
                scope = "read"
                dry_run = "false"
                async_candidate = "false"
                requires_params = False

            descriptors.append(
                {
                    "name": name,
                    "category": category,
                    "requires_params": requires_params,
                    "mutating": _bool(mutating),
                    "timeout_seconds": int(timeout),
                    "required_scope": scope,
                    "supports_dry_run": _bool(dry_run),
                    "async_candidate": _bool(async_candidate),
                    "handler": handler,
                    "descriptor_macro": macro_name,
                    "source": _project_relative(CPP_SERVER),
                    "line": line_number,
                }
            )
            break
        else:
            raise RuntimeError(f"Unrecognized command descriptor macro at {CPP_SERVER}:{line_number}: {stripped}")

    return descriptors


def read_mcp_tools() -> list[str]:
    """Read @mcp.tool wrappers from the Python MCP client."""
    lines = MCP_CLIENT.read_text(encoding="utf-8").splitlines()
    tools: list[str] = []
    for index, line in enumerate(lines):
        if not PY_TOOL_RE.match(line):
            continue
        for candidate in lines[index + 1 : index + 8]:
            match = PY_DEF_RE.match(candidate)
            if match:
                tools.append(match.group(1))
                break
        else:
            raise RuntimeError(f"@mcp.tool() at {MCP_CLIENT}:{index + 1} has no nearby function")
    return tools


def _has_mcp_tool_decorator(function: ast.FunctionDef) -> bool:
    """Return true when a function is decorated with @mcp.tool()."""
    for decorator in function.decorator_list:
        call = decorator if isinstance(decorator, ast.Call) else None
        func = call.func if call else decorator
        if not isinstance(func, ast.Attribute) or func.attr != "tool":
            continue
        if isinstance(func.value, ast.Name) and func.value.id == "mcp":
            return True
    return False


def _literal_default(node: ast.AST | None) -> object:
    if node is None:
        return None
    try:
        return ast.literal_eval(node)
    except (TypeError, ValueError):
        return None


def _schema_for_annotation(annotation: ast.AST | None) -> dict:
    """Convert the type-hint subset used by MCP wrappers to JSON Schema."""
    if annotation is None:
        return {}

    if isinstance(annotation, ast.Name):
        if annotation.id == "str":
            return {"type": "string"}
        if annotation.id == "int":
            return {"type": "integer"}
        if annotation.id == "float":
            return {"type": "number"}
        if annotation.id == "bool":
            return {"type": "boolean"}
        if annotation.id == "dict":
            return {"type": "object", "additionalProperties": True}
        if annotation.id == "list":
            return {"type": "array"}
        return {}

    if isinstance(annotation, ast.Constant) and isinstance(annotation.value, str):
        return _schema_for_annotation(ast.Name(id=annotation.value, ctx=ast.Load()))

    if isinstance(annotation, ast.Subscript):
        base_name = annotation.value.id if isinstance(annotation.value, ast.Name) else ""
        if base_name == "list":
            return {"type": "array", "items": _schema_for_annotation(annotation.slice)}
        if base_name == "dict":
            return {"type": "object", "additionalProperties": True}
        return {}

    if isinstance(annotation, ast.BinOp) and isinstance(annotation.op, ast.BitOr):
        left = {"type": "null"} if isinstance(annotation.left, ast.Constant) and annotation.left.value is None else _schema_for_annotation(annotation.left)
        right = {"type": "null"} if isinstance(annotation.right, ast.Constant) and annotation.right.value is None else _schema_for_annotation(annotation.right)
        variants = [schema for schema in (left, right) if schema]
        if len(variants) == 1:
            return variants[0]
        if variants:
            return {"anyOf": variants}

    if isinstance(annotation, ast.Constant) and annotation.value is None:
        return {"type": "null"}

    return {}


def _ast_source(node: ast.AST | None) -> str:
    if node is None:
        return ""
    try:
        return ast.unparse(node)
    except Exception:
        return ""


def _send_command_names(function: ast.FunctionDef) -> list[str]:
    """Return literal TCP command names referenced by _send_command calls."""
    names: list[str] = []

    class Visitor(ast.NodeVisitor):
        def visit_Call(self, node: ast.Call) -> None:  # noqa: N802 - ast visitor API
            command_arg_index: int | None = None
            if isinstance(node.func, ast.Name) and node.func.id == "_send_command":
                command_arg_index = 0
            elif isinstance(node.func, ast.Name) and node.func.id == "_send_command_to_port":
                command_arg_index = 1

            if command_arg_index is not None and len(node.args) > command_arg_index:
                arg = node.args[command_arg_index]
                if isinstance(arg, ast.Constant) and isinstance(arg.value, str):
                    names.append(arg.value)

            self.generic_visit(node)

    Visitor().visit(function)
    return names


def read_mcp_tool_wrappers() -> dict[str, dict]:
    """Read wrapper metadata from @mcp.tool functions in the Python MCP client."""
    tree = ast.parse(MCP_CLIENT.read_text(encoding="utf-8"), filename=str(MCP_CLIENT))
    wrappers: dict[str, dict] = {}

    for node in tree.body:
        if not isinstance(node, ast.FunctionDef) or not _has_mcp_tool_decorator(node):
            continue

        positional_args = list(node.args.posonlyargs) + list(node.args.args)
        defaults: list[ast.AST | None] = [None] * (len(positional_args) - len(node.args.defaults)) + list(node.args.defaults)
        params: list[dict] = []
        for arg, default_node in zip(positional_args, defaults):
            if arg.arg in {"self", "cls"}:
                continue
            params.append(
                {
                    "name": arg.arg,
                    "kind": "positional_or_keyword",
                    "annotation": _ast_source(arg.annotation),
                    "default": _ast_source(default_node) if default_node is not None else None,
                    "required": default_node is None,
                }
            )

        for arg, default_node in zip(node.args.kwonlyargs, node.args.kw_defaults):
            params.append(
                {
                    "name": arg.arg,
                    "kind": "keyword_only",
                    "annotation": _ast_source(arg.annotation),
                    "default": _ast_source(default_node) if default_node is not None else None,
                    "required": default_node is None,
                }
            )

        wrappers[node.name] = {
            "name": node.name,
            "source": _project_relative(MCP_CLIENT),
            "line": node.lineno,
            "params": params,
            "returns": _ast_source(node.returns),
            "literal_send_commands": _send_command_names(node),
        }

    return wrappers


def read_mcp_tool_signatures() -> dict[str, dict]:
    """Read strict JSON Schema parameter shapes from @mcp.tool wrapper signatures."""
    tree = ast.parse(MCP_CLIENT.read_text(encoding="utf-8"), filename=str(MCP_CLIENT))
    signatures: dict[str, dict] = {}

    for node in tree.body:
        if not isinstance(node, ast.FunctionDef) or not _has_mcp_tool_decorator(node):
            continue

        positional_args = list(node.args.posonlyargs) + list(node.args.args)
        defaults: list[ast.AST | None] = [None] * (len(positional_args) - len(node.args.defaults)) + list(node.args.defaults)
        properties: dict[str, dict] = {}
        required: list[str] = []

        for arg, default_node in zip(positional_args, defaults):
            if arg.arg in {"self", "cls"}:
                continue
            schema = _schema_for_annotation(arg.annotation)
            default_value = _literal_default(default_node)
            if default_node is not None and default_value is not None:
                schema = dict(schema)
                schema["default"] = default_value
            properties[arg.arg] = schema
            if default_node is None:
                required.append(arg.arg)

        for arg, default_node in zip(node.args.kwonlyargs, node.args.kw_defaults):
            schema = _schema_for_annotation(arg.annotation)
            default_value = _literal_default(default_node)
            if default_node is not None and default_value is not None:
                schema = dict(schema)
                schema["default"] = default_value
            properties[arg.arg] = schema
            if default_node is None:
                required.append(arg.arg)

        signatures[node.name] = {
            "type": "object",
            "properties": properties,
            "required": required,
            "additionalProperties": False,
        }

    return signatures


def build_wrapper_spec(command_manifest: dict, tool_schemas: dict) -> dict:
    """Build a generated registry that ties TCP descriptors to Python wrappers."""
    wrappers = read_mcp_tool_wrappers()
    command_names = [command["name"] for command in command_manifest["commands"]]
    command_set = set(command_names)
    tools: dict[str, dict] = {}
    wrapper_drift: list[dict] = []

    for tool_name in sorted(tool_schemas.get("tools", {}).keys()):
        schema = tool_schemas["tools"][tool_name]
        source = schema.get("x-commonai", {}).get("source", "unknown")
        wrapper = wrappers.get(tool_name)
        item = {
            "name": tool_name,
            "source": source,
            "wrapper_present": wrapper is not None,
            "expected_tcp_command": tool_name if source == "tcp_command" else "",
            "literal_send_commands": wrapper.get("literal_send_commands", []) if wrapper else [],
            "signature": schema,
            "wrapper": wrapper or {},
        }
        if source == "tcp_command":
            item["calls_expected_tcp_command"] = bool(wrapper and tool_name in wrapper.get("literal_send_commands", []))
            if not item["wrapper_present"] or not item["calls_expected_tcp_command"]:
                wrapper_drift.append(
                    {
                        "name": tool_name,
                        "issue": "missing_or_mismatched_tcp_wrapper_call",
                        "literal_send_commands": item["literal_send_commands"],
                    }
                )
        tools[tool_name] = item

    missing_wrappers = sorted(name for name in command_names if name not in wrappers)
    extra_wrappers = sorted(name for name in wrappers if name not in command_set and name not in CLIENT_ONLY_TOOLS)

    return {
        "schema_version": 1,
        "server": "CommonAIExport",
        "generated_at_utc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "manifest_source": "FAIExportTCPServer::GetCommandDescriptors",
        "generator": _project_relative(Path(__file__)),
        "wrapper_source": _project_relative(MCP_CLIENT),
        "tool_count": len(tools),
        "tcp_command_count": len(command_names),
        "client_only_count": len(CLIENT_ONLY_TOOLS),
        "wrapper_count": len(wrappers),
        "missing_tcp_wrappers": missing_wrappers,
        "extra_tcp_wrappers": extra_wrappers,
        "wrapper_drift": wrapper_drift,
        "tools": tools,
    }


def _signature_text(params: list[dict]) -> str:
    parts: list[str] = []
    for param in params:
        text = param["name"]
        if param.get("annotation"):
            text += f": {param['annotation']}"
        if not param.get("required"):
            text += f" = {param.get('default') or 'None'}"
        parts.append(text)
    return ", ".join(parts)


def build_wrapper_stubs(command_manifest: dict, wrapper_spec: dict) -> str:
    """Generate reviewable Python wrapper stubs from descriptor and signature metadata."""
    lines = [
        '"""Generated CommonAIExport MCP wrapper stubs.',
        "",
        "This file is generated by Resources/Scripts/generate_mcp_artifacts.py.",
        "It is a review/regeneration aid and is not imported by the MCP client.",
        '"""',
        "",
        "from __future__ import annotations",
        "",
        "",
    ]

    for command in command_manifest["commands"]:
        name = command["name"]
        item = wrapper_spec["tools"].get(name, {})
        wrapper = item.get("wrapper") or {}
        params = wrapper.get("params") or []
        signature = _signature_text(params)
        lines.append(f"def {name}({signature}) -> str:" if signature else f"def {name}() -> str:")
        lines.append(f'    """Generated stub for TCP command `{name}` ({command["category"]})."""')

        payload_names = [param["name"] for param in params if param["name"] not in {"scope", "dry_run"}]
        has_scope = any(param["name"] == "scope" for param in params)
        has_dry_run = any(param["name"] == "dry_run" for param in params)

        if payload_names:
            lines.append("    params = {")
            for param_name in payload_names:
                lines.append(f'        "{param_name}": {param_name},')
            lines.append("    }")
        else:
            lines.append("    params = None")

        if has_scope or has_dry_run:
            scope_arg = "scope" if has_scope else '""'
            dry_run_arg = "dry_run" if has_dry_run else "False"
            lines.append(f'    return _format_response(_send_command("{name}", params, _request_meta({scope_arg}, {dry_run_arg})))')
        elif payload_names:
            lines.append(f'    return _format_response(_send_command("{name}", params))')
        else:
            lines.append(f'    return _format_response(_send_command("{name}"))')
        lines.extend(["", ""])

    return "\n".join(lines)


def build_command_manifest() -> dict:
    descriptors = parse_cpp_descriptors()
    categories = Counter(descriptor["category"] for descriptor in descriptors)
    return {
        "schema_version": 3,
        "server": "CommonAIExport",
        "generated_at_utc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "manifest_source": "FAIExportTCPServer::GetCommandDescriptors",
        "generator": _project_relative(Path(__file__)),
        "command_count": len(descriptors),
        "category_count": len(categories),
        "categories": dict(sorted(categories.items())),
        "supported_scopes": ["read", "write", "destructive"],
        "scope_model": "read < write < destructive; destructive commands require explicit meta.scope",
        "commands": descriptors,
    }


def build_tool_schemas(command_manifest: dict) -> dict:
    schemas: dict[str, dict] = {}
    tool_signatures = read_mcp_tool_signatures()

    for command in command_manifest["commands"]:
        signature_schema = tool_signatures.get(
            command["name"],
            {"type": "object", "properties": {}, "required": [], "additionalProperties": False},
        )
        schemas[command["name"]] = {
            **signature_schema,
            "x-commonai": {
                "source": "tcp_command",
                "schema_source": "python_mcp_signature",
                "category": command["category"],
                "required_scope": command["required_scope"],
                "mutating": command["mutating"],
                "supports_dry_run": command["supports_dry_run"],
                "async_candidate": command["async_candidate"],
                "timeout_seconds": command["timeout_seconds"],
            },
            "annotations": {
                "readOnlyHint": not command["mutating"],
                "destructiveHint": command["required_scope"] == "destructive",
                "idempotentHint": not command["mutating"],
            },
        }

    for tool_name in sorted(CLIENT_ONLY_TOOLS):
        signature_schema = tool_signatures.get(
            tool_name,
            {"type": "object", "properties": {}, "required": [], "additionalProperties": False},
        )
        schemas[tool_name] = {
            **signature_schema,
            "x-commonai": {
                "source": "python_client_only",
                "schema_source": "python_mcp_signature",
                "category": "Client",
                "required_scope": "read",
                "mutating": False,
                "supports_dry_run": False,
                "async_candidate": False,
            },
            "annotations": {
                "readOnlyHint": True,
                "destructiveHint": False,
                "idempotentHint": True,
            },
        }

    return {
        "schema_version": 1,
        "server": "CommonAIExport",
        "generated_at_utc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "tool_count": len(schemas),
        "tools": schemas,
    }


def build_server_metadata(command_manifest: dict, tool_schemas: dict) -> dict:
    return {
        "schema_version": 1,
        "name": "commonai-export",
        "display_name": "CommonAIExport",
        "description": "Project-local Unreal Editor automation MCP bridge for ProjectOkey.",
        "version": "0.4.1",
        "generated_at_utc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "transports": {
            "stdio": {
                "command": "python",
                "args": ["Plugins/CommonAIExport/MCPClient/ai_widget_mcp_client.py"],
            },
            "native_http_mcp": {
                "discovery_file": "Intermediate/AIExport_http_port.txt",
                "path": "/mcp",
                "bind": "127.0.0.1",
                "protocol_version": "2025-06-18",
                "supports_sessions": True,
                "supports_pagination": True,
                "optional_bearer_token_env": "COMMONAI_MCP_HTTP_TOKEN",
            },
        },
        "tools": {
            "total": tool_schemas["tool_count"],
            "tcp_commands": command_manifest["command_count"],
            "client_only": sorted(CLIENT_ONLY_TOOLS),
            "schemas": "Resources/Generated/CommonAIExport_ToolSchemas.json",
            "wrapper_spec": "Resources/Generated/CommonAIExport_WrapperSpec.json",
            "wrapper_stubs": "Resources/Generated/CommonAIExport_MCPWrapperStubs.py",
            "schema_source": "Python MCP wrapper signatures",
            "strict_parameter_schemas": True,
            "wrapper_drift_checked": True,
        },
        "resources": COMMONAI_RESOURCES,
        "prompts": COMMONAI_PROMPTS,
        "security": {
            "localhost_only": True,
            "scopes": ["read", "write", "destructive"],
            "dry_run": True,
            "destructive_requires_explicit_scope": True,
            "allowed_origins_env": "COMMONAI_MCP_HTTP_ALLOWED_ORIGINS",
        },
    }


def build_tool_catalog(command_manifest: dict, tool_schemas: dict) -> str:
    lines = [
        "# CommonAIExport Generated Tool Catalog",
        "",
        "> Generated from `FAIExportTCPServer::GetCommandDescriptors`; do not edit by hand.",
        "",
        f"- TCP commands: {command_manifest['command_count']}",
        f"- MCP tools: {tool_schemas['tool_count']}",
        f"- Categories: {command_manifest['category_count']}",
        "- Parameter schemas: strict top-level JSON Schema from Python MCP wrapper signatures",
        "- Wrapper registry: `Resources/Generated/CommonAIExport_WrapperSpec.json`",
        "- Wrapper stubs: `Resources/Generated/CommonAIExport_MCPWrapperStubs.py`",
        "",
        "## Categories",
        "",
        "| Category | Count |",
        "|---|---:|",
    ]
    for category, count in command_manifest["categories"].items():
        lines.append(f"| `{category}` | {count} |")

    lines.extend(
        [
            "",
            "## TCP Commands",
            "",
            "| Name | Category | Scope | Mutating | Dry-run | Async | Timeout |",
            "|---|---|---|---:|---:|---:|---:|",
        ]
    )
    for command in command_manifest["commands"]:
        mutating = str(command["mutating"]).lower()
        supports_dry_run = str(command["supports_dry_run"]).lower()
        async_candidate = str(command["async_candidate"]).lower()
        lines.append(
            f"| `{command['name']}` | `{command['category']}` | `{command['required_scope']}` | "
            f"{mutating} | {supports_dry_run} | {async_candidate} | {command['timeout_seconds']} |"
        )

    lines.extend(
        [
            "",
            "## Client-Only MCP Tools",
            "",
        ]
    )
    for tool_name in sorted(CLIENT_ONLY_TOOLS):
        lines.append(f"- `{tool_name}`")

    lines.append("")
    return "\n".join(lines)


def build_ai_reference_tool_summary(command_manifest: dict, tool_schemas: dict) -> str:
    """Build the generated AI_REFERENCE tool summary block."""
    source_counts = Counter(
        schema.get("x-commonai", {}).get("source", "unknown")
        for schema in tool_schemas.get("tools", {}).values()
    )
    lines = [
        "## Tool Count by Category",
        "",
        AI_REFERENCE_SUMMARY_BEGIN,
        "> Generated by `Resources/Scripts/generate_mcp_artifacts.py`; do not edit this block by hand.",
        "",
        f"- TCP commands: `{command_manifest['command_count']}`",
        f"- Client-only MCP tools: `{source_counts.get('python_client_only', 0)}`",
        f"- Total MCP tools: `{tool_schemas['tool_count']}`",
        f"- Categories: `{command_manifest['category_count']}`",
        "- Full generated catalog: `Resources/Generated/CommonAIExport_ToolCatalog.md`",
        "",
        "| Category | Count |",
        "|---|---:|",
    ]
    for category, count in command_manifest["categories"].items():
        lines.append(f"| `{category}` | {count} |")

    lines.extend(
        [
            f"| **Total** | **{tool_schemas['tool_count']}** |",
            "",
            AI_REFERENCE_SUMMARY_END,
            "",
        ]
    )
    return "\n".join(lines)


def update_ai_reference(command_manifest: dict, tool_schemas: dict, path: Path = AI_REFERENCE_PATH) -> None:
    """Replace the generated AI_REFERENCE tool summary block."""
    text = path.read_text(encoding="utf-8")
    replacement = build_ai_reference_tool_summary(command_manifest, tool_schemas)

    if AI_REFERENCE_SUMMARY_BEGIN in text and AI_REFERENCE_SUMMARY_END in text:
        start = text.index("## Tool Count by Category")
        end = text.index(AI_REFERENCE_SUMMARY_END, start) + len(AI_REFERENCE_SUMMARY_END)
        rule_match = re.search(r"\r?\n---\r?\n", text[end:])
        if rule_match:
            end += rule_match.start()
        new_text = text[:start] + replacement + text[end:]
    else:
        start = text.index("## Tool Count by Category")
        rule_match = re.search(r"\r?\n---\r?\n", text[start:])
        if not rule_match:
            raise RuntimeError("AI_REFERENCE.md has no horizontal rule after Tool Count by Category")
        next_rule = start + rule_match.start()
        new_text = text[:start] + replacement + text[next_rule:]

    if new_text != text:
        path.write_text(new_text, encoding="utf-8")


def write_artifacts(output_dir: Path = GENERATED_DIR) -> dict[str, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)

    command_manifest = build_command_manifest()
    tool_schemas = build_tool_schemas(command_manifest)
    wrapper_spec = build_wrapper_spec(command_manifest, tool_schemas)
    server_metadata = build_server_metadata(command_manifest, tool_schemas)
    tool_catalog = build_tool_catalog(command_manifest, tool_schemas)
    wrapper_stubs = build_wrapper_stubs(command_manifest, wrapper_spec)

    targets = {
        "command_manifest": output_dir / COMMAND_MANIFEST_PATH.name,
        "tool_schemas": output_dir / TOOL_SCHEMAS_PATH.name,
        "server_metadata": output_dir / SERVER_METADATA_PATH.name,
        "tool_catalog": output_dir / TOOL_CATALOG_PATH.name,
        "wrapper_spec": output_dir / WRAPPER_SPEC_PATH.name,
        "wrapper_stubs": output_dir / WRAPPER_STUBS_PATH.name,
    }
    targets["command_manifest"].write_text(json.dumps(command_manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    targets["tool_schemas"].write_text(json.dumps(tool_schemas, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    targets["server_metadata"].write_text(json.dumps(server_metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    targets["tool_catalog"].write_text(tool_catalog, encoding="utf-8")
    targets["wrapper_spec"].write_text(json.dumps(wrapper_spec, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    targets["wrapper_stubs"].write_text(wrapper_stubs, encoding="utf-8")
    if output_dir.resolve() == GENERATED_DIR.resolve():
        update_ai_reference(command_manifest, tool_schemas)
        targets["ai_reference"] = AI_REFERENCE_PATH
    return targets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=GENERATED_DIR)
    args = parser.parse_args()

    targets = write_artifacts(args.output_dir)
    for label, path in targets.items():
        print(f"{label}: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
