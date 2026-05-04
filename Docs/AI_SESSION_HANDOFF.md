# AI Session Handoff

Last updated: 2026-05-04

This plugin copy starts from CommonAIExport Git commit:

```text
7065f53a1c290a6123f671aed79bcd9e81149b53
feat: modularize CommonAIExport TCP server
```

## Current State

- `AIExportTCPServer.cpp` has been reduced to core server ownership: port
  discovery, command descriptors, descriptor-to-dispatch conversion, dispatch,
  HTTP/TCP lifecycle/callback wiring, registry file handling, and JSON response
  helpers.
- Command implementation is split across matched modules:
  `CommandDispatch`, `CommandHandlers`, `HttpMcp`, `Transport`, and
  `RuntimeDiagnostics`.
- Generated MCP artifacts describe `129` TCP commands across `24` categories
  and `146` MCP tools including client-only helpers.
- The ProjectOkey source session validated this state with contract/static
  tests, preflight, guarded C++ build, and a final live-editor mutating smoke:
  `OkeyGame-7424-55560`, TCP `55560`, HTTP `55610`.
- ProjectOkey root submissions for that work were:
  `dv.commit.553` (`CommonAIExport: modularize TCP server dispatch`) and
  `dv.commit.554` (`docs: update AI session handoff`).

## Plugin-Local UI Transfer Package

This copy now includes a portable TSpec/UI transfer workflow:

- `Docs/AI_UI_Transfer/README.md`
- `Docs/AI_UI_Transfer/`
- `Docs/UI_TSpec/`
- `Resources/Scripts/ValidateUITSpecs.ps1`
- `AGENTS.md`

The goal is that a fresh agent started in this standalone plugin repository can
answer "where did we stop?" and can apply the same TSpec discipline when the
plugin is installed into a host Unreal project.

## Next Useful Work

- Keep `AIExportTCPServer.cpp` mostly core unless a small cohesive ownership
  group emerges.
- If extending the plugin for UI workflows, add TSpec-aware helper commands only
  after preserving the existing contract/static/preflight/smoke test chain.
- For production WBP changes in any host project, create or update the TSpec,
  run the validator, then mutate through CommonAIExport tools.

