#!/usr/bin/env python3
"""Behavioral tests for MCPToolkit multi-editor client routing."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import threading
import time
import types
import unittest


PLUGIN_ROOT = Path(__file__).resolve().parents[2]
CLIENT_PATH = PLUGIN_ROOT / "MCPClient" / "ai_widget_mcp_client.py"


class _DummyFastMCP:
    def __init__(self, *args, **kwargs) -> None:
        pass

    @staticmethod
    def _decorator(*args, **kwargs):
        def decorate(function):
            return function

        return decorate

    tool = _decorator
    resource = _decorator
    prompt = _decorator


sys.modules["fastmcp"] = types.SimpleNamespace(FastMCP=_DummyFastMCP)
spec = importlib.util.spec_from_file_location("mct_routing_client_under_test", CLIENT_PATH)
if spec is None or spec.loader is None:
    raise RuntimeError(f"Could not load MCP client from {CLIENT_PATH}")
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


class EditorRoutingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.originals = {
            name: getattr(client, name)
            for name in (
                "_read_registry_entries",
                "_probe_editor",
                "_list_editors",
                "_resolve_editor",
                "_send_command_to_port",
            )
        }

    def tearDown(self) -> None:
        for name, value in self.originals.items():
            setattr(client, name, value)

    def test_exact_editor_id_probes_only_matching_registry_entry(self) -> None:
        entries = [
            {
                "editor_id": "Target-100-55560",
                "port": 55560,
                "project_dir": "D:/ProjectTarget",
            },
            *[
                {
                    "editor_id": f"Stale-{index}-5556{index}",
                    "port": 55561 + index,
                    "project_dir": "D:/ProjectOther",
                }
                for index in range(6)
            ],
        ]
        probed_ports: list[int] = []

        client._read_registry_entries = lambda: entries

        def probe(entry: dict) -> dict:
            probed_ports.append(int(entry["port"]))
            return {**entry, "alive": True, "status": "alive"}

        client._probe_editor = probe
        client._list_editors = lambda include_stale=False: self.fail("exact id unexpectedly scanned all editors")

        target, error = client._resolve_editor(editor_id="Target-100-55560")

        self.assertIsNone(error)
        self.assertEqual(target["port"], 55560)
        self.assertEqual(probed_ports, [55560])

    def test_explicit_port_must_match_requested_editor_id(self) -> None:
        client._probe_editor = lambda entry: {
            **entry,
            "editor_id": "Different-200-55560",
            "project_dir": "D:/Different",
            "alive": True,
            "status": "alive",
        }

        target, error = client._resolve_editor(editor_id="Expected-100-55560", port=55560)

        self.assertIsNone(target)
        self.assertIn("different MCPToolkit editor_id", error["error"])

    def test_registry_listing_probes_entries_concurrently_and_preserves_order(self) -> None:
        entries = [
            {"editor_id": f"Editor-{index}", "port": 55560 + index}
            for index in range(8)
        ]
        active_count = 0
        maximum_active = 0
        lock = threading.Lock()

        client._read_registry_entries = lambda: entries

        def probe(entry: dict) -> dict:
            nonlocal active_count, maximum_active
            with lock:
                active_count += 1
                maximum_active = max(maximum_active, active_count)
            time.sleep(0.02)
            with lock:
                active_count -= 1
            return {**entry, "alive": True, "status": "alive"}

        client._probe_editor = probe
        editors = client._list_editors(include_stale=True)

        self.assertGreater(maximum_active, 1)
        self.assertEqual([editor["port"] for editor in editors], [entry["port"] for entry in entries])

    def test_editor_call_many_resolves_once_and_stops_on_error(self) -> None:
        resolve_count = 0
        sent: list[tuple[int, str, dict | None, dict | None]] = []

        def resolve(editor_id: str, project_dir: str, port: int):
            nonlocal resolve_count
            resolve_count += 1
            return {
                "editor_id": "Target-100-55560",
                "project_name": "Target",
                "project_dir": "D:/Target",
                "port": 55560,
                "pid": 100,
            }, None

        def send(port: int, command: str, params: dict | None = None, meta: dict | None = None) -> dict:
            sent.append((port, command, params, meta))
            if command == "second":
                return {"success": False, "error": "expected failure"}
            return {"success": True, "data": {"command": command}}

        client._resolve_editor = resolve
        client._send_command_to_port = send

        result = json.loads(client.editor_call_many(
            commands=[
                {"command": "first", "params": {"value": 1}},
                {"command": "second", "scope": "write"},
                {"command": "third"},
            ],
            editor_id="Target-100-55560",
            scope="read",
            stop_on_error=True,
        ))

        self.assertEqual(resolve_count, 1)
        self.assertEqual([call[1] for call in sent], ["first", "second"])
        self.assertEqual(sent[0][3], {"scope": "read"})
        self.assertEqual(sent[1][3], {"scope": "write"})
        self.assertFalse(result["success"])
        self.assertTrue(result["stopped_early"])
        self.assertEqual(result["executed_count"], 2)
        self.assertFalse(result["transactional"])


def _run_live_measurement(editor_id: str) -> int:
    resolve_started = time.monotonic()
    target, error = client._resolve_editor(editor_id=editor_id)
    resolve_elapsed_ms = int((time.monotonic() - resolve_started) * 1000)

    list_started = time.monotonic()
    editors = client._list_editors(include_stale=True)
    list_elapsed_ms = int((time.monotonic() - list_started) * 1000)

    batch_started = time.monotonic()
    batch = json.loads(client.editor_call_many(
        commands=[
            {"command": "server_status"},
            {"command": "editor_identity"},
        ],
        editor_id=editor_id,
        scope="read",
    ))
    batch_elapsed_ms = int((time.monotonic() - batch_started) * 1000)

    payload = {
        "success": error is None and target is not None and batch.get("success") is True,
        "requested_editor_id": editor_id,
        "resolve_elapsed_ms": resolve_elapsed_ms,
        "resolved_target": target,
        "resolve_error": error,
        "list_elapsed_ms": list_elapsed_ms,
        "editor_count": len(editors),
        "alive_count": sum(1 for editor in editors if editor.get("alive")),
        "stale_count": sum(1 for editor in editors if not editor.get("alive")),
        "probe_elapsed_ms": [editor.get("probe_elapsed_ms", 0) for editor in editors],
        "batch_elapsed_ms": batch_elapsed_ms,
        "batch_requested_count": batch.get("requested_count"),
        "batch_executed_count": batch.get("executed_count"),
        "batch_failure_count": batch.get("failure_count"),
    }
    print(json.dumps(payload, indent=2))
    return 0 if payload["success"] else 1


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--live-editor-id":
        raise SystemExit(_run_live_measurement(sys.argv[2]))
    unittest.main()
