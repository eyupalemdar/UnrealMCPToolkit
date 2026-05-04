# AI UI Transfer Entry Point

This is the tool-agnostic entry point for Reference Image -> Pencil -> Unreal
Engine 5.7 UI transfer using CommonAIExport.

Any AI agent working on this flow must start here:

1. Read `Docs/AI_SESSION_HANDOFF.md`.
2. Read `Docs/AI_UI_Transfer/START_HERE.md`.
3. Read `Docs/CommonUI_Architecture.md` for CommonUI/UMG rules.
4. Read `Docs/Reference/AI_REFERENCE.md` for CommonAIExport MCP and TCP tool syntax.
5. Use `Docs/UI_TSpec/tspec.schema.json` and
   `Resources/Scripts/ValidateUITSpecs.ps1` before mutating any Widget
   Blueprint.
6. Use `Docs/AI_UI_Transfer/UE_COMPONENT_PROTOCOL.md` and
   `Docs/AI_UI_Transfer/component_recipes/` before experimenting with UE
   components.

For a copy/paste session starter, use
`Docs/AI_UI_Transfer/AGENT_BOOTSTRAP_PROMPT.md`.

Tool-specific files such as `CLAUDE.md`, `AGENTS.md`, `GEMINI.md`, `.claude/*`,
and `.codex/*` should point back here. The canonical workflow is under
`Docs/AI_UI_Transfer/` and `Docs/UI_TSpec/`.

