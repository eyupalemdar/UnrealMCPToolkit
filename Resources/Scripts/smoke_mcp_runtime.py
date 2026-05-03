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
    return Path.cwd().resolve()


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
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall(data)
        chunks: list[bytes] = []
        while True:
            try:
                chunk = sock.recv(65536)
            except TimeoutError:
                break
            if not chunk:
                break
            chunks.append(chunk)
            if len(chunk) < 65536:
                break
    raw = b"".join(chunks).decode("utf-8")
    return json.loads(raw)


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
            return {
                "success": True,
                "status": response.status,
                "headers": dict(response.headers.items()),
                "body": json.loads(body) if body else {},
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


def run_smoke() -> dict:
    tcp_port = _read_port("AIExport_port.txt", DEFAULT_TCP_PORT)
    http_port = _read_port("AIExport_http_port.txt", 0)
    _assert(http_port > 0, "native HTTP port file missing")

    ping = _send_tcp(tcp_port, {"type": "ping"})
    _assert(bool(ping.get("success")), "TCP ping failed")

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
    _assert(tool_count == 102, f"unexpected native MCP tool count: {tool_count}")

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

    delete_session = _http_request(http_port, "/mcp", headers={"Mcp-Session-Id": session_id}, method="DELETE")
    _assert(bool(delete_session.get("success") and delete_session.get("body", {}).get("success")), "MCP session delete failed")
    after_delete = _mcp(http_port, 91, "tools/list", {}, session_id=session_id)
    _assert(after_delete.get("body", {}).get("error", {}).get("code") == -32001, "deleted session did not return -32001")

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
        "native_mcp_page_count": page_count,
        "unsupported_protocol_code": bad_protocol.get("body", {}).get("error", {}).get("code"),
        "dry_run_scope_gate": True,
        "destructive_scope_gate": True,
        "async_task_status": task_result.get("data", {}).get("status") if task_result else "",
        "session_delete_success": True,
        "audit_log_path": str(audit_path),
        "audit_line_count": len(audit_lines),
        "audit_parse_checked": min(len(audit_lines), 20),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="Print compact JSON instead of pretty JSON")
    args = parser.parse_args()

    try:
        result = run_smoke()
    except Exception as exc:
        result = {"success": False, "error": str(exc)}
        print(json.dumps(result, indent=None if args.json else 2, ensure_ascii=False))
        return 1

    print(json.dumps(result, indent=None if args.json else 2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
