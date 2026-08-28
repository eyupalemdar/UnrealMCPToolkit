# Agent Instructions

For any Unreal UI transfer or Widget Blueprint mutation, the consuming host
repository's `AGENTS.md`, design contracts, schemas and validators are
authoritative. Do not rely on tool-specific memory. Read host instructions
first when they exist; otherwise start with the plugin compatibility references:

- `Docs/AI_SESSION_HANDOFF.md`
- `Docs/AI_UI_Transfer/README.md`
- `Docs/AI_UI_Transfer/START_HERE.md`
- `Docs/CommonUI_Architecture.md`
- `Docs/Reference/AI_REFERENCE.md`
- `Docs/UI_TSpec/README.md`
- `Docs/UI_TSpec/tspec.schema.json`
- `Docs/AI_UI_Transfer/UE_COMPONENT_PROTOCOL.md`
- `Docs/AI_UI_Transfer/component_recipes/README.md`

The plugin's bundled TSpec and recipe material is upstream compatibility
reference, not authority over stricter host contracts.

No Unreal Widget Blueprint mutation is allowed before a matching host TSpec
exists and the host's exact validation command passes before and after mutation.
When the host has no stricter validator, run:

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/ValidateUITSpecs.ps1
```

Do not learn UE/UMG/CommonUI component behavior by trial and error in the target
WBP. For uncertain components, read the qualified host recipe first. If none
exists, use a minimal probe WBP under the host's declared probe root, exclude it
from cooking, verify it, then update the host recipe/registry before touching
production assets. Production assets may not retain probe dependencies.

