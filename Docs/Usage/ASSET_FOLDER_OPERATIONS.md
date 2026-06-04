# Asset Folder Operations

Use `move_folder_assets` when an MCP workflow needs to move or copy every asset
under a Content Browser folder in one request.

## Tool

```python
move_folder_assets(
    source_folder: str,
    target_folder: str,
    operation: str = "move",
    recursive: bool = True,
    scope: str = "",
    dry_run: bool = False,
)
```

## Engine API Mapping

| `operation` | Unreal API | Behavior |
|---|---|---|
| `"move"` | `UEditorAssetSubsystem::RenameDirectory` | Moves all contained assets to the target folder. |
| `"copy"` | `UEditorAssetSubsystem::DuplicateDirectory` | Duplicates all contained assets to the target folder. |

This command does not move or copy `.uasset` files directly on disk. It uses
Unreal Editor asset APIs so the Asset Registry and loaded asset state stay in
sync with the operation.

## Safety Rules

- `source_folder` and `target_folder` must be Content Browser folder paths under
  `/Game`, for example `/Game/UI/Hud/SeatPlates`.
- The target folder must be empty. The command refuses to merge into existing
  target assets.
- `recursive` must be `True`; Unreal directory operations apply to contained
  assets recursively.
- Execution requires write scope. Use `dry_run=True` for a no-mutation scope
  check before running the operation.
- After a move, run `list_redirectors` and `fixup_redirectors` if the workflow
  needs explicit redirector cleanup.

## Example

```python
move_folder_assets(
    source_folder="/Game/UI/Hud/Players/SeatPlates",
    target_folder="/Game/UI/Hud/SeatPlates",
    operation="move",
    scope="write",
    dry_run=True,
)

move_folder_assets(
    source_folder="/Game/UI/Hud/Players/SeatPlates",
    target_folder="/Game/UI/Hud/SeatPlates",
    operation="move",
    scope="write",
)
```
