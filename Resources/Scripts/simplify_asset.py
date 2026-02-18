#!/usr/bin/env python3
"""
Asset Simplifier Dispatcher

Detects the asset type from raw export content and calls the appropriate
simplifier script. This enables a unified interface for all asset types.

Usage:
    python simplify_asset.py <input_file> [--output <output_file>]

Supported asset types:
    - Blueprint (bp_simplify.py)
    - AnimBlueprint (animbp_simplify.py)
    - WidgetBlueprint (widget_simplify.py)
    - DataAsset (dataasset_simplify.py)
    - InputAction/InputMappingContext (input_simplify.py)
    - GameplayAbility (ability_simplify.py)
"""

import sys
import os
import subprocess
from pathlib import Path

# Script directory
SCRIPT_DIR = Path(__file__).parent

# Asset type to simplifier script mapping
SIMPLIFIERS = {
    'animbp': 'animbp_simplify.py',
    'widget': 'widget_simplify.py',
    'blueprint': 'bp_simplify.py',
    'dataasset': 'dataasset_simplify.py',
    'input': 'input_simplify.py',
    'ability': 'ability_simplify.py',
    'material': 'material_simplify.py',
}


def detect_asset_type(file_path: str) -> str:
    """
    Detect the asset type from the raw file content.

    Reads the first portion of the file to identify key markers
    that indicate the asset type. Supports both:
    - UE copy/paste format (Begin Object...End Object)
    - C++ commandlet export format (=== BLUEPRINT: === headers)
    """
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            # Read first 5KB - usually enough for type detection
            content = f.read(5000)
    except Exception as e:
        print(f"Error reading file for type detection: {e}")
        return 'blueprint'  # Default fallback

    # ========================================
    # C++ Export Format Detection (=== HEADER ===)
    # Check these first as they are more explicit
    # ========================================

    if '=== ANIM BLUEPRINT:' in content:
        return 'animbp'

    if '=== WIDGET BLUEPRINT:' in content:
        return 'widget'

    if '=== INPUT ACTION:' in content or '=== INPUT MAPPING CONTEXT:' in content:
        return 'input'

    if '=== DATA ASSET:' in content:
        return 'dataasset'

    # Physical Material (check before Material — same property format as DataAsset)
    if '=== PHYSICAL MATERIAL:' in content:
        return 'dataasset'

    # Material detection (check before generic Blueprint)
    if '=== MATERIAL:' in content or '=== MATERIAL INSTANCE' in content:
        return 'material'

    # Generic Blueprint with C++ format (covers all Blueprint-derived classes)
    if '=== BLUEPRINT:' in content:
        return 'blueprint'

    # ========================================
    # UE Copy/Paste Format Detection
    # ========================================

    # Material markers (check before AnimBlueprint since both can have expressions)
    if 'MaterialDomain' in content and 'MaterialGraphNode' in content:
        return 'material'

    if 'MaterialExpression' in content and 'BlendMode' in content:
        return 'material'

    # AnimBlueprint markers
    if 'AnimBlueprintGeneratedClass' in content or 'AnimGraph' in content:
        return 'animbp'

    # Widget Blueprint markers
    if 'WidgetBlueprintGeneratedClass' in content or 'WidgetTree' in content:
        return 'widget'

    # GameplayAbility markers (check before generic Blueprint)
    if 'GameplayAbility' in content or 'K2Node_LatentAbilityCall' in content:
        if 'GA_' in file_path or 'Ability' in file_path:
            return 'ability'

    # Input asset markers
    if 'InputAction' in content or 'InputMappingContext' in content:
        if 'IA_' in file_path or 'IMC_' in file_path:
            return 'input'

    # DataAsset markers (check before generic Blueprint)
    if 'UDataAsset' in content or ('DataAsset' in content and 'BlueprintGeneratedClass' not in content):
        return 'dataasset'

    # Generic Blueprint (fallback for any Blueprint type)
    if 'BlueprintGeneratedClass' in content or 'Begin Object' in content:
        return 'blueprint'

    # Default to blueprint simplifier
    return 'blueprint'


def get_simplifier_path(asset_type: str) -> Path:
    """Get the full path to the simplifier script for the given asset type."""
    script_name = SIMPLIFIERS.get(asset_type, 'bp_simplify.py')
    return SCRIPT_DIR / script_name


def run_simplifier(input_file: str, asset_type: str, output_file: str = None) -> bool:
    """
    Run the appropriate simplifier script for the detected asset type.

    Returns True if successful, False otherwise.
    """
    simplifier_path = get_simplifier_path(asset_type)

    if not simplifier_path.exists():
        print(f"Warning: Simplifier script not found: {simplifier_path}")
        print(f"Falling back to bp_simplify.py")
        simplifier_path = SCRIPT_DIR / 'bp_simplify.py'

        if not simplifier_path.exists():
            print(f"Error: No simplifier scripts found!")
            return False

    # Build command
    cmd = [sys.executable, str(simplifier_path), input_file]

    print(f"Detected asset type: {asset_type}")
    print(f"Using simplifier: {simplifier_path.name}")
    print(f"Running: {' '.join(cmd)}")
    print("-" * 40)

    try:
        result = subprocess.run(cmd, capture_output=False, text=True)
        return result.returncode == 0
    except Exception as e:
        print(f"Error running simplifier: {e}")
        return False


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        print(__doc__)
        print("\nAvailable simplifiers:")
        for asset_type, script in SIMPLIFIERS.items():
            script_path = SCRIPT_DIR / script
            status = "OK" if script_path.exists() else "MISSING"
            print(f"  {asset_type}: {script} [{status}]")
        sys.exit(1)

    input_file = sys.argv[1]

    # Check if input file exists
    if not os.path.exists(input_file):
        print(f"Error: Input file not found: {input_file}")
        sys.exit(1)

    # If C++ pipeline passes a _temp_raw.txt (filtered defaults), replace its
    # contents with the full _raw.txt when available. Python simplifiers do
    # their own filtering and need complete Begin Object blocks that C++
    # default-value filtering strips out. We overwrite in-place so the
    # simplifier still writes output to _temp_simplified.txt (which C++ expects).
    if '_temp_raw' in input_file:
        full_raw = input_file.replace('_temp_raw', '_raw')
        if os.path.exists(full_raw):
            import shutil
            shutil.copy2(full_raw, input_file)
            print(f"Replaced filtered temp with full raw content")

    # Optional output file argument
    output_file = None
    if len(sys.argv) > 2 and sys.argv[2] == '--output' and len(sys.argv) > 3:
        output_file = sys.argv[3]

    # Detect asset type
    asset_type = detect_asset_type(input_file)

    # Run the appropriate simplifier
    success = run_simplifier(input_file, asset_type, output_file)

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
