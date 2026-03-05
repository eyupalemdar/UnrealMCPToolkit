#!/usr/bin/env python3
"""
Asset Export Wrapper for Claude Code

Main interface script for exporting and simplifying UE asset content.
Claude Code calls this script to get readable asset information.

Current Mode: Manual Export (user copies from UE editor)
Future Mode: Automated via AIExportCommandlet

Usage:
    # Simplify an existing raw export file
    python export_asset.py <raw_file.txt>

    # Future: Auto-export from UE (when Commandlet is ready)
    python export_asset.py --asset "/Game/Blueprints/BP_Player"

Output:
    Creates simplified file in Dev/AIExports/ folder
    Returns the path to the simplified file for Claude to read

Workflow (Current):
    1. User: Opens asset in UE editor
    2. User: Copies content (Ctrl+C on nodes/class defaults)
    3. User: Pastes to Dev/AIExports/<AssetName>_raw.txt
    4. Claude: Calls this script to simplify
    5. Claude: Reads the simplified output

Workflow (Future with Commandlet):
    1. Claude: Calls this script with --asset flag
    2. Script: Runs UE Commandlet (headless)
    3. Script: Calls simplifier on raw output
    4. Claude: Reads the simplified output
"""

import sys
import os
import subprocess
from pathlib import Path
from datetime import datetime

# Paths
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent.parent  # Up from Resources/Scripts to project root
EXPORT_DIR = PROJECT_ROOT / "Dev" / "AIExports"

# UE paths (for future Commandlet integration)
# Override via UE_ENGINE_DIR env var if needed
UE_ENGINE_PATH = Path(os.environ.get("UE_ENGINE_DIR", ""))
UE_EDITOR_CMD = UE_ENGINE_PATH / "UnrealEditor-Cmd.exe"
# Find .uproject file in project root
_uproject_files = list(PROJECT_ROOT.glob("*.uproject"))
PROJECT_FILE = _uproject_files[0] if _uproject_files else PROJECT_ROOT / "Project.uproject"


def ensure_export_dir():
    """Ensure the AIExports directory exists"""
    EXPORT_DIR.mkdir(parents=True, exist_ok=True)
    return EXPORT_DIR


def simplify_file(input_file: Path) -> Path:
    """
    Run the appropriate simplifier on a raw export file.

    Args:
        input_file: Path to the raw export file

    Returns:
        Path to the simplified output file
    """
    simplifier_script = SCRIPT_DIR / "simplify_asset.py"

    if not simplifier_script.exists():
        print(f"Error: Simplifier script not found: {simplifier_script}")
        sys.exit(1)

    print(f"Running simplifier on: {input_file}")

    result = subprocess.run(
        [sys.executable, str(simplifier_script), str(input_file)],
        capture_output=True,
        text=True
    )

    if result.returncode != 0:
        print(f"Simplifier error: {result.stderr}")
        sys.exit(1)

    # Print simplifier output
    print(result.stdout)

    # Return the simplified file path
    base_name = input_file.stem.replace('_raw', '')
    simplified_file = input_file.parent / f"{base_name}_simplified.txt"

    return simplified_file


def export_asset_commandlet(asset_path: str) -> Path:
    """
    Export an asset using the UE Commandlet (NOT YET IMPLEMENTED).

    This will be enabled once AIExportCommandlet is created.

    Args:
        asset_path: UE asset path (e.g., "/Game/Blueprints/BP_Player")

    Returns:
        Path to the raw export file
    """
    print("=" * 60)
    print("AUTOMATED EXPORT (Commandlet Mode)")
    print("=" * 60)
    print(f"Asset: {asset_path}")
    print("")

    # Check if UE editor exists
    if not UE_EDITOR_CMD.exists():
        print(f"Error: Unreal Editor not found at: {UE_EDITOR_CMD}")
        print("Please update UE_ENGINE_PATH in this script.")
        sys.exit(1)

    # Check if project file exists
    if not PROJECT_FILE.exists():
        print(f"Error: Project file not found: {PROJECT_FILE}")
        sys.exit(1)

    # TODO: Once Commandlet is implemented, run it here
    # cmd = [
    #     str(UE_EDITOR_CMD),
    #     str(PROJECT_FILE),
    #     "-run=AIExport",
    #     f"-asset={asset_path}"
    # ]
    # result = subprocess.run(cmd, capture_output=True, text=True)

    print("ERROR: Commandlet mode not yet implemented!")
    print("")
    print("Please use manual mode:")
    print("  1. Open the asset in Unreal Editor")
    print("  2. Select all nodes (Ctrl+A in graph)")
    print("  3. Copy (Ctrl+C)")
    print("  4. Paste into a text file: Dev/AIExports/<AssetName>_raw.txt")
    print("  5. Run: python export_asset.py Dev/AIExports/<AssetName>_raw.txt")
    sys.exit(1)


def print_usage():
    """Print usage information"""
    print(__doc__)
    print("")
    print("Examples:")
    print("  # Simplify a manually exported file:")
    print("  python export_asset.py Dev/AIExports/BP_Player_raw.txt")
    print("")
    print("  # Future: Auto-export (when Commandlet is ready):")
    print("  python export_asset.py --asset /Game/Blueprints/BP_Player")
    print("")
    print("Available simplifiers:")

    simplifiers = [
        ("bp_simplify.py", "Blueprint"),
        ("animbp_simplify.py", "AnimBlueprint"),
        ("widget_simplify.py", "Widget Blueprint"),
        ("dataasset_simplify.py", "DataAsset"),
        ("input_simplify.py", "Input Action/Context"),
        ("ability_simplify.py", "GameplayAbility"),
    ]

    for script, desc in simplifiers:
        script_path = SCRIPT_DIR / script
        status = "OK" if script_path.exists() else "MISSING"
        print(f"  - {desc}: {script} [{status}]")


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        print_usage()
        sys.exit(1)

    # Ensure export directory exists
    ensure_export_dir()

    # Check for --asset flag (future Commandlet mode)
    if sys.argv[1] == "--asset":
        if len(sys.argv) < 3:
            print("Error: --asset requires an asset path")
            print("Example: python export_asset.py --asset /Game/Blueprints/BP_Player")
            sys.exit(1)
        asset_path = sys.argv[2]
        raw_file = export_asset_commandlet(asset_path)
        simplified_file = simplify_file(raw_file)
    else:
        # Manual mode: input file provided
        input_file = Path(sys.argv[1])

        if not input_file.exists():
            # Try relative to export dir
            input_file = EXPORT_DIR / sys.argv[1]
            if not input_file.exists():
                print(f"Error: File not found: {sys.argv[1]}")
                sys.exit(1)

        simplified_file = simplify_file(input_file)

    # Output result for Claude Code
    print("")
    print("=" * 60)
    print("EXPORT COMPLETE")
    print("=" * 60)
    print(f"Simplified file: {simplified_file}")
    print("")
    print("Claude Code can now read this file:")
    print(f"  Read {simplified_file}")


if __name__ == "__main__":
    main()
