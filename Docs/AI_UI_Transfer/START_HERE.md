# START HERE: Reference Image -> Pencil -> UE 5.7

This workflow is agent-neutral. Codex, Gemini CLI, Kimi Code, or any other agent
should follow the same contract.

## Goal

Turn a reference image into a Pencil design and then into a UE 5.7 CommonUI/UMG
Widget Blueprint through deterministic contracts, not visual guessing inside
Unreal.

The agent may make creative decisions during reference analysis and Pencil
design. During Unreal build, the agent is only a TSpec executor.

## Required Reading

Before building or mutating any WBP:

1. `Docs/AI_SESSION_HANDOFF.md`
2. `Docs/AI_UI_Transfer/README.md`
3. `Docs/AI_UI_Transfer/START_HERE.md`
4. `Docs/CommonUI_Architecture.md`
5. `Docs/Reference/AI_REFERENCE.md`
6. `Docs/UI_TSpec/README.md`
7. `Docs/UI_TSpec/tspec.schema.json`
8. `Docs/AI_UI_Transfer/TOOL_CAPABILITY_MATRIX.md`
9. `Docs/AI_UI_Transfer/UE_COMPONENT_PROTOCOL.md`
10. `Docs/AI_UI_Transfer/component_recipes/README.md`

If a host project has stronger local instructions, follow those as project
overrides while preserving the TSpec-before-mutation rule.

## Five-Layer Pipeline

1. Reference analysis
2. Pencil design system
3. TSpec contract
4. UE build
5. Verification

## Reference Analysis

Create one analysis per screen or reusable flow:

```text
Docs/UI_ReferenceAnalysis/<screen>.reference_analysis.md
```

Use `Docs/AI_UI_Transfer/reference_analysis.template.md`.

A reference can contribute composition, hierarchy, rhythm, state behavior, and
color relationships. It must not contribute copied IP, exact trade dress, logos,
fonts, icons, or assets.

## Pencil Design System

In Pencil, prefer reusable components over one-off screen drawings:

- nav item
- card
- currency chip
- icon button
- modal
- tab
- list row
- action chip

If the current `.pen` file does not have reusable components yet, record that as
design-system debt in the TSpec. Do not hide it in session memory.

## TSpec Contract

Every screen needs a TSpec before Unreal mutation:

```text
Docs/UI_TSpec/<screen>.tspec.json
```

Use `Docs/UI_TSpec/templates/tspec.template.json` and validate with:

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/ValidateUITSpecs.ps1
```

When running from a host project that keeps specs elsewhere:

```powershell
powershell -ExecutionPolicy Bypass -File Plugins/MCPToolkit/Resources/Scripts/ValidateUITSpecs.ps1 -Root . -SpecDirectory Docs/Tasarim/UI_TSpecs
```

The TSpec must include:

- Pencil file and frame id
- UE WBP path and parent class
- CommonUI base/layer/input expectations
- root shell mode
- Pencil node id -> UE widget mapping
- slot properties
- style tokens/assets
- component state props
- capture matrix

## Mode Decision

| Screen type | Mode | Root shell |
|---|---|---|
| Splash, loading, static brand screen | `letterbox` | `Root_Overlay -> BG_Layer + Content_Scale -> SafeZone -> Content_Box` |
| Menu, shop, settings with sidebar/topbar/bottombar | `hybrid` | `Root_Overlay -> BG_Image + Content_SafeZone -> MainCanvas` |
| Gameplay HUD or in-game overlay | `adaptive` | `Root_Overlay -> BG_Layer + Content_SafeZone -> Adaptive_Canvas` |

## Unreal Build Rules

- Do not call `add_widget`, `set_canvas_slot_layout`, or related MCP mutation
  tools before TSpec validation.
- Use CommonUI parent classes according to the host project's architecture.
- For any unfamiliar or uncertain UE/UMG/CommonUI component, follow
  `UE_COMPONENT_PROTOCOL.md` before touching the production WBP.
- Build the WBP from TSpec topological order.
- For reusable Widget Blueprint children, use explicit asset/generated-class
  paths with `add_widget`.
- Compile once after the full tree and properties are applied.
- Always call `reload_asset` after compile.

## When A Tool Blocks The Build

Tool failures are part of the contract, not something to solve by visual
guessing. The agent must stop the current mutation path and record:

- the exact tool call and asset path
- expected result
- actual result
- whether the issue is TSpec, asset, MCPToolkit, editor cache, or unsupported
  UE behavior

Then prefer a MCPToolkit/tool fix or a TSpec correction. Fallbacks that
reduce structure require an explicit TSpec deviation.

## Verification

No final answer before:

- `compile_and_save`
- `reload_asset`
- `get_widget_tree` compared against TSpec
- `capture_widget_preview` using TSpec capture matrix
- Pencil screenshot comparison when available
- fidelity/progress log update in the host project
