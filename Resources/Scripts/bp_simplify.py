import re
import sys
import os
from collections import defaultdict, OrderedDict
import json

# Import universal strip
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

try:
    from strip_utils import strip_file
except ImportError:
    strip_file = None

# Placeholder for escaped quotes during preprocessing.
# Prevents DefaultValue="(TagName=\"...\")" from being truncated
# when \" is naively replaced with " before regex extraction.
_ESCAPED_QUOTE = '\x01'


class BlueprintSimplifier:
    def __init__(self):
        self.nodes = OrderedDict()
        self.connections = defaultdict(list)
        self.execution_flow = []

    def preprocess_blueprint_text(self, bp_text):
        """Processes single-line and BPGraph formatted blueprints."""
        # Check the BPGraph() wrapper
        if bp_text.strip().startswith('BPGraph('):
            # Extract the NodesString parameter
            # Capture entire NodesString even when it embeds escaped quotes
            nodes_match = re.search(
                r'NodesString="((?:\\.|[^"\\])*)"', bp_text, re.DOTALL
            )
            if nodes_match:
                bp_text = nodes_match.group(1)

        # Convert escaped newline sequences back to real newlines
        bp_text = bp_text.replace('\\r\\n', '\n')
        bp_text = bp_text.replace('\\n', '\n')
        bp_text = bp_text.replace('\\t', '\t')

        # Fix escaped single quotes (safe — single quotes don't delimit values)
        bp_text = bp_text.replace("\\'", "'")
        # Replace escaped double quotes with placeholder to preserve them
        # inside DefaultValue="..." during regex extraction
        bp_text = bp_text.replace('\\"', _ESCAPED_QUOTE)

        return bp_text

    def parse_blueprint_text(self, bp_text):
        """Parses and simplifies blueprint text content."""
        # Run preprocessing first
        bp_text = self.preprocess_blueprint_text(bp_text)

        output = []

        # Parse nodes
        node_pattern = r'Begin Object Class=([^\s]+)\s+Name="([^"]+)".*?End Object'

        # Check if we have any Begin Object patterns
        has_patterns = bool(re.search(node_pattern, bp_text, re.DOTALL))

        for match in re.finditer(node_pattern, bp_text, re.DOTALL):
            node_class = match.group(1)
            node_name = match.group(2)
            node_content = match.group(0)

            # Comment nodes
            if 'EdGraphNode_Comment' in node_class:
                comment = re.search(r'NodeComment="([^"]+)"', node_content)
                if comment:
                    clean_comment = comment.group(1).replace('\\r\\n', ' ').replace('\\n', ' ').replace("\\'", "'").replace(_ESCAPED_QUOTE, '"')
                    output.append(f"// {clean_comment}")
                continue

            # Remove unnecessary data
            simplified = self._simplify_node(node_class, node_name, node_content)
            if simplified:
                output.append(simplified)

        # Fallback: If no Begin Object patterns found (C++ export format),
        # preserve the original content as it's already readable
        if not has_patterns and not output:
            return bp_text.strip()

        return '\n'.join(output)

    def _simplify_node(self, node_class, node_name, content):
        """Simplifies node content."""
        lines = []

        # Determine the node type
        node_type = node_class.split('.')[-1].replace("'", "")
        lines.append(f"\n[{node_name}] Type: {node_type}")

        # Function Entry
        if 'K2Node_FunctionEntry' in node_class:
            func_ref = re.search(r'FunctionReference=.*?MemberName="([^"]+)"', content)
            if func_ref:
                lines.append(f"  FunctionEntry: {func_ref.group(1)}")
            # Function metadata
            metadata = re.search(r'MetaData=\(Category=NSLOCTEXT\([^,]+,[^,]+,\s*"([^"]+)"', content)
            if metadata:
                lines.append(f"  Category: {metadata.group(1)}")

        # Function Result
        elif 'K2Node_FunctionResult' in node_class:
            func_ref = re.search(r'FunctionReference=.*?MemberName="([^"]+)"', content)
            if func_ref:
                lines.append(f"  FunctionResult: {func_ref.group(1)}")
            # User defined pins (return values)
            user_pins = re.findall(r'UserDefinedPin.*?PinName="([^"]+)".*?PinCategory="([^"]+)"', content)
            for pin_name, pin_cat in user_pins:
                lines.append(f"  Returns: {pin_name} ({pin_cat})")

        # Event node
        elif 'K2Node_Event' in node_class:
            event_ref = re.search(r'EventReference=.*?MemberName="([^"]+)"', content)
            if event_ref:
                lines.append(f"  Event: {event_ref.group(1)}")

        # Custom Event
        elif 'K2Node_CustomEvent' in node_class:
            custom_name = re.search(r'CustomFunctionName="([^"]+)"', content)
            if custom_name:
                lines.append(f"  CustomEvent: {custom_name.group(1)}")

        # Function Call
        elif 'K2Node_CallFunction' in node_class:
            func_ref = re.search(r'FunctionReference=.*?MemberName="([^"]+)"', content)
            member_parent = re.search(r'MemberParent="([^"]+)"', content)
            if func_ref:
                func_name = func_ref.group(1)
                lines.append(f"  Function: {func_name}")
                if member_parent:
                    parent = member_parent.group(1).split('/')[-1].replace("'", "")
                    lines.append(f"  Class: {parent}")

        # Variable Get/Set
        elif 'K2Node_VariableGet' in node_class or 'K2Node_VariableSet' in node_class:
            var_ref = re.search(r'VariableReference=.*?MemberName="([^"]+)"', content)
            if var_ref:
                action = "Get" if "VariableGet" in node_class else "Set"
                lines.append(f"  Variable{action}: {var_ref.group(1)}")

        # Dynamic Cast
        elif 'K2Node_DynamicCast' in node_class:
            target = re.search(r'TargetType="([^"]+)"', content)
            if target:
                cast_to = target.group(1).split('/')[-1].replace("'", "")
                lines.append(f"  CastTo: {cast_to}")

        # Branch (If-Then-Else)
        elif 'K2Node_IfThenElse' in node_class:
            lines.append(f"  Branch: If-Then-Else")

        # Promotable Operator (Math operations)
        elif 'K2Node_PromotableOperator' in node_class:
            op_name = re.search(r'OperationName="([^"]+)"', content)
            func_name = re.search(r'MemberName="([^"]+)"', content)
            if op_name:
                lines.append(f"  Operation: {op_name.group(1)}")
            if func_name:
                lines.append(f"  Function: {func_name.group(1)}")

        # Execution Sequence
        elif 'K2Node_ExecutionSequence' in node_class:
            lines.append(f"  ExecutionSequence: Multiple outputs")

        # Latent Ability Call
        elif 'K2Node_LatentAbilityCall' in node_class:
            factory = re.search(r'ProxyFactoryFunctionName="([^"]+)"', content)
            proxy_class = re.search(r'ProxyClass="([^"]+)"', content)
            if factory:
                lines.append(f"  AbilityTask: {factory.group(1)}")
            if proxy_class:
                task_class = proxy_class.group(1).split('/')[-1].replace("'", "")
                lines.append(f"  TaskClass: {task_class}")

        # Message (Interface call)
        elif 'K2Node_Message' in node_class:
            func_ref = re.search(r'FunctionReference=.*?MemberName="([^"]+)"', content)
            if func_ref:
                lines.append(f"  InterfaceCall: {func_ref.group(1)}")

        # Macro Instance
        elif 'K2Node_MacroInstance' in node_class:
            macro_ref = re.search(r'MacroGraphReference=.*?MacroGraph="([^"]+)"', content)
            if macro_ref:
                macro_path = macro_ref.group(1)
                macro_name = macro_path.split(':')[-1].replace("'", "")
                lines.append(f"  Macro: {macro_name}")
            else:
                # Alternative format
                macro_name_alt = re.search(r'ReferencedMacro="([^"]+)"', content)
                if macro_name_alt:
                    lines.append(f"  Macro: {macro_name_alt.group(1)}")

        # Unknown K2Node type — graceful degradation
        elif 'K2Node_' in node_class or 'EdGraphNode_' in node_class:
            # Try to extract any useful info
            member = re.search(r'MemberName="([^"]+)"', content)
            func_ref = re.search(r'FunctionReference=.*?MemberName="([^"]+)"', content)
            if func_ref:
                lines.append(f"  [K2: {node_type}] Function: {func_ref.group(1)}")
            elif member:
                lines.append(f"  [K2: {node_type}] Member: {member.group(1)}")
            else:
                lines.append(f"  [K2: {node_type}]")

        # Parse values and connections
        self._parse_values_and_connections(content, lines)

        return '\n'.join(lines) if len(lines) > 1 else None

    def _parse_values_and_connections(self, content, lines):
        """Parses pin values and connections."""
        for pin_block in self._iter_pin_blocks(content):
            pin_name = self._safe_search(r'PinName="([^"]+)"', pin_block)
            if not pin_name:
                continue

            direction = self._pin_direction_label(pin_block)
            pin_category = self._safe_search(r'PinType\.PinCategory="([^"]+)"', pin_block)
            pin_subcategory = self._safe_search(r'PinType\.PinSubCategory="([^"]+)"', pin_block)
            pin_sub_obj = self._safe_search(r'PinType\.PinSubCategoryObject="([^"]+)"', pin_block)
            pin_type = self._format_pin_type(pin_category, pin_subcategory, pin_sub_obj)

            default_value = self._safe_search(r'DefaultValue="([^"]*)"', pin_block)
            autogenerated_value = self._safe_search(r'AutogeneratedDefaultValue="([^"]*)"', pin_block)
            value = default_value if default_value is not None else autogenerated_value
            value = value if value not in (None, "") else None

            # Restore escaped quotes that were replaced with placeholder
            if value is not None:
                value = value.replace(_ESCAPED_QUOTE, '"')

            # Extract DefaultObject for class/asset-reference pins
            # (e.g., GameplayEffect, GamePhaseAbility, specific blueprint refs).
            # Skip Default__ static library targets (KismetMathLibrary, etc.)
            default_obj = None
            default_obj_raw = self._safe_search(
                r'DefaultObject="?([^",)\s]+)"?', pin_block)
            if default_obj_raw and 'Default__' not in default_obj_raw:
                obj_name = default_obj_raw.split('/')[-1]
                if '.' in obj_name:
                    obj_name = obj_name.split('.')[-1]
                if obj_name.endswith('_C'):
                    obj_name = obj_name[:-2]
                if obj_name:
                    default_obj = obj_name

            link_targets = self._extract_link_targets(pin_block)
            if link_targets:
                link_targets = list(dict.fromkeys(link_targets))

            parts = [f"  Pin[{direction}] {pin_name}"]
            if pin_type:
                parts.append(f": {pin_type}")
            if value is not None:
                parts.append(f" Default={value}")
            if default_obj:
                parts.append(f" DefaultObj={default_obj}")
            if link_targets:
                parts.append(f" -> {', '.join(link_targets)}")
            lines.append("".join(parts))

    def _safe_search(self, pattern, text):
        match = re.search(pattern, text)
        return match.group(1) if match else None

    def _format_pin_type(self, category, subcategory, sub_obj):
        parts = []
        if category:
            parts.append(category)
        if subcategory:
            parts.append(subcategory)
        if sub_obj:
            parts.append(sub_obj.split('/')[-1].replace("'", ""))
        return " ".join(parts).strip()

    def _pin_direction_label(self, pin_block):
        is_output = 'Direction="EGPD_Output"' in pin_block
        pin_category = self._safe_search(r'PinType\.PinCategory="([^"]+)"', pin_block)
        if pin_category == "exec":
            return "ExecOut" if is_output else "ExecIn"
        return "Out" if is_output else "In"

    def _extract_link_targets(self, pin_block):
        targets = []
        for linked in re.findall(r'LinkedTo=\(([^)]+)\)', pin_block):
            node_matches = re.findall(r'K2Node_[A-Za-z0-9_]+', linked)
            if node_matches:
                targets.extend(node_matches)
                continue
            tokens = [token for token in re.split(r'[,\s]+', linked.strip()) if token]
            if tokens:
                targets.append(tokens[0])
        return targets

    def _iter_pin_blocks(self, content):
        marker = 'CustomProperties Pin ('
        start = 0
        while True:
            idx = content.find(marker, start)
            if idx == -1:
                break
            idx += len(marker)
            depth = 1
            end = idx
            while end < len(content) and depth > 0:
                char = content[end]
                if char == '(':
                    depth += 1
                elif char == ')':
                    depth -= 1
                end += 1
            yield content[idx:end-1]
            start = end


