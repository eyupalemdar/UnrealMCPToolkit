#!/usr/bin/env python3
"""
Recursive Export Test: B_ShooterGame_Elimination

BFS-walks all /Game/ asset references starting from a root blueprint,
exports each via the MCPToolkit TCP server, and validates outputs.

Usage:
    MSYS_NO_PATHCONV=1 py -3 test_recursive_export.py
    MSYS_NO_PATHCONV=1 py -3 test_recursive_export.py /Game/Other/Root
    MSYS_NO_PATHCONV=1 py -3 test_recursive_export.py --max 20
"""

import re
import sys
import time
import os
from collections import deque
from pathlib import Path

# Import from sibling module
sys.path.insert(0, str(Path(__file__).parent))
from ai_export_client import send_command, get_port, find_port_file_upward

# --- Config ---
DEFAULT_ROOT = "/ShooterCore/Experiences/B_ShooterGame_Elimination"
MAX_ASSETS = 50

# --- Reference extraction regexes ---
# Quoted content-path references: '/Game/...', '/ShooterCore/...', etc.
QUOTED_CONTENT_REF = re.compile(r"'(/(?:Game|[A-Z][A-Za-z0-9_]+)/[^']+)'")
# Bare content-path references (after = sign, in property values, etc.)
BARE_CONTENT_REF = re.compile(r"/(Game|[A-Z][A-Za-z0-9_]+)/[A-Za-z0-9_/.\-]+")
# Paths to SKIP — engine/script refs and engine-only mount points
SKIP_PREFIXES = ("/Script/", "/Engine/", "/Temp/", "/Memory/")


def normalize_asset_path(raw_path: str) -> str:
    """
    Normalize a raw UE asset reference to a clean package path.
    Strips .ObjectName suffix and _C class suffix.
    """
    path = raw_path.strip().strip("'\"")

    # Remove .ObjectName (e.g. /Game/Foo/Bar.Bar_C -> /Game/Foo/Bar)
    if "." in path:
        path = path.split(".")[0]

    # Remove trailing _C (class suffix)
    if path.endswith("_C"):
        path = path[:-2]

    # Remove trailing : and anything after (subobject refs)
    if ":" in path:
        path = path.split(":")[0]

    return path


def should_skip_ref(path: str) -> bool:
    """Check if a reference should be skipped (engine class, too short, etc.)."""
    for prefix in SKIP_PREFIXES:
        if path.startswith(prefix):
            return True
    # Must have at least 3 segments: /<Mount>/<Dir>/<Asset> (e.g. /ShooterCore/Game/B_Hero)
    # Paths with only 2 segments like /ShooterCore/Elimination are directory fragments, not assets
    parts = path.strip("/").split("/")
    if len(parts) < 3:
        return True
    return False


def extract_references(raw_text: str) -> set:
    """
    Extract unique content asset references from raw export text.
    Handles /Game/, /ShooterCore/, and other plugin mount points.
    Skips /Script/, /Engine/ and other non-exportable references.
    """
    refs = set()

    # Quoted references: '/<MountPoint>/Path/Asset.Object'
    for match in QUOTED_CONTENT_REF.finditer(raw_text):
        full_path = match.group(1)
        if not should_skip_ref(full_path):
            refs.add(normalize_asset_path(full_path))

    # Bare references: /<MountPoint>/Path/Asset
    for match in BARE_CONTENT_REF.finditer(raw_text):
        full_path = match.group(0)
        # Clean trailing punctuation that regex might grab
        full_path = full_path.rstrip(".,;)>\"'")
        if not should_skip_ref(full_path):
            refs.add(normalize_asset_path(full_path))

    return refs


def export_asset(asset_path: str, port: int) -> dict:
    """
    Export a single asset via TCP and return result info.

    Returns dict with keys:
        success, skipped, error,
        raw_file, stripped_file, simplified_file,
        raw_size, stripped_size, simplified_size
    """
    result = {
        "asset_path": asset_path,
        "success": False,
        "skipped": False,
        "error": None,
        "raw_file": None,
        "stripped_file": None,
        "simplified_file": None,
        "raw_size": 0,
        "stripped_size": 0,
        "simplified_size": 0,
    }

    response = send_command(
        "export_blueprint",
        params={"asset_path": asset_path, "both_formats": True},
        port=port,
    )

    if not response.get("success"):
        err = response.get("error", "Unknown error")
        # Unsupported types are skipped, not failed
        skip_keywords = [
            "not supported",
            "unsupported",
            "not a blueprint",
            "StaticMesh",
            "SkeletalMesh",
            "Texture",
            "Material",
            "not found",
            "does not exist",
            "Failed to load",
            "Sound",
            "Animation",
            "Skeleton",
            "Physics",
            "Niagara",
            "cannot export",
        ]
        if any(kw.lower() in err.lower() for kw in skip_keywords):
            result["skipped"] = True
            result["error"] = err
        else:
            result["error"] = err
        return result

    data = response.get("data", {})
    result["success"] = True
    result["raw_file"] = data.get("raw_file")
    result["stripped_file"] = data.get("stripped_file")
    result["simplified_file"] = data.get("simplified_file")

    # Measure file sizes
    for key in ("raw_file", "stripped_file", "simplified_file"):
        fpath = result[key]
        if fpath and os.path.isfile(fpath):
            result[key.replace("file", "size")] = os.path.getsize(fpath)

    return result


