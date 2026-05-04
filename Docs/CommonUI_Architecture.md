# CommonUI Architecture Notes

CommonAIExport does not impose a game-specific UI framework, but UI transfer
work should preserve the host project's CommonUI architecture.

## Rules

- Use the host project's intended base class for activatable screens, modal
  layers, and reusable CommonButton widgets.
- Keep gameplay drag/drop or pointer-heavy widgets on plain `UUserWidget` when
  CommonUI activation/focus would interfere with pointer lifecycle.
- Record CommonUI expectations in every TSpec under `commonUi`:
  `baseClass`, `layer`, `inputConfig`, `backAction`, and `focusRoot`.
- Do not mutate a production Widget Blueprint until the TSpec has passed
  validation.
- Build reusable components as real child Widget Blueprint instances; do not
  fake reuse by writing class or asset paths into slots.
- After mutation, run `compile_and_save`, `reload_asset`, `get_widget_tree`, and
  capture checks from the TSpec verification matrix.

## Host Project Overrides

An installed project may keep a richer architecture document, for example:

```text
Docs/Mimari/CommonUI_Architecture.md
```

If a host project provides one, it overrides this plugin-level generic note.

