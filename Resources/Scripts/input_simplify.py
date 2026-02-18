#!/usr/bin/env python3
"""
Input Asset Simplifier

Parses and simplifies UInputAction and UInputMappingContext exports.
Extracts action configurations, triggers, modifiers, and key mappings.

Usage:
    python input_simplify.py <input_file>

Output:
    Creates <input_file>_simplified.txt with structured input data.
"""

import re
import sys
import os
from typing import Dict, List, Any, Optional
from dataclasses import dataclass, field


@dataclass
class TriggerInfo:
    """Represents an input trigger"""
    trigger_type: str
    properties: Dict[str, str] = field(default_factory=dict)


@dataclass
class ModifierInfo:
    """Represents an input modifier"""
    modifier_type: str
    properties: Dict[str, str] = field(default_factory=dict)


@dataclass
class InputActionInfo:
    """Represents an Input Action"""
    name: str
    value_type: str = "Digital"  # Digital, Axis1D, Axis2D, Axis3D
    description: str = ""
    triggers: List[TriggerInfo] = field(default_factory=list)
    modifiers: List[ModifierInfo] = field(default_factory=list)
    consume_input: bool = True
    trigger_when_paused: bool = False


@dataclass
class KeyMappingInfo:
    """Represents a key mapping in a context"""
    action_name: str
    key: str
    triggers: List[TriggerInfo] = field(default_factory=list)
    modifiers: List[ModifierInfo] = field(default_factory=list)


@dataclass
class MappingContextInfo:
    """Represents an Input Mapping Context"""
    name: str
    description: str = ""
    mappings: List[KeyMappingInfo] = field(default_factory=list)


