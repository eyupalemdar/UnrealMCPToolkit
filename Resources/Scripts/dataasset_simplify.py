#!/usr/bin/env python3
"""
DataAsset Simplifier

Parses and simplifies UDataAsset exports from Unreal Engine.
Extracts property names and values in a clean, readable format.

Usage:
    python dataasset_simplify.py <input_file>

Output:
    Creates <input_file>_simplified.txt with structured property data.
"""

import re
import sys
import os
from typing import Dict, List, Any, Optional
from dataclasses import dataclass, field


@dataclass
class PropertyInfo:
    """Represents a DataAsset property"""
    name: str
    value: str
    property_type: str = "unknown"
    category: str = ""
    is_array: bool = False
    array_elements: List[str] = field(default_factory=list)


class DataAssetSimplifier:
    """Simplifier for UDataAsset exported text"""

    def __init__(self):
        self.asset_name = "Unknown"
        self.asset_class = "UDataAsset"
        self.properties: Dict[str, PropertyInfo] = {}
        self.nested_objects: Dict[str, Dict] = {}

    def parse_content(self, content: str):
        """Parse the DataAsset text content"""
        # Expand escaped characters
        content = content.replace('\\r\\n', '\n')
        content = content.replace('\\n', '\n')
        content = content.replace('\\t', '\t')
        content = content.replace('\\"', '"')

        # Try to extract asset name and class
        class_match = re.search(r'Begin Object Class=([^\s]+)\s+Name="([^"]+)"', content)
        if class_match:
            self.asset_class = class_match.group(1).split('.')[-1].replace("'", "")
            self.asset_name = class_match.group(2)

        # Parse top-level properties (simple key=value patterns)
        self._parse_simple_properties(content)

        # Parse array properties
        self._parse_array_properties(content)

        # Parse struct properties
        self._parse_struct_properties(content)

        # Parse nested Begin Object blocks
        self._parse_nested_objects(content)

    def _parse_simple_properties(self, content: str):
        """Parse simple property=value assignments"""
        # Pattern for simple property assignments
        # Exclude lines that start with Begin/End or are inside parentheses contexts
        lines = content.split('\n')

        for line in lines:
            line = line.strip()

            # Skip structural markers
            if line.startswith('Begin ') or line.startswith('End '):
                continue

            # Skip empty lines and comments
            if not line or line.startswith('//'):
                continue

            # Match simple property=value
            match = re.match(r'^(\w+)=(.+)$', line)
            if match:
                prop_name = match.group(1)
                prop_value = match.group(2).strip()

                # Skip metadata properties
                if prop_name in ['NodeGuid', 'NodePosX', 'NodePosY', 'PinId', 'CustomProperties']:
                    continue

                # Determine type from value format
                prop_type = self._infer_type(prop_value)

                self.properties[prop_name] = PropertyInfo(
                    name=prop_name,
                    value=prop_value,
                    property_type=prop_type
                )

    def _parse_array_properties(self, content: str):
        """Parse array properties like MyArray(0)=Value"""
        # Pattern for indexed array elements
        array_pattern = r'^(\w+)\((\d+)\)=(.+)$'

        arrays: Dict[str, List[str]] = {}

        for line in content.split('\n'):
            line = line.strip()
            match = re.match(array_pattern, line)
            if match:
                prop_name = match.group(1)
                index = int(match.group(2))
                value = match.group(3).strip()

                if prop_name not in arrays:
                    arrays[prop_name] = []

                # Ensure list is large enough
                while len(arrays[prop_name]) <= index:
                    arrays[prop_name].append("")

                arrays[prop_name][index] = value

        # Store as properties
        for prop_name, elements in arrays.items():
            self.properties[prop_name] = PropertyInfo(
                name=prop_name,
                value=f"[{len(elements)} elements]",
                property_type="Array",
                is_array=True,
                array_elements=elements
            )

    def _parse_struct_properties(self, content: str):
        """Parse struct properties like Property=(Member1=Value1,Member2=Value2)"""
        struct_pattern = r'^(\w+)=\(([^)]+)\)$'

        for line in content.split('\n'):
            line = line.strip()
            match = re.match(struct_pattern, line)
            if match:
                prop_name = match.group(1)
                struct_content = match.group(2)

                # Skip if already parsed
                if prop_name in self.properties:
                    continue

                # Parse struct members
                members = self._parse_struct_members(struct_content)

                self.properties[prop_name] = PropertyInfo(
                    name=prop_name,
                    value=f"({struct_content})",
                    property_type="Struct"
                )

    def _parse_struct_members(self, struct_content: str) -> Dict[str, str]:
        """Parse comma-separated struct members"""
        members = {}
        # Split by comma, but respect nested parentheses
        depth = 0
        current = ""
        for char in struct_content:
            if char == '(':
                depth += 1
            elif char == ')':
                depth -= 1
            elif char == ',' and depth == 0:
                if '=' in current:
                    key, value = current.split('=', 1)
                    members[key.strip()] = value.strip()
                current = ""
                continue
            current += char

        if current and '=' in current:
            key, value = current.split('=', 1)
            members[key.strip()] = value.strip()

        return members

    def _parse_nested_objects(self, content: str):
        """Parse nested Begin Object blocks"""
        object_pattern = r'Begin Object(?: Class=([^\s]+))? Name="([^"]+)"(.*?)End Object'

        for match in re.finditer(object_pattern, content, re.DOTALL):
            obj_class = match.group(1) or "Unknown"
            obj_name = match.group(2)
            obj_content = match.group(3)

            # Clean class name
            obj_class = obj_class.split('.')[-1].replace("'", "") if obj_class else "Unknown"

            # Parse properties within this object
            obj_props = {}
            for line in obj_content.split('\n'):
                line = line.strip()
                if '=' in line and not line.startswith('Begin') and not line.startswith('End'):
                    parts = line.split('=', 1)
                    if len(parts) == 2:
                        key = parts[0].strip()
                        value = parts[1].strip()
                        # Skip metadata
                        if key not in ['NodeGuid', 'NodePosX', 'NodePosY', 'PinId']:
                            obj_props[key] = value

            if obj_props:
                self.nested_objects[obj_name] = {
                    'class': obj_class,
                    'properties': obj_props
                }

    def _infer_type(self, value: str) -> str:
        """Infer property type from value format"""
        if value.lower() in ['true', 'false']:
            return "bool"
        if value.startswith('"') and value.endswith('"'):
            return "string"
        if value.startswith('(') and value.endswith(')'):
            return "struct"
        if re.match(r'^-?\d+$', value):
            return "int"
        if re.match(r'^-?\d+\.\d+$', value):
            return "float"
        if '/' in value and "'" in value:
            return "object_ref"
        return "unknown"

    def generate_simplified(self) -> str:
        """Generate simplified output format"""
        output = []
        output.append("=" * 60)
        output.append(f"DATAASSET: {self.asset_name}")
        output.append(f"Class: {self.asset_class}")
        output.append("=" * 60)
        output.append("")

        # Group properties by type
        simple_props = []
        array_props = []
        struct_props = []
        ref_props = []

        for prop in self.properties.values():
            if prop.is_array:
                array_props.append(prop)
            elif prop.property_type == "struct":
                struct_props.append(prop)
            elif prop.property_type == "object_ref":
                ref_props.append(prop)
            else:
                simple_props.append(prop)

        # Output simple properties
        if simple_props:
            output.append("[Properties]")
            for prop in sorted(simple_props, key=lambda p: p.name):
                output.append(f"  {prop.name}: {prop.value}")
            output.append("")

        # Output reference properties
        if ref_props:
            output.append("[References]")
            for prop in sorted(ref_props, key=lambda p: p.name):
                # Simplify reference paths
                simplified_ref = self._simplify_reference(prop.value)
                output.append(f"  {prop.name}: {simplified_ref}")
            output.append("")

        # Output struct properties
        if struct_props:
            output.append("[Structs]")
            for prop in sorted(struct_props, key=lambda p: p.name):
                output.append(f"  {prop.name}:")
                # Parse and display struct members
                if prop.value.startswith('(') and prop.value.endswith(')'):
                    members = self._parse_struct_members(prop.value[1:-1])
                    for key, val in members.items():
                        output.append(f"    {key}: {val}")
            output.append("")

        # Output array properties
        if array_props:
            output.append("[Arrays]")
            for prop in sorted(array_props, key=lambda p: p.name):
                output.append(f"  {prop.name}: [{len(prop.array_elements)} elements]")
                for i, elem in enumerate(prop.array_elements[:10]):  # Limit to first 10
                    simplified_elem = self._simplify_value(elem)
                    output.append(f"    [{i}]: {simplified_elem}")
                if len(prop.array_elements) > 10:
                    output.append(f"    ... and {len(prop.array_elements) - 10} more")
            output.append("")

        # Output nested objects
        if self.nested_objects:
            output.append("[Nested Objects]")
            for obj_name, obj_data in self.nested_objects.items():
                output.append(f"  {obj_name} ({obj_data['class']}):")
                for key, val in obj_data['properties'].items():
                    simplified_val = self._simplify_value(val)
                    output.append(f"    {key}: {simplified_val}")
            output.append("")

        return '\n'.join(output)

    def _simplify_reference(self, ref: str) -> str:
        """Simplify an object reference path"""
        # Extract just the asset name from full paths
        match = re.search(r"['/]([^'/]+)'?$", ref)
        if match:
            return match.group(1)
        return ref

    def _simplify_value(self, value: str) -> str:
        """Simplify a property value for display"""
        # Truncate very long values
        if len(value) > 100:
            return value[:100] + "..."

        # Simplify object references
        if '/' in value and "'" in value:
            return self._simplify_reference(value)

        return value


