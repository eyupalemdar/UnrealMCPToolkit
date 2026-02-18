#!/usr/bin/env python3
"""
GameplayAbility Simplifier

Parses and simplifies GameplayAbility Blueprint exports from Unreal Engine.
Extracts ability configuration, tasks, tags, and execution flow.

Usage:
    python ability_simplify.py <input_file>

Output:
    Creates <input_file>_simplified.txt with structured ability data.
"""

import re
import sys
import os
from typing import Dict, List, Any, Optional
from dataclasses import dataclass, field

# Import universal strip
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

try:
    from strip_utils import strip_file
except ImportError:
    strip_file = None


@dataclass
class AbilityTaskInfo:
    """Represents a Gameplay Ability Task"""
    task_name: str
    task_class: str
    factory_function: str = ""
    properties: Dict[str, str] = field(default_factory=dict)


@dataclass
class GameplayTagInfo:
    """Represents a Gameplay Tag"""
    tag_name: str
    tag_type: str = ""  # AbilityTags, BlockAbilitiesWithTag, etc.


@dataclass
class GameplayEffectInfo:
    """Represents a Gameplay Effect reference"""
    effect_name: str
    effect_type: str = ""  # Cost, Cooldown, Applied


class GameplayAbilitySimplifier:
    """Simplifier for GameplayAbility Blueprint exports"""

    def __init__(self):
        self.ability_name = "Unknown"
        self.ability_class = "GameplayAbility"
        self.parent_class = ""

        # Ability configuration
        self.config: Dict[str, str] = {}

        # Tags
        self.ability_tags: List[GameplayTagInfo] = []
        self.block_tags: List[GameplayTagInfo] = []
        self.cancel_tags: List[GameplayTagInfo] = []
        self.activation_required_tags: List[GameplayTagInfo] = []
        self.activation_blocked_tags: List[GameplayTagInfo] = []

        # Effects
        self.cost_effect: Optional[str] = None
        self.cooldown_effect: Optional[str] = None
        self.applied_effects: List[str] = []

        # Tasks
        self.tasks: List[AbilityTaskInfo] = []

        # Functions
        self.functions: List[str] = []
        self.events: List[str] = []

    def parse_content(self, content: str):
        """Parse the GameplayAbility Blueprint content"""
        # Expand escaped characters
        content = content.replace('\\r\\n', '\n')
        content = content.replace('\\n', '\n')
        content = content.replace('\\t', '\t')
        content = content.replace('\\"', '"')

        # Extract ability name
        name_match = re.search(r'Name="(GA_[^"]+)"', content)
        if name_match:
            self.ability_name = name_match.group(1)
        else:
            # Try alternative patterns
            name_match = re.search(r'BlueprintGeneratedClass.*?Name="([^"]+)"', content)
            if name_match:
                self.ability_name = name_match.group(1)

        # Parse parent class
        parent_match = re.search(r'ParentClass=.*?([^/]+)\'', content)
        if parent_match:
            self.parent_class = parent_match.group(1)

        # Parse configuration
        self._parse_config(content)

        # Parse tags
        self._parse_tags(content)

        # Parse effects
        self._parse_effects(content)

        # Parse ability tasks
        self._parse_tasks(content)

        # Parse functions and events
        self._parse_functions(content)

    def _parse_config(self, content: str):
        """Parse ability configuration properties"""
        config_patterns = [
            (r'bReplicateInputDirectly=(\w+)', 'ReplicateInputDirectly'),
            (r'bServerRespectsRemoteAbilityCancellation=(\w+)', 'ServerRespectsRemoteCancellation'),
            (r'NetExecutionPolicy=([^\s,)]+)', 'NetExecutionPolicy'),
            (r'NetSecurityPolicy=([^\s,)]+)', 'NetSecurityPolicy'),
            (r'InstancingPolicy=([^\s,)]+)', 'InstancingPolicy'),
            (r'bRetriggerInstancedAbility=(\w+)', 'RetriggerInstanced'),
            (r'bActivateOnInput=(\w+)', 'ActivateOnInput'),
            (r'bHasBlueprintActivate=(\w+)', 'HasBPActivate'),
            (r'bHasBlueprintCanUse=(\w+)', 'HasBPCanUse'),
            (r'bHasBlueprintActivateFromEvent=(\w+)', 'HasBPActivateFromEvent'),
        ]

        for pattern, config_name in config_patterns:
            match = re.search(pattern, content)
            if match:
                value = match.group(1)
                # Simplify enum values
                if '::' in value:
                    value = value.split('::')[-1]
                self.config[config_name] = value

    def _parse_tags(self, content: str):
        """Parse gameplay tags"""
        tag_sections = [
            ('AbilityTags', self.ability_tags),
            ('BlockAbilitiesWithTag', self.block_tags),
            ('CancelAbilitiesWithTag', self.cancel_tags),
            ('ActivationRequiredTags', self.activation_required_tags),
            ('ActivationBlockedTags', self.activation_blocked_tags),
        ]

        for section_name, tag_list in tag_sections:
            # Pattern for tag arrays
            pattern = rf'{section_name}=\(([^)]+)\)'
            match = re.search(pattern, content)
            if match:
                tags_str = match.group(1)
                # Extract individual tags
                tag_matches = re.findall(r'TagName="([^"]+)"', tags_str)
                for tag_name in tag_matches:
                    tag_list.append(GameplayTagInfo(
                        tag_name=tag_name,
                        tag_type=section_name
                    ))

            # Also check GameplayTagContainer format
            container_pattern = rf'{section_name}=\(GameplayTags=\(([^)]+)\)\)'
            container_match = re.search(container_pattern, content)
            if container_match:
                tags_str = container_match.group(1)
                tag_matches = re.findall(r'TagName="([^"]+)"', tags_str)
                for tag_name in tag_matches:
                    if not any(t.tag_name == tag_name for t in tag_list):
                        tag_list.append(GameplayTagInfo(
                            tag_name=tag_name,
                            tag_type=section_name
                        ))

    def _parse_effects(self, content: str):
        """Parse gameplay effect references"""
        # Cost effect
        cost_match = re.search(r'CostGameplayEffectClass=.*?([^/\'"]+)\'', content)
        if cost_match:
            self.cost_effect = cost_match.group(1)

        # Cooldown effect
        cooldown_match = re.search(r'CooldownGameplayEffectClass=.*?([^/\'"]+)\'', content)
        if cooldown_match:
            self.cooldown_effect = cooldown_match.group(1)

        # Applied effects (from MakeOutgoingGameplayEffectSpec calls)
        applied_pattern = r'MakeOutgoingGameplayEffectSpec.*?GameplayEffectClass=.*?([^/\'"]+)\''
        for match in re.finditer(applied_pattern, content):
            effect_name = match.group(1)
            if effect_name not in self.applied_effects:
                self.applied_effects.append(effect_name)

    def _parse_tasks(self, content: str):
        """Parse ability task usage"""
        # Look for K2Node_LatentAbilityCall nodes
        task_pattern = r'K2Node_LatentAbilityCall.*?ProxyFactoryFunctionName="([^"]+)".*?ProxyClass="([^"]+)"'

        for match in re.finditer(task_pattern, content, re.DOTALL):
            factory_func = match.group(1)
            proxy_class = match.group(2).split('/')[-1].replace("'", "")

            task = AbilityTaskInfo(
                task_name=proxy_class,
                task_class=proxy_class,
                factory_function=factory_func
            )
            self.tasks.append(task)

        # Also look for direct task references
        direct_pattern = r'AbilityTask_(\w+)'
        for match in re.finditer(direct_pattern, content):
            task_name = f"AbilityTask_{match.group(1)}"
            if not any(t.task_name == task_name for t in self.tasks):
                self.tasks.append(AbilityTaskInfo(
                    task_name=task_name,
                    task_class=task_name
                ))

    def _parse_functions(self, content: str):
        """Parse ability functions and events"""
        # Function entries
        func_pattern = r'K2Node_FunctionEntry.*?FunctionReference=.*?MemberName="([^"]+)"'
        for match in re.finditer(func_pattern, content, re.DOTALL):
            func_name = match.group(1)
            if func_name not in self.functions:
                self.functions.append(func_name)

        # Custom events
        event_pattern = r'K2Node_CustomEvent.*?CustomFunctionName="([^"]+)"'
        for match in re.finditer(event_pattern, content, re.DOTALL):
            event_name = match.group(1)
            if event_name not in self.events:
                self.events.append(event_name)

        # Standard ability events
        standard_events = ['ActivateAbility', 'EndAbility', 'CancelAbility',
                          'InputPressed', 'InputReleased', 'OnGiveAbility',
                          'OnRemoveAbility', 'OnAvatarSet']
        for event in standard_events:
            if event in content and event not in self.functions:
                self.functions.append(event)

        # Graceful degradation: capture unknown GAS-related node types
        # that aren't covered by the K2Node patterns above
        known_k2_prefixes = (
            'K2Node_FunctionEntry', 'K2Node_FunctionResult',
            'K2Node_CustomEvent', 'K2Node_Event',
            'K2Node_CallFunction', 'K2Node_VariableGet',
            'K2Node_VariableSet', 'K2Node_IfThenElse',
            'K2Node_ExecutionSequence', 'K2Node_LatentAbilityCall',
            'K2Node_PromotableOperator', 'K2Node_DynamicCast',
            'K2Node_Knot', 'K2Node_MacroInstance',
            'K2Node_CommutativeAssociativeBinaryOperator',
            'K2Node_PropertyAccess', 'K2Node_Message',
        )
        for m in re.finditer(
            r'Begin Object Class=\S+\.(\w+)\s+Name="([^"]+)"', content
        ):
            cls = m.group(1)
            name = m.group(2)
            if cls.startswith('K2Node_') and not cls.startswith(known_k2_prefixes):
                member = re.search(
                    rf'Begin Object.*?Name="{re.escape(name)}".*?MemberName="([^"]+)"',
                    content, re.DOTALL
                )
                label = f'{cls}: {member.group(1)}' if member else cls
                entry = f"[Unknown: {label}]"
                if entry not in self.functions:
                    self.functions.append(entry)

    def generate_simplified(self) -> str:
        """Generate simplified output format"""
        output = []
        output.append("=" * 60)
        output.append(f"GAMEPLAY ABILITY: {self.ability_name}")
        if self.parent_class:
            output.append(f"Parent: {self.parent_class}")
        output.append("=" * 60)
        output.append("")

        # Configuration
        if self.config:
            output.append("[Configuration]")
            for key, value in sorted(self.config.items()):
                output.append(f"  {key}: {value}")
            output.append("")

        # Tags
        all_tags = [
            ("Ability Tags", self.ability_tags),
            ("Blocks Abilities With", self.block_tags),
            ("Cancels Abilities With", self.cancel_tags),
            ("Activation Required", self.activation_required_tags),
            ("Activation Blocked", self.activation_blocked_tags),
        ]

        has_tags = any(tags for _, tags in all_tags)
        if has_tags:
            output.append("[Gameplay Tags]")
            for section_name, tags in all_tags:
                if tags:
                    output.append(f"  {section_name}:")
                    for tag in tags:
                        output.append(f"    - {tag.tag_name}")
            output.append("")

        # Effects
        has_effects = self.cost_effect or self.cooldown_effect or self.applied_effects
        if has_effects:
            output.append("[Gameplay Effects]")
            if self.cost_effect:
                output.append(f"  Cost: {self.cost_effect}")
            if self.cooldown_effect:
                output.append(f"  Cooldown: {self.cooldown_effect}")
            if self.applied_effects:
                output.append("  Applied Effects:")
                for effect in self.applied_effects:
                    output.append(f"    - {effect}")
            output.append("")

        # Tasks
        if self.tasks:
            output.append("[Ability Tasks]")
            for task in self.tasks:
                task_str = f"  - {task.task_name}"
                if task.factory_function:
                    task_str += f" (via {task.factory_function})"
                output.append(task_str)
            output.append("")

        # Functions and Events
        if self.functions or self.events:
            output.append("[Functions & Events]")
            if self.functions:
                output.append("  Functions:")
                for func in self.functions:
                    output.append(f"    - {func}")
            if self.events:
                output.append("  Custom Events:")
                for event in self.events:
                    output.append(f"    - {event}")
            output.append("")

        # Summary
        output.append("[Summary]")
        output.append(f"  Tags: {sum(len(tags) for _, tags in all_tags)}")
        output.append(f"  Tasks: {len(self.tasks)}")
        output.append(f"  Functions: {len(self.functions)}")
        output.append(f"  Events: {len(self.events)}")

        return '\n'.join(output)


def simplify_ability(content: str) -> str:
    """Main function to simplify GameplayAbility content"""
    simplifier = GameplayAbilitySimplifier()
    simplifier.parse_content(content)
    return simplifier.generate_simplified()


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        print("Usage: python ability_simplify.py <input_file>")
        print("Example: python ability_simplify.py GA_Weapon_Fire.txt")
        sys.exit(1)

    input_file = sys.argv[1]

    # Check file exists
    if not os.path.exists(input_file):
        print(f"Error: File not found: {input_file}")
        sys.exit(1)

    # Pass 1: Universal safe strip -> _stripped.txt
    if strip_file is not None and '_temp_raw' not in input_file:
        strip_file(input_file)
    else:
        print("Warning: strip_utils not found, skipping strip pass")

    # Pass 2: Type-specific simplify -> _simplified.txt
    # Read input
    try:
        with open(input_file, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
        print(f"File read: {input_file}")
    except Exception as e:
        print(f"Error reading file: {e}")
        sys.exit(1)

    # Simplify
    simplified = simplify_ability(content)

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
