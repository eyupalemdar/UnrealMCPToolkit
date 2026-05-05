# Changelog

## Unreleased

### Documentation

- Reworked the root `README.md` into a concise GitHub entry point that links to
  detailed docs instead of duplicating long reference material.
- Added `Docs/README.md` as the primary English documentation index.
- Added `Docs/README.tr.md` as a Turkish secondary-language overview.
- Added `Docs/Usage/BUILD_PLUGIN.md` for UE 5.7 Win64/Linux packaging.
- Added `Docs/Usage/EXPORT_SYSTEM.md` for exporter architecture, output
  formats, simplifier scripts, commandlet usage, and troubleshooting.

### Build

- Added `Resources/Scripts/BuildPlugin.ps1` as a repo-local BuildPlugin wrapper.
- Defaulted plugin packaging to UE 5.7 for both `Win64` and `Linux`.
- Declared `SupportedTargetPlatforms` as `Win64` and `Linux` in
  `MCPToolkit.uplugin`.
- Added UE 5.7 Linux toolchain discovery for
  `C:\UnrealToolchains\v26_clang-20.1.8-rockylinux8\`.
- Stamped packaged `.uplugin` descriptors with `EngineVersion: "5.7.0"` without
  modifying the source `MCPToolkit.uplugin`.

### Removed

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
- UE 5.7 `BuildPlugin -TargetPlatforms=Win64`