def _split_sections(content):
    """Split content by === HEADER === markers. Returns [(header, body), ...]."""
    pattern = re.compile(r'^(===\s+.+?\s+===)\s*$', re.MULTILINE)
    parts = pattern.split(content)
    # parts = [before_first_header, header1, body1, header2, body2, ...]
    sections = []
    i = 1  # skip anything before the first header
    while i < len(parts) - 1:
        header = parts[i].strip()
        body = parts[i + 1]
        sections.append((header, body))
        i += 2
    return sections


def _extract_section_name(header):
    """Extract section name from === NAME === header."""
    m = re.match(r'===\s+(.+?)\s+===', header)
    return m.group(1) if m else header


def _simplify_blueprint_cpp(bp_text):
    """Simplify C++ export format blueprint, preserving section structure."""
    sections = _split_sections(bp_text)
    out = []

    for header, body in sections:
        name = _extract_section_name(header)

        if name.startswith('GRAPH:') or name.startswith('EVENT GRAPH:'):
            # Apply K2 graph simplification to graph sections
            graph_name = name.split(':', 1)[1].strip() if ':' in name else name
            body_stripped = body.strip()
            if not body_stripped:
                continue

            # Check if there are Begin Object blocks (K2 nodes)
            has_begin_objects = bool(re.search(
                r'Begin Object Class=([^\s]+)\s+Name="([^"]+)"', body_stripped, re.DOTALL))

            if has_begin_objects:
                simplifier = BlueprintSimplifier()
                simplified = simplifier.parse_blueprint_text(body_stripped)
                if simplified and simplified.strip():
                    out.append(f'=== GRAPH: {graph_name} (simplified) ===')
                    out.append(simplified)
                    out.append('')
                else:
                    out.append(header)
                    out.append(body_stripped)
                    out.append('')
            else:
                # No Begin Object blocks — pass through (e.g., comment-only graphs)
                out.append(header)
                if body_stripped:
                    out.append(body_stripped)
                out.append('')
        else:
            # Non-graph sections: BLUEPRINT header, INTERFACES, CLASS DEFAULTS,
            # VARIABLES, etc. — pass through as they're already concise
            out.append(header)
            body_stripped = body.strip()
            if body_stripped:
                out.append(body_stripped)
            out.append('')

    return '\n'.join(out)


