#!/usr/bin/env python3
"""Validate CommonAIExport TCP command descriptors against MCP tool wrappers."""

from __future__ import annotations

import re
import sys
import json
from pathlib import Path

from generate_mcp_artifacts import (
    AI_REFERENCE_SUMMARY_BEGIN,
    AI_REFERENCE_SUMMARY_END,
    CLIENT_ONLY_TOOLS,
    COMMAND_MANIFEST_PATH,
    SERVER_METADATA_PATH,
    TOOL_CATALOG_PATH,
    TOOL_SCHEMAS_PATH,
    WRAPPER_SPEC_PATH,
    WRAPPER_STUBS_PATH,
    build_ai_reference_tool_summary,
    build_command_manifest,
    build_wrapper_spec,
    build_wrapper_stubs,
    build_tool_schemas,
)

PLUGIN_ROOT = Path(__file__).resolve().parents[2]
CPP_SERVER = PLUGIN_ROOT / "Source" / "CommonAIExport" / "Private" / "AIExportTCPServer.cpp"
MCP_CLIENT = PLUGIN_ROOT / "MCPClient" / "ai_widget_mcp_client.py"
AI_REFERENCE = PLUGIN_ROOT / "AI_REFERENCE.md"

CPP_COMMAND_RE = re.compile(r'AI_COMMAND_(?:NO_|OPTIONAL_)?PARAMS(?:_SCOPE)?\("([^"]+)"')
PY_TOOL_RE = re.compile(r"^@mcp\.tool\(\)")
PY_DEF_RE = re.compile(r"^def ([a-zA-Z_][a-zA-Z0-9_]*)\(")
DOC_COUNT_RES = (
    re.compile(r">\s*(\d+)\s+MCP tools"),
    re.compile(r"\|\s*\*\*Total\*\*\s*\|\s*\*\*(\d+)\*\*"),
    re.compile(r"\*(\d+)\s+MCP tools,"),
)


def _duplicates(items: list[str]) -> list[str]:
    seen: set[str] = set()
    duplicates: set[str] = set()
    for item in items:
        if item in seen:
            duplicates.add(item)
        seen.add(item)
    return sorted(duplicates)


def _read_cpp_commands() -> list[str]:
    text = CPP_SERVER.read_text(encoding="utf-8")
    return CPP_COMMAND_RE.findall(text)


def _read_mcp_tools() -> list[str]:
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
            raise RuntimeError(f"@mcp.tool() at line {index + 1} has no nearby function")

    return tools


def _read_doc_counts() -> list[int]:
    text = AI_REFERENCE.read_text(encoding="utf-8")
    counts: list[int] = []
    for pattern in DOC_COUNT_RES:
        counts.extend(int(match) for match in pattern.findall(text))
    return counts


def _stable_wrapper_spec(spec: dict) -> dict:
    stable = dict(spec)
    stable.pop("generated_at_utc", None)
    return stable


