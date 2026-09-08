# Explicit owned-PIE pointer and geometry fixture

Developer/editor acceptance utility, compiled with WITH_AUTOMATION_TESTS in the
existing native test source. It does not add a runtime game API or start PIE.
Run `MCT.OwnedPIEPointer` through editor_console_command only after preparing an
explicit owned session request. Do not use the normal Automation RunTests
worker, which resets an existing PIE session before test bodies.

Request: `Saved/Automation/MCPToolkit/OwnedPointerRequest.json`.
Result: `Saved/Automation/MCPToolkit/OwnedPointerResult.json`.

| Field | Contract |
|---|---|
| pid | Discover the current editor PID; must match this process. |
| created_unix | UTC Unix seconds; request expires after60seconds. |
| request_id | Unique, nonempty nonce; replay and overlapping sequences fail. |
| controller, hud | Exact objects in the same active PIE; HUD must own this controller. |
| widgets | Exact UWidgets inside this HUD to measure. |
| inspect_only | true reads geometry without input. |
| source, target | For a drag, exact constructed widgets inside this HUD. |

Coordinates and sizes come directly from UWidget.GetCachedGeometry. The pointer
uses the source/target centers, emits down/move/up over core-ticker frames and
records drag detection, release and continued world validity. The result echoes
request_id. A passing drag also requires caller-side verification of the intended
data change, not merely a delivered event. No OS foreground or hardware mouse
injection is required. No assets are mutated.

ProjectOkey's `Scripts/Tools/gameplay_shared_board_live.py native_probe drag
--production` produces the request and verifies that the source tile appears in
the previously empty target slot. Its inspect form records all layout rectangles.
The active production4:3 case passed;1080p before/after34-widget geometry is
identical and720p physical edge drift is documented with the existing DPI curve.
