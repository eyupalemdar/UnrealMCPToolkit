#!/usr/bin/env python3
"""Run the CommonAIExport MCP artifact and contract preflight checks."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
PLUGIN_ROOT = SCRIPT_DIR.parents[1]
PROJECT_ROOT = SCRIPT_DIR.parents[3]

GENERATE = SCRIPT_DIR / "generate_mcp_artifacts.py"
VALIDATE = SCRIPT_DIR / "validate_mcp_contract.py"
STATIC_TESTS = SCRIPT_DIR / "test_mcp_contract.py"
RUNTIME_SMOKE = SCRIPT_DIR / "smoke_mcp_runtime.py"
MCP_CLIENT = PLUGIN_ROOT / "MCPClient" / "ai_widget_mcp_client.py"

PY_COMPILE_TARGETS = [
    MCP_CLIENT,
    GENERATE,
    VALIDATE,
    STATIC_TESTS,
    RUNTIME_SMOKE,
]


def _run(label: str, command: list[str]) -> int:
    print(f"\n== {label} ==")
    print(" ".join(command))
    completed = subprocess.run(command, cwd=PROJECT_ROOT)
    if completed.returncode != 0:
        print(f"{label} failed with exit code {completed.returncode}")
    return completed.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--skip-generate",
        action="store_true",
        help="Validate existing generated artifacts without regenerating them first.",
    )
    parser.add_argument(
        "--runtime-smoke",
        action="store_true",
        help="Also run smoke_mcp_runtime.py against a live editor.",
    )
    parser.add_argument(
        "--mutating-smoke",
        action="store_true",
        help="When used with --runtime-smoke, run isolated mutating WBP, Blueprint graph, Material, and generic Asset smoke checks.",
    )
    args = parser.parse_args()

    steps: list[tuple[str, list[str]]] = []
    if not args.skip_generate:
        steps.append(("Generate MCP artifacts", [sys.executable, str(GENERATE)]))
    steps.extend(
        [
            ("Validate MCP contract", [sys.executable, str(VALIDATE)]),
            ("Run MCP static tests", [sys.executable, str(STATIC_TESTS)]),
            (
                "Compile Python entry points",
                [sys.executable, "-m", "py_compile", *[str(path) for path in PY_COMPILE_TARGETS]],
            ),
        ]
    )
    if args.runtime_smoke:
        runtime_command = [sys.executable, str(RUNTIME_SMOKE)]
        if args.mutating_smoke:
            runtime_command.append("--mutating-smoke")
        steps.append(("Run MCP runtime smoke", runtime_command))

    for label, command in steps:
        exit_code = _run(label, command)
        if exit_code != 0:
            return exit_code

    print("\nCommonAIExport MCP preflight passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