def _validate_generated_artifacts(expected_tcp_commands: list[str], expected_mcp_count: int) -> list[str]:
    errors: list[str] = []
    required_paths = [
        COMMAND_MANIFEST_PATH,
        TOOL_SCHEMAS_PATH,
        SERVER_METADATA_PATH,
        TOOL_CATALOG_PATH,
        WRAPPER_SPEC_PATH,
        WRAPPER_STUBS_PATH,
    ]
    missing = [path for path in required_paths if not path.exists()]
    if missing:
        return ["Generated MCP artifacts are missing; run generate_mcp_artifacts.py: " + ", ".join(str(path) for path in missing)]

    try:
        generated_manifest = json.loads(COMMAND_MANIFEST_PATH.read_text(encoding="utf-8"))
        generated_schemas = json.loads(TOOL_SCHEMAS_PATH.read_text(encoding="utf-8"))
        generated_server = json.loads(SERVER_METADATA_PATH.read_text(encoding="utf-8"))
        generated_wrapper_spec = json.loads(WRAPPER_SPEC_PATH.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return [f"Generated MCP artifact is invalid JSON: {exc}"]

    current_manifest = build_command_manifest()
    current_schemas = build_tool_schemas(current_manifest)
    current_wrapper_spec = build_wrapper_spec(current_manifest, current_schemas)

    generated_names = [command.get("name") for command in generated_manifest.get("commands", [])]
    current_names = [command.get("name") for command in current_manifest.get("commands", [])]
    if generated_names != current_names or generated_names != expected_tcp_commands:
        errors.append("Generated command manifest is stale; run generate_mcp_artifacts.py")

    if generated_manifest.get("command_count") != len(expected_tcp_commands):
        errors.append(
            f"Generated command manifest count mismatch: {generated_manifest.get('command_count')} "
            f"(expected {len(expected_tcp_commands)})"
        )

    if generated_schemas.get("tool_count") != expected_mcp_count:
        errors.append(
            f"Generated tool schema count mismatch: {generated_schemas.get('tool_count')} "
            f"(expected {expected_mcp_count})"
        )

    generated_schema_names = sorted(generated_schemas.get("tools", {}).keys())
    current_schema_names = sorted(current_schemas.get("tools", {}).keys())
    if generated_schema_names != current_schema_names:
        errors.append("Generated tool schemas are stale; run generate_mcp_artifacts.py")
    else:
        for tool_name in generated_schema_names:
            generated_schema = generated_schemas.get("tools", {}).get(tool_name, {})
            current_schema = current_schemas.get("tools", {}).get(tool_name, {})
            if generated_schema.get("additionalProperties") is not False:
                errors.append(f"Generated schema for {tool_name} is broad; run generate_mcp_artifacts.py")
                break
            if generated_schema.get("properties") != current_schema.get("properties"):
                errors.append(f"Generated schema properties for {tool_name} are stale; run generate_mcp_artifacts.py")
                break
            if generated_schema.get("required") != current_schema.get("required"):
                errors.append(f"Generated schema required list for {tool_name} is stale; run generate_mcp_artifacts.py")
                break

    if generated_server.get("tools", {}).get("total") != expected_mcp_count:
        errors.append(
            f"Generated server metadata tool count mismatch: {generated_server.get('tools', {}).get('total')} "
            f"(expected {expected_mcp_count})"
        )
    if generated_server.get("tools", {}).get("wrapper_drift_checked") is not True:
        errors.append("Generated server metadata is missing wrapper_drift_checked=true; run generate_mcp_artifacts.py")

    if _stable_wrapper_spec(generated_wrapper_spec) != _stable_wrapper_spec(current_wrapper_spec):
        errors.append("Generated wrapper spec is stale; run generate_mcp_artifacts.py")
    if generated_wrapper_spec.get("wrapper_drift"):
        errors.append("Generated wrapper spec reports TCP wrapper drift")
    if generated_wrapper_spec.get("missing_tcp_wrappers"):
        errors.append("Generated wrapper spec reports missing TCP wrappers")
    if generated_wrapper_spec.get("extra_tcp_wrappers"):
        errors.append("Generated wrapper spec reports extra TCP wrappers")

    current_wrapper_stubs = build_wrapper_stubs(current_manifest, current_wrapper_spec)
    if WRAPPER_STUBS_PATH.read_text(encoding="utf-8") != current_wrapper_stubs:
        errors.append("Generated wrapper stubs are stale; run generate_mcp_artifacts.py")

    catalog_text = TOOL_CATALOG_PATH.read_text(encoding="utf-8")
    if f"- TCP commands: {len(expected_tcp_commands)}" not in catalog_text or f"- MCP tools: {expected_mcp_count}" not in catalog_text:
        errors.append("Generated tool catalog count summary is stale; run generate_mcp_artifacts.py")
    if "CommonAIExport_WrapperSpec.json" not in catalog_text or "CommonAIExport_MCPWrapperStubs.py" not in catalog_text:
        errors.append("Generated tool catalog wrapper summary is stale; run generate_mcp_artifacts.py")

    ai_reference_text = AI_REFERENCE.read_text(encoding="utf-8")
    if AI_REFERENCE_SUMMARY_BEGIN not in ai_reference_text or AI_REFERENCE_SUMMARY_END not in ai_reference_text:
        errors.append("AI_REFERENCE.md generated tool summary block is missing; run generate_mcp_artifacts.py")
    else:
        expected_summary = build_ai_reference_tool_summary(current_manifest, current_schemas)
        if expected_summary not in ai_reference_text:
            errors.append("AI_REFERENCE.md generated tool summary block is stale; run generate_mcp_artifacts.py")

    return errors


def main() -> int:
    cpp_commands = _read_cpp_commands()
    mcp_tools = _read_mcp_tools()
    doc_counts = _read_doc_counts()

    errors: list[str] = []

    for label, items in (("C++ commands", cpp_commands), ("MCP tools", mcp_tools)):
        dupes = _duplicates(items)
        if dupes:
            errors.append(f"{label} contain duplicate names: {', '.join(dupes)}")

    cpp_set = set(cpp_commands)
    mcp_set = set(mcp_tools)

    missing_in_mcp = sorted(cpp_set - mcp_set)
    extra_in_mcp = sorted(mcp_set - cpp_set - CLIENT_ONLY_TOOLS)
    missing_client_only = sorted(CLIENT_ONLY_TOOLS - mcp_set)

    if missing_in_mcp:
        errors.append("Missing MCP wrappers: " + ", ".join(missing_in_mcp))
    if extra_in_mcp:
        errors.append("MCP wrappers without C++ commands: " + ", ".join(extra_in_mcp))
    if missing_client_only:
        errors.append("Missing client-only MCP tools: " + ", ".join(missing_client_only))

    expected_count = len(cpp_commands) + len(CLIENT_ONLY_TOOLS)
    errors.extend(_validate_generated_artifacts(cpp_commands, expected_count))

    if not doc_counts:
        errors.append("AI_REFERENCE.md has no MCP tool count markers")
    else:
        bad_doc_counts = sorted({count for count in doc_counts if count != expected_count})
        if bad_doc_counts:
            errors.append(
                "AI_REFERENCE.md count mismatch: "
                + ", ".join(str(count) for count in bad_doc_counts)
                + f" (expected {expected_count})"
            )

    if errors:
        print("CommonAIExport MCP contract validation failed:")
        for error in errors:
            print(f"  - {error}")
        print(f"C++ commands: {len(cpp_commands)}")
        print(f"MCP tools: {len(mcp_tools)}")
        print("Client-only MCP tools: " + ", ".join(sorted(CLIENT_ONLY_TOOLS)))
        if doc_counts:
            print("Doc counts: " + ", ".join(str(count) for count in doc_counts))
        return 1

    print(
        "CommonAIExport MCP contract valid: "
        f"{len(cpp_commands)} TCP commands, {len(mcp_tools)} MCP tools, docs count {expected_count}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