def validate_export(result: dict) -> list:
    """
    Validate a successful export result. Returns list of error strings.
    """
    errors = []
    asset = result["asset_path"]

    # Raw file must exist and be non-empty
    raw = result.get("raw_file")
    if not raw or not os.path.isfile(raw):
        errors.append(f"{asset}: raw file missing")
    elif result["raw_size"] == 0:
        errors.append(f"{asset}: raw file is empty")

    # Simplified file must exist and be non-empty
    simp = result.get("simplified_file")
    if not simp or not os.path.isfile(simp):
        errors.append(f"{asset}: simplified file missing")
    elif result["simplified_size"] == 0:
        errors.append(f"{asset}: simplified file is empty")

    # Stripped file (optional — may not always be produced)
    stripped = result.get("stripped_file")
    if stripped and os.path.isfile(stripped):
        if result["stripped_size"] == 0:
            errors.append(f"{asset}: stripped file is empty")
        # Allow small overhead (headers/formatting) — only flag if >25% larger AND >1KB bigger
        str_over = result["stripped_size"] - result["raw_size"]
        if str_over > 1024 and result["raw_size"] > 0 and str_over > result["raw_size"] * 0.25:
            errors.append(
                f"{asset}: stripped ({result['stripped_size']}) >> raw ({result['raw_size']})"
            )

    # Simplified should be smaller than raw (same tolerance)
    sim_over = result["simplified_size"] - result["raw_size"]
    if sim_over > 1024 and result["raw_size"] > 0 and sim_over > result["raw_size"] * 0.25:
        errors.append(
            f"{asset}: simplified ({result['simplified_size']}) >> raw ({result['raw_size']})"
        )

    # Check for error content in files
    for key in ("raw_file", "simplified_file", "stripped_file"):
        fpath = result.get(key)
        if fpath and os.path.isfile(fpath):
            try:
                with open(fpath, "r", encoding="utf-8", errors="replace") as f:
                    head = f.read(500)
                if head.startswith("Error:"):
                    errors.append(f"{asset}: {key} starts with 'Error:'")
            except Exception:
                pass

    return errors


def short_name(asset_path: str) -> str:
    """Get short display name from asset path."""
    return asset_path.rsplit("/", 1)[-1] if "/" in asset_path else asset_path


def format_size(size: int) -> str:
    """Format byte size with commas."""
    return f"{size:,}"


def pct_change(old: int, new: int) -> str:
    """Format percentage change."""
    if old == 0:
        return "N/A"
    change = ((new - old) / old) * 100
    return f"{change:+.1f}%"


