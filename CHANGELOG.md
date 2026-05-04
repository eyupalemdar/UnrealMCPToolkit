# Changelog

## 2026-05-05 - Unreal MCP Toolkit Rename

Renamed the plugin identity from CommonAIExport to Unreal MCP Toolkit with
`MCPToolkit` as the module name, `MCT` as the C++ type prefix, and
`MCPTOOLKIT_API` as the module API macro. Added CoreRedirects for reflected
types so existing projects can migrate old `/Script/CommonAIExport` references.
New HTTP/MCP env variables use `MCPTOOLKIT_*`, with old `COMMONAI_*` and
`COMMONAIEXPORT_*` names kept as compatibility fallbacks.

## 2026-05-05 - MCPToolkit Unreal Automation Layering Pass

### Changelist 1 - Agent And UI Transfer Guardrails

Codified the agent workflow rules, removed vendor-specific assistant references,
and documented the TSpec-first UI transfer workflow. This makes future WBP/UI
automation safer by forcing recipe and schema validation before production asset
mutation.

Commits:
- `8961ac5` docs: add UI transfer TSpec workflow
- `85eff2a` docs: add agent workflow guardrails
- `597fbcf` docs: remove vendor-specific assistant references

### Changelist 2 - Native Automation Surface Expansion

Expanded the Unreal TCP/MCP command surface with project config automation,
reflection/type discovery, runtime component hierarchy details, and a capability
matrix validator. This established the first mechanical coverage check for new
native commands and Python MCP tools.

Commits:
- `565d34b` feat: add Unreal automation command coverage
- `6be4801` feat: add project config automation commands
- `36390c2` feat: add reflected type discovery commands
- `33ea875` chore: add capability matrix validation
- `3f29e62` feat: add runtime component hierarchy details

### Changelist 3 - World, Asset, And Domain Diagnostics

Added targeted read-only diagnostics for Sequencer, spline actors, landscape,
foliage, PCG, level structure, StaticMesh, SkeletalMesh, animation assets, and
Niagara. These stay command-owned because they provide scoped inspection
responses rather than canonical asset export or reusable asset mutation.

Commits:
- `30190a2` feat: add sequencer asset inspection
- `824fd6a` feat: add spline actor authoring commands
- `93b1f21` feat: add landscape diagnostics commands
- `f82713e` feat: add foliage diagnostics commands
- `aead3c8` feat: add pcg diagnostics commands
- `68b90ca` feat: add level structure diagnostics
- `6dba4b2` feat: add static mesh diagnostics
- `80e4a5b` feat: add skeletal mesh diagnostics
- `2019bee` feat: add animation asset diagnostics
- `e7fb2cd` feat: add niagara asset diagnostics

### Changelist 4 - Builder Layer Refactors

Moved reusable asset mutation logic out of transport handlers into dedicated
Builder classes for Blueprint SCS components, DataTables, and asset imports.
CommandHandlers now parse JSON and format responses while Builders own Unreal
asset construction/mutation APIs.

Commits:
- `073822b` feat: add blueprint component authoring commands
- `eafd64a` refactor: add blueprint component builder
- `45373c4` refactor: add data table builder
- `0e38e23` refactor: add asset import builder

### Changelist 5 - DataTable Export Coverage

Added canonical DataTable export support and made supported export types
registry-driven. DataTable rows and row struct metadata are now covered by
`UMCTDataTableExporter`, with docs synced to the new export surface.

Commits:
- `516ca82` docs: record command layering audit
- `a9e0822` feat: add data table exporter
- `1b21820` docs: sync data table export support
- `6f38804` refactor: trim export command includes

### Changelist 6 - Capability Layer Enforcement

Added a second matrix, `Resources/CapabilityLayerMatrix.json`, that records the
Builder/Exporter decision for every capability. Contract validation now rejects
missing layer decisions, stale handler references, and invalid Builder/Exporter
class references.

Commits:
- `3707b04` test: enforce capability layer matrix

### Changelist 7 - Commandlet Export And Widget Reordering

Completed the Python export wrapper path so automation clients can run the
existing `MCTExport` commandlet directly instead of falling back to manual copy
instructions. The wrapper now resolves the host project, UnrealEditor-Cmd,
output directory, commandlet mode, and simplified output path while preserving
manual `_raw.txt` simplification.

Implemented index-aware widget moves using UE 5.7 `UPanelWidget` APIs. Widget
reordering now uses `ShiftChild` for same-parent moves and `InsertChildAt` for
cross-parent moves, keeps compatible slot data through slot templates, rejects
self/descendant moves, and returns the applied index in the handler response.

## Validation

- `python Resources/Scripts/validate_mcp_contract.py`
- `python Resources/Scripts/test_mcp_contract.py`
- UE 5.7 `BuildPlugin -TargetPlatforms=Win64`