def simplify_dataasset(content: str) -> str:
    """Main function to simplify DataAsset content"""
    simplifier = DataAssetSimplifier()
    simplifier.parse_content(content)
    return simplifier.generate_simplified()


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        print("Usage: python dataasset_simplify.py <input_file>")
        print("Example: python dataasset_simplify.py DA_Rules_Standard101.txt")
        sys.exit(1)

    input_file = sys.argv[1]

    # Check file exists
    if not os.path.exists(input_file):
        print(f"Error: File not found: {input_file}")
        sys.exit(1)

    # Read input
    try:
        with open(input_file, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
        print(f"File read: {input_file}")
    except Exception as e:
        print(f"Error reading file: {e}")
        sys.exit(1)

    # Simplify
    simplified = simplify_dataasset(content)

    # Generate output filename (remove _raw suffix if present)
    base_name = os.path.splitext(input_file)[0]
    if base_name.endswith('_raw'):
        base_name = base_name[:-4]
    output_file = f"{base_name}_simplified.txt"

    # Write output
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(simplified)
        print(f"Simplified output saved: {output_file}")

        # Size comparison
        original_size = os.path.getsize(input_file)
        simplified_size = os.path.getsize(output_file)
        reduction = ((original_size - simplified_size) / original_size) * 100

        print(f"\nSize comparison:")
        print(f"  Original: {original_size:,} bytes")
        print(f"  Simplified: {simplified_size:,} bytes")
        print(f"  Reduction: {reduction:.1f}%")

    except Exception as e:
        print(f"Error writing output: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
