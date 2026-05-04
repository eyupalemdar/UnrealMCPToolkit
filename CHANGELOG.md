# Changelog

## 2026-05-05 - CommonAIExport Unreal Automation Layering Pass

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
`UAIDataTableExporter`, with docs synced to the new export surface.

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

## Validation

- `python Resources/Scripts/validate_mcp_contract.py`
- `python Resources/Scripts/test_mcp_contract.py`
- UE 5.7 `BuildPlugin -TargetPlatforms=Win64`
