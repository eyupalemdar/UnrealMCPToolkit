#!/usr/bin/env python3
"""
Unreal Engine AnimBlueprint Text Format Simplifier

Supports two input formats:
  1. UE copy/paste format (BPGraph(GraphName="..." NodesString="..."))
  2. C++ commandlet export format (=== SECTION === headers)

For the C++ format, performs deep simplification:
  - Filters CLASS DEFAULTS to meaningful properties only
  - Compacts AnimGraph nodes to show type + key props + connections
  - Collapses trivial transitions
  - Delegates K2 logic graphs to BlueprintSimplifier
  - Compacts linked-layer stubs to single lines
"""

import re
import json
from typing import Dict, List, Any, Tuple, Optional
from dataclasses import dataclass, field
from collections import defaultdict, OrderedDict
import sys
import os


# ---------------------------------------------------------------------------
# Import BlueprintSimplifier for K2 node delegation
# ---------------------------------------------------------------------------
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

try:
    from bp_simplify import BlueprintSimplifier
except ImportError:
    BlueprintSimplifier = None


# ===========================================================================
#  Dataclasses (kept for UE copy/paste format backward compat)
# ===========================================================================

@dataclass
class Pin:
    id: str
    name: str
    type: str
    direction: str = "input"
    linked_to: List[Tuple[str, str]] = field(default_factory=list)
    default_value: Optional[str] = None


@dataclass
class Node:
    id: str
    type: str
    name: str
    position: Tuple[float, float] = (0, 0)
    pins: Dict[str, Pin] = field(default_factory=dict)
    properties: Dict[str, Any] = field(default_factory=dict)


@dataclass
class Graph:
    name: str
    type: str = "Function"
    nodes: Dict[str, Node] = field(default_factory=dict)
    local_variables: List[Dict[str, str]] = field(default_factory=list)


# ===========================================================================
#  Main Simplifier
# ===========================================================================