def simplify_blueprint(bp_text):
    """Simplifies blueprint text."""
    # C++ export format detection — route to section-aware parser
    if '=== BLUEPRINT:' in bp_text:
        return _simplify_blueprint_cpp(bp_text)

    # Original UE copy/paste format path (unchanged)
    simplifier = BlueprintSimplifier()
    output = []
    simplified_content = simplifier.parse_blueprint_text(bp_text)
    output.append(simplified_content)

    return '\n'.join(output)


# Usage example
if __name__ == "__main__":
    # Get the file name from the argument or prompt the user
    if len(sys.argv) > 1:
        input_file = sys.argv[1]
    else:
        input_file = input("Blueprint dosya adini girin (orn: GA_Weapon_AutoReload.txt): ")

    # Ensure the file exists
    if not os.path.exists(input_file):
        print(f"Hata: '{input_file}' dosyasi bulunamadi!")
        sys.exit(1)

    # Pass 1: Universal safe strip -> _stripped.txt
    if strip_file is not None and '_temp_raw' not in input_file:
        strip_file(input_file)
    else:
        print("Warning: strip_utils not found, skipping strip pass")

    # Pass 2: Type-specific simplify -> _simplified.txt
    # Read from the file
    try:
        with open(input_file, 'r', encoding='utf-8') as f:
            blueprint_text = f.read()
        print(f"Dosya okundu: {input_file}")
    except Exception as e:
        print(f"Dosya okuma hatasi: {e}")
        sys.exit(1)

    # Simplify
    simplified = simplify_blueprint(blueprint_text)

    # Build the output file name (remove _raw suffix if present)
    base_name = os.path.splitext(input_file)[0]
    if base_name.endswith('_raw'):
        base_name = base_name[:-4]
    output_file = f"{base_name}_simplified.txt"

    # Save the output to a file
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(simplified)
        print(f"Sadelestirilen dosya kaydedildi: {output_file}")

        # Size comparison
        original_size = os.path.getsize(input_file)
        simplified_size = os.path.getsize(output_file)
        reduction = ((original_size - simplified_size) / original_size) * 100

        print(f"\nBoyut karsilastirmasi:")
        print(f"  Orijinal: {original_size:,} byte")
        print(f"  Sadelestirilen: {simplified_size:,} byte")
        print(f"  Azaltma: %{reduction:.1f}")

    except Exception as e:
        print(f"Dosya yazma hatasi: {e}")
        sys.exit(1)
