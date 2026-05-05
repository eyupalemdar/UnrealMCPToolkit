# ScrollBox Recipe

## Purpose

Use `ScrollBox` when a tab or panel can overflow vertically and should expose a
scrollbar only when the content is larger than the available viewport.

## Source Evidence

Targeted UE source files:

```text
<UE_ROOT>\Engine\Source\Runtime\UMG\Public\Components\ScrollBox.h
<UE_ROOT>\Engine\Source\Runtime\UMG\Private\Components\ScrollBox.cpp
<UE_ROOT>\Engine\Source\Runtime\UMG\Public\Components\ScrollBoxSlot.h
<UE_ROOT>\Engine\Source\Runtime\UMG\Private\Components\ScrollBoxSlot.cpp
<UE_ROOT>\Engine\Source\Runtime\Slate\Private\Widgets\Layout\SScrollBox.cpp
<UE_ROOT>\Engine\Source\Runtime\Slate\Private\Widgets\Layout\SScrollBar.cpp
```

Key facts:

- `UScrollBox` is a `UPanelWidget`.
- Its child slot type is `UScrollBoxSlot`.
- `AlwaysShowScrollbar=false` makes Slate bind scrollbar visibility to
  `SScrollBar::ShouldBeVisible`, which only returns visible when the scroll
  track is needed.
- Use an inner content panel with `ScrollBoxSlot.Padding` when the design needs
  space between the scrollbar viewport and its content.

## Correct MCP Shape

For a tab inside a `WidgetSwitcher`, make the switcher child the scrollbox and
put the original content panel inside it:

```text
add_widget(parent_wbp, "/Game/UI/Menu/MenuScrollBox", "AudioContentScroll", "ContentSwitcher")
move_widget(parent_wbp, "AudioContent", "AudioContentScroll")
set_slot_property(parent_wbp, "AudioContent", "Padding", "(Left=40,Top=48,Right=40,Bottom=0)")
set_widget_property(parent_wbp, "AudioContentScroll", "AlwaysShowScrollbar", "False")
set_widget_property(parent_wbp, "AudioContentScroll", "AlwaysShowScrollbarTrack", "False")
```

## Do Not

- Do not use `AlwaysShowScrollbar=true` for acceptance UI unless the design
  explicitly requires a permanently visible scrollbar.
- Do not put row widgets directly in the scrollbox when the design needs shared
  top/side padding; use an inner `VerticalBox`.
- Do not change the child order of a `WidgetSwitcher` that is driven by enum
  indices.

## Verification

Required checks:

1. `get_widget_tree` shows the `WidgetSwitcher` children in enum order.
2. Each overflow-capable tab root is a `MenuScrollBox_C`.
3. The content panel inside each scrollbox has a `ScrollBoxSlot`.
4. Runtime captures show the content inset from the scroll viewport and no
   visible scrollbar on tabs that do not overflow.
