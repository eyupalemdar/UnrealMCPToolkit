#!/usr/bin/env python3
"""
Unreal Engine Blueprint Text Format Simplifier
General-purpose tool for simplifying Unreal Engine Blueprint text exports
Supports AnimBlueprints, WidgetBlueprints, ActorBlueprints, etc.
"""

import re
import json
from typing import Dict, List, Any, Tuple, Optional
from dataclasses import dataclass, field
from collections import defaultdict
import sys
import os


@dataclass
class Pin:
    """Represents a Blueprint node pin"""
    id: str
    name: str
    type: str
    direction: str = "input"
    linked_to: List[Tuple[str, str]] = field(default_factory=list)  # (node_id, pin_id)
    default_value: Optional[str] = None


@dataclass
class Node:
    """Represents a Blueprint node"""
    id: str
    type: str
    name: str
    position: Tuple[float, float] = (0, 0)
    pins: Dict[str, Pin] = field(default_factory=dict)
    properties: Dict[str, Any] = field(default_factory=dict)


@dataclass
class Graph:
    """Represents a Blueprint graph or function"""
    name: str
    type: str = "Function"
    nodes: Dict[str, Node] = field(default_factory=dict)
    local_variables: List[Dict[str, str]] = field(default_factory=list)


class UnrealBPSimplifier:
    """Main class for simplifying Unreal Engine Blueprint text format"""

    # Common Unreal path aliases for compression
    PATH_ALIASES = {
        '/Script/BlueprintGraph': '@BPGraph',
        '/Script/Engine': '@Engine',
        '/Script/AnimGraph': '@AnimGraph',
        '/Script/CoreUObject': '@Core',
        '/Script/UnrealEd': '@Editor',
        '/Script/PropertyAccessNode': '@PropAccess',
        '/Script/AnimGraphRuntime': '@AnimRuntime',
        '/Game/Characters/Heroes/Mannequin': '@Mannequin',
        '/Game/Characters': '@Characters',
    }

    # Node type simplifications
    NODE_TYPES = {
        'K2Node_CallFunction': 'CallFunc',
        'K2Node_VariableGet': 'GetVar',
        'K2Node_VariableSet': 'SetVar',
        'K2Node_FunctionEntry': 'FuncEntry',
        'K2Node_FunctionResult': 'FuncResult',
        'K2Node_ExecutionSequence': 'Sequence',
        'K2Node_IfThenElse': 'Branch',
        'K2Node_CommutativeAssociativeBinaryOperator': 'BinaryOp',
        'K2Node_PropertyAccess': 'PropAccess',
        'K2Node_Knot': 'Reroute',
        'AnimGraphNode_': 'Anim_',
        'EdGraphNode_Comment': 'Comment',
    }

    # Pin type simplifications
    PIN_TYPE_MAP = {
        'exec': 'flow',
        'bool': 'bool',
        'real': 'float',
        'int': 'int',
        'string': 'string',
        'struct': 'struct',
        'object': 'object',
        'class': 'class',
        'interface': 'interface',
    }

    def __init__(self):
        self.graphs = {}
        self.current_graph = None
        self.node_id_map = {}  # Maps original IDs to simplified IDs
        self.path_cache = {}  # Cache for path simplifications
        self.content_lines = []

    def parse_unreal_path(self, path: str) -> str:
        """Parse and simplify Unreal Engine paths"""
        if path in self.path_cache:
            return self.path_cache[path]

        simplified = path

        # Apply aliases
        for full_path, alias in self.PATH_ALIASES.items():
            if full_path in simplified:
                simplified = simplified.replace(full_path, alias)

        # Extract just the class/asset name if it's very long
        if len(simplified) > 100:
            # Try to extract just the important part
            match = re.search(r"['/]([^'/]+)'?$", simplified)
            if match:
                simplified = f".../{match.group(1)}"

        self.path_cache[path] = simplified
        return simplified

    def simplify_node_type(self, class_path: str) -> str:
        """Simplify node class names"""
        # First try to extract just the class name
        # Handle format: /Script/ModuleName.ClassName
        class_match = re.search(r"/Script/[\w\.]+\.(\w+)", class_path)
        if class_match:
            class_name = class_match.group(1)
        else:
            # Try format: Class=/Script/ModuleName.ClassName
            class_match = re.search(r"Class=/Script/[\w\.]+\.(\w+)", class_path)
            if class_match:
                class_name = class_match.group(1)
            else:
                # Just take the last part after any dot or slash
                parts = class_path.replace('/', '.').split('.')
                class_name = parts[-1] if parts else class_path

        # Apply simplifications
        for pattern, replacement in self.NODE_TYPES.items():
            if class_name.startswith(pattern):
                remainder = class_name[len(pattern):]
                return f"{replacement}{remainder}"

        return class_name

    def parse_pin_type(self, pin_str: str) -> Dict[str, Any]:
        """Parse pin type information"""
        pin_info = {
            'category': 'unknown',
            'subcategory': None,
            'object': None,
        }

        # Parse PinType.PinCategory
        category_match = re.search(r'PinType\.PinCategory="([^"]+)"', pin_str)
        if category_match:
            category = category_match.group(1)
            pin_info['category'] = self.PIN_TYPE_MAP.get(category, category)

        # Parse PinType.PinSubCategory
        subcat_match = re.search(r'PinType\.PinSubCategory="([^"]+)"', pin_str)
        if subcat_match and subcat_match.group(1):
            pin_info['subcategory'] = subcat_match.group(1)

        # Parse PinType.PinSubCategoryObject
        obj_match = re.search(r'PinType\.PinSubCategoryObject=([^,)]+)', pin_str)
        if obj_match and obj_match.group(1) != 'None':
            obj_path = obj_match.group(1).strip('"')
            pin_info['object'] = self.parse_unreal_path(obj_path)

        return pin_info

    def parse_pin_connections(self, pin_str: str) -> List[Tuple[str, str]]:
        """Parse pin connections from LinkedTo field"""
        connections = []
        linked_match = re.search(r'LinkedTo=\(([^)]+)\)', pin_str)
        if linked_match:
            linked_str = linked_match.group(1)
            # Parse each connection
            for conn in linked_str.split(','):
                conn = conn.strip()
                if ' ' in conn:
                    parts = conn.split(' ')
                    if len(parts) >= 2:
                        node_id = parts[0]
                        pin_id = parts[1]
                        connections.append((node_id, pin_id))
        return connections

    def expand_bpgraph(self, content: str) -> str:
        """Expand single-line BPGraph definitions"""
        # Replace \r\n with actual newlines within BPGraph blocks
        expanded = content.replace('\\r\\n', '\n')
        expanded = expanded.replace('\\t', '\t')
        expanded = expanded.replace('\\"', '"')
        return expanded

    def parse_node(self, node_block: str) -> Optional[Node]:
        """Parse a Begin Object ... End Object block into a Node"""
        # Extract node class and name
        begin_match = re.search(r'Begin Object Class=([^ ]+) Name="([^"]+)"', node_block)
        if not begin_match:
            # Try alternate format
            begin_match = re.search(r'Begin Object Name="([^"]+)".*?Class=([^ ]+)', node_block)
            if not begin_match:
                return None
            node_name = begin_match.group(1)
            class_path = begin_match.group(2)
        else:
            class_path = begin_match.group(1)
            node_name = begin_match.group(2)

        # Create node with simplified type
        node = Node(
            id=node_name,
            type=self.simplify_node_type(class_path),
            name=node_name
        )

        # Parse position
        pos_x_match = re.search(r'NodePosX=([-\d]+)', node_block)
        pos_y_match = re.search(r'NodePosY=([-\d]+)', node_block)
        if pos_x_match and pos_y_match:
            node.position = (int(pos_x_match.group(1)), int(pos_y_match.group(1)))

        # Parse properties
        guid_match = re.search(r'NodeGuid=([A-F0-9]+)', node_block)
        if guid_match:
            node.properties['guid'] = guid_match.group(1)

        # Parse function references
        func_ref_match = re.search(r'FunctionReference=\(([^)]+)\)', node_block)
        if func_ref_match:
            node.properties['function'] = func_ref_match.group(1)

        # Parse member references
        var_ref_match = re.search(r'VariableReference=\(([^)]+)\)', node_block)
        if var_ref_match:
            node.properties['variable'] = var_ref_match.group(1)

        # Parse pins
        pin_matches = re.finditer(
            r'CustomProperties Pin \(([^)]+)\)',
            node_block
        )

        for pin_match in pin_matches:
            pin_str = pin_match.group(1)

            # Extract pin ID and name
            id_match = re.search(r'PinId=([A-F0-9]+)', pin_str)
            name_match = re.search(r'PinName="([^"]+)"', pin_str)

            if id_match and name_match:
                pin = Pin(
                    id=id_match.group(1),
                    name=name_match.group(1),
                    type='unknown'
                )

                # Parse direction
                if 'Direction="EGPD_Output"' in pin_str:
                    pin.direction = 'output'

                # Parse type
                pin_type = self.parse_pin_type(pin_str)
                if pin_type['subcategory']:
                    pin.type = f"{pin_type['category']}:{pin_type['subcategory']}"
                else:
                    pin.type = pin_type['category']

                # Parse connections
                pin.linked_to = self.parse_pin_connections(pin_str)

                # Parse default value
                default_match = re.search(r'DefaultValue="([^"]+)"', pin_str)
                if default_match:
                    pin.default_value = default_match.group(1)

                node.pins[pin.name] = pin

        return node

    def parse_graph_block(self, content: str) -> Graph:
        """Parse a graph or function block"""
        graph = Graph(name="Unknown")

        # Parse graph name
        name_match = re.search(r'GraphName="([^"]+)"', content)
        if name_match:
            graph.name = name_match.group(1)

        # Parse local variables
        var_matches = re.finditer(
            r'LocalVariables\(\d+\)=\(([^)]+)\)',
            content
        )
        for var_match in var_matches:
            var_str = var_match.group(1)
            var_info = {}

            name_match = re.search(r'VarName="([^"]+)"', var_str)
            if name_match:
                var_info['name'] = name_match.group(1)

            type_match = re.search(r'VarType=\(PinCategory="([^"]+)"', var_str)
            if type_match:
                var_info['type'] = type_match.group(1)

            if var_info:
                graph.local_variables.append(var_info)

        # Parse nodes
        node_blocks = re.finditer(
            r'Begin Object.*?Name="([^"]+)".*?End Object',
            content,
            re.DOTALL
        )

        for node_block_match in node_blocks:
            node = self.parse_node(node_block_match.group(0))
            if node:
                graph.nodes[node.id] = node

        return graph

    def parse_content(self, content: str):
        """Parse the entire Blueprint text content"""
        # Handle BPGraph blocks first
        # Use a different approach: find BPGraph start, then parse manually
        bpgraph_starts = []
        for match in re.finditer(r'BPGraph\(GraphName="([^"]+)"', content):
            bpgraph_starts.append((match.start(), match.group(1)))

        for start_pos, graph_name in bpgraph_starts:
            # Find the end of this BPGraph block
            # Look for the matching closing parenthesis
            paren_count = 0
            i = content.index('BPGraph(', start_pos)
            i += 8  # Skip 'BPGraph('
            in_string = False
            escaped = False
            end_pos = -1

            while i < len(content):
                char = content[i]

                if escaped:
                    escaped = False
                elif char == '\\':
                    escaped = True
                elif char == '"' and not escaped:
                    in_string = not in_string
                elif not in_string:
                    if char == '(':
                        paren_count += 1
                    elif char == ')':
                        if paren_count == 0:
                            end_pos = i
                            break
                        paren_count -= 1

                i += 1

            if end_pos > 0:
                # Extract the full BPGraph content
                bpgraph_content = content[start_pos:end_pos + 1]

                # Extract NodesString with better regex
                # The NodesString contains everything until the next parameter or closing paren
                nodes_start = bpgraph_content.find('NodesString="')
                if nodes_start >= 0:
                    nodes_start += 13  # Skip 'NodesString="'
                    nodes_end = nodes_start

                    # Find the end of NodesString - it ends with " followed by comma or paren
                    in_escape = False
                    while nodes_end < len(bpgraph_content):
                        if bpgraph_content[nodes_end] == '\\' and not in_escape:
                            in_escape = True
                        elif bpgraph_content[nodes_end] == '"' and not in_escape:
                            # Check if next char is comma or closing paren
                            if nodes_end + 1 < len(bpgraph_content):
                                next_char = bpgraph_content[nodes_end + 1]
                                if next_char in ',)':
                                    break
                        else:
                            in_escape = False
                        nodes_end += 1

                    nodes_string = bpgraph_content[nodes_start:nodes_end]

                    # Expand the nodes string
                    expanded_content = self.expand_bpgraph(nodes_string)

                    # Create graph and parse its content
                    graph = Graph(name=graph_name)

                    # Parse nodes from expanded content
                    node_pattern = r'Begin Object.*?End Object'
                    node_matches = re.finditer(node_pattern, expanded_content, re.DOTALL)

                    for node_match in node_matches:
                        node = self.parse_node(node_match.group(0))
                        if node:
                            graph.nodes[node.id] = node

                    # Parse local variables if they exist
                    var_pattern = r'LocalVariables\(\d+\)=\(([^)]+)\)'
                    for var_match in re.finditer(var_pattern, expanded_content):
                        var_str = var_match.group(1)
                        var_info = {}

                        name_match = re.search(r'VarName="([^"]+)"', var_str)
                        if name_match:
                            var_info['name'] = name_match.group(1)

                        type_match = re.search(r'VarType=\(PinCategory="([^"]+)"', var_str)
                        if type_match:
                            var_info['type'] = type_match.group(1)

                        if var_info:
                            graph.local_variables.append(var_info)

                    self.graphs[graph_name] = graph

        # Also look for function graphs that aren't in BPGraph format
        # Parse any remaining top-level blocks
        remaining_content = content
        self._parse_toplevel_blocks(remaining_content)

    def _parse_toplevel_blocks(self, content: str):
        """Parse top-level Begin Object blocks that aren't part of BPGraph"""
        # Look for graph-like structures
        block_pattern = r'Begin Object.*?(?:Graph|Function|State).*?End Object'

        for match in re.finditer(block_pattern, content, re.DOTALL):
            block = match.group(0)
            # Check if this contains nodes
            if 'NodeGuid' in block or 'Nodes(' in block:
                # This might be a graph structure
                # Try to parse it as a graph
                graph = self.parse_graph_block(block)
                if graph.name != "Unknown" and graph.name not in self.graphs:
                    self.graphs[graph.name] = graph

    def generate_simplified_format(self) -> str:
        """Generate simplified, human-readable format"""
        output = []
        output.append("=" * 80)
        output.append("SIMPLIFIED UNREAL BLUEPRINT FORMAT")
        output.append("=" * 80)
        output.append("")

        # Output graphs
        for graph_name, graph in self.graphs.items():
            output.append(f"\n## GRAPH: {graph_name}")
            output.append("-" * 40)

            # Local variables
            if graph.local_variables:
                output.append("\nLOCAL VARIABLES:")
                for var in graph.local_variables:
                    output.append(f"  - {var.get('name', 'unknown')}: {var.get('type', 'unknown')}")

            # Nodes
            output.append("\nNODES:")
            for node_id, node in graph.nodes.items():
                # Display node with type
                output.append(f"\n  [{node.type}] {node.name}")
                if node.position != (0, 0):
                    output.append(f"    Position: {node.position}")

                # Properties
                if 'function' in node.properties:
                    output.append(f"    Function: {node.properties['function']}")
                if 'variable' in node.properties:
                    output.append(f"    Variable: {node.properties['variable']}")

                # Pins
                if node.pins:
                    output.append("    Pins:")
                    for pin_name, pin in node.pins.items():
                        pin_str = f"      {pin.direction[0].upper()}: {pin_name} ({pin.type})"
                        if pin.linked_to:
                            connections = [f"{n}:{p}" for n, p in pin.linked_to]
                            pin_str += f" -> {', '.join(connections)}"
                        if pin.default_value:
                            pin_str += f" = {pin.default_value}"
                        output.append(pin_str)

            # Generate connection flow
            output.append("\nFLOW:")
            self._generate_flow_diagram(graph, output)

        return '\n'.join(output)

    def _generate_flow_diagram(self, graph: Graph, output: List[str]):
        """Generate a flow diagram for the graph"""
        # Find entry points
        entry_nodes = [n for n in graph.nodes.values()
                      if 'Entry' in n.type or 'FuncEntry' in n.type]

        if not entry_nodes:
            output.append("  No entry point found")
            return

        # Trace execution flow
        for entry in entry_nodes:
            visited = set()
            self._trace_flow(entry, graph, output, visited, indent=2)

    def _trace_flow(self, node: Node, graph: Graph, output: List[str],
                    visited: set, indent: int = 2):
        """Recursively trace execution flow"""
        if node.id in visited:
            output.append(" " * indent + f"(loop to {node.name})")
            return

        visited.add(node.id)

        # Generate node description
        node_desc = node.name
        if 'function' in node.properties:
            func_parts = node.properties['function'].split(',')
            for part in func_parts:
                if 'MemberName=' in part:
                    func_name = part.split('=')[1].strip('"')
                    node_desc = f"{func_name}()"
                    break

        output.append(" " * indent + f"-> {node_desc}")

        # Follow execution pins
        exec_pins = [p for p in node.pins.values()
                    if p.direction == 'output' and 'then' in p.name.lower()]

        for pin in exec_pins:
            for next_node_id, _ in pin.linked_to:
                if next_node_id in graph.nodes:
                    self._trace_flow(graph.nodes[next_node_id], graph,
                                   output, visited, indent + 2)

    def simplify_file(self, input_path: str, output_path: str):
        """Main function to simplify a Blueprint text file"""
        print(f"Reading file: {input_path}")

        try:
            with open(input_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
        except Exception as e:
            print(f"Error reading file: {e}")
            return

        print(f"File size: {len(content)} bytes")
        print("Parsing content...")

        self.parse_content(content)

        print(f"Found {len(self.graphs)} graphs")

        # Generate simplified format
        simplified = self.generate_simplified_format()

        # Write output
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(simplified)

        print(f"Simplified output written to: {output_path}")
        print(f"Output size: {len(simplified)} bytes")
        print(f"Compression ratio: {(1 - len(simplified)/len(content))*100:.1f}%")


def main():
    """Main entry point"""
    # Check command line arguments
    if len(sys.argv) < 2:
        print("Usage: python animbp_simplify.py <input_file>")
        print("Example: python animbp_simplify.py AegisAnimInstance.txt")
        sys.exit(1)

    simplifier = UnrealBPSimplifier()

    # Get input file from command line
    input_file = sys.argv[1]

    # Generate output file names based on input file name (remove _raw suffix if present)
    base_name = os.path.splitext(input_file)[0]
    if base_name.endswith('_raw'):
        base_name = base_name[:-4]
    output_file = f"{base_name}_simplified.txt"
    json_output_file = f"{base_name}_data.json"

    # Process the file
    simplifier.simplify_file(input_file, output_file)

    # Also create a JSON output for programmatic access
    json_output = {}
    for graph_name, graph in simplifier.graphs.items():
        json_output[graph_name] = {
            'variables': graph.local_variables,
            'nodes': {
                node_id: {
                    'type': node.type,
                    'position': node.position,
                    'properties': node.properties,
                    'pins': {
                        pin_name: {
                            'type': pin.type,
                            'direction': pin.direction,
                            'connections': pin.linked_to,
                            'default': pin.default_value
                        }
                        for pin_name, pin in node.pins.items()
                    }
                }
                for node_id, node in graph.nodes.items()
            }
        }

    with open(json_output_file, 'w', encoding='utf-8') as f:
        json.dump(json_output, f, indent=2)

    print(f"JSON data written to: {json_output_file}")


if __name__ == "__main__":
    main()
