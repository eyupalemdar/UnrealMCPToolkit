#!/usr/bin/env python3
"""
Widget Blueprint Simplifier

Parses and simplifies UMG Widget Blueprint exports from Unreal Engine.
Extracts widget hierarchy, bindings, variables, class defaults, and event graphs.

Usage:
    python widget_simplify.py <input_file>

Output:
    Creates <input_file>_simplified.txt with structured widget data.
"""

import re
import sys
import os
from typing import Dict, List, Any, Optional, Tuple
from dataclasses import dataclass, field

# Import BlueprintSimplifier for graph parsing
try:
    from bp_simplify import BlueprintSimplifier
except ImportError:
    BlueprintSimplifier = None

# Import universal strip
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

try:
    from strip_utils import strip_file
except ImportError:
    strip_file = None


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


@dataclass
class VariableInfo:
    """Represents a blueprint variable"""
    name: str
    var_type: str
    default_value: Optional[str] = None
    flags: List[str] = field(default_factory=list)


@dataclass
class InterfaceInfo:
    """Represents an implemented interface"""
    name: str
    functions: List[str] = field(default_factory=list)


@dataclass
class AnimationInfo:
    """Represents a widget animation"""
    name: str
    duration: float = 0.0
    bound_widgets: List[str] = field(default_factory=list)
    tracks: List[Dict[str, Any]] = field(default_factory=list)


@dataclass
class GraphInfo:
    """Represents a blueprint graph (event graph, function graph, etc.)"""
    name: str
    graph_type: str = "EventGraph"  # EventGraph, FunctionGraph, MacroGraph
    simplified_content: str = ""


