# Changelog

## Unreleased

## 1.1.2 - Multi-Editor Routing Latency

### Added

- Added `editor_call_many` for bounded, ordered calls that resolve and verify a
  selected editor once while preserving per-command scope and dry-run checks.
- Added behavioral routing tests to the standard MCP preflight.

### Fixed

- Exact `editor_id` routing now probes only the matching registry record instead
  of serially probing every historical editor and commandlet entry.
- Full editor listing and project-directory fallback now probe distinct ports
  concurrently while preserving deterministic result order.
- Explicit-port routing now verifies any supplied editor ID and project path,
  preventing a port-only shortcut from silently reaching the wrong project.
- Dynamic routing tools no longer advertise themselves as read-only or
  idempotent in generated MCP metadata.

### Safety

- Multi-call execution is explicitly non-transactional, stops on the first
  error by default, remains bounded to 100 commands, and does not batch
  long-running build/cook jobs automatically.

## 1.1.1 - UE 5.8 API Compatibility

### Fixed

- Replaced deprecated or encapsulated UE 5.8 DataLayer, PCG, UImage, Material
  usage and TickFunction access with their supported APIs.
- Exported HLOD rebuild-policy data and recovered the optional hash from policy
  reflection without requiring the optional HLOD utilities plugin.
- Kept `last_tick_game_time` for response compatibility while adding the
  accurately named `last_interval_tick_game_time` field.

### Changed

- Added Mac to the Editor-only supported platform allowlist.
- Generalized host policy and source-checkout export examples so the shared
  plugin no longer embeds a product-specific repository path.
- Clarified that consuming-project TSpec, recipe and validation policy takes
  precedence over bundled compatibility documentation.
- Made repeated artifact generation byte-stable by preserving generated
  timestamps when JSON payloads are otherwise unchanged.

## 1.1.0 - Extension Command Registry

### Added

- Added the public extension command registry used by external editor plugins
  such as AIAssetPipeline.
- Included registered extension commands in command listing, manifest export,
  dry-run handling, scope validation, and mutating command serialization.

### Changed

- Stabilized the asset import builder surface used by plugin-owned batch import
  commands.
- Kept AI asset packaging logic outside MCPToolkit; MCPToolkit remains the
  editor automation bridge.

### Documentation

- Reworked the root `README.md` into a concise GitHub entry point that links to
  detailed docs instead of duplicating long reference material.
- Added `Docs/README.md` as the primary English documentation index.
- Added `Docs/README.tr.md` as a Turkish secondary-language overview.
- Added `Docs/Usage/EXPORT_SYSTEM.md` for exporter architecture, output
  formats, simplifier scripts, commandlet usage, and troubleshooting.

### Changed

- Generalized host-project-specific prompts, sample paths, and recipe source
  references so plugin docs and MCP prompt surfaces stay project-independent.

### Removed

- Removed repo-local packaging documentation and wrapper scripts.
- Removed the legacy reflected-type redirect config from
  `Config/DefaultEngine.ini`.

## 2026-05-05 - Unreal MCP Toolkit Rename

### Changed

- Renamed the plugin identity from `CommonAIExport` to `Unreal MCP Toolkit`.
- Renamed the module to `MCPToolkit`, the C++ type prefix to `MCT`, and the
  module API macro to `MCPTOOLKIT_API`.
- Switched generated artifacts and plugin metadata to the `MCPToolkit` naming
  surface.
- Added `MCPTOOLKIT_*` HTTP/MCP environment variable names while keeping
  existing `COMMONAI_*` and `COMMONAIEXPORT_*` names as compatibility fallbacks
  where those external clients still depend on them.

## 2026-05-05 - MCPToolkit Unreal Automation Layering Pass

### Agent And UI Transfer Guardrails

- Documented the TSpec-first UI transfer workflow.
- Added agent workflow guardrails for Widget Blueprint mutation.
- Removed vendor-specific assistant references from the UI transfer docs.

Related commits:

- `8961ac5` docs: add UI transfer TSpec workflow
- `85eff2a` docs: add agent workflow guardrails
- `597fbcf` docs: remove vendor-specific assistant references

### Native Automation Surface Expansion

- Added project config automation commands.
- Added reflection/type discovery commands.
- Added runtime component hierarchy details.
- Added the first capability matrix validation layer for native commands and
  Python MCP tools.

Related commits:

- `565d34b` feat: add Unreal automation command coverage
- `6be4801` feat: add project config automation commands
- `36390c2` feat: add reflected type discovery commands
- `33ea875` chore: add capability matrix validation
- `3f29e62` feat: add runtime component hierarchy details

### World, Asset, And Domain Diagnostics

- Added read-only diagnostics for Sequencer, landscape, foliage, PCG, level
  structure, StaticMesh, SkeletalMesh, animation assets, and Niagara.
- Added spline actor authoring commands for editor spline workflows.

Related commits:

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

### Builder Layer Refactors

- Moved reusable Unreal asset mutation logic out of transport handlers and into
  dedicated Builder classes.
- Added Builders for Blueprint SCS components, DataTables, and asset imports.
- Kept command handlers focused on JSON parsing, scheduling, and responses.

Related commits:

- `073822b` feat: add blueprint component authoring commands
- `eafd64a` refactor: add blueprint component builder
- `45373c4` refactor: add data table builder
- `0e38e23` refactor: add asset import builder

### DataTable Export Coverage

- Added canonical DataTable export support through `UMCTDataTableExporter`.
- Made supported export type documentation registry-driven.
- Documented DataTable rows and row struct metadata as supported export content.

Related commits:

- `516ca82` docs: record command layering audit
- `a9e0822` feat: add data table exporter
- `1b21820` docs: sync data table export support
- `6f38804` refactor: trim export command includes

### Capability Layer Enforcement

- Added `Resources/CapabilityLayerMatrix.json`.
- Extended contract validation to reject missing layer decisions, stale handler
  references, and invalid Builder/Exporter class references.

Related commit:

- `3707b04` test: enforce capability layer matrix

### Commandlet Export And Widget Reordering

- Completed the Python export wrapper path for running the `MCTExport`
  commandlet directly from automation clients.
- Added host project, UnrealEditor-Cmd, output directory, commandlet mode, and
  simplified output path resolution to `Resources/Scripts/export_asset.py`.
- Implemented index-aware Widget Blueprint moves using UE 5.7 panel APIs.
- Preserved compatible slot data during widget moves, rejected self/descendant
  moves, and returned the applied index in handler responses.

Related commit:

- `4eb7754` fix: complete commandlet export and widget reordering

## Validation

- `python Resources/Scripts/validate_mcp_contract.py`
- `python Resources/Scripts/test_mcp_contract.py`