class UnrealBPSimplifier:
    """Simplifies AnimBlueprint exports in both UE-paste and C++ formats."""

    # Path aliases (UE paste format)
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
        self.node_id_map = {}
        self.path_cache = {}
        self.content_lines = []
        self._is_cpp_format = False
        self._cpp_output_lines = []

    # -----------------------------------------------------------------------
    #  C++ Format: Top-level detection and orchestration
    # -----------------------------------------------------------------------

    def _split_into_sections(self, content: str) -> List[Tuple[str, str]]:
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

    def _parse_cpp_format(self, content: str):
        """Main orchestrator for C++ export format."""
        out = self._cpp_output_lines
        sections = self._split_into_sections(content)

        # Separate sections by type
        header_section = None
        class_defaults_body = None
        metadata_sections = []  # pass-through
        graph_sections = []     # need simplification

        METADATA_KEYS = {
            'VARIABLES', 'INTERFACES', 'ANIMGRAPH SUMMARY',
            'ANIM NOTIFIES', 'LINKED ANIM LAYERS', 'ANIM SLOT NAMES',
        }

        for header, body in sections:
            name_match = re.match(r'===\s+(.+?)\s+===', header)
            if not name_match:
                continue
            name = name_match.group(1)

            if name.startswith('ANIM BLUEPRINT:'):
                header_section = (header, body)
            elif name == 'CLASS DEFAULTS':
                class_defaults_body = body
            elif name in METADATA_KEYS:
                metadata_sections.append((header, body))
            elif name.startswith('GRAPH:'):
                graph_name = name[6:].strip()
                graph_sections.append((graph_name, body))
            else:
                metadata_sections.append((header, body))

        # 1. Header
        if header_section:
            out.append(header_section[0])
            for line in header_section[1].strip().splitlines():
                line = line.strip()
                if line:
                    out.append(line)
            out.append('')

        # 2. CLASS DEFAULTS (filtered)
        if class_defaults_body is not None:
            self._emit_filtered_class_defaults(class_defaults_body, out)

        # 3. Metadata sections (pass-through, already concise)
        for header, body in metadata_sections:
            out.append(header)
            body_stripped = body.strip()
            if body_stripped:
                out.append(body_stripped)
            out.append('')

        # 4. Graph sections — collect linked layer stubs together
        linked_layer_stubs = []
        other_graphs = []
        for graph_name, body in graph_sections:
            if graph_name in self._LINKED_LAYER_NAMES:
                linked_layer_stubs.append((graph_name, body))
            else:
                other_graphs.append((graph_name, body))

        # Emit regular graphs
        for graph_name, body in other_graphs:
            self._emit_graph_section(graph_name, body, out)

        # Emit linked layer stubs grouped together
        if linked_layer_stubs:
            out.append('=== LINKED LAYER GRAPHS (stubs) ===')
            for graph_name, body in linked_layer_stubs:
                self._emit_linked_layer_stub(graph_name, body.strip(), out)
            out.append('')

    # -----------------------------------------------------------------------
    #  CLASS DEFAULTS filter
    # -----------------------------------------------------------------------

    _CDO_SKIP_PREFIXES = (
        '__AnimBlueprintMutables',
        'AnimBlueprintExtension_',
    )

    _CDO_EMPTY_VALUES = {
        '()', 'False', '0', '0.000000', 'None', '""', '-1',
        '(X=0.000000,Y=0.000000,Z=0.000000)',
        '(Pitch=0.000000,Yaw=0.000000,Roll=0.000000)',
        'NewEnumerator0',
    }

    # CDO keys whose default values are uninteresting
    _CDO_SKIP_KEYS = {
        'RootMotionMode', 'bUseMultiThreadedAnimationUpdate',
        'bUsingCopyPoseFromMesh', 'bReceiveNotifiesFromLinkedInstances',
        'bPropagateNotifiesToLinkedInstances', 'bUseMainInstanceMontageEvaluationData',
        'OnMontageBlendingOut', 'OnMontageBlendedIn', 'OnMontageStarted',
        'OnMontageEnded', 'OnAllMontageInstancesEnded', 'OnMontageSectionChanged',
        'GroundDistance', 'WorldLocation', 'WorldRotation', 'WorldVelocity',
        'LocalVelocity2D', 'LocalAcceleration2D', 'PivotDirection2D',
    }

    _CDO_ANIM_KEYS = {
        'Name', 'SlotName', 'CachePoseName', 'Layer', 'LayerGroup',
        'Interface', 'ControlRigClass', 'BlendSpace', 'StateMachine',
        'bCanEnterTransition', 'StateIndex', 'FunctionName',
        'BecomeRelevantFunction', 'UpdateFunction', 'StateEntryFunction',
    }

    def _emit_filtered_class_defaults(self, body: str, out: List[str]):
        out.append('=== CLASS DEFAULTS (filtered) ===')
        for line in body.strip().splitlines():
            line = line.strip()
            if not line:
                continue

            skip = False
            for prefix in self._CDO_SKIP_PREFIXES:
                if line.startswith(prefix):
                    skip = True
                    break
            if skip:
                continue

            eq_idx = line.find('=')
            if eq_idx < 0:
                out.append(line)
                continue
            key = line[:eq_idx]
            value = line[eq_idx + 1:]

            if value in self._CDO_EMPTY_VALUES:
                continue

            # AnimGraphNode_TransitionResult: keep only bCanEnterTransition=True
            if key.startswith('AnimGraphNode_TransitionResult'):
                if 'bCanEnterTransition=True' not in value:
                    continue
                out.append(f'  {key}: bCanEnterTransition=True')
                continue

            # AnimGraphNode CDO lines: extract key properties
            if key.startswith('AnimGraphNode_'):
                compact = self._compact_cdo_anim_node(key, value)
                if compact:
                    out.append(f'  {compact}')
                continue

            # K2Node_PropertyAccess: skip default 0 values
            if key.startswith('K2Node_PropertyAccess'):
                if value in ('0.000000', 'False', 'MOVE_None',
                             '(X=0.000000,Y=0.000000,Z=0.000000)',
                             '(Pitch=0.000000,Yaw=0.000000,Roll=0.000000)'):
                    continue

            # __CustomProperty: skip defaults
            if key.startswith('__CustomProperty_'):
                if value in ('0.000000', 'False'):
                    continue

            # Skip known uninteresting keys
            if key in self._CDO_SKIP_KEYS:
                continue

            # Skip lines whose value is only empty function refs
            if self._is_all_empty_parens(value):
                continue

            # GameplayTagPropertyMap: too noisy
            if key == 'GameplayTagPropertyMap':
                out.append(f'  {key}: (gameplay tag bindings, omitted for brevity)')
                continue

            out.append(f'  {key}={value}')

        out.append('')

    def _compact_cdo_anim_node(self, key: str, value: str) -> Optional[str]:
        """Extract key properties from an AnimGraphNode CDO line."""
        props = {}
        for prop_key in self._CDO_ANIM_KEYS:
            pattern = rf'{prop_key}="([^"]*)"'
            m = re.search(pattern, value)
            if m:
                v = m.group(1)
                if v and v not in ('', 'None'):
                    props[prop_key] = v
                continue
            pattern = rf'{prop_key}=([^,()\s]+)'
            m = re.search(pattern, value)
            if m:
                v = m.group(1)
                if v and v not in ('', 'None', '()', 'False', '0', '-1'):
                    props[prop_key] = v
                continue
            if prop_key in ('BecomeRelevantFunction', 'UpdateFunction', 'StateEntryFunction'):
                pattern = rf'{prop_key}=\(FunctionName="([^"]+)"'
                m = re.search(pattern, value)
                if m:
                    props[prop_key] = m.group(1)

        # Shorten Interface path
        if 'Interface' in props:
            short = re.search(r"([^/.]+)\.([^']+)'?$", props['Interface'])
            if short:
                props['Interface'] = short.group(1)

        # Shorten BlendSpace path
        if 'BlendSpace' in props:
            short = re.search(r"([^/.]+)\.([^']+)'?$", props['BlendSpace'])
            if short:
                props['BlendSpace'] = short.group(2)

        # Shorten ControlRigClass
        if 'ControlRigClass' in props:
            short = re.search(r"([^/.]+)_C'?$", props['ControlRigClass'])
            if short:
                props['ControlRigClass'] = short.group(1)

        if not props:
            return None

        prop_str = ', '.join(f'{k}={v}' for k, v in props.items())
        return f'{key}: ({prop_str})'

    def _is_all_empty_parens(self, value: str) -> bool:
        """Check if value consists only of empty function refs."""
        cleaned = value
        empty_patterns = [
            r'InitialUpdateFunction=\(\)',
            r'BecomeRelevantFunction=\(\)',
            r'UpdateFunction=\(\)',
            r'StateEntryFunction=\(\)',
            r'StateFullyBlendedInFunction=\(\)',
            r'StateExitFunction=\(\)',
            r'StateFullyBlendedOutFunction=\(\)',
        ]
        for pat in empty_patterns:
            cleaned = re.sub(pat, '', cleaned)
        cleaned = cleaned.replace(',', '').strip()
        return len(cleaned) == 0 or cleaned == '()'

    # -----------------------------------------------------------------------
    #  Graph section classification and emission
    # -----------------------------------------------------------------------

    _LINKED_LAYER_NAMES = {
        'FullBodyAdditives', 'FullBody_IdleState', 'FullBody_StartState',
        'FullBody_CycleState', 'FullBody_StopState', 'FullBody_PivotState',
        'FullBody_Aiming', 'FullBody_JumpStartState', 'FullBody_JumpApexState',
        'FullBody_FallLandState', 'FullBody_FallLoopState',
        'FullBody_JumpStartLoopState', 'FullBody_SkeletalControls',
        'LeftHandPose_OverrideState',
    }

    def _emit_graph_section(self, graph_name: str, body: str, out: List[str]):
        """Classify and emit a single GRAPH section."""
        body_stripped = body.strip()
        if not body_stripped:
            return

        k2_node_count = len(re.findall(r'K2Node_', body_stripped))

        # Use Begin Object Class= to detect actual node types (not ExportPath refs)
        has_state_entry = bool(re.search(r'Begin Object Class=\S*AnimStateEntryNode', body_stripped))
        has_state_node = bool(re.search(r'Begin Object Class=\S*AnimStateNode\s', body_stripped))

        # 1. Transition graphs
        if graph_name == 'Transition':
            self._emit_transition_graph(body_stripped, out)
            return

        # 2. Main AnimGraph (check BEFORE state machine — AnimGraph body contains SM nodes)
        if graph_name == 'AnimGraph':
            self._emit_anim_graph(graph_name, body_stripped, out)
            return

        # 3. State machine (has actual AnimStateEntryNode or AnimStateNode as top-level classes)
        if has_state_entry or has_state_node:
            self._emit_state_machine_graph(graph_name, body_stripped, out)
            return

        # 4. State inner graphs with only AnimGraphNodes (Idle, Start, etc.)
        has_anim_nodes = bool(re.search(r'Begin Object Class=\S*AnimGraphNode_', body_stripped))
        if has_anim_nodes and k2_node_count == 0:
            self._emit_anim_state_graph(graph_name, body_stripped, out)
            return

        # 5. K2 logic graphs
        if k2_node_count > 0:
            self._emit_k2_graph(graph_name, body_stripped, out)
            return

        # 6. Comment-only graphs (EventGraph)
        if 'EdGraphNode_Comment' in body_stripped:
            self._emit_comment_graph(graph_name, body_stripped, out)
            return

        # 7. Fallback
        line_count = body_stripped.count('\n') + 1
        if line_count <= 20:
            out.append(f'=== GRAPH: {graph_name} ===')
            out.append(body_stripped)
            out.append('')
        else:
            out.append(f'=== GRAPH: {graph_name} === ({line_count} lines, summarized)')
            out.append(f'  (contains {line_count} lines of node data)')
            out.append('')

    # -----------------------------------------------------------------------
    #  Linked Layer Stubs
    # -----------------------------------------------------------------------

    def _emit_linked_layer_stub(self, graph_name: str, body: str, out: List[str]):
        """Compact a linked-layer stub to 1 line."""
        layer_group = ''
        lg_match = re.search(r'LayerGroup="([^"]*)"', body)
        if lg_match and lg_match.group(1):
            layer_group = f' [LayerGroup={lg_match.group(1)}]'

        input_poses = []
        for m in re.finditer(r'AnimGraphNode_LinkedInputPose.*?Name="([^"]+)"', body):
            input_poses.append(m.group(1))

        if input_poses:
            poses_str = ', '.join(input_poses)
            out.append(f'  {graph_name}{layer_group} (InputPoses: {poses_str})')
        else:
            out.append(f'  {graph_name}{layer_group}')

    # -----------------------------------------------------------------------
    #  Transition Graphs
    # -----------------------------------------------------------------------

    _transition_counter = 0

    def _emit_transition_graph(self, body: str, out: List[str]):
        """Emit a transition graph. Skip trivial, compact logic ones."""
        self._transition_counter += 1

        has_k2_nodes = bool(re.search(r'K2Node_', body))

        if not has_k2_nodes:
            # Trivial if no logic connected to bCanEnterTransition pin
            can_enter_pin = re.search(
                r'PinName="bCanEnterTransition".*?LinkedTo=\(([^)]+)\)', body
            )
            if not can_enter_pin:
                return

        # Compact: extract variable reads and operators for the condition
        var_reads = []
        for m in re.finditer(r'K2Node_VariableGet.*?MemberName="([^"]+)"', body, re.DOTALL):
            var_reads.append(m.group(1))
        ops = []
        for m in re.finditer(r'MemberName="(Boolean\w+|Not_PreBool|[A-Z]\w*_\w+)"', body):
            ops.append(m.group(1))
        func_calls = []
        for m in re.finditer(r'K2Node_CallFunction.*?MemberName="([^"]+)"', body, re.DOTALL):
            func_calls.append(m.group(1))

        parts = []
        if var_reads:
            parts.append(' && '.join(dict.fromkeys(var_reads)))
        if func_calls:
            parts.append(', '.join(dict.fromkeys(func_calls)) + '()')
        if ops:
            unique_ops = list(dict.fromkeys(ops))
            parts.append(f'[{", ".join(unique_ops)}]')

        condition = ' | '.join(parts) if parts else '(logic)'
        out.append(f'  Transition #{self._transition_counter}: {condition}')

    # -----------------------------------------------------------------------
    #  State Machine Graph (LocomotionSM)
    # -----------------------------------------------------------------------

    def _emit_state_machine_graph(self, graph_name: str, body: str, out: List[str]):
        """Extract state names and transitions from a state machine graph."""
        out.append(f'=== STATE MACHINE: {graph_name} ===')

        states = {}  # node_name -> state_display_name

        # Extract states from AnimStateNode + AnimationStateGraph Name="..."
        for m in re.finditer(
            r'Begin Object Class=\S*AnimStateNode\s+Name="([^"]+)".*?'
            r'AnimationStateGraph Name="([^"]+)"',
            body, re.DOTALL
        ):
            node_name = m.group(1)
            state_name = m.group(2)
            states[node_name] = state_name

        # Fallback: BoundGraph="...StateName"
        for m in re.finditer(
            r'Begin Object Class=\S*AnimStateNode\s+Name="([^"]+)".*?'
            r'BoundGraph="[^"]*?\.(\w+)"',
            body, re.DOTALL
        ):
            node_name = m.group(1)
            state_name = m.group(2)
            if node_name not in states:
                states[node_name] = state_name

        # Conduit nodes
        for m in re.finditer(
            r'Begin Object Class=\S*AnimStateConduitNode\s+Name="([^"]+)".*?'
            r'(?:BoundGraph="[^"]*?\.(\w+)"|AnimationStateGraph Name="([^"]+)")',
            body, re.DOTALL
        ):
            node_name = m.group(1)
            state_name = m.group(2) or m.group(3)
            if state_name:
                states[node_name] = f'{state_name} (conduit)'

        # Entry node
        entry_match = re.search(
            r'AnimStateEntryNode.*?LinkedTo=\((\w+)\s+', body, re.DOTALL
        )
        entry_target = None
        if entry_match:
            entry_target = entry_match.group(1)

        out.append(f'  States: {", ".join(states.values()) if states else "(none found)"}')
        if entry_target and entry_target in states:
            out.append(f'  Entry → {states[entry_target]}')

        # Extract transitions
        transitions = []
        for m in re.finditer(
            r'Begin Object Class=\S*AnimStateTransitionNode\s+Name="([^"]+)"(.*?)End Object\n(?=Begin Object Class=|$)',
            body, re.DOTALL
        ):
            trans_body = m.group(2)

            in_links = re.findall(r'PinName="In".*?LinkedTo=\(([^)]+)\)', trans_body)
            out_links = re.findall(r'PinName="Out".*?LinkedTo=\(([^)]+)\)', trans_body)

            sources = set()
            targets = set()
            for link_str in in_links:
                for node_ref in re.findall(r'(\w+)\s+[A-F0-9]+', link_str):
                    if node_ref in states:
                        sources.add(states[node_ref])
            for link_str in out_links:
                for node_ref in re.findall(r'(\w+)\s+[A-F0-9]+', link_str):
                    if node_ref in states:
                        targets.add(states[node_ref])

            if sources and targets:
                for s in sources:
                    for t in targets:
                        transitions.append(f'{s} → {t}')

        out.append('  Transitions:')
        if transitions:
            for t in transitions:
                out.append(f'    {t}')
        else:
            out.append('    (transition details in sub-graphs)')
        out.append('')

    # -----------------------------------------------------------------------
    #  AnimGraph (main anim graph)
    # -----------------------------------------------------------------------

    def _emit_anim_graph(self, graph_name: str, body: str, out: List[str]):
        """Simplify the main AnimGraph with node chain tracing."""
        out.append(f'=== GRAPH: {graph_name} (simplified) ===')
        nodes = self._parse_anim_nodes(body)

        for node_name, info in nodes.items():
            node_type = info['type_short']
            props = info.get('props', {})
            connections = info.get('connections', [])

            prop_parts = []
            for pk, pv in props.items():
                prop_parts.append(f'{pk}={pv}')
            prop_str = f' ({", ".join(prop_parts)})' if prop_parts else ''

            conn_str = ''
            if connections:
                conn_str = ' → ' + ', '.join(connections)

            out.append(f'  [{node_type}] {node_name}{prop_str}{conn_str}')

        out.append('')

    def _parse_anim_nodes(self, body: str) -> Dict[str, dict]:
        """Parse Begin Object blocks from a graph body, extracting key info."""
        nodes = OrderedDict()
        blocks = self._extract_top_level_objects(body)

        for block in blocks:
            class_match = re.search(
                r'Begin Object Class=\S+\.(\w+)\s+Name="([^"]+)"', block
            )
            if not class_match:
                continue

            class_name = class_match.group(1)
            node_name = class_match.group(2)

            type_short = class_name
            for prefix in ('AnimGraphNode_', 'AnimState', 'EdGraphNode_'):
                if type_short.startswith(prefix):
                    type_short = type_short[len(prefix):]
                    break

            info = {
                'class': class_name,
                'type_short': type_short,
                'props': {},
                'connections': [],
            }

            # Node=(...) properties
            node_prop = re.search(r'^\s+Node=\((.+?)\)\s*$', block, re.MULTILINE)
            if node_prop:
                self._extract_anim_props(node_prop.group(1), info['props'])

            # CacheName / NameOfCache
            for prop_key in ('CacheName', 'NameOfCache'):
                m = re.search(rf'{prop_key}="([^"]+)"', block)
                if m:
                    info['props']['CachePose'] = m.group(1)

            # Output pin connections (LinkedTo)
            for pin_match in re.finditer(r'CustomProperties Pin \(([^)]+(?:\([^)]*\))*[^)]*)\)', block):
                pin_str = pin_match.group(1)
                is_output = 'Direction="EGPD_Output"' in pin_str

                linked = re.search(r'LinkedTo=\(([^)]+)\)', pin_str)
                if linked and is_output:
                    targets = re.findall(r'(\w+)\s+[A-F0-9]+', linked.group(1))
                    for target in targets:
                        info['connections'].append(target)

            # Comment nodes
            if class_name == 'EdGraphNode_Comment':
                comment_m = re.search(r'NodeComment="([^"]+)"', block)
                if comment_m:
                    text = comment_m.group(1).replace('\\r\\n', ' ').replace('\\n', ' ')
                    if len(text) > 120:
                        text = text[:117] + '...'
                    info['props']['Comment'] = text

            # UpdateFunction / BecomeRelevantFunction
            for func_key in ('UpdateFunction', 'BecomeRelevantFunction'):
                m = re.search(rf'{func_key}=\(MemberName="([^"]+)"', block)
                if m:
                    info['props'][func_key] = m.group(1)

            # FunctionReference
            func_ref = re.search(r'FunctionReference=\(.*?MemberName="([^"]+)"', block)
            if func_ref:
                info['props']['Function'] = func_ref.group(1)

            nodes[node_name] = info

        return nodes

    def _extract_anim_props(self, prop_text: str, props: dict):
        """Extract key properties from a Node=(...) text."""
        for key in ('SlotName', 'Layer', 'LayerGroup', 'CachePoseName', 'Name'):
            m = re.search(rf'{key}="([^"]*)"', prop_text)
            if m and m.group(1):
                props[key] = m.group(1)

        # BlendSpace short name
        bs = re.search(r"BlendSpace=\"[^\"]*?([^/.]+)\.([^'\"]+)", prop_text)
        if bs:
            props['BlendSpace'] = bs.group(2)

        # Interface short name
        iface = re.search(r"Interface=\"[^\"]*?([^/.]+)\.", prop_text)
        if iface:
            props['Interface'] = iface.group(1)

        # ControlRigClass short name
        cr = re.search(r"ControlRigClass=\"[^\"]*?([^/.]+)_C", prop_text)
        if cr:
            props['ControlRig'] = cr.group(1)

        if 'bCanEnterTransition=True' in prop_text:
            props['bCanEnterTransition'] = 'True'

        alpha = re.search(r'Alpha=([0-9.]+)', prop_text)
        if alpha and alpha.group(1) != '1.000000':
            props['Alpha'] = alpha.group(1)

        bm = re.search(r'BlendMode=(\w+)', prop_text)
        if bm and bm.group(1) != 'EBlendMode::Default':
            props['BlendMode'] = bm.group(1)

        if 'bIgnoreForRelevancyTest=True' in prop_text:
            props['IgnoreRelevancy'] = 'True'

        rpt = re.search(r'RefPoseType=(\w+)', prop_text)
        if rpt:
            props['RefPoseType'] = rpt.group(1)

        mt = re.search(r'MaxTransitionsPerFrame=(\d+)', prop_text)
        if mt:
            props['MaxTransitions'] = mt.group(1)

    def _extract_top_level_objects(self, body: str) -> List[str]:
        """Extract top-level Begin Object...End Object blocks (handles nesting)."""
        blocks = []
        lines = body.splitlines()
        i = 0
        while i < len(lines):
            stripped = lines[i].strip()
            if stripped.startswith('Begin Object Class=') and 'Name="' in stripped:
                depth = 1
                block_lines = [lines[i]]
                i += 1
                while i < len(lines) and depth > 0:
                    l = lines[i].strip()
                    if l.startswith('Begin Object'):
                        depth += 1
                    elif l == 'End Object':
                        depth -= 1
                    block_lines.append(lines[i])
                    i += 1
                blocks.append('\n'.join(block_lines))
            else:
                i += 1
        return blocks

    # -----------------------------------------------------------------------
    #  Anim State Inner Graphs (Idle, Start, Cycle, etc.)
    # -----------------------------------------------------------------------

    def _emit_anim_state_graph(self, graph_name: str, body: str, out: List[str]):
        """Compact a state inner graph (AnimGraphNode_* only)."""
        nodes = self._parse_anim_nodes(body)
        if not nodes:
            return

        out.append(f'  --- State: {graph_name} ---')
        for node_name, info in nodes.items():
            props = info.get('props', {})
            conns = info.get('connections', [])
            prop_str = ', '.join(f'{k}={v}' for k, v in props.items()) if props else ''
            conn_str = (' → ' + ', '.join(conns)) if conns else ''
            if prop_str:
                out.append(f'    [{info["type_short"]}] {node_name} ({prop_str}){conn_str}')
            else:
                out.append(f'    [{info["type_short"]}] {node_name}{conn_str}')

    # -----------------------------------------------------------------------
    #  K2 Logic Graphs
    # -----------------------------------------------------------------------

    def _emit_k2_graph(self, graph_name: str, body: str, out: List[str]):
        """Compact K2 logic graph: extract entry, calls, variables, flow."""
        out.append(f'=== FUNCTION: {graph_name} ===')

        blocks = self._extract_top_level_objects(body)
        if not blocks:
            out.append('  (empty)')
            out.append('')
            return

        # Collect nodes for compact output
        local_vars = []
        func_calls = []     # (name, node_name)
        var_gets = []
        var_sets = []
        branches = []
        comments = []
        exec_flow = []      # ordered by exec pin chain
        other_nodes = []

        # Build a map: PinId -> (node_name, pin_name) for exec flow tracing
        pin_owner = {}       # pinId -> node_class_info
        node_exec_out = {}   # node_name -> [target_node_names]

        for block in blocks:
            cm = re.search(r'Begin Object Class=\S+\.(\w+)\s+Name="([^"]+)"', block)
            if not cm:
                continue
            class_name = cm.group(1)
            node_name = cm.group(2)

            if class_name == 'EdGraphNode_Comment':
                cmt = re.search(r'NodeComment="([^"]+)"', block)
                if cmt:
                    text = cmt.group(1).replace('\\r\\n', ' ').replace('\\n', ' ').replace("\\'", "'")
                    if len(text) > 150:
                        text = text[:147] + '...'
                    comments.append(text)
                continue

            if class_name == 'K2Node_FunctionEntry':
                # Extract local variables
                for lv in re.finditer(r'VarName="([^"]+)"', block):
                    vtype = ''
                    tm = re.search(rf'{re.escape(lv.group(1))}.*?PinCategory="([^"]+)"', block)
                    if tm:
                        vtype = f': {tm.group(1)}'
                    local_vars.append(f'{lv.group(1)}{vtype}')
                # Extract function name
                fn = re.search(r'FunctionReference=.*?MemberName="([^"]+)"', block)
                if fn:
                    out.append(f'  Entry: {fn.group(1)}')

            elif class_name == 'K2Node_FunctionResult':
                pass  # Just marks the end

            elif class_name == 'K2Node_CallFunction':
                fn = re.search(r'MemberName="([^"]+)"', block)
                if fn:
                    func_calls.append(fn.group(1))

            elif class_name == 'K2Node_VariableGet':
                vn = re.search(r'MemberName="([^"]+)"', block)
                if vn:
                    var_gets.append(vn.group(1))

            elif class_name == 'K2Node_VariableSet':
                vn = re.search(r'MemberName="([^"]+)"', block)
                if vn:
                    var_sets.append(vn.group(1))

            elif class_name == 'K2Node_IfThenElse':
                branches.append('Branch')

            elif 'K2Node_CommutativeAssociativeBinaryOperator' in class_name:
                fn = re.search(r'MemberName="([^"]+)"', block)
                if fn:
                    other_nodes.append(fn.group(1))

            elif class_name == 'K2Node_PromotableOperator':
                fn = re.search(r'MemberName="([^"]+)"', block)
                if fn:
                    other_nodes.append(fn.group(1))

            elif class_name == 'K2Node_PropertyAccess':
                # Thread-safe property access
                pa = re.search(r'PathAsText="([^"]+)"', block)
                if pa:
                    var_gets.append(f'@{pa.group(1)}')

        if local_vars:
            out.append(f'  Locals: {", ".join(dict.fromkeys(local_vars))}')
        if comments:
            for c in comments:
                out.append(f'  // {c}')
        if func_calls:
            out.append(f'  Calls: {", ".join(dict.fromkeys(func_calls))}')
        if var_gets:
            out.append(f'  Reads: {", ".join(dict.fromkeys(var_gets))}')
        if var_sets:
            out.append(f'  Writes: {", ".join(dict.fromkeys(var_sets))}')
        if branches:
            out.append(f'  Branches: {len(branches)}')
        if other_nodes:
            out.append(f'  Ops: {", ".join(dict.fromkeys(other_nodes))}')

        out.append('')

    # -----------------------------------------------------------------------
    #  Comment-only Graphs (e.g., EventGraph)
    # -----------------------------------------------------------------------

    def _emit_comment_graph(self, graph_name: str, body: str, out: List[str]):
        """Extract comments from comment-only graphs."""
        out.append(f'=== GRAPH: {graph_name} ===')
        for m in re.finditer(r'NodeComment="([^"]+)"', body):
            text = m.group(1).replace('\\r\\n', ' ').replace('\\n', ' ').replace("\\'", "'")
            if len(text) > 200:
                text = text[:197] + '...'
            out.append(f'  // {text}')
        out.append('')

    # -----------------------------------------------------------------------
    #  UE Copy/Paste Format (backward compatibility — unchanged logic)
    # -----------------------------------------------------------------------

    def parse_unreal_path(self, path: str) -> str:
        if path in self.path_cache:
            return self.path_cache[path]
        simplified = path
        for full_path, alias in self.PATH_ALIASES.items():
            if full_path in simplified:
                simplified = simplified.replace(full_path, alias)
        if len(simplified) > 100:
            match = re.search(r"['/]([^'/]+)'?$", simplified)
            if match:
                simplified = f".../{match.group(1)}"
        self.path_cache[path] = simplified
        return simplified

    def simplify_node_type(self, class_path: str) -> str:
        class_match = re.search(r"/Script/[\w\.]+\.(\w+)", class_path)
        if class_match:
            class_name = class_match.group(1)
        else:
            class_match = re.search(r"Class=/Script/[\w\.]+\.(\w+)", class_path)
            if class_match:
                class_name = class_match.group(1)
            else:
                parts = class_path.replace('/', '.').split('.')
                class_name = parts[-1] if parts else class_path
        for pattern, replacement in self.NODE_TYPES.items():
            if class_name.startswith(pattern):
                remainder = class_name[len(pattern):]
                return f"{replacement}{remainder}"
        return class_name

    def parse_pin_type(self, pin_str: str) -> Dict[str, Any]:
        pin_info = {'category': 'unknown', 'subcategory': None, 'object': None}
        category_match = re.search(r'PinType\.PinCategory="([^"]+)"', pin_str)
        if category_match:
            pin_info['category'] = self.PIN_TYPE_MAP.get(category_match.group(1), category_match.group(1))
        subcat_match = re.search(r'PinType\.PinSubCategory="([^"]+)"', pin_str)
        if subcat_match and subcat_match.group(1):
            pin_info['subcategory'] = subcat_match.group(1)
        obj_match = re.search(r'PinType\.PinSubCategoryObject=([^,)]+)', pin_str)
        if obj_match and obj_match.group(1) != 'None':
            pin_info['object'] = self.parse_unreal_path(obj_match.group(1).strip('"'))
        return pin_info

    def parse_pin_connections(self, pin_str: str) -> List[Tuple[str, str]]:
        connections = []
        linked_match = re.search(r'LinkedTo=\(([^)]+)\)', pin_str)
        if linked_match:
            for conn in linked_match.group(1).split(','):
                conn = conn.strip()
                if ' ' in conn:
                    parts = conn.split(' ')
                    if len(parts) >= 2:
                        connections.append((parts[0], parts[1]))
        return connections

    def expand_bpgraph(self, content: str) -> str:
        expanded = content.replace('\\r\\n', '\n')
        expanded = expanded.replace('\\t', '\t')
        expanded = expanded.replace('\\"', '"')
        return expanded

    def parse_node(self, node_block: str) -> Optional[Node]:
        begin_match = re.search(r'Begin Object Class=([^ ]+) Name="([^"]+)"', node_block)
        if not begin_match:
            begin_match = re.search(r'Begin Object Name="([^"]+)".*?Class=([^ ]+)', node_block)
            if not begin_match:
                return None
            node_name = begin_match.group(1)
            class_path = begin_match.group(2)
        else:
            class_path = begin_match.group(1)
            node_name = begin_match.group(2)

        node = Node(id=node_name, type=self.simplify_node_type(class_path), name=node_name)

        pos_x_match = re.search(r'NodePosX=([-\d]+)', node_block)
        pos_y_match = re.search(r'NodePosY=([-\d]+)', node_block)
        if pos_x_match and pos_y_match:
            node.position = (int(pos_x_match.group(1)), int(pos_y_match.group(1)))

        guid_match = re.search(r'NodeGuid=([A-F0-9]+)', node_block)
        if guid_match:
            node.properties['guid'] = guid_match.group(1)

        func_ref_match = re.search(r'FunctionReference=\(([^)]+)\)', node_block)
        if func_ref_match:
            node.properties['function'] = func_ref_match.group(1)

        var_ref_match = re.search(r'VariableReference=\(([^)]+)\)', node_block)
        if var_ref_match:
            node.properties['variable'] = var_ref_match.group(1)

        pin_matches = re.finditer(r'CustomProperties Pin \(([^)]+)\)', node_block)
        for pin_match in pin_matches:
            pin_str = pin_match.group(1)
            id_match = re.search(r'PinId=([A-F0-9]+)', pin_str)
            name_match = re.search(r'PinName="([^"]+)"', pin_str)
            if id_match and name_match:
                pin = Pin(id=id_match.group(1), name=name_match.group(1), type='unknown')
                if 'Direction="EGPD_Output"' in pin_str:
                    pin.direction = 'output'
                pin_type = self.parse_pin_type(pin_str)
                if pin_type['subcategory']:
                    pin.type = f"{pin_type['category']}:{pin_type['subcategory']}"
                else:
                    pin.type = pin_type['category']
                pin.linked_to = self.parse_pin_connections(pin_str)
                default_match = re.search(r'DefaultValue="([^"]+)"', pin_str)
                if default_match:
                    pin.default_value = default_match.group(1)
                node.pins[pin.name] = pin

        return node

    def parse_graph_block(self, content: str) -> Graph:
        graph = Graph(name="Unknown")
        name_match = re.search(r'GraphName="([^"]+)"', content)
        if name_match:
            graph.name = name_match.group(1)

        for var_match in re.finditer(r'LocalVariables\(\d+\)=\(([^)]+)\)', content):
            var_str = var_match.group(1)
            var_info = {}
            nm = re.search(r'VarName="([^"]+)"', var_str)
            if nm:
                var_info['name'] = nm.group(1)
            tm = re.search(r'VarType=\(PinCategory="([^"]+)"', var_str)
            if tm:
                var_info['type'] = tm.group(1)
            if var_info:
                graph.local_variables.append(var_info)

        for node_block_match in re.finditer(r'Begin Object.*?Name="([^"]+)".*?End Object', content, re.DOTALL):
            node = self.parse_node(node_block_match.group(0))
            if node:
                graph.nodes[node.id] = node

        return graph

    def parse_content(self, content: str):
        """Parse the entire content — dispatches to C++ or UE-paste format."""
        # C++ format detection
        if '=== ANIM BLUEPRINT:' in content:
            self._is_cpp_format = True
            self._transition_counter = 0
            self._parse_cpp_format(content)
            return

        # UE copy/paste format (existing logic, unchanged)
        bpgraph_starts = []
        for match in re.finditer(r'BPGraph\(GraphName="([^"]+)"', content):
            bpgraph_starts.append((match.start(), match.group(1)))

        for start_pos, graph_name in bpgraph_starts:
            paren_count = 0
            i = content.index('BPGraph(', start_pos)
            i += 8
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
                bpgraph_content = content[start_pos:end_pos + 1]
                nodes_start = bpgraph_content.find('NodesString="')
                if nodes_start >= 0:
                    nodes_start += 13
                    nodes_end = nodes_start
                    in_escape = False
                    while nodes_end < len(bpgraph_content):
                        if bpgraph_content[nodes_end] == '\\' and not in_escape:
                            in_escape = True
                        elif bpgraph_content[nodes_end] == '"' and not in_escape:
                            if nodes_end + 1 < len(bpgraph_content):
                                next_char = bpgraph_content[nodes_end + 1]
                                if next_char in ',)':
                                    break
                        else:
                            in_escape = False
                        nodes_end += 1

                    nodes_string = bpgraph_content[nodes_start:nodes_end]
                    expanded_content = self.expand_bpgraph(nodes_string)
                    graph = Graph(name=graph_name)

                    for node_match in re.finditer(r'Begin Object.*?End Object', expanded_content, re.DOTALL):
                        node = self.parse_node(node_match.group(0))
                        if node:
                            graph.nodes[node.id] = node

                    for var_match in re.finditer(r'LocalVariables\(\d+\)=\(([^)]+)\)', expanded_content):
                        var_str = var_match.group(1)
                        var_info = {}
                        nm = re.search(r'VarName="([^"]+)"', var_str)
                        if nm:
                            var_info['name'] = nm.group(1)
                        tm = re.search(r'VarType=\(PinCategory="([^"]+)"', var_str)
                        if tm:
                            var_info['type'] = tm.group(1)
                        if var_info:
                            graph.local_variables.append(var_info)

                    self.graphs[graph_name] = graph

        self._parse_toplevel_blocks(content)

    def _parse_toplevel_blocks(self, content: str):
        for match in re.finditer(r'Begin Object.*?(?:Graph|Function|State).*?End Object', content, re.DOTALL):
            block = match.group(0)
            if 'NodeGuid' in block or 'Nodes(' in block:
                graph = self.parse_graph_block(block)
                if graph.name != "Unknown" and graph.name not in self.graphs:
                    self.graphs[graph.name] = graph

    def generate_simplified_format(self) -> str:
        """Generate output — dispatches based on format."""
        if self._is_cpp_format:
            return '\n'.join(self._cpp_output_lines)

        # UE copy/paste format
        output = []
        output.append("=" * 80)
        output.append("SIMPLIFIED UNREAL BLUEPRINT FORMAT")
        output.append("=" * 80)
        output.append("")

        for graph_name, graph in self.graphs.items():
            output.append(f"\n## GRAPH: {graph_name}")
            output.append("-" * 40)

            if graph.local_variables:
                output.append("\nLOCAL VARIABLES:")
                for var in graph.local_variables:
                    output.append(f"  - {var.get('name', 'unknown')}: {var.get('type', 'unknown')}")

            output.append("\nNODES:")
            for node_id, node in graph.nodes.items():
                output.append(f"\n  [{node.type}] {node.name}")
                if node.position != (0, 0):
                    output.append(f"    Position: {node.position}")
                if 'function' in node.properties:
                    output.append(f"    Function: {node.properties['function']}")
                if 'variable' in node.properties:
                    output.append(f"    Variable: {node.properties['variable']}")
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

            output.append("\nFLOW:")
            self._generate_flow_diagram(graph, output)

        return '\n'.join(output)

    def _generate_flow_diagram(self, graph: Graph, output: List[str]):
        entry_nodes = [n for n in graph.nodes.values()
                      if 'Entry' in n.type or 'FuncEntry' in n.type]
        if not entry_nodes:
            output.append("  No entry point found")
            return
        for entry in entry_nodes:
            visited = set()
            self._trace_flow(entry, graph, output, visited, indent=2)

    def _trace_flow(self, node: Node, graph: Graph, output: List[str],
                    visited: set, indent: int = 2):
        if node.id in visited:
            output.append(" " * indent + f"(loop to {node.name})")
            return
        visited.add(node.id)
        node_desc = node.name
        if 'function' in node.properties:
            for part in node.properties['function'].split(','):
                if 'MemberName=' in part:
                    node_desc = f"{part.split('=')[1].strip(chr(34))}()"
                    break
        output.append(" " * indent + f"-> {node_desc}")
        exec_pins = [p for p in node.pins.values()
                    if p.direction == 'output' and 'then' in p.name.lower()]
        for pin in exec_pins:
            for next_node_id, _ in pin.linked_to:
                if next_node_id in graph.nodes:
                    self._trace_flow(graph.nodes[next_node_id], graph,
                                   output, visited, indent + 2)

    def simplify_file(self, input_path: str, output_path: str):
        """Main function to simplify a Blueprint text file."""
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

        if self._is_cpp_format:
            print(f"Detected C++ export format")
        else:
            print(f"Found {len(self.graphs)} graphs")

        simplified = self.generate_simplified_format()

        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(simplified)

        print(f"Simplified output written to: {output_path}")
        print(f"Output size: {len(simplified)} bytes")
        if len(content) > 0:
            print(f"Compression ratio: {(1 - len(simplified)/len(content))*100:.1f}%")


def main():
    """Main entry point"""
    if len(sys.argv) < 2:
        print("Usage: python animbp_simplify.py <input_file>")
        print("Example: python animbp_simplify.py ABP_Mannequin_Base_raw.txt")
        sys.exit(1)

    simplifier = UnrealBPSimplifier()
    input_file = sys.argv[1]

    base_name = os.path.splitext(input_file)[0]
    if base_name.endswith('_raw'):
        base_name = base_name[:-4]
    output_file = f"{base_name}_simplified.txt"
    json_output_file = f"{base_name}_data.json"

    simplifier.simplify_file(input_file, output_file)

    # JSON output only for UE-paste format
    if not simplifier._is_cpp_format and simplifier.graphs:
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
    elif simplifier._is_cpp_format:
        print("C++ format detected — JSON output skipped (simplified text only)")


if __name__ == "__main__":
    main()