class WidgetBlueprintSimplifier:
    """Simplifier for Widget Blueprint exported text"""

    # Noise patterns to filter out from widget properties
    NOISE_PATTERNS = [
        r'.*Delegate=\(null\)\.None',
        r'bHiddenInDesigner=.*',
        r'bExpandedInDesigner=.*',
        r'bLockedInDesigner=.*',
        r'PersistentGuid=.*',
        r'NodeGuid=.*',
        r'NodePosX=.*',
        r'NodePosY=.*',
        r'PinId=.*',
        r'ExportPath=.*',
        r'bOrphanedPin=.*',
        r'bAdvancedView=.*',
        r'bDefaultValueIsReadOnly=.*',
        r'bDefaultValueIsIgnored=.*',
        r'bNotConnectable=.*',
        r'bHidden=False',
    ]

    # Properties to keep in simplified output (semantic properties)
    SEMANTIC_WIDGET_PROPS = {
        'Text', 'Visibility', 'bIsVariable', 'Font', 'ColorAndOpacity',
        'Justification', 'Brush', 'Style', 'ToolTipText', 'IsEnabled',
        'RenderOpacity', 'MinDesiredWidth', 'MinDesiredHeight', 'Padding',
        'HorizontalAlignment', 'VerticalAlignment', 'bIsReadOnly', 'HintText',
        'Content', 'Image', 'BackgroundColor', 'ForegroundColor'
    }

    # Class default properties to filter as noise
    NOISE_CLASS_DEFAULTS = {
        'bHiddenInDesigner', 'bExpandedInDesigner', 'bLockedInDesigner',
        'bOverrideAccessibleDefaults', 'bCanChildrenBeAccessible',
        'AccessibleBehavior', 'AccessibleSummaryBehavior', 'FlowDirectionPreference',
        'bOverride_Cursor', 'Cursor', 'Clipping', 'PixelSnapping',
        'RenderTransformPivot', 'DesignTimeSize', 'DesignSizeMode', 'PaletteCategory',
        'bAutomaticallyRegisterInputOnConstruction', 'DesiredFocusWidget'
    }

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

        # New fields for enhanced parsing
        self.parent_class: str = ""
        self.named_slots: List[str] = []
        self.animations_full: List[AnimationInfo] = []
        self.class_defaults: Dict[str, str] = {}
        self.blueprint_variables: List[VariableInfo] = []
        self.interfaces: List[InterfaceInfo] = []
        self.graphs: List[GraphInfo] = []
        self._sections: Dict[str, str] = {}

    def parse_content(self, content: str):
        """Parse the Widget Blueprint text content"""
        # Expand escaped characters
        content = content.replace('\\r\\n', '\n')
        content = content.replace('\\n', '\n')
        content = content.replace('\\t', '\t')
        content = content.replace('\\"', '"')

        # Check for C++ export format first (=== WIDGET BLUEPRINT: ===)
        if '=== WIDGET BLUEPRINT:' in content or '=== WIDGET TREE ===' in content:
            self._is_cpp_format = True
            # Extract widget name from C++ format header
            name_match = re.search(r'=== WIDGET BLUEPRINT: ([^=]+) ===', content)
            if name_match:
                self.widget_name = name_match.group(1).strip()
            self._raw_content = content
            self._parse_cpp_format(content)
            return

        # UE copy/paste format parsing below...
        name_match = re.search(r'WidgetBlueprintGeneratedClass.*?Name="([^"]+)"', content)
        if name_match:
            self.widget_name = name_match.group(1)

        self._parse_widget_tree(content)
        self._parse_bindings(content)
        self._parse_animations(content)
        self._parse_variables(content)
        self._parse_functions(content)

    def _split_into_sections(self, content: str) -> Dict[str, str]:
        """Split content into sections based on === SECTION === markers"""
        sections = {}
        current_section = "_header"
        current_content = []

        for line in content.split('\n'):
            section_match = re.match(r'^===\s*([^=]+?)\s*===\s*$', line)
            if section_match:
                # Save previous section
                if current_content:
                    sections[current_section] = '\n'.join(current_content)
                current_section = section_match.group(1).strip()
                current_content = []
            else:
                current_content.append(line)

        # Save last section
        if current_content:
            sections[current_section] = '\n'.join(current_content)

        return sections

    def _parse_cpp_format(self, content: str):
        """Parse C++ export format (=== SECTION === style)"""
        self._sections = self._split_into_sections(content)

        # Parse header section for parent class
        self._parse_widget_header()

        # Parse widget tree section
        if 'WIDGET TREE' in self._sections:
            self._parse_cpp_widget_tree(self._sections['WIDGET TREE'])

        # Parse named slots
        if 'NAMED SLOTS' in self._sections:
            self._parse_named_slots(self._sections['NAMED SLOTS'])

        # Parse animations section
        if 'ANIMATIONS' in self._sections:
            self._parse_animations_full(self._sections['ANIMATIONS'])

        # Parse bindings section
        if 'BINDINGS' in self._sections:
            self._parse_bindings_section(self._sections['BINDINGS'])

        # Parse class defaults
        if 'CLASS DEFAULTS' in self._sections:
            self._parse_class_defaults(self._sections['CLASS DEFAULTS'])

        # Parse variables section
        if 'VARIABLES' in self._sections:
            self._parse_variables_section(self._sections['VARIABLES'])

        # Parse interfaces
        if 'INTERFACES' in self._sections:
            self._parse_interfaces(self._sections['INTERFACES'])

        # Parse all graph sections
        for section_name, section_content in self._sections.items():
            if section_name.startswith('GRAPH:'):
                graph_name = section_name[6:].strip()
                self._parse_graph(graph_name, section_content)
            elif section_name == 'EVENT GRAPH':
                self._parse_graph('EventGraph', section_content)

    def _parse_widget_header(self):
        """Parse widget header for parent class and other metadata"""
        header = self._sections.get('_header', '')

        # Extract parent class
        parent_match = re.search(r'ParentClass:\s*(\S+)', header)
        if parent_match:
            self.parent_class = parent_match.group(1)

        # Also check WIDGET BLUEPRINT section
        for section_name, section_content in self._sections.items():
            if 'WIDGET BLUEPRINT:' in section_name or 'BLUEPRINT:' in section_name:
                parent_match = re.search(r'ParentClass:\s*(\S+)', section_content)
                if parent_match:
                    self.parent_class = parent_match.group(1)
                    break

    def _parse_cpp_widget_tree(self, tree_content: str):
        """Parse widget tree from C++ format"""
        parent_stack = []  # Stack of (indent, widget_name)
        current_widget = None
        current_widget_name = None

        for line in tree_content.split('\n'):
            if not line.strip():
                continue

            # Match widget line: [WidgetName] WidgetClass
            widget_match = re.match(r'^(\s*)\[([^\]]+)\]\s*(\S+)', line)
            if widget_match:
                indent = len(widget_match.group(1))
                widget_name = widget_match.group(2)
                widget_class = widget_match.group(3)

                # Create widget info
                widget = WidgetInfo(
                    name=widget_name,
                    widget_class=widget_class
                )

                # Update parent stack based on indent
                while parent_stack and parent_stack[-1][0] >= indent:
                    parent_stack.pop()

                if parent_stack:
                    widget.parent = parent_stack[-1][1]
                    if widget.parent in self.widgets:
                        self.widgets[widget.parent].children.append(widget_name)
                else:
                    self.root_widget = widget_name

                parent_stack.append((indent, widget_name))
                self.widgets[widget_name] = widget
                current_widget = widget
                current_widget_name = widget_name
                continue

            # Parse widget properties (indented lines after widget declaration)
            if current_widget and line.strip():
                prop_line = line.strip()

                # Check for bIsVariable
                if prop_line.startswith('bIsVariable:') and 'true' in prop_line.lower():
                    current_widget.is_variable = True
                    continue

                # Check for Slot properties
                slot_match = re.match(r'Slot\.(\w+)(?:=|:)\s*(.+)', prop_line)
                if slot_match:
                    prop_name = slot_match.group(1)
                    prop_value = slot_match.group(2).strip()
                    current_widget.slot_properties[prop_name] = prop_value
                    continue

                # Regular property with = or :
                prop_match = re.match(r'(\w+(?:\.\w+)*)(?:=|:)\s*(.+)', prop_line)
                if prop_match:
                    prop_name = prop_match.group(1)
                    prop_value = prop_match.group(2).strip()

                    # Filter noise
                    if not self._is_noise_property(prop_name, prop_value):
                        current_widget.properties[prop_name] = prop_value

    def _is_noise_property(self, prop_name: str, prop_value: str) -> bool:
        """Check if a property should be filtered as noise"""
        # Check null delegates (any delegate with (null).None or containing (null))
        if prop_value == '(null).None':
            return True
        if prop_name.endswith('Delegate') and '(null)' in prop_value:
            return True
        if prop_name.startswith('OnMouse') and prop_name.endswith('Event') and '(null)' in prop_value:
            return True

        # Slots (internal UMG slot references)
        if prop_name == 'Slots':
            return True

        # Check designer flags and known noise properties
        noise_props = [
            'bHiddenInDesigner', 'bExpandedInDesigner', 'bLockedInDesigner',
            'bOverrideAccessibleDefaults', 'bCanChildrenBeAccessible',
            'AccessibleTextDelegate', 'AccessibleSummaryTextDelegate',
            'bIsEnabledDelegate', 'ToolTipTextDelegate', 'ToolTipWidgetDelegate',
            'VisibilityDelegate', 'PersistentGuid', 'NodeGuid', 'DisplayLabel'
        ]
        if prop_name in noise_props:
            return True

        # bOverride_* flags: drop when False (override disabled)
        if prop_name.startswith('bOverride_') and prop_value.strip().lower() == 'false':
            return True

        # Check default transform values
        if prop_name == 'RenderTransform':
            if 'Translation=(X=0.000000,Y=0.000000)' in prop_value and \
               'Scale=(X=1.000000,Y=1.000000)' in prop_value and \
               'Shear=(X=0.000000,Y=0.000000)' in prop_value:
                return True

        if prop_name == 'RenderTransformPivot':
            if prop_value == '(X=0.500000,Y=0.500000)':
                return True

        # Check default pixel snapping
        if prop_name == 'PixelSnapping' and prop_value == 'Inherit':
            return True

        # ShadowColorAndOpacity: noise when alpha is 0
        if prop_name == 'ShadowColorAndOpacity':
            if 'A=0.000000' in prop_value or 'A=0' in prop_value:
                return True

        # ShadowOffset: default (X=1,Y=1)
        if prop_name == 'ShadowOffset':
            v = prop_value.strip()
            if v in ('(X=1.000000,Y=1.000000)', '(X=1,Y=1)'):
                return True

        # StrikeBrush: default (ImageSize 32x32)
        if prop_name == 'StrikeBrush':
            if 'ImageSize=(X=32.000000,Y=32.000000)' in prop_value or \
               'ImageSize=(X=32,Y=32)' in prop_value:
                return True

        # DesiredSizeScale: default (1,1)
        if prop_name == 'DesiredSizeScale':
            v = prop_value.strip()
            if v in ('(X=1.000000,Y=1.000000)', '(X=1,Y=1)', '(1,1)'):
                return True

        # ContentColorAndOpacity: default white (1,1,1,1)
        if prop_name == 'ContentColorAndOpacity':
            v = prop_value.strip()
            if v in ('(R=1.000000,G=1.000000,B=1.000000,A=1.000000)',
                     '(R=1,G=1,B=1,A=1)', '(1,1,1,1)'):
                return True

        # BrushColor: default white (1,1,1,1)
        if prop_name == 'BrushColor':
            v = prop_value.strip()
            if v in ('(R=1.000000,G=1.000000,B=1.000000,A=1.000000)',
                     '(R=1,G=1,B=1,A=1)', '(1,1,1,1)'):
                return True

        # Default alignment values
        if prop_name == 'HorizontalAlignment' and prop_value.strip() == 'HAlign_Fill':
            return True
        if prop_name == 'VerticalAlignment' and prop_value.strip() == 'VAlign_Fill':
            return True

        # Default text properties
        if prop_name == 'Justification' and prop_value.strip() in ('Left', 'ETextJustify::Left'):
            return True
        if prop_name == 'TextOverflowPolicy' and prop_value.strip() in ('Clip', 'ETextOverflowPolicy::Clip'):
            return True
        if prop_name == 'WrappingPolicy' and prop_value.strip() in ('DefaultWrapping', 'ETextWrappingPolicy::DefaultWrapping'):
            return True
        if prop_name == 'AutoWrapText' and prop_value.strip().lower() == 'false':
            return True
        if prop_name == 'MinDesiredWidth' and prop_value.strip() in ('0', '0.0', '0.000000'):
            return True
        if prop_name == 'WrapTextAt' and prop_value.strip() in ('0', '0.0', '0.000000'):
            return True
        if prop_name == 'LineHeightPercentage' and prop_value.strip() in ('1.0', '1.000000', '1'):
            return True

        # Default boolean properties (noise when at default value)
        default_true_bools = {
            'bShowEffectWhenDisabled', 'ApplyLineHeightToBottomLine',
        }
        default_false_bools = {
            'bFlipForRightToLeftFlowDirection', 'bWrapWithInvalidationPanel',
            'bSimpleTextMode',
        }
        if prop_name in default_true_bools and prop_value.strip().lower() == 'true':
            return True
        if prop_name in default_false_bools and prop_value.strip().lower() == 'false':
            return True

        return False

    def _parse_named_slots(self, content: str):
        """Parse named slots section"""
        for line in content.split('\n'):
            line = line.strip()
            if line and not line.startswith('#'):
                self.named_slots.append(line)

    def _parse_animations_full(self, content: str):
        """Parse animations section with full details"""
        current_anim = None

        for line in content.split('\n'):
            line = line.strip()
            if not line:
                continue

            # Match animation header: [AnimName] or AnimName:
            anim_match = re.match(r'^\[([^\]]+)\]|^(\w+):$', line)
            if anim_match:
                anim_name = anim_match.group(1) or anim_match.group(2)
                current_anim = AnimationInfo(name=anim_name)
                self.animations_full.append(current_anim)
                self.animations.append(anim_name)
                continue

            if current_anim:
                # Parse duration
                dur_match = re.match(r'Duration:\s*([\d.]+)', line)
                if dur_match:
                    current_anim.duration = float(dur_match.group(1))
                    continue

                # Parse bound widgets
                widget_match = re.match(r'Widgets?:\s*(.+)', line)
                if widget_match:
                    widgets = [w.strip() for w in widget_match.group(1).split(',')]
                    current_anim.bound_widgets.extend(widgets)
                    continue

                # Parse tracks
                track_match = re.match(r'-\s*(\w+):\s*(.+)', line)
                if track_match:
                    current_anim.tracks.append({
                        'type': track_match.group(1),
                        'target': track_match.group(2)
                    })

    def _parse_bindings_section(self, content: str):
        """Parse bindings section"""
        for line in content.split('\n'):
            line = line.strip()
            if not line:
                continue

            # Match: Widget.Property <- Function or Widget.Property -> Function
            binding_match = re.match(r'(\w+)\.(\w+)\s*(?:<-|->|:)\s*(\w+)', line)
            if binding_match:
                self.bindings.append(BindingInfo(
                    widget_name=binding_match.group(1),
                    property_name=binding_match.group(2),
                    function_name=binding_match.group(3)
                ))

    def _parse_class_defaults(self, content: str):
        """Parse class defaults section with noise filtering"""
        for line in content.split('\n'):
            line = line.strip()
            if not line:
                continue

            # Match: PropertyName=Value
            match = re.match(r'(\w+)=(.+)', line)
            if match:
                prop_name = match.group(1)
                prop_value = match.group(2)

                # Filter noise properties
                if prop_name in self.NOISE_CLASS_DEFAULTS:
                    continue

                # Filter null delegates
                if prop_value == '(null).None':
                    continue

                # Filter empty values
                if prop_value == '()':
                    continue

                # Filter default transform
                if prop_name == 'RenderTransform' and 'X=0.000000' in prop_value:
                    continue

                self.class_defaults[prop_name] = prop_value

    def _parse_variables_section(self, content: str):
        """Parse variables section"""
        for line in content.split('\n'):
            line = line.strip()
            if not line:
                continue

            # Match: VarName: Type [Flags]
            match = re.match(r'(\w+):\s*(\w+)(?:\s*\[([^\]]+)\])?', line)
            if match:
                var_name = match.group(1)
                var_type = match.group(2)
                flags_str = match.group(3) or ""
                flags = [f.strip() for f in flags_str.split(',')] if flags_str else []

                var_info = VariableInfo(
                    name=var_name,
                    var_type=var_type,
                    flags=flags
                )
                self.blueprint_variables.append(var_info)
                self.variables[var_name] = var_type

    def _parse_interfaces(self, content: str):
        """Parse interfaces section"""
        current_interface = None

        for line in content.split('\n'):
            line = line.strip()
            if not line:
                continue

            # Match interface name
            if not line.startswith('-') and not line.startswith('*'):
                current_interface = InterfaceInfo(name=line)
                self.interfaces.append(current_interface)
            elif current_interface and (line.startswith('-') or line.startswith('*')):
                # Function of current interface
                func_name = line.lstrip('-* ').strip()
                current_interface.functions.append(func_name)

    def _parse_graph(self, graph_name: str, content: str):
        """Parse a graph section using BlueprintSimplifier"""
        # Check if content contains Begin Object patterns (raw node data)
        has_node_objects = 'Begin Object Class=' in content

        if has_node_objects and BlueprintSimplifier:
            # Use BlueprintSimplifier to parse and simplify graph content
            bp_simplifier = BlueprintSimplifier()
            simplified_content = bp_simplifier.parse_blueprint_text(content)

            graph_info = GraphInfo(
                name=graph_name,
                graph_type=self._determine_graph_type(graph_name),
                simplified_content=simplified_content
            )
        else:
            # Content is already simplified or BlueprintSimplifier not available
            graph_info = GraphInfo(
                name=graph_name,
                graph_type=self._determine_graph_type(graph_name),
                simplified_content=content.strip()
            )

        self.graphs.append(graph_info)

        # Extract function names from graph content
        self._extract_functions_from_graph(graph_info.simplified_content)

    def _determine_graph_type(self, graph_name: str) -> str:
        """Determine the type of graph from its name"""
        name_lower = graph_name.lower()
        if 'event' in name_lower:
            return 'EventGraph'
        elif 'construct' in name_lower:
            return 'ConstructionScript'
        elif 'function' in name_lower:
            return 'FunctionGraph'
        elif 'macro' in name_lower:
            return 'MacroGraph'
        return 'EventGraph'

    def _extract_functions_from_graph(self, content: str):
        """Extract function/event names from simplified graph content"""
        # Match Event: X or CustomEvent: X
        for match in re.finditer(r'(?:Event|CustomEvent):\s*(\w+)', content):
            event_name = match.group(1)
            if event_name not in self.functions:
                self.functions.append(f"Event: {event_name}")

        # Match FunctionEntry: X
        for match in re.finditer(r'FunctionEntry:\s*(\w+)', content):
            func_name = match.group(1)
            if func_name not in self.functions:
                self.functions.append(func_name)

    # Original methods for UE copy/paste format
    def _parse_widget_tree(self, content: str):
        """Parse the widget hierarchy from UE format"""
        widget_pattern = r'Begin Object Class=([^\s]+)\s+Name="([^"]+)"(.*?)End Object'

        for match in re.finditer(widget_pattern, content, re.DOTALL):
            class_path = match.group(1)
            widget_name = match.group(2)
            widget_content = match.group(3)

            widget_class = class_path.split('.')[-1].replace("'", "")

            if not self._is_widget_class(widget_class):
                continue

            widget = WidgetInfo(
                name=widget_name,
                widget_class=widget_class
            )

            self._parse_widget_properties(widget_content, widget)

            if 'bIsVariable=True' in widget_content:
                widget.is_variable = True

            self.widgets[widget_name] = widget

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
        for name, widget in self.widgets.items():
            if 'Canvas' in widget.widget_class and widget.parent is None:
                self.root_widget = name
                break

        if not self.root_widget and self.widgets:
            for name, widget in self.widgets.items():
                if 'Panel' in widget.widget_class:
                    self.root_widget = name
                    break

    def _parse_bindings(self, content: str):
        """Parse property bindings"""
        binding_pattern = r'Bindings\(\d+\)=\(([^)]+)\)'

        for match in re.finditer(binding_pattern, content):
            binding_str = match.group(1)

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
            name_match = re.search(r'["\']([^"\']+)["\']', anim_ref)
            if name_match:
                self.animations.append(name_match.group(1))

    def _parse_variables(self, content: str):
        """Parse widget variables"""
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
        func_pattern = r'K2Node_FunctionEntry.*?FunctionReference=.*?MemberName="([^"]+)"'

        for match in re.finditer(func_pattern, content, re.DOTALL):
            func_name = match.group(1)
            if func_name not in self.functions:
                self.functions.append(func_name)

        event_pattern = r'K2Node_CustomEvent.*?CustomFunctionName="([^"]+)"'
        for match in re.finditer(event_pattern, content, re.DOTALL):
            event_name = match.group(1)
            if event_name not in self.functions:
                self.functions.append(f"Event: {event_name}")

    def _generate_cpp_simplified(self) -> str:
        """Generate simplified output from C++ format data"""
        output = []
        output.append("=" * 50)
        output.append(f"WIDGET: {self.widget_name}")
        output.append("=" * 50)
        output.append("")

        # Parent class
        if self.parent_class:
            output.append(f"Parent: {self.parent_class}")
            output.append("")

        # Widget hierarchy
        output.append("[Widget Tree]")
        if self.root_widget:
            self._output_widget_tree_detailed(self.root_widget, output, indent=2)
        elif self.widgets:
            for name, widget in sorted(self.widgets.items()):
                var_marker = " [VAR]" if widget.is_variable else ""
                output.append(f"  - {name} ({widget.widget_class}){var_marker}")
        else:
            output.append("  (No widgets parsed)")
        output.append("")

        # Named slots
        if self.named_slots:
            output.append("[Named Slots]")
            for slot in self.named_slots:
                output.append(f"  - {slot}")
            output.append("")

        # Animations
        if self.animations_full:
            output.append("[Animations]")
            for anim in self.animations_full:
                dur_str = f" {anim.duration}s" if anim.duration > 0 else ""
                output.append(f"  [{anim.name}]{dur_str}")
                if anim.bound_widgets:
                    output.append(f"    Widgets: {', '.join(anim.bound_widgets)}")
                for track in anim.tracks:
                    output.append(f"    - {track['type']}: {track['target']}")
            output.append("")
        elif self.animations:
            output.append("[Animations]")
            for anim in self.animations:
                output.append(f"  - {anim}")
            output.append("")

        # Class defaults (filtered)
        if self.class_defaults:
            output.append("[Class Defaults]")
            for prop, value in sorted(self.class_defaults.items()):
                # Truncate very long values
                if len(value) > 100:
                    value = value[:100] + "..."
                output.append(f"  {prop}: {value}")
            output.append("")

        # Variables
        if self.blueprint_variables:
            output.append("[Variables]")
            for var in self.blueprint_variables:
                flags_str = f" [{', '.join(var.flags)}]" if var.flags else ""
                default_str = f" = {var.default_value}" if var.default_value else ""
                output.append(f"  {var.name}: {var.var_type}{flags_str}{default_str}")
            output.append("")
        elif self.variables:
            output.append("[Variables]")
            for var_name, var_type in sorted(self.variables.items()):
                if not var_name.startswith('_'):
                    output.append(f"  {var_name}: {var_type}")
            output.append("")

        # Interfaces
        if self.interfaces:
            output.append("[Interfaces]")
            for iface in self.interfaces:
                output.append(f"  {iface.name}")
                for func in iface.functions:
                    output.append(f"    - {func}")
            output.append("")

        # Bindings
        if self.bindings:
            output.append("[Bindings]")
            for binding in self.bindings:
                output.append(f"  {binding.widget_name}.{binding.property_name} <- {binding.function_name}()")
            output.append("")

        # Graphs
        if self.graphs:
            for graph in self.graphs:
                output.append(f"[Graph: {graph.name}]")
                if graph.simplified_content:
                    # Indent graph content
                    for line in graph.simplified_content.split('\n'):
                        if line.strip():
                            output.append(f"  {line}")
                output.append("")

        return '\n'.join(output)

    def _deduplicate_properties(self, properties: Dict[str, str]) -> Dict[str, str]:
        """Remove redundant struct properties and apply SizeBox override logic.

        - Struct dedup: if both 'Font' (full struct) and 'Font.Typeface' (decomposed) exist, drop 'Font'
        - SizeBox override: if bOverride_HeightOverride=False, drop both flag and HeightOverride;
          if =True, keep HeightOverride but drop the flag (implied)
        """
        result = dict(properties)

        # --- Struct dedup: drop full struct if decomposed sub-properties exist ---
        struct_prefixes = set()
        for key in result:
            if '.' in key:
                struct_prefixes.add(key.split('.')[0])
        for prefix in struct_prefixes:
            if prefix in result:
                del result[prefix]

        # --- SizeBox override pattern ---
        override_keys = [k for k in result if k.startswith('bOverride_')]
        for flag_key in override_keys:
            # e.g. bOverride_HeightOverride -> HeightOverride
            value_key = flag_key[len('bOverride_'):]
            flag_val = result[flag_key].strip().lower()
            if flag_val == 'false':
                # Override disabled: drop both flag and value
                result.pop(flag_key, None)
                result.pop(value_key, None)
            elif flag_val == 'true':
                # Override enabled: keep value, drop flag (implied)
                result.pop(flag_key, None)

        return result

    @staticmethod
    def _format_value(value: str, max_len: int = 120) -> str:
        """Truncate value at max_len chars."""
        if len(value) > max_len:
            return value[:max_len] + "..."
        return value

    def _output_widget_tree_detailed(self, widget_name: str, output: List[str], indent: int = 0):
        """Recursively output widget tree with ALL meaningful properties"""
        widget = self.widgets.get(widget_name)
        if not widget:
            return

        prefix = " " * indent
        var_marker = " [VAR]" if widget.is_variable else ""
        output.append(f"{prefix}- {widget.name} ({widget.widget_class}){var_marker}")

        # Deduplicate and clean properties
        props = self._deduplicate_properties(widget.properties)

        # Priority ordering: high-priority props first, then rest alphabetically
        priority_prefixes = ['Visibility', 'Text', 'Font.', 'ColorAndOpacity']

        def prop_sort_key(key: str):
            for i, prefix in enumerate(priority_prefixes):
                if key == prefix or key.startswith(prefix):
                    return (0, i, key)
            return (1, 0, key)

        sorted_props = sorted(props.keys(), key=prop_sort_key)

        for prop in sorted_props:
            value = self._format_value(props[prop])
            output.append(f"{prefix}    {prop}: {value}")

        # Output slot properties
        for prop, value in widget.slot_properties.items():
            if value and value != 'HAlign_Fill' and value != 'VAlign_Fill':
                output.append(f"{prefix}    Slot.{prop}: {value}")

        # Output children
        for child_name in widget.children:
            self._output_widget_tree_detailed(child_name, output, indent + 2)

    def _output_widget_tree(self, widget_name: str, output: List[str], indent: int = 0):
        """Recursively output widget tree"""
        widget = self.widgets.get(widget_name)
        if not widget:
            return

        prefix = " " * indent
        var_marker = " [VAR]" if widget.is_variable else ""
        output.append(f"{prefix}- {widget.name} ({widget.widget_class}){var_marker}")

        for child_name in widget.children:
            self._output_widget_tree(child_name, output, indent + 2)

    def generate_simplified(self) -> str:
        """Generate simplified output format"""
        if self._is_cpp_format:
            return self._generate_cpp_simplified()

        output = []
        output.append("=" * 60)
        output.append(f"WIDGET BLUEPRINT: {self.widget_name}")
        output.append("=" * 60)
        output.append("")

        output.append("[Widget Hierarchy]")
        if self.root_widget:
            self._output_widget_tree(self.root_widget, output, indent=2)
        else:
            for name, widget in sorted(self.widgets.items()):
                var_marker = " [VAR]" if widget.is_variable else ""
                output.append(f"  - {name} ({widget.widget_class}){var_marker}")
        output.append("")

        var_widgets = [w for w in self.widgets.values() if w.is_variable]
        if var_widgets:
            output.append("[Exposed Variables (Widgets)]")
            for widget in sorted(var_widgets, key=lambda w: w.name):
                output.append(f"  - {widget.name}: {widget.widget_class}")
            output.append("")

        if self.variables:
            output.append("[Custom Variables]")
            for var_name, var_type in sorted(self.variables.items()):
                output.append(f"  - {var_name}: {var_type}")
            output.append("")

        if self.bindings:
            output.append("[Property Bindings]")
            for binding in self.bindings:
                output.append(f"  - {binding.widget_name}.{binding.property_name} <- {binding.function_name}()")
            output.append("")

        if self.animations:
            output.append("[Animations]")
            for anim in self.animations:
                output.append(f"  - {anim}")
            output.append("")

        if self.functions:
            output.append("[Functions/Events]")
            for func in self.functions:
                output.append(f"  - {func}")
            output.append("")

        output.append("[Widget Details]")
        for name, widget in sorted(self.widgets.items()):
            if widget.properties or widget.slot_properties:
                output.append(f"\n  {name} ({widget.widget_class}):")
                for prop, val in widget.properties.items():
                    output.append(f"    {prop}: {val}")
                for prop, val in widget.slot_properties.items():
                    output.append(f"    Slot.{prop}: {val}")

        return '\n'.join(output)


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

    if not os.path.exists(input_file):
        print(f"Error: File not found: {input_file}")
        sys.exit(1)

    # Pass 1: Universal safe strip -> _stripped.txt
    if strip_file is not None and '_temp_raw' not in input_file:
        strip_file(input_file)

    try:
        with open(input_file, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
        print(f"File read: {input_file}")
    except Exception as e:
        print(f"Error reading file: {e}")
        sys.exit(1)

    simplified = simplify_widget(content)

    base_name = os.path.splitext(input_file)[0]
    if base_name.endswith('_raw'):
        base_name = base_name[:-4]
    output_file = f"{base_name}_simplified.txt"

    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(simplified)
        print(f"Simplified output saved: {output_file}")

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