class InputAssetSimplifier:
    """Simplifier for Input Action and Mapping Context exports"""

    # Value type mapping
    VALUE_TYPES = {
        'ETriggerValueType::Digital': 'Digital (bool)',
        'ETriggerValueType::Axis1D': 'Axis1D (float)',
        'ETriggerValueType::Axis2D': 'Axis2D (Vector2D)',
        'ETriggerValueType::Axis3D': 'Axis3D (Vector)',
        'Digital': 'Digital (bool)',
        'Axis1D': 'Axis1D (float)',
        'Axis2D': 'Axis2D (Vector2D)',
        'Axis3D': 'Axis3D (Vector)',
    }

    def __init__(self):
        self.asset_type = "Unknown"
        self.input_actions: Dict[str, InputActionInfo] = {}
        self.mapping_contexts: Dict[str, MappingContextInfo] = {}

    def parse_content(self, content: str):
        """Parse the input asset text content"""
        # Expand escaped characters
        content = content.replace('\\r\\n', '\n')
        content = content.replace('\\n', '\n')
        content = content.replace('\\t', '\t')
        content = content.replace('\\"', '"')

        # Detect asset type
        if 'InputMappingContext' in content or 'IMC_' in content:
            self.asset_type = "InputMappingContext"
            self._parse_mapping_context(content)
        elif 'InputAction' in content or 'IA_' in content:
            self.asset_type = "InputAction"
            self._parse_input_action(content)
        else:
            # Try both
            self._parse_input_action(content)
            self._parse_mapping_context(content)

    def _parse_input_action(self, content: str):
        """Parse Input Action content"""
        # Find InputAction objects
        action_pattern = r'Begin Object Class=.*?InputAction.*?Name="([^"]+)"(.*?)End Object'

        for match in re.finditer(action_pattern, content, re.DOTALL):
            action_name = match.group(1)
            action_content = match.group(2)

            action = InputActionInfo(name=action_name)

            # Parse value type
            value_match = re.search(r'ValueType=([^\s,)]+)', action_content)
            if value_match:
                raw_type = value_match.group(1)
                action.value_type = self.VALUE_TYPES.get(raw_type, raw_type)

            # Parse description
            desc_match = re.search(r'ActionDescription=NSLOCTEXT\([^,]+,[^,]+,\s*"([^"]+)"\)', action_content)
            if desc_match:
                action.description = desc_match.group(1)

            # Parse consume input
            consume_match = re.search(r'bConsumeInput=(\w+)', action_content)
            if consume_match:
                action.consume_input = consume_match.group(1).lower() == 'true'

            # Parse triggers
            action.triggers = self._parse_triggers(action_content)

            # Parse modifiers
            action.modifiers = self._parse_modifiers(action_content)

            self.input_actions[action_name] = action

        # Also look for simpler format
        if not self.input_actions:
            self._parse_simple_action_format(content)

    def _parse_simple_action_format(self, content: str):
        """Parse simpler Input Action format"""
        # Look for action name from filename or content
        name_match = re.search(r'Name="(IA_[^"]+)"', content)
        if name_match:
            action_name = name_match.group(1)
        else:
            action_name = "InputAction"

        action = InputActionInfo(name=action_name)

        # Value type
        if 'Axis2D' in content or 'FVector2D' in content:
            action.value_type = 'Axis2D (Vector2D)'
        elif 'Axis1D' in content:
            action.value_type = 'Axis1D (float)'
        elif 'Axis3D' in content:
            action.value_type = 'Axis3D (Vector)'
        else:
            action.value_type = 'Digital (bool)'

        action.triggers = self._parse_triggers(content)
        action.modifiers = self._parse_modifiers(content)

        self.input_actions[action_name] = action

    def _parse_mapping_context(self, content: str):
        """Parse Input Mapping Context content"""
        # Find context name
        context_pattern = r'Begin Object Class=.*?InputMappingContext.*?Name="([^"]+)"(.*?)End Object'

        for match in re.finditer(context_pattern, content, re.DOTALL):
            context_name = match.group(1)
            context_content = match.group(2)

            context = MappingContextInfo(name=context_name)

            # Parse mappings
            context.mappings = self._parse_key_mappings(context_content)

            self.mapping_contexts[context_name] = context

        # Also parse top-level mappings array
        if not self.mapping_contexts:
            self._parse_toplevel_mappings(content)

        # Try human-readable C++ export format
        if not self.mapping_contexts:
            self._parse_human_readable_context(content)

    def _parse_human_readable_context(self, content: str):
        """Parse human-readable C++ export format for Input Mapping Context.

        The C++ exporter produces format like:
            === INPUT MAPPING CONTEXT: IMC_Default ===
            ...
            [0] IA_Move
              Key: W
              Modifiers:
                - Negate (...)
              Triggers:
                - Down
            [1] IA_Look
              Key: Mouse2D
        """
        # Header: === INPUT MAPPING CONTEXT: IMC_Default ===
        header_match = re.search(
            r'=== INPUT MAPPING CONTEXT:\s+(\S+)\s+===', content)
        if not header_match:
            return

        context_name = header_match.group(1)
        context = MappingContextInfo(name=context_name)

        # Split content into mapping blocks starting with [N]
        # Find all [N] markers and extract blocks between them
        block_starts = list(re.finditer(r'^\s*\[(\d+)\]\s+(\S+)', content, re.MULTILINE))

        for i, match in enumerate(block_starts):
            action_name = match.group(2)

            # Get block content (from this [N] to next [N] or end)
            block_start = match.end()
            if i + 1 < len(block_starts):
                block_end = block_starts[i + 1].start()
            else:
                block_end = len(content)

            block_content = content[block_start:block_end]

            # Parse Key
            key_match = re.search(r'Key:\s+(\S+)', block_content)
            key = key_match.group(1) if key_match else "Unknown"

            mapping = KeyMappingInfo(action_name=action_name, key=key)

            # Parse Modifiers block
            modifiers_section = re.search(
                r'Modifiers:\s*\n((?:\s+-\s+.*\n)*)', block_content)
            if modifiers_section:
                for mod_match in re.finditer(r'-\s+(\w+)', modifiers_section.group(1)):
                    modifier = ModifierInfo(modifier_type=mod_match.group(1))
                    mapping.modifiers.append(modifier)

            # Parse Triggers block
            triggers_section = re.search(
                r'Triggers:\s*\n((?:\s+-\s+.*\n)*)', block_content)
            if triggers_section:
                for trig_match in re.finditer(r'-\s+(\w+)', triggers_section.group(1)):
                    trigger = TriggerInfo(trigger_type=trig_match.group(1))
                    mapping.triggers.append(trigger)

            context.mappings.append(mapping)

        if context.mappings:
            self.mapping_contexts[context_name] = context

    def _parse_toplevel_mappings(self, content: str):
        """Parse top-level Mappings array"""
        # Look for Mappings array
        mapping_pattern = r'Mappings\((\d+)\)=\(([^)]+(?:\([^)]*\)[^)]*)*)\)'

        context_name = "IMC_Default"
        name_match = re.search(r'Name="(IMC_[^"]+)"', content)
        if name_match:
            context_name = name_match.group(1)

        context = MappingContextInfo(name=context_name)

        for match in re.finditer(mapping_pattern, content):
            mapping_content = match.group(2)

            mapping = KeyMappingInfo(action_name="", key="")

            # Extract action reference
            action_match = re.search(r'Action=.*?(["\']?)([^"\']+)\1', mapping_content)
            if action_match:
                action_ref = action_match.group(2)
                # Extract action name from path
                action_name = action_ref.split('.')[-1].replace("'", "")
                mapping.action_name = action_name

            # Extract key
            key_match = re.search(r'Key=\(KeyName="([^"]+)"', mapping_content)
            if key_match:
                mapping.key = key_match.group(1)

            if mapping.action_name and mapping.key:
                context.mappings.append(mapping)

        if context.mappings:
            self.mapping_contexts[context_name] = context

    def _parse_key_mappings(self, content: str) -> List[KeyMappingInfo]:
        """Parse key mappings from content"""
        mappings = []

        # Enhanced mapping pattern
        mapping_pattern = r'Mappings\(\d+\)=\(([^)]+(?:\([^)]*\)[^)]*)*)\)'

        for match in re.finditer(mapping_pattern, content):
            mapping_str = match.group(1)

            mapping = KeyMappingInfo(action_name="", key="")

            # Action
            action_match = re.search(r'Action=.*?/([^/]+)\.([^"\']+)', mapping_str)
            if action_match:
                mapping.action_name = action_match.group(2).replace("'", "")

            # Key
            key_match = re.search(r'Key=\(KeyName="([^"]+)"', mapping_str)
            if key_match:
                mapping.key = key_match.group(1)

            # Triggers for this mapping
            mapping.triggers = self._parse_triggers(mapping_str)

            # Modifiers for this mapping
            mapping.modifiers = self._parse_modifiers(mapping_str)

            if mapping.action_name or mapping.key:
                mappings.append(mapping)

        return mappings

    def _parse_triggers(self, content: str) -> List[TriggerInfo]:
        """Parse triggers from content"""
        triggers = []

        # Look for trigger objects
        trigger_pattern = r'Triggers\(\d+\)=([^\s,]+)|Class=.*?(InputTrigger\w+)'

        for match in re.finditer(trigger_pattern, content):
            trigger_ref = match.group(1) or match.group(2)

            # Extract trigger type
            type_match = re.search(r'InputTrigger(\w+)', trigger_ref)
            if type_match:
                trigger_type = type_match.group(1)
                trigger = TriggerInfo(trigger_type=trigger_type)

                # Parse common trigger properties
                if 'HoldTime' in content:
                    hold_match = re.search(r'HoldTimeThreshold=([\d.]+)', content)
                    if hold_match:
                        trigger.properties['HoldTime'] = hold_match.group(1)

                triggers.append(trigger)

        return triggers

    def _parse_modifiers(self, content: str) -> List[ModifierInfo]:
        """Parse modifiers from content"""
        modifiers = []

        # Look for modifier objects
        modifier_pattern = r'Modifiers\(\d+\)=([^\s,]+)|Class=.*?(InputModifier\w+)'

        for match in re.finditer(modifier_pattern, content):
            modifier_ref = match.group(1) or match.group(2)

            # Extract modifier type
            type_match = re.search(r'InputModifier(\w+)', modifier_ref)
            if type_match:
                modifier_type = type_match.group(1)
                modifier = ModifierInfo(modifier_type=modifier_type)

                # Parse common modifier properties
                if 'Negate' in modifier_type:
                    negate_match = re.search(r'bX=(\w+).*?bY=(\w+).*?bZ=(\w+)', content)
                    if negate_match:
                        modifier.properties['Negate'] = f"X={negate_match.group(1)}, Y={negate_match.group(2)}, Z={negate_match.group(3)}"

                if 'Swizzle' in modifier_type:
                    swizzle_match = re.search(r'Order=(\w+)', content)
                    if swizzle_match:
                        modifier.properties['Order'] = swizzle_match.group(1)

                if 'DeadZone' in modifier_type:
                    deadzone_match = re.search(r'LowerThreshold=([\d.]+).*?UpperThreshold=([\d.]+)', content)
                    if deadzone_match:
                        modifier.properties['DeadZone'] = f"{deadzone_match.group(1)} - {deadzone_match.group(2)}"

                modifiers.append(modifier)

        return modifiers

    def generate_simplified(self) -> str:
        """Generate simplified output format"""
        output = []
        output.append("=" * 60)
        output.append(f"INPUT ASSET: {self.asset_type}")
        output.append("=" * 60)
        output.append("")

        # Input Actions
        if self.input_actions:
            output.append("[Input Actions]")
            for name, action in sorted(self.input_actions.items()):
                output.append(f"\n  {name}:")
                output.append(f"    ValueType: {action.value_type}")
                if action.description:
                    output.append(f"    Description: {action.description}")
                output.append(f"    ConsumeInput: {action.consume_input}")

                if action.triggers:
                    output.append("    Triggers:")
                    for trigger in action.triggers:
                        trigger_str = f"      - {trigger.trigger_type}"
                        if trigger.properties:
                            props = ", ".join(f"{k}={v}" for k, v in trigger.properties.items())
                            trigger_str += f" ({props})"
                        output.append(trigger_str)

                if action.modifiers:
                    output.append("    Modifiers:")
                    for modifier in action.modifiers:
                        mod_str = f"      - {modifier.modifier_type}"
                        if modifier.properties:
                            props = ", ".join(f"{k}={v}" for k, v in modifier.properties.items())
                            mod_str += f" ({props})"
                        output.append(mod_str)
            output.append("")

        # Mapping Contexts
        if self.mapping_contexts:
            output.append("[Input Mapping Contexts]")
            for name, context in sorted(self.mapping_contexts.items()):
                output.append(f"\n  {name}:")
                if context.description:
                    output.append(f"    Description: {context.description}")

                output.append("    Key Mappings:")
                for mapping in context.mappings:
                    output.append(f"      - {mapping.key} -> {mapping.action_name}")
                    if mapping.triggers:
                        triggers_str = ", ".join(t.trigger_type for t in mapping.triggers)
                        output.append(f"        Triggers: {triggers_str}")
                    if mapping.modifiers:
                        mods_str = ", ".join(m.modifier_type for m in mapping.modifiers)
                        output.append(f"        Modifiers: {mods_str}")
            output.append("")

        # Summary
        output.append("[Summary]")
        output.append(f"  Input Actions: {len(self.input_actions)}")
        output.append(f"  Mapping Contexts: {len(self.mapping_contexts)}")
        total_mappings = sum(len(c.mappings) for c in self.mapping_contexts.values())
        output.append(f"  Total Key Mappings: {total_mappings}")

        return '\n'.join(output)


def simplify_input(content: str) -> str:
    """Main function to simplify Input asset content"""
    simplifier = InputAssetSimplifier()
    simplifier.parse_content(content)
    return simplifier.generate_simplified()


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        print("Usage: python input_simplify.py <input_file>")
        print("Example: python input_simplify.py IA_Move.txt")
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
    simplified = simplify_input(content)

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
