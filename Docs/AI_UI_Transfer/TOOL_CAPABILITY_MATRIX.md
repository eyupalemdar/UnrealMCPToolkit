# Tool Capability Matrix

This is the canonical MCPToolkit/Pencil MCP behavior matrix for UI transfer.
Update this file when a behavior is observed twice or when a tool contract
changes.

## General Rules

| Area | Reliable usage | Risk / note |
|---|---|---|
| Widget tree creation | `create_widget_blueprint`, `add_widget`, one `compile_and_save` after the tree is done | Do not compile after each node |
| Custom WBP composition | Add child components by WBP asset path or generated class path | `list_widget_classes()` is discovery only, not the source of truth |
| Widget properties | Prefer `set_widget_property` one field at a time | `set_widget_properties` can silently skip inherited or UObject ref fields |
| Canvas slots | Prefer direct `LayoutData.Offsets` via `set_slot_property` | `set_canvas_slot_layout` can miss Left/Top offsets |
| SizeBox overrides | Set override flag and value | `WidthOverride` without `bOverride_WidthOverride=true` may not apply |
| Editor refresh | `compile_and_save` -> `reload_asset` | Open editor tabs can show cached instances |
| Capture | Use warmup plus final capture | Border tint rendering can be misleading |

## Known Behaviors

| Tool / field | Behavior | Rule |
|---|---|---|
| `set_canvas_slot_layout` | Position fields may not write to Offsets.Left/Top | Write `LayoutData.Offsets` directly |
| `SizeBox` | Override value alone is insufficient | Set `bOverride_WidthOverride` and `bOverride_HeightOverride` |
| Widget enum properties | String enum import can fail | Use numeric enum values when needed |
| New component WBP instance | `list_widget_classes()` may not list a just-created WBP class yet | Use `/Game/.../W_Name`, `/Game/.../W_Name_C`, `/Game/.../W_Name.W_Name_C`, or `WidgetBlueprintGeneratedClass'/Game/.../W_Name.W_Name_C'` with `add_widget` |
| Generated class discovery | A generated class not appearing in `list_widget_classes()` is not proof that it cannot be used | Treat `list_widget_classes()` as a convenience list; verify `add_widget` with the full path |
| WidgetSwitcher content | Assigning class/asset paths through slot `Content` is not valid composition | Add actual widget instances as children, then set active index/state |
| NamedSlot content | `set_slot_property Content=/Game/...` is not a deterministic design-time injection route | Replace/add the concrete child widget instance instead |
| Runtime tab creation | Runtime `CreateWidget` graph generation is a separate capability | Prefer design-time WBP composition unless the TSpec explicitly requires runtime population |
| C++ parent class | Blueprint asset path is wrong for native parent | Use `/Script/Module.ClassName` |
| UObject refs | Stale asset registry can make refs null | Check `get_asset_properties`, then `reload_asset` if needed |
| `capture_widget_preview` | First capture can miss streamed textures | Treat first capture as warmup for heavy assets |
| Border tint capture | Border can render brighter/wrong in capture | Confirm through editor/PIE or export dump |
| `get_variables` | Returns BP user variables, not BindWidget children | Inspect widget tree for child widgets |

## Tool Blocker Handling

When an agent hits an MCP/tool limitation, do not keep prompting around it and
do not silently collapse the design into a worse structure.

1. Reduce the issue to a minimal repro: asset paths, tool call, expected result,
   actual result.
2. Classify it as one of: invalid TSpec, missing asset, MCPToolkit tool
   defect, Unreal editor state/cache issue, or unsupported design request.
3. Update this matrix when the behavior is confirmed.
4. Prefer a tool/plugin fix over a design fallback.
5. Use fallbacks such as inline tab content only when the TSpec records the
   deviation and the user accepts the loss of reuse.

For component-specific uncertainty, follow `UE_COMPONENT_PROTOCOL.md`: read the
recipe, read targeted UE source if needed, build a probe WBP, then update the
recipe/matrix before mutating production assets.
