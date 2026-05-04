# WidgetSwitcher Recipe

## Purpose

Use `WidgetSwitcher` when multiple child widgets exist in the tree and exactly
one child should be visible/active at a time.

## Source Evidence

Targeted UE source files:

```text
D:\Programlama\Projeler\UnrealEngine\Engine\Source\Runtime\UMG\Public\Components\WidgetSwitcher.h
D:\Programlama\Projeler\UnrealEngine\Engine\Source\Runtime\UMG\Private\Components\WidgetSwitcher.cpp
D:\Programlama\Projeler\UnrealEngine\Engine\Source\Runtime\UMG\Public\Components\WidgetSwitcherSlot.h
D:\Programlama\Projeler\UnrealEngine\Engine\Source\Runtime\UMG\Private\Components\WidgetSwitcherSlot.cpp
```

Key facts:

- `UWidgetSwitcher` is a `UPanelWidget`.
- Its children are real widget instances, not class references.
- The active child is selected with `ActiveWidgetIndex` / `SetActiveWidgetIndex`.
- Child slot type is `UWidgetSwitcherSlot`.

## Correct MCP Shape

Create the switcher, add actual child widgets to it, then set the active index:

```text
add_widget(parent_wbp, "WidgetSwitcher", "ContentSwitcher", "ParentPanel")
add_widget(parent_wbp, "/Game/UI/Path/W_TabGeneral", "GeneralTab", "ContentSwitcher")
add_widget(parent_wbp, "/Game/UI/Path/W_TabAudio", "AudioTab", "ContentSwitcher")
set_widget_property(parent_wbp, "ContentSwitcher", "ActiveWidgetIndex", "0")
```

For custom Widget Blueprint children, use explicit asset/generated-class paths:

```text
/Game/UI/Path/W_TabGeneral
/Game/UI/Path/W_TabGeneral_C
/Game/UI/Path/W_TabGeneral.W_TabGeneral_C
WidgetBlueprintGeneratedClass'/Game/UI/Path/W_TabGeneral.W_TabGeneral_C'
```

## Do Not

- Do not assign `/Game/...` class paths through slot `Content`.
- Do not use `set_slot_property(..., "Content", "...")` to populate a
  WidgetSwitcher.
- Do not use runtime `CreateWidget` graph generation unless the TSpec explicitly
  requires runtime population.
- Do not test uncertain settings on the production WBP.

## Verification

Required checks:

1. `get_widget_tree` shows `ContentSwitcher` with child widget instances.
2. `ActiveWidgetIndex` is set to the expected index.
3. `capture_widget_preview` shows only the active child.

If this fails twice, stop and follow `../UE_COMPONENT_PROTOCOL.md` blocker
handling.

