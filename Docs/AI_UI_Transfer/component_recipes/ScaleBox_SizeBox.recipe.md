# ScaleBox + SizeBox Recipe

Use this pattern when a group of absolute-positioned CanvasPanel children must
scale as one coherent reference playfield.

## Verified Shape

```text
Overlay
  ScaleBox
    SizeBox
      CanvasPanel
```

Set:

- `ScaleBox.Stretch = ScaleToFit`
- `ScaleBox.StretchDirection = Both`
- `ScaleBox` parent slot alignment: `HAlign_Fill` / `VAlign_Fill`
- `SizeBox.bOverride_WidthOverride = true`
- `SizeBox.WidthOverride = <reference width>`
- `SizeBox.bOverride_HeightOverride = true`
- `SizeBox.HeightOverride = <reference height>`
- `SizeBox` ScaleBox slot alignment: `HAlign_Center` / `VAlign_Center`

Then move the existing reference CanvasPanel under the SizeBox. The CanvasPanel
keeps its reference-coordinate anchors and offsets; the ScaleBox applies one
uniform render/layout scale to all descendants.

## Verified Probe

Probe asset:

```text
/Game/UI/_AIProbe/ScaleBox/WBP_ScaleBoxCanvasProbe_0430
```

Verification:

- `compile_and_save` succeeded.
- `reload_asset` succeeded.
- `get_widget_tree` showed
  `RootOverlay -> PlayfieldScaleBox -> ReferenceSizeBox -> ReferenceCanvas`.
- Runtime captures at `1920x1080`, `1539x1063`, and `696x462` showed top,
  center, and bottom markers scaling uniformly without changing relative
  position.

## Notes

- This is preferable to scaling only one child widget when sibling components
  are authored in the same absolute reference space.
- It will preserve aspect ratio and letterbox whichever axis has excess space.
- For USizeBox overrides, set the boolean override flag and the numeric value;
  setting only `WidthOverride` or `HeightOverride` is not enough.
