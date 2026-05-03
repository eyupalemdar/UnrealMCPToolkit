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
    build_ai_reference_tool_summary,
    build_command_manifest,
    build_tool_schemas,
    read_mcp_tool_signatures,
)


SCOPE_RANK = {"read": 0, "write": 1, "destructive": 2}


def _failures() -> list[str]:
    failures: list[str] = []
    manifest = build_command_manifest()
    schemas = build_tool_schemas(manifest)
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
