# Agent Bootstrap Prompt

Use this prompt when starting Codex, Gemini CLI, Kimi Code, or another agent for
Reference Image -> Pencil -> UE work.

```text
You are working in this repository on the Reference Image -> Pencil -> Unreal
Engine 5.7 CommonUI/UMG transfer pipeline.

Before changing any Pencil file, Widget Blueprint, asset, or documentation, read:

1. Docs/AI_SESSION_HANDOFF.md
2. Docs/AI_UI_Transfer/README.md
3. Docs/AI_UI_Transfer/START_HERE.md
4. Docs/CommonUI_Architecture.md
5. Docs/Reference/AI_REFERENCE.md
6. Docs/UI_TSpec/README.md
7. Docs/UI_TSpec/tspec.schema.json
8. Docs/AI_UI_Transfer/TOOL_CAPABILITY_MATRIX.md
9. Docs/AI_UI_Transfer/UE_COMPONENT_PROTOCOL.md
10. Docs/AI_UI_Transfer/component_recipes/README.md

Follow the five-layer pipeline:
reference analysis -> Pencil design system -> TSpec -> UE build -> verification.

Do not mutate any Widget Blueprint before producing and validating the TSpec.
During the UE build, you are a TSpec executor, not a visual designer.

Do not learn UE components by trial and error inside the production WBP. For
uncertain components, read the component recipe first. If no recipe exists,
locate the host Unreal Engine source root, read targeted component source files,
create a minimal probe WBP under /Game/UI/_AIProbe/<ComponentName>/, verify it,
then update the recipe/matrix before touching the production WBP. After two
failed MCP mutation attempts, stop and report a minimal repro.

For reusable Widget Blueprint children, do not depend on list_widget_classes()
as the authority. Use explicit WBP asset/generated class paths with add_widget,
for example:

- /Game/UI/Path/W_Component
- /Game/UI/Path/W_Component_C
- /Game/UI/Path/W_Component.W_Component_C
- WidgetBlueprintGeneratedClass'/Game/UI/Path/W_Component.W_Component_C'

If an MCP/tool call blocks the build, stop the current mutation path and report
a minimal repro: exact tool call, asset path, expected result, actual result,
and classification: TSpec issue, missing asset, CommonAIExport defect, editor
cache issue, or unsupported UE behavior. Do not silently inline or simplify the
design unless the TSpec records that deviation.

Before final response, complete compile_and_save, reload_asset, widget-tree
diff against TSpec, multi-ratio capture, Pencil screenshot comparison, and
fidelity log update.

Target task:
<describe the screen/component/reference image here>
```
