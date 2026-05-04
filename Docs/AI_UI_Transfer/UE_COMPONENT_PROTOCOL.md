# UE Component Protocol

This protocol prevents agents from learning Unreal/UMG/CommonUI components by
trial and error inside production Widget Blueprints.

## Rule

Do not experiment on the target screen. For an unfamiliar or uncertain
component, use this order:

1. Read the component recipe.
2. Read the CommonAIExport tool reference and capability matrix.
3. Read the targeted Unreal Engine source files.
4. Build a tiny probe Widget Blueprint.
5. Update the recipe/matrix, then apply the TSpec to the production WBP.

## Source Roots

The host project root is the directory that owns the `.uproject` file.

Unreal Engine source root is project/environment specific. If an environment
does not document it, locate it before reading engine component source.

For UMG components, read only the targeted files first:

```text
Engine/Source/Runtime/UMG/Public/Components/<Component>.h
Engine/Source/Runtime/UMG/Private/Components/<Component>.cpp
```

If the component wraps Slate, also read the matching Slate files:

```text
Engine/Source/Runtime/Slate/Public/Widgets/...
Engine/Source/Runtime/Slate/Private/Widgets/...
```

For CommonUI components, read targeted files under:

```text
Engine/Plugins/Runtime/CommonUI/Source/CommonUI/
```

Do not run broad recursive engine searches unless the targeted files do not
answer the question.

## Probe Rule

A probe is required when the agent is not certain which MCP call/property shape
works for a component.

The probe must be minimal:

1. Create a scratch WBP under `/Game/UI/_AIProbe/<ComponentName>/`.
2. Add only the component and the smallest possible children.
3. Set the disputed property or slot value.
4. Run `compile_and_save`.
5. Run `reload_asset`.
6. Run `get_widget_tree`.
7. Run `capture_widget_preview` at one normal landscape resolution.
8. Record the result in the component recipe or tool matrix.

After two failed MCP mutation attempts, stop experimenting and report a minimal
repro:

- exact asset path
- exact MCP call
- expected result
- actual result
- classification: TSpec issue, missing asset, CommonAIExport defect, editor
  cache issue, or unsupported UE behavior

## Production Rule

Production WBP mutation is allowed only after:

- the TSpec is validated
- every uncertain component has a recipe or successful probe
- the build path is deterministic

Fallbacks that reduce structure require an explicit TSpec deviation.

