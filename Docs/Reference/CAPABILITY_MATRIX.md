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
