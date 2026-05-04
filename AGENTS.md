# Agent Instructions

For any Unreal UI transfer or Widget Blueprint mutation task, do not rely on
tool-specific memory. Start with:

- `Docs/AI_SESSION_HANDOFF.md`
- `Docs/AI_UI_Transfer/README.md`
- `Docs/AI_UI_Transfer/START_HERE.md`
- `Docs/CommonUI_Architecture.md`
- `Docs/Reference/AI_REFERENCE.md`
- `Docs/UI_TSpec/README.md`
- `Docs/UI_TSpec/tspec.schema.json`
- `Docs/AI_UI_Transfer/UE_COMPONENT_PROTOCOL.md`
- `Docs/AI_UI_Transfer/component_recipes/README.md`

No Unreal Widget Blueprint mutation is allowed before a TSpec exists and passes:

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/ValidateUITSpecs.ps1
```

When validating a host project's TSpecs from an installed plugin, pass the host
root and spec directory explicitly:

```powershell
powershell -ExecutionPolicy Bypass -File Plugins/CommonAIExport/Resources/Scripts/ValidateUITSpecs.ps1 -Root . -SpecDirectory Docs/Tasarim/UI_TSpecs
```

Do not learn UE/UMG/CommonUI component behavior by trial and error in the target
WBP. For uncertain components, read the recipe first. If no recipe exists, use a
minimal probe WBP under `/Game/UI/_AIProbe/<ComponentName>/`, verify it, then
update the recipe/matrix before touching production assets.

