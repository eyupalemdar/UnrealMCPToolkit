# CommonAIExport Capability Matrix

`Resources/CapabilityMatrix.json` is the control surface for feature growth.
Every native TCP command and every Python client-only MCP tool must appear in
exactly one capability entry.

## What It Catches

- A new TCP command that was added to C++ but not assigned to a capability.
- A removed or renamed command that still appears in the matrix.
- Duplicate command coverage across capability entries.
- A new client-only MCP tool that has no capability owner.
- Missing capability metadata such as owner domain, status, and extension policy.

## Update Rule

Before adding a new Unreal automation feature:

1. Search the matrix for an existing capability that already owns the workflow.
2. If the workflow fits, extend the existing command family and add the command
   name to that capability.
3. If it is genuinely new, create a new lower_snake_case capability with a clear
   `extension_policy`.
4. Keep semantic overlap notes in the capability text. The validator catches
   exact coverage mistakes; reviewers still use the policy text to catch
   design-level duplicates.

## Layering Check

For every new or changed `CommandHandlers` file, make an explicit
Builder/Exporter decision before merging:

| Command shape | Builder decision | Exporter decision |
|---|---|---|
| Creates or mutates a reusable Unreal asset, subasset, graph, SCS tree, or template object | Put the Unreal mutation API in a `Builders` class; the handler should only parse params, schedule the Game Thread work, and format the response | Add an exporter only if the resulting asset type needs canonical `export_asset` or `list_supported_types` coverage |
| Writes live editor/world/runtime state | Add a builder only when the domain logic is reusable across more than one command family; otherwise keep it in the handler | Do not add an exporter unless there is a persistent asset representation to export |
| Read-only diagnostics or targeted inspection | Usually no builder; shared parsing/summary code can live in domain utilities when it repeats | Usually no exporter; command responses are scoped diagnostics, not canonical asset exports |
| Canonical asset serialization for AI review | No builder unless creation/mutation is also supported | Add or extend an `Exporters` class and register it with `UAIExporterRegistry` |

Current audit notes:

- `blueprint_components` uses `UAIBlueprintComponentBuilder` because Actor
  Blueprint SCS mutation is reusable asset authoring. It does not need a
  dedicated exporter because whole-Blueprint export is already owned by the
  Blueprint exporter.
- `spline_authoring` remains command-handler owned for now because it edits live
  editor actor/component state. Add a spline builder only if spline authoring
  grows beyond this command family or starts producing reusable spline assets.
- `data_tables` uses `UAIDataTableBuilder` because DataTable creation, row
  mutation, and CSV import are reusable asset authoring. It also uses
  `UAIDataTableExporter` because DataTable rows are useful canonical
  `export_asset` content.
- `import_asset_files` uses `UAIAssetImportBuilder` because external file
  ingestion creates persistent Unreal assets and should not live in transport
  handlers. Exporters are not involved; imported Texture and Font assets are
  covered by their own read/export surfaces.
- `asset_authoring_registry` already delegates creation and generic property
  mutation to `UAIAssetFactory`/`UAIDataAssetBuilder`. Registry queries,
  rename/delete, redirector cleanup, and reload remain command-owned editor
  lifecycle operations unless their logic starts repeating across typed asset
  builders.
- `editor_actor_operations`, `project_introspection_and_plugins`, and
  `project_config_mutation` remain command-owned because they orchestrate live
  editor state or project files rather than building a reusable Unreal asset.
- Static Mesh, Skeletal Mesh, Animation, Niagara, Sequencer, Landscape, Foliage,
  PCG, and level-structure commands are currently read-only diagnostics, so
  adding Builders or Exporters would duplicate the existing command response
  surface.

## Required Checks

Run these after command, wrapper, or matrix changes:

```powershell
python Resources/Scripts/generate_mcp_artifacts.py
python Resources/Scripts/validate_mcp_contract.py
python Resources/Scripts/test_mcp_contract.py
```

For an installed plugin under a host project, run the same scripts through the
host-relative `Plugins/CommonAIExport/...` path.

## Fields

| Field | Purpose |
|---|---|
| `id` | Stable lower_snake_case capability identifier |
| `domain` | Broad owner area such as `ui`, `asset`, `runtime`, or `client` |
| `title` | Human-readable feature group |
| `status` | `native`, `client_only`, or `hybrid` |
| `category_tags` | Manifest categories covered by this capability |
| `commands` | Native TCP command names; each command must appear exactly once |
| `client_tools` | Python-only MCP tool names; each client-only tool must appear exactly once |
| `extension_policy` | Review guidance for whether a future feature belongs here |

The matrix is intentionally hand-maintained. Generated manifests say what exists;
this matrix says where each feature belongs and forces every addition through
that ownership check.
