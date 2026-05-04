# UI TSpecs

This folder contains machine-readable contracts for Pencil -> Unreal Widget
Blueprint transfers. Every production screen mutation needs one
`*.tspec.json` file before any WBP changes.

## Required Flow

1. Write or update reference analysis.
2. Read the Pencil frame.
3. Produce a TSpec.
4. Validate the TSpec.
5. Get user/project approval when required.
6. Mutate UE WBP only according to the TSpec.
7. Compile, reload, widget-tree diff, capture, and update the host project's
   fidelity/progress log.

## Validate

From this plugin repository:

```powershell
powershell -ExecutionPolicy Bypass -File Resources/Scripts/ValidateUITSpecs.ps1
```

From a host Unreal project:

```powershell
powershell -ExecutionPolicy Bypass -File Plugins/CommonAIExport/Resources/Scripts/ValidateUITSpecs.ps1 -Root . -SpecDirectory Docs/Tasarim/UI_TSpecs
```

## Mode Contract

| Mode | rootShell.type | Usage |
|---|---|---|
| `letterbox` | `letterbox-default` | Splash/loading/static screen |
| `hybrid` | `hybrid-adaptive` | Menu/shop/settings with sidebar/topbar/bottombar |
| `adaptive` | `adaptive-default` | Gameplay HUD and in-game overlay |

## Files

- `tspec.schema.json`: shared JSON schema.
- `templates/tspec.template.json`: starting template.
- `examples/`: examples only; not active production contracts.

