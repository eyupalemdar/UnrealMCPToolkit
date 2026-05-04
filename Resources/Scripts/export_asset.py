#!/usr/bin/env python3
"""
Asset export wrapper for MCP-compatible automation clients.

The script supports two workflows:

    python export_asset.py Dev/AIExports/BP_Player_raw.txt
    python export_asset.py --asset /Game/Blueprints/BP_Player

Manual mode simplifies an existing raw export file. Asset mode runs the
MCPToolkit MCTExport commandlet through UnrealEditor-Cmd and returns the
simplified export path.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Optional


SCRIPT_DIR = Path(__file__).resolve().parent
PLUGIN_ROOT = SCRIPT_DIR.parent.parent
DEFAULT_ENGINE_DIR = Path(r"C:\Program Files\Epic Games\UE_5.7")
INVALID_FILENAME_CHARS = '/\\:*?"<>|'


class ExportError(RuntimeError):
    """Raised for user-facing export failures."""


def normalize_path_arg(value: str) -> Path:
    return Path(value).expanduser()


def normalize_asset_path(asset_path: str) -> str:
    value = asset_path.strip()

    first_quote = value.find("'")
    last_quote = value.rfind("'")
    if first_quote != -1 and last_quote > first_quote:
        value = value[first_quote + 1:last_quote].strip()

    if len(value) >= 2 and value[0] in ("'", '"') and value[-1] == value[0]:
        value = value[1:-1].strip()

    return value


def sanitize_file_name(name: str) -> str:
    result = name
    for char in INVALID_FILENAME_CHARS:
        result = result.replace(char, "_")
    return result


def asset_file_base_name(asset_path: str) -> str:
    cleaned = normalize_asset_path(asset_path)
    slash_index = max(cleaned.rfind("/"), cleaned.rfind("\\"))
    dot_index = cleaned.rfind(".")

    if dot_index > slash_index:
        name = cleaned[dot_index + 1:]
    elif slash_index != -1:
        name = cleaned[slash_index + 1:]
    else:
        name = cleaned

    if not name:
        raise ExportError(f"Could not infer asset name from path: {asset_path}")

    return sanitize_file_name(name)


def find_project_file(project_arg: Optional[str], required: bool = True) -> Optional[Path]:
    search_roots: List[Path] = []

    if project_arg:
        requested = normalize_path_arg(project_arg)
        if requested.suffix.lower() == ".uproject":
            project_file = requested.resolve()
            if not project_file.exists():
                raise ExportError(f"Project file not found: {project_file}")
            return project_file

        requested_dir = requested.resolve()
        if not requested_dir.exists() or not requested_dir.is_dir():
            raise ExportError(f"Project directory not found: {requested_dir}")
        search_roots.append(requested_dir)
    else:
        search_roots.extend([Path.cwd().resolve(), SCRIPT_DIR, *SCRIPT_DIR.parents])

    for root in search_roots:
        try:
            matches = sorted(root.glob("*.uproject"))
        except OSError:
            continue
        if matches:
            return matches[0].resolve()

    if required:
        raise ExportError(
            "Project file not found. Pass --project with a .uproject path or project root."
        )
    return None


def editor_candidates_from_root(root: Path) -> Iterable[Path]:
    if root.name.lower() == "unrealeditor-cmd.exe":
        yield root
        return

    yield root / "Engine" / "Binaries" / "Win64" / "UnrealEditor-Cmd.exe"
    yield root / "Binaries" / "Win64" / "UnrealEditor-Cmd.exe"


def find_editor_cmd(editor_cmd_arg: Optional[str], engine_dir_arg: Optional[str]) -> Path:
    if editor_cmd_arg:
        editor_cmd = normalize_path_arg(editor_cmd_arg).resolve()
        if not editor_cmd.exists():
            raise ExportError(f"UnrealEditor-Cmd not found: {editor_cmd}")
        return editor_cmd

    roots: List[Path] = []
    if engine_dir_arg:
        roots.append(normalize_path_arg(engine_dir_arg))

    for env_name in ("UE_ENGINE_DIR", "UE_ROOT"):
        env_value = os.environ.get(env_name)
        if env_value:
            roots.append(normalize_path_arg(env_value))

    roots.append(DEFAULT_ENGINE_DIR)

    checked: List[Path] = []
    for root in roots:
        for candidate in editor_candidates_from_root(root.expanduser()):
            candidate = candidate.resolve()
            checked.append(candidate)
            if candidate.exists():
                return candidate

    checked_text = "\n  ".join(str(path) for path in checked)
    raise ExportError(
        "UnrealEditor-Cmd.exe not found. Pass --editor-cmd or --engine-dir.\n"
        f"Checked:\n  {checked_text}"
    )


def resolve_output_dir(output_arg: Optional[str], project_file: Path) -> Path:
    if output_arg:
        output_dir = normalize_path_arg(output_arg)
        if not output_dir.is_absolute():
            output_dir = project_file.parent / output_dir
    else:
        output_dir = project_file.parent / "Dev" / "AIExports"

    output_dir = output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    return output_dir


def default_manual_export_dirs() -> List[Path]:
    dirs: List[Path] = []

    project_file = find_project_file(None, required=False)
    if project_file:
        dirs.append(project_file.parent / "Dev" / "AIExports")

    dirs.append(PLUGIN_ROOT / "Dev" / "AIExports")
    return dirs


def resolve_manual_input(raw_file_arg: str) -> Path:
    raw_file = normalize_path_arg(raw_file_arg)
    candidates = [raw_file]

    if not raw_file.is_absolute():
        candidates.append(Path.cwd() / raw_file)
        for export_dir in default_manual_export_dirs():
            candidates.append(export_dir / raw_file)

    for candidate in candidates:
        candidate = candidate.resolve()
        if candidate.exists():
            return candidate

    checked = "\n  ".join(str(path.resolve()) for path in candidates)
    raise ExportError(f"Input file not found: {raw_file_arg}\nChecked:\n  {checked}")


def simplify_file(input_file: Path) -> Path:
    simplifier_script = SCRIPT_DIR / "simplify_asset.py"
    if not simplifier_script.exists():
        raise ExportError(f"Simplifier script not found: {simplifier_script}")

    print(f"Running simplifier on: {input_file}")
    result = subprocess.run(
        [sys.executable, str(simplifier_script), str(input_file)],
        capture_output=True,
        text=True,
    )

    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
    if result.stderr:
        print(result.stderr, end="" if result.stderr.endswith("\n") else "\n", file=sys.stderr)

    if result.returncode != 0:
        raise ExportError(f"Simplifier failed with exit code {result.returncode}")

    stem = input_file.stem
    base_name = stem[:-4] if stem.endswith("_raw") else stem
    simplified_file = input_file.parent / f"{base_name}_simplified.txt"
    if not simplified_file.exists():
        raise ExportError(f"Simplifier completed but output was not found: {simplified_file}")
    return simplified_file.resolve()


def parse_exported_paths(output_text: str) -> Dict[str, Path]:
    paths: Dict[str, Path] = {}
    pattern = re.compile(r"\bto:\s*(.+?_(raw|simplified)\.txt)\s*$", re.IGNORECASE)

    for line in output_text.splitlines():
        match = pattern.search(line.strip())
        if not match:
            continue

        path = match.group(1).strip().strip('"')
        kind = match.group(2).lower()
        paths[kind] = Path(path).resolve()

    return paths


def expected_export_paths(output_dir: Path, asset_path: str) -> Dict[str, Path]:
    base_name = asset_file_base_name(asset_path)
    return {
        "raw": output_dir / f"{base_name}_raw.txt",
        "simplified": output_dir / f"{base_name}_simplified.txt",
    }


def merge_existing_paths(
    parsed_paths: Dict[str, Path],
    expected_paths: Dict[str, Path],
) -> Dict[str, Path]:
    result = dict(parsed_paths)

    for kind, path in expected_paths.items():
        if kind not in result and path.exists():
            result[kind] = path.resolve()

    for kind, path in list(result.items()):
        if not path.exists():
            result.pop(kind)

    return result


def commandlet_mode_switch(mode: str) -> str:
    normalized_mode = "simplify" if mode == "simplified" else mode
    return {
        "raw": "-raw",
        "simplify": "-simplify",
        "both": "-both",
    }[normalized_mode]


def run_commandlet(
    editor_cmd: Path,
    project_file: Path,
    asset_path: str,
    output_dir: Path,
    mode: str,
    extra_ue_args: List[str],
) -> Dict[str, Path]:
    normalized_asset_path = normalize_asset_path(asset_path)
    command = [
        str(editor_cmd),
        str(project_file),
        "-run=MCTExport",
        f"-asset={normalized_asset_path}",
        f"-output={output_dir}",
        commandlet_mode_switch(mode),
        "-nullrhi",
        "-unattended",
        "-nosplash",
        "-nopause",
    ]
    command.extend(extra_ue_args)

    print("=" * 60)
    print("AIEXPORT COMMANDLET")
    print("=" * 60)
    print(f"Editor:  {editor_cmd}")
    print(f"Project: {project_file}")
    print(f"Asset:   {normalized_asset_path}")
    print(f"Output:  {output_dir}")
    print(f"Mode:    {mode}")

    result = subprocess.run(command, capture_output=True, text=True)
    combined_output = "\n".join(part for part in (result.stdout, result.stderr) if part)

    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
    if result.stderr:
        print(result.stderr, end="" if result.stderr.endswith("\n") else "\n", file=sys.stderr)

    if result.returncode != 0:
        raise ExportError(f"MCTExport commandlet failed with exit code {result.returncode}")

    parsed_paths = parse_exported_paths(combined_output)
    expected_paths = expected_export_paths(output_dir, normalized_asset_path)
    return merge_existing_paths(parsed_paths, expected_paths)


def export_asset(args: argparse.Namespace) -> Path:
    project_file = find_project_file(args.project)
    editor_cmd = find_editor_cmd(args.editor_cmd, args.engine_dir)
    output_dir = resolve_output_dir(args.output_dir, project_file)
    mode = args.mode

    exported_paths = run_commandlet(
        editor_cmd=editor_cmd,
        project_file=project_file,
        asset_path=args.asset,
        output_dir=output_dir,
        mode=mode,
        extra_ue_args=args.ue_arg or [],
    )

    raw_file = exported_paths.get("raw")
    simplified_file = exported_paths.get("simplified")

    if mode == "raw":
        if not raw_file:
            raise ExportError("Commandlet completed but did not produce a raw export file")
        simplified_file = simplify_file(raw_file)
    elif not simplified_file and raw_file:
        simplified_file = simplify_file(raw_file)

    if not simplified_file or not simplified_file.exists():
        raise ExportError("Export completed but no simplified file was produced")

    return simplified_file.resolve()


def print_available_simplifiers() -> None:
    simplifiers = [
        ("bp_simplify.py", "Blueprint"),
        ("animbp_simplify.py", "AnimBlueprint"),
        ("widget_simplify.py", "Widget Blueprint"),
        ("dataasset_simplify.py", "DataAsset"),
        ("input_simplify.py", "Input Action/Context"),
        ("ability_simplify.py", "GameplayAbility"),
        ("material_simplify.py", "Material"),
    ]

    print("\nAvailable simplifiers:")
    for script, description in simplifiers:
        script_path = SCRIPT_DIR / script
        status = "OK" if script_path.exists() else "MISSING"
        print(f"  - {description}: {script} [{status}]")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Export Unreal assets through MCPToolkit or simplify an existing raw export.",
    )
    parser.add_argument(
        "raw_file",
        nargs="?",
        help="Existing *_raw.txt file to simplify in manual mode.",
    )
    parser.add_argument(
        "--asset",
        help="Unreal asset path to export, for example /Game/Blueprints/BP_Player.",
    )
    parser.add_argument(
        "--project",
        help="Host .uproject file or project root. Auto-detected from parent directories when omitted.",
    )
    parser.add_argument(
        "--engine-dir",
        help="Unreal Engine install root or Engine directory. Defaults to UE_ENGINE_DIR, UE_ROOT, then UE_5.7.",
    )
    parser.add_argument(
        "--editor-cmd",
        help="Explicit path to UnrealEditor-Cmd.exe.",
    )
    parser.add_argument(
        "--output-dir",
        "--output",
        dest="output_dir",
        help="Output directory. Relative paths are resolved under the host project root.",
    )
    parser.add_argument(
        "--mode",
        choices=("both", "raw", "simplify", "simplified"),
        default="both",
        help="Commandlet output mode. Default: both.",
    )
    parser.add_argument(
        "--ue-arg",
        action="append",
        default=[],
        help="Additional Unreal command-line argument. Repeat for multiple arguments.",
    )
    parser.add_argument(
        "--list-simplifiers",
        action="store_true",
        help="List simplifier script availability and exit.",
    )
    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()

    if args.list_simplifiers:
        print_available_simplifiers()
        return 0

    try:
        if args.asset:
            simplified_file = export_asset(args)
        elif args.raw_file:
            raw_file = resolve_manual_input(args.raw_file)
            simplified_file = simplify_file(raw_file)
        else:
            parser.print_help()
            print_available_simplifiers()
            return 1

        print("")
        print("=" * 60)
        print("EXPORT COMPLETE")
        print("=" * 60)
        print(f"Simplified file: {simplified_file}")
        return 0
    except ExportError as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
