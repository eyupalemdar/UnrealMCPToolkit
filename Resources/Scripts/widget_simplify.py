#!/usr/bin/env python3
"""
Widget Blueprint Simplifier

Parses and simplifies UMG Widget Blueprint exports from Unreal Engine.
Extracts widget hierarchy, bindings, and event graphs.

Usage:
    python widget_simplify.py <input_file>

Output:
    Creates <input_file>_simplified.txt with structured widget data.
"""

import re
import sys
import os
from typing import Dict, List, Any, Optional
from dataclasses import dataclass, field


@dataclass
class WidgetInfo:
    """Represents a widget in the hierarchy"""
    name: str
    widget_class: str
    parent: Optional[str] = None
    children: List[str] = field(default_factory=list)
    properties: Dict[str, str] = field(default_factory=dict)
    slot_properties: Dict[str, str] = field(default_factory=dict)
    is_variable: bool = False


@dataclass
class BindingInfo:
    """Represents a property binding"""
    widget_name: str
    property_name: str
    function_name: str
    binding_type: str = "Function"


class WidgetBlueprintSimplifier:
    """Simplifier for Widget Blueprint exported text"""

    def __init__(self):
        self.widget_name = "Unknown"
        self.root_widget: Optional[str] = None
        self.widgets: Dict[str, WidgetInfo] = {}
        self.bindings: List[BindingInfo] = []
        self.animations: List[str] = []
        self.event_dispatchers: List[str] = []
        self.variables: Dict[str, str] = {}
        self.functions: List[str] = []
        # C++ format support
        self._is_cpp_format = False
        self._raw_content = ""

    def parse_content(self, content: str):
        """Parse the Widget Blueprint text content"""
        # Expand escaped characters
        content = content.replace('\\r\\n', '\n')
        content = content.replace('\\n', '\n')
        content = content.replace('\\t', '\t')
        content = content.replace('\\"', '"')

        # Check for C++ export format first (=== WIDGET BLUEPRINT: ===)
        # This format is already human-readable, so preserve it as-is
        if '=== WIDGET BLUEPRINT:' in content:
            self._is_cpp_format = True
            # Extract widget name from C++ format header
            name_match = re.search(r'=== WIDGET BLUEPRINT: ([^=]+) ===', content)
            if name_match:
                self.widget_name = name_match.group(1).strip()
            self._raw_content = content
            return

        # UE copy/paste format parsing below...

        # Extract widget blueprint name
        name_match = re.search(r'WidgetBlueprintGeneratedClass.*?Name="([^"]+)"', content)
        if name_match:
            self.widget_name = name_match.group(1)

        # Parse widget tree
        self._parse_widget_tree(content)

        # Parse bindings
        self._parse_bindings(content)

        # Parse animations
        self._parse_animations(content)

        # Parse variables
        self._parse_variables(content)

        # Parse functions from graph
        self._parse_functions(content)

    def _parse_widget_tree(self, content: str):
        """Parse the widget hierarchy"""
        # Find widget objects
        widget_pattern = r'Begin Object Class=([^\s]+)\s+Name="([^"]+)"(.*?)End Object'

        for match in re.finditer(widget_pattern, content, re.DOTALL):
            class_path = match.group(1)
            widget_name = match.group(2)
            widget_content = match.group(3)

            # Extract class name
            widget_class = class_path.split('.')[-1].replace("'", "")

            # Skip non-widget objects
            if not self._is_widget_class(widget_class):
                continue

            widget = WidgetInfo(
                name=widget_name,
                widget_class=widget_class
            )

            # Parse properties
            self._parse_widget_properties(widget_content, widget)

            # Check if it's a variable
            if 'bIsVariable=True' in widget_content:
                widget.is_variable = True

            self.widgets[widget_name] = widget

        # Build hierarchy from Slot references
        self._build_hierarchy()

    def _is_widget_class(self, class_name: str) -> bool:
        """Check if class name looks like a widget class"""
        widget_indicators = [
            'Widget', 'Panel', 'Button', 'Text', 'Image', 'Border',
            'Canvas', 'Horizontal', 'Vertical', 'Grid', 'Uniform',
            'ScrollBox', 'ListView', 'TreeView', 'Spacer', 'Slider',
            'ProgressBar', 'CheckBox', 'ComboBox', 'EditableText',
            'RichText', 'Overlay', 'WidgetSwitcher', 'SafeZone',
            'ScaleBox', 'SizeBox', 'Throbber', 'CircularThrobber',
            'WrapBox', 'NamedSlot', 'RetainerBox', 'InvalidationBox'
        ]
        return any(indicator in class_name for indicator in widget_indicators)

    def _parse_widget_properties(self, content: str, widget: WidgetInfo):
        """Parse widget-specific properties"""
        # Common properties to extract
        property_patterns = [
            (r'Text=NSLOCTEXT\([^,]+,[^,]+,\s*"([^"]+)"\)', 'Text'),
            (r'Text=INVTEXT\("([^"]+)"\)', 'Text'),
            (r'ToolTipText=NSLOCTEXT\([^,]+,[^,]+,\s*"([^"]+)"\)', 'Tooltip'),
            (r'Visibility=([^\s,)]+)', 'Visibility'),
            (r'RenderOpacity=([\d.]+)', 'Opacity'),
            (r'bIsEnabled=(\w+)', 'IsEnabled'),
            (r'ColorAndOpacity=\(([^)]+)\)', 'Color'),
            (r'Font=\(([^)]+)\)', 'Font'),
            (r'Brush=\(([^)]+)\)', 'Brush'),
            (r'Justification=([^\s,)]+)', 'Justification'),
            (r'Padding=\(([^)]+)\)', 'Padding'),
        ]

        for pattern, prop_name in property_patterns:
            match = re.search(pattern, content)
            if match:
                widget.properties[prop_name] = match.group(1)

        # Slot properties
        slot_patterns = [
            (r'Slot\.HorizontalAlignment=([^\s,)]+)', 'HAlign'),
            (r'Slot\.VerticalAlignment=([^\s,)]+)', 'VAlign'),
            (r'Slot\.Padding=\(([^)]+)\)', 'Padding'),
            (r'Slot\.Size\.SizeRule=([^\s,)]+)', 'SizeRule'),
            (r'Anchors=\(([^)]+)\)', 'Anchors'),
            (r'Offsets=\(([^)]+)\)', 'Offsets'),
        ]

        for pattern, prop_name in slot_patterns:
            match = re.search(pattern, content)
            if match:
                widget.slot_properties[prop_name] = match.group(1)

    def _build_hierarchy(self):
        """Build parent-child relationships from slot references"""
        # Look for Slots array in content which defines hierarchy
        # This is a simplified approach - real hierarchy comes from WidgetTree

        # Find root widget (usually CanvasPanel or first panel without parent)
        for name, widget in self.widgets.items():
            if 'Canvas' in widget.widget_class and widget.parent is None:
                self.root_widget = name
                break

        if not self.root_widget and self.widgets:
            # Pick first panel-type widget as root
            for name, widget in self.widgets.items():
                if 'Panel' in widget.widget_class:
                    self.root_widget = name
                    break

    def _parse_bindings(self, content: str):
        """Parse property bindings"""
        binding_pattern = r'Bindings\(\d+\)=\(([^)]+)\)'

        for match in re.finditer(binding_pattern, content):
            binding_str = match.group(1)

            # Extract binding details
            widget_match = re.search(r'ObjectName="([^"]+)"', binding_str)
            prop_match = re.search(r'PropertyName="([^"]+)"', binding_str)
            func_match = re.search(r'FunctionName="([^"]+)"', binding_str)

            if widget_match and prop_match and func_match:
                self.bindings.append(BindingInfo(
                    widget_name=widget_match.group(1),
                    property_name=prop_match.group(1),
                    function_name=func_match.group(1)
                ))

    def _parse_animations(self, content: str):
        """Parse widget animations"""
        anim_pattern = r'Animations\(\d+\)=([^\s,]+)'

        for match in re.finditer(anim_pattern, content):
            anim_ref = match.group(1)
            # Extract animation name
            name_match = re.search(r'["\']([^"\']+)["\']', anim_ref)
            if name_match:
                self.animations.append(name_match.group(1))

    def _parse_variables(self, content: str):
        """Parse widget variables (beyond bIsVariable widgets)"""
        var_pattern = r'NewVariables\(\d+\)=\(([^)]+)\)'

        for match in re.finditer(var_pattern, content):
            var_str = match.group(1)
            name_match = re.search(r'VarName="([^"]+)"', var_str)
            type_match = re.search(r'VarType=\([^)]*PinCategory="([^"]+)"', var_str)

            if name_match:
                var_name = name_match.group(1)
                var_type = type_match.group(1) if type_match else "unknown"
                self.variables[var_name] = var_type

    def _parse_functions(self, content: str):
        """Parse function names from graphs"""
        # Look for function entry nodes
        func_pattern = r'K2Node_FunctionEntry.*?FunctionReference=.*?MemberName="([^"]+)"'

        for match in re.finditer(func_pattern, content, re.DOTALL):
            func_name = match.group(1)
            if func_name not in self.functions:
                self.functions.append(func_name)

        # Also look for custom events
        event_pattern = r'K2Node_CustomEvent.*?CustomFunctionName="([^"]+)"'
        for match in re.finditer(event_pattern, content, re.DOTALL):
            event_name = match.group(1)
            if event_name not in self.functions:
                self.functions.append(f"Event: {event_name}")

    def generate_simplified(self) -> str:
        """Generate simplified output format"""
        # If C++ format, return as-is (already readable and well-structured)
        if self._is_cpp_format:
            return self._raw_content.strip()

        output = []
        output.append("=" * 60)
        output.append(f"WIDGET BLUEPRINT: {self.widget_name}")
        output.append("=" * 60)
        output.append("")

        # Widget hierarchy
        output.append("[Widget Hierarchy]")
        if self.root_widget:
            self._output_widget_tree(self.root_widget, output, indent=2)
        else:
            # Just list all widgets
            for name, widget in sorted(self.widgets.items()):
                var_marker = " [VAR]" if widget.is_variable else ""
                output.append(f"  - {name} ({widget.widget_class}){var_marker}")
        output.append("")

        # Widget variables (marked as bIsVariable)
        var_widgets = [w for w in self.widgets.values() if w.is_variable]
        if var_widgets:
            output.append("[Exposed Variables (Widgets)]")
            for widget in sorted(var_widgets, key=lambda w: w.name):
                output.append(f"  - {widget.name}: {widget.widget_class}")
            output.append("")

        # Custom variables
        if self.variables:
            output.append("[Custom Variables]")
            for var_name, var_type in sorted(self.variables.items()):
                output.append(f"  - {var_name}: {var_type}")
            output.append("")

        # Property bindings
        if self.bindings:
            output.append("[Property Bindings]")
            for binding in self.bindings:
                output.append(f"  - {binding.widget_name}.{binding.property_name} <- {binding.function_name}()")
            output.append("")

        # Animations
        if self.animations:
            output.append("[Animations]")
            for anim in self.animations:
                output.append(f"  - {anim}")
            output.append("")

        # Functions
        if self.functions:
            output.append("[Functions/Events]")
            for func in self.functions:
                output.append(f"  - {func}")
            output.append("")

        # Detailed widget properties
        output.append("[Widget Details]")
        for name, widget in sorted(self.widgets.items()):
            if widget.properties or widget.slot_properties:
                output.append(f"\n  {name} ({widget.widget_class}):")
                for prop, val in widget.properties.items():
                    output.append(f"    {prop}: {val}")
                for prop, val in widget.slot_properties.items():
                    output.append(f"    Slot.{prop}: {val}")

        return '\n'.join(output)

    def _output_widget_tree(self, widget_name: str, output: List[str], indent: int = 0):
        """Recursively output widget tree"""
        widget = self.widgets.get(widget_name)
        if not widget:
            return

        prefix = " " * indent
        var_marker = " [VAR]" if widget.is_variable else ""
        output.append(f"{prefix}- {widget.name} ({widget.widget_class}){var_marker}")

        # Output children
        for child_name in widget.children:
            self._output_widget_tree(child_name, output, indent + 2)


def simplify_widget(content: str) -> str:
    """Main function to simplify Widget Blueprint content"""
    simplifier = WidgetBlueprintSimplifier()
    simplifier.parse_content(content)
    return simplifier.generate_simplified()


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        print("Usage: python widget_simplify.py <input_file>")
        print("Example: python widget_simplify.py WBP_MainMenu.txt")
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
    simplified = simplify_widget(content)

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