def print_report(results: list, all_errors: list, elapsed: float):
    """Print the final test report."""
    exported = [r for r in results if r["success"]]
    skipped = [r for r in results if r["skipped"]]
    failed = [r for r in results if not r["success"] and not r["skipped"]]

    root_name = short_name(results[0]["asset_path"]) if results else "?"

    print()
    print("=" * 72)
    print("  RECURSIVE EXPORT TEST REPORT")
    print("=" * 72)
    print(f"  Root:     {root_name}")
    print(f"  Exported: {len(exported)} | Skipped: {len(skipped)} | Failed: {len(failed)}")
    print(f"  Time:     {elapsed:.1f}s")
    print()

    # Size comparison table
    if exported:
        print("=" * 72)
        print("  SIZE COMPARISON")
        print("=" * 72)

        # Header
        name_w = max(len(short_name(r["asset_path"])) for r in exported)
        name_w = max(name_w, 5)  # minimum
        name_w = min(name_w, 35)  # cap

        hdr = (
            f"  {'Asset':<{name_w}} | {'Raw':>9} | {'Stripped':>9} | "
            f"{'Simplified':>10} | {'Raw>Str':>8} | {'Raw>Sim':>8}"
        )
        print(hdr)
        print("  " + "-" * (len(hdr) - 2))

        total_raw = 0
        total_str = 0
        total_sim = 0

        for r in exported:
            name = short_name(r["asset_path"])[:name_w]
            raw_s = r["raw_size"]
            str_s = r["stripped_size"]
            sim_s = r["simplified_size"]
            total_raw += raw_s
            total_str += str_s
            total_sim += sim_s

            row = (
                f"  {name:<{name_w}} | {format_size(raw_s):>9} | "
                f"{format_size(str_s):>9} | {format_size(sim_s):>10} | "
                f"{pct_change(raw_s, str_s):>8} | {pct_change(raw_s, sim_s):>8}"
            )
            print(row)

        print("  " + "-" * (len(hdr) - 2))
        row = (
            f"  {'TOTALS':<{name_w}} | {format_size(total_raw):>9} | "
            f"{format_size(total_str):>9} | {format_size(total_sim):>10} | "
            f"{pct_change(total_raw, total_str):>8} | {pct_change(total_raw, total_sim):>8}"
        )
        print(row)
        print()

    # Skipped assets
    if skipped:
        print("=" * 72)
        print(f"  SKIPPED ({len(skipped)})")
        print("=" * 72)
        for r in skipped:
            print(f"  {short_name(r['asset_path'])}: {r['error']}")
        print()

    # Failed assets
    if failed:
        print("=" * 72)
        print(f"  FAILED ({len(failed)})")
        print("=" * 72)
        for r in failed:
            print(f"  {short_name(r['asset_path'])}: {r['error']}")
        print()

    # Validation
    print("=" * 72)
    print("  VALIDATION")
    print("=" * 72)
    if all_errors:
        print(f"  Issues found: {len(all_errors)}")
        for e in all_errors:
            print(f"    - {e}")
    else:
        print("  All outputs valid: YES")
    print()


def run_recursive_export(root_path: str, max_assets: int):
    """BFS recursive export starting from root_path."""
    port = get_port()

    # Verify server is reachable
    ping = send_command("ping", port=port)
    if not ping.get("success"):
        print(f"ERROR: Cannot reach TCP server on port {port}")
        print(f"  {ping.get('error', 'Unknown error')}")
        print("  Is Unreal Editor running with MCPToolkit plugin?")
        sys.exit(1)

    server_info = ping.get("data", {})
    print(f"Connected to {server_info.get('server', 'MCTExport')} on port {server_info.get('port', port)}")
    print(f"Root asset: {root_path}")
    print(f"Max assets: {max_assets}")
    print()

    queue = deque([root_path])
    visited = set()
    results = []
    all_errors = []
    start_time = time.time()

    while queue and len(visited) < max_assets:
        asset_path = queue.popleft()

        if asset_path in visited:
            continue
        visited.add(asset_path)

        idx = len(results) + 1
        name = short_name(asset_path)
        print(f"[{idx}/{max_assets}] Exporting: {name} ...", end=" ", flush=True)

        result = export_asset(asset_path, port)
        results.append(result)

        if result["success"]:
            print(
                f"OK  (raw={format_size(result['raw_size'])}, "
                f"sim={format_size(result['simplified_size'])})"
            )

            # Validate
            errs = validate_export(result)
            all_errors.extend(errs)

            # Extract references from raw file for BFS
            raw_file = result.get("raw_file")
            if raw_file and os.path.isfile(raw_file):
                try:
                    with open(raw_file, "r", encoding="utf-8", errors="replace") as f:
                        raw_text = f.read()
                    refs = extract_references(raw_text)
                    new_refs = refs - visited
                    for ref in sorted(new_refs):
                        if ref not in visited:
                            queue.append(ref)
                    if new_refs:
                        print(f"         +{len(new_refs)} new references queued")
                except Exception as e:
                    print(f"         Warning: could not read raw file: {e}")

        elif result["skipped"]:
            print(f"SKIP ({result['error']})")
        else:
            print(f"FAIL ({result['error']})")

    elapsed = time.time() - start_time
    print_report(results, all_errors, elapsed)

    # Exit code
    failed_count = sum(1 for r in results if not r["success"] and not r["skipped"])
    return 1 if failed_count > 0 or all_errors else 0


def main():
    root = DEFAULT_ROOT
    max_assets = MAX_ASSETS

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--max" and i + 1 < len(args):
            max_assets = int(args[i + 1])
            i += 2
        elif not args[i].startswith("-"):
            root = args[i]
            i += 1
        else:
            print(f"Unknown option: {args[i]}")
            print(__doc__)
            sys.exit(1)

    sys.exit(run_recursive_export(root, max_assets))


if __name__ == "__main__":
    main()
