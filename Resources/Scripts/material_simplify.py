#!/usr/bin/env python3
"""
Material Asset Simplifier

Parses raw Material export text and simplifies it for AI consumption.
Handles both C++ export format (=== MATERIAL: ===) and UE copy/paste format.

Simplification rules:
- Remove Reroute nodes (visual-only, no semantic meaning)
- Resolve connections through Reroute nodes to show actual data flow
- Keep all meaningful operations (Add, Multiply, Lerp, etc.)
- Preserve parameter information and descriptions
- Show clear input/output relationships

Usage:
    python material_simplify.py <input_file>

Output:
    Creates <input_file_base>_simplified.txt in the same directory
"""

import re
import sys
import os
from collections import defaultdict, OrderedDict
from pathlib import Path


class MaterialSimplifier:
    def __init__(self):
        self.material_name = ""
        self.material_path = ""
        self.material_type = "MATERIAL"  # or "MATERIAL INSTANCE"
        self.metadata = {}
        self.settings = {}
        self.parameters = {
            'scalar': [],
            'vector': [],
            'texture': [],
            'static_switch': []
        }
        self.expressions = OrderedDict()
        self.root_connections = []
        self.parent_material = ""

    def parse_material_text(self, text):
        """Parse material export text and generate simplified output."""
        # Check if it's C++ export format
        if '=== MATERIAL' in text:
            return self._simplify_cpp_export(text)

        # Otherwise parse UE copy/paste format
        self._parse_ue_format(text)
        return self._generate_simplified_output()

    def _simplify_cpp_export(self, text):
        """Parse and simplify C++ export format."""
        lines = text.strip().split('\n')

        # Reset state
        self.expressions = OrderedDict()
        self.root_connections = []

        current_section = None
        current_expr = None

        for line in lines:
            stripped = line.strip()
            # Check if line is indented (for expression properties)
            is_indented = line.startswith('  ') and not line.startswith('===')

            # Header detection
            if stripped.startswith('=== MATERIAL INSTANCE'):
                match = re.search(r'=== (MATERIAL INSTANCE[^:]*): ([^=]+) ===', stripped)
                if match:
                    self.material_type = match.group(1).strip()
                    self.material_name = match.group(2).strip()
                continue
            elif stripped.startswith('=== MATERIAL:'):
                match = re.search(r'=== MATERIAL: ([^=]+) ===', stripped)
                if match:
                    self.material_type = "MATERIAL"
                    self.material_name = match.group(1).strip()
                continue
            elif stripped.startswith('=== MATERIAL GRAPH ==='):
                current_section = 'graph'
                continue

            # Path
            if stripped.startswith('Path:'):
                self.material_path = stripped[5:].strip()
                continue

            # Parent (for material instances)
            if stripped.startswith('Parent:'):
                self.parent_material = stripped[7:].strip()
                continue

            # Metadata
            if stripped.startswith('Domain:'):
                self.metadata['Domain'] = stripped[7:].strip()
                continue
            if stripped.startswith('BlendMode:'):
                self.metadata['BlendMode'] = stripped[10:].strip()
                continue
            if stripped.startswith('ShadingModel:'):
                self.metadata['ShadingModel'] = stripped[13:].strip()
                continue
            if stripped.startswith('TwoSided:'):
                self.metadata['TwoSided'] = stripped[9:].strip()
                continue

            # Section headers
            if stripped.startswith('--- ') and stripped.endswith(' ---'):
                section_name = stripped[4:-4].strip()
                if 'Scalar' in section_name:
                    current_section = 'scalar'
                elif 'Vector' in section_name:
                    current_section = 'vector'
                elif 'Texture' in section_name:
                    current_section = 'texture'
                elif 'Static Switch' in section_name:
                    current_section = 'static_switch'
                elif 'Root' in section_name:
                    current_section = 'root'
                elif 'Settings' in section_name:
                    current_section = 'settings'
                else:
                    current_section = None
                continue

            # Parameters
            if current_section in ('scalar', 'vector', 'texture', 'static_switch'):
                if stripped and stripped != '(none)':
                    match = re.match(r'(\w+)\s*=\s*(.+)', stripped)
                    if match:
                        self.parameters[current_section].append({
                            'name': match.group(1),
                            'value': match.group(2)
                        })
                continue

            # Root connections - supports both old "Property: Connected" and new "Property <- Expression" formats
            if current_section == 'root':
                # New format: "EmissiveColor <- MaterialExpressionMultiply_4"
                root_match = re.match(r'(\w+)\s+<-\s+(.+)', stripped)
                if root_match:
                    prop_name = root_match.group(1)
                    expr_name = root_match.group(2).strip()
                    # Simplify expression name
                    expr_name = re.sub(r'^MaterialExpression', '', expr_name)
                    self.root_connections.append({
                        'property': prop_name,
                        'expression': expr_name
                    })
                # Old format: "Property: Connected"
                elif ':' in stripped and 'Connected' in stripped:
                    prop = stripped.split(':')[0].strip()
                    self.root_connections.append({
                        'property': prop,
                        'expression': 'Connected'
                    })
                continue

            # Settings
            if current_section == 'settings':
                if ':' in stripped:
                    key, value = stripped.split(':', 1)
                    if value.strip() not in ('False', '0', '0.0'):
                        self.settings[key.strip()] = value.strip()
                continue

            # Graph expressions
            if current_section == 'graph':
                # Expression header: [Type] Name "Description"
                expr_match = re.match(r'\[(\w+)\]\s+(\S+)(?:\s+"([^"]*)")?', stripped)
                if expr_match:
                    expr_type = expr_match.group(1)
                    expr_name = expr_match.group(2)
                    expr_desc = expr_match.group(3) or ""

                    current_expr = {
                        'type': expr_type,
                        'name': expr_name,
                        'description': expr_desc,
                        'properties': {},
                        'inputs': [],
                        'outputs': []
                    }
                    self.expressions[expr_name] = current_expr
                    continue

                # Expression properties/inputs (indented lines under expression)
                if current_expr and is_indented:
                    prop_line = stripped

                    # Input connection: A <- Source.Output
                    input_match = re.match(r'(\w+)\s+<-\s+(\S+)', prop_line)
                    if input_match:
                        current_expr['inputs'].append({
                            'pin': input_match.group(1),
                            'source': input_match.group(2)
                        })
                        continue

                    # Property: Key = Value
                    prop_match = re.match(r'(\w+)\s*=\s*(.+)', prop_line)
                    if prop_match:
                        current_expr['properties'][prop_match.group(1)] = prop_match.group(2)
                        continue

                    # Outputs line
                    if prop_line.startswith('Outputs:'):
                        current_expr['outputs'] = prop_line[8:].strip().split(', ')

        return self._generate_simplified_output()

    def _generate_simplified_output(self):
        """Generate the simplified output text."""
        output = []

        # Header
        output.append(f"=== {self.material_type}: {self.material_name} ===")

        # Compact metadata line
        meta_parts = []
        if self.metadata.get('Domain'):
            meta_parts.append(f"Domain: {self.metadata['Domain']}")
        if self.metadata.get('BlendMode'):
            meta_parts.append(f"BlendMode: {self.metadata['BlendMode']}")
        if self.metadata.get('ShadingModel'):
            meta_parts.append(f"ShadingModel: {self.metadata['ShadingModel']}")
        if meta_parts:
            output.append(' | '.join(meta_parts))

        # Parent (for instances)
        if self.parent_material:
            output.append(f"Parent: {self.parent_material}")

        output.append("")

        # Parameters section - compact format
        output.append("PARAMETERS:")
        has_params = False

        for param in self.parameters['scalar']:
            output.append(f"  {param['name']}: {param['value']} (Scalar)")
            has_params = True
        for param in self.parameters['vector']:
            output.append(f"  {param['name']}: {param['value']} (Vector)")
            has_params = True
        for param in self.parameters['texture']:
            # Simplify texture path
            tex_value = param['value']
            if '/' in tex_value:
                tex_value = tex_value.split('/')[-1].split('.')[0]
            output.append(f"  {param['name']}: {tex_value} (Texture)")
            has_params = True
        for param in self.parameters['static_switch']:
            output.append(f"  {param['name']}: {param['value']} (Switch)")
            has_params = True

        if not has_params:
            output.append("  (none)")

        # Root connections with actual expression names
        if self.root_connections:
            output.append("")
            output.append("ROOT CONNECTIONS:")
            for conn in self.root_connections:
                if isinstance(conn, dict):
                    output.append(f"  {conn['property']} <- {conn['expression']}")
                else:
                    # Legacy format (just property name)
                    output.append(f"  {conn}: Connected")

        # Build simplified graph (skip Reroute, resolve connections)
        if self.expressions:
            output.append("")
            output.append("MATERIAL LOGIC:")
            output.extend(self._build_simplified_graph())

        return '\n'.join(output)

    def _build_simplified_graph(self):
        """Build simplified graph representation, removing Reroute nodes."""
        output = []

        # First pass: identify Reroute nodes and their through-connections
        reroute_map = {}  # Maps Reroute output to its actual source

        for name, expr in self.expressions.items():
            if expr['type'] == 'Reroute':
                # Find what this reroute connects to
                if expr['inputs']:
                    source = expr['inputs'][0]['source']
                    # Resolve the source (might be another reroute)
                    reroute_map[name] = source

        # Resolve reroute chains
        def resolve_source(source):
            """Resolve a source through any Reroute chain."""
            # Extract node name from "NodeName.Output" format
            node_name = source.split('.')[0]

            # Check if it's a reroute
            if node_name in reroute_map:
                return resolve_source(reroute_map[node_name])
            return source

        # Second pass: output meaningful nodes with resolved connections
        meaningful_nodes = []

        for name, expr in self.expressions.items():
            # Skip Reroute nodes
            if expr['type'] == 'Reroute':
                continue

            meaningful_nodes.append((name, expr))

        # Group by function/purpose
        parameters = []
        operations = []
        samplers = []

        for name, expr in meaningful_nodes:
            if 'Parameter' in expr['type']:
                parameters.append((name, expr))
            elif 'TextureSample' in expr['type']:
                samplers.append((name, expr))
            else:
                operations.append((name, expr))

        # Output parameters first (they are inputs to the material)
        if parameters:
            output.append("")
            output.append("  [Parameters]")
            for name, expr in parameters:
                desc = f' "{expr["description"]}"' if expr['description'] else ''
                param_name = expr['properties'].get('ParameterName', name)
                default = expr['properties'].get('DefaultValue', '')
                texture = expr['properties'].get('Texture', '')

                if default:
                    output.append(f"    {param_name}{desc} = {default}")
                elif texture:
                    # Simplify texture path
                    if '/' in texture:
                        texture = texture.split('/')[-1].split('.')[0]
                    output.append(f"    {param_name}{desc} -> {texture}")
                else:
                    output.append(f"    {param_name}{desc}")

        # Output texture samplers
        if samplers:
            output.append("")
            output.append("  [Texture Sampling]")
            for name, expr in samplers:
                desc = f' "{expr["description"]}"' if expr['description'] else ''
                # Show inputs (Coordinates, TextureObject)
                inputs_str = []
                for inp in expr['inputs']:
                    resolved = resolve_source(inp['source'])
                    # Simplify source name
                    resolved = self._simplify_node_ref(resolved)
                    # Make input names clearer
                    pin_name = inp['pin']
                    if pin_name == 'Coordinates':
                        pin_name = 'UV'
                    elif pin_name == 'TextureObject':
                        pin_name = 'Tex'
                    inputs_str.append(f"{pin_name}={resolved}")

                simple_name = self._simplify_node_name(name)
                if inputs_str:
                    output.append(f"    {simple_name}{desc}: {', '.join(inputs_str)}")
                else:
                    output.append(f"    {simple_name}{desc}")

        # Output operations (the actual logic)
        if operations:
            output.append("")
            output.append("  [Operations]")
            for name, expr in operations:
                desc = f' "{expr["description"]}"' if expr['description'] else ''
                expr_type = expr['type']

                # Build input list with resolved sources
                inputs_str = []
                for inp in expr['inputs']:
                    resolved = resolve_source(inp['source'])
                    resolved = self._simplify_node_ref(resolved)
                    inputs_str.append(f"{inp['pin']}={resolved}")

                # Show key properties (filter out internal ones)
                props_str = []
                for key, value in expr['properties'].items():
                    if key in ('ParameterName', 'DefaultValue', 'Texture'):
                        continue
                    # Special handling for MaterialFunction - show simplified path
                    if key == 'MaterialFunction':
                        # Extract function name from path like /Engine/Functions/.../Pi.Pi
                        func_name = value.split('/')[-1] if '/' in value else value
                        props_str.append(f"Func={func_name}")
                    elif key.startswith('FunctionInput') or key.startswith('FunctionOutput'):
                        # Show function inputs/outputs
                        props_str.append(f"{key}={value}")
                    elif key in ('Value', 'Mask', 'CoordinateIndex', 'UTiling', 'VTiling'):
                        # Show important properties
                        props_str.append(f"{key}={value}")
                    elif key.startswith('Const'):
                        # Show Const values (ConstA, ConstB, ConstAlpha, ConstMin, ConstMax, ConstExponent, etc.)
                        # These are critical for understanding material logic when inputs are not connected
                        props_str.append(f"{key}={value}")
                    elif key == 'Period':
                        # Cosine/Sine Period is critical for understanding the function
                        props_str.append(f"{key}={value}")

                # Compact output
                line = f"    [{expr_type}]{desc}"
                if inputs_str:
                    line += f" ({', '.join(inputs_str)})"
                if props_str:
                    line += f" {{{', '.join(props_str)}}}"

                output.append(line)

        return output

    def _simplify_node_name(self, name):
        """Simplify node name by removing MaterialExpression prefix."""
        # Remove common prefixes
        name = re.sub(r'^MaterialExpression', '', name)
        name = re.sub(r'^MaterialGraphNode_', 'Node', name)
        return name

    def _simplify_node_ref(self, ref):
        """Simplify a node reference like 'MaterialExpressionAdd_0.Output'."""
        parts = ref.split('.')
        node = self._simplify_node_name(parts[0])
        if len(parts) > 1 and parts[1] != 'Output':
            return f"{node}.{parts[1]}"
        return node

    def _parse_ue_format(self, text):
        """Parse UE copy/paste format with Begin Object/End Object blocks."""
        # Extract material name from file content
        name_match = re.search(r"Name=\"([^\"]+)\"", text)
        if name_match:
            self.material_name = name_match.group(1).split('.')[0]

        # Parse Tagged JSON blocks
        self._parse_tagged_blocks(text)

        # Parse MaterialGraphNode blocks
        self._parse_graph_nodes(text)

    def _parse_tagged_blocks(self, text):
        """Parse JSON-like Tagged blocks with material properties."""
        json_pattern = r'\{\s*"Tagged"\s*:\s*\[(.*?)\]\s*\}'

        for match in re.finditer(json_pattern, text, re.DOTALL):
            try:
                block_content = match.group(1)
                pairs = re.findall(r'\[\s*"([^"]+)"\s*,\s*"([^"]*)"\s*\]', block_content)

                for key, value in pairs:
                    if '.' in key:
                        continue

                    if key == 'MaterialDomain':
                        self.metadata['Domain'] = value.replace('MD_', '')
                    elif key == 'BlendMode':
                        self.metadata['BlendMode'] = value.replace('BLEND_', '')

            except Exception:
                continue

    def _parse_graph_nodes(self, text):
        """Parse MaterialGraphNode blocks."""
        node_pattern = r'Begin Object Class=([^\s]+)\s+Name="([^"]+)".*?End Object'

        for match in re.finditer(node_pattern, text, re.DOTALL):
            node_class = match.group(1)
            node_name = match.group(2)
            node_content = match.group(0)

            if 'MaterialGraphNode_Root' in node_class:
                continue

            if 'MaterialGraphNode' in node_class:
                self._parse_expression_node(node_name, node_content)

    def _parse_expression_node(self, node_name, content):
        """Parse a material expression node."""
        expr_info = {
            'type': '',
            'name': node_name,
            'description': '',
            'properties': {},
            'inputs': [],
            'outputs': []
        }

        # Extract expression type
        expr_type_match = re.search(r'Begin Object Class=/Script/Engine\.(\w+)\s+Name="([^"]+)"', content)
        if expr_type_match:
            expr_type = expr_type_match.group(1)
            if expr_type.startswith('MaterialExpression'):
                expr_type = expr_type[18:]
            expr_info['type'] = expr_type

        # Extract description
        desc_match = re.search(r'Desc="([^"]*)"', content)
        if desc_match and desc_match.group(1):
            expr_info['description'] = desc_match.group(1)

        self.expressions[node_name] = expr_info


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    input_file = sys.argv[1]

    if not os.path.exists(input_file):
        print(f"Error: Input file not found: {input_file}")
        sys.exit(1)

    try:
        with open(input_file, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading file: {e}")
        sys.exit(1)

    simplifier = MaterialSimplifier()
    simplified = simplifier.parse_material_text(content)

    input_path = Path(input_file)
    output_file = input_path.parent / f"{input_path.stem.replace('_raw', '')}_simplified.txt"

    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(simplified)
        print(f"Simplified material written to: {output_file}")
    except Exception as e:
        print(f"Error writing output: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
